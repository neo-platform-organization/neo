# General affine views and explicit edit constraints

[Axis views](projection.md) · [Geometry](geometry.md) · [Affine maths](affine.md)

`neo.spatial.linearview`0.1.0 exports a `view` copy prototype and pins geometry0.1.0.
Its literal behaviors, source connection, projection settings and derived outputs
live in the single loaded image graph and checkpoint. No C primitive, runtime
source-module evaluation or seed example is added.

## Forward projection

Grant READ1 source authority. For source dimension n and view-dimension m, provide
row-major `map` P with m*n entries and `translation` t with m entries. `project`
reads current source packed vertices and computes y=P*x+t for every vertex. It
publishes packed result, result-dimension, result-vertex-count and source-indices
(the unchanged source order). Source topology is neither copied nor implicitly
validated, and collapse/overlap creates no automatic normal or unique picking hit.
Projection coordinates stay in the source's declared numeric frame convention;
there is no implicit frame/unit conversion or perspective divide.

Forward maps may be rectangular, singular or dimension-increasing. Translation
works for zero-dimensional sources. An empty view has empty coordinates while
retaining vertex count/source mapping. No fixed dimension ceiling is introduced;
dense storage and invocation/allocation budgets remain binding.

`initialize` reads source shape, allocates zero map/translation/lift from counts,
and sets edit-enabled=false. It returns true without publishing a projection.
Set map/translation explicitly afterward. Project ignores lift and edit policy.
Results are derived snapshots: explicitly refresh after source or view edits.

## Explicit editing policy

Lower-dimensional projection has no unique inverse. Editing is disabled until the
caller sets `edit-enabled=true` and supplies a row-major `lift` L with n*m entries.
`validate-lift` checks nonnegative edit-tolerance, m<=n and each once-rounded entry
of P*L against identity within edit-tolerance (default10 raw). This is an explicit
approximate right-inverse constraint, not an automatically chosen pseudoinverse,
orthogonality proof, condition estimate or authority grant. Loose caller tolerances
weaken the check and do not prove mathematical rank/invertibility. Every edit
revalidates the constraint; a previous validation result is not trusted as a cache.

Set vertex-index to an original source position and edit to m target coordinates.
`edit-vertex` rereads the latest source and computes:

```text
y = round(P*x) + t
delta = edit - y
candidate = x + round(L*delta)
```

It then projects the candidate and requires every coordinate to match edit within
edit-tolerance before any source write. A valid P*L can still fail this final
quantized representability check. Different lifts deliberately choose different
corrections: for P=[1,1], L=[1,0] keeps the second coordinate; L=[0,1] keeps the
first. No implicit edit policy is chosen from the apparent view.

With exact arithmetic and P*L=I, this preserves the complementary component
(I−L*P)*x. This is the complement defined by the caller's lift, not necessarily
an orthogonal projection onto the mathematical kernel. Arbitrary coordinate
components may change. Fixed-point rounding and approximate constraints do not
guarantee exact preservation of that expression or a global error bound; the
explicit final reprojection check bounds the accepted visible-coordinate error.
Coordinate preservation is exact for the axis/lift choices used in the examples.

Zero-dimensional/zero-view constraints are vacuous; an empty edit preserves source
coordinates and still invalidates its geometry validation flag. Index bounds still
require an actual vertex. Overcomplete maps can project but are rejected by edit
validation. Rank-deficient maps normally fail the right-inverse check; there is
no hidden inverse fallback.

## Numeric and mutation contract

Values use signed64 integers at scale1,000,000; dimensions and indices are unscaled.
Every dot accumulates checked raw products/sums and rounds once nearest with ties
away from zero before translation/correction addition. All intermediate operations,
including residual subtraction/absolute value, must fit. Overflow fails explicitly,
even if a final exact value would fit. Source/view data are mutable graph objects;
instances require sequential use and do not implement optimistic revision checks.

Before publication, validation/calculation failures preserve source coordinates
and previous published outputs. Scratch fields can change. A successful edit writes
source valid=false, then source vertices, then edit-result (the full candidate).
Interruption/allocation failure during publication can leave the invalidated flag
or an applied edit without its published response; this is not transactional.
Projected result remains unchanged until refreshed. Revalidate geometry after edits.
READ|WRITE3 source authority is required for edits, independently of edit-enabled.

Forward work is O(vertices*m*n). Lift validation costs O(m*m*n), with dense map/lift
storage. High-dimensional cases can exceed the CLI's default100,000 steps; universal
regressions use bounded250,000-step calls for spatial cases through257 dimensions.
There is no increased CLI limit or resumable continuation in this checkpoint.

## Example in one image

Import/install geometry and linearview artifacts in the OS graph through the
package manager. Instantiate geometry as g and the linearview view export as view:

```text
link /workspace/view source /workspace/g 3
set /workspace/g/dimension 3
set /workspace/g/vertex-count 1
call /workspace/g initialize
set /workspace/g/vertices [1000000 2000000 5000000]
set /workspace/view/view-dimension 2
set /workspace/view/map [1000000 1000000 0 0 1000000 1000000]
set /workspace/view/translation [3000000 4000000]
set /workspace/view/lift [1000000 -1000000 0 1000000 0 0]
set /workspace/view/edit-enabled true
set /workspace/view/vertex-index 0
set /workspace/view/edit [9000000 8000000]
call /workspace/view edit-vertex
call /workspace/view project
```

Original projection is [6000000,11000000]. The edited source is
`[7000000 -1000000 5000000]`, preserving the chosen third coordinate; refreshed
projection is `[9000000 8000000]`. Save the complete image and reopen it: source,
view, grants, installed artifacts and edit policy remain in the same graph.

Strict Windows universal selftest covers projection/edit constraints through
0,1,2,4,7,33,100,257 dimensions, disabled edits, non-axis maps and invalid lifts
retaining source. Optional `python neo/packages/tests/linearview_edit.py` checks134
independent projection/edit/source outputs,40 forward maps,30 constrained edits,
two lift choices, final precision rejection, invalid shape/rank/index/authority,
checked overflow and full OS single-image install/save/reopen. Python is optional
test tooling. [Perspective views](perspective.md) now provide a separate forward-only operation.
Renderer primitive clipping and milestone9 acceptance audit remain.
