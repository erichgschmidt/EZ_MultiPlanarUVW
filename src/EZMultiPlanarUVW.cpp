/*
    EZMultiPlanarUVW.cpp
    3ds Max 2023 C++ SDK  —  OSM modifier  —  .dlm

    Six signed projections (X+/X-/Y+/Y-/Z+/Z-) in grouped map channels.
    Per-face normals decide which projection wins per face.
    Each row has its own full UV transform (tile, offset, flip, swap).
    Blend output channel: per-vertex triplanar weights for material blending.
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
// Param IDs  —  10 per row × 6 rows = 60, then seam (5) + blend (4)
// ---------------------------------------------------------------------------

enum ParamBlockIDs { kMainPBlock = 0 };

enum ParamIDs
{
    // Row 1 (params 0-9)
    pb_en1=0, pb_ch1, pb_proj1, pb_uTile1, pb_vTile1,
    pb_uOff1, pb_vOff1, pb_flipU1, pb_flipV1, pb_swap1,
    // Row 2 (10-19)
    pb_en2, pb_ch2, pb_proj2, pb_uTile2, pb_vTile2,
    pb_uOff2, pb_vOff2, pb_flipU2, pb_flipV2, pb_swap2,
    // Row 3 (20-29)
    pb_en3, pb_ch3, pb_proj3, pb_uTile3, pb_vTile3,
    pb_uOff3, pb_vOff3, pb_flipU3, pb_flipV3, pb_swap3,
    // Row 4 (30-39)
    pb_en4, pb_ch4, pb_proj4, pb_uTile4, pb_vTile4,
    pb_uOff4, pb_vOff4, pb_flipU4, pb_flipV4, pb_swap4,
    // Row 5 (40-49)
    pb_en5, pb_ch5, pb_proj5, pb_uTile5, pb_vTile5,
    pb_uOff5, pb_vOff5, pb_flipU5, pb_flipV5, pb_swap5,
    // Row 6 (50-59)
    pb_en6, pb_ch6, pb_proj6, pb_uTile6, pb_vTile6,
    pb_uOff6, pb_vOff6, pb_flipU6, pb_flipV6, pb_swap6,
    // Seam options (60-64)
    pb_normalThreshold, pb_mergeIslands, pb_parkNonMatching, pb_parkU, pb_parkV,
    // Blend output (65-68)
    pb_enableBlend, pb_channelBlend, pb_blendPower, pb_showBlend
};

// Per-row param accessors (r = 0..5)
static inline ParamID RowEn   (int r){ return (ParamID)(pb_en1    + r*10); }
static inline ParamID RowCh   (int r){ return (ParamID)(pb_ch1    + r*10); }
static inline ParamID RowProj (int r){ return (ParamID)(pb_proj1  + r*10); }
static inline ParamID RowUTile(int r){ return (ParamID)(pb_uTile1 + r*10); }
static inline ParamID RowVTile(int r){ return (ParamID)(pb_vTile1 + r*10); }
static inline ParamID RowUOff (int r){ return (ParamID)(pb_uOff1  + r*10); }
static inline ParamID RowVOff (int r){ return (ParamID)(pb_vOff1  + r*10); }
static inline ParamID RowFlipU(int r){ return (ParamID)(pb_flipU1 + r*10); }
static inline ParamID RowFlipV(int r){ return (ParamID)(pb_flipV1 + r*10); }
static inline ParamID RowSwap (int r){ return (ParamID)(pb_swap1  + r*10); }

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
    int          IsPublic()              override { return TRUE; }
    void*        Create(BOOL = FALSE)    override;
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

    struct BlendDisplayCache {
        std::vector<Point3> verts, colors;
        std::vector<int>    indices;
        bool valid = false;
    };
    mutable BlendDisplayCache m_displayCache;

    EZMultiPlanarUVW()  { g_EZMultiPlanarUVWDesc.MakeAutoParamBlocks(this); }
    ~EZMultiPlanarUVW() override = default;

    // ---- Animatable --------------------------------------------------------
    void        DeleteThis()   override { delete this; }
    int         NumSubs()      override { return 1; }
    Animatable* SubAnim(int i) override { return i == 0 ? pblock : nullptr; }
    TSTR        SubAnimName(int i, bool = true) override
    { return i == 0 ? TSTR(_T("Parameters")) : TSTR(_T("")); }

    // ---- ReferenceTarget ---------------------------------------------------
    RefTargetHandle Clone(RemapDir& remap) override
    {
        EZMultiPlanarUVW* c = new EZMultiPlanarUVW();
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
    { g_EZMultiPlanarUVWDesc.BeginEditParams(ip, this, flags, prev); }
    void EndEditParams(IObjParam* ip, ULONG flags, Animatable* next) override
    { g_EZMultiPlanarUVWDesc.EndEditParams(ip, this, flags, next); }

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
            const Point3 col = (m_displayCache.colors[i0]+m_displayCache.colors[i1]+m_displayCache.colors[i2])/3.0f;
            gw->setColor(FILL_COLOR, col.x, col.y, col.z);
            Point3 pts[3] = { m_displayCache.verts[i0], m_displayCache.verts[i1], m_displayCache.verts[i2] };
            gw->triangle(pts, uvw);
        }
        gw->setRndLimits(saved);
        return 1;
    }

    // ---- Modifier ----------------------------------------------------------
    ChannelMask ChannelsUsed()   override { return GEOM_CHANNEL|TOPO_CHANNEL|TEXMAP_CHANNEL; }
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
    static float SafeRange(float v) { return std::fabs(v) < 1e-6f ? 1.0f : v; }

    float PBf(ParamID id, TimeValue t, float def) const
    { float  v=def; if(pblock) pblock->GetValue(id,t,v,FOREVER); return v; }
    int   PBi(ParamID id, TimeValue t, int   def) const
    { int    v=def; if(pblock) pblock->GetValue(id,t,v,FOREVER); return v; }
    BOOL  PBb(ParamID id, TimeValue t, BOOL  def) const
    { BOOL   v=def; if(pblock) pblock->GetValue(id,t,v,FOREVER); return v; }

    // ========================================================================
    // Per-row config (read once per ModifyObject call)
    // ========================================================================

    struct RowConfig
    {
        int   ch;
        int   proj;    // 0-5: X+/X-/Y+/Y-/Z+/Z-
        float uTile, vTile, uOff, vOff;
        bool  flipU, flipV, swapUV;
        int   rowIdx;  // 0-5, used as merge-island key component
    };

    std::vector<RowConfig> ReadRows(TimeValue t) const
    {
        std::vector<RowConfig> rows;
        // Channel defaults: rows 0-1 → ch1, 2-3 → ch2, 4-5 → ch3
        static const int defCh[6]   = {1,1,2,2,3,3};
        static const int defProj[6] = {0,1,2,3,4,5};

        for (int r = 0; r < 6; ++r)
        {
            BOOL en = FALSE;
            pblock->GetValue(RowEn(r), t, en, FOREVER);
            if (!en) continue;

            RowConfig rc;
            rc.rowIdx = r;
            rc.ch     = ClampCh(PBi(RowCh(r),   t, defCh[r]));
            rc.proj   = std::clamp(PBi(RowProj(r), t, defProj[r]), 0, 5);
            rc.uTile  = PBf(RowUTile(r), t, 1.0f);
            rc.vTile  = PBf(RowVTile(r), t, 1.0f);
            rc.uOff   = PBf(RowUOff(r),  t, 0.0f);
            rc.vOff   = PBf(RowVOff(r),  t, 0.0f);
            rc.flipU  = PBb(RowFlipU(r), t, FALSE) != FALSE;
            rc.flipV  = PBb(RowFlipV(r), t, FALSE) != FALSE;
            rc.swapUV = PBb(RowSwap(r),  t, FALSE) != FALSE;
            rows.push_back(rc);
        }
        return rows;
    }

    // ========================================================================
    // Projection geometry  (proj 0-5)
    // ========================================================================

    static Point3 ProjDir(int proj)
    {
        switch (proj) {
        case 0: return Point3( 1.0f, 0.0f, 0.0f); // X+
        case 1: return Point3(-1.0f, 0.0f, 0.0f); // X-
        case 2: return Point3( 0.0f, 1.0f, 0.0f); // Y+
        case 3: return Point3( 0.0f,-1.0f, 0.0f); // Y-
        case 4: return Point3( 0.0f, 0.0f, 1.0f); // Z+
        case 5: return Point3( 0.0f, 0.0f,-1.0f); // Z-
        default: return Point3(0.0f, 0.0f, 1.0f);
        }
    }

    Point3 NormPt(const Point3& p, const Point3& mn, const Point3& mx) const
    {
        return Point3(
            (p.x - mn.x) / SafeRange(mx.x - mn.x),
            (p.y - mn.y) / SafeRange(mx.y - mn.y),
            (p.z - mn.z) / SafeRange(mx.z - mn.z));
    }

    // Raw UV before per-row transform (matches MAXScript uvFromProjection)
    Point3 UVFromProj(const Point3& p, const Point3& mn, const Point3& mx, int proj) const
    {
        const Point3 n = NormPt(p, mn, mx);
        switch (proj) {
        case 0: return Point3(        n.y,         n.z, 0.0f); // X+
        case 1: return Point3(1.0f -  n.y,         n.z, 0.0f); // X-
        case 2: return Point3(1.0f -  n.x,         n.z, 0.0f); // Y+
        case 3: return Point3(        n.x,         n.z, 0.0f); // Y-
        case 4: return Point3(        n.x,         n.y, 0.0f); // Z+
        case 5: return Point3(        n.x,  1.0f - n.y, 0.0f); // Z-
        default: return Point3(n.x, n.y, 0.0f);
        }
    }

    // Per-row UV transform
    static Point3 TransformUV(const RowConfig& rc, const Point3& uv)
    {
        float u = uv.x;
        float v = uv.y;
        if (rc.swapUV) std::swap(u, v);
        if (rc.flipU)  u = 1.0f - u;
        if (rc.flipV)  v = 1.0f - v;
        return Point3(u * rc.uTile + rc.uOff, v * rc.vTile + rc.vOff, 0.0f);
    }

    // Per-face geometric normal (not averaged)
    static Point3 FaceNorm(const Mesh& mesh, int f)
    {
        const Face& fc = mesh.faces[f];
        const Point3 e1 = mesh.verts[fc.v[1]] - mesh.verts[fc.v[0]];
        const Point3 e2 = mesh.verts[fc.v[2]] - mesh.verts[fc.v[0]];
        const Point3 n  = e1 ^ e2;
        const float len = Length(n);
        return len > 1e-6f ? n / len : Point3(0.0f, 0.0f, 1.0f);
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
    //  rowGroup — all RowConfigs that share this channel.
    //             Each face picks the one whose ProjDir best matches its normal.
    // ========================================================================

    void ApplyGroupedChannel(
        TimeValue t, Mesh& mesh, int ch,
        const std::vector<const RowConfig*>& rowGroup,
        const Point3& mn, const Point3& mx) const
    {
        if (rowGroup.empty()) return;
        ch = ClampCh(ch);
        EnsureChannel(mesh, ch);

        const float threshold = PBf(pb_normalThreshold, t, 0.001f);
        const BOOL  doMerge   = PBb(pb_mergeIslands,    t, TRUE);
        const BOOL  doPark    = PBb(pb_parkNonMatching, t, TRUE);
        const float parkU     = PBf(pb_parkU,           t, -1.0f);
        const float parkV     = PBf(pb_parkV,           t, -1.0f);

        // Build map verts
        std::unordered_map<int64_t, int> keyToVert;
        std::vector<Point3> mapVerts;
        std::vector<std::array<int, 3>> mapFaces(mesh.numFaces);

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

            // Best-matching row for this face
            int   bestRowIdx   = -1;
            float bestDot      = -2.0f;
            for (int ri = 0; ri < (int)rowGroup.size(); ++ri)
            {
                float d = DotProd(fn, ProjDir(rowGroup[ri]->proj));
                if (d > bestDot) { bestDot = d; bestRowIdx = ri; }
            }

            const bool parked = bestRowIdx < 0 || (doPark && bestDot <= threshold);

            for (int c = 0; c < 3; ++c)
            {
                const int mv = fc.v[c];
                int64_t   key;
                Point3    uv;

                if (parked)
                {
                    // key: proj=7 (unused) → all parked verts in same or unique slot
                    key = doMerge
                        ? int64_t(7) * 2000000LL + mv
                        : int64_t(-1) * 2000000LL + int64_t(f * 3 + c);
                    uv = Point3(parkU, parkV, 0.0f);
                }
                else
                {
                    const RowConfig& rc  = *rowGroup[bestRowIdx];
                    const Point3 rawUV   = UVFromProj(mesh.verts[mv], mn, mx, rc.proj);
                    uv  = TransformUV(rc, rawUV);
                    key = doMerge
                        ? int64_t(rc.proj) * 2000000LL + mv
                        : int64_t(f * 3 + c + 1) * 10000LL;
                }
                mapFaces[f][c] = getOrAdd(key, uv);
            }
        }

        if (mapVerts.empty())
        {
            mapVerts.push_back(Point3(0.0f, 0.0f, 0.0f));
            for (auto& mf : mapFaces) mf = {0, 0, 0};
        }

        mesh.setNumMapVerts(ch, (int)mapVerts.size(), FALSE);
        mesh.setNumMapFaces(ch, mesh.numFaces,        FALSE);
        MeshMap& map = mesh.Map(ch);
        for (int i = 0; i < (int)mapVerts.size(); ++i) map.tv[i] = mapVerts[i];
        for (int f = 0; f < mesh.numFaces; ++f)
        {
            map.tf[f].t[0] = mapFaces[f][0];
            map.tf[f].t[1] = mapFaces[f][1];
            map.tf[f].t[2] = mapFaces[f][2];
        }
    }

    // ========================================================================
    // Blend output channel + display cache
    // ========================================================================

    void ComputeVertexNormals(const Mesh& mesh, std::vector<Point3>& out) const
    {
        out.assign(mesh.numVerts, Point3(0.0f, 0.0f, 0.0f));
        for (int f = 0; f < mesh.numFaces; ++f)
        {
            const Point3 fn = FaceNorm(mesh, f);
            out[mesh.faces[f].v[0]] += fn;
            out[mesh.faces[f].v[1]] += fn;
            out[mesh.faces[f].v[2]] += fn;
        }
        for (auto& n : out)
        {
            float len = Length(n);
            n = len > 1e-6f ? n / len : Point3(0.0f, 0.0f, 1.0f);
        }
    }

    static Point3 BlendWeight(const Point3& n, float power)
    {
        float wx = std::pow(std::fabs(n.x), power);
        float wy = std::pow(std::fabs(n.y), power);
        float wz = std::pow(std::fabs(n.z), power);
        const float s = wx + wy + wz;
        if (s > 1e-6f) { wx/=s; wy/=s; wz/=s; }
        return Point3(wx, wy, wz);
    }

    void PopulateDisplayCache(const Mesh& mesh, TimeValue t) const
    {
        const float power = std::max(0.001f, PBf(pb_blendPower, t, 1.0f));
        std::vector<Point3> normals;
        ComputeVertexNormals(mesh, normals);
        m_displayCache.verts.assign(mesh.verts, mesh.verts + mesh.numVerts);
        m_displayCache.colors.resize(mesh.numVerts);
        for (int v = 0; v < mesh.numVerts; ++v)
            m_displayCache.colors[v] = BlendWeight(normals[v], power);
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
            map.tv[v] = BlendWeight(normals[v], power);
    }

    // ========================================================================
    // Top-level apply
    // ========================================================================

    void ApplyAll(TimeValue t, Mesh& mesh) const
    {
        const std::vector<RowConfig> rows = ReadRows(t);

        // Group rows by map channel
        std::map<int, std::vector<const RowConfig*>> chGroups;
        for (const auto& rc : rows)
            chGroups[rc.ch].push_back(&rc);

        // Mesh bounds (object space)
        Box3 bounds; bounds.Init();
        for (int i = 0; i < mesh.numVerts; ++i) bounds += mesh.verts[i];
        if (bounds.IsEmpty()) { bounds += Point3(0,0,0); bounds += Point3(1,1,1); }
        const Point3 mn = bounds.Min(), mx = bounds.Max();

        // Pre-expand map table
        int highest = 1;
        for (auto& [ch, _] : chGroups) highest = std::max(highest, ch);
        EnsureChannel(mesh, ClampCh(highest));

        // Write each channel group
        for (auto& [ch, group] : chGroups)
            ApplyGroupedChannel(t, mesh, ch, group, mn, mx);

        // Blend output
        if (PBb(pb_enableBlend, t, FALSE))
        {
            EnsureChannel(mesh, ClampCh(PBi(pb_channelBlend, t, 10)));
            ApplyBlendChannel(t, mesh);
        }

        if (PBb(pb_showBlend, t, FALSE))
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
// Direction dropdown DlgProc
// proj params have no p_ui — the comboboxes are populated and synced here.
// ---------------------------------------------------------------------------

static const TCHAR* kDirLabels[6] = {
    _T("X+"), _T("X-"), _T("Y+"), _T("Y-"), _T("Z+"), _T("Z-")
};
static const int kComboIDs[6] = {
    IDC_COMBO_PR_1, IDC_COMBO_PR_2, IDC_COMBO_PR_3,
    IDC_COMBO_PR_4, IDC_COMBO_PR_5, IDC_COMBO_PR_6
};
// proj param IDs — pb_proj1=2, then every 10
static const ParamID kProjIDs[6] = {
    (ParamID)pb_proj1, (ParamID)pb_proj2, (ParamID)pb_proj3,
    (ParamID)pb_proj4, (ParamID)pb_proj5, (ParamID)pb_proj6
};

class EZRowDlgProc : public ParamMap2UserDlgProc
{
    static void FillCombo(HWND cb)
    {
        SendMessage(cb, CB_RESETCONTENT, 0, 0);
        for (int i = 0; i < 6; ++i)
            SendMessage(cb, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(kDirLabels[i]));
    }

    static void SyncCombo(HWND hDlg, int r, IParamMap2* map)
    {
        HWND cb = GetDlgItem(hDlg, kComboIDs[r]);
        if (!cb || !map || !map->GetParamBlock()) return;
        int val = r; // fallback = row index
        map->GetParamBlock()->GetValue(kProjIDs[r], 0, val, FOREVER);
        SendMessage(cb, CB_SETCURSEL, (WPARAM)std::clamp(val, 0, 5), 0);
    }

public:
    INT_PTR DlgProc(TimeValue t, IParamMap2* map, HWND hWnd,
                    UINT msg, WPARAM wParam, LPARAM /*lParam*/) override
    {
        switch (msg)
        {
        case WM_INITDIALOG:
            for (int r = 0; r < 6; ++r)
            {
                HWND cb = GetDlgItem(hWnd, kComboIDs[r]);
                if (cb) { FillCombo(cb); SyncCombo(hWnd, r, map); }
            }
            break;

        case WM_COMMAND:
            if (HIWORD(wParam) == CBN_SELCHANGE)
            {
                const int ctrlID = LOWORD(wParam);
                for (int r = 0; r < 6; ++r)
                {
                    if (ctrlID != kComboIDs[r]) continue;
                    HWND cb = GetDlgItem(hWnd, ctrlID);
                    int sel = (int)SendMessage(cb, CB_GETCURSEL, 0, 0);
                    if (sel >= 0 && map && map->GetParamBlock())
                        map->GetParamBlock()->SetValue(kProjIDs[r], t, sel);
                    return TRUE;
                }
            }
            break;
        }
        return FALSE;
    }

    void DeleteThis() override {} // static instance — never heap-allocated
};

