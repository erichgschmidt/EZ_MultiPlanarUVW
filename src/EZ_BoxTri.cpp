/*
    EZ_BoxTri.cpp
    3ds Max 2023 C++ SDK  —  OSM modifier  —  .dlm

    4-layer box-triplanar UV authoring + normalised blend channel writer.

    Layers (fixed semantic roles, configurable per-layer mapping type):
        1 Wall X    (default Planar X — UV from Y/Z)
        2 Wall Y    (default Planar Y — UV from X/Z)
        3 Floor     (default Planar Z — UV from X/Y)
        4 Ceiling   (default Planar Z — UV from X/Y)

    Mapping type per layer (10 options):
        Planar X / Y / Z         — bounds-normalised planar
        Cyl Z / X / Y            — cylindrical around chosen axis (U=angle, V=along)
        Spherical                — around bounds centre
        World X / Y / Z          — raw world coords (tile = world units)

    Output channels:
        UV layer channels        — each enabled layer writes welded UV islands
        Blend ch (default 10)    — (R=WallX, G=WallY, B=Floor); ceiling = 1-R-G-B
        Preview ch (default 0)   — RGB by preview mode (5 selectable remaps)

    Blend math (4-component normalised):
        wx = pow(|N.x|, sharp) * wallXBias
        wy = pow(|N.y|, sharp) * wallYBias
        fl = pow(up,    sharp) * floorBias     ; up   = remap(max(+N.z,0), floorStart..1)
        ce = pow(down,  sharp) * ceilingBias   ; down = remap(max(-N.z,0), ceilStart..1)
        normalise so wx+wy+fl+ce = 1
        if hardSnap > 0 and max >= hardSnap: dominant gets 1.0, others 0
        Disabled layers contribute 0 bias.

    UV topology:
        Texture layer channels weld shared corners into islands where the final
        UVs match. Blend/preview channels stay face-corner so blend values remain
        constant per face and debug colour output cannot smear across faces.
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
#include <array>
#include <cmath>
#include <unordered_map>
#include <vector>

HINSTANCE hInstance = nullptr;

#define EZ_BOXTRI_CLASS_ID Class_ID(0x6d2f3a11, 0x1e0b7c50)

static const TCHAR* kPluginName   = _T("EZ BoxTri");
static const TCHAR* kCategoryName = _T("EZ Tools");

// ---------------------------------------------------------------------------
// Param IDs
// ---------------------------------------------------------------------------

enum ParamBlockIDs { kMainPBlock = 0 };

enum ParamIDs
{
    // Layer 1 = Wall X  (params 0-8)
    pb_en1=0, pb_ch1, pb_type1, pb_ut1, pb_vt1, pb_uoff1, pb_voff1, pb_flipU1, pb_flipV1,
    // Layer 2 = Wall Y  (9-17)
    pb_en2,   pb_ch2, pb_type2, pb_ut2, pb_vt2, pb_uoff2, pb_voff2, pb_flipU2, pb_flipV2,
    // Layer 3 = Floor   (18-26)
    pb_en3,   pb_ch3, pb_type3, pb_ut3, pb_vt3, pb_uoff3, pb_voff3, pb_flipU3, pb_flipV3,
    // Layer 4 = Ceiling (27-35)
    pb_en4,   pb_ch4, pb_type4, pb_ut4, pb_vt4, pb_uoff4, pb_voff4, pb_flipU4, pb_flipV4,
    // Blend math (36-45)
    pb_sharp, pb_invertZ,
    pb_wallXBias, pb_wallYBias, pb_floorBias, pb_ceilBias,
    pb_floorStart, pb_ceilStart,
    pb_hardDom, pb_hardThresh,
    // Output (46-49)
    pb_blendCh, pb_previewCh, pb_previewMode,
    pb_signedFix,
    pb_writePreview
};

static const int kLayerStride = 9;
static inline ParamID LParEn   (int r) { return (ParamID)(pb_en1    + r*kLayerStride); }
static inline ParamID LParCh   (int r) { return (ParamID)(pb_ch1    + r*kLayerStride); }
static inline ParamID LParType (int r) { return (ParamID)(pb_type1  + r*kLayerStride); }
static inline ParamID LParUT   (int r) { return (ParamID)(pb_ut1    + r*kLayerStride); }
static inline ParamID LParVT   (int r) { return (ParamID)(pb_vt1    + r*kLayerStride); }
static inline ParamID LParUOff (int r) { return (ParamID)(pb_uoff1  + r*kLayerStride); }
static inline ParamID LParVOff (int r) { return (ParamID)(pb_voff1  + r*kLayerStride); }
static inline ParamID LParFlipU(int r) { return (ParamID)(pb_flipU1 + r*kLayerStride); }
static inline ParamID LParFlipV(int r) { return (ParamID)(pb_flipV1 + r*kLayerStride); }

enum LayerRole { Role_WallX = 0, Role_WallY = 1, Role_Floor = 2, Role_Ceiling = 3 };

enum MappingType
{
    Map_PlanarX = 0, Map_PlanarY = 1, Map_PlanarZ = 2,
    Map_CylZ    = 3, Map_CylX    = 4, Map_CylY    = 5,
    Map_Sphere  = 6,
    Map_WorldX  = 7, Map_WorldY  = 8, Map_WorldZ  = 9,
    Map_Count   = 10
};

// ---------------------------------------------------------------------------
// Forward
// ---------------------------------------------------------------------------

class EZBoxTri;

// ---------------------------------------------------------------------------
// ClassDesc2
// ---------------------------------------------------------------------------

class EZBoxTriClassDesc : public ClassDesc2
{
public:
    int          IsPublic()              override { return TRUE; }
    void*        Create(BOOL = FALSE)    override;
    const TCHAR* ClassName()             override { return kPluginName; }
    const TCHAR* NonLocalizedClassName() override { return kPluginName; }
    SClass_ID    SuperClassID()          override { return OSM_CLASS_ID; }
    Class_ID     ClassID()               override { return EZ_BOXTRI_CLASS_ID; }
    const TCHAR* Category()              override { return kCategoryName; }
    const TCHAR* InternalName()          override { return _T("EZBoxTri"); }
    HINSTANCE    HInstance()             override { return hInstance; }
};

static EZBoxTriClassDesc g_EZBoxTriDesc;
ClassDesc2* GetEZBoxTriDesc() { return &g_EZBoxTriDesc; }

// ---------------------------------------------------------------------------
// Reset every param in a block to its descriptor default. Called from the
// modifier constructor so a freshly-applied modifier always starts at defaults
// (clones and scene loads overwrite these afterwards, so they keep their values).
// Handles the int/bool/float param types this modifier uses.
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
// DlgProc — populates the 4 Type comboboxes + Preview Mode combobox and
//           handles selection changes back to the param block.
// ---------------------------------------------------------------------------

static const TCHAR* kTypeLabels[Map_Count] = {
    _T("Planar X"), _T("Planar Y"), _T("Planar Z"),
    _T("Cyl Z"),    _T("Cyl X"),    _T("Cyl Y"),
    _T("Spherical"),
    _T("World X"),  _T("World Y"),  _T("World Z")
};
static const int kTypeComboIDs[4] = {
    IDC_COMBO_TYPE_1, IDC_COMBO_TYPE_2, IDC_COMBO_TYPE_3, IDC_COMBO_TYPE_4
};
static const ParamID kTypeParamIDs[4] = {
    (ParamID)pb_type1, (ParamID)pb_type2, (ParamID)pb_type3, (ParamID)pb_type4
};

static const TCHAR* kPreviewModeLabels[5] = {
    _T("SideX / SideY / Top"),
    _T("SideX / SideY / Bottom"),
    _T("Side / Top / Bottom"),
    _T("SideX / SideY / Top+Bot"),
    _T("Dominant Debug")
};

class EZBoxTriDlgProc : public ParamMap2UserDlgProc
{
    static void FillTypeCombo(HWND cb)
    {
        SendMessage(cb, CB_RESETCONTENT, 0, 0);
        for (int i = 0; i < Map_Count; ++i)
            SendMessage(cb, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(kTypeLabels[i]));
    }

    static void FillPrevCombo(HWND cb)
    {
        SendMessage(cb, CB_RESETCONTENT, 0, 0);
        for (int i = 0; i < 5; ++i)
            SendMessage(cb, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(kPreviewModeLabels[i]));
    }

    static void SyncTypeCombo(HWND hDlg, int r, IParamMap2* map)
    {
        if (!map || !map->GetParamBlock()) return;
        HWND cb = GetDlgItem(hDlg, kTypeComboIDs[r]);
        if (!cb) return;
        int val = 0;
        map->GetParamBlock()->GetValue(kTypeParamIDs[r], 0, val, FOREVER);
        SendMessage(cb, CB_SETCURSEL, (WPARAM)std::clamp(val, 0, Map_Count - 1), 0);
    }

    static void SyncPrevCombo(HWND hDlg, IParamMap2* map)
    {
        if (!map || !map->GetParamBlock()) return;
        HWND cb = GetDlgItem(hDlg, IDC_COMBO_PREVMODE);
        if (!cb) return;
        int val = 3; // 1-based default
        map->GetParamBlock()->GetValue((ParamID)pb_previewMode, 0, val, FOREVER);
        SendMessage(cb, CB_SETCURSEL, (WPARAM)std::clamp(val - 1, 0, 4), 0);
    }

public:
    INT_PTR DlgProc(TimeValue t, IParamMap2* map, HWND hWnd,
                    UINT msg, WPARAM wParam, LPARAM /*lParam*/) override
    {
        switch (msg)
        {
        case WM_INITDIALOG:
        {
            for (int r = 0; r < 4; ++r)
            {
                HWND cb = GetDlgItem(hWnd, kTypeComboIDs[r]);
                if (cb) { FillTypeCombo(cb); SyncTypeCombo(hWnd, r, map); }
            }
            HWND pcb = GetDlgItem(hWnd, IDC_COMBO_PREVMODE);
            if (pcb) { FillPrevCombo(pcb); SyncPrevCombo(hWnd, map); }
            break;
        }

        case WM_COMMAND:
            if (HIWORD(wParam) == CBN_SELCHANGE)
            {
                const int ctrlID = LOWORD(wParam);

                // Type combos
                for (int r = 0; r < 4; ++r)
                {
                    if (ctrlID != kTypeComboIDs[r]) continue;
                    HWND cb = GetDlgItem(hWnd, ctrlID);
                    int sel = (int)SendMessage(cb, CB_GETCURSEL, 0, 0);
                    if (sel >= 0 && map && map->GetParamBlock())
                        map->GetParamBlock()->SetValue(kTypeParamIDs[r], t, sel);
                    return TRUE;
                }

                // Preview mode
                if (ctrlID == IDC_COMBO_PREVMODE)
                {
                    HWND cb = GetDlgItem(hWnd, ctrlID);
                    int sel = (int)SendMessage(cb, CB_GETCURSEL, 0, 0);
                    if (sel >= 0 && map && map->GetParamBlock())
                        map->GetParamBlock()->SetValue((ParamID)pb_previewMode, t, sel + 1);
                    return TRUE;
                }
            }
            break;
        }
        return FALSE;
    }

    void DeleteThis() override {} // static instance
};

