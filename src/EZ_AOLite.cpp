/*
    EZ_AOLite.cpp
    3ds Max 2023 C++ SDK  —  OSM modifier  —  part of EZ_BoxTri.dlm

    "EZ AO" — the lightweight pseudo-AO modifier. Just the original three
    contributors (Cavity = curvature, Height, Down = normals), each a plain
    0..1 weight, plus Strength. No bipolar weights, combine modes, or extra
    signals — that's "EZ AO 2". This one is for instant, no-fuss results.

    ao = 1 - clamp((cavity*cav + height*hgt + down*dwn) * strength)
    Written per-vertex to a map channel (default 11). Red-only or grayscale,
    optional invert, optional ch0 preview. Face-corner write for ch0 (avoids
    the welded/face-corner restructure crash when stacked).
*/

#include "resource.h"

#include <max.h>
#include <iparamb2.h>
#include <modstack.h>
#include <object.h>
#include <mesh.h>
#include <triobj.h>
#include <istdplug.h>

#include <algorithm>
#include <cmath>
#include <vector>

extern HINSTANCE hInstance;  // defined in EZ_BoxTri.cpp

#define EZ_AOLITE_CLASS_ID Class_ID(0x6d2f3a11, 0x1e0b7c90)

static const TCHAR* kAlName     = _T("EZ AO");
static const TCHAR* kAlCategory = _T("EZ Tools");

enum AlParamBlockIDs { kAlPBlock = 0 };

enum AlParamIDs
{
    al_ch = 0,
    al_cavity,
    al_height,
    al_down,
    al_strength,
    al_gray,
    al_invert,
    al_preview
};

static void AlResetPB2ToDefaults(IParamBlock2* pb)
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

class EZAOLiteClassDesc : public ClassDesc2
{
public:
    int          IsPublic()              override { return TRUE; }
    void*        Create(BOOL = FALSE)    override;
    const TCHAR* ClassName()             override { return kAlName; }
    const TCHAR* NonLocalizedClassName() override { return kAlName; }
    SClass_ID    SuperClassID()          override { return OSM_CLASS_ID; }
    Class_ID     ClassID()               override { return EZ_AOLITE_CLASS_ID; }
    const TCHAR* Category()              override { return kAlCategory; }
    const TCHAR* InternalName()          override { return _T("EZAOSimple"); }
    HINSTANCE    HInstance()             override { return hInstance; }
};

static EZAOLiteClassDesc g_EZAOLiteDesc;
ClassDesc2* GetEZAOLiteDesc() { return &g_EZAOLiteDesc; }

class EZAOLite : public Modifier
{
public:
    IParamBlock2* pblock = nullptr;

    EZAOLite()  { g_EZAOLiteDesc.MakeAutoParamBlocks(this); AlResetPB2ToDefaults(pblock); }
    ~EZAOLite() override = default;

    SClass_ID   SuperClassID() override { return OSM_CLASS_ID; }
    Class_ID    ClassID()      override { return EZ_AOLITE_CLASS_ID; }
    void        DeleteThis()   override { delete this; }
    int         NumSubs()      override { return 1; }
    Animatable* SubAnim(int i) override { return i == 0 ? pblock : nullptr; }
    TSTR        SubAnimName(int i, bool = true) override
    { return i == 0 ? TSTR(_T("Parameters")) : TSTR(_T("")); }

