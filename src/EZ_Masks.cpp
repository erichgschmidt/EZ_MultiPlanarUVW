/*
    EZ_Masks.cpp
    3ds Max 2023 C++ SDK  —  OSM modifier  —  part of EZ_BoxTri.dlm

    Procedural per-vertex mask baker. Generalises the EZ BoxTri AO modifier:
    computes several cheap geometric "signals" in one pass and routes a chosen
    signal into each of R / G / B of a target map channel, with per-channel
    weight (over-crankable) and invert.

    Signals (all rayless, one shared 1-ring/edge pass):
        Cavity      concave recesses  (dirt / grime / occlusion)
        Convex      convex edges      (wear / chipping / rim)
        Curv Mag    |signed curvature| (detail / grunge)
        Up-Facing   dot(N, +Z)        (snow / dust on tops)
        Down-Facing dot(N, -Z)        (drips / sheltered undersides)
        Height      Z within bounds   (waterline / strata)
        Roughness   normal variance   (noisy vs smooth regions)

    Per channel: source signal -> pow(value, contrast) -> * weight -> optional
    invert (per-channel and/or global) -> optional clamp 0..1 (off = overcrank).
    Optional blur (neighbour mean, N iterations) softens per-vertex faceting.

    Cavity/Convex share one signed-curvature value (+ = concave, - = convex),
    matching the AO modifier's convention.
*/

#include "resource.h"

#include <max.h>
#include <iparamb2.h>
#include <iparamm2.h>
#include <modstack.h>
#include <object.h>
#include <mesh.h>
#include <triobj.h>
#include <istdplug.h>

#include <algorithm>
#include <cmath>
#include <vector>

extern HINSTANCE hInstance;  // defined in EZ_BoxTri.cpp

#define EZ_MASKS_CLASS_ID Class_ID(0x6d2f3a11, 0x1e0b7c70)

static const TCHAR* kMkName     = _T("EZ Procedural Masks");
static const TCHAR* kMkCategory = _T("EZ Tools");

// ---------------------------------------------------------------------------
// Signals
// ---------------------------------------------------------------------------
enum Signal
{
    Sig_None = 0,
    Sig_Cavity,      // 1
    Sig_Convex,      // 2
    Sig_CurvMag,     // 3
    Sig_UpFacing,    // 4
    Sig_DownFacing,  // 5
    Sig_Height,      // 6
    Sig_Roughness,   // 7
    Sig_RayAO,       // 8  (ray-traced occlusion; needs BVH)
    Sig_Count        // 9
};

static const TCHAR* kSignalLabels[Sig_Count] = {
    _T("None"), _T("Cavity"), _T("Convex"), _T("Curv Mag"),
    _T("Up-Facing"), _T("Down-Facing"), _T("Height"), _T("Roughness"),
    _T("Ray AO")
};

// ---------------------------------------------------------------------------
// Param IDs
// ---------------------------------------------------------------------------
enum MkParamBlockIDs { kMkPBlock = 0 };

enum MkParamIDs
{
    mk_targetCh = 0,
    mk_contrast,
    mk_blur,
    mk_invertAll,
    mk_overcrank,
    mk_preview,
    mk_srcR, mk_wR, mk_invR,
    mk_srcG, mk_wG, mk_invG,
    mk_srcB, mk_wB, mk_invB,
    mk_raySamples, mk_rayDist
};

// ---------------------------------------------------------------------------
// Reset-to-defaults helper (defined in EZ_BoxTri.cpp would differ TU; keep local)
// ---------------------------------------------------------------------------
static void MkResetPB2ToDefaults(IParamBlock2* pb)
{
    if (!pb) return;
    ParamBlockDesc2* d = pb->GetDesc();
    if (!d) return;
    const int n = d->Count();
    for (int i = 0; i < n; ++i)
    {
        const ParamID id = d->IndextoID(i);
        const ParamDef& pd = d->GetParamDef(id);
        switch (pd.type)
        {
        case TYPE_INT:
        case TYPE_BOOL:  pb->SetValue(id, 0, pd.def.i); break;
        case TYPE_FLOAT: pb->SetValue(id, 0, pd.def.f); break;
        default: break;
        }
    }
}

