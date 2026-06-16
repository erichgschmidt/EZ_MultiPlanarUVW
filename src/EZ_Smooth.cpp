/*
    EZ_Smooth.cpp
    3ds Max 2023 C++ SDK  —  OSM modifier  —  part of EZ_BoxTri.dlm

    Procedurally rewrites face smoothing groups from geometry.

    Pipeline:
        1. Per-face normals.
        2. Classify each interior edge soft / hard:
             - invisible (quad-diagonal) edges are ALWAYS soft.
             - hard if dihedral angle > Angle Threshold, optionally biased by
               local curvature (Curvature Weight) so subtle creases in busy
               areas still break.
             - Respect Existing Hard Edges: an edge already creased in the
               input (its two faces share no smoothing bit) stays hard.
        3. Union-find: flood-fill soft-connected faces into smooth regions.
        4. Min Island: merge regions smaller than the threshold into a neighbour.
        5. Graph-colour regions into the 32 group bits (lowest free bit not used
           by a hard-adjacent neighbour) — this is how it stays <= 32 groups via
           bit reuse. Each face gets one bit = 1 << colour.

    Smoothing groups live in the TOPO channel. Explicit/edited normals silently
    override smoothing groups, so by default we clear them (toggle off to keep).
*/

#include "resource.h"

#include <max.h>
#include <iparamb2.h>
#include <modstack.h>
#include <object.h>
#include <mesh.h>
#include <meshadj.h>
#include <triobj.h>
#include <istdplug.h>

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

extern HINSTANCE hInstance;  // defined in EZ_BoxTri.cpp

#define EZ_SMOOTH_CLASS_ID Class_ID(0x6d2f3a11, 0x1e0b7c80)

static const TCHAR* kSmName     = _T("EZ Smoothing Groups");
static const TCHAR* kSmCategory = _T("EZ Tools");

enum SmParamBlockIDs { kSmPBlock = 0 };

enum SmParamIDs
{
    sm_angle = 0,
    sm_curvWeight,
    sm_minIsland,
    sm_respectHard,
    sm_selOnly,
    sm_clearNormals
};

// ---------------------------------------------------------------------------
static void SmResetPB2ToDefaults(IParamBlock2* pb)
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
class EZSmoothClassDesc : public ClassDesc2
{
public:
    int          IsPublic()              override { return TRUE; }
    void*        Create(BOOL = FALSE)    override;
    const TCHAR* ClassName()             override { return kSmName; }
    const TCHAR* NonLocalizedClassName() override { return kSmName; }
    SClass_ID    SuperClassID()          override { return OSM_CLASS_ID; }
    Class_ID     ClassID()               override { return EZ_SMOOTH_CLASS_ID; }
    const TCHAR* Category()              override { return kSmCategory; }
    const TCHAR* InternalName()          override { return _T("EZSmoothingGroups"); }
    HINSTANCE    HInstance()             override { return hInstance; }
};

static EZSmoothClassDesc g_EZSmoothDesc;
ClassDesc2* GetEZSmoothDesc() { return &g_EZSmoothDesc; }

// ---------------------------------------------------------------------------
// Modifier
// ---------------------------------------------------------------------------
class EZSmooth : public Modifier
{
public:
    IParamBlock2* pblock = nullptr;

    EZSmooth()  { g_EZSmoothDesc.MakeAutoParamBlocks(this); SmResetPB2ToDefaults(pblock); }
    ~EZSmooth() override = default;

    SClass_ID   SuperClassID() override { return OSM_CLASS_ID; }
    Class_ID    ClassID()      override { return EZ_SMOOTH_CLASS_ID; }
    void        DeleteThis()   override { delete this; }
    int         NumSubs()      override { return 1; }
    Animatable* SubAnim(int i) override { return i == 0 ? pblock : nullptr; }
    TSTR        SubAnimName(int i, bool = true) override
    { return i == 0 ? TSTR(_T("Parameters")) : TSTR(_T("")); }

    RefTargetHandle Clone(RemapDir& remap) override
    {
        EZSmooth* c = new EZSmooth();
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
        if (msg == REFMSG_CHANGE) NotifyDependents(FOREVER, PART_TOPO, REFMSG_CHANGE);
        return REF_SUCCEED;
    }

    int           NumParamBlocks()              override { return 1; }
    IParamBlock2* GetParamBlock(int i)          override { return i == 0 ? pblock : nullptr; }
    IParamBlock2* GetParamBlockByID(BlockID id) override { return id == kSmPBlock ? pblock : nullptr; }