    RefTargetHandle Clone(RemapDir& remap) override
    {
        EZAOLite* c = new EZAOLite();
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
    IParamBlock2* GetParamBlockByID(BlockID id) override { return id == kAlPBlock ? pblock : nullptr; }

    CreateMouseCallBack* GetCreateMouseCallBack() override { return nullptr; }
    const TCHAR* GetObjectName(bool) const override { return kAlName; }

    void BeginEditParams(IObjParam* ip, ULONG flags, Animatable* prev) override
    { g_EZAOLiteDesc.BeginEditParams(ip, this, flags, prev); }
    void EndEditParams(IObjParam* ip, ULONG flags, Animatable* next) override
    { g_EZAOLiteDesc.EndEditParams(ip, this, flags, next); }

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

    float PBf(AlParamIDs id, TimeValue t, float def) const
    { float v = def; if (pblock) pblock->GetValue((ParamID)id, t, v, FOREVER); return v; }
    int   PBi(AlParamIDs id, TimeValue t, int   def) const
    { int   v = def; if (pblock) pblock->GetValue((ParamID)id, t, v, FOREVER); return v; }
    BOOL  PBb(AlParamIDs id, TimeValue t, BOOL  def) const
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

    void EnsureChannel(Mesh& mesh, int ch) const
    {
        if (ch < 1 || ch > 99) return;
        const int needed = ch + 1;
        if (mesh.getNumMaps() < needed) mesh.setNumMaps(needed, TRUE);
        if (!mesh.mapSupport(ch)) mesh.setMapSupport(ch, TRUE);
    }

    void Apply(TimeValue t, Mesh& mesh) const
    {
        const int   ch       = ClampCh(PBi(al_ch, t, 11));
        const float wCav     = std::max(0.0f, PBf(al_cavity,   t, 1.0f));
        const float wHgt     = std::max(0.0f, PBf(al_height,   t, 0.0f));
        const float wDwn     = std::max(0.0f, PBf(al_down,     t, 0.0f));
        const float strength = std::max(0.0f, PBf(al_strength, t, 1.0f));
        const bool  gray     = PBb(al_gray,    t, FALSE) != FALSE;
        const bool  invert   = PBb(al_invert,  t, FALSE) != FALSE;
        const bool  preview  = PBb(al_preview, t, FALSE) != FALSE;

        const int nv = mesh.numVerts;

        Box3 b; b.Init();
        for (int i = 0; i < nv; ++i) b += mesh.verts[i];
        if (b.IsEmpty()) { b += Point3(0,0,0); b += Point3(1,1,1); }
        const Point3 mn = b.Min();
        const float zRange = SafeDim(b.Max().z - mn.z);

        auto faceValid = [nv](const Face& fc){ return fc.v[0]<(DWORD)nv && fc.v[1]<(DWORD)nv && fc.v[2]<(DWORD)nv; };

        std::vector<Point3> vn(nv, Point3(0,0,0)), nbrSum(nv, Point3(0,0,0));
        std::vector<int>    nbrCnt(nv, 0);
        for (int f = 0; f < mesh.numFaces; ++f)
        {
            const Face& fc = mesh.faces[f];
            if (!faceValid(fc)) continue;
            const Point3 fn = FaceNormal(mesh, f);
            for (int c = 0; c < 3; ++c)
            {
                const int v = fc.v[c], a = fc.v[(c+1)%3], d = fc.v[(c+2)%3];
                vn[v] += fn;
                nbrSum[v] += mesh.verts[a] + mesh.verts[d];
                nbrCnt[v] += 2;
            }
        }

        std::vector<float> aoVal((size_t)nv, 1.0f);
        for (int v = 0; v < nv; ++v)
        {
            Point3 n = vn[v];
            const float nl = Length(n);
            n = nl > 1e-6f ? n / nl : Point3(0,0,1);

            float cav = 0.0f;
            if (nbrCnt[v] > 0)
            {
                Point3 toC = nbrSum[v] / (float)nbrCnt[v] - mesh.verts[v];
                const float tl = Length(toC);
                if (tl > 1e-6f) cav = std::clamp(DotProd(toC / tl, n), 0.0f, 1.0f);
            }
            const float hgt = std::clamp(1.0f - (mesh.verts[v].z - mn.z) / zRange, 0.0f, 1.0f);
            const float dwn = std::clamp(-n.z, 0.0f, 1.0f);

            float occ = std::clamp((wCav*cav + wHgt*hgt + wDwn*dwn) * strength, 0.0f, 1.0f);
            aoVal[v] = invert ? occ : (1.0f - occ);
        }

        // ---- Write target channel, face-corner (ch0 -> legacy vertCol) ----
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
                    const int corner = f*3 + c;
                    mesh.vcFace[f].t[c] = (DWORD)corner;
                    const DWORD vv = fc.v[c];
                    const float a = (vv < (DWORD)nv) ? aoVal[vv] : 1.0f;
                    mesh.vertCol[corner] = gray ? Point3(a,a,a) : Point3(a,0.0f,0.0f);
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
                const int corner = f*3 + c;
                map.tf[f].t[c] = (DWORD)corner;
                const DWORD vv = fc.v[c];
                const float a = (vv < (DWORD)nv) ? aoVal[vv] : 1.0f;
                map.tv[corner] = gray ? Point3(a,a,a) : Point3(a,0.0f,0.0f);
            }
        }

