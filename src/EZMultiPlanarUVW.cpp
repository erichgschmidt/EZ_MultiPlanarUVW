/*
    EZMultiPlanarUVW.cpp
    3ds Max 2023 C++ SDK  —  OSM (object-space modifier)  —  .dlm

    Ports the working EZ_MultiPlanarUVW.ms behaviour:
        - Four independently-enabled planar UV projections
        - Default channels 1/2/3/4 with X/Z/Y/Y axis defaults
        - Normalised to object local bounds
        - Global U/V tile, offset, flip, swap
        - Automatic map-channel slot expansion
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
#include <utilapi.h>

#include <algorithm>
#include <cmath>
#include <vector>

// ---------------------------------------------------------------------------
// Module handle
// ---------------------------------------------------------------------------

HINSTANCE hInstance = nullptr;

// ---------------------------------------------------------------------------
// Class ID  (matches the MAXScript version so scenes are compatible)
// ---------------------------------------------------------------------------

#define EZ_MULTIPLANAR_UVW_CLASS_ID Class_ID(0x6d2f3a11, 0x1e0b7c44)

static const TCHAR* kPluginName    = _T("EZ Multi Planar UVW");
static const TCHAR* kCategoryName  = _T("EZ Tools");

// ---------------------------------------------------------------------------
// Param block / param IDs
// ---------------------------------------------------------------------------

enum ParamBlockIDs { kMainPBlock = 0 };

enum ParamIDs
{
    pb_enable1 = 0,
    pb_channel1,
    pb_axis1,

    pb_enable2,
    pb_channel2,
    pb_axis2,

    pb_enable3,
    pb_channel3,
    pb_axis3,

    pb_enable4,
    pb_channel4,
    pb_axis4,

    pb_uTile,
    pb_vTile,
    pb_uOffset,
    pb_vOffset,

    pb_flipU,
    pb_flipV,
    pb_swapUV,

    pb_enableBlend,
    pb_channelBlend,
    pb_blendPower
};

// Axis indices — 0-based, matches the dropdown order in the dialog
enum AxisMode
{
    Axis_X = 0, // project onto Y/Z  -> U=Y, V=Z
    Axis_Y = 1, // project onto X/Z  -> U=X, V=Z
    Axis_Z = 2  // project onto X/Y  -> U=X, V=Y
};

// ---------------------------------------------------------------------------
// Forward declaration
// ---------------------------------------------------------------------------

class EZMultiPlanarUVW;

// ---------------------------------------------------------------------------
// ClassDesc2
// ---------------------------------------------------------------------------

class EZMultiPlanarUVWClassDesc : public ClassDesc2
{
public:
    int          IsPublic()  override { return TRUE; }
    void*        Create(BOOL loading = FALSE) override;

    const TCHAR* ClassName()     override { return kPluginName; }
    const TCHAR* NonLocalizedClassName() override { return kPluginName; }
    SClass_ID    SuperClassID()  override { return OSM_CLASS_ID; }
    Class_ID     ClassID()       override { return EZ_MULTIPLANAR_UVW_CLASS_ID; }
    const TCHAR* Category()      override { return kCategoryName; }
    const TCHAR* InternalName()  override { return _T("EZMultiPlanarUVW"); }
    HINSTANCE    HInstance()     override { return hInstance; }
};

static EZMultiPlanarUVWClassDesc g_EZMultiPlanarUVWDesc;

ClassDesc2* GetEZMultiPlanarUVWDesc() { return &g_EZMultiPlanarUVWDesc; }

// ---------------------------------------------------------------------------
// Modifier class
// ---------------------------------------------------------------------------

class EZMultiPlanarUVW : public Modifier
{
public:
    IParamBlock2* pblock = nullptr;

    EZMultiPlanarUVW()  { g_EZMultiPlanarUVWDesc.MakeAutoParamBlocks(this); }
    ~EZMultiPlanarUVW() override = default;

    // ---- Animatable --------------------------------------------------------

    void DeleteThis() override { delete this; }

    int          NumSubs()          override { return 1; }
    Animatable*  SubAnim(int i)     override { return (i == 0) ? pblock : nullptr; }
    TSTR         SubAnimName(int i, bool /*localized*/ = true) override
    {
        return (i == 0) ? TSTR(_T("Parameters")) : TSTR(_T(""));
    }

    // ---- ReferenceTarget ---------------------------------------------------

    RefTargetHandle Clone(RemapDir& remap) override
    {
        EZMultiPlanarUVW* copy = new EZMultiPlanarUVW();
        if (pblock)
            copy->ReplaceReference(0, remap.CloneRef(pblock));
        BaseClone(this, copy, remap);
        return copy;
    }

    int             NumRefs()             override { return 1; }
    RefTargetHandle GetReference(int i)   override { return (i == 0) ? pblock : nullptr; }
    void            SetReference(int i, RefTargetHandle rtarg) override
    {
        if (i == 0)
            pblock = static_cast<IParamBlock2*>(rtarg);
    }

    RefResult NotifyRefChanged(
        const Interval& /*changeInt*/,
        RefTargetHandle /*hTarget*/,
        PartID&         /*partID*/,
        RefMessage       message,
        BOOL             /*propagate*/) override
    {
        if (message == REFMSG_CHANGE)
            NotifyDependents(FOREVER, PART_TEXMAP, REFMSG_CHANGE);
        return REF_SUCCEED;
    }

    // ---- IParamArray2 interface (for P_AUTO_CONSTRUCT) ---------------------

    int           NumParamBlocks()             override { return 1; }
    IParamBlock2* GetParamBlock(int i)         override { return (i == 0) ? pblock : nullptr; }
    IParamBlock2* GetParamBlockByID(BlockID id) override
    {
        return (id == kMainPBlock) ? pblock : nullptr;
    }

    // ---- BaseObject --------------------------------------------------------

    CreateMouseCallBack* GetCreateMouseCallBack() override { return nullptr; }

    const TCHAR* GetObjectName(bool /*localized*/) const override { return kPluginName; }

    void BeginEditParams(IObjParam* ip, ULONG flags, Animatable* prev) override
    {
        g_EZMultiPlanarUVWDesc.BeginEditParams(ip, this, flags, prev);
    }

    void EndEditParams(IObjParam* ip, ULONG flags, Animatable* next) override
    {
        g_EZMultiPlanarUVWDesc.EndEditParams(ip, this, flags, next);
    }

    // ---- Modifier ----------------------------------------------------------

    ChannelMask ChannelsUsed()   override { return GEOM_CHANNEL | TOPO_CHANNEL | TEXMAP_CHANNEL; }
    ChannelMask ChannelsChanged() override { return TEXMAP_CHANNEL; }

    Class_ID InputType() override { return Class_ID(TRIOBJ_CLASS_ID, 0); }

    Interval LocalValidity(TimeValue t) override
    {
        Interval valid = FOREVER;
        if (pblock)
            pblock->GetValidity(t, valid);
        return valid;
    }

    void ModifyObject(TimeValue t, ModContext& /*mc*/, ObjectState* os, INode* /*node*/) override
    {
        if (!os || !os->obj || !pblock)
            return;

        const Class_ID triID(TRIOBJ_CLASS_ID, 0);
        if (!os->obj->CanConvertToType(triID))
            return;

        TriObject* triObj = static_cast<TriObject*>(os->obj->ConvertToType(t, triID));
        if (!triObj)
            return;

        if (os->obj != triObj)
            os->obj = triObj;

        Mesh& mesh = triObj->GetMesh();
        if (mesh.numVerts <= 0 || mesh.numFaces <= 0)
            return;

        ApplyMultiPlanarUVW(t, mesh);

        mesh.InvalidateGeomCache();
        mesh.InvalidateTopologyCache();
        triObj->UpdateValidity(TEXMAP_CHAN_NUM, LocalValidity(t));
    }

    // ========================================================================