static EZRowDlgProc g_RowDlgProc;

// ---------------------------------------------------------------------------
// ParamBlockDesc2 macro — one row at a time
// proj is TYPE_INT with no p_ui (handled by EZRowDlgProc combobox)
// ---------------------------------------------------------------------------

#define ROW_PARAMS(N, defCh, defProj)                                                            \
    pb_en##N,    _T("en"#N),    TYPE_BOOL,  P_ANIMATABLE, IDS_EN##N,                            \
        p_default, TRUE,                                                                          \
        p_ui, TYPE_SINGLECHEKBOX, IDC_CHK_EN_##N,                                                \
    p_end,                                                                                        \
    pb_ch##N,    _T("ch"#N),    TYPE_INT,   P_ANIMATABLE, IDS_CH##N,                            \
        p_default, defCh, p_range, 1, 99,                                                        \
        p_ui, TYPE_SPINNER, EDITTYPE_INT, IDC_EDIT_CH_##N, IDC_SPIN_CH_##N, SPIN_AUTOSCALE,     \
    p_end,                                                                                        \
    pb_proj##N,  _T("proj"#N),  TYPE_INT,   P_ANIMATABLE, IDS_PR##N,                            \
        p_default, defProj, p_range, 0, 5,                                                       \
    p_end,                                                                                        \
    pb_uTile##N, _T("ut"#N),    TYPE_FLOAT, P_ANIMATABLE, IDS_UT##N,                            \
        p_default, 1.0f, p_range, -9999.0f, 9999.0f,                                            \
        p_ui, TYPE_SPINNER, EDITTYPE_FLOAT, IDC_EDIT_UT_##N,   IDC_SPIN_UT_##N,   SPIN_AUTOSCALE, \
    p_end,                                                                                        \
    pb_vTile##N, _T("vt"#N),    TYPE_FLOAT, P_ANIMATABLE, IDS_VT##N,                            \
        p_default, 1.0f, p_range, -9999.0f, 9999.0f,                                            \
        p_ui, TYPE_SPINNER, EDITTYPE_FLOAT, IDC_EDIT_VT_##N,   IDC_SPIN_VT_##N,   SPIN_AUTOSCALE, \
    p_end,                                                                                        \
    pb_uOff##N,  _T("uoff"#N),  TYPE_FLOAT, P_ANIMATABLE, IDS_UOFF##N,                          \
        p_default, 0.0f, p_range, -9999.0f, 9999.0f,                                            \
        p_ui, TYPE_SPINNER, EDITTYPE_FLOAT, IDC_EDIT_UOFF_##N, IDC_SPIN_UOFF_##N, SPIN_AUTOSCALE, \
    p_end,                                                                                        \
    pb_vOff##N,  _T("voff"#N),  TYPE_FLOAT, P_ANIMATABLE, IDS_VOFF##N,                          \
        p_default, 0.0f, p_range, -9999.0f, 9999.0f,                                            \
        p_ui, TYPE_SPINNER, EDITTYPE_FLOAT, IDC_EDIT_VOFF_##N, IDC_SPIN_VOFF_##N, SPIN_AUTOSCALE, \
    p_end,                                                                                        \
    pb_flipU##N, _T("flipU"#N), TYPE_BOOL,  P_ANIMATABLE, IDS_FLIPU##N,                         \
        p_default, FALSE, p_ui, TYPE_SINGLECHEKBOX, IDC_CHK_FLIPU_##N,                          \
    p_end,                                                                                        \
    pb_flipV##N, _T("flipV"#N), TYPE_BOOL,  P_ANIMATABLE, IDS_FLIPV##N,                         \
        p_default, FALSE, p_ui, TYPE_SINGLECHEKBOX, IDC_CHK_FLIPV_##N,                          \
    p_end,                                                                                        \
    pb_swap##N,  _T("swap"#N),  TYPE_BOOL,  P_ANIMATABLE, IDS_SWAP##N,                          \
        p_default, FALSE, p_ui, TYPE_SINGLECHEKBOX, IDC_CHK_SWAP_##N,                           \
    p_end,

