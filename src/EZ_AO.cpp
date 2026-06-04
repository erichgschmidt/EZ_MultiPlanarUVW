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

enum AOParamBlockIDs { kAOPBlock = 0 };

enum AOParamIDs
{
    pb_ao_ch = 0,
    pb_ao_cavity,
    pb_ao_height,
    pb_ao_down,
    pb_ao_strength,
    pb_ao_gray,
    pb_ao_preview
};

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

    EZBoxTriAO()  { g_EZBoxTriAODesc.MakeAutoParamBlocks(this); }
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
        if (!os || !os->obj || !pblock) return;
        const Class_ID triID(TRIOBJ_CLASS_ID, 0);
        if (!os->obj->CanConvertToType(triID)) return;
        TriObject* tri = static_cast<TriObject*>(os->obj->ConvertToType(t, triID));
        if (!tri) return;
        if (os->obj != tri) os->obj = tri;
        Mesh& mesh = tri->GetMesh();
        if (mesh.numVerts <= 0 || mesh.numFaces <= 0) return;
        Apply(t, mesh);
        mesh.InvalidateGeomCache();
        mesh.InvalidateTopologyCache();
        tri->UpdateValidity(TEXMAP_CHAN_NUM, LocalValidity(t));
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

    void Apply(TimeValue t, Mesh& mesh) const
    {
        const int   ch       = ClampCh(PBi(pb_ao_ch,       t, 11));
        const float wCav     = std::max(0.0f, PBf(pb_ao_cavity,   t, 1.0f));
        const float wHgt     = std::max(0.0f, PBf(pb_ao_height,   t, 0.0f));
        const float wDwn     = std::max(0.0f, PBf(pb_ao_down,     t, 0.0f));
        const float strength = std::max(0.0f, PBf(pb_ao_strength, t, 1.0f));
        const bool  gray     = PBb(pb_ao_gray,    t, FALSE) != FALSE;
        const bool  preview  = PBb(pb_ao_preview, t, FALSE) != FALSE;

        const int nv = mesh.numVerts;

        // Bounds (Z range for height term)
        Box3 b; b.Init();
        for (int i = 0; i < nv; ++i) b += mesh.verts[i];
        if (b.IsEmpty()) { b += Point3(0,0,0); b += Point3(1,1,1); }
        const Point3 mn = b.Min();
        const float zRange = SafeDim(b.Max().z - mn.z);

        // Smoothed vertex normals + neighbour-centroid accumulation (one pass)
        std::vector<Point3> vn(nv, Point3(0,0,0));
        std::vector<Point3> nbrSum(nv, Point3(0,0,0));
        std::vector<int>    nbrCnt(nv, 0);
        for (int f = 0; f < mesh.numFaces; ++f)
        {
            const Face&  fc = mesh.faces[f];
            const Point3 fn = FaceNormal(mesh, f);
            for (int c = 0; c < 3; ++c)
            {
                const int v = fc.v[c];
                const int a = fc.v[(c + 1) % 3];
                const int d = fc.v[(c + 2) % 3];
                vn[v]    += fn;
                nbrSum[v] += mesh.verts[a] + mesh.verts[d];
                nbrCnt[v] += 2;
            }
        }

        // Build the AO channel (welded per-vertex: smooth gradient, compact)
        EnsureChannel(mesh, ch);
        mesh.setNumMapVerts(ch, nv, FALSE);
        mesh.setNumMapFaces(ch, mesh.numFaces, FALSE);
        MeshMap& map = mesh.Map(ch);
        for (int f = 0; f < mesh.numFaces; ++f)
        {
            map.tf[f].t[0] = mesh.faces[f].v[0];
            map.tf[f].t[1] = mesh.faces[f].v[1];
            map.tf[f].t[2] = mesh.faces[f].v[2];
        }

        // Optional viewport preview: mirror AO into vertex-color ch0 (grayscale)
        // so it's visible in the Nitrous viewport. Overlays the blend preview
        // since this modifier sits above EZ BoxTri in the stack. Enable the
        // object's Vertex Color display to see it.
        if (preview)
        {
            mesh.setNumVertCol(nv, FALSE);
            mesh.setNumVCFaces(mesh.numFaces, FALSE);
            for (int f = 0; f < mesh.numFaces; ++f)
            {
                mesh.vcFace[f].t[0] = mesh.faces[f].v[0];
                mesh.vcFace[f].t[1] = mesh.faces[f].v[1];
                mesh.vcFace[f].t[2] = mesh.faces[f].v[2];
            }
        }

        for (int v = 0; v < nv; ++v)
        {
            Point3 n = vn[v];
            const float nl = Length(n);
            n = nl > 1e-6f ? n / nl : Point3(0,0,1);

            // cavity
            float cav = 0.0f;
            if (nbrCnt[v] > 0)
            {
                const Point3 centroid = nbrSum[v] / (float)nbrCnt[v];
                Point3 toC = centroid - mesh.verts[v];
                const float tl = Length(toC);
                if (tl > 1e-6f)
                    cav = std::clamp(DotProd(toC / tl, n), 0.0f, 1.0f);
            }

            const float hgt = std::clamp(1.0f - (mesh.verts[v].z - mn.z) / zRange, 0.0f, 1.0f);
            const float dwn = std::clamp(-n.z, 0.0f, 1.0f);

            float occ = wCav * cav + wHgt * hgt + wDwn * dwn;
            occ = std::clamp(occ * strength, 0.0f, 1.0f);
            const float ao = 1.0f - occ;

            map.tv[v] = gray ? Point3(ao, ao, ao) : Point3(ao, 0.0f, 0.0f);
            if (preview) mesh.vertCol[v] = Point3(ao, ao, ao);
        }
    }
};