static EZBoxTriDlgProc g_DlgProc;

// ---------------------------------------------------------------------------
// Modifier class
// ---------------------------------------------------------------------------

class EZBoxTri : public Modifier
{
public:
    IParamBlock2* pblock = nullptr;

    EZBoxTri()  { g_EZBoxTriDesc.MakeAutoParamBlocks(this); ResetPB2ToDefaults(pblock); }
    ~EZBoxTri() override = default;

    // ---- Animatable --------------------------------------------------------
    SClass_ID   SuperClassID() override { return OSM_CLASS_ID; }
    Class_ID    ClassID()      override { return EZ_BOXTRI_CLASS_ID; }
    void        DeleteThis()   override { delete this; }
    int         NumSubs()      override { return 1; }
    Animatable* SubAnim(int i) override { return i == 0 ? pblock : nullptr; }
    TSTR        SubAnimName(int i, bool = true) override
    { return i == 0 ? TSTR(_T("Parameters")) : TSTR(_T("")); }

    // ---- ReferenceTarget ---------------------------------------------------
    RefTargetHandle Clone(RemapDir& remap) override
    {
        EZBoxTri* c = new EZBoxTri();
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
    IParamBlock2* GetParamBlockByID(BlockID id) override { return id == kMainPBlock ? pblock : nullptr; }

    // ---- BaseObject --------------------------------------------------------
    CreateMouseCallBack* GetCreateMouseCallBack() override { return nullptr; }
    const TCHAR* GetObjectName(bool) const override { return kPluginName; }

    void BeginEditParams(IObjParam* ip, ULONG flags, Animatable* prev) override
    { g_EZBoxTriDesc.BeginEditParams(ip, this, flags, prev); }
    void EndEditParams(IObjParam* ip, ULONG flags, Animatable* next) override
    { g_EZBoxTriDesc.EndEditParams(ip, this, flags, next); }

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
    // ========================================================================
    // Per-modifier-evaluation config struct (one param read per call)
    // ========================================================================
    struct LayerCfg
    {
        bool  enabled;
        int   channel;
        int   type;     // MappingType
        float uTile, vTile, uOff, vOff;
        bool  flipU, flipV;
    };

    struct Cfg
    {
        LayerCfg layer[4];
        float sharp;
        bool  invertZ;
        float biasX, biasY, biasFl, biasCe;
        float floorStart, ceilStart;
        bool  hardDom;
        float hardThresh;
        int   blendCh, previewCh, previewMode;
        bool  signedFix;
        bool  writePreview;
    };

    static int   ClampCh(int ch) { return std::clamp(ch, 1, 99); }
    static float SafeDim(float v) { return std::fabs(v) < 1e-3f ? 1.0f : v; }
    static float Clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }

    float PBf(ParamID id, TimeValue t, float def) const
    { float v = def; if (pblock) pblock->GetValue(id, t, v, FOREVER); return v; }
    int   PBi(ParamID id, TimeValue t, int   def) const
    { int   v = def; if (pblock) pblock->GetValue(id, t, v, FOREVER); return v; }
    BOOL  PBb(ParamID id, TimeValue t, BOOL  def) const
    { BOOL  v = def; if (pblock) pblock->GetValue(id, t, v, FOREVER); return v; }

    void ReadConfig(TimeValue t, Cfg& cfg) const
    {
        static const int defCh[4]   = { 1, 2, 3, 4 };
        static const int defType[4] = { Map_PlanarX, Map_PlanarY, Map_PlanarZ, Map_PlanarZ };

        for (int r = 0; r < 4; ++r)
        {
            cfg.layer[r].enabled = PBb(LParEn(r),   t, TRUE) != FALSE;
            cfg.layer[r].channel = ClampCh(PBi(LParCh(r), t, defCh[r]));
            cfg.layer[r].type    = std::clamp(PBi(LParType(r), t, defType[r]), 0, Map_Count - 1);
            cfg.layer[r].uTile   = PBf(LParUT(r),   t, 1.0f);
            cfg.layer[r].vTile   = PBf(LParVT(r),   t, 1.0f);
            cfg.layer[r].uOff    = PBf(LParUOff(r), t, 0.0f);
            cfg.layer[r].vOff    = PBf(LParVOff(r), t, 0.0f);
            cfg.layer[r].flipU   = PBb(LParFlipU(r), t, FALSE) != FALSE;
            cfg.layer[r].flipV   = PBb(LParFlipV(r), t, FALSE) != FALSE;
        }
        cfg.sharp       = std::max(0.001f, PBf((ParamID)pb_sharp,      t, 4.0f));
        cfg.invertZ     = PBb((ParamID)pb_invertZ,   t, FALSE) != FALSE;
        cfg.biasX       = PBf((ParamID)pb_wallXBias, t, 1.0f);
        cfg.biasY       = PBf((ParamID)pb_wallYBias, t, 1.0f);
        cfg.biasFl      = PBf((ParamID)pb_floorBias, t, 1.0f);
        cfg.biasCe      = PBf((ParamID)pb_ceilBias,  t, 1.0f);
        cfg.floorStart  = PBf((ParamID)pb_floorStart, t, 0.25f);
        cfg.ceilStart   = PBf((ParamID)pb_ceilStart,  t, 0.25f);
        cfg.hardDom     = PBb((ParamID)pb_hardDom,   t, FALSE) != FALSE;
        cfg.hardThresh  = PBf((ParamID)pb_hardThresh,t, 0.0f);
        cfg.blendCh     = ClampCh(PBi((ParamID)pb_blendCh, t, 10));
        cfg.previewCh   = std::clamp(PBi((ParamID)pb_previewCh, t, 0), 0, 99);
        cfg.previewMode = std::clamp(PBi((ParamID)pb_previewMode, t, 3), 1, 5);
        cfg.signedFix   = PBb((ParamID)pb_signedFix, t, TRUE) != FALSE;
        cfg.writePreview = PBb((ParamID)pb_writePreview, t, FALSE) != FALSE;
    }

    // ========================================================================
    // 4-component normalised blend (matches script's calc4WeightsFromNormal)
    // ========================================================================
    struct Weights4 { float wx, wy, fl, ce; };