        if (preview)
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
                        const int corner = f*3 + c;
                        mesh.vcFace[f].t[c] = (DWORD)corner;
                        const DWORD vv = fc.v[c];
                        const float a = (vv < (DWORD)nv) ? aoVal[vv] : 1.0f;
                        mesh.vertCol[corner] = Point3(a,a,a);
                    }
                }
        }
    }
};

void* EZAOLiteClassDesc::Create(BOOL) { return new EZAOLite(); }

// ---------------------------------------------------------------------------
// ParamBlockDesc2
// ---------------------------------------------------------------------------
static ParamBlockDesc2 g_AlPBlock
(
    kAlPBlock, _T("params"), IDS_AL_PARAMS, &g_EZAOLiteDesc,
    P_AUTO_CONSTRUCT | P_AUTO_UI,
    0,
    IDD_PANEL_AOLITE, IDS_AL_PARAMS, 0, 0, nullptr,

    al_ch, _T("aoChannel"), TYPE_INT, P_ANIMATABLE, IDS_AL_CH,
        p_default, 11, p_range, 0, 99,
        p_ui, TYPE_SPINNER, EDITTYPE_INT, IDC_AL_EDIT_CH, IDC_AL_SPIN_CH, SPIN_AUTOSCALE,
    p_end,
    al_cavity, _T("cavity"), TYPE_FLOAT, P_ANIMATABLE, IDS_AL_CAVITY,
        p_default, 1.0f, p_range, 0.0f, 1.0f,
        p_ui, TYPE_SPINNER, EDITTYPE_FLOAT, IDC_AL_EDIT_CAVITY, IDC_AL_SPIN_CAVITY, SPIN_AUTOSCALE,
    p_end,
    al_height, _T("height"), TYPE_FLOAT, P_ANIMATABLE, IDS_AL_HEIGHT,
        p_default, 0.0f, p_range, 0.0f, 1.0f,
        p_ui, TYPE_SPINNER, EDITTYPE_FLOAT, IDC_AL_EDIT_HEIGHT, IDC_AL_SPIN_HEIGHT, SPIN_AUTOSCALE,
    p_end,
    al_down, _T("down"), TYPE_FLOAT, P_ANIMATABLE, IDS_AL_DOWN,
        p_default, 0.0f, p_range, 0.0f, 1.0f,
        p_ui, TYPE_SPINNER, EDITTYPE_FLOAT, IDC_AL_EDIT_DOWN, IDC_AL_SPIN_DOWN, SPIN_AUTOSCALE,
    p_end,
    al_strength, _T("strength"), TYPE_FLOAT, P_ANIMATABLE, IDS_AL_STRENGTH,
        p_default, 1.0f, p_range, 0.0f, 8.0f,
        p_ui, TYPE_SPINNER, EDITTYPE_FLOAT, IDC_AL_EDIT_STRENGTH, IDC_AL_SPIN_STRENGTH, SPIN_AUTOSCALE,
    p_end,
    al_gray, _T("grayscale"), TYPE_BOOL, P_ANIMATABLE, IDS_AL_GRAY,
        p_default, FALSE, p_ui, TYPE_SINGLECHEKBOX, IDC_AL_CHK_GRAY,
    p_end,
    al_invert, _T("invert"), TYPE_BOOL, P_ANIMATABLE, IDS_AL_INVERT,
        p_default, FALSE, p_ui, TYPE_SINGLECHEKBOX, IDC_AL_CHK_INVERT,
    p_end,
    al_preview, _T("preview"), TYPE_BOOL, P_ANIMATABLE, IDS_AL_PREVIEW,
        p_default, FALSE, p_ui, TYPE_SINGLECHEKBOX, IDC_AL_CHK_PREVIEW,
    p_end,

    p_end
);