// ---------------------------------------------------------------------------
// ClassDesc2
// ---------------------------------------------------------------------------
class EZMasksClassDesc : public ClassDesc2
{
public:
    int          IsPublic()              override { return TRUE; }
    void*        Create(BOOL = FALSE)    override;
    const TCHAR* ClassName()             override { return kMkName; }
    const TCHAR* NonLocalizedClassName() override { return kMkName; }
    SClass_ID    SuperClassID()          override { return OSM_CLASS_ID; }
    Class_ID     ClassID()               override { return EZ_MASKS_CLASS_ID; }
    const TCHAR* Category()              override { return kMkCategory; }
    const TCHAR* InternalName()          override { return _T("EZProceduralMasks"); }
    HINSTANCE    HInstance()             override { return hInstance; }
};

static EZMasksClassDesc g_EZMasksDesc;
ClassDesc2* GetEZMasksDesc() { return &g_EZMasksDesc; }

// ---------------------------------------------------------------------------
// DlgProc — populate the 3 source comboboxes and sync selection to the pblock.
// ---------------------------------------------------------------------------
static const int kMkComboIDs[3] = { IDC_MK_COMBO_SRCR, IDC_MK_COMBO_SRCG, IDC_MK_COMBO_SRCB };
static const MkParamIDs kMkSrcParamIDs[3] = { mk_srcR, mk_srcG, mk_srcB };

class EZMasksDlgProc : public ParamMap2UserDlgProc
{
    static void FillCombo(HWND cb)
    {
        SendMessage(cb, CB_RESETCONTENT, 0, 0);
        for (int i = 0; i < Sig_Count; ++i)
            SendMessage(cb, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(kSignalLabels[i]));
    }
    static void Sync(HWND hDlg, int r, IParamMap2* map)
    {
        if (!map || !map->GetParamBlock()) return;
        HWND cb = GetDlgItem(hDlg, kMkComboIDs[r]);
        if (!cb) return;
        int val = 0;
        map->GetParamBlock()->GetValue((ParamID)kMkSrcParamIDs[r], 0, val, FOREVER);
        SendMessage(cb, CB_SETCURSEL, (WPARAM)std::clamp(val, 0, Sig_Count - 1), 0);
    }
public:
    INT_PTR DlgProc(TimeValue t, IParamMap2* map, HWND hWnd,
                    UINT msg, WPARAM wParam, LPARAM) override
    {
        switch (msg)
        {
        case WM_INITDIALOG:
            for (int r = 0; r < 3; ++r)
            {
                HWND cb = GetDlgItem(hWnd, kMkComboIDs[r]);
                if (cb) { FillCombo(cb); Sync(hWnd, r, map); }
            }
            break;
        case WM_COMMAND:
            if (HIWORD(wParam) == CBN_SELCHANGE)
            {
                const int id = LOWORD(wParam);
                for (int r = 0; r < 3; ++r)
                    if (id == kMkComboIDs[r])
                    {
                        int sel = (int)SendMessage(GetDlgItem(hWnd, id), CB_GETCURSEL, 0, 0);
                        if (sel >= 0 && map && map->GetParamBlock())
                            map->GetParamBlock()->SetValue((ParamID)kMkSrcParamIDs[r], t, sel);
                        return TRUE;
                    }
            }
            break;
        }
        return FALSE;
    }
    void DeleteThis() override {}
};
static EZMasksDlgProc g_MkDlgProc;

// ---------------------------------------------------------------------------
// Minimal BVH for ray-occlusion (ray-traced AO). Median centroid split,
// cached triangle edges, any-hit traversal with an explicit stack.
// ---------------------------------------------------------------------------
struct MkBVH
{
    struct Node { Box3 box; int left = -1, right = -1, start = 0, count = 0; };
    std::vector<Node>   nodes;
    std::vector<int>    tri;             // permuted triangle indices
    std::vector<Point3> v0, e1, e2;      // per-triangle (orig index): vert0 + edges

    void build(const Mesh& m)
    {
        const int nf = m.numFaces;
        v0.resize(nf); e1.resize(nf); e2.resize(nf);
        tri.resize(nf);
        std::vector<Point3> cent(nf);
        std::vector<Box3>   tbox(nf);
        for (int f = 0; f < nf; ++f)
        {
            const Face& fc = m.faces[f];
            const Point3 a = m.verts[fc.v[0]];
            const Point3 b = m.verts[fc.v[1]];
            const Point3 c = m.verts[fc.v[2]];
            v0[f] = a; e1[f] = b - a; e2[f] = c - a;
            Box3 bb; bb.Init(); bb += a; bb += b; bb += c;
            tbox[f] = bb; cent[f] = (a + b + c) / 3.0f; tri[f] = f;
        }
        nodes.clear();
        nodes.reserve((size_t)nf * 2 + 1);
        if (nf > 0) buildRange(0, nf, cent, tbox);
    }