    static Weights4 Calc4Weights(const Point3& nIn, const Cfg& cfg)
    {
        Point3 n = nIn;
        if (Length(n) < 1e-6f) n = Point3(0,0,1);
        else                   n = Normalize(n);
        if (cfg.invertZ) n.z = -n.z;

        const float upRaw   = std::max(n.z,  0.0f);
        const float downRaw = std::max(-n.z, 0.0f);

        float up   = 0.0f;
        float down = 0.0f;
        if (upRaw   > cfg.floorStart) up   = (upRaw   - cfg.floorStart) / (1.0f - cfg.floorStart);
        if (downRaw > cfg.ceilStart)  down = (downRaw - cfg.ceilStart)  / (1.0f - cfg.ceilStart);
        up   = Clamp01(up);
        down = Clamp01(down);

        // Disabled layers contribute zero
        const float biasX  = cfg.layer[Role_WallX].enabled    ? cfg.biasX  : 0.0f;
        const float biasY  = cfg.layer[Role_WallY].enabled    ? cfg.biasY  : 0.0f;
        const float biasFl = cfg.layer[Role_Floor].enabled    ? cfg.biasFl : 0.0f;
        const float biasCe = cfg.layer[Role_Ceiling].enabled  ? cfg.biasCe : 0.0f;

        float wx = std::pow(std::fabs(n.x), cfg.sharp) * biasX;
        float wy = std::pow(std::fabs(n.y), cfg.sharp) * biasY;
        float fl = std::pow(up,             cfg.sharp) * biasFl;
        float ce = std::pow(down,           cfg.sharp) * biasCe;

        const float sum = wx + wy + fl + ce;
        if (sum < 1e-6f) { wx = 1.0f; wy = 0.0f; fl = 0.0f; ce = 0.0f; }
        else             { wx /= sum; wy /= sum; fl /= sum; ce /= sum; }

        // hardSnap > 0: dominant wins entirely if max >= threshold
        // hardDom (bool): same idea, threshold ignored
        const float maxW = std::max(std::max(wx, wy), std::max(fl, ce));
        const bool  snap = cfg.hardDom || (cfg.hardThresh > 0.0f && maxW >= cfg.hardThresh);
        if (snap)
        {
            if      (maxW == wx) { wx = 1; wy = 0; fl = 0; ce = 0; }
            else if (maxW == wy) { wx = 0; wy = 1; fl = 0; ce = 0; }
            else if (maxW == fl) { wx = 0; wy = 0; fl = 1; ce = 0; }
            else                 { wx = 0; wy = 0; fl = 0; ce = 1; }
        }
        return { wx, wy, fl, ce };
    }

    static Point3 PreviewFromWeights(const Weights4& w, int mode)
    {
        switch (mode)
        {
        case 1: return Point3(w.wx,      w.wy,      w.fl);          // WallX/Y/Floor
        case 2: return Point3(w.wx,      w.wy,      w.ce);          // WallX/Y/Ceiling
        case 3: return Point3(w.wx+w.wy, w.fl,      w.ce);          // Wall/Floor/Ceiling
        case 4: return Point3(w.wx,      w.wy,      w.fl + w.ce);   // WallX/Y/Floor+Ceiling
        case 5:                                                     // Dominant debug
        {
            const float m = std::max(std::max(w.wx, w.wy), std::max(w.fl, w.ce));
            if      (m == w.wx) return Point3(1, 0, 0);
            else if (m == w.wy) return Point3(0, 1, 0);
            else if (m == w.fl) return Point3(0, 0, 1);
            else                return Point3(1, 1, 0); // yellow = ceiling
        }
        default: return Point3(w.wx+w.wy, w.fl, w.ce);
        }
    }

    // ========================================================================
    // Face normal (geometric, not averaged)
    // ========================================================================
    static Point3 FaceNormal(const Mesh& mesh, int f)
    {
        const Face& fc = mesh.faces[f];
        const Point3 e1 = mesh.verts[fc.v[1]] - mesh.verts[fc.v[0]];
        const Point3 e2 = mesh.verts[fc.v[2]] - mesh.verts[fc.v[0]];
        const Point3 n  = e1 ^ e2;
        const float  L  = Length(n);
        return L > 1e-6f ? n / L : Point3(0,0,1);
    }

    // ========================================================================
    // Mapping  —  raw UV from point, mapping-type-specific
    //
    // signedAxisFlipU: caller has already decided whether to flip-U for the
    // signed-axis no-mirror fix (only for Planar X/Y/Z modes); we still apply
    // it here at the end of the raw UV so it composes correctly with row-flip.
    // ========================================================================
    static Point3 UVFromMapping(
        const Point3& p, const Point3& mn, const Point3& mx, const Point3& centre,
        int type, bool signedFlipU)
    {
        const float sx = SafeDim(mx.x - mn.x);
        const float sy = SafeDim(mx.y - mn.y);
        const float sz = SafeDim(mx.z - mn.z);
        const float kTwoPi = 6.28318530718f;
        const float kPi    = 3.14159265359f;

        float u = 0.0f, v = 0.0f;

        switch (type)
        {
        case Map_PlanarX:  u = (p.y - mn.y) / sy; v = (p.z - mn.z) / sz; break;
        case Map_PlanarY:  u = (p.x - mn.x) / sx; v = (p.z - mn.z) / sz; break;
        case Map_PlanarZ:  u = (p.x - mn.x) / sx; v = (p.y - mn.y) / sy; break;

        case Map_CylZ:
        {
            const float rx = p.x - centre.x;
            const float ry = p.y - centre.y;
            u = std::atan2(ry, rx) / kTwoPi + 0.5f;
            v = (p.z - mn.z) / sz;
            break;
        }
        case Map_CylX:
        {
            const float ry = p.y - centre.y;
            const float rz = p.z - centre.z;
            u = std::atan2(rz, ry) / kTwoPi + 0.5f;
            v = (p.x - mn.x) / sx;
            break;
        }
        case Map_CylY:
        {
            const float rx = p.x - centre.x;
            const float rz = p.z - centre.z;
            u = std::atan2(rx, rz) / kTwoPi + 0.5f;
            v = (p.y - mn.y) / sy;
            break;
        }
        case Map_Sphere:
        {
            const Point3 r = p - centre;
            const float  L = Length(r);
            Point3 rn = L > 1e-6f ? r / L : Point3(0,0,1);
            u = std::atan2(rn.y, rn.x) / kTwoPi + 0.5f;
            v = std::asin(std::clamp(rn.z, -1.0f, 1.0f)) / kPi + 0.5f;
            break;
        }
        case Map_WorldX:   u = p.y; v = p.z; break;
        case Map_WorldY:   u = p.x; v = p.z; break;
        case Map_WorldZ:   u = p.x; v = p.y; break;
        }

        if (signedFlipU) u = 1.0f - u;
        return Point3(u, v, 0.0f);
    }