void* EZBoxTriAOClassDesc::Create(BOOL) { return new EZBoxTriAO(); }

// ---------------------------------------------------------------------------
// DlgProc — makes the Preview checkbox behave like a DCM's display toggle:
// when toggled, it also turns the selected object's vertex-color display on/off
// so the ch0 AO write is immediately visible without manual setup.
// ---------------------------------------------------------------------------

class EZAODlgProc : public ParamMap2UserDlgProc
{
public:
    INT_PTR DlgProc(TimeValue, IParamMap2*, HWND hWnd,
                    UINT msg, WPARAM wParam, LPARAM) override
    {
        if (msg == WM_COMMAND && LOWORD(wParam) == IDC_AO_CHK_PREVIEW)
        {
            const bool on = (IsDlgButtonChecked(hWnd, IDC_AO_CHK_PREVIEW) == BST_CHECKED);
            Interface* ip = GetCOREInterface();
            INode* node = ip ? ip->GetSelNode(0) : nullptr;
            if (node)
            {
                node->SetCVertMode(on ? 1 : 0);     // show vertex colours
                node->SetVertexColorType(0);        // 0 = vertex colour (map ch 0)
                node->SetShadeCVerts(FALSE);        // raw, unshaded, for inspection
                if (ip) ip->ForceCompleteRedraw();
            }
        }
        return FALSE;
    }
    void DeleteThis() override {} // static instance
};

static EZAODlgProc g_AODlgProc;

// ---------------------------------------------------------------------------
// ParamBlockDesc2
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
        p_default, 1.0f, p_range, 0.0f, 1.0f,
        p_ui, TYPE_SPINNER, EDITTYPE_FLOAT, IDC_AO_EDIT_CAVITY, IDC_AO_SPIN_CAVITY, SPIN_AUTOSCALE,
    p_end,
    pb_ao_height, _T("height"), TYPE_FLOAT, P_ANIMATABLE, IDS_AO_HEIGHT,
        p_default, 0.0f, p_range, 0.0f, 1.0f,
        p_ui, TYPE_SPINNER, EDITTYPE_FLOAT, IDC_AO_EDIT_HEIGHT, IDC_AO_SPIN_HEIGHT, SPIN_AUTOSCALE,
    p_end,
    pb_ao_down, _T("down"), TYPE_FLOAT, P_ANIMATABLE, IDS_AO_DOWN,
        p_default, 0.0f, p_range, 0.0f, 1.0f,
        p_ui, TYPE_SPINNER, EDITTYPE_FLOAT, IDC_AO_EDIT_DOWN, IDC_AO_SPIN_DOWN, SPIN_AUTOSCALE,
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

    p_end
);