    int buildRange(int start, int count, std::vector<Point3>& cent, std::vector<Box3>& tbox)
    {
        Node nd; nd.start = start; nd.count = count; nd.box.Init();
        for (int i = 0; i < count; ++i) nd.box += tbox[tri[start + i]];
        const int self = (int)nodes.size();
        nodes.push_back(nd);
        if (count <= 4) return self;                         // leaf (count > 0)

        const Point3 ext = nodes[self].box.Width();
        const int axis = (ext.x >= ext.y && ext.x >= ext.z) ? 0 : (ext.y >= ext.z ? 1 : 2);
        const int mid = start + count / 2;
        std::nth_element(tri.begin() + start, tri.begin() + mid, tri.begin() + start + count,
            [&](int a, int b) { return cent[a][axis] < cent[b][axis]; });
        const int l = buildRange(start, mid - start, cent, tbox);
        const int r = buildRange(mid, start + count - mid, cent, tbox);
        nodes[self].left = l; nodes[self].right = r; nodes[self].count = 0;  // internal
        return self;
    }

    static bool rayBox(const Point3& o, const Point3& invD, const Box3& bb, float tMax)
    {
        float t0 = 0.0f, t1 = tMax;
        for (int a = 0; a < 3; ++a)
        {
            float n = (bb.pmin[a] - o[a]) * invD[a];
            float f = (bb.pmax[a] - o[a]) * invD[a];
            if (n > f) std::swap(n, f);
            if (n > t0) t0 = n;
            if (f < t1) t1 = f;
            if (t0 > t1) return false;
        }
        return true;
    }

    bool triHit(int f, const Point3& o, const Point3& d, float tMin, float tMax) const
    {
        const Point3 p = d ^ e2[f];
        const float det = DotProd(e1[f], p);
        if (std::fabs(det) < 1e-9f) return false;
        const float inv = 1.0f / det;
        const Point3 tv = o - v0[f];
        const float u = DotProd(tv, p) * inv;
        if (u < 0.0f || u > 1.0f) return false;
        const Point3 q = tv ^ e1[f];
        const float vv = DotProd(d, q) * inv;
        if (vv < 0.0f || u + vv > 1.0f) return false;
        const float tt = DotProd(e2[f], q) * inv;
        return tt > tMin && tt < tMax;
    }

    bool occluded(const Point3& o, const Point3& d, float tMin, float tMax) const
    {
        if (nodes.empty()) return false;
        const Point3 invD(1.0f / (std::fabs(d.x) < 1e-9f ? 1e-9f : d.x),
                          1.0f / (std::fabs(d.y) < 1e-9f ? 1e-9f : d.y),
                          1.0f / (std::fabs(d.z) < 1e-9f ? 1e-9f : d.z));
        int stack[64]; int sp = 0; stack[sp++] = 0;
        while (sp > 0)
        {
            const Node& nd = nodes[stack[--sp]];
            if (!rayBox(o, invD, nd.box, tMax)) continue;
            if (nd.count > 0)
            {
                for (int i = 0; i < nd.count; ++i)
                    if (triHit(tri[nd.start + i], o, d, tMin, tMax)) return true;
            }
            else if (nd.left >= 0 && sp < 62)
            {
                stack[sp++] = nd.left; stack[sp++] = nd.right;
            }
        }
        return false;
    }
};

// van der Corput / radical inverse base 2 (stable low-discrepancy sampling)
static float MkRadicalInverse2(unsigned i)
{
    i = (i << 16) | (i >> 16);
    i = ((i & 0x55555555u) << 1) | ((i & 0xAAAAAAAAu) >> 1);
    i = ((i & 0x33333333u) << 2) | ((i & 0xCCCCCCCCu) >> 2);
    i = ((i & 0x0F0F0F0Fu) << 4) | ((i & 0xF0F0F0F0u) >> 4);
    i = ((i & 0x00FF00FFu) << 8) | ((i & 0xFF00FF00u) >> 8);
    return (float)i * 2.3283064365386963e-10f;  // / 2^32
}

// ---------------------------------------------------------------------------
// Modifier
// ---------------------------------------------------------------------------
class EZMasks : public Modifier
{
public:
    IParamBlock2* pblock = nullptr;

    EZMasks()  { g_EZMasksDesc.MakeAutoParamBlocks(this); MkResetPB2ToDefaults(pblock); }
    ~EZMasks() override = default;

