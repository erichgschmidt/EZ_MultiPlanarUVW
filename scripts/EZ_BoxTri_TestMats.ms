/*
    EZ_BoxTri_TestMats.ms
    Helper rollout for testing the EZ_BoxTri plugin's channel output.

    Drop on a viewport (or run from MAXScript Editor) to open the panel.
    Select an object with EZ BoxTri applied, then click any of the buttons
    to swap in a test material that visualises a specific channel.

    Each material is self-illuminated 100% so lighting doesn't muddy the
    colour data, and showInViewport is forced ON.
*/

global EZ_BoxTri_TestRollout

fn EZ_BoxTri_GetSelected =
(
    if selection.count < 1 then
    (
        messageBox "Select an object with EZ BoxTri applied first." title:"EZ BoxTri Test"
        undefined
    )
    else
    (
        selection[1]
    )
)

-- Find the EZ BoxTri modifier on a node by capability (has .previewMode).
-- Robust against the modifier's display name changing.
fn EZ_BoxTri_FindMod n =
(
    local found = undefined
    if n != undefined do
    (
        for m in n.modifiers while found == undefined do
        (
            local hasIt = false
            try ( local probe = m.previewMode; hasIt = true ) catch hasIt = false
            if hasIt do found = m
        )
    )
    found
)

-- Set the modifier's previewMode (1-5) and force vertex-channel display of
-- the preview channel so the result shows live in the viewport.
fn EZ_BoxTri_SetPreviewMode modeVal =
(
    local n = EZ_BoxTri_GetSelected()
    if n == undefined do return false

    local mod = EZ_BoxTri_FindMod n
    if mod == undefined do
    (
        messageBox "No EZ BoxTri modifier found on the selected object." title:"EZ BoxTri Test"
        return false
    )

    local prevCh = 0
    try prevCh = mod.previewCh catch prevCh = 0

    try mod.previewMode = modeVal catch
    (
        format "Could not set previewMode. Exception:%\n" (getCurrentException())
    )

    -- Show whatever channel the preview is writing to.
    try n.showVertexColors = true catch()
    try n.vertexColorType = 0 catch()
    try n.vertexColorMapChannel = prevCh catch()

    -- A plain self-illuminated white material lets vertex-color display read
    -- through cleanly (no diffuse map fighting the VC display).
    try
    (
        if n.material == undefined or (findString (n.material.name as string) "BoxTri VCD Passthrough") == undefined do
        (
            local m = StandardMaterial name:"BoxTri VCD Passthrough"
            m.diffuse = white
            try m.selfIllumAmount = 100 catch()
            n.material = m
        )
    )
    catch()

    try update n catch()
    try forceCompleteRedraw() catch()
    format "Preview mode set to % on % (showing ch %)\n" modeVal n.name prevCh
    true
)

fn EZ_BoxTri_MakeVCMat matName mapCh =
(
    -- StandardMaterial with diffuseMap = Vertex_Color reading the given channel.
    -- IMPORTANT: Vertex_Color has a subchannel mode (vertexColorType):
    --     0 = Vertex Color (= map ch 0 = legacy vertCol storage)
    --     1 = Vertex Illumination (ch -1)
    --     2 = Vertex Alpha          (ch -2)
    --     3 = Map Channel  (then .mapChannel picks 1..99)
    -- Setting .mapChannel alone is ignored unless mode is also 3.
    local m = StandardMaterial name:matName
    local vc = Vertex_Color()
    if mapCh == 0 then
    (
        try vc.vertexColorType = 0 catch()
    )
    else
    (
        try vc.vertexColorType = 3 catch()
        try vc.mapChannel = mapCh catch()
        try vc.map_channel = mapCh catch()
    )
    m.diffuseMap = vc
    m.diffuse = white
    try m.selfIllumAmount = 100 catch()
    try m.showInViewport = true catch()
    try showTextureMap m vc true catch()
    m
)

fn EZ_BoxTri_MakeCheckerMat matName mapCh tileVal =
(
    local m = StandardMaterial name:matName
    local ck = Checker()
    try ck.coords.mapChannel = mapCh catch()
    try ck.coords.U_Tiling = tileVal catch()
    try ck.coords.V_Tiling = tileVal catch()
    try ck.color1 = color 30 30 30 catch()
    try ck.color2 = color 230 230 230 catch()
    m.diffuseMap = ck
    m.diffuse = white
    try m.selfIllumAmount = 100 catch()
    try m.showInViewport = true catch()
    try showTextureMap m ck true catch()
    m
)

fn EZ_BoxTri_Apply matVal =
(
    local n = EZ_BoxTri_GetSelected()
    if n == undefined do return false
    n.material = matVal
    -- CRITICAL: turn vertex-color display OFF for material/checker tests.
    -- If a "LIVE Preview Mode" button left it ON, the viewport composites
    -- the vertex colours over the material and the checker looks tinted.
    try n.showVertexColors = false catch()
    try update n catch()
    try forceCompleteRedraw() catch()
    true
)

try destroyDialog EZ_BoxTri_TestRollout catch()