    // Returns true if the layer's mapping is planar (X/Y/Z) and should respect
    // the signed-axis fix.
    static bool   IsPlanarBounded(int type) { return type >= Map_PlanarX && type <= Map_PlanarZ; }
    static int    PlanarAxisOf(int type)    { return type - Map_PlanarX; } // 0,1,2 = X,Y,Z

    // ========================================================================
    // Channel-write helper
    // ========================================================================
    // ch in [-2, 99]:
    //   -2 = alpha, -1 = illum, 0 = vertex color (all always present)
    //    1..99 = standard map channels (need setNumMaps + mapSupport)
    void EnsureChannel(Mesh& mesh, int ch) const
    {
        if (ch < 1) return;  // negative + 0 are always available
        if (ch > 99) return;
        const int needed = ch + 1;
        if (mesh.getNumMaps() < needed) mesh.setNumMaps(needed, TRUE);
        if (!mesh.mapSupport(ch)) mesh.setMapSupport(ch, TRUE);
    }

    // Allocate face-corner-unwelded storage: 3 unique map-verts per face,
    // mapVert index = face * 3 + corner.
    //
    // Channel 0 uses the legacy vertCol / vcFace arrays — Mesh::Map(0) wraps
    // them but setNumMapVerts(0, N) doesn't reliably allocate vertCol storage,
    // so the unified API silently no-ops. Drop to the explicit setNumVertCol /
    // setNumVCFaces calls for ch 0.
    void PrepareFaceCornerChannel(Mesh& mesh, int ch) const
    {
        if (ch < 0 || ch > 99) return;
        const int nv = mesh.numFaces * 3;

        if (ch == 0)
        {
            mesh.setNumVertCol(nv, FALSE);
            mesh.setNumVCFaces(mesh.numFaces, FALSE);
            for (int f = 0; f < mesh.numFaces; ++f)
            {
                mesh.vcFace[f].t[0] = f * 3;
                mesh.vcFace[f].t[1] = f * 3 + 1;
                mesh.vcFace[f].t[2] = f * 3 + 2;
            }
            return;
        }

        EnsureChannel(mesh, ch);
        mesh.setNumMapVerts(ch, nv, FALSE);
        mesh.setNumMapFaces(ch, mesh.numFaces, FALSE);
        MeshMap& map = mesh.Map(ch);
        for (int f = 0; f < mesh.numFaces; ++f)
        {
            map.tf[f].t[0] = f * 3;
            map.tf[f].t[1] = f * 3 + 1;
            map.tf[f].t[2] = f * 3 + 2;
        }
    }

    struct UVIslandKey
    {
        DWORD v;
        long long u;
        long long w;

        bool operator==(const UVIslandKey& o) const
        {
            return v == o.v && u == o.u && w == o.w;
        }
    };

    struct UVIslandKeyHash
    {
        size_t operator()(const UVIslandKey& k) const
        {
            size_t h = std::hash<DWORD>{}(k.v);
            h ^= std::hash<long long>{}(k.u) + 0x9e3779b9u + (h << 6) + (h >> 2);
            h ^= std::hash<long long>{}(k.w) + 0x9e3779b9u + (h << 6) + (h >> 2);
            return h;
        }
    };

    static long long QuantizeUV(float v)
    {
        return (long long)std::llround((double)v * 1000000.0);
    }

    static Point3 FinalLayerUV(
        const Mesh& mesh, const Face& fc, int corner,
        const Point3& mn, const Point3& mx, const Point3& centre,
        const LayerCfg& L, bool signedFlipU)
    {
        const Point3& p = mesh.verts[fc.v[corner]];
        Point3 uv = UVFromMapping(p, mn, mx, centre, L.type, signedFlipU);
        if (L.flipU) uv.x = 1.0f - uv.x;
        if (L.flipV) uv.y = 1.0f - uv.y;
        uv.x = uv.x * L.uTile + L.uOff;
        uv.y = uv.y * L.vTile + L.vOff;
        return uv;
    }

    bool FaceIndicesValid(const Mesh& mesh, const Face& fc) const
    {
        const DWORD nv = (DWORD)mesh.numVerts;
        return fc.v[0] < nv && fc.v[1] < nv && fc.v[2] < nv;
    }