    SClass_ID   SuperClassID() override { return OSM_CLASS_ID; }
    Class_ID    ClassID()      override { return EZ_MASKS_CLASS_ID; }
    void        DeleteThis()   override { delete this; }
    int         NumSubs()      override { return 1; }
    Animatable* SubAnim(int i) override { return i == 0 ? pblock : nullptr; }
    TSTR        SubAnimName(int i, bool = true) override
    { return i == 0 ? TSTR(_T("Parameters")) : TSTR(_T("")); }

    RefTargetHandle Clone(RemapDir& remap) override
    {
        EZMasks* c = new EZMasks();
        if (pblock) c->ReplaceReference(0, remap.CloneRef(pblock));
        BaseClone(this, c, remap);
        return c;
    }
    int             NumRefs()           override { return 1; }
    RefTargetHandle GetReference(int i) override { return i == 0 ? pblock : nullptr; }
    void SetReference(int i, RefTargetHandle r) override
    { if (i == 0) pblock = static_cast<IParamBlock2*>(r); }
    RefResult NotifyRefChanged(const Interval&, RefTargetHandle, PartID&,
                               RefMessage msg, BOOL) override
    {
        if (msg == REFMSG_CHANGE) NotifyDependents(FOREVER, PART_TEXMAP, REFMSG_CHANGE);
        return REF_SUCCEED;
    }

    int           NumParamBlocks()              override { return 1; }
    IParamBlock2* GetParamBlock(int i)          override { return i == 0 ? pblock : nullptr; }
    IParamBlock2* GetParamBlockByID(BlockID id) override { return id == kMkPBlock ? pblock : nullptr; }

    CreateMouseCallBack* GetCreateMouseCallBack() override { return nullptr; }
    const TCHAR* GetObjectName(bool) const override { return kMkName; }

    void BeginEditParams(IObjParam* ip, ULONG flags, Animatable* prev) override
    { g_EZMasksDesc.BeginEditParams(ip, this, flags, prev); }
    void EndEditParams(IObjParam* ip, ULONG flags, Animatable* next) override
    { g_EZMasksDesc.EndEditParams(ip, this, flags, next); }

    ChannelMask ChannelsUsed()    override { return GEOM_CHANNEL|TOPO_CHANNEL|TEXMAP_CHANNEL; }
    ChannelMask ChannelsChanged() override { return TEXMAP_CHANNEL; }
    Class_ID    InputType()       override { return Class_ID(TRIOBJ_CLASS_ID, 0); }

    Interval LocalValidity(TimeValue t) override
    {
        Interval v = FOREVER;
        if (pblock) pblock->GetValidity(t, v);
        return v;
    }