static ParamBlockDesc2 g_MainPBlock
(
    kMainPBlock, _T("params"), IDS_PARAMS, &g_EZMultiPlanarUVWDesc,
    P_AUTO_CONSTRUCT | P_AUTO_UI,
    0,
    IDD_PANEL, IDS_PARAMS, 0, 0, &g_RowDlgProc,

    // Axis-pair defaults: 1-2 → ch1 (X+/X-), 3-4 → ch2 (Y+/Y-), 5-6 → ch3 (Z+/Z-)
    ROW_PARAMS(1, 1, 0)   // X+
    ROW_PARAMS(2, 1, 1)   // X-
    ROW_PARAMS(3, 2, 2)   // Y+
    ROW_PARAMS(4, 2, 3)   // Y-
    ROW_PARAMS(5, 3, 4)   // Z+
    ROW_PARAMS(6, 3, 5)   // Z-

    // Seam options
    pb_normalThreshold, _T("threshold"), TYPE_FLOAT, P_ANIMATABLE, IDS_THRESH,
        p_default, 0.001f, p_range, 0.0f, 1.0f,
        p_ui, TYPE_SPINNER, EDITTYPE_FLOAT, IDC_EDIT_THRESH, IDC_SPIN_THRESH, SPIN_AUTOSCALE,
    p_end,
    pb_mergeIslands, _T("mergeIslands"), TYPE_BOOL, P_ANIMATABLE, IDS_MERGE,
        p_default, TRUE, p_ui, TYPE_SINGLECHEKBOX, IDC_CHK_MERGE,
    p_end,
    pb_parkNonMatching, _T("parkNonMatching"), TYPE_BOOL, P_ANIMATABLE, IDS_PARK_NM,
        p_default, TRUE, p_ui, TYPE_SINGLECHEKBOX, IDC_CHK_PARK,
    p_end,
    pb_parkU, _T("parkU"), TYPE_FLOAT, P_ANIMATABLE, IDS_PARKU,
        p_default, -1.0f, p_range, -9999.0f, 9999.0f,
        p_ui, TYPE_SPINNER, EDITTYPE_FLOAT, IDC_EDIT_PARKU, IDC_SPIN_PARKU, SPIN_AUTOSCALE,
    p_end,
    pb_parkV, _T("parkV"), TYPE_FLOAT, P_ANIMATABLE, IDS_PARKV,
        p_default, -1.0f, p_range, -9999.0f, 9999.0f,
        p_ui, TYPE_SPINNER, EDITTYPE_FLOAT, IDC_EDIT_PARKV, IDC_SPIN_PARKV, SPIN_AUTOSCALE,
    p_end,

    // Blend output
    pb_enableBlend,  _T("enableBlend"),  TYPE_BOOL,  P_ANIMATABLE, IDS_EN_BLEND,
        p_default, FALSE, p_ui, TYPE_SINGLECHEKBOX, IDC_CHK_EN_BLEND,
    p_end,
    pb_channelBlend, _T("channelBlend"), TYPE_INT,   P_ANIMATABLE, IDS_CH_BLEND,
        p_default, 10, p_range, 1, 99,
        p_ui, TYPE_SPINNER, EDITTYPE_INT, IDC_EDIT_CH_BLEND, IDC_SPIN_CH_BLEND, SPIN_AUTOSCALE,
    p_end,
    pb_blendPower,   _T("blendPower"),   TYPE_FLOAT, P_ANIMATABLE, IDS_POWER,
        p_default, 1.0f, p_range, 0.001f, 32.0f,
        p_ui, TYPE_SPINNER, EDITTYPE_FLOAT, IDC_EDIT_POWER, IDC_SPIN_POWER, SPIN_AUTOSCALE,
    p_end,
    pb_showBlend,    _T("showBlend"),    TYPE_BOOL,  P_ANIMATABLE, IDS_SHOW,
        p_default, FALSE, p_ui, TYPE_SINGLECHEKBOX, IDC_CHK_SHOW,
    p_end,

    p_end
);

// ---------------------------------------------------------------------------
// DLL entry points
// ---------------------------------------------------------------------------

BOOL WINAPI DllMain(HINSTANCE hinstDLL, ULONG fdwReason, LPVOID)
{
    if (fdwReason == DLL_PROCESS_ATTACH) { hInstance = hinstDLL; DisableThreadLibraryCalls(hInstance); }
    return TRUE;
}

extern "C"
{
    __declspec(dllexport) const TCHAR* LibDescription()   { return _T("EZ Multi Planar UVW Modifier"); }
    __declspec(dllexport) int          LibNumberClasses()  { return 1; }
    __declspec(dllexport) ClassDesc*   LibClassDesc(int i) { return i==0 ? GetEZMultiPlanarUVWDesc() : nullptr; }
    __declspec(dllexport) ULONG        LibVersion()        { return VERSION_3DSMAX; }
    __declspec(dllexport) ULONG        CanAutoDefer()      { return 1; }
}
