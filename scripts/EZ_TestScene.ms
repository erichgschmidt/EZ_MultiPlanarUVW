/*
    EZ_TestScene.ms
    Builds a row of varied test meshes for the EZ Tools family. Non-destructive
    (creates new named objects; doesn't touch anything already in the scene).

    Each shape stresses a different signal:
      ChamferBox   - hard/soft edges  -> EZ Smoothing Groups, cavity AO
      Cylinder     - caps vs sides    -> smoothing-group seams
      GeoSphere    - smooth curvature -> height/down AO
      Torus        - continuous curve -> convex/cavity, curvature masks
      Noisy box    - dense detail     -> cavity/roughness AO, ray AO

    Run it, then use EZ_Apply.ms to drop EZ modifiers on the selection.
*/

(
    local built = #()
    local x = 0.0
    local gap = 70.0

    fn place o = ( o.pos = [o.pos.x, o.pos.y, o.pos.z]; o )

    -- 1. ChamferBox (extended primitive)
    local cb = ChamferBox length:44 width:44 height:44 fillet:4 filletSegs:3 \
        lengthsegs:1 widthsegs:1 heightsegs:1 name:"EZ_Test_ChamferBox"
    cb.pos = [x, 0, cb.height/2 + cb.fillet]
    append built cb
    x += gap

    -- 2. Cylinder with caps
    local cy = Cylinder radius:20 height:52 sides:28 capsegs:1 heightsegs:1 \
        name:"EZ_Test_Cylinder"
    cy.pos = [x, 0, 0]
    append built cy
    x += gap

    -- 3. GeoSphere (even triangulation, good for curvature AO)
    local gs = GeoSphere radius:24 segs:5 name:"EZ_Test_GeoSphere"
    gs.pos = [x, 0, gs.radius]
    append built gs
    x += gap

    -- 4. Torus
    local to = Torus radius1:22 radius2:8 segs:30 sides:16 name:"EZ_Test_Torus"
    to.pos = [x, 0, 8]
    append built to
    x += gap

    -- 5. Dense subdivided box + Noise -> bumpy detail for cavity/roughness
    local nb = Box length:44 width:44 height:44 lengthsegs:10 widthsegs:10 heightsegs:10 \
        name:"EZ_Test_NoisyBox"
    nb.pos = [x, 0, 22]
    local nz = NoiseModifier()
    nz.strength = [7, 7, 7]
    nz.scale = 26
    nz.seed = 12345
    try ( nz.fractal = true; nz.roughness = 0.4; nz.iterations = 4 ) catch()
    addModifier nb nz
    append built nb
    x += gap

    select built
    try ( max tool zoomextents all ) catch()
    format "Built % test objects: %\n" built.count (for o in built collect o.name)
    format "Tip: select one (or all) and run EZ_Apply.ms to add EZ modifiers.\n"
)
