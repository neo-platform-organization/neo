# Segment sections of coordinate planes

[Geometry](geometry.md) · [Coordinate-band views](slices.md)

`neo.spatial.section`0.2.0 exports `section` and [triangle-section](triangle-sections.md), pinning geometry0.1.0. Its literal
behaviors and derived geometry live in the single image graph. Grant a READ1
`source` connection to geometry; set `plane-axis` and raw fixed-point `plane-value`,
then call `intersect`. Source coordinates/topology are never changed. No C runtime
primitive, source-module evaluation, implicit frame conversion or seed demo is added.

## Scope and cases

This computes actual intersections of the source segment topology with one exact
coordinate plane. It validates source packed shape, segment pairs and indices.
Other topology is ignored; the source's cached valid flag is not trusted. Plane
axis must exist, so dimension zero explicitly fails. There is no dimension ceiling.

| Segment relation | Output |
| --- | --- |
| Endpoints strictly on opposite sides | One interpolated point. |
| Exactly one endpoint on plane | That endpoint as one point. |
| Both endpoints on plane, distinct coordinates | One retained coplanar segment with two vertices. |
| Both endpoints on plane, identical coordinates | One point, using the first endpoint. |
| Both strictly on the same side | No geometry. |

No epsilon or band is implicit. Coordinate-band stored-vertex selection remains
in projection0.2. Shared contacts are not welded: each source segment owns its
outputs in source order, preserving separate provenance. Duplicate points are
valid. Triangle/face intersections now have a separate export. Arbitrary planes, multi-plane clipping,
connected contours, welding and hit/picking policy are future work.

## Derived geometry and provenance

| Field | Meaning |
| --- | --- |
| `result` | Full n-dimensional generated vertices, packed vertex-major. |
| `result-dimension`, `result-vertex-count` | Explicit output shape. |
| `result-points` | Indices of isolated point contacts in result vertices. |
| `result-segments` | Consecutive output index pairs for retained coplanar segments. |
| `source-segments` | One source segment index per output vertex. |
| `source-vertices` | Original endpoint index, or -1 for an interior intersection. |
| `parameter-numerators`, `parameter-denominators` | Exact rational t per vertex in A+t(B−A). |
| `segment-sources` | Source segment index per output segment. |
| `segment-kinds` | One0/1/2 per source segment: miss, point contact, coplanar segment. |

Endpoint parameters are0/1 or1/1. Interior ratios retain the signed numerator
plane−Aaxis and denominator Baxis−Aaxis, without gcd reduction; both can be negative.
The ratio lies strictly between0 and1. It describes the exact pre-quantization
intersection. Packed indices are positions in the source snapshot, not persistent
per-vertex identities. Outputs are derived snapshots: recompute after source or
plane edits. Source connection identity remains explicit; changing it does not
silently reinterpret old provenance as fresh. No inverse editing of generated
intersections is provided.

Coplanar segment endpoints belong to result-segments and are not additionally
listed as isolated points. Misses yield empty arrays and retain dimension metadata.
Output source frame is implicit through the explicit source geometry relationship;
no transformed/second authoritative scene is created.

## Numerical and failure contract

Interior coordinates use the exact rational expression
`(Acoord*(Baxis−plane) + Bcoord*(plane−Aaxis)) / (Baxis−Aaxis)`.
The complete coordinate rounds once, nearest with ties away from zero, with at
most0.5 raw-unit rounding error when all checked intermediates fit. The plane
coordinate is written exactly. For example, an exact coordinate of−0.5 rounds
to−1, rather than rounding only the displacement from A. Coplanar/endpoint cases
copy source coordinates directly and avoid unnecessary interpolation arithmetic.

All differences, products, sums and rounding intermediates are signed64 checked
operations. Overflow fails explicitly even if the final exact coordinate would
fit. There is no wide-integer fallback, saturation or silent loss of provenance.
Calculation/validation failures retain previous published outputs; scratch state
may change. Publishing arrays and metadata is sequential, so interruption during
publication can leave a partial snapshot. Instances require sequential use.
Two passes count/classify then construct outputs, O(segments*n), with explicit
output allocations. Actual graph/array/invocation budgets remain binding.

## Example in one loaded image

Install geometry and section artifacts into the OS graph with pkg-load/install/
clear. Instantiate geometry as `g` and section as `section` through package-manager,
then use:

```text
link /workspace/section source /workspace/g 1
set /workspace/g/dimension 2
set /workspace/g/vertex-count 2
call /workspace/g initialize
set /workspace/g/vertices [-1000000 -1000000 1000000 0]
set /workspace/g/segments [0 1]
set /workspace/section/plane-axis 0
set /workspace/section/plane-value 0
call /workspace/section intersect
show /workspace/section/result-points
show /workspace/section/source-segments
save build/x86_64/Windows/section-image.neo
```

The generated point is `[0 -500000]`, point topology `[0]`, source-segments `[0]`,
source-vertices `[-1]`, exact parameter1000000/2000000. Reopening the complete
saved image retains installed artifacts, source, section, grants and output.

Strict Windows universal selftest checks intersections through dimensions1,2,3,4,
7,33,100,257, endpoint/coplanar/miss/degenerate cases, topology and rational provenance,
and rejected axes retaining output. Optional
`python neo/packages/tests/section_intersections.py` checks361 outputs over40 shapes
against an independent exact-rational oracle, including reversed endpoints, signed
half ties, invalid shape/axis/index/kind, overflow and retained outputs. Full OS
artifact install/instances and single-image checkpoint/reopen pass, with source
unchanged. Python remains optional test tooling, not an image runtime dependency.
