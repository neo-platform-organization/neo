# Homogeneous perspective views

[General affine views](linearview.md) · [Geometry](geometry.md)

`neo.spatial.perspective`0.1.0 exports `view`, pinning geometry0.1.0. Its literal
behavior, source connection and derived outputs remain in the single loaded image
graph/checkpoint. Grant READ1 source authority. No runtime primitive, external
source-module execution, implicit camera convention or seed demo is added.

## Map and divide

For source dimension n and view-dimension m, provide row-major map with (m+1)*n
entries and offset with m+1 entries. The last row is the denominator w. Each source
vertex is transformed to homogeneous coordinates:

```text
h = round(map * x) + offset
w = h[m]
result[j] = round(h[j] * 1000000 / w), j < m
```

All stored scalars use signed64 scale1,000,000. Dimensions/indices are unscaled.
Every homogeneous dot rounds once nearest with ties away from zero before offset;
perspective division rounds the full resulting coordinate by the same rule.
Division is performed only for accepted positive denominators.

`initialize` reads source shape, allocates zero map/offset and sets the last offset
to1,000,000, returning true without publishing. It preserves caller clipping settings.
There is no assumed 3D camera, handedness, focal axis or implicit unit conversion.
For a 3D->2D example, choose rows x,y,z so h=[x,y,z]. Then w is the caller's z and
the canonical volume is |x|<=z, |y|<=z. Other explicit maps can assign w differently.
Zero-dimensional sources and zero-dimensional views are valid within budgets.

## Vertex clipping rules

`near` defaults to1 raw unit and must be positive. `far` defaults to0 (no upper
bound); a nonzero far must be at least near. Both bounds are inclusive, and refer
to the homogeneous denominator, not automatically to Euclidean camera distance.
`clip-volume` defaults true and adds inclusive -w<=h[j]<=w for every numerator.
Set it false to retain only denominator bounds. No clipped vertex is divided.

`clip-codes` contains one code per source vertex, with this priority:

| Code | Meaning |
| --- | --- |
| 0 | Accepted. |
| 1 | w<near, including zero and negative w. |
| 2 | Nonzero far bound exceeded. |
| 3 | Denominator accepted, but outside the enabled canonical numerator volume. |

`project` reads current source packed shape and publishes visible result coordinates,
result-dimension, result-vertex-count, result-source-count, source-indices (one
source position per visible row), and visibility (one0/1 per source vertex).
It also publishes homogeneous coordinates for every source vertex and denominators
for every source vertex, including rejected ones. Source order/identity is retained
through the explicit relationship and position mapping. Coincident projected
vertices stay distinct; there is no automatic unique picking rule.

Outputs are snapshots and require explicit refresh after source/map/settings edits.
This view does not mutate source, validate/copy topology, interpolate attributes,
generate clipped edge/face topology or provide inverse editing. An edge or face
can cross the visible volume even when all its original vertices are rejected:
vertex masks are not a whole-primitive culling rule. Renderer-level homogeneous
primitive clipping belongs to the later renderer milestone. Existing coordinate-
plane sections generate their own geometry and are a separate operation.

Milestone11 now supplies [homogeneous segment clipping](segment-clipper.md) as a
separate image package. It reads the full homogeneous payload and source segment
topology, preserving crossing edges that compact vertex output alone cannot
represent. Triangle clipping remains subsequent work; project itself is unchanged.

## Numeric and publication limits

Raw products/sums, offset additions, numerator scaling, division and rounding
intermediates are checked signed64 arithmetic. Overflow fails explicitly even
when a final exact answer would fit. Homogeneous quantization happens before
visibility classification; boundaries therefore apply to the stored rounded h,
not an unbounded exact pre-quantization transform. There is no saturation, epsilon,
wide-integer fallback or division through a zero/negative w. This package does not
claim a total error bound near a small positive denominator.

Validation/calculation failures before publication retain previous outputs;
scratch fields may change. All homogeneous transforms are computed, including
those subsequently clipped, and can overflow. Array/metadata publication is
sequential and can be partial if interrupted; this is not a transaction. Use
instances sequentially. Work is O(vertices*(m+1)*n + visible*m), with packed output
and homogeneous scratch arrays. No dimensional ceiling is hard-coded. Actual
allocation/invocation limits apply; spatial C regressions use bounded250,000-step
calls for high-dimensional cases without changing the CLI's100,000-step default.

## Example in one image

Import/install geometry and perspective artifacts through package-manager in the
OS graph. Instantiate geometry as g and the perspective view export as view:

```text
link /workspace/view source /workspace/g 1
set /workspace/g/dimension 3
set /workspace/g/vertex-count 2
call /workspace/g initialize
set /workspace/g/vertices [1000000 0 2000000 0 0 0]
set /workspace/view/view-dimension 2
set /workspace/view/map [1000000 0 0 0 1000000 0 0 0 1000000]
set /workspace/view/offset [0 0 0]
set /workspace/view/near 1000000
set /workspace/view/far 4000000
call /workspace/view project
show /workspace/view/clip-codes
show /workspace/view/source-indices
```

Visible result is `[500000 0]`, codes `[0 1]`, mapping `[0]`. The second vertex
has w=0 and is rejected without division. Save the complete image; reopening
retains installed artifacts, source grant, settings and derived results.

Strict Windows universal selftest covers source dimensions0,1,2,4,7,33,100,257,
positive perspective divide, zero/negative/near/far denominators, inclusive volume
boundaries, mappings and retained output on invalid settings. Optional
`python neo/packages/tests/perspective_projection.py` checks312 independent outputs
across52 shapes against an exact-rational reference, signed half ties, enabled/
disabled volume clipping, invalid shape/kind/bounds, checked overflow and full OS
single-image install/save/reopen. This workflow installs geometry and perspective;
it is not a capacity claim for every spatial/network package installed together.
Python is optional test tooling. Milestone9 acceptance/integration audit is next.