    // Texture layers should appear as usable UV islands in Unwrap UVW, not as
    // disconnected per-triangle shells. Weld by original mesh vertex plus final
    // UV, preserving seams where signed-axis flips or mapping changes produce a
    // different coordinate at the same vertex.
    void BuildLayerIslandChannel(
        Mesh& mesh, int ch, const LayerCfg& L,
        const Point3& mn, const Point3& mx, const Point3& centre,
        bool signedFix) const
    {
        if (ch < 1 || ch > 99) return;

        EnsureChannel(mesh, ch);
        mesh.setNumMapFaces(ch, mesh.numFaces, FALSE);
        MeshMap& map = mesh.Map(ch);
        if (!map.tf) return;

        std::vector<Point3> mapVerts;
        mapVerts.reserve((size_t)std::max(mesh.numVerts, 1));
        std::unordered_map<UVIslandKey, DWORD, UVIslandKeyHash> lookup;
        lookup.reserve((size_t)std::max(mesh.numVerts * 2, 8));

        for (int f = 0; f < mesh.numFaces; ++f)
        {
            const Face& fc = mesh.faces[f];
            if (!FaceIndicesValid(mesh, fc))
            {
                map.tf[f].t[0] = map.tf[f].t[1] = map.tf[f].t[2] = 0;
                continue;
            }

            const Point3 fN = FaceNormal(mesh, f);
            bool signedFlipU = false;
            if (signedFix && IsPlanarBounded(L.type))
            {
                const int axis = PlanarAxisOf(L.type);
                const float comp = (axis == 0) ? fN.x : (axis == 1) ? fN.y : fN.z;
                signedFlipU = comp < 0.0f;
            }

            for (int c = 0; c < 3; ++c)
            {
                const Point3 uv = FinalLayerUV(mesh, fc, c, mn, mx, centre, L, signedFlipU);
                const UVIslandKey key{ fc.v[c], QuantizeUV(uv.x), QuantizeUV(uv.y) };

                auto it = lookup.find(key);
                if (it == lookup.end())
                {
                    const DWORD idx = (DWORD)mapVerts.size();
                    mapVerts.push_back(uv);
                    it = lookup.emplace(key, idx).first;
                }
                map.tf[f].t[c] = it->second;
            }
        }

        if (mapVerts.empty())
            mapVerts.push_back(Point3(0, 0, 0));

        mesh.setNumMapVerts(ch, (int)mapVerts.size(), FALSE);
        MeshMap& finalMap = mesh.Map(ch);
        if (!finalMap.tv) return;
        for (int i = 0; i < (int)mapVerts.size(); ++i)
            finalMap.tv[i] = mapVerts[(size_t)i];
    }

    // ========================================================================
    // Top-level apply
    // ========================================================================
    void Apply(TimeValue t, Mesh& mesh) const
    {
        Cfg cfg;
        ReadConfig(t, cfg);

        // Bounds + centre (one sweep)
        Box3 bounds; bounds.Init();
        for (int i = 0; i < mesh.numVerts; ++i) bounds += mesh.verts[i];
        if (bounds.IsEmpty()) { bounds += Point3(0,0,0); bounds += Point3(1,1,1); }
        const Point3 mn = bounds.Min();
        const Point3 mx = bounds.Max();
        const Point3 centre = (mn + mx) * 0.5f;

        // Texture layers are welded into UV islands. Blend/preview remain
        // face-corner because they are per-face diagnostic/blend payloads.
        for (int r = 0; r < 4; ++r)
            if (cfg.layer[r].enabled)
                BuildLayerIslandChannel(mesh, cfg.layer[r].channel, cfg.layer[r],
                                        mn, mx, centre, cfg.signedFix);
        PrepareFaceCornerChannel(mesh, cfg.blendCh);
        // Preview (vertex-colour-style RGB) is OFF by default: only prepared and
        // written when Write Preview is enabled, so the mapper doesn't paint
        // vertex colours onto the model unless the user asks. The blend channel
        // (ch10) is always written regardless.
        if (cfg.writePreview)
            PrepareFaceCornerChannel(mesh, cfg.previewCh);

        // Cache channel/map refs to avoid Mesh::Map() lookups in the inner loop.
        // If preview/blend share a channel with a layer or with each other,
        // the later write wins — intentional, user can pick distinct channels.
        MeshMap* blendMap   = &mesh.Map(cfg.blendCh);
        const bool previewIsVC = (cfg.previewCh == 0);
        MeshMap* previewMap = (cfg.writePreview && !previewIsVC) ? &mesh.Map(cfg.previewCh) : nullptr;

        // Per-face main loop
        for (int f = 0; f < mesh.numFaces; ++f)
        {
            const Face&  fc  = mesh.faces[f];
            const Point3 fN  = FaceNormal(mesh, f);
            const Weights4 w = Calc4Weights(fN, cfg);
            const Point3 blendUV = Point3(w.wx, w.wy, w.fl);
            const Point3 prevUV  = PreviewFromWeights(w, cfg.previewMode);

            const int mvBase = f * 3;

#if 0
            // Per-layer UV write
            for (int r = 0; r < 4; ++r)
            {
                if (!cfg.layer[r].enabled) continue;
                const LayerCfg& L = cfg.layer[r];

                // Signed-axis no-mirror: per face, if normal is negative on
                // this layer's planar axis, flip U so adjacent faces on the
                // opposite side don't mirror.
                bool signedFlipU = false;
                if (cfg.signedFix && IsPlanarBounded(L.type))
                {
                    const int axis = PlanarAxisOf(L.type);
                    const float comp = (axis == 0) ? fN.x : (axis == 1) ? fN.y : fN.z;
                    if (comp < 0.0f) signedFlipU = true;
                }

                for (int c = 0; c < 3; ++c)
                {
                    const Point3& p = mesh.verts[fc.v[c]];
                    Point3 uv = UVFromMapping(p, mn, mx, centre, L.type, signedFlipU);
                    if (L.flipU) uv.x = 1.0f - uv.x;
                    if (L.flipV) uv.y = 1.0f - uv.y;
                    uv.x = uv.x * L.uTile + L.uOff;
                    uv.y = uv.y * L.vTile + L.vOff;
                    layerMap[r]->tv[mvBase + c] = uv;
                }
            }

            // Blend ch — constant per face (kills intra-face gradient)
#endif
            blendMap->tv[mvBase    ] = blendUV;
            blendMap->tv[mvBase + 1] = blendUV;
            blendMap->tv[mvBase + 2] = blendUV;

            // Preview ch — only when Write Preview is enabled (ch 0 routed to
            // legacy vertCol). Off by default so vertex colours aren't painted.
            if (cfg.writePreview)
            {
                if (previewIsVC)
                {
                    mesh.vertCol[mvBase    ] = prevUV;
                    mesh.vertCol[mvBase + 1] = prevUV;
                    mesh.vertCol[mvBase + 2] = prevUV;
                }
                else if (previewMap)
                {
                    previewMap->tv[mvBase    ] = prevUV;
                    previewMap->tv[mvBase + 1] = prevUV;
                    previewMap->tv[mvBase + 2] = prevUV;
                }
            }
        }
    }
};

