/*
    EZMultiPlanarUVW.cpp
    3ds Max 2023 C++ SDK  —  OSM modifier  —  .dlm

    Six signed planar projections (X+/X-/Y+/Y-/Z+/Z-) written into grouped
    map channels.  Multiple rows can share a channel; per-face normals decide
    which projection wins.  Seam-split at projection boundaries.

    Each row has its own U/V tile.  Global U/V offset + flip/swap applies after.

    Blend output channel: per-vertex triplanar weights (R=X,G=Y,B=Z) for
    material blending.  Optional viewport colour overlay via Display().
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
#include <array>
#include <cmath>
#include <map>
#include <unordered_map>
#include <vector>

HINSTANCE hInstance = nullptr;

#define EZ_MULTIPLANAR_UVW_CLASS_ID Class_ID(0x6d2f3a11, 0x1e0b7c44)

static const TCHAR* kPluginName   = _T("EZ Multi Planar UVW");
static const TCHAR* kCategoryName = _T("EZ Tools");

// ---------------------------------------------------------------------------
// Param IDs  (6 rows × 5 params + global UV + seam options + blend output)
// ---------------------------------------------------------------------------

enum ParamBlockIDs { kMainPBlock = 0 };

enum ParamIDs
{
    // Row 1
    pb_en1 = 0, pb_ch1, pb_proj1, pb_uTile1, pb_vTile1,
    // Row 2
    pb_en2,     pb_ch2, pb_proj2, pb_uTile2, pb_vTile2,
    // Row 3
    pb_en3,     pb_ch3, pb_proj3, pb_uTile3, pb_vTile3,
    // Row 4
    pb_en4,     pb_ch4, pb_proj4, pb_uTile4, pb_vTile4,
    // Row 5
    pb_en5,     pb_ch5, pb_proj5, pb_uTile5, pb_vTile5,
    // Row 6
    pb_en6,     pb_ch6, pb_proj6, pb_uTile6, pb_vTile6,
    // Global UV
    pb_uOffset, pb_vOffset, pb_flipU, pb_flipV, pb_swapUV,
    // Seam options
    pb_normalThreshold, pb_mergeIslands, pb_parkNonMatching, pb_parkU, pb_parkV,
    // Blend output
    pb_enableBlend, pb_channelBlend, pb_blendPower, pb_showBlend
};

// Row param base IDs (pb_en1 = 0, stride = 5)
static const int kRowStride = 5;
static inline ParamID RowEn   (int r) { return (ParamID)(pb_en1    + r * kRowStride); }
static inline ParamID RowCh   (int r) { return (ParamID)(pb_ch1    + r * kRowStride); }
static inline ParamID RowProj (int r) { return (ParamID)(pb_proj1  + r * kRowStride); }
static inline ParamID RowUTile(int r) { return (ParamID)(pb_uTile1 + r * kRowStride); }
static inline ParamID RowVTile(int r) { return (ParamID)(pb_vTile1 + r * kRowStride); }

// ---------------------------------------------------------------------------
// Forward declarations
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
    const TCHAR* ClassName()             override { return kPluginName; }
    const TCHAR* NonLocalizedClassName() override { return kPluginName; }
    SClass_ID    SuperClassID()          override { return OSM_CLASS_ID; }
    Class_ID     ClassID()               override { return EZ_MULTIPLANAR_UVW_CLASS_ID; }
    const TCHAR* Category()              override { return kCategoryName; }
    const TCHAR* InternalName()          override { return _T("EZMultiPlanarUVW"); }
    HINSTANCE    HInstance()             override { return hInstance; }
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

    struct BlendDisplayCache
    {
        std::vector<Point3> verts;
        std::vector<Point3> colors;
        std::vector<int>    indices;
        bool valid = false;
    };
    mutable BlendDisplayCache m_displayCache;

    EZMultiPlanarUVW()  { g_EZMultiPlanarUVWDesc.MakeAutoParamBlocks(this); }
    ~EZMultiPlanarUVW() override = default;

    // ---- Animatable --------------------------------------------------------
    void        DeleteThis() override { delete this; }
    int         NumSubs()    override { return 1; }
    Animatable* SubAnim(int i) override { return (i == 0) ? pblock : nullptr; }
    TSTR        SubAnimName(int i, bool = true) override
    {
        return (i == 0) ? TSTR(_T("Parameters")) : TSTR(_T(""));
    }

    // ---- ReferenceTarget ---------------------------------------------------
    RefTargetHandle Clone(RemapDir& remap) override
    {
        EZMultiPlanarUVW* c = new EZMultiPlanarUVW();
        if (pblock) c->ReplaceReference(0, remap.CloneRef(pblock));
        BaseClone(this, c, remap);
        return c;
    }
    int             NumRefs()           override { return 1; }
    RefTargetHandle GetReference(int i) override { return (i == 0) ? pblock : nullptr; }
    void            SetReference(int i, RefTargetHandle r) override
    {
        if (i == 0) pblock = static_cast<IParamBlock2*>(r);
    }
    RefResult NotifyRefChanged(const Interval&, RefTargetHandle, PartID&,
                               RefMessage msg, BOOL) override
    {
        if (msg == REFMSG_CHANGE)
            NotifyDependents(FOREVER, PART_TEXMAP, REFMSG_CHANGE);
        return REF_SUCCEED;
    }

    // ---- Param block access ------------------------------------------------
    int           NumParamBlocks()              override { return 1; }
    IParamBlock2* GetParamBlock(int i)          override { return (i == 0) ? pblock : nullptr; }
    IParamBlock2* GetParamBlockByID(BlockID id) override { return (id == kMainPBlock) ? pblock : nullptr; }

    // ---- BaseObject --------------------------------------------------------
    CreateMouseCallBack* GetCreateMouseCallBack() override { return nullptr; }
    const TCHAR* GetObjectName(bool) const override { return kPluginName; }

    void BeginEditParams(IObjParam* ip, ULONG flags, Animatable* prev) override
    {
        g_EZMultiPlanarUVWDesc.BeginEditParams(ip, this, flags, prev);
    }
    void EndEditParams(IObjParam* ip, ULONG flags, Animatable* next) override
    {
        g_EZMultiPlanarUVWDesc.EndEditParams(ip, this, flags, next);
    }

    // ---- Viewport display --------------------------------------------------
    void GetWorldBoundBox(TimeValue t, INode* inode, ViewExp*, Box3& box) override
    {
        if (!m_displayCache.valid) return;
        Matrix3 tm = inode->GetObjectTM(t);
        for (const auto& v : m_displayCache.verts) box += v * tm;
    }

    int Display(TimeValue t, INode* inode, ViewExp* vpt, int) override
    {
        if (!pblock || !m_displayCache.valid) return 0;
        BOOL show = FALSE;
        pblock->GetValue(pb_showBlend, t, show, FOREVER);
        if (!show) return 0;

        GraphicsWindow* gw = vpt->getGW();
        gw->setTransform(inode->GetObjectTM(t));
        const DWORD saved = gw->getRndLimits();
        gw->setRndLimits(GW_Z_BUFFER | GW_FLAT);

        const int nf = (int)m_displayCache.indices.size() / 3;
        Point3 uvw[3] = {};
        for (int f = 0; f < nf; ++f)
        {
            const int i0 = m_displayCache.indices[f*3];
            const int i1 = m_displayCache.indices[f*3+1];
            const int i2 = m_displayCache.indices[f*3+2];
            const Point3 col = (m_displayCache.colors[i0] +
                                m_displayCache.colors[i1] +
                                m_displayCache.colors[i2]) / 3.0f;
            gw->setColor(FILL_COLOR, col.x, col.y, col.z);
            Point3 pts[3] = { m_displayCache.verts[i0],
                              m_displayCache.verts[i1],
                              m_displayCache.verts[i2] };
            gw->triangle(pts, uvw);
        }
        gw->setRndLimits(saved);
        return 1;
    }

    // ---- Modifier ----------------------------------------------------------
    ChannelMask ChannelsUsed()   override { return GEOM_CHANNEL | TOPO_CHANNEL | TEXMAP_CHANNEL; }
    ChannelMask ChannelsChanged() override { return TEXMAP_CHANNEL; }
    Class_ID    InputType()      override { return Class_ID(TRIOBJ_CLASS_ID, 0); }

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
        ApplyAll(t, mesh);
        mesh.InvalidateGeomCache();
        mesh.InvalidateTopologyCache();
        tri->UpdateValidity(TEXMAP_CHAN_NUM, LocalValidity(t));
    }

private:
    // ========================================================================
    // Helpers
    // ========================================================================

    static int   ClampCh(int ch) { return std::clamp(ch, 1, 99); }
    static float SafeRange(float v) { return (std::fabs(v) < 1e-6f) ? 1.0f : v; }

    float  PBf(ParamID id, TimeValue t, float  def) const { float  v=def; if(pblock) pblock->GetValue(id,t,v,FOREVER); return v; }
    int    PBi(ParamID id, TimeValue t, int    def) const { int    v=def; if(pblock) pblock->GetValue(id,t,v,FOREVER); return v; }
    BOOL   PBb(ParamID id, TimeValue t, BOOL   def) const { BOOL   v=def; if(pblock) pblock->GetValue(id,t,v,FOREVER); return v; }

    // ========================================================================
    // Projection geometry
    // ========================================================================

    // Signed projection direction (proj 1-6)
    static Point3 ProjDir(int proj)
    {
        switch (proj) {
        case 1: return Point3( 1, 0, 0); // X+
        case 2: return Point3(-1, 0, 0); // X-
        case 3: return Point3( 0, 1, 0); // Y+
        case 4: return Point3( 0,-1, 0); // Y-
        case 5: return Point3( 0, 0, 1); // Z+
        case 6: return Point3( 0, 0,-1); // Z-
        default: return Point3(0, 0, 1);
        }
    }

    // Normalised object-space position
    Point3 NormPt(const Point3& p, const Point3& mn, const Point3& mx) const
    {
        return Point3(
            (p.x - mn.x) / SafeRange(mx.x - mn.x),
            (p.y - mn.y) / SafeRange(mx.y - mn.y),
            (p.z - mn.z) / SafeRange(mx.z - mn.z));
    }

    // Raw UV from signed projection (matches MAXScript uvFromProjection)
    Point3 UVFromProj(const Point3& p, const Point3& mn, const Point3& mx, int proj) const
    {
        const Point3 n = NormPt(p, mn, mx);
        switch (proj) {
        case 1: return Point3(        n.y,          n.z, 0.0f); // X+
        case 2: return Point3(1.0f -  n.y,          n.z, 0.0f); // X-
        case 3: return Point3(1.0f -  n.x,          n.z, 0.0f); // Y+
        case 4: return Point3(        n.x,          n.z, 0.0f); // Y-
        case 5: return Point3(        n.x,          n.y, 0.0f); // Z+
        case 6: return Point3(        n.x,  1.0f -  n.y, 0.0f); // Z-
        default: return Point3(n.x, n.y, 0.0f);
        }
    }

    // Per-row scale, then global offset/flip/swap
    Point3 TransformUV(TimeValue t, const Point3& uv, float uTileRow, float vTileRow) const
    {
        float u = uv.x;
        float v = uv.y;
        if (PBb(pb_swapUV, t, FALSE)) std::swap(u, v);
        if (PBb(pb_flipU,  t, FALSE)) u = 1.0f - u;
        if (PBb(pb_flipV,  t, FALSE)) v = 1.0f - v;
        return Point3(
            u * uTileRow + PBf(pb_uOffset, t, 0.0f),
            v * vTileRow + PBf(pb_vOffset, t, 0.0f),
            0.0f);
    }

    // Per-face normal (not averaged)
    static Point3 FaceNorm(const Mesh& mesh, int f)
    {
        const Face& face = mesh.faces[f];
        const Point3 e1 = mesh.verts[face.v[1]] - mesh.verts[face.v[0]];
        const Point3 e2 = mesh.verts[face.v[2]] - mesh.verts[face.v[0]];
        const Point3 n  = e1 ^ e2;
        const float  len = Length(n);
        return (len > 1e-6f) ? n / len : Point3(0, 0, 1);
    }

    // ========================================================================
    // Map channel management
    // ========================================================================

    void EnsureChannel(Mesh& mesh, int ch) const
    {
        ch = ClampCh(ch);
        const int needed = ch + 1;
        if (mesh.getNumMaps() < needed) mesh.setNumMaps(needed, TRUE);
        if (!mesh.mapSupport(ch)) mesh.setMapSupport(ch, TRUE);
    }

    // ========================================================================
    // Grouped channel writer
    //
    //  projList  — projection indices (1-6) assigned to this channel
    //  uTiles / vTiles — per-projection tile values, same order as projList
    // ========================================================================

    void ApplyGroupedChannel(
        TimeValue t, Mesh& mesh, int ch,
        const std::vector<int>&   projList,
        const std::vector<float>& uTiles,
        const std::vector<float>& vTiles,
        const Point3& mn, const Point3& mx) const
    {
        if (projList.empty()) return;
        ch = ClampCh(ch);
        EnsureChannel(mesh, ch);

        const float threshold  = PBf(pb_normalThreshold, t, 0.001f);
        const BOOL  doMerge    = PBb(pb_mergeIslands,    t, TRUE);
        const BOOL  doPark     = PBb(pb_parkNonMatching, t, TRUE);
        const float parkU_val  = PBf(pb_parkU,           t, -1.0f);
        const float parkV_val  = PBf(pb_parkV,           t, -1.0f);

        // key → 0-based mapVert index
        std::unordered_map<int64_t, int> keyToVert;
        std::vector<Point3> mapVerts;
        std::vector<std::array<int,3>> mapFaces(mesh.numFaces);

        auto getOrAdd = [&](int64_t key, const Point3& uv) -> int
        {
            auto it = keyToVert.find(key);
            if (it != keyToVert.end()) return it->second;
            int idx = (int)mapVerts.size();
            mapVerts.push_back(uv);
            keyToVert[key] = idx;
            return idx;
        };

        for (int f = 0; f < mesh.numFaces; ++f)
        {
            const Point3 fn = FaceNorm(mesh, f);
            const Face&  fc = mesh.faces[f];

            // Choose best projection for this face
            int   bestProjIdx = -1;
            float bestDot     = -2.0f;
            for (int pi = 0; pi < (int)projList.size(); ++pi)
            {
                float d = DotProd(fn, ProjDir(projList[pi]));
                if (d > bestDot) { bestDot = d; bestProjIdx = pi; }
            }

            const bool parked = (bestProjIdx < 0) ||
                                (doPark && bestDot <= threshold);

            for (int c = 0; c < 3; ++c)
            {
                const int mv = fc.v[c]; // mesh vert index
                int64_t key;
                Point3  uv;

                if (parked)
                {
                    key = doMerge
                        ? (int64_t)0 * 2000000LL + mv
                        : (int64_t)(-1) * 2000000LL + (int64_t)(f * 3 + c);
                    uv = Point3(parkU_val, parkV_val, 0.0f);
                }
                else
                {
                    const int   pi   = projList[bestProjIdx];
                    const float ut   = uTiles[bestProjIdx];
                    const float vt   = vTiles[bestProjIdx];
                    const Point3 raw = UVFromProj(mesh.verts[mv], mn, mx, pi);
                    uv  = TransformUV(t, raw, ut, vt);
                    key = doMerge
                        ? (int64_t)pi * 2000000LL + mv
                        : (int64_t)(f * 3 + c + 1) * 10000LL;
                }
                mapFaces[f][c] = getOrAdd(key, uv);
            }
        }

        if (mapVerts.empty())
        {
            mapVerts.push_back(Point3(0, 0, 0));
            for (auto& mf : mapFaces) mf = {0, 0, 0};
        }

        mesh.setNumMapVerts(ch, (int)mapVerts.size(), FALSE);
        mesh.setNumMapFaces(ch, mesh.numFaces,        FALSE);
        MeshMap& map = mesh.Map(ch);
        for (int i = 0; i < (int)mapVerts.size(); ++i)
            map.tv[i] = mapVerts[i];
        for (int f = 0; f < mesh.numFaces; ++f)
        {
            map.tf[f].t[0] = mapFaces[f][0];
            map.tf[f].t[1] = mapFaces[f][1];
            map.tf[f].t[2] = mapFaces[f][2];
        }
    }

    // ========================================================================
    // Blend output channel
    // ========================================================================

    void ComputeVertexNormals(const Mesh& mesh, std::vector<Point3>& out) const
    {
        out.assign(mesh.numVerts, Point3(0, 0, 0));
        for (int f = 0; f < mesh.numFaces; ++f)
        {
            const Point3 fn = FaceNorm(mesh, f);
            const Face& fc  = mesh.faces[f];
            out[fc.v[0]] += fn;
            out[fc.v[1]] += fn;
            out[fc.v[2]] += fn;
        }
        for (auto& n : out)
        {
            float len = Length(n);
            n = (len > 1e-6f) ? n / len : Point3(0, 0, 1);
        }
    }

    void PopulateDisplayCache(const Mesh& mesh, TimeValue t) const
    {
        const float power = std::max(0.001f, PBf(pb_blendPower, t, 1.0f));
        std::vector<Point3> normals;
        ComputeVertexNormals(mesh, normals);

        m_displayCache.verts.assign(mesh.verts, mesh.verts + mesh.numVerts);
        m_displayCache.colors.resize(mesh.numVerts);
        for (int v = 0; v < mesh.numVerts; ++v)
        {
            const Point3& n = normals[v];
            float wx = std::pow(std::fabs(n.x), power);
            float wy = std::pow(std::fabs(n.y), power);
            float wz = std::pow(std::fabs(n.z), power);
            const float s = wx + wy + wz;
            if (s > 1e-6f) { wx/=s; wy/=s; wz/=s; }
            m_displayCache.colors[v] = Point3(wx, wy, wz);
        }
        m_displayCache.indices.resize(mesh.numFaces * 3);
        for (int f = 0; f < mesh.numFaces; ++f)
        {
            m_displayCache.indices[f*3]   = mesh.faces[f].v[0];
            m_displayCache.indices[f*3+1] = mesh.faces[f].v[1];
            m_displayCache.indices[f*3+2] = mesh.faces[f].v[2];
        }
        m_displayCache.valid = true;
    }

    void ApplyBlendChannel(TimeValue t, Mesh& mesh) const
    {
        const int   ch    = ClampCh(PBi(pb_channelBlend, t, 10));
        const float power = std::max(0.001f, PBf(pb_blendPower, t, 1.0f));

        std::vector<Point3> normals;
        ComputeVertexNormals(mesh, normals);
        EnsureChannel(mesh, ch);
        mesh.setNumMapVerts(ch, mesh.numVerts, FALSE);
        mesh.setNumMapFaces(ch, mesh.numFaces, FALSE);
        MeshMap& map = mesh.Map(ch);
        for (int f = 0; f < mesh.numFaces; ++f)
        {
            map.tf[f].t[0] = mesh.faces[f].v[0];
            map.tf[f].t[1] = mesh.faces[f].v[1];
            map.tf[f].t[2] = mesh.faces[f].v[2];
        }
        for (int v = 0; v < mesh.numVerts; ++v)
        {
            const Point3& n = normals[v];
            float wx = std::pow(std::fabs(n.x), power);
            float wy = std::pow(std::fabs(n.y), power);
            float wz = std::pow(std::fabs(n.z), power);
            const float s = wx + wy + wz;
            if (s > 1e-6f) { wx/=s; wy/=s; wz/=s; }
            map.tv[v] = Point3(wx, wy, wz);
        }
    }

    // ========================================================================
    // Top-level apply
    // ========================================================================

    void ApplyAll(TimeValue t, Mesh& mesh) const
    {
        // Collect enabled rows
        struct RowInfo { int ch; int proj; float ut; float vt; };
        std::vector<RowInfo> rows;
        for (int r = 0; r < 6; ++r)
        {
            BOOL en = FALSE;
            pblock->GetValue(RowEn(r), t, en, FOREVER);
            if (!en) continue;
            int   ch   = ClampCh(PBi(RowCh(r),   t, r < 2 ? 1 : r < 4 ? 2 : 3));
            int   proj = std::clamp(PBi(RowProj(r), t, r + 1), 1, 6);
            float ut   = PBf(RowUTile(r), t, 1.0f);
            float vt   = PBf(RowVTile(r), t, 1.0f);
            rows.push_back({ch, proj, ut, vt});
        }

        // Group rows by map channel
        std::map<int, std::vector<int>> chToRows; // ch → indices into rows
        for (int i = 0; i < (int)rows.size(); ++i)
            chToRows[rows[i].ch].push_back(i);

        // Compute mesh bounds
        Box3 bounds;
        bounds.Init();
        for (int i = 0; i < mesh.numVerts; ++i) bounds += mesh.verts[i];
        if (bounds.IsEmpty()) { bounds += Point3(0,0,0); bounds += Point3(1,1,1); }
        const Point3 mn = bounds.Min();
        const Point3 mx = bounds.Max();

        // Pre-expand map table
        int highest = 1;
        for (auto& [ch, _] : chToRows) highest = std::max(highest, ch);
        EnsureChannel(mesh, ClampCh(highest));

        // Write each channel group
        for (auto& [ch, rowIdxs] : chToRows)
        {
            std::vector<int>   projs;
            std::vector<float> uts, vts;
            for (int ri : rowIdxs)
            {
                projs.push_back(rows[ri].proj);
                uts.push_back(rows[ri].ut);
                vts.push_back(rows[ri].vt);
            }
            ApplyGroupedChannel(t, mesh, ch, projs, uts, vts, mn, mx);
        }

        // Blend output channel
        const BOOL enBlend  = PBb(pb_enableBlend, t, FALSE);
        const BOOL showBlend = PBb(pb_showBlend,  t, FALSE);
        if (enBlend)
        {
            const int blCh = ClampCh(PBi(pb_channelBlend, t, 10));
            EnsureChannel(mesh, blCh);
            ApplyBlendChannel(t, mesh);
        }
        if (showBlend)
            PopulateDisplayCache(mesh, t);
        else
            m_displayCache.valid = false;
    }
};

// ---------------------------------------------------------------------------
// ClassDesc2::Create
// ---------------------------------------------------------------------------

void* EZMultiPlanarUVWClassDesc::Create(BOOL) { return new EZMultiPlanarUVW(); }

// ---------------------------------------------------------------------------
// ParamBlockDesc2
// ---------------------------------------------------------------------------

// Macro helpers to reduce repetition for each row
#define ROW_PARAMS(N, defCh, defProj)                                                   \
    pb_en##N,    _T("en")   #N, TYPE_BOOL,  P_ANIMATABLE, IDS_EN##N,                   \
        p_default, TRUE,                                                                 \
        p_ui, TYPE_SINGLECHEKBOX, IDC_CHK_EN##N,                                        \
    p_end,                                                                               \
    pb_ch##N,    _T("ch")   #N, TYPE_INT,   P_ANIMATABLE, IDS_CH##N,                   \
        p_default, defCh,  p_range, 1, 99,                                              \
        p_ui, TYPE_SPINNER, EDITTYPE_INT,   IDC_EDIT_CH##N,    IDC_SPIN_CH##N,    SPIN_AUTOSCALE, \
    p_end,                                                                               \
    pb_proj##N,  _T("proj") #N, TYPE_INT,   P_ANIMATABLE, IDS_PR##N,                   \
        p_default, defProj, p_range, 1, 6,                                              \
        p_ui, TYPE_SPINNER, EDITTYPE_INT,   IDC_EDIT_PR##N,    IDC_SPIN_PR##N,    SPIN_AUTOSCALE, \
    p_end,                                                                               \
    pb_uTile##N, _T("ut")   #N, TYPE_FLOAT, P_ANIMATABLE, IDS_UT##N,                   \
        p_default, 1.0f,   p_range, -9999.0f, 9999.0f,                                 \
        p_ui, TYPE_SPINNER, EDITTYPE_FLOAT, IDC_EDIT_UT##N,    IDC_SPIN_UT##N,    SPIN_AUTOSCALE, \
    p_end,                                                                               \
    pb_vTile##N, _T("vt")   #N, TYPE_FLOAT, P_ANIMATABLE, IDS_VT##N,                   \
        p_default, 1.0f,   p_range, -9999.0f, 9999.0f,                                 \
        p_ui, TYPE_SPINNER, EDITTYPE_FLOAT, IDC_EDIT_VT##N,    IDC_SPIN_VT##N,    SPIN_AUTOSCALE, \
    p_end,

static ParamBlockDesc2 g_MainPBlock
(
    kMainPBlock, _T("params"), IDS_PARAMS, &g_EZMultiPlanarUVWDesc,
    P_AUTO_CONSTRUCT | P_AUTO_UI,
    0,
    IDD_PANEL, IDS_PARAMS, 0, 0, nullptr,

    // Axis-pair defaults: rows 1-2 on ch1 (X+/X-), 3-4 on ch2 (Y+/Y-), 5-6 on ch3 (Z+/Z-)
    ROW_PARAMS(1, 1, 1)
    ROW_PARAMS(2, 1, 2)
    ROW_PARAMS(3, 2, 3)
    ROW_PARAMS(4, 2, 4)
    ROW_PARAMS(5, 3, 5)
    ROW_PARAMS(6, 3, 6)

    // Global UV
    pb_uOffset, _T("uOffset"), TYPE_FLOAT, P_ANIMATABLE, IDS_UOFFSET,
        p_default, 0.0f, p_range, -9999.0f, 9999.0f,
        p_ui, TYPE_SPINNER, EDITTYPE_FLOAT, IDC_EDIT_UOFFSET, IDC_SPIN_UOFFSET, SPIN_AUTOSCALE,
    p_end,
    pb_vOffset, _T("vOffset"), TYPE_FLOAT, P_ANIMATABLE, IDS_VOFFSET,
        p_default, 0.0f, p_range, -9999.0f, 9999.0f,
        p_ui, TYPE_SPINNER, EDITTYPE_FLOAT, IDC_EDIT_VOFFSET, IDC_SPIN_VOFFSET, SPIN_AUTOSCALE,
    p_end,
    pb_flipU,   _T("flipU"),   TYPE_BOOL,  P_ANIMATABLE, IDS_FLIPU,
        p_default, FALSE, p_ui, TYPE_SINGLECHEKBOX, IDC_CHK_FLIPU,
    p_end,
    pb_flipV,   _T("flipV"),   TYPE_BOOL,  P_ANIMATABLE, IDS_FLIPV,
        p_default, FALSE, p_ui, TYPE_SINGLECHEKBOX, IDC_CHK_FLIPV,
    p_end,
    pb_swapUV,  _T("swapUV"),  TYPE_BOOL,  P_ANIMATABLE, IDS_SWAPUV,
        p_default, FALSE, p_ui, TYPE_SINGLECHEKBOX, IDC_CHK_SWAPUV,
    p_end,

    // Seam options
    pb_normalThreshold, _T("normalThreshold"), TYPE_FLOAT, P_ANIMATABLE, IDS_NORMAL_THRESHOLD,
        p_default, 0.001f, p_range, 0.0f, 1.0f,
        p_ui, TYPE_SPINNER, EDITTYPE_FLOAT, IDC_EDIT_THRESHOLD, IDC_SPIN_THRESHOLD, SPIN_AUTOSCALE,
    p_end,
    pb_mergeIslands,    _T("mergeIslands"),    TYPE_BOOL,  P_ANIMATABLE, IDS_MERGE_ISLANDS,
        p_default, TRUE, p_ui, TYPE_SINGLECHEKBOX, IDC_CHK_MERGE,
    p_end,
    pb_parkNonMatching, _T("parkNonMatching"), TYPE_BOOL,  P_ANIMATABLE, IDS_PARK_NON_MATCHING,
        p_default, TRUE, p_ui, TYPE_SINGLECHEKBOX, IDC_CHK_PARK,
    p_end,
    pb_parkU, _T("parkU"), TYPE_FLOAT, P_ANIMATABLE, IDS_PARK_U,
        p_default, -1.0f, p_range, -9999.0f, 9999.0f,
        p_ui, TYPE_SPINNER, EDITTYPE_FLOAT, IDC_EDIT_PARKU, IDC_SPIN_PARKU, SPIN_AUTOSCALE,
    p_end,
    pb_parkV, _T("parkV"), TYPE_FLOAT, P_ANIMATABLE, IDS_PARK_V,
        p_default, -1.0f, p_range, -9999.0f, 9999.0f,
        p_ui, TYPE_SPINNER, EDITTYPE_FLOAT, IDC_EDIT_PARKV, IDC_SPIN_PARKV, SPIN_AUTOSCALE,
    p_end,

    // Blend output
    pb_enableBlend,  _T("enableBlend"),  TYPE_BOOL,  P_ANIMATABLE, IDS_ENABLE_BLEND,
        p_default, FALSE, p_ui, TYPE_SINGLECHEKBOX, IDC_CHK_ENABLE_BLEND,
    p_end,
    pb_channelBlend, _T("channelBlend"), TYPE_INT,   P_ANIMATABLE, IDS_CHANNEL_BLEND,
        p_default, 10, p_range, 1, 99,
        p_ui, TYPE_SPINNER, EDITTYPE_INT, IDC_EDIT_CHANNEL_BLEND, IDC_SPIN_CHANNEL_BLEND, SPIN_AUTOSCALE,
    p_end,
    pb_blendPower,   _T("blendPower"),   TYPE_FLOAT, P_ANIMATABLE, IDS_BLEND_POWER,
        p_default, 1.0f, p_range, 0.001f, 32.0f,
        p_ui, TYPE_SPINNER, EDITTYPE_FLOAT, IDC_EDIT_BLEND_POWER, IDC_SPIN_BLEND_POWER, SPIN_AUTOSCALE,
    p_end,
    pb_showBlend,    _T("showBlend"),    TYPE_BOOL,  P_ANIMATABLE, IDS_SHOW_BLEND,
        p_default, FALSE, p_ui, TYPE_SINGLECHEKBOX, IDC_CHK_SHOW_BLEND,
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

extern "C"
{
    __declspec(dllexport) const TCHAR* LibDescription()  { return _T("EZ Multi Planar UVW Modifier"); }
    __declspec(dllexport) int          LibNumberClasses() { return 1; }
    __declspec(dllexport) ClassDesc*   LibClassDesc(int i){ return (i==0) ? GetEZMultiPlanarUVWDesc() : nullptr; }
    __declspec(dllexport) ULONG        LibVersion()       { return VERSION_3DSMAX; }
    __declspec(dllexport) ULONG        CanAutoDefer()     { return 1; }
}
