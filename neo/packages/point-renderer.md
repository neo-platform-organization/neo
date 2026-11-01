# Projected vertex-cloud renderer

`neo.render.points` 0.1.0 exports `renderer`. This is milestone11's first bounded
reference-renderer subset: point plotting from compact projected vertex rows.
Import/install the inert artifact into the single live image graph and instantiate
through the package manager. Rendering loads no files and adds no C drawing
primitive or boot-image demo.

## Connections

| Connection | Rights | Contract |
| --- | --- | --- |
| `view` | READ (1) | result-dimension=2, nonnegative result-vertex-count, packed result containing exactly two integers per row |
| `buffer` | READ+WRITE (3) | Host-granted RGBA buffer; dimensions define the viewport |
| `window` | WRITE (2), for frame only | Host-granted presentation resource |

Axis, general affine and perspective views publish this compact result shape.
Refresh the selected view explicitly before rendering. Perspective/slice results
already compact selected vertices; do not apply their source-sized visibility
mask again. This renderer plots every result row independently of source geometry's
explicit points topology. Segment/triangle and section topology rendering is later.

Any source dimension can feed a 2D projection. Rendering does not own or change
source geometry, select axes, infer frame/camera transforms or implement inverse
edits. Multiple views can share one geometry; changing the renderer's view
connection selects a view while preserving source identity.

## Drawing and failure policy

Set integer origin-x/origin-y, positive pixels-per-unit, and red/green/blue/alpha
bytes (defaults: zero origin, scale1, opaque white). Coordinates use the spatial
scale1,000,000. Mapping is:

```text
x = origin-x + truncate(view-x * pixels-per-unit / 1000000)
y = origin-y - truncate(view-y * pixels-per-unit / 1000000)
```

Division truncates toward zero. Positive view y points upward. Intermediate signed
64-bit arithmetic is checked; overflow is explicit even if a mathematical final
coordinate would fit. There is no antialiasing, blending or colour conversion.
Display scale is not implicit: image layout chooses its pixel mapping.

`render` validates view shape, scale and colour, then stages all screen addresses.
Out-of-viewport rows get -1 and are skipped. After staging, it clears the complete
buffer to zero (transparent black), writes configured RGBA pixels, and publishes
screen-indices/drawn-count. Duplicate rows can hit the same pixel; drawn-count
counts accepted rows, not distinct pixels. Empty views clear to zero.

`frame` calls render, then window-present. Render alone needs no window. The
caller owns scheduling and availability/retry policy. Presentation failure does
not undo drawing. Instances use sequential scratch fields, including derived
copies of the latest view data; those are not authoritative geometry stores.

Ordinary validation and address overflow happen before clear. Denied buffer WRITE
preserves old pixels. Budget/allocation failure during drawing/publication can
leave a partial frame or old reports. There is no transaction or back buffer yet.

## Validation and current scope

Universal tests validate/install/resolve/copy the package, then import renderer,
geometry and projection into one graph. Exact pixels are checked for dimensions
2,3,4,33, offscreen rejection, edits, hidden-coordinate/source-identity preservation,
a second view, empty rows, invalid colour, transform overflow, denied writes and
graph save/reopen after removing external grants.

The reference workload is four projected vertices into5×5 RGBA under a100,000-step
frame budget. Tests print the measured maximum frame steps. Correctness requires
exact pixel equality and unchanged source geometry. This is not a large-scene
responsiveness claim. Allocation and behavior budgets apply; no source-dimension
limit or dimension-specific renderer case is added.

The strict Windows checkpoint measured979 steps/frame at maximum across the four
source dimensions. Projection is refreshed before the measured renderer call;
this count excludes projection cost and is not a wall-clock timing guarantee.

Lines/primitive clipping, filled triangles, depth, glyphs, dirty updates, picking
and a GUI remain unfinished. Existing demos still own their original rasterizers;
this checkpoint does not claim to migrate them all.