rollout EZ_BoxTri_TestRollout "EZ BoxTri Test Materials" width:260
(
    group "LIVE Preview Mode (viewport, ch 0)"
    (
        label lbl_live1 "Drives the modifier's Mode + shows it live." align:#left
        button btn_mode1 "1: WallX=R WallY=G Floor=B" width:240 height:24
        button btn_mode3 "3: Wall=R Floor=G Ceiling=B" width:240 height:24
        button btn_mode5 "5: Dominant Debug (R/G/B/yellow)" width:240 height:24
        button btn_mode2 "2: WallX=R WallY=G Ceiling=B" width:118 height:22 across:2
        button btn_mode4 "4: WX=R WY=G Fl+Cl=B" width:118 height:22
        label lbl_live2 "Mode 1: red=+/-X  green=+/-Y  blue=top  black=bottom" align:#left
    )

    group "Blend Channel (ch 10) — render to verify"
    (
        label lbl_blend  "R=WallX  G=WallY  B=Floor  black=Ceiling" align:#left
        label lbl_blend2 "Apply, then press F9 to render (viewport may fall back to ch0)." align:#left
        spinner spn_blendCh "Blend Ch:" range:[0,99,10] type:#integer fieldwidth:40 align:#left
        button btn_blendVC "Apply Blend-Channel RGB Material" width:240 height:26
    )

    group "Preview Channel — uses preview-mode remap"
    (
        spinner spn_prevCh  "Preview Ch:" range:[0,99,0] type:#integer fieldwidth:40 align:#left
        button btn_prevVC  "Show Preview Channel as RGB" width:240 height:26
        label lbl_prev "Default ch0 = vertex color. Try ch11/12 for extra previews." align:#left
    )

    group "Per-Layer UV Checker"
    (
        label lbl_layers "Pick which UV channel to checker." align:#left
        spinner spn_uvCh "UV Channel:" range:[1,99,1] type:#integer fieldwidth:40 align:#left across:2
        spinner spn_tile "Tiling:"     range:[0.1,256.0,8.0] type:#float fieldwidth:50 align:#left
        button btn_check1 "Wall X (ch1)"  width:118 height:24 across:2
        button btn_check2 "Wall Y (ch2)"  width:118 height:24
        button btn_check3 "Floor (ch3)"   width:118 height:24 across:2
        button btn_check4 "Ceiling (ch4)" width:118 height:24
        button btn_checkAny "Apply Checker to spinner channel" width:240 height:24
    )

    group "Misc"
    (
        button btn_showVCD "Force Vertex Channel Display ON (ch 0)" width:240 height:24
        button btn_hideVCD "Vertex Channel Display OFF" width:240 height:24
        button btn_report  "Report Map Channels on Selected" width:240 height:24
    )

    on btn_mode1 pressed do EZ_BoxTri_SetPreviewMode 1
    on btn_mode2 pressed do EZ_BoxTri_SetPreviewMode 2
    on btn_mode3 pressed do EZ_BoxTri_SetPreviewMode 3
    on btn_mode4 pressed do EZ_BoxTri_SetPreviewMode 4
    on btn_mode5 pressed do EZ_BoxTri_SetPreviewMode 5

    on btn_blendVC pressed do
    (
        EZ_BoxTri_Apply (EZ_BoxTri_MakeVCMat ("BoxTri Blend ch" + (spn_blendCh.value as string)) spn_blendCh.value)
    )

    on btn_prevVC pressed do
    (
        EZ_BoxTri_Apply (EZ_BoxTri_MakeVCMat ("BoxTri Prev ch" + (spn_prevCh.value as string)) spn_prevCh.value)
    )

    on btn_check1 pressed do
        EZ_BoxTri_Apply (EZ_BoxTri_MakeCheckerMat "BoxTri Checker ch1" 1 spn_tile.value)

    on btn_check2 pressed do
        EZ_BoxTri_Apply (EZ_BoxTri_MakeCheckerMat "BoxTri Checker ch2" 2 spn_tile.value)

    on btn_check3 pressed do
        EZ_BoxTri_Apply (EZ_BoxTri_MakeCheckerMat "BoxTri Checker ch3" 3 spn_tile.value)

    on btn_check4 pressed do
        EZ_BoxTri_Apply (EZ_BoxTri_MakeCheckerMat "BoxTri Checker ch4" 4 spn_tile.value)

    on btn_checkAny pressed do
        EZ_BoxTri_Apply (EZ_BoxTri_MakeCheckerMat ("BoxTri Checker ch" + (spn_uvCh.value as string)) spn_uvCh.value spn_tile.value)

    on btn_showVCD pressed do
    (
        local n = EZ_BoxTri_GetSelected()
        if n != undefined do
        (
            try n.showVertexColors = true catch()
            try n.vertexColorType = 0 catch()
            try n.vertexColorMapChannel = 0 catch()
            try forceCompleteRedraw() catch()
            format "Vertex Channel Display ON, reading ch 0\n"
        )
    )

    on btn_hideVCD pressed do
    (
        local n = EZ_BoxTri_GetSelected()
        if n != undefined do
        (
            try n.showVertexColors = false catch()
            try forceCompleteRedraw() catch()
            format "Vertex Channel Display OFF\n"
        )
    )

    on btn_report pressed do
    (
        local n = EZ_BoxTri_GetSelected()
        if n == undefined do return false
        local tm = snapshotAsMesh n
        format "----- EZ BoxTri channel report for: % -----\n" n.name
        format "Verts: % Faces: % NumMaps: %\n" tm.numverts tm.numfaces (meshop.getNumMaps tm)
        for ch = 0 to 12 do
        (
            local sup = false
            local mv = 0
            try sup = meshop.getMapSupport tm ch catch()
            try mv  = meshop.getNumMapVerts tm ch catch()
            if sup or mv > 0 do format "  ch %  support:% mapVerts:%\n" ch sup mv
        )
        free tm
        true
    )
)

createDialog EZ_BoxTri_TestRollout 260 620