    void ModifyObject(TimeValue t, ModContext&, ObjectState* os, INode*) override
    {
        if (!os || !os->obj || !pblock) return;
        const Class_ID triID(TRIOBJ_CLASS_ID, 0);
        if (!os->obj->CanConvertToType(triID)) return;
        TriObject* tri = static_cast<TriObject*>(os->obj->ConvertToType(t, triID));
        if (!tri) return;
        if (os->obj != tri) os->obj = tri;
        Mesh& mesh = tri->GetMesh();
        if (mesh.numVerts <= 0 || mesh.numFaces <= 0) return;
        Apply(t, mesh);
        mesh.InvalidateGeomCache();   // texmap-only; do NOT invalidate topology
        tri->UpdateValidity(TEXMAP_CHAN_NUM, LocalValidity(t));
    }

private:
    static int   ClampCh(int ch) { return std::clamp(ch, 0, 99); }
    static float SafeDim(float v) { return std::fabs(v) < 1e-3f ? 1.0f : v; }
    static float Clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }

    float PBf(MkParamIDs id, TimeValue t, float def) const
    { float v = def; if (pblock) pblock->GetValue((ParamID)id, t, v, FOREVER); return v; }
    int   PBi(MkParamIDs id, TimeValue t, int   def) const
    { int   v = def; if (pblock) pblock->GetValue((ParamID)id, t, v, FOREVER); return v; }
    BOOL  PBb(MkParamIDs id, TimeValue t, BOOL  def) const
    { BOOL  v = def; if (pblock) pblock->GetValue((ParamID)id, t, v, FOREVER); return v; }

    static Point3 FaceNormal(const Mesh& mesh, int f)
    {
        const Face& fc = mesh.faces[f];
        const Point3 e1 = mesh.verts[fc.v[1]] - mesh.verts[fc.v[0]];
        const Point3 e2 = mesh.verts[fc.v[2]] - mesh.verts[fc.v[0]];
        const Point3 n  = e1 ^ e2;
        const float  L  = Length(n);
        return L > 1e-6f ? n / L : Point3(0,0,1);
    }

    // -----------------------------------------------------------------------
    // Map-channel prep (face-corner, matching the mapper; ch0 -> legacy vertCol)
    // -----------------------------------------------------------------------
    void EnsureChannel(Mesh& mesh, int ch) const
    {
        if (ch < 1 || ch > 99) return;
        const int needed = ch + 1;
        if (mesh.getNumMaps() < needed) mesh.setNumMaps(needed, TRUE);
        if (!mesh.mapSupport(ch)) mesh.setMapSupport(ch, TRUE);
    }

    // Ray-traced ambient occlusion, per vertex (1 = open, 0 = occluded).
    // Cosine-weighted hemisphere around the vertex normal; deterministic
    // low-discrepancy samples (no per-frame flicker). distFrac = max ray
    // length as a fraction of the bbox diagonal.
    static void ComputeRayAO(const Mesh& mesh, const std::vector<Point3>& vnUnit,
                             int samples, float distFrac, std::vector<float>& out)
    {
        const int nv = mesh.numVerts;
        out.assign(nv, 1.0f);
        if (mesh.numFaces <= 0) return;

        MkBVH bvh;
        bvh.build(mesh);

        Box3 bb; bb.Init();
        for (int i = 0; i < nv; ++i) bb += mesh.verts[i];
        const float diag = Length(bb.Max() - bb.Min());
        const float eps  = std::max(1e-5f, diag * 1e-4f);
        const float tMax = std::max(eps * 4.0f, diag * distFrac);
        const float invS = 1.0f / (float)samples;

        for (int v = 0; v < nv; ++v)
        {
            Point3 N = vnUnit[v];
            const float nl = Length(N);
            N = nl > 1e-6f ? N / nl : Point3(0, 0, 1);
            const Point3 T = (std::fabs(N.z) < 0.999f)
                                ? Normalize(N ^ Point3(0, 0, 1))
                                : Normalize(N ^ Point3(1, 0, 0));
            const Point3 Bx = N ^ T;
            const Point3 o  = mesh.verts[v] + N * eps;

            int occ = 0;
            for (int s = 0; s < samples; ++s)
            {
                const float u1 = (s + 0.5f) * invS;
                const float u2 = MkRadicalInverse2((unsigned)s);
                const float r  = std::sqrt(u1);
                const float th = 6.28318530718f * u2;
                const float x  = r * std::cos(th);
                const float y  = r * std::sin(th);
                const float z  = std::sqrt(std::max(0.0f, 1.0f - u1));
                const Point3 dir = T * x + Bx * y + N * z;
                if (bvh.occluded(o, dir, eps, tMax)) ++occ;
            }
            out[v] = 1.0f - (float)occ * invS;
        }
    }

    void Apply(TimeValue t, Mesh& mesh) const
    {
        const int   ch        = ClampCh(PBi(mk_targetCh, t, 12));
        const float contrast  = std::max(0.01f, PBf(mk_contrast, t, 1.0f));
        const int   blur      = std::clamp(PBi(mk_blur, t, 0), 0, 20);
        const bool  invertAll = PBb(mk_invertAll, t, FALSE) != FALSE;
        const bool  overcrank = PBb(mk_overcrank, t, FALSE) != FALSE;
        const bool  preview   = PBb(mk_preview,   t, FALSE) != FALSE;

        const int srcCh[3] = {
            std::clamp(PBi(mk_srcR, t, Sig_Cavity), 0, Sig_Count - 1),
            std::clamp(PBi(mk_srcG, t, Sig_Convex), 0, Sig_Count - 1),
            std::clamp(PBi(mk_srcB, t, Sig_Roughness), 0, Sig_Count - 1),
        };
        const float wCh[3]  = { PBf(mk_wR, t, 1.0f), PBf(mk_wG, t, 1.0f), PBf(mk_wB, t, 1.0f) };
        const bool  invCh[3]= { PBb(mk_invR, t, FALSE)!=FALSE, PBb(mk_invG, t, FALSE)!=FALSE, PBb(mk_invB, t, FALSE)!=FALSE };
        const int   raySamples = std::clamp(PBi(mk_raySamples, t, 16), 1, 256);
        const float rayDistFrac= std::clamp(PBf(mk_rayDist, t, 0.5f), 0.01f, 1.0f);

        const int nv = mesh.numVerts;

        // Bounds (height)
        Box3 b; b.Init();
        for (int i = 0; i < nv; ++i) b += mesh.verts[i];
        if (b.IsEmpty()) { b += Point3(0,0,0); b += Point3(1,1,1); }
        const Point3 mn = b.Min();
        const float zRange = SafeDim(b.Max().z - mn.z);

        // One pass: vertex normals (unit-sum + count for roughness), neighbour centroid
        std::vector<Point3> vnUnit(nv, Point3(0,0,0));  // sum of UNIT face normals (roughness)
        std::vector<int>    vnCnt (nv, 0);
        std::vector<Point3> nbrSum(nv, Point3(0,0,0));
        std::vector<int>    nbrCnt(nv, 0);
        auto faceValid = [nv](const Face& fc){ return fc.v[0]<(DWORD)nv && fc.v[1]<(DWORD)nv && fc.v[2]<(DWORD)nv; };

        for (int f = 0; f < mesh.numFaces; ++f)
        {
            const Face& fc = mesh.faces[f];
            if (!faceValid(fc)) continue;
            const Point3 fn = FaceNormal(mesh, f);
            for (int c = 0; c < 3; ++c)
            {
                const int v = fc.v[c];
                const int a = fc.v[(c + 1) % 3];
                const int d = fc.v[(c + 2) % 3];
                vnUnit[v] += fn; vnCnt[v] += 1;
                nbrSum[v] += mesh.verts[a] + mesh.verts[d];
                nbrCnt[v] += 2;
            }
        }

        // Ray-traced AO (only built if a channel asks for it — it's the
        // expensive one: builds a BVH and casts raySamples rays per vertex).
        std::vector<float> rayAO;
        const bool needRay = (srcCh[0] == Sig_RayAO || srcCh[1] == Sig_RayAO || srcCh[2] == Sig_RayAO);
        if (needRay)
            ComputeRayAO(mesh, vnUnit, raySamples, rayDistFrac, rayAO);

        // Per-signal value per vertex
        auto signalAt = [&](int v, int sig) -> float
        {
            Point3 n = vnUnit[v];
            const float nl = Length(n);
            n = nl > 1e-6f ? n / nl : Point3(0,0,1);

            float signedCurv = 0.0f;
            if (nbrCnt[v] > 0)
            {
                const Point3 centroid = nbrSum[v] / (float)nbrCnt[v];
                Point3 toC = centroid - mesh.verts[v];
                const float tl = Length(toC);
                if (tl > 1e-6f) signedCurv = DotProd(toC / tl, n); // + concave, - convex
            }
            switch (sig)
            {
            case Sig_Cavity:     return Clamp01(signedCurv);
            case Sig_Convex:     return Clamp01(-signedCurv);
            case Sig_CurvMag:    return Clamp01(std::fabs(signedCurv));
            case Sig_UpFacing:   return Clamp01(n.z);
            case Sig_DownFacing: return Clamp01(-n.z);
            case Sig_Height:     return Clamp01((mesh.verts[v].z - mn.z) / zRange);
            case Sig_Roughness:
            {
                if (vnCnt[v] <= 0) return 0.0f;
                const float coherence = Length(vnUnit[v]) / (float)vnCnt[v]; // 1=flat
                return Clamp01(1.0f - coherence);
            }
            case Sig_RayAO:
                return (v < (int)rayAO.size()) ? rayAO[v] : 1.0f;
            default: return 0.0f;
            }
        };

        // Compose per-vertex RGB
        std::vector<Point3> col((size_t)nv, Point3(0,0,0));
        for (int v = 0; v < nv; ++v)
        {
            float rgb[3];
            for (int ci = 0; ci < 3; ++ci)
            {
                float s = (srcCh[ci] == Sig_None) ? 0.0f : signalAt(v, srcCh[ci]);
                if (invCh[ci] ^ invertAll) s = 1.0f - s;
                float out = std::pow(Clamp01(s), contrast) * wCh[ci];
                if (!overcrank) out = Clamp01(out);
                rgb[ci] = out;
            }
            col[v] = Point3(rgb[0], rgb[1], rgb[2]);
        }

        // Optional blur (neighbour mean across the 1-ring) — softens faceting
        if (blur > 0)
        {
            // Build a simple per-vertex neighbour accumulation from faces each pass.
            std::vector<Point3> tmp(nv);
            std::vector<Point3> acc(nv);
            std::vector<int>    cnt(nv);
            for (int it = 0; it < blur; ++it)
            {
                std::fill(acc.begin(), acc.end(), Point3(0,0,0));
                std::fill(cnt.begin(), cnt.end(), 0);
                for (int f = 0; f < mesh.numFaces; ++f)
                {
                    const Face& fc = mesh.faces[f];
                    if (!faceValid(fc)) continue;
                    for (int c = 0; c < 3; ++c)
                    {
                        const int v = fc.v[c];
                        const int a = fc.v[(c + 1) % 3];
                        const int d = fc.v[(c + 2) % 3];
                        acc[v] += col[a] + col[d]; cnt[v] += 2;
                    }
                }
                for (int v = 0; v < nv; ++v)
                    tmp[v] = (cnt[v] > 0) ? (col[v] + acc[v]) / (float)(cnt[v] + 1) : col[v];
                col.swap(tmp);
            }
        }

        // ---- Write the target channel, FACE-CORNER (3 cverts/face) ----
        // Face-corner matches the mapper and avoids the welded/face-corner
        // restructure that corrupts ch0 vertex colour when stacked.
        if (ch == 0)
        {
            const int ncv = mesh.numFaces * 3;
            mesh.setNumVertCol(ncv, FALSE);
            mesh.setNumVCFaces(mesh.numFaces, FALSE);
            if (!mesh.vcFace || !mesh.vertCol) return;
            for (int f = 0; f < mesh.numFaces; ++f)
            {
                const Face& fc = mesh.faces[f];
                for (int c = 0; c < 3; ++c)
                {
                    const int corner = f * 3 + c;
                    mesh.vcFace[f].t[c] = (DWORD)corner;
                    const DWORD vv = fc.v[c];
                    mesh.vertCol[corner] = (vv < (DWORD)nv) ? col[vv] : Point3(0,0,0);
                }
            }
            return;
        }

        EnsureChannel(mesh, ch);
        const int ncv = mesh.numFaces * 3;
        mesh.setNumMapVerts(ch, ncv, FALSE);
        mesh.setNumMapFaces(ch, mesh.numFaces, FALSE);
        MeshMap& map = mesh.Map(ch);
        if (!map.tf || !map.tv || map.fnum < mesh.numFaces || map.vnum < ncv) return;
        for (int f = 0; f < mesh.numFaces; ++f)
        {
            const Face& fc = mesh.faces[f];
            for (int c = 0; c < 3; ++c)
            {
                const int corner = f * 3 + c;
                map.tf[f].t[c] = (DWORD)corner;
                const DWORD vv = fc.v[c];
                map.tv[corner] = (vv < (DWORD)nv) ? col[vv] : Point3(0,0,0);
            }
        }

        // Optional preview: mirror to vertex colour ch0 (face-corner) so it can
        // be shown in the viewport. Does not touch node display state.
        if (preview && ch != 0)
        {
            const int ncv2 = mesh.numFaces * 3;
            mesh.setNumVertCol(ncv2, FALSE);
            mesh.setNumVCFaces(mesh.numFaces, FALSE);
            if (mesh.vcFace && mesh.vertCol)
                for (int f = 0; f < mesh.numFaces; ++f)
                {
                    const Face& fc = mesh.faces[f];
                    for (int c = 0; c < 3; ++c)
                    {
                        const int corner = f * 3 + c;
                        mesh.vcFace[f].t[c] = (DWORD)corner;
                        const DWORD vv = fc.v[c];
                        mesh.vertCol[corner] = (vv < (DWORD)nv) ? col[vv] : Point3(0,0,0);
                    }
                }
        }
    }
};