// ---------------------------------------------------------------------------
// ClassDesc2::Create
// ---------------------------------------------------------------------------

void* EZBoxTriClassDesc::Create(BOOL) { return new EZBoxTri(); }

// ---------------------------------------------------------------------------
// ParamBlockDesc2 macro — one layer at a time
// ---------------------------------------------------------------------------

#define LAYER_PARAMS(N, defCh, defType)                                                              \
    pb_en##N,    _T("en"   #N), TYPE_BOOL,  P_ANIMATABLE, IDS_EN##N,                                \
        p_default, TRUE,                                                                              \
        p_ui, TYPE_SINGLECHEKBOX, IDC_CHK_EN_##N,                                                    \
    p_end,                                                                                            \
    pb_ch##N,    _T("ch"   #N), TYPE_INT,   P_ANIMATABLE, IDS_CH##N,                                \
        p_default, defCh, p_range, 1, 99,                                                            \
        p_ui, TYPE_SPINNER, EDITTYPE_INT,   IDC_EDIT_CH_##N,   IDC_SPIN_CH_##N,   SPIN_AUTOSCALE,   \
    p_end,                                                                                            \
    pb_type##N,  _T("type" #N), TYPE_INT,   P_ANIMATABLE, IDS_TYPE##N,                              \
        p_default, defType, p_range, 0, Map_Count - 1,                                              \
    p_end,                                                                                            \
    pb_ut##N,    _T("ut"   #N), TYPE_FLOAT, P_ANIMATABLE, IDS_UT##N,                                \
        p_default, 1.0f, p_range, -9999.0f, 9999.0f,                                                \
        p_ui, TYPE_SPINNER, EDITTYPE_FLOAT, IDC_EDIT_UT_##N,   IDC_SPIN_UT_##N,   SPIN_AUTOSCALE,   \
    p_end,                                                                                            \
    pb_vt##N,    _T("vt"   #N), TYPE_FLOAT, P_ANIMATABLE, IDS_VT##N,                                \
        p_default, 1.0f, p_range, -9999.0f, 9999.0f,                                                \
        p_ui, TYPE_SPINNER, EDITTYPE_FLOAT, IDC_EDIT_VT_##N,   IDC_SPIN_VT_##N,   SPIN_AUTOSCALE,   \
    p_end,                                                                                            \
    pb_uoff##N,  _T("uoff" #N), TYPE_FLOAT, P_ANIMATABLE, IDS_UOFF##N,                              \
        p_default, 0.0f, p_range, -9999.0f, 9999.0f,                                                \
        p_ui, TYPE_SPINNER, EDITTYPE_FLOAT, IDC_EDIT_UOFF_##N, IDC_SPIN_UOFF_##N, SPIN_AUTOSCALE,   \
    p_end,                                                                                            \
    pb_voff##N,  _T("voff" #N), TYPE_FLOAT, P_ANIMATABLE, IDS_VOFF##N,                              \
        p_default, 0.0f, p_range, -9999.0f, 9999.0f,                                                \
        p_ui, TYPE_SPINNER, EDITTYPE_FLOAT, IDC_EDIT_VOFF_##N, IDC_SPIN_VOFF_##N, SPIN_AUTOSCALE,   \
    p_end,                                                                                            \
    pb_flipU##N, _T("flipU"#N), TYPE_BOOL,  P_ANIMATABLE, IDS_FLIPU##N,                             \
        p_default, FALSE, p_ui, TYPE_SINGLECHEKBOX, IDC_CHK_FLIPU_##N,                              \
    p_end,                                                                                            \
    pb_flipV##N, _T("flipV"#N), TYPE_BOOL,  P_ANIMATABLE, IDS_FLIPV##N,                             \
        p_default, FALSE, p_ui, TYPE_SINGLECHEKBOX, IDC_CHK_FLIPV_##N,                              \
    p_end,

