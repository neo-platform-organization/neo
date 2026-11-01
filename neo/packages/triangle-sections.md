# Triangle sections of coordinate planes

[Segment sections](section.md) · [Geometry](geometry.md)

`neo.spatial.section`0.2.0 adds a separate `triangle-section` copy export alongside
the unchanged segment `section` export, with the same geometry0.1.0 dependency.
Grant a READ1 `source` geometry connection, set plane-axis/plane-value, and call
`intersect`. Behaviors, source, derived geometry and provenance live in one image
graph/checkpoint. No runtime primitive, external source-module evaluation or
boot-seed example is added. Source data is never changed.

## Classification and degeneracy

Input packed coordinates and triangle triples/indices are checked; other source
topology and its cached validation flag are ignored. An exact coordinate plane
requires an existing axis, so zero-dimensional input explicitly fails.

Each face's exact affine rank is checked with signed64 differences and minors:
rank0 means one coincident point, rank1 a collinear convex hull, rank2 a face.
Degenerate faces are reduced to their point or segment extent before intersection.
For a rank1 face, extrema are taken on the last axis with a nonzero difference;
this orders the endpoints deterministically without inventing an area or normal.
All original vertices remain source data even when a hull representation omits an
intermediate collinear vertex.

| Result | `triangle-kinds` | Behavior |
| --- | --- | --- |
| Miss | 0 | No contact geometry. |
| Point | 1 | Tangency, a crossing of a degenerate hull, or a coincident on-plane point. |
| Segment | 2 | Crossing face, on-plane edge, or coplanar degenerate hull. |
| Triangle | 3 | Coplanar nondegenerate face, with original vertex order preserved. |

Rank2 faces collect on-plane vertices in source order, then strict edge crossings
in AB,BC,CA order. They have at most two contacts unless fully coplanar. Rank0/1
inputs produce no face topology. Shared contacts across input faces are not welded;
per-face ownership is retained. There is no normal, contour stitching, unique
picking result, arbitrary plane, half-space clipping or inverse-edit policy here.

## Outputs and provenance

`result`, result-dimension and result-vertex-count describe packed full-nD output.
Result-points/result-segments/result-triangles contain explicit point indices,
consecutive pairs and triples into that output. Coplanar triangle edges are not
also emitted as independent segments. Per-face triangle-kinds and triangle-ranks
record output type and original rank, respectively.

Each output vertex has source-triangles, source-vertices (-1 for an interpolated
contact), source-edge-vertices (a pair per output vertex), parameter-numerators
and parameter-denominators. The source pair A,B with exact signed t describes
A+t(B−A) before rounding. Endpoints use pair(v,v), t=0/1. A reduced hull can use
any original pair that spans that face's actual extent, so source-vertices can be
-1 even when the same point occurs as an intermediate original vertex. No fake
endpoint identity is assigned.

Segment-sources and triangle-sources map output primitives to source triangle
positions. Segment-degenerate flags whether the two rounded output endpoints are
identical. Distinct exact contacts can quantize to one coordinate: topology is
retained and flagged rather than silently becoming a nondegenerate hit.
Triangle-degenerate is zero for retained rank2 coplanar faces, whose coordinates
are copied unchanged. Point contacts require no area classification.

All provenance indices refer to the source snapshot's packed positions, not
persistent per-vertex object identities. Outputs are derived snapshots; recompute
after changing source, source connection or plane. Frames/units remain those of
the explicitly referenced source, without implicit conversion.

## Precision, failures and work bounds

Interior coordinates use the segment package's final-coordinate rational formula,
rounded once nearest with ties away from zero; the plane coordinate stays exact.
Each coordinate has at most0.5 raw rounding error when all checked intermediates
fit. Exact t remains available independently of those rounded coordinates.
Rank differences/products/minors and interpolation/rounding are checked signed64
arithmetic. Overflow fails even if a final exact answer would fit, and may occur
while classifying a face that would miss the plane. There is no wide-integer,
saturation, tolerance or hidden pseudoinverse fallback.

Validation/calculation failures before publication retain previous outputs; scratch
state may change. Publishing arrays and metadata is sequential, not transactional,
so interruption during publication can leave a partial snapshot. Use instances
sequentially. Work is O(faces*n), with at most three contact descriptors per input
face plus derived packed arrays. Dimensions are generated from counts; no hard
ceiling or dimension-specific algorithm exists. Invocation and allocation budgets
still apply. Worst-case257D crossing construction exceeds the CLI's normal100,000
steps; the universal C regression uses a bounded250,000-step invocation for those
cases. This is not resumable slicing or an increased CLI default.

## Example in the single image

Install geometry and section artifacts into the OS graph using the package manager.
Instantiate geometry as `g`, and the `triangle-section` export as `faces`:

```text
link /workspace/faces source /workspace/g 1
set /workspace/g/dimension 3
set /workspace/g/vertex-count 3
call /workspace/g initialize
set /workspace/g/vertices [-2 0 0 2 0 0 0 4 0]
set /workspace/g/triangles [0 1 2]
set /workspace/faces/plane-axis 0
set /workspace/faces/plane-value 0
call /workspace/faces intersect
show /workspace/faces/result-segments
```

Result is `[0 4 0 0 0 0]`, segment indices `[0 1]`, rank `[2]`, kind `[2]`.
The first contact is original vertex2; the second interpolates source vertices0,1.
Save the complete image to retain the installed graph, source grants and results.

Strict Windows universal selftest covers generated crossings through dimensions
1,2,3,4,7,33,100,257, mixed coplanar rank2/rank1/rank0 outputs, provenance topology
and rejected indices. Optional `python neo/packages/tests/triangle_sections.py`
checks52 cases /780 output arrays against independent exact-rational/Gram-rank
references, including reversed/degenerate geometry, rounded collapse, invalid
shape/axis/index/kind, rank overflow and retained outputs. Full OS install,
instances and single-image save/reopen pass. The previous361 segment-reference
outputs also pass. Python is optional test tooling only.

Milestone9 remains partial. General projections and clipping contracts still need
implementation and an acceptance audit before presentation/GUI milestones begin.