private:
    // ---- Helpers -----------------------------------------------------------

    static int ClampChannel(int ch)
    {
        return std::clamp(ch, 1, 99);
    }

    static float SafeRange(float v)
    {
        return (std::fabs(v) < 1e-6f) ? 1.0f : v;
    }

    static float  PBFloat(IParamBlock2* pb, ParamID id, TimeValue t, float  def)
    {
        float  v = def; if (pb) pb->GetValue(id, t, v, FOREVER); return v;
    }
    static int    PBInt  (IParamBlock2* pb, ParamID id, TimeValue t, int    def)
    {
        int    v = def; if (pb) pb->GetValue(id, t, v, FOREVER); return v;
    }
    static BOOL   PBBool (IParamBlock2* pb, ParamID id, TimeValue t, BOOL   def)
    {
        BOOL   v = def; if (pb) pb->GetValue(id, t, v, FOREVER); return v;
    }

    // ---- Geometry ----------------------------------------------------------

    Box3 MeshBounds(const Mesh& mesh) const
    {
        Box3 b;
        b.Init();
        for (int i = 0; i < mesh.numVerts; ++i)
            b += mesh.verts[i];
        if (b.IsEmpty())
        {
            b += Point3(0, 0, 0);
            b += Point3(1, 1, 1);
        }
        return b;
    }

    Point3 NormalizedPt(const Point3& p, const Point3& mn, const Point3& mx) const
    {
        return Point3(
            (p.x - mn.x) / SafeRange(mx.x - mn.x),
            (p.y - mn.y) / SafeRange(mx.y - mn.y),
            (p.z - mn.z) / SafeRange(mx.z - mn.z)
        );
    }

    Point3 UVFromAxis(const Point3& p, const Point3& mn, const Point3& mx, int axis) const
    {
        const Point3 n = NormalizedPt(p, mn, mx);
        switch (axis)
        {
        case Axis_X: return Point3(n.y, n.z, 0.0f);  // side:  U=Y V=Z
        case Axis_Y: return Point3(n.x, n.z, 0.0f);  // front: U=X V=Z
        case Axis_Z: return Point3(n.x, n.y, 0.0f);  // top:   U=X V=Y
        default:     return Point3(n.x, n.y, 0.0f);
        }
    }

    Point3 TransformUV(TimeValue t, const Point3& uv) const
    {
        float u = uv.x;
        float v = uv.y;

        if (PBBool(pblock, pb_swapUV, t, FALSE)) std::swap(u, v);
        if (PBBool(pblock, pb_flipU,  t, FALSE)) u = 1.0f - u;
        if (PBBool(pblock, pb_flipV,  t, FALSE)) v = 1.0f - v;

        return Point3(
            u * PBFloat(pblock, pb_uTile,   t, 1.0f) + PBFloat(pblock, pb_uOffset, t, 0.0f),
            v * PBFloat(pblock, pb_vTile,   t, 1.0f) + PBFloat(pblock, pb_vOffset, t, 0.0f),
            uv.z
        );
    }

    // ---- Map channel management --------------------------------------------

    void EnsureChannel(Mesh& mesh, int ch) const
    {
        ch = ClampChannel(ch);
        const int needed = ch + 1;
        if (mesh.getNumMaps() < needed)
            mesh.setNumMaps(needed, TRUE);
        if (!mesh.mapSupport(ch))
            mesh.setMapSupport(ch, TRUE);
    }

    void BuildChannelTopology(Mesh& mesh, int ch) const
    {
        ch = ClampChannel(ch);
        EnsureChannel(mesh, ch);

        mesh.setNumMapVerts(ch, mesh.numVerts,  FALSE);
        mesh.setNumMapFaces(ch, mesh.numFaces,  FALSE);

        MeshMap& map = mesh.Map(ch);
        for (int f = 0; f < mesh.numFaces; ++f)
        {
            const Face& face = mesh.faces[f];
            map.tf[f].t[0] = face.v[0];
            map.tf[f].t[1] = face.v[1];
            map.tf[f].t[2] = face.v[2];
        }
    }

    void ApplyPlanarChannel(
        TimeValue t, Mesh& mesh, int ch, int axis,
        const Point3& mn, const Point3& mx) const
    {
        ch = ClampChannel(ch);
        BuildChannelTopology(mesh, ch);

        MeshMap& map = mesh.Map(ch);
        for (int v = 0; v < mesh.numVerts; ++v)
            map.tv[v] = TransformUV(t, UVFromAxis(mesh.verts[v], mn, mx, axis));
    }

    // ---- Blend output ------------------------------------------------------

    void ComputeVertexNormals(const Mesh& mesh, std::vector<Point3>& out) const
    {
        out.assign(mesh.numVerts, Point3(0.0f, 0.0f, 0.0f));

        for (int f = 0; f < mesh.numFaces; ++f)
        {
            const Face& face = mesh.faces[f];
            const Point3& p0 = mesh.verts[face.v[0]];
            const Point3& p1 = mesh.verts[face.v[1]];
            const Point3& p2 = mesh.verts[face.v[2]];

            const Point3 edge1 = p1 - p0;
            const Point3 edge2 = p2 - p0;
            const Point3 faceNorm = Normalize(edge1 ^ edge2);

            out[face.v[0]] += faceNorm;
            out[face.v[1]] += faceNorm;
            out[face.v[2]] += faceNorm;
        }

        for (auto& n : out)
        {
            const float len = Length(n);
            n = (len > 1e-6f) ? (n / len) : Point3(0.0f, 0.0f, 1.0f);
        }
    }

    void ApplyBlendChannel(TimeValue t, Mesh& mesh) const
    {
        const int   ch    = ClampChannel(PBInt  (pblock, pb_channelBlend, t, 10));
        const float power = std::max(0.001f, PBFloat(pblock, pb_blendPower,   t,  1.0f));

        std::vector<Point3> normals;
        ComputeVertexNormals(mesh, normals);

        BuildChannelTopology(mesh, ch);
        MeshMap& map = mesh.Map(ch);

        for (int v = 0; v < mesh.numVerts; ++v)
        {
            const Point3& n = normals[v];
            float wx = std::pow(std::fabs(n.x), power);
            float wy = std::pow(std::fabs(n.y), power);
            float wz = std::pow(std::fabs(n.z), power);
            const float sum = wx + wy + wz;
            if (sum > 1e-6f) { wx /= sum; wy /= sum; wz /= sum; }
            map.tv[v] = Point3(wx, wy, wz);
        }
    }

    // ---- Top-level apply ---------------------------------------------------

    void ApplyMultiPlanarUVW(TimeValue t, Mesh& mesh) const
    {
        const Box3   bounds = MeshBounds(mesh);
        const Point3 mn     = bounds.Min();
        const Point3 mx     = bounds.Max();

        // Read all params up front
        const BOOL en1  = PBBool(pblock, pb_enable1, t, TRUE);
        const BOOL en2  = PBBool(pblock, pb_enable2, t, TRUE);
        const BOOL en3  = PBBool(pblock, pb_enable3, t, TRUE);
        const BOOL en4  = PBBool(pblock, pb_enable4, t, TRUE);

        const int  ch1  = ClampChannel(PBInt(pblock, pb_channel1, t, 1));
        const int  ch2  = ClampChannel(PBInt(pblock, pb_channel2, t, 2));
        const int  ch3  = ClampChannel(PBInt(pblock, pb_channel3, t, 3));
        const int  ch4  = ClampChannel(PBInt(pblock, pb_channel4, t, 4));

        const int  ax1  = std::clamp(PBInt(pblock, pb_axis1, t, Axis_X), 0, 2);
        const int  ax2  = std::clamp(PBInt(pblock, pb_axis2, t, Axis_Z), 0, 2);
        const int  ax3  = std::clamp(PBInt(pblock, pb_axis3, t, Axis_Y), 0, 2);
        const int  ax4  = std::clamp(PBInt(pblock, pb_axis4, t, Axis_Y), 0, 2);

        const BOOL enBlend = PBBool(pblock, pb_enableBlend,  t, FALSE);
        const int  chBlend = ClampChannel(PBInt(pblock, pb_channelBlend, t, 10));

        // Pre-expand map table to the highest channel needed in one call
        int highest = 1;
        if (en1)     highest = std::max(highest, ch1);
        if (en2)     highest = std::max(highest, ch2);
        if (en3)     highest = std::max(highest, ch3);
        if (en4)     highest = std::max(highest, ch4);
        if (enBlend) highest = std::max(highest, chBlend);
        EnsureChannel(mesh, ClampChannel(highest));

        if (en1)     ApplyPlanarChannel(t, mesh, ch1, ax1, mn, mx);
        if (en2)     ApplyPlanarChannel(t, mesh, ch2, ax2, mn, mx);
        if (en3)     ApplyPlanarChannel(t, mesh, ch3, ax3, mn, mx);
        if (en4)     ApplyPlanarChannel(t, mesh, ch4, ax4, mn, mx);
        if (enBlend) ApplyBlendChannel(t, mesh);
    }
};