    CreateMouseCallBack* GetCreateMouseCallBack() override { return nullptr; }
    const TCHAR* GetObjectName(bool) const override { return kSmName; }

    void BeginEditParams(IObjParam* ip, ULONG flags, Animatable* prev) override
    { g_EZSmoothDesc.BeginEditParams(ip, this, flags, prev); }
    void EndEditParams(IObjParam* ip, ULONG flags, Animatable* next) override
    { g_EZSmoothDesc.EndEditParams(ip, this, flags, next); }

    ChannelMask ChannelsUsed()    override { return TOPO_CHANNEL|GEOM_CHANNEL; }
    ChannelMask ChannelsChanged() override { return TOPO_CHANNEL; }
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
        mesh.InvalidateTopologyCache();   // smGroup is TOPO
        mesh.InvalidateGeomCache();       // rebuild render normals
        tri->UpdateValidity(TOPO_CHAN_NUM, LocalValidity(t));
        tri->UpdateValidity(GEOM_CHAN_NUM, LocalValidity(t));
    }

private:
    float PBf(SmParamIDs id, TimeValue t, float def) const
    { float v = def; if (pblock) pblock->GetValue((ParamID)id, t, v, FOREVER); return v; }
    int   PBi(SmParamIDs id, TimeValue t, int   def) const
    { int   v = def; if (pblock) pblock->GetValue((ParamID)id, t, v, FOREVER); return v; }
    BOOL  PBb(SmParamIDs id, TimeValue t, BOOL  def) const
    { BOOL  v = def; if (pblock) pblock->GetValue((ParamID)id, t, v, FOREVER); return v; }

    static Point3 FaceNormal(const Mesh& m, int f)
    {
        const Face& fc = m.faces[f];
        const Point3 e1 = m.verts[fc.v[1]] - m.verts[fc.v[0]];
        const Point3 e2 = m.verts[fc.v[2]] - m.verts[fc.v[0]];
        const Point3 n  = e1 ^ e2;
        const float  L  = Length(n);
        return L > 1e-6f ? n / L : Point3(0,0,1);
    }

    // simple union-find
    static int Find(std::vector<int>& p, int x)
    {
        while (p[x] != x) { p[x] = p[p[x]]; x = p[x]; }
        return x;
    }
    static void Union(std::vector<int>& p, int a, int b)
    {
        a = Find(p, a); b = Find(p, b);
        if (a != b) p[a] = b;
    }

    void Apply(TimeValue t, Mesh& mesh) const
    {
        const float angleDeg   = std::clamp(PBf(sm_angle, t, 45.0f), 0.0f, 180.0f);
        const float curvWeight = std::clamp(PBf(sm_curvWeight, t, 0.0f), 0.0f, 1.0f);
        const int   minIsland  = std::max(1, PBi(sm_minIsland, t, 1));
        const bool  respect    = PBb(sm_respectHard, t, TRUE) != FALSE;
        const bool  selOnly    = PBb(sm_selOnly, t, FALSE) != FALSE;
        const bool  clearNorm  = PBb(sm_clearNormals, t, TRUE) != FALSE;

        const int nf = mesh.numFaces;
        const int nv = mesh.numVerts;
        const float cosThr = std::cos(angleDeg * 3.14159265359f / 180.0f);
        const float sThr   = (1.0f - cosThr) * 0.5f;   // sharpness at threshold

        auto faceValid = [nv](const Face& fc){ return fc.v[0]<(DWORD)nv && fc.v[1]<(DWORD)nv && fc.v[2]<(DWORD)nv; };

        // Face normals
        std::vector<Point3> Nf((size_t)nf);
        for (int f = 0; f < nf; ++f) Nf[f] = FaceNormal(mesh, f);

        // Per-vertex curvature magnitude (only if used)
        std::vector<float> curvMag;
        if (curvWeight > 0.0f)
        {
            std::vector<Point3> vn(nv, Point3(0,0,0)), nbrSum(nv, Point3(0,0,0));
            std::vector<int>    nbrCnt(nv, 0);
            for (int f = 0; f < nf; ++f)
            {
                const Face& fc = mesh.faces[f];
                if (!faceValid(fc)) continue;
                for (int c = 0; c < 3; ++c)
                {
                    const int v = fc.v[c], a = fc.v[(c+1)%3], d = fc.v[(c+2)%3];
                    vn[v] += Nf[f];
                    nbrSum[v] += mesh.verts[a] + mesh.verts[d];
                    nbrCnt[v] += 2;
                }
            }
            curvMag.assign(nv, 0.0f);
            for (int v = 0; v < nv; ++v)
            {
                Point3 n = vn[v]; const float nl = Length(n);
                n = nl > 1e-6f ? n / nl : Point3(0,0,1);
                if (nbrCnt[v] > 0)
                {
                    Point3 toC = nbrSum[v] / (float)nbrCnt[v] - mesh.verts[v];
                    const float tl = Length(toC);
                    if (tl > 1e-6f) curvMag[v] = std::min(1.0f, std::fabs(DotProd(toC/tl, n)));
                }
            }
        }

        // Existing smoothing groups (for "respect existing hard edges")
        std::vector<DWORD> oldSG;
        if (respect)
        {
            oldSG.resize(nf);
            for (int f = 0; f < nf; ++f) oldSG[f] = mesh.faces[f].getSmGroup();
        }

        // Build interior edges. key = minV * 2^32 + maxV.
        struct EdgeRec { int f0 = -1, f1 = -1; bool vis = true; };
        std::unordered_map<int64_t, EdgeRec> edges;
        edges.reserve((size_t)nf * 2);
        for (int f = 0; f < nf; ++f)
        {
            const Face& fc = mesh.faces[f];
            if (!faceValid(fc)) continue;
            for (int e = 0; e < 3; ++e)
            {
                int va = fc.v[e], vb = fc.v[(e+1)%3];
                const bool vis = fc.getEdgeVis(e) != 0;
                if (va > vb) std::swap(va, vb);
                const int64_t key = (int64_t)va * 0x100000000LL + (int64_t)vb;
                EdgeRec& r = edges[key];
                if (r.f0 < 0) { r.f0 = f; r.vis = vis; }
                else          { r.f1 = f; r.vis = r.vis && vis; }  // invisible if either side hidden
            }
        }

        // Classify + union soft edges; collect interior edges for graph/min-island
        std::vector<int> parent((size_t)nf);
        for (int f = 0; f < nf; ++f) parent[f] = f;

        struct IEdge { int a, b; bool hard; };
        std::vector<IEdge> iedges;
        iedges.reserve(edges.size());

        for (auto& kv : edges)
        {
            const EdgeRec& r = kv.second;
            if (r.f1 < 0) continue;              // open boundary edge
            const int A = r.f0, B = r.f1;

            bool hard;
            if (!r.vis)
            {
                hard = false;                    // quad diagonal: always soft
            }
            else
            {
                const float c = DotProd(Nf[A], Nf[B]);
                float score = (1.0f - c) * 0.5f;
                if (curvWeight > 0.0f && nv > 0)
                {
                    // average endpoint curvature of the shared edge
                    int va = mesh.faces[A].v[0], vb = mesh.faces[A].v[1];
                    // find the two shared verts between A and B
                    // (cheap: use the edge key back-decoded)
                    const int64_t key = kv.first;
                    va = (int)(key >> 32); vb = (int)(key & 0xffffffff);
                    const float ec = 0.5f * (curvMag[va] + curvMag[vb]);
                    score += curvWeight * ec;
                }
                hard = score > sThr;
            }

            if (respect && !hard)
            {
                // edge already creased in the input -> keep it hard
                const DWORD sgA = oldSG[A], sgB = oldSG[B];
                if (sgA != 0 && sgB != 0 && (sgA & sgB) == 0) hard = true;
            }

            if (!hard) Union(parent, A, B);
            iedges.push_back({ A, B, hard });
        }

        // Min-island merge: merge small regions into a neighbour across an edge.
        if (minIsland > 1)
        {
            for (int pass = 0; pass < 3; ++pass)
            {
                std::unordered_map<int,int> size;
                for (int f = 0; f < nf; ++f) size[Find(parent, f)]++;
                bool changed = false;
                for (const IEdge& ie : iedges)
                {
                    const int ra = Find(parent, ie.a), rb = Find(parent, ie.b);
                    if (ra == rb) continue;
                    if (size[ra] < minIsland || size[rb] < minIsland)
                    {
                        Union(parent, ie.a, ie.b);
                        changed = true;
                    }
                }
                if (!changed) break;
            }
        }

        // Graph-colour regions into <=32 bits.
        // Adjacency = regions separated by a still-hard edge.
        std::unordered_map<int,int> regIdx;          // root -> dense index
        auto idxOf = [&](int root) -> int
        {
            auto it = regIdx.find(root);
            if (it != regIdx.end()) return it->second;
            const int id = (int)regIdx.size();
            regIdx[root] = id; return id;
        };
        for (int f = 0; f < nf; ++f) idxOf(Find(parent, f));
        const int nreg = (int)regIdx.size();

        std::vector<std::vector<int>> adj((size_t)nreg);
        std::vector<int> regSize((size_t)nreg, 0);
        for (int f = 0; f < nf; ++f) regSize[idxOf(Find(parent, f))]++;
        for (const IEdge& ie : iedges)
        {
            if (!ie.hard) continue;
            const int ra = idxOf(Find(parent, ie.a));
            const int rb = idxOf(Find(parent, ie.b));
            if (ra != rb) { adj[ra].push_back(rb); adj[rb].push_back(ra); }
        }

        // process largest regions first for stable colouring
        std::vector<int> order((size_t)nreg);
        for (int i = 0; i < nreg; ++i) order[i] = i;
        std::sort(order.begin(), order.end(), [&](int a, int b){ return regSize[a] > regSize[b]; });

        std::vector<int> colorBit((size_t)nreg, -1);
        for (int oi = 0; oi < nreg; ++oi)
        {
            const int rg = order[oi];
            unsigned used = 0;
            for (int nb : adj[rg]) if (colorBit[nb] >= 0) used |= (1u << colorBit[nb]);
            int bit = 0;
            while (bit < 32 && (used & (1u << bit))) ++bit;
            colorBit[rg] = (bit < 32) ? bit : 0;   // fallback: reuse bit 0
        }

        // Write smGroup per face (gated by selection).
        for (int f = 0; f < nf; ++f)
        {
            if (selOnly && !mesh.faceSel[f]) continue;
            const int rg = idxOf(Find(parent, f));
            mesh.faces[f].setSmGroup(1u << colorBit[rg]);
        }

        // Explicit/edited normals override smoothing groups -> clear so SG wins.
        if (clearNorm)
            mesh.ClearSpecifiedNormals();
    }
};

