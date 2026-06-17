/*
    EZ_Apply.ms
    Launcher + channel inspector for the EZ Tools family.

    - Lists every EZ modifier class registered in this Max session (found by
      name, so it works regardless of how Max spells the MAXScript class) and
      adds the chosen one to the selected objects.
    - Reports the data in any map channel (min/max/avg of R/G/B) on the
      evaluated mesh.
    - Builds a grayscale "viz copy" from a channel's Red (e.g. AO in ch11,
      a mask in ch12) so you can SEE it in the viewport without a material.

    All read operations are non-destructive (they snapshot the mesh).
*/

global EZ_Apply_Rollout

-- normalize a class name to lowercase alphanumerics ("EZ AO" / "EZ_AO" -> "ezao")
fn ez_norm s =
(
    local o = ""
    for i = 1 to s.count do
    (
        local ch = s[i]
        if ((ch >= "0" and ch <= "9") or (ch >= "a" and ch <= "z") or (ch >= "A" and ch <= "Z")) do o += ch
    )
    toLower o
)

-- every modifier class whose name begins with "ez"
fn ez_listClasses =
(
    local out = #()
    for c in modifier.classes do
        if (findString (ez_norm (c as string)) "ez") == 1 do append out c
    out
)

fn ez_classByName displayName =
(
    local t = ez_norm displayName
    for c in modifier.classes do if (ez_norm (c as string)) == t do return c
    undefined
)

fn ez_snap o =
(
    local tm = undefined
    try tm = snapshotAsMesh o catch tm = undefined
    tm
)

fn ez_reportChannel ch =
(
    if selection.count < 1 do ( messageBox "Select an object first." title:"EZ Apply"; return false )
    local o = selection[1]
    local tm = ez_snap o
    if tm == undefined do ( messageBox "Could not evaluate mesh." title:"EZ Apply"; return false )
    local supported = false
    try supported = (meshop.getMapSupport tm ch) catch supported = false
    if not supported do ( format "Object %: channel % has NO data.\n" o.name ch; free tm; return false )
    local n = 0
    try n = (meshop.getNumMapVerts tm ch) catch n = 0
    if n == 0 do ( format "Object %: channel % present but empty.\n" o.name ch; free tm; return false )
    local rmn=1e9; local rmx=-1e9; local gmn=1e9; local gmx=-1e9; local bmn=1e9; local bmx=-1e9
    local rs=0.0; local gs=0.0; local bs=0.0
    for i = 1 to n do
    (
        local p = meshop.getMapVert tm ch i
        rmn=amin rmn p.x; rmx=amax rmx p.x; rs+=p.x
        gmn=amin gmn p.y; gmx=amax gmx p.y; gs+=p.y
        bmn=amin bmn p.z; bmx=amax bmx p.z; bs+=p.z
    )
    format "----- % : channel % (% map-verts) -----\n" o.name ch n
    format "  R(U): min % max % avg %\n" rmn rmx (rs/n)
    format "  G(V): min % max % avg %\n" gmn gmx (gs/n)
    format "  B(W): min % max % avg %\n" bmn bmx (bs/n)
    free tm
    true
)

-- grayscale copy from a channel's Red (.x), per vertex, beside the original
fn ez_vizChannel ch =
(
    if selection.count < 1 do ( messageBox "Select an object first." title:"EZ Apply"; return false )
    local o = selection[1]
    local tm = ez_snap o
    if tm == undefined do return false
    local supported = false
    try supported = (meshop.getMapSupport tm ch) catch supported = false
    if not supported do ( messageBox ("Channel " + ch as string + " has no data on this object.") title:"EZ Apply"; free tm; return false )

    local viz = Editable_Mesh()
    viz.mesh = copy tm
    viz.name = uniqueName (o.name + "_CH" + ch as string + "_VIZ")
    viz.transform = o.transform
    try viz.pos = o.pos + [o.max.x - o.min.x + 10, 0, 0] catch()

    local m = viz.mesh
    local nf = m.numfaces
    -- read channel per face-corner, write greyscale to vertex colour ch0
    setNumCPVVerts m (nf*3)
    buildVCFaces m
    for f = 1 to nf do
    (
        local mf = meshop.getMapFace tm ch f
        local base = (f-1)*3
        for c = 1 to 3 do
        (
            local mv = meshop.getMapVert tm ch (mf[c] as integer)
            local g = (amin (amax mv.x 0.0) 1.0)
            setVertColor m (base + c) (color (g*255) (g*255) (g*255))
        )
        setVCFace m f (base+1) (base+2) (base+3)
    )
    update viz
    viz.showVertexColors = true
    try viz.vertexColorType = 0 catch()
    free tm
    try forceCompleteRedraw() catch()
    format "Built % (grayscale of channel % Red). Original untouched.\n" viz.name ch
    true
)

try destroyDialog EZ_Apply_Rollout catch()

rollout EZ_Apply_Rollout "EZ Tools - Apply & Inspect" width:250
(
    label  lblA "Add EZ modifier to selected:" align:#left
    dropdownlist ddl_class "" width:236 align:#center
    button btn_add "Add to Selected" width:236 height:24

    group "Channel inspector"
    (
        spinner spn_ch "Channel:" range:[0,99,11] type:#integer fieldwidth:42 align:#left
        button btn_report "Report Channel Data" width:222 height:22
        button btn_viz    "Viz Channel -> Gray Copy" width:222 height:22
    )

    local ezClasses = #()

    fn refresh =
    (
        ezClasses = ez_listClasses()
        local names = for c in ezClasses collect (c as string)
        if names.count == 0 do names = #("(no EZ classes found - is the plugin loaded?)")
        ddl_class.items = names
    )

    on EZ_Apply_Rollout open do refresh()

    on btn_add pressed do
    (
        if selection.count < 1 then ( messageBox "Select object(s) first." title:"EZ Apply" )
        else if ezClasses.count == 0 then ( messageBox "No EZ modifier classes found." title:"EZ Apply" )
        else
        (
            local c = ezClasses[ddl_class.selection]
            local added = 0
            for o in selection do ( try ( addModifier o (c()); added += 1 ) catch() )
            format "Added '%' to % object(s).\n" (c as string) added
            try forceCompleteRedraw() catch()
        )
    )

    on btn_report pressed do ez_reportChannel spn_ch.value
    on btn_viz    pressed do ez_vizChannel spn_ch.value
)

createDialog EZ_Apply_Rollout 250 250