void* EZMasksClassDesc::Create(BOOL) { return new EZMasks(); }

// ---------------------------------------------------------------------------
// ParamBlockDesc2
// ---------------------------------------------------------------------------
#define MK_CHANNEL_PARAMS(LET, defSrc, srcStr, wStr, invStr, comboID, wEdit, wSpin, invChk) \
    mk_src##LET, _T("src" #LET), TYPE_INT, P_ANIMATABLE, srcStr,                            \
        p_default, defSrc, p_range, 0, Sig_Count - 1,                                       \
    p_end,                                                                                  \
    mk_w##LET,   _T("w"   #LET), TYPE_FLOAT, P_ANIMATABLE, wStr,                            \
        p_default, 1.0f, p_range, 0.0f, 8.0f,                                               \
        p_ui, TYPE_SPINNER, EDITTYPE_FLOAT, wEdit, wSpin, SPIN_AUTOSCALE,                   \
    p_end,                                                                                  \
    mk_inv##LET, _T("inv" #LET), TYPE_BOOL, P_ANIMATABLE, invStr,                           \
        p_default, FALSE, p_ui, TYPE_SINGLECHEKBOX, invChk,                                 \
    p_end,

static ParamBlockDesc2 g_MkPBlock
(
    kMkPBlock, _T("params"), IDS_MK_PARAMS, &g_EZMasksDesc,
    P_AUTO_CONSTRUCT | P_AUTO_UI,
    0,
    IDD_PANEL_MASKS, IDS_MK_PARAMS, 0, 0, &g_MkDlgProc,

    mk_targetCh, _T("targetChannel"), TYPE_INT, P_ANIMATABLE, IDS_MK_TARGETCH,
        p_default, 12, p_range, 0, 99,
        p_ui, TYPE_SPINNER, EDITTYPE_INT, IDC_MK_EDIT_TARGETCH, IDC_MK_SPIN_TARGETCH, SPIN_AUTOSCALE,
    p_end,
    mk_contrast, _T("contrast"), TYPE_FLOAT, P_ANIMATABLE, IDS_MK_CONTRAST,
        p_default, 1.0f, p_range, 0.1f, 8.0f,
        p_ui, TYPE_SPINNER, EDITTYPE_FLOAT, IDC_MK_EDIT_CONTRAST, IDC_MK_SPIN_CONTRAST, SPIN_AUTOSCALE,
    p_end,
    mk_blur, _T("blur"), TYPE_INT, P_ANIMATABLE, IDS_MK_BLUR,
        p_default, 0, p_range, 0, 20,
        p_ui, TYPE_SPINNER, EDITTYPE_INT, IDC_MK_EDIT_BLUR, IDC_MK_SPIN_BLUR, SPIN_AUTOSCALE,
    p_end,
    mk_invertAll, _T("invertAll"), TYPE_BOOL, P_ANIMATABLE, IDS_MK_INVERTALL,
        p_default, FALSE, p_ui, TYPE_SINGLECHEKBOX, IDC_MK_CHK_INVERTALL,
    p_end,
    mk_overcrank, _T("overcrank"), TYPE_BOOL, P_ANIMATABLE, IDS_MK_OVERCRANK,
        p_default, FALSE, p_ui, TYPE_SINGLECHEKBOX, IDC_MK_CHK_OVERCRANK,
    p_end,
    mk_preview, _T("preview"), TYPE_BOOL, P_ANIMATABLE, IDS_MK_PREVIEW,
        p_default, FALSE, p_ui, TYPE_SINGLECHEKBOX, IDC_MK_CHK_PREVIEW,
    p_end,

    MK_CHANNEL_PARAMS(R, Sig_Cavity,    IDS_MK_SRCR, IDS_MK_WR, IDS_MK_INVR, IDC_MK_COMBO_SRCR, IDC_MK_EDIT_WR, IDC_MK_SPIN_WR, IDC_MK_CHK_INVR)
    MK_CHANNEL_PARAMS(G, Sig_Convex,    IDS_MK_SRCG, IDS_MK_WG, IDS_MK_INVG, IDC_MK_COMBO_SRCG, IDC_MK_EDIT_WG, IDC_MK_SPIN_WG, IDC_MK_CHK_INVG)
    MK_CHANNEL_PARAMS(B, Sig_Roughness, IDS_MK_SRCB, IDS_MK_WB, IDS_MK_INVB, IDC_MK_COMBO_SRCB, IDC_MK_EDIT_WB, IDC_MK_SPIN_WB, IDC_MK_CHK_INVB)

    mk_raySamples, _T("raySamples"), TYPE_INT, P_ANIMATABLE, IDS_MK_RAYSAMP,
        p_default, 16, p_range, 1, 256,
        p_ui, TYPE_SPINNER, EDITTYPE_INT, IDC_MK_EDIT_RAYSAMP, IDC_MK_SPIN_RAYSAMP, SPIN_AUTOSCALE,
    p_end,
    mk_rayDist, _T("rayDist"), TYPE_FLOAT, P_ANIMATABLE, IDS_MK_RAYDIST,
        p_default, 0.5f, p_range, 0.01f, 1.0f,
        p_ui, TYPE_SPINNER, EDITTYPE_FLOAT, IDC_MK_EDIT_RAYDIST, IDC_MK_SPIN_RAYDIST, SPIN_AUTOSCALE,
    p_end,

    p_end
);
