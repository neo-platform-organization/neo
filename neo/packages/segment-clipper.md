# Homogeneous segment clipping

`neo.render.segment-clipper` 0.1.0 exports `clipper`. Import/install this inert
artifact into the single live image and instantiate through the existing package
manager. Its literal behavior clips source segments before perspective division.
No kernel drawing primitive, source loader or OS-seed example is introduced.

## Connections and snapshots

Grant READ `input` to a refreshed perspective view with view-dimension2,
result-source-count N, homogeneous containing exactly3N integers, near/far and
clip-volume settings, and a READ `source` connection. Source exposes vertex-count
N and valid segment index pairs. The clipper ignores compact result/visibility
and vertex rejection codes: every original homogeneous vertex remains available.

Refresh perspective after source/map changes, then call clip. Counts/shape checks
cannot detect stale coordinates with an unchanged vertex count. near/far and
clip-volume are current input fields. No behavior is invoked through READ links.

The clipper owns a READ `source` self-connection and publishes generated 2D
geometry/view fields. Link [line renderer](line-renderer.md) view to this clipper
to render its generated segments. The self-connection is closed inside the copied
prototype and remaps on copy. Original geometry remains at input.source and is
never overwritten.

## Interval clipping and exact division

Fixed-point scale is1,000,000. near>0; far0 disables its plane, otherwise far>=near.
clip-volume must be boolean. Enabled half-spaces are inclusive:

```text
w >= near
w <= far                    (far nonzero)
-w <= x <= w, -w <= y <= w   (clip-volume enabled)
```

Every plane is linear on A+t(B-A). Intersect the retained t interval with [0,1]
using reduced exact fractions. Empty intervals reject edges; single-point
intervals emit degenerate segments. Equal endpoints and repeated edges are valid.

For retained n/d, compute weighted homogeneous numerators without first rounding
the intersection to an integer:

```text
H[j] = A[j]*(d-n) + B[j]*n
W    = A[w]*(d-n) + B[w]*n
projected[j] = round(H[j]*1000000 / W)
```

W must be positive. Reduce the H/W ratio before scaling, then round nearest with
ties away from zero. Premature intersection rounding can erase a raw-unit
coordinate near a small denominator. No zero/negative w is divided.

Plane distances, cross-comparisons, weighted products and rounding use checked
signed64 arithmetic. Fraction reduction avoids some growth but is not arbitrary
precision. Intermediates can overflow even when the mathematical answer fits;
there is no floating/wide-integer fallback. Plane calculations can fail even for
an edge eventually rejected by another plane.

## Output and provenance

Each accepted original edge emits two independent projected vertices and a
segment. Shared/coincident endpoints are not welded. Original order is preserved.

| Field | Meaning |
| --- | --- |
| result, vertices | Packed generated 2D coordinates |
| result-dimension, dimension | 2 |
| result-vertex-count, result-source-count, vertex-count | Twice accepted-segments |
| source-indices, segments | Identity row map and consecutive generated index pairs |
| accepted-segments | Retained edge count |
| edge-indices | Original source-segment ordinal for each generated segment |
| parameter-numerators, parameter-denominators | Reduced original-edge t per generated endpoint |

Fully rejected/empty topology publishes empty arrays/counts. Reversed edges have
parameters relative to their reversed source direction. Provenance supplies no
implicit picking/editing rule. Generated coordinates and topology are derived
state, while source geometry remains authoritative.

Validation/calculation precedes publication; ordinary failures retain published
outputs while scratch may change. Sequential publication can be partial on
allocation/budget failure. Instances are sequential, not transactional/reentrant.
Work/storage is linear in source vertices and segments, with at most six planes
per edge and Euclidean fraction reductions. Real allocation/behavior budgets apply.

## Verification and remaining work

Universal tests use source dimensions3,4,7,33 through perspective, clipper and line
renderer, checking24 exact RGBA frames: crossing edges with both endpoints rejected,
near/far/side boundaries, original zero/negative w, rational provenance, unchanged
source and invalid-near/overflow output retention. Clip budget is100,000 steps.

The maximum measured clip stage was2,575 steps across those reference cases.
Perspective refresh and line rasterization are separate calls and are excluded
from that count; this is not a wall-clock or large-scene responsiveness claim.

Optional tooling from repository root:

```powershell
python neo/packages/tests/homogeneous_segments.py
```

It checks107 clips/provenance sets against exact rational arithmetic, including
random/reversed segments, disabled side planes, signed half ties and raw-unit
precision. Geometry, perspective, clipper and line packages install in one OS
image. Generated line coverage and single-source save/reopen are verified without
artifact reload. Python is test tooling only.

Triangle clipping, material/depth interpolation, filled polygons, depth buffers,
glyphs and dirty updates remain open. This is a 2D-destination segment subset of
milestone11; milestone10's native/platform validation gaps remain tracked.
