# EZ BoxTri

A 3ds Max 2023 C++ modifier (`.dlm`) for **4-layer box-triplanar UV authoring +
blend-weight export**, aimed at engines/shaders that have no native triplanar
support. The modifier projects up to four texture layers onto a mesh and writes
per-vertex blend weights so the target shader can mix the four textures by
surface orientation.

It is a compiled port of the `EZ_BoxTri_v14` MAXScript prototype, with the live
Data-Channel-Modifier stack replaced by a single native modifier pass.

---

## What it produces

Apply the modifier and it writes, in one pass:

| Channel | Default | Contents |
|---|---|---|
| Layer UV channels | ch 1-4 | Planar (or cyl/spherical/world) UVs for each of the 4 layers |
| Blend channel | ch 10 | `U = Side X weight`, `V = Side Y weight`, `W = Top weight`. **Bottom is implicit:** `bottom = 1 − U − V − W` |
| Preview channel | ch 0 (vertex color) | RGB visualisation of the blend, remappable via Preview Mode |

All channels are written **face-corner-unwelded** (3 unique map-verts per face).
This is required for the signed-axis no-mirror trick and keeps the blend constant
per face (no intra-face gradient).

### The 4 layers

| Layer | Blends on | Default mapping |
|---|---|---|
| **Side X** | `\|N.x\|` (X-facing) | Planar X (UV from Y/Z) |
| **Side Y** | `\|N.y\|` (Y-facing) | Planar Y (UV from X/Z) |
| **Top**    | `+N.z` (up-facing)   | Planar Z (UV from X/Y) |
| **Bottom** | `−N.z` (down-facing) | Planar Z (UV from X/Y) |

The blend role is fixed to the axis; the **mapping type** of each layer is free.

---

## Blend math

Per face (using the geometric face normal `N`):

```
if invertZ: N.z = -N.z
up    = remap(max(+N.z, 0), TopStart .. 1)   clamped 0..1
down  = remap(max(-N.z, 0), BotStart .. 1)   clamped 0..1
wx = pow(|N.x|, sharp) * SideXBias
wy = pow(|N.y|, sharp) * SideYBias
top = pow(up,   sharp) * TopBias
bot = pow(down, sharp) * BotBias
normalise so wx + wy + top + bot = 1
if HardSnap > 0 and max >= HardSnap (or Hard Snap checkbox): dominant -> 1, rest -> 0
```

Disabled layers contribute zero bias; the remaining layers renormalise.

**Knobs**

- **Sharp** — transition sharpness (`pow` exponent). Higher = tighter blend borders.
- **Side/Top/Bottom Bias** — per-layer weight multiplier before normalising.
- **Top Start / Bottom Start** — vertical dead-zone before floor/ceiling kick in.
- **Invert Z** — swaps Top/Bottom.
- **Hard Snap** — pick a single dominant layer per face (no blending).

---

## Mapping types (per layer)

`Planar X/Y/Z` (bounds-normalised) · `Cyl Z/X/Y` (angle U, axis-along V) ·
`Spherical` (around bounds centre) · `World X/Y/Z` (raw coords, tile = world units).

**Signed-Axis Fix** (Output group): for planar modes, if a face's normal is
negative on the layer's axis, U is flipped so opposite-facing sides don't mirror
across the seam.

---

## Install (prebuilt)

A compiled build is checked in at [`bin/EZ_BoxTri.dlm`](bin/EZ_BoxTri.dlm).
Copy it into your 3ds Max 2023 plugins folder (or a custom plugin path) and
restart Max. It registers three modifiers under the **EZ Tools** category:
**EZ BoxTri**, **EZ BoxTri AO**, and **EZ Procedural Masks**.

## Build

Requires the 3ds Max 2023 SDK and VS 2019 (v142 toolset).

```bat
cd EZ_BoxTri
mkdir build && cd build
cmake -G "Visual Studio 16 2019" -A x64 ^
  -DMAXSDK_DIR="C:/Program Files/Autodesk/3ds Max 2023 SDK/maxsdk" ..
cmake --build . --config Release
```

Output: `build/Release/EZ_BoxTri.dlm`. Copy it to:

```
C:\Program Files\Autodesk\3ds Max 2023\Plugins\
```

The modifier appears as **EZ BoxTri** under the **EZ Tools** category.

---

## Testing

`scripts/EZ_BoxTri_TestMats.ms` — a helper rollout. Drop it on a viewport, select
an object with EZ BoxTri applied, and:

- **LIVE Preview Mode** buttons drive the modifier's Preview Mode and show the
  blend live on ch 0 (vertex display). Mode 1 maps each layer to a pure channel:
  red = ±X sides, green = ±Y sides, blue = top, black = bottom.
- **Per-Layer UV Checker** drops a checker on a layer's UV channel to inspect the
  projection.
- **Report Channels** dumps which channels carry data.
- **Clear Material** resets the object to plain shaded.

---

## Export notes

The blend weights live in **map channel 10** as UVW data (not vertex color).
When exporting (FBX/OBJ/etc.), confirm your exporter carries map channel 10 and
that your target shader reads it as `(SideX, SideY, Top)` with Bottom as the
`1 − U − V − W` remainder. Collapse the stack (Convert to Editable Mesh) before
export to bake the channels.
