# Topology-aware viewport line renderer

`neo.render.lines` 0.1.0 exports `renderer`. Import/install this inert artifact
into the single live image and instantiate it through the existing package
manager. Geometry, view and renderer remain ordinary objects in that graph.
Rendering loads no source modules and adds no kernel drawing operation.

## Input contract

Grant READ `view`, READ+WRITE `buffer`, and WRITE `window` (frame only). The view
must expose result-dimension2, nonnegative result-vertex-count, result-source-count,
packed result coordinates, source-indices, and a READ-accessible `source`
connection to the original geometry. Topology comes from that source's `segments`
array and vertex-count. No separate renderer source link can accidentally point
at another geometry.

This first line subset requires every source vertex to appear exactly once in
the view: result count must equal source count and source-indices must be a valid
permutation. Topology indices remain source indices; an inverse map resolves them
to projected rows. Segment arrays must contain pairs of in-range indices.
Repeated/reversed segments and coincident endpoints are allowed. Rendering
does not validate unrelated triangle topology or mutate source data.

Refresh the view explicitly after geometry edits. Count/shape checks do not detect
a stale view whose source coordinates changed without changing its vertex count.

Compact perspective/slice outputs missing vertices fail explicitly. Dropping an
edge because an endpoint was rejected is not correct whole-primitive clipping:
it may cross a visible volume. Homogeneous edge clipping and generated intersection
vertices remain future work. Full-row axis and affine views meet this contract;
full-row positive-denominator perspective output can also be sampled, but no
frustum clipping is inferred from this renderer.

## Mapping and viewport sampling

Mapping/colour fields follow the [point renderer](point-renderer.md): fixed-point
scale1,000,000, integer positive pixels-per-unit, origin-x/origin-y and RGBA bytes.
Positive view y points upward; mapped coordinates are integer drawable pixels.

For each mapped segment, choose x as its major axis when abs(dx)>=abs(dy),
otherwise y. Canonicalize its endpoints so major coordinates increase. Examine
only integer major coordinates inside both the segment's major interval and the
viewport. The minor coordinate is:

```text
minor = minor0 + truncate((minor1-minor0)*(major-major0)/(major1-major0))
```

For a coincident endpoint, use minor0 without division. Keep a sample only if its
minor coordinate is also inside the viewport. Truncation is toward zero on the
interpolated offset; canonicalization makes reversed edges produce identical
pixels. Endpoints and viewport bounds are inclusive. This defines a deterministic
integer DDA coverage rule, not antialiased or conservative geometric coverage.

This is viewport clipping of raster samples. Each edge examines at most width or
height samples, even when its endpoints are billions of pixels away. No loop
walks the offscreen world-space length. It does not produce a new clipped topology
or exact rational viewport-boundary vertices. Checked arithmetic may overflow
before division; no wide-integer or floating fallback is hidden in the image.

## Handlers and failure boundary

`prepare` takes positive width/height fields and stages a width×height 0/1
pixel-mask without requiring a buffer/window. It validates geometry/view shape,
indices, colour and scale, calculates mapped coordinates, and samples segments.
On success it publishes pixel-mask, drawn-count (unique covered pixels) and
drawn-segments (segments producing at least one retained sample). Overlapping
segments count separately in drawn-segments but share mask pixels. Empty topology
publishes a zero mask. Scratch changes on failure; published reports retain their
previous values until publication, which is sequential and not transactional.

`render` obtains dimensions from buffer, calls prepare, clears all RGBA bytes to
zero and writes configured colour at marked pixels. `frame` renders and presents.
No alpha blending, depth test, scheduling or retry policy is added. Instances are
sequential and not reentrant. Geometry/projection remain source authority; screen
coordinates, inverse maps and masks are derived scratch/output.

Ordinary validation and checked sampling failures occur before buffer clear.
Denied buffer WRITE preserves old pixels. Budget/allocation failure while drawing
or publishing can leave a partial buffer/report; presentation failure does not
undo drawing. `prepare` costs O(vertices + indices + width×height +
segments×max(width,height)) in the worst case; mask storage is O(width×height).
Real allocation/behavior budgets apply. This reference path is not yet optimized
for large displays or dense meshes.

## Verification

The universal selftest checks32 exact RGBA reference frames across source
dimensions2,3,4,33: horizontal/vertical/diagonal/reversed/degenerate/offscreen edges,
billion-pixel endpoint distances, source-coordinate preservation, malformed
topology, compact-view rejection, and overflow before clear. The measured maximum
was2,554 renderer steps per5×5 single-segment frame, under100,000. Projection is
outside that measured frame budget; no wall-clock responsiveness claim is made.

From the repository root, optional test tooling runs:

```powershell
python neo/packages/tests/line_rasterization.py
```

It checks356 masks against exact-rational viewport-grid intersections, including
random slopes, reversal, large distances and a nontrivial source-row permutation.
It installs/instances geometry, projection and renderer in the OS image, saves one
image source, reopens it and samples again without artifact reload. Python is
authoring/test tooling only.

Milestone11 remains partial: homogeneous primitive clipping, filled triangles,
depth, glyphs, dirty updates and GUI integration remain open. The existing point
renderer and standalone demo rasterizers are unchanged.

The [homogeneous segment clipper](segment-clipper.md) now supplies a generated
full-row view satisfying this input contract. Link view to the clipper after
refreshing perspective and clipping. Crossing edges survive without weakening
the rejection of incomplete ordinary compact view mappings.
