/*
    EZ_AOCombine_Test.ms
    Demonstrates the Combine modes on "EZ AO" (the simple one).

    It stacks TWO EZ AO modifiers on the same channel (11):
        BASE  - height-driven AO  (Replace)
        TOP   - cavity-driven AO  (Combine = your choice)
    The TOP reads what BASE wrote and blends per the Combine mode, so you can
    watch Replace / Multiply / Min / Max / Add / Subtract live.

    "Preview on object" turns on the TOP modifier's ch0 preview and the node's
    vertex-colour display, so the COMBINED result shows directly in the viewport
    (no material needed). Flip the Combine dropdown and watch it update.

    Select a mesh first (e.g. EZ_Test_ChamferBox from EZ_TestScene.ms); if
    nothing is selected it builds a chamfer box to demo on.
*/

global EZ_AOCombine_Rollout

fn ezc_norm s =
(
    local o = ""
    for i = 1 to s.count do
    (
        local ch = s[i]
        if ((ch >= "0" and ch <= "9") or (ch >= "a" and ch <= "z") or (ch >= "A" and ch <= "Z")) do o += ch
    )
    toLower o
)
fn ezc_classByName displayName =
(
    local t = ezc_norm displayName
    for c in modifier.classes do if (ezc_norm (c as string)) == t do return c
    undefined
)

rollout EZ_AOCombine_Rollout "EZ AO - Combine Demo" width:250
(
    local DEMO_CH = 11
    local baseMod = undefined
    local topMod  = undefined
    local obj     = undefined

    label lbl1 "Combine modes: Replace/Multiply/Min/" align:#left
    label lbl2 "Max/Add/Subtract on a 2-layer AO stack." align:#left
    button btn_build "Build / Rebuild Demo Stack" width:236 height:24

    group "Top layer (cavity)"
    (
        dropdownlist ddl_mode "Combine:" items:#("Replace","Multiply","Min","Max","Add","Subtract") width:236
        spinner spn_cav "Cavity wt:" range:[0,1,1.0] type:#float fieldwidth:46 align:#left
        spinner spn_str "Strength:" range:[0,8,1.0] type:#float fieldwidth:46 align:#left
    )
    group "Base layer (height)"
    (
        spinner spn_hgt "Height wt:" range:[0,1,1.0] type:#float fieldwidth:46 align:#left
    )

    checkbox chk_prev "Preview combined result on object (ch0)" checked:true
    button btn_viz "Viz ch11 -> Gray Copy" width:236 height:22
    button btn_clear "Remove EZ AO mods from object" width:236 height:22

    fn ezc_getClass =
    (
        local c = ezc_classByName "EZ AO"
        if c == undefined do messageBox "Could not find the 'EZ AO' modifier class - is EZ_BoxTri.dlm loaded?" title:"EZ AO Combine"
        c
    )

    fn ezc_removeEZAO o =
    (
        local cls = ezc_classByName "EZ AO"
        if cls == undefined do return 0
        local removed = 0
        for i = o.modifiers.count to 1 by -1 do
            if (classof o.modifiers[i]) == cls do ( deleteModifier o i; removed += 1 )
        removed
    )

    fn ezc_buildStack =
    (
        local cls = ezc_getClass()
        if cls == undefined do return false
        if selection.count >= 1 then obj = selection[1]
        else
        (
            obj = ChamferBox length:44 width:44 height:44 fillet:4 filletSegs:3 name:"EZ_AOCombine_Demo"
            obj.pos = [0,0,26]
            select obj
        )
        ezc_removeEZAO obj

        baseMod = cls()
        baseMod.name = "EZ AO BASE (height)"
        baseMod.aoChannel = DEMO_CH
        baseMod.cavity = 0.0; baseMod.height = spn_hgt.value; baseMod.down = 0.0
        baseMod.strength = 1.0; baseMod.combine = 0     -- Replace
        baseMod.grayscale = true; baseMod.preview = false

        topMod = cls()
        topMod.name = "EZ AO TOP (cavity)"
        topMod.aoChannel = DEMO_CH
        topMod.cavity = spn_cav.value; topMod.height = 0.0; topMod.down = 0.0
        topMod.strength = spn_str.value; topMod.combine = (ddl_mode.selection - 1)
        topMod.grayscale = true; topMod.preview = chk_prev.checked

        addModifier obj baseMod     -- added first -> lower in stack -> runs first
        addModifier obj topMod      -- added second -> top -> runs last, reads base

        obj.showVertexColors = chk_prev.checked
        try obj.vertexColorType = 0 catch()
        try forceCompleteRedraw() catch()
        format "Built combine demo on '%': BASE(height) + TOP(cavity, %).\n" \
            obj.name (ddl_mode.items[ddl_mode.selection])
        true
    )

    fn ezc_alive = ( obj != undefined and (isValidNode obj) and topMod != undefined and baseMod != undefined )

    on btn_build pressed do ezc_buildStack()

    on ddl_mode selected i do
        if ezc_alive() do ( topMod.combine = (i - 1); try forceCompleteRedraw() catch()
            format "Combine -> %\n" ddl_mode.items[i] )

    on spn_cav changed v do if ezc_alive() do ( topMod.cavity = v; try forceCompleteRedraw() catch() )
    on spn_str changed v do if ezc_alive() do ( topMod.strength = v; try forceCompleteRedraw() catch() )
    on spn_hgt changed v do if ezc_alive() do ( baseMod.height = v; try forceCompleteRedraw() catch() )

    on chk_prev changed st do
        if ezc_alive() do
        (
            topMod.preview = st
            obj.showVertexColors = st
            try forceCompleteRedraw() catch()
        )

    on btn_clear pressed do
        if obj != undefined and (isValidNode obj) do
        (
            local n = ezc_removeEZAO obj
            try obj.showVertexColors = false catch()
            baseMod = undefined; topMod = undefined
            try forceCompleteRedraw() catch()
            format "Removed % EZ AO modifier(s).\n" n
        )

    -- grayscale copy from ch11 Red beside the object
    on btn_viz pressed do
    (
        if obj == undefined or (not isValidNode obj) do ( messageBox "Build the demo first." title:"EZ AO Combine"; return false )
        local tm = snapshotAsMesh obj
        local ch = DEMO_CH
        if not (meshop.getMapSupport tm ch) do ( messageBox ("Channel " + ch as string + " is empty.") title:"EZ AO Combine"; free tm; return false )
        local viz = Editable_Mesh()
        viz.mesh = copy tm
        viz.name = uniqueName (obj.name + "_CH11_VIZ")
        viz.transform = obj.transform
        try viz.pos = obj.pos + [obj.max.x - obj.min.x + 10, 0, 0] catch()
        local m = viz.mesh
        local nf = m.numfaces
        setNumCPVVerts m (nf*3); buildVCFaces m
        for f = 1 to nf do
        (
            local mf = meshop.getMapFace tm ch f
            local base = (f-1)*3
            for c = 1 to 3 do
            (
                local mv = meshop.getMapVert tm ch (mf[c] as integer)
                local g = (amin (amax mv.x 0.0) 1.0)
                setVertColor m (base+c) (color (g*255) (g*255) (g*255))
            )
            setVCFace m f (base+1) (base+2) (base+3)
        )
        update viz
        viz.showVertexColors = true
        try viz.vertexColorType = 0 catch()
        free tm
        try forceCompleteRedraw() catch()
        format "Built % from ch11.\n" viz.name
    )

    on EZ_AOCombine_Rollout open do ddl_mode.selection = 5   -- default to "Add" for an obvious effect
)

createDialog EZ_AOCombine_Rollout 250 312
