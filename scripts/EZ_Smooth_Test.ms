/*
    EZ_Smooth_Test.ms
    Diagnostic rollout for the EZ Smoothing Groups modifier (non-destructive).

    Drop on a viewport (or run from the MAXScript Editor). Select an object
    with EZ Smoothing Groups applied, then:
      - Report Groups : count distinct group bits + faces per bit on the
                        EVALUATED mesh (snapshot; original untouched).
      - Count In Bit  : how many faces carry a given group bit.
      - Viz Copy      : drop a colour-per-group COPY of the mesh next to the
                        original so you can SEE the islands (original untouched).

    The shaded viewport already shows the result directly: hard edges look
    faceted, smooth edges look rounded. The report just quantifies it.
*/

global EZ_Smooth_Test_Rollout

fn EZ_Smooth_Snap =
(
    if selection.count < 1 then ( messageBox "Select an object first." title:"EZ Smooth Test"; undefined )
    else
    (
        local n = selection[1]
        local tm = undefined
        try tm = snapshotAsMesh n catch tm = undefined
        if tm == undefined then ( messageBox "Could not evaluate mesh." title:"EZ Smooth Test"; undefined )
        else #(n, tm)
    )
)

fn EZ_Smooth_Report =
(
    local r = EZ_Smooth_Snap()
    if r == undefined do return false
    local n = r[1]; local tm = r[2]
    local nf = tm.numfaces
    local distinct = #()
    local bitCount = for i in 1 to 32 collect 0
    local zeroFaces = 0

    for f = 1 to nf do
    (
        local sg = getFaceSmoothGroup tm f
        if sg == 0 do zeroFaces += 1
        if (findItem distinct sg) == 0 do append distinct sg
        for bit = 0 to 31 do
            if (bit.and sg (bit.shift 1 bit)) != 0 do bitCount[bit + 1] += 1
    )

    local usedBits = 0
    for bit = 1 to 32 do if bitCount[bit] > 0 do usedBits += 1

    format "----- EZ Smoothing Groups: % -----\n" n.name
    format "Faces: %   Distinct masks: %   Faces with NO group: %\n" nf distinct.count zeroFaces
    format "Group BITS in use: % of 32\n" usedBits
    for bit = 1 to 32 do
        if bitCount[bit] > 0 do format "  bit %  : % faces\n" bit bitCount[bit]
    if zeroFaces > 0 do format "NOTE: % faces have smoothing group 0 (all-hard edges).\n" zeroFaces
    free tm
    true
)

fn EZ_Smooth_CountInBit grp =
(
    local r = EZ_Smooth_Snap()
    if r == undefined do return false
    local tm = r[2]
    local mask = bit.shift 1 (grp - 1)
    local cnt = 0
    for f = 1 to tm.numfaces do
        if (bit.and (getFaceSmoothGroup tm f) mask) != 0 do cnt += 1
    format "Group bit %: % of % faces carry it.\n" grp cnt tm.numfaces
    free tm
    true
)

-- Non-destructive: build a colour-per-group COPY beside the original.
fn EZ_Smooth_VizCopy =
(
    local r = EZ_Smooth_Snap()
    if r == undefined do return false
    local n = r[1]; local tm = r[2]
    local nf = tm.numfaces

    local viz = Editable_Mesh()
    viz.mesh = copy tm
    viz.name = uniqueName (n.name + "_SG_VIZ")
    viz.transform = n.transform
    try viz.pos = n.pos + [n.max.x - n.min.x + 10, 0, 0] catch()

    local m = viz.mesh
    local masks = #(); local cols = #()
    setNumCPVVerts m (nf * 3)
    buildVCFaces m
    for f = 1 to nf do
    (
        local sg = getFaceSmoothGroup m f
        local idx = findItem masks sg
        if idx == 0 do
        (
            append masks sg
            local h = (mod (masks.count * 0.61803398875) 1.0) * 360.0
            local c = color (mod (masks.count*97) 255) (mod (masks.count*53+80) 255) (mod (masks.count*29+160) 255)
            try c = (colorHsv h 200 235) catch()
            append cols c
            idx = masks.count
        )
        local fv = (f - 1) * 3
        setVertColor m (fv + 1) cols[idx]
        setVertColor m (fv + 2) cols[idx]
        setVertColor m (fv + 3) cols[idx]
        setVCFace m f (fv + 1) (fv + 2) (fv + 3)
    )
    update viz
    viz.showVertexColors = true
    try viz.vertexColorType = 0 catch()
    free tm
    try forceCompleteRedraw() catch()
    format "Built % (colour per smoothing group, % groups). Original untouched.\n" viz.name masks.count
    true
)

try destroyDialog EZ_Smooth_Test_Rollout catch()

rollout EZ_Smooth_Test_Rollout "EZ Smoothing Groups Test" width:240
(
    button btn_report "Report Smoothing Groups" width:220 height:26
    spinner spn_group "Group bit:" range:[1,32,1] type:#integer fieldwidth:40 align:#left
    button btn_count  "Count Faces In Group Bit" width:220 height:22
    button btn_viz    "Viz Copy (colour per group)" width:220 height:26
    label  lbl "Viz makes a coloured COPY beside the original." align:#left

    on btn_report pressed do EZ_Smooth_Report()
    on btn_count  pressed do EZ_Smooth_CountInBit spn_group.value
    on btn_viz    pressed do EZ_Smooth_VizCopy()
)

createDialog EZ_Smooth_Test_Rollout 240 170