void* EZSmoothClassDesc::Create(BOOL) { return new EZSmooth(); }

// ---------------------------------------------------------------------------
// ParamBlockDesc2
// ---------------------------------------------------------------------------
static ParamBlockDesc2 g_SmPBlock
(
    kSmPBlock, _T("params"), IDS_SM_PARAMS, &g_EZSmoothDesc,
    P_AUTO_CONSTRUCT | P_AUTO_UI,
    0,
    IDD_PANEL_SMOOTH, IDS_SM_PARAMS, 0, 0, nullptr,

    sm_angle, _T("angle"), TYPE_FLOAT, P_ANIMATABLE, IDS_SM_ANGLE,
        p_default, 45.0f, p_range, 0.0f, 180.0f,
        p_ui, TYPE_SPINNER, EDITTYPE_FLOAT, IDC_SM_EDIT_ANGLE, IDC_SM_SPIN_ANGLE, SPIN_AUTOSCALE,
    p_end,
    sm_curvWeight, _T("curvWeight"), TYPE_FLOAT, P_ANIMATABLE, IDS_SM_CURVWT,
        p_default, 0.0f, p_range, 0.0f, 1.0f,
        p_ui, TYPE_SPINNER, EDITTYPE_FLOAT, IDC_SM_EDIT_CURVWT, IDC_SM_SPIN_CURVWT, SPIN_AUTOSCALE,
    p_end,
    sm_minIsland, _T("minIsland"), TYPE_INT, P_ANIMATABLE, IDS_SM_MINISLAND,
        p_default, 1, p_range, 1, 9999,
        p_ui, TYPE_SPINNER, EDITTYPE_INT, IDC_SM_EDIT_MINISLAND, IDC_SM_SPIN_MINISLAND, SPIN_AUTOSCALE,
    p_end,
    sm_respectHard, _T("respectHard"), TYPE_BOOL, P_ANIMATABLE, IDS_SM_RESPECT,
        p_default, TRUE, p_ui, TYPE_SINGLECHEKBOX, IDC_SM_CHK_RESPECT,
    p_end,
    sm_selOnly, _T("selectionOnly"), TYPE_BOOL, P_ANIMATABLE, IDS_SM_SELONLY,
        p_default, FALSE, p_ui, TYPE_SINGLECHEKBOX, IDC_SM_CHK_SELONLY,
    p_end,
    sm_clearNormals, _T("clearNormals"), TYPE_BOOL, P_ANIMATABLE, IDS_SM_CLEARNORM,
        p_default, TRUE, p_ui, TYPE_SINGLECHEKBOX, IDC_SM_CHK_CLEARNORM,
    p_end,

    p_end
);
