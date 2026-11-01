# Filled triangles and screen-space depth

`neo.render.triangles` 0.1.0 exports `renderer`. Install/instantiate this inert
package in the single live image. Coverage, depth and RGBA writes are literal
image behavior over existing graph/array/buffer primitives. No C rasterizer,
source loader or OS-seed demonstration is added.

## Inputs and authority

Grant READ `view`, READ+WRITE `buffer`, and WRITE `window` for frame only. The
view contract matches the full-row [line renderer](line-renderer.md):
result-dimension2, result-vertex-count=result-source-count=source.vertex-count,
packed result coordinates and a valid source-indices permutation. The view's READ
source connection supplies triangle index triples. Indices remain source indices;
the renderer resolves projected rows through an inverse map.

Provide `depths` with one signed integer per source vertex. Depths are explicit
screen-space values: smaller is nearer. The caller chooses consistent units and
derives suitable post-projection depth. World-space z or homogeneous w is not
automatically a correct screen-space interpolant. This package neither reads a
hidden source axis nor provides perspective-correct world attributes.

Optional `face-colours` contains four RGBA bytes per original triangle. Empty uses
uniform red/green/blue/alpha fields, all default255. All colour fields must be
bytes. Alpha is stored, without blending or alpha-dependent depth rejection; even
an alpha-zero face can occlude. Source geometry, topology and depths are not
modified by rendering. Refresh views explicitly after source edits.

Compacted/missing vertices are rejected. This package does not infer whole-face
visibility from a vertex mask. Homogeneous triangle clipping is still required to
handle general faces crossing perspective boundaries; the segment clipper only
generates segments and is not a triangle-clipping substitute.

## Coverage

Map view coordinates using the existing origin-x/origin-y/pixels-per-unit
convention: scale1,000,000, truncation toward zero, positive view y upward. The
mapped vertices are integer drawable coordinates. Pixel samples are at
(x+1/2,y+1/2). Coverage uses exact integer edge functions on doubled coordinates.

Normalize winding to positive oriented area by exchanging the second and third
vertices and their depths. Degenerate projected triangles emit no fragments.
For a directed edge with positive-area winding in the downward-y screen grid,
include a zero edge-function sample only when dy<0 or (dy=0 and dx>0). This is the
top-left rule: shared edges belong to one adjacent triangle, independent of input
winding. There is no antialiasing, conservative coverage or back-face culling.

Only the triangle's viewport-clamped bounding box is sampled, so work does not
walk an offscreen world-sized rectangle. This clips raster samples, not geometric
topology. Checked coordinate/area calculations can still overflow even for a
triangle whose samples would be outside the viewport.

## Depth, output and failure policy

Interpolate source-indexed depth with screen barycentric weights, then round once
to the nearest integer with ties away from zero. Compare these quantized depths;
equal values retain the earlier source-triangle ordinal. Distinct exact depths
that round equally are ties under this contract. Unequal quantized depths yield
the same visible colour when face order changes with its associated colours.

`prepare` takes positive width/height fields, validates input and builds reports
without a native buffer/window:

| Field | Meaning |
| --- | --- |
| owners | One original triangle ordinal per pixel; -1 means uncovered |
| depth-buffer | Quantized winning depth per pixel; zero when uncovered |
| drawn-count | Distinct covered pixels |
| fragment-count | Covered triangle samples before the depth test |

Always consult owners to distinguish an uncovered pixel from a real depth0.
Negative depths are valid. Empty or degenerate topology produces owners=-1 and
zero depths/counts. Depth state is rebuilt on every prepare, not retained across
frames or multiple renderer instances.

`render` gets dimensions from buffer, calls prepare, clears RGBA bytes to zero
and writes each winner's flat colour. `frame` renders then presents. Layout,
schedule, availability/retry and composition remain caller policy. Instances use
sequential scratch fields and are not reentrant.

Shape/index/colour/depth validation and all coverage/depth calculation precede
buffer clear. Checked signed64 overflow is explicit, including weighted depth
products that might mathematically cancel. No floating/wide-integer fallback is
used. Ordinary failures and denied buffer WRITE preserve prior pixels. Scratch
can change; output publication/drawing can be partial on budget/allocation
failure. Presentation failure does not undo rendering. No transaction/back buffer
or dirty update is implied.

Storage is O(vertices + triangles + viewport pixels). Worst-case sampling is
O(triangles × viewport pixels), plus shape/mapping validation. Actual allocation
and execution budgets bound work; this reference implementation is not optimized
for large displays or dense meshes.

## Verification and current scope

Universal tests check12 exact overlap frames from source dimensions2,3,4,33,
including reversed draw order, flat face colours, a crossing depth gradient and
equal-depth ties. A two-triangle quad checks16 unique samples and exactly16
fragments across its shared diagonal. Source coordinates stay unchanged, and
invalid depth length, overflow and denied WRITE preserve prior RGBA.

The maximum measured overlap frame was14,538 steps for two triangles into5×5
RGBA, under100,000. Projection is excluded; this is not a wall-clock or large-scene
responsiveness claim.

Optional tooling from repository root:

```powershell
python neo/packages/tests/triangle_rasterization.py
```

It checks84 scenes against exact-rational pixel-centre coverage/interpolation:
random slopes/depths, winding, negative and tied depths, shared edges, degenerate/
empty/offscreen triangles and nontrivial source-row remapping. It installs geometry,
projection and renderer into one OS image and verifies save/reopen without
artifact reload. Python remains test tooling only.

Milestone11 remains partial: homogeneous triangle clipping, glyphs, dirty updates,
renderer integration/quality audit and GUI work are still open. Milestone10's
remaining native/platform validation gaps remain separately tracked.
