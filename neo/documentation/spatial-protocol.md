# Numeric and spatial reference protocol

[Language documentation](README.md) · [VM status](../../documentation/reference/runtime-foundation.md)

Milestone8 specified the reference contract; milestone9 now passes its
[acceptance audit](../../documentation/reference/spatial-audit.md) for the documented
Euclidean/fixed-point subset. The common spatial
foundation now includes [a vector package](../packages/vector.neo): computed-length
construction, checked addition/subtraction/scaling, once-rounded fixed-point dot
products, integer-root magnitude and precision-preserving normalization. See the
[vector guide](../packages/vector.md) for checked intermediate range and explicit
approximation bounds. [Linear matrices](../packages/matrix.md) now provide compact
identities, rectangular application/composition and residual-checked inversion.
[Affine transforms](../packages/affine.md) add translation, point/displacement
application, composition and inversion. [Spaces and frames](../packages/frames.md)
now supply explicit identity, parent paths and checked coordinate conversion.
[Geometry](../packages/geometry.md) now supplies packed vertices, explicit
point/segment/triangle topology and degeneracy validation. [Axis projections](../packages/projection.md)
provide derived views and edits preserving hidden coordinates. [Coordinate-band
slices](../packages/slices.md) add stored-vertex selection and source-row mappings.
[Segment sections](../packages/section.md) now generate exact-plane contacts
and coplanar segment topology with rational provenance. [Triangle sections](../packages/triangle-sections.md)
now handle face crossings, coplanarity and degenerate hulls. [General affine
views](../packages/linearview.md) add explicit lift-constrained editing.
[Perspective views](../packages/perspective.md) now provide homogeneous projection
and vertex clipping; renderer-level primitive clipping remains pending; cross-space units/custom metrics are not implemented. No geometry is added to the
OS seed by this specification.

The implementation target uses signed-64-bit fixed-point scalars with scale
1,000,000, carried by ordinary integer payloads/arrays. Counts and indices remain
unscaled. Arithmetic is checked; products/dot components quantize once using
nearest rounding with ties away from zero. Intermediate overflow and division by
zero fail explicitly. Floating-point syntax, implicit casts and silent saturation
are not introduced. Approximate library comparisons have explicit tolerances;
ordinary language equality stays exact.

Vectors/matrices have checked explicit shapes with no fixed maximum dimension.
Explicit factories generate the structures/operations needed for an object's n;
compact/lazy transforms avoid mandatory dense n*n storage. Concrete operations
remain subject to memory, index and execution budgets. Zero-dimensional
points and empty identities are valid; non-spatial objects have no spatial
attachment and are not implicitly located at an origin.

Points and displacements refer to explicit frames. Spaces declare ordered axes,
units and the reference Euclidean metric. Frame parenting uses explicit graph
connections independently of containment. Equal names do not establish equal
space identity. Unit/frame conversion is explicit, and spatial relationships
never grant authority.

Matrices use row-major storage with column-vector semantics:

```text
point in parent = A * point in local + translation
displacement in parent = A * displacement in local
compose (B,u) after (A,t) = (B*A, B*t + u)
```

Forward singular maps may flatten geometry; inverse/conversion operations must
reject singular or numerically invalid inverses. Angles use turns and rotations
choose an explicit pair of axes, so the same protocol extends beyond 2D/3D.

Geometry owns packed vertices and explicit indexed point/segment/triangle topology.
Invalid shape/indices fail validation. Degenerate faces do not invent normals or
hits. Higher-dimensional cells and custom metrics are later protocols.

Views retain source identity and produce derived projections/slices. Editing an
axis projection preserves hidden coordinates; a general or perspective view
requires explicit constraints before converting an edit back to source space.
Multiple overlapping candidates remain inspectable. Views and their controls are
graph objects, not another authoritative scene model.

Expected examples include a 4D source [1,2,3,4] shown on axes 0,2 as [1,3]; editing
the view to [9,8] produces [9,2,8,4]. A 7D vector [1,2,3,4,5,6,7] dotted with itself
is 140. A singular 2D flattening map can apply forward but cannot be inverted.
All numbers here are decoded values; stored spatial values are scaled explicitly.

Milestone 9 implements these contracts as non-graphical packages with exact and
approximate acceptance checks before GUI rendering work. Fixed-point scalar
choice and numerical thresholds are provisional library decisions,
not additional settled language semantics.