static ParamBlockDesc2 g_MainPBlock
(
    kMainPBlock, _T("params"), IDS_PARAMS, &g_EZBoxTriDesc,
    P_AUTO_CONSTRUCT | P_AUTO_UI,
    0,
    IDD_PANEL, IDS_PARAMS, 0, 0, &g_DlgProc,

    LAYER_PARAMS(1, 1, Map_PlanarX)   // Wall X
    LAYER_PARAMS(2, 2, Map_PlanarY)   // Wall Y
    LAYER_PARAMS(3, 3, Map_PlanarZ)   // Floor
    LAYER_PARAMS(4, 4, Map_PlanarZ)   // Ceiling

    // Blend math
    pb_sharp, _T("sharp"), TYPE_FLOAT, P_ANIMATABLE, IDS_SHARP,
        p_default, 4.0f, p_range, 0.5f, 16.0f,
        p_ui, TYPE_SPINNER, EDITTYPE_FLOAT, IDC_EDIT_SHARP, IDC_SPIN_SHARP, SPIN_AUTOSCALE,
    p_end,
    pb_invertZ, _T("invertZ"), TYPE_BOOL, P_ANIMATABLE, IDS_INVERTZ,
        p_default, FALSE, p_ui, TYPE_SINGLECHEKBOX, IDC_CHK_INVERTZ,
    p_end,
    pb_wallXBias, _T("wallXBias"), TYPE_FLOAT, P_ANIMATABLE, IDS_WALLXBIAS,
        p_default, 1.0f, p_range, 0.01f, 20.0f,
        p_ui, TYPE_SPINNER, EDITTYPE_FLOAT, IDC_EDIT_WALLXBIAS, IDC_SPIN_WALLXBIAS, SPIN_AUTOSCALE,
    p_end,
    pb_wallYBias, _T("wallYBias"), TYPE_FLOAT, P_ANIMATABLE, IDS_WALLYBIAS,
        p_default, 1.0f, p_range, 0.01f, 20.0f,
        p_ui, TYPE_SPINNER, EDITTYPE_FLOAT, IDC_EDIT_WALLYBIAS, IDC_SPIN_WALLYBIAS, SPIN_AUTOSCALE,
    p_end,
    pb_floorBias, _T("floorBias"), TYPE_FLOAT, P_ANIMATABLE, IDS_FLOORBIAS,
        p_default, 1.0f, p_range, 0.01f, 20.0f,
        p_ui, TYPE_SPINNER, EDITTYPE_FLOAT, IDC_EDIT_FLOORBIAS, IDC_SPIN_FLOORBIAS, SPIN_AUTOSCALE,
    p_end,
    pb_ceilBias, _T("ceilBias"), TYPE_FLOAT, P_ANIMATABLE, IDS_CEILBIAS,
        p_default, 1.0f, p_range, 0.01f, 20.0f,
        p_ui, TYPE_SPINNER, EDITTYPE_FLOAT, IDC_EDIT_CEILBIAS, IDC_SPIN_CEILBIAS, SPIN_AUTOSCALE,
    p_end,
    pb_floorStart, _T("floorStart"), TYPE_FLOAT, P_ANIMATABLE, IDS_FLOORSTART,
        p_default, 0.25f, p_range, 0.0f, 0.95f,
        p_ui, TYPE_SPINNER, EDITTYPE_FLOAT, IDC_EDIT_FLOORSTART, IDC_SPIN_FLOORSTART, SPIN_AUTOSCALE,
    p_end,
    pb_ceilStart, _T("ceilStart"), TYPE_FLOAT, P_ANIMATABLE, IDS_CEILSTART,
        p_default, 0.25f, p_range, 0.0f, 0.95f,
        p_ui, TYPE_SPINNER, EDITTYPE_FLOAT, IDC_EDIT_CEILSTART, IDC_SPIN_CEILSTART, SPIN_AUTOSCALE,
    p_end,
    pb_hardDom, _T("hardDom"), TYPE_BOOL, P_ANIMATABLE, IDS_HARDDOM,
        p_default, FALSE, p_ui, TYPE_SINGLECHEKBOX, IDC_CHK_HARDDOM,
    p_end,
    pb_hardThresh, _T("hardThresh"), TYPE_FLOAT, P_ANIMATABLE, IDS_HARDTHRESH,
        p_default, 0.0f, p_range, 0.0f, 1.0f,
        p_ui, TYPE_SPINNER, EDITTYPE_FLOAT, IDC_EDIT_HARDTHRESH, IDC_SPIN_HARDTHRESH, SPIN_AUTOSCALE,
    p_end,

    // Output
    pb_blendCh, _T("blendCh"), TYPE_INT, P_ANIMATABLE, IDS_BLENDCH,
        p_default, 10, p_range, 1, 99,
        p_ui, TYPE_SPINNER, EDITTYPE_INT, IDC_EDIT_BLENDCH, IDC_SPIN_BLENDCH, SPIN_AUTOSCALE,
    p_end,
    pb_previewCh, _T("previewCh"), TYPE_INT, P_ANIMATABLE, IDS_PREVIEWCH,
        p_default, 0, p_range, 0, 99,
        p_ui, TYPE_SPINNER, EDITTYPE_INT, IDC_EDIT_PREVIEWCH, IDC_SPIN_PREVIEWCH, SPIN_AUTOSCALE,
    p_end,
    pb_previewMode, _T("previewMode"), TYPE_INT, P_ANIMATABLE, IDS_PREVIEWMODE,
        p_default, 3, p_range, 1, 5,
    p_end,
    pb_signedFix, _T("signedFix"), TYPE_BOOL, P_ANIMATABLE, IDS_SIGNEDFIX,
        p_default, TRUE, p_ui, TYPE_SINGLECHEKBOX, IDC_CHK_SIGNEDFIX,
    p_end,
    pb_writePreview, _T("writePreview"), TYPE_BOOL, P_ANIMATABLE, IDS_WRITEPREVIEW,
        p_default, FALSE, p_ui, TYPE_SINGLECHEKBOX, IDC_CHK_WRITEPREVIEW,
    p_end,

    p_end
);

// ---------------------------------------------------------------------------
// DLL entry points
// ---------------------------------------------------------------------------

BOOL WINAPI DllMain(HINSTANCE hinstDLL, ULONG fdwReason, LPVOID)
{
    if (fdwReason == DLL_PROCESS_ATTACH)
    {
        hInstance = hinstDLL;
        DisableThreadLibraryCalls(hInstance);
    }
    return TRUE;
}

// Companion modifier classes live in EZ_AO.cpp, EZ_Masks.cpp, EZ_Smooth.cpp
extern ClassDesc2* GetEZBoxTriAODesc();
extern ClassDesc2* GetEZMasksDesc();
extern ClassDesc2* GetEZSmoothDesc();

extern "C"
{
    __declspec(dllexport) const TCHAR* LibDescription()    { return _T("EZ BoxTri Triplanar + AO + Masks + Smoothing"); }
    __declspec(dllexport) int          LibNumberClasses()  { return 4; }
    __declspec(dllexport) ClassDesc*   LibClassDesc(int i)
    {
        switch (i)
        {
        case 0:  return GetEZBoxTriDesc();
        case 1:  return GetEZBoxTriAODesc();
        case 2:  return GetEZMasksDesc();
        case 3:  return GetEZSmoothDesc();
        default: return nullptr;
        }
    }
    __declspec(dllexport) ULONG        LibVersion()        { return VERSION_3DSMAX; }
    __declspec(dllexport) ULONG        CanAutoDefer()      { return 1; }
}
