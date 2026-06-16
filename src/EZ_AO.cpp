/*
    EZ_AO.cpp
    3ds Max 2023 C++ SDK  —  OSM modifier  —  part of EZ_BoxTri.dlm

    Standalone pseudo-AO modifier.  Computes a per-vertex ambient-occlusion
    approximation and writes it to a map channel (default 11).

    Why it's separate from EZ BoxTri:
        AO must reflect the FINAL geometry, so it should run AFTER any
        displacement.  UVs / blend weights want the clean base mesh.  Keeping
        AO as its own modifier lets the artist order the stack:

            [ EZ BoxTri ]   UVs + blend on clean geo
            [ Displace  ]   changes geometry
            [ EZ BoxTri AO ] recomputes ch11 on the displaced surface

    AO value (per vertex), 1 = lit / white, 0 = occluded / black:
        cavity  — concavity from mesh connectivity (offset to neighbour
                  centroid vs. smoothed vertex normal; +N = dip = occluded)
        height  — lower within the object's Z bounds darkens
        down    — down-facing (-Z) darkens
        ao = 1 - clamp((wCav*cav + wHgt*hgt + wDwn*dwn) * strength)

    Storage:  red-only (R=ao, G=B=0) by default, or grayscale (R=G=B=ao).
    Written per-vertex (welded) for a smooth gradient.
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

// hInstance is defined in EZ_BoxTri.cpp (this TU shares the same DLL).
extern HINSTANCE hInstance;

#define EZ_BOXTRI_AO_CLASS_ID Class_ID(0x6d2f3a11, 0x1e0b7c60)

static const TCHAR* kAOName     = _T("EZ BoxTri AO");
static const TCHAR* kAOCategory = _T("EZ Tools");

// ---------------------------------------------------------------------------
// Crash diagnostic logger. Keep the call sites as cheap no-ops so targeted
// logging can be re-enabled quickly if another stack-order issue appears.
// ---------------------------------------------------------------------------
#define kAODebugLog 0
static void AOLog(const char* msg)
{
#if kAODebugLog
    FILE* f = fopen("C:\\Users\\Gus\\Documents\\maxdev_plugins\\ez_ao_log.txt", "a");
    if (f) { fputs(msg, f); fputc('\n', f); fclose(f); }
#else
    (void)msg;
#endif
}
static void AOLogI(const char* fmt, int a, int b = 0)
{
#if kAODebugLog
    char buf[256]; _snprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, a, b);
    AOLog(buf);
#else
    (void)fmt; (void)a; (void)b;
#endif
}

enum AOParamBlockIDs { kAOPBlock = 0 };

enum AOParamIDs
{
    pb_ao_ch = 0,
    pb_ao_cavity,
    pb_ao_height,
    pb_ao_down,
    pb_ao_strength,
    pb_ao_gray,
    pb_ao_preview,
    pb_ao_invert,
    pb_ao_convex,
    pb_ao_curvMag,
    pb_ao_upFacing,
    pb_ao_roughness,
    pb_ao_combine
};

// How this modifier's AO combines with whatever is already in the channel.
enum AOCombine
{
    Comb_Replace = 0,   // overwrite (default)
    Comb_Multiply,      // existing * new  (layer / accumulate occlusion)
    Comb_Min,           // darkest wins
    Comb_Max,           // lightest wins
    Comb_Add,           // existing + new
    Comb_Subtract,      // existing - new  (carve out)
    Comb_Count
};
static const TCHAR* kAOCombineLabels[Comb_Count] = {
    _T("Replace"), _T("Multiply"), _T("Min"), _T("Max"), _T("Add"), _T("Subtract")
};

// ---------------------------------------------------------------------------
// Reset every param in a block to its descriptor default. Called from the
// modifier constructor so a freshly-applied modifier always starts at defaults
// (clones and scene loads overwrite these afterwards, so they keep their values).
// Handles the int/bool/float param types these modifiers use.
// ---------------------------------------------------------------------------
static void ResetPB2ToDefaults(IParamBlock2* pb)
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

class EZBoxTriAOClassDesc : public ClassDesc2
{
public:
    int          IsPublic()              override { return TRUE; }
    void*        Create(BOOL = FALSE)    override;
    const TCHAR* ClassName()             override { return kAOName; }
    const TCHAR* NonLocalizedClassName() override { return kAOName; }
    SClass_ID    SuperClassID()          override { return OSM_CLASS_ID; }
    Class_ID     ClassID()               override { return EZ_BOXTRI_AO_CLASS_ID; }
    const TCHAR* Category()              override { return kAOCategory; }
    const TCHAR* InternalName()          override { return _T("EZBoxTriAO"); }
    HINSTANCE    HInstance()             override { return hInstance; }
};

static EZBoxTriAOClassDesc g_EZBoxTriAODesc;
ClassDesc2* GetEZBoxTriAODesc() { return &g_EZBoxTriAODesc; }

// ---------------------------------------------------------------------------
// Modifier
// ---------------------------------------------------------------------------

class EZBoxTriAO : public Modifier
{
public:
    IParamBlock2* pblock = nullptr;

    EZBoxTriAO()  { g_EZBoxTriAODesc.MakeAutoParamBlocks(this); ResetPB2ToDefaults(pblock); }
    ~EZBoxTriAO() override = default;

    // ---- Animatable --------------------------------------------------------
    SClass_ID   SuperClassID() override { return OSM_CLASS_ID; }
    Class_ID    ClassID()      override { return EZ_BOXTRI_AO_CLASS_ID; }
    void        DeleteThis()   override { delete this; }
    int         NumSubs()      override { return 1; }
    Animatable* SubAnim(int i) override { return i == 0 ? pblock : nullptr; }
    TSTR        SubAnimName(int i, bool = true) override
    { return i == 0 ? TSTR(_T("Parameters")) : TSTR(_T("")); }

    // ---- ReferenceTarget ---------------------------------------------------
    RefTargetHandle Clone(RemapDir& remap) override
    {
        EZBoxTriAO* c = new EZBoxTriAO();
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

    // ---- Param blocks ------------------------------------------------------
    int           NumParamBlocks()              override { return 1; }
    IParamBlock2* GetParamBlock(int i)          override { return i == 0 ? pblock : nullptr; }
    IParamBlock2* GetParamBlockByID(BlockID id) override { return id == kAOPBlock ? pblock : nullptr; }

    // ---- BaseObject --------------------------------------------------------
    CreateMouseCallBack* GetCreateMouseCallBack() override { return nullptr; }
    const TCHAR* GetObjectName(bool) const override { return kAOName; }

    void BeginEditParams(IObjParam* ip, ULONG flags, Animatable* prev) override
    { g_EZBoxTriAODesc.BeginEditParams(ip, this, flags, prev); }
    void EndEditParams(IObjParam* ip, ULONG flags, Animatable* next) override
    { g_EZBoxTriAODesc.EndEditParams(ip, this, flags, next); }

    // ---- Modifier ----------------------------------------------------------
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
        AOLog("MO: enter");
        if (!os || !os->obj || !pblock) { AOLog("MO: null os/obj/pblock -> ret"); return; }
        const Class_ID triID(TRIOBJ_CLASS_ID, 0);
        if (!os->obj->CanConvertToType(triID)) { AOLog("MO: cannot convert -> ret"); return; }
        TriObject* tri = static_cast<TriObject*>(os->obj->ConvertToType(t, triID));
        if (!tri) { AOLog("MO: tri null -> ret"); return; }
        if (os->obj != tri) os->obj = tri;
        Mesh& mesh = tri->GetMesh();
        AOLogI("MO: mesh nv=%d nf=%d", mesh.numVerts, mesh.numFaces);
        if (mesh.numVerts <= 0 || mesh.numFaces <= 0) { AOLog("MO: empty mesh -> ret"); return; }
        Apply(t, mesh);
        AOLog("MO: apply done");
        // AO only writes a texture/vertex-colour channel. Do NOT invalidate the
        // topology cache: topology is unchanged, and forcing a rebuild on a mesh
        // that carries the mapper's face-corner map tables can crash. Flushing
        // the geom cache is enough to refresh the display.
        mesh.InvalidateGeomCache();
        tri->UpdateValidity(TEXMAP_CHAN_NUM, LocalValidity(t));
        AOLog("MO: exit");
    }

private:
    static int   ClampCh(int ch) { return std::clamp(ch, 1, 99); }
    static float SafeDim(float v) { return std::fabs(v) < 1e-3f ? 1.0f : v; }

    float PBf(AOParamIDs id, TimeValue t, float def) const
    { float v = def; if (pblock) pblock->GetValue(id, t, v, FOREVER); return v; }
    int   PBi(AOParamIDs id, TimeValue t, int   def) const
    { int   v = def; if (pblock) pblock->GetValue(id, t, v, FOREVER); return v; }
    BOOL  PBb(AOParamIDs id, TimeValue t, BOOL  def) const
    { BOOL  v = def; if (pblock) pblock->GetValue(id, t, v, FOREVER); return v; }

    static Point3 FaceNormal(const Mesh& mesh, int f)
    {
        const Face& fc = mesh.faces[f];
        const Point3 e1 = mesh.verts[fc.v[1]] - mesh.verts[fc.v[0]];
        const Point3 e2 = mesh.verts[fc.v[2]] - mesh.verts[fc.v[0]];
        const Point3 n  = e1 ^ e2;
        const float  L  = Length(n);
        return L > 1e-6f ? n / L : Point3(0,0,1);
    }

    void EnsureChannel(Mesh& mesh, int ch) const
    {
        if (ch < 1 || ch > 99) return;
        const int needed = ch + 1;
        if (mesh.getNumMaps() < needed) mesh.setNumMaps(needed, TRUE);
        if (!mesh.mapSupport(ch)) mesh.setMapSupport(ch, TRUE);
    }

    // Snapshot the existing per-vertex AO scalar (the red/.x component) from a
    // channel, sampling through its map-face table so it works for welded or
    // face-corner data. out[v] = -1 where there is no existing data.
    static void ReadExistingScalar(Mesh& mesh, int ch, std::vector<float>& out)
    {
        const int nv = mesh.numVerts;
        out.assign(nv, -1.0f);
        std::vector<float> acc(nv, 0.0f);
        std::vector<int>   cnt(nv, 0);

        if (ch == 0)
        {
            if (mesh.numCVerts <= 0 || !mesh.vertCol || !mesh.vcFace) return;
            for (int f = 0; f < mesh.numFaces; ++f)
                for (int c = 0; c < 3; ++c)
                {
                    const DWORD v = mesh.faces[f].v[c];
                    const int idx = mesh.vcFace[f].t[c];
                    if (v < (DWORD)nv && idx >= 0 && idx < mesh.numCVerts)
                    { acc[v] += mesh.vertCol[idx].x; cnt[v] += 1; }
                }
        }
        else
        {
            if (ch < 1 || ch >= mesh.getNumMaps() || !mesh.mapSupport(ch)) return;
            MeshMap& m = mesh.Map(ch);
            if (!m.tf || !m.tv || m.fnum < mesh.numFaces) return;
            for (int f = 0; f < mesh.numFaces; ++f)
                for (int c = 0; c < 3; ++c)
                {
                    const DWORD v = mesh.faces[f].v[c];
                    const int idx = m.tf[f].t[c];
                    if (v < (DWORD)nv && idx >= 0 && idx < m.vnum)
                    { acc[v] += m.tv[idx].x; cnt[v] += 1; }
                }
        }
        for (int v = 0; v < nv; ++v)
            if (cnt[v] > 0) out[v] = acc[v] / (float)cnt[v];
    }

    // Combine this modifier's AO value with the existing channel value.
    static float CombineAO(int mode, float existing, float val)
    {
        if (existing < 0.0f) return val;   // no existing data here
        switch (mode)
        {
        case Comb_Multiply: return existing * val;
        case Comb_Min:      return std::min(existing, val);
        case Comb_Max:      return std::max(existing, val);
        case Comb_Add:      return std::clamp(existing + val, 0.0f, 1.0f);
        case Comb_Subtract: return std::clamp(existing - val, 0.0f, 1.0f);
        default:            return val;    // Replace
        }
    }

    void Apply(TimeValue t, Mesh& mesh) const
    {
        AOLog("Apply: enter");
        const int   ch       = ClampCh(PBi(pb_ao_ch,       t, 11));
        // Contributor weights are BIPOLAR: positive adds occlusion (darkens),
        // negative subtracts (carves AO back out, e.g. brighten edges).
        const float wCav     = PBf(pb_ao_cavity,    t, 1.0f);
        const float wHgt     = PBf(pb_ao_height,    t, 0.0f);
        const float wDwn     = PBf(pb_ao_down,      t, 0.0f);
        const float wConv    = PBf(pb_ao_convex,    t, 0.0f);
        const float wCurv    = PBf(pb_ao_curvMag,   t, 0.0f);
        const float wUp      = PBf(pb_ao_upFacing,  t, 0.0f);
        const float wRough   = PBf(pb_ao_roughness, t, 0.0f);
        const float strength = std::max(0.0f, PBf(pb_ao_strength, t, 1.0f));
        const bool  gray     = PBb(pb_ao_gray,    t, FALSE) != FALSE;
        const bool  preview  = PBb(pb_ao_preview, t, FALSE) != FALSE;
        const bool  invert   = PBb(pb_ao_invert,  t, FALSE) != FALSE;
        const int   combine  = std::clamp(PBi(pb_ao_combine, t, Comb_Replace), 0, Comb_Count - 1);

        const int nv = mesh.numVerts;

        // If combining with the existing channel, snapshot its per-vertex value
        // (the red/AO component) BEFORE we rebuild the channel below.
        std::vector<float> existing;
        if (combine != Comb_Replace)
            ReadExistingScalar(mesh, ch, existing);

        // Bounds (Z range for height term)
        Box3 b; b.Init();
        for (int i = 0; i < nv; ++i) b += mesh.verts[i];
        if (b.IsEmpty()) { b += Point3(0,0,0); b += Point3(1,1,1); }
        const Point3 mn = b.Min();
        const float zRange = SafeDim(b.Max().z - mn.z);

        // A face is only safe to touch if all 3 indices are in range. During a
        // stack re-eval (toggling this modifier on/off over the mapper) the mesh
        // can transiently present out-of-range indices; an unguarded vn[v] write
        // would corrupt the heap (hard crash later in ntdll).
        auto faceValid = [nv](const Face& fc) -> bool
        {
            return fc.v[0] < (DWORD)nv && fc.v[1] < (DWORD)nv && fc.v[2] < (DWORD)nv;
        };

        // Smoothed vertex normals + neighbour-centroid accumulation (one pass).
        // vnCnt = incident face count (for the roughness / normal-variance term).
        std::vector<Point3> vn(nv, Point3(0,0,0));
        std::vector<int>    vnCnt(nv, 0);
        std::vector<Point3> nbrSum(nv, Point3(0,0,0));
        std::vector<int>    nbrCnt(nv, 0);
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
                vn[v]    += fn;  vnCnt[v] += 1;
                nbrSum[v] += mesh.verts[a] + mesh.verts[d];
                nbrCnt[v] += 2;
            }
        }

        AOLog("Apply: normals loop done");
        // Build the AO channel (welded per-vertex: smooth gradient, compact)
        AOLogI("Apply: EnsureChannel ch=%d numMaps(before)=%d", ch, mesh.getNumMaps());
        EnsureChannel(mesh, ch);
        AOLogI("Apply: post-ensure numMaps=%d support=%d", mesh.getNumMaps(), mesh.mapSupport(ch));
        mesh.setNumMapVerts(ch, nv, FALSE);
        mesh.setNumMapFaces(ch, mesh.numFaces, FALSE);
        AOLog("Apply: setNumMap done");
        MeshMap& map = mesh.Map(ch);
        AOLogI("Apply: Map() fnum=%d vnum=%d", map.fnum, map.vnum);
        // Defensive: bail if the channel allocation didn't take, rather than
        // writing into null/undersized arrays (hard crash).
        if (!map.tf || !map.tv ||
            map.fnum < mesh.numFaces || map.vnum < nv)
            { AOLog("Apply: map alloc short -> ret"); return; }
        const DWORD nvMax = (DWORD)(nv - 1);
        for (int f = 0; f < mesh.numFaces; ++f)
        {
            // Clamp indices so a transiently-bad face can't store an out-of-range
            // map-vert reference (which a downstream channel reader would OOB on).
            const Face& fc = mesh.faces[f];
            map.tf[f].t[0] = std::min<DWORD>(fc.v[0], nvMax);
            map.tf[f].t[1] = std::min<DWORD>(fc.v[1], nvMax);
            map.tf[f].t[2] = std::min<DWORD>(fc.v[2], nvMax);
        }
        AOLog("Apply: tf loop done");

        // Compute per-vertex AO, store it, and write the ch11 (welded) channel.
        std::vector<float> aoVal((size_t)nv, 1.0f);
        for (int v = 0; v < nv; ++v)
        {
            Point3 n = vn[v];
            const float nl = Length(n);
            n = nl > 1e-6f ? n / nl : Point3(0,0,1);

            // signed curvature: + concave (cavity), - convex (edge)
            float signedCurv = 0.0f;
            if (nbrCnt[v] > 0)
            {
                const Point3 centroid = nbrSum[v] / (float)nbrCnt[v];
                Point3 toC = centroid - mesh.verts[v];
                const float tl = Length(toC);
                if (tl > 1e-6f) signedCurv = DotProd(toC / tl, n);
            }
            const float cav     = std::clamp(signedCurv, 0.0f, 1.0f);
            const float convex  = std::clamp(-signedCurv, 0.0f, 1.0f);
            const float curvMag = std::clamp(std::fabs(signedCurv), 0.0f, 1.0f);

            const float hgt = std::clamp(1.0f - (mesh.verts[v].z - mn.z) / zRange, 0.0f, 1.0f);
            const float dwn = std::clamp(-n.z, 0.0f, 1.0f);
            const float up  = std::clamp( n.z, 0.0f, 1.0f);
            const float rough = (vnCnt[v] > 0)
                ? std::clamp(1.0f - Length(vn[v]) / (float)vnCnt[v], 0.0f, 1.0f) : 0.0f;

            // Additive (bipolar) occlusion: each contributor can add or subtract.
            float occ = wCav * cav + wHgt * hgt + wDwn * dwn
                      + wConv * convex + wCurv * curvMag + wUp * up + wRough * rough;
            occ = std::clamp(occ * strength, 0.0f, 1.0f);
            // Default: 1 = lit/open, 0 = occluded. Invert to store occlusion
            // amount instead (1 = occluded), to match shaders that expect that.
            float ao = invert ? occ : (1.0f - occ);

            // Combine with the channel's existing AO (Replace/Mul/Min/Max/Add/Sub).
            if (combine != Comb_Replace && v < (int)existing.size())
                ao = CombineAO(combine, existing[v], ao);

            aoVal[v] = ao;
            map.tv[v] = gray ? Point3(ao, ao, ao) : Point3(ao, 0.0f, 0.0f);
        }
        AOLog("Apply: tv(ch11) loop done");

        // Optional viewport preview: mirror AO into vertex-colour ch0 (grayscale).
        // CRITICAL: write ch0 with FACE-CORNER topology (3 cverts per face) so it
        // matches the mapper's ch0 below us. The previous welded write shrank
        // ch0 from 3*faces down to numVerts, restructuring the legacy vertCol
        // state that is aliased into the unified map table -> heap corruption /
        // hard crash when stacked over EZ BoxTri. Same-topology overwrite is safe.
        if (preview)
        {
            const int ncv = mesh.numFaces * 3;
            mesh.setNumVertCol(ncv, FALSE);
            mesh.setNumVCFaces(mesh.numFaces, FALSE);
            if (mesh.vcFace && mesh.vertCol)
            {
                for (int f = 0; f < mesh.numFaces; ++f)
                {
                    const Face& fc = mesh.faces[f];
                    for (int c = 0; c < 3; ++c)
                    {
                        const int corner = f * 3 + c;
                        mesh.vcFace[f].t[c] = (DWORD)corner;
                        const DWORD vv = fc.v[c];
                        const float a = (vv < (DWORD)nv) ? aoVal[vv] : 1.0f;
                        mesh.vertCol[corner] = Point3(a, a, a);
                    }
                }
            }
            AOLog("Apply: preview(face-corner) done");
        }
        AOLog("Apply: exit");
    }
};

void* EZBoxTriAOClassDesc::Create(BOOL) { return new EZBoxTriAO(); }

// ---------------------------------------------------------------------------
// DlgProc — populates the Combine-mode combobox and syncs it to the pblock.
// (Only purpose: the combobox; the modifier does NOT touch node display.)
// ---------------------------------------------------------------------------
class EZAODlgProc : public ParamMap2UserDlgProc
{
public:
    INT_PTR DlgProc(TimeValue t, IParamMap2* map, HWND hWnd,
                    UINT msg, WPARAM wParam, LPARAM) override
    {
        switch (msg)
        {
        case WM_INITDIALOG:
        {
            HWND cb = GetDlgItem(hWnd, IDC_AO_COMBO_COMBINE);
            if (cb && map && map->GetParamBlock())
            {
                SendMessage(cb, CB_RESETCONTENT, 0, 0);
                for (int i = 0; i < Comb_Count; ++i)
                    SendMessage(cb, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(kAOCombineLabels[i]));
                int val = 0;
                map->GetParamBlock()->GetValue((ParamID)pb_ao_combine, 0, val, FOREVER);
                SendMessage(cb, CB_SETCURSEL, (WPARAM)std::clamp(val, 0, Comb_Count - 1), 0);
            }
            break;
        }
        case WM_COMMAND:
            if (HIWORD(wParam) == CBN_SELCHANGE && LOWORD(wParam) == IDC_AO_COMBO_COMBINE)
            {
                int sel = (int)SendMessage(GetDlgItem(hWnd, IDC_AO_COMBO_COMBINE), CB_GETCURSEL, 0, 0);
                if (sel >= 0 && map && map->GetParamBlock())
                    map->GetParamBlock()->SetValue((ParamID)pb_ao_combine, t, sel);
                return TRUE;
            }
            break;
        }
        return FALSE;
    }
    void DeleteThis() override {}
};
static EZAODlgProc g_AODlgProc;

// ---------------------------------------------------------------------------
// ParamBlockDesc2
//
// Note: the modifier deliberately does NOT touch the node's vertex-colour
// display state. "Preview to ch0" only writes data; you control whether
// vertex colours are shown (Object Properties > Vertex Channel Display, or the
// test rollout buttons) so your material/shader stays visible when you want it.
// ---------------------------------------------------------------------------

static ParamBlockDesc2 g_AOPBlock
(
    kAOPBlock, _T("params"), IDS_AO_PARAMS, &g_EZBoxTriAODesc,
    P_AUTO_CONSTRUCT | P_AUTO_UI,
    0,
    IDD_PANEL_AO, IDS_AO_PARAMS, 0, 0, &g_AODlgProc,

    pb_ao_ch, _T("aoChannel"), TYPE_INT, P_ANIMATABLE, IDS_AO_CH,
        p_default, 11, p_range, 1, 99,
        p_ui, TYPE_SPINNER, EDITTYPE_INT, IDC_AO_EDIT_CH, IDC_AO_SPIN_CH, SPIN_AUTOSCALE,
    p_end,
    pb_ao_cavity, _T("cavity"), TYPE_FLOAT, P_ANIMATABLE, IDS_AO_CAVITY,
        p_default, 1.0f, p_range, -4.0f, 4.0f,
        p_ui, TYPE_SPINNER, EDITTYPE_FLOAT, IDC_AO_EDIT_CAVITY, IDC_AO_SPIN_CAVITY, SPIN_AUTOSCALE,
    p_end,
    pb_ao_height, _T("height"), TYPE_FLOAT, P_ANIMATABLE, IDS_AO_HEIGHT,
        p_default, 0.0f, p_range, -4.0f, 4.0f,
        p_ui, TYPE_SPINNER, EDITTYPE_FLOAT, IDC_AO_EDIT_HEIGHT, IDC_AO_SPIN_HEIGHT, SPIN_AUTOSCALE,
    p_end,
    pb_ao_down, _T("down"), TYPE_FLOAT, P_ANIMATABLE, IDS_AO_DOWN,
        p_default, 0.0f, p_range, -4.0f, 4.0f,
        p_ui, TYPE_SPINNER, EDITTYPE_FLOAT, IDC_AO_EDIT_DOWN, IDC_AO_SPIN_DOWN, SPIN_AUTOSCALE,
    p_end,
    pb_ao_convex, _T("convex"), TYPE_FLOAT, P_ANIMATABLE, IDS_AO_CONVEX,
        p_default, 0.0f, p_range, -4.0f, 4.0f,
        p_ui, TYPE_SPINNER, EDITTYPE_FLOAT, IDC_AO_EDIT_CONVEX, IDC_AO_SPIN_CONVEX, SPIN_AUTOSCALE,
    p_end,
    pb_ao_curvMag, _T("curvMag"), TYPE_FLOAT, P_ANIMATABLE, IDS_AO_CURVMAG,
        p_default, 0.0f, p_range, -4.0f, 4.0f,
        p_ui, TYPE_SPINNER, EDITTYPE_FLOAT, IDC_AO_EDIT_CURVMAG, IDC_AO_SPIN_CURVMAG, SPIN_AUTOSCALE,
    p_end,
    pb_ao_upFacing, _T("upFacing"), TYPE_FLOAT, P_ANIMATABLE, IDS_AO_UPFACING,
        p_default, 0.0f, p_range, -4.0f, 4.0f,
        p_ui, TYPE_SPINNER, EDITTYPE_FLOAT, IDC_AO_EDIT_UPFACING, IDC_AO_SPIN_UPFACING, SPIN_AUTOSCALE,
    p_end,
    pb_ao_roughness, _T("roughness"), TYPE_FLOAT, P_ANIMATABLE, IDS_AO_ROUGHNESS,
        p_default, 0.0f, p_range, -4.0f, 4.0f,
        p_ui, TYPE_SPINNER, EDITTYPE_FLOAT, IDC_AO_EDIT_ROUGHNESS, IDC_AO_SPIN_ROUGHNESS, SPIN_AUTOSCALE,
    p_end,
    pb_ao_strength, _T("strength"), TYPE_FLOAT, P_ANIMATABLE, IDS_AO_STRENGTH,
        p_default, 1.0f, p_range, 0.0f, 8.0f,
        p_ui, TYPE_SPINNER, EDITTYPE_FLOAT, IDC_AO_EDIT_STRENGTH, IDC_AO_SPIN_STRENGTH, SPIN_AUTOSCALE,
    p_end,
    pb_ao_gray, _T("grayscale"), TYPE_BOOL, P_ANIMATABLE, IDS_AO_GRAY,
        p_default, FALSE, p_ui, TYPE_SINGLECHEKBOX, IDC_AO_CHK_GRAY,
    p_end,
    pb_ao_preview, _T("preview"), TYPE_BOOL, P_ANIMATABLE, IDS_AO_PREVIEW,
        p_default, FALSE, p_ui, TYPE_SINGLECHEKBOX, IDC_AO_CHK_PREVIEW,
    p_end,
    pb_ao_invert, _T("invert"), TYPE_BOOL, P_ANIMATABLE, IDS_AO_INVERT,
        p_default, FALSE, p_ui, TYPE_SINGLECHEKBOX, IDC_AO_CHK_INVERT,
    p_end,
    pb_ao_combine, _T("combine"), TYPE_INT, P_ANIMATABLE, IDS_AO_COMBINE,
        p_default, Comb_Replace, p_range, 0, Comb_Count - 1,
    p_end,

    p_end
);