// ---------------------------------------------------------------------------
// ClassDesc2::Create
// ---------------------------------------------------------------------------

void* EZMultiPlanarUVWClassDesc::Create(BOOL /*loading*/)
{
    return new EZMultiPlanarUVW();
}

// ---------------------------------------------------------------------------
// ParamBlockDesc2
// Axis selection uses TYPE_RADIO (3 radio buttons per channel).
// Spinners use paired CustEdit + SpinnerControl IDs.
// ---------------------------------------------------------------------------

static ParamBlockDesc2 g_MainPBlock
(
    kMainPBlock,
    _T("params"),
    IDS_PARAMS,
    &g_EZMultiPlanarUVWDesc,
    P_AUTO_CONSTRUCT | P_AUTO_UI,
    0,                          // P_AUTO_CONSTRUCT ref index

    // P_AUTO_UI args: dialog ID, title string, flags, appendRollout, dlgProc
    IDD_PANEL, IDS_PARAMS, 0, 0, nullptr,

    // ---- Channel 1 ---------------------------------------------------------
    pb_enable1,   _T("enable1"),  TYPE_BOOL,  P_ANIMATABLE, IDS_ENABLE1,
        p_default, TRUE,
        p_ui, TYPE_SINGLECHEKBOX, IDC_CHK_ENABLE1,
    p_end,

    pb_channel1,  _T("channel1"), TYPE_INT,   P_ANIMATABLE, IDS_CHANNEL1,
        p_default, 1,
        p_range, 1, 99,
        p_ui, TYPE_SPINNER, EDITTYPE_INT, IDC_EDIT_CHANNEL1, IDC_SPIN_CHANNEL1, SPIN_AUTOSCALE,
    p_end,

    pb_axis1,     _T("axis1"),    TYPE_INT,   P_ANIMATABLE, IDS_AXIS1,
        p_default, Axis_X,
        p_range, 0, 2,
        p_ui, TYPE_RADIO, 3, IDC_RAD_AXIS1_X, IDC_RAD_AXIS1_Y, IDC_RAD_AXIS1_Z,
    p_end,

    // ---- Channel 2 ---------------------------------------------------------
    pb_enable2,   _T("enable2"),  TYPE_BOOL,  P_ANIMATABLE, IDS_ENABLE2,
        p_default, TRUE,
        p_ui, TYPE_SINGLECHEKBOX, IDC_CHK_ENABLE2,
    p_end,

    pb_channel2,  _T("channel2"), TYPE_INT,   P_ANIMATABLE, IDS_CHANNEL2,
        p_default, 2,
        p_range, 1, 99,
        p_ui, TYPE_SPINNER, EDITTYPE_INT, IDC_EDIT_CHANNEL2, IDC_SPIN_CHANNEL2, SPIN_AUTOSCALE,
    p_end,

    pb_axis2,     _T("axis2"),    TYPE_INT,   P_ANIMATABLE, IDS_AXIS2,
        p_default, Axis_Z,
        p_range, 0, 2,
        p_ui, TYPE_RADIO, 3, IDC_RAD_AXIS2_X, IDC_RAD_AXIS2_Y, IDC_RAD_AXIS2_Z,
    p_end,

    // ---- Channel 3 ---------------------------------------------------------
    pb_enable3,   _T("enable3"),  TYPE_BOOL,  P_ANIMATABLE, IDS_ENABLE3,
        p_default, TRUE,
        p_ui, TYPE_SINGLECHEKBOX, IDC_CHK_ENABLE3,
    p_end,

    pb_channel3,  _T("channel3"), TYPE_INT,   P_ANIMATABLE, IDS_CHANNEL3,
        p_default, 3,
        p_range, 1, 99,
        p_ui, TYPE_SPINNER, EDITTYPE_INT, IDC_EDIT_CHANNEL3, IDC_SPIN_CHANNEL3, SPIN_AUTOSCALE,
    p_end,

    pb_axis3,     _T("axis3"),    TYPE_INT,   P_ANIMATABLE, IDS_AXIS3,
        p_default, Axis_Y,
        p_range, 0, 2,
        p_ui, TYPE_RADIO, 3, IDC_RAD_AXIS3_X, IDC_RAD_AXIS3_Y, IDC_RAD_AXIS3_Z,
    p_end,

    // ---- Channel 4 ---------------------------------------------------------
    pb_enable4,   _T("enable4"),  TYPE_BOOL,  P_ANIMATABLE, IDS_ENABLE4,
        p_default, TRUE,
        p_ui, TYPE_SINGLECHEKBOX, IDC_CHK_ENABLE4,
    p_end,

    pb_channel4,  _T("channel4"), TYPE_INT,   P_ANIMATABLE, IDS_CHANNEL4,
        p_default, 4,
        p_range, 1, 99,
        p_ui, TYPE_SPINNER, EDITTYPE_INT, IDC_EDIT_CHANNEL4, IDC_SPIN_CHANNEL4, SPIN_AUTOSCALE,
    p_end,

    pb_axis4,     _T("axis4"),    TYPE_INT,   P_ANIMATABLE, IDS_AXIS4,
        p_default, Axis_Y,
        p_range, 0, 2,
        p_ui, TYPE_RADIO, 3, IDC_RAD_AXIS4_X, IDC_RAD_AXIS4_Y, IDC_RAD_AXIS4_Z,
    p_end,

    // ---- Global UV ---------------------------------------------------------
    pb_uTile,     _T("uTile"),    TYPE_FLOAT, P_ANIMATABLE, IDS_UTILE,
        p_default, 1.0f,
        p_range, -9999.0f, 9999.0f,
        p_ui, TYPE_SPINNER, EDITTYPE_FLOAT, IDC_EDIT_UTILE, IDC_SPIN_UTILE, SPIN_AUTOSCALE,
    p_end,

    pb_vTile,     _T("vTile"),    TYPE_FLOAT, P_ANIMATABLE, IDS_VTILE,
        p_default, 1.0f,
        p_range, -9999.0f, 9999.0f,
        p_ui, TYPE_SPINNER, EDITTYPE_FLOAT, IDC_EDIT_VTILE, IDC_SPIN_VTILE, SPIN_AUTOSCALE,
    p_end,

    pb_uOffset,   _T("uOffset"),  TYPE_FLOAT, P_ANIMATABLE, IDS_UOFFSET,
        p_default, 0.0f,
        p_range, -9999.0f, 9999.0f,
        p_ui, TYPE_SPINNER, EDITTYPE_FLOAT, IDC_EDIT_UOFFSET, IDC_SPIN_UOFFSET, SPIN_AUTOSCALE,
    p_end,

    pb_vOffset,   _T("vOffset"),  TYPE_FLOAT, P_ANIMATABLE, IDS_VOFFSET,
        p_default, 0.0f,
        p_range, -9999.0f, 9999.0f,
        p_ui, TYPE_SPINNER, EDITTYPE_FLOAT, IDC_EDIT_VOFFSET, IDC_SPIN_VOFFSET, SPIN_AUTOSCALE,
    p_end,

    pb_flipU,     _T("flipU"),    TYPE_BOOL,  P_ANIMATABLE, IDS_FLIPU,
        p_default, FALSE,
        p_ui, TYPE_SINGLECHEKBOX, IDC_CHK_FLIPU,
    p_end,

    pb_flipV,     _T("flipV"),    TYPE_BOOL,  P_ANIMATABLE, IDS_FLIPV,
        p_default, FALSE,
        p_ui, TYPE_SINGLECHEKBOX, IDC_CHK_FLIPV,
    p_end,

    pb_swapUV,    _T("swapUV"),   TYPE_BOOL,  P_ANIMATABLE, IDS_SWAPUV,
        p_default, FALSE,
        p_ui, TYPE_SINGLECHEKBOX, IDC_CHK_SWAPUV,
    p_end,

    // ---- Blend Output ------------------------------------------------------
    pb_enableBlend,  _T("enableBlend"),  TYPE_BOOL,  P_ANIMATABLE, IDS_ENABLE_BLEND,
        p_default, FALSE,
        p_ui, TYPE_SINGLECHEKBOX, IDC_CHK_ENABLE_BLEND,
    p_end,

    pb_channelBlend, _T("channelBlend"), TYPE_INT,   P_ANIMATABLE, IDS_CHANNEL_BLEND,
        p_default, 10,
        p_range, 1, 99,
        p_ui, TYPE_SPINNER, EDITTYPE_INT, IDC_EDIT_CHANNEL_BLEND, IDC_SPIN_CHANNEL_BLEND, SPIN_AUTOSCALE,
    p_end,

    pb_blendPower,   _T("blendPower"),   TYPE_FLOAT, P_ANIMATABLE, IDS_BLEND_POWER,
        p_default, 1.0f,
        p_range, 0.001f, 32.0f,
        p_ui, TYPE_SPINNER, EDITTYPE_FLOAT, IDC_EDIT_BLEND_POWER, IDC_SPIN_BLEND_POWER, SPIN_AUTOSCALE,
    p_end,

    p_end
);

// ---------------------------------------------------------------------------
// DLL entry points
// ---------------------------------------------------------------------------

BOOL WINAPI DllMain(HINSTANCE hinstDLL, ULONG fdwReason, LPVOID /*lpvReserved*/)
{
    if (fdwReason == DLL_PROCESS_ATTACH)
    {
        hInstance = hinstDLL;
        DisableThreadLibraryCalls(hInstance);
    }
    return TRUE;
}

extern "C"
{
    __declspec(dllexport) const TCHAR* LibDescription()
    {
        return _T("EZ Multi Planar UVW Modifier");
    }

    __declspec(dllexport) int LibNumberClasses()
    {
        return 1;
    }

    __declspec(dllexport) ClassDesc* LibClassDesc(int i)
    {
        return (i == 0) ? GetEZMultiPlanarUVWDesc() : nullptr;
    }

    __declspec(dllexport) ULONG LibVersion()
    {
        return VERSION_3DSMAX;
    }

    __declspec(dllexport) ULONG CanAutoDefer()
    {
        return 1;
    }
}
