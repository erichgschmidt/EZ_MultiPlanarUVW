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

global EZ_BoxTri_Floater

try (closeRolloutFloater EZ_BoxTri_Floater) catch()
try (destroyDialog EZ_BoxTri_TestRollout) catch()

rollout EZ_BoxTri_TestRollout "EZ BoxTri Test Materials" width:248
(
    group "LIVE Preview (viewport, ch 0)"
    (
        button btn_mode1 "1: WallX=R WallY=G Floor=B" width:230 height:22
        button btn_mode3 "3: Wall=R Floor=G Ceil=B" width:230 height:22
        button btn_mode5 "5: Dominant (R/G/B/yellow)" width:230 height:22
        button btn_mode2 "2: WX/WY/Ceil" width:113 height:20 across:2
        button btn_mode4 "4: WX/WY/Fl+Cl" width:113 height:20
        label lbl_live "Mode 1: red=+/-X grn=+/-Y blu=top blk=btm" align:#left
    )

    group "Blend Channel (render w/ F9)"
    (
        spinner spn_blendCh "Blend Ch:" range:[0,99,10] type:#integer fieldwidth:38 align:#left
        button btn_blendVC "Apply Blend RGB Material" width:230 height:22
    )

    group "Per-Layer UV Checker"
    (
        spinner spn_tile "Tiling:" range:[0.1,256.0,8.0] type:#float fieldwidth:50 align:#left
        button btn_check1 "Wall X 1" width:113 height:20 across:2
        button btn_check2 "Wall Y 2" width:113 height:20
        button btn_check3 "Floor 3"  width:113 height:20 across:2
        button btn_check4 "Ceiling 4" width:113 height:20
        spinner spn_uvCh "Any Ch:" range:[1,99,1] type:#integer fieldwidth:38 align:#left across:2
        button btn_checkAny "Checker Any" width:113 height:20
    )

    group "Misc"
    (
        button btn_showVCD "Vtx Display ON (ch0)" width:113 height:20 across:2
        button btn_hideVCD "Vtx Display OFF" width:113 height:20
        button btn_report  "Report Channels" width:230 height:20
    )

    on btn_mode1 pressed do EZ_BoxTri_SetPreviewMode 1
    on btn_mode2 pressed do EZ_BoxTri_SetPreviewMode 2
    on btn_mode3 pressed do EZ_BoxTri_SetPreviewMode 3
    on btn_mode4 pressed do EZ_BoxTri_SetPreviewMode 4
    on btn_mode5 pressed do EZ_BoxTri_SetPreviewMode 5

    on btn_blendVC pressed do
        EZ_BoxTri_Apply (EZ_BoxTri_MakeVCMat ("BoxTri Blend ch" + (spn_blendCh.value as string)) spn_blendCh.value)

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
        )
    )

    on btn_hideVCD pressed do
    (
        local n = EZ_BoxTri_GetSelected()
        if n != undefined do
        (
            try n.showVertexColors = false catch()
            try forceCompleteRedraw() catch()
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

-- Rollout floater scrolls when content exceeds the visible height,
-- unlike createDialog which clips.
EZ_BoxTri_Floater = newRolloutFloater "EZ BoxTri Test" 268 460
addRollout EZ_BoxTri_TestRollout EZ_BoxTri_Floater
