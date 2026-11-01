# Coordinate-band slices and visibility

[Axis views](projection.md) · [Geometry](geometry.md)

`neo.spatial.projection`0.2.0 adds explicit coordinate-band selection to the
existing image-owned view. It still pins geometry0.1.0. Installed artifacts,
source geometry, views and grants remain part of the single loaded image graph.
This is selection of stored vertices, not generated segment/triangle intersections.

## Constraints and outputs

Set three parallel packed arrays: `slice-axes`, `slice-values`, and
`slice-tolerances`. Axes must be unique and within source dimension; values and
nonnegative tolerances are raw signed64 fixed-point values at scale1,000,000.
A vertex is visible when every specified coordinate satisfies
`abs(coordinate - value) <= tolerance`. Both boundaries are included. Zero
constraints select all vertices; zero tolerance selects exact coordinates.
Projection axes and constrained axes may overlap. Multiple constraints combine
with AND; there is no implicit constraint on unlisted axes.

`slice` validates the latest source, projection axes and constraints, then publishes:

| Field | Meaning |
| --- | --- |
| `result` | Visible vertices projected on the ordered `axes`, packed vertex-major. |
| `result-dimension` | Number of projection axes. |
| `result-vertex-count` | Number of visible vertices. |
| `result-source-count` | Source vertex count at calculation time. |
| `source-indices` | One original source vertex index per visible result row, in source order. |
| `visibility` | One integer0/1 per source vertex. |

The ordinary `project` handler now publishes the same mapping fields with all
source vertices visible, clearing any stale filtered mapping. An empty visible
set yields empty result/map and an all-zero mask. Empty projection axes can yield
an empty result with a nonempty map/count. Zero-dimensional vertices with no
constraints are all visible. Duplicate/overlapping projected positions stay
separate entries; no unique picking result is assumed.

Coordinate differences and absolute values are checked signed64 arithmetic.
Extreme inputs may explicitly overflow even when the mathematical comparison
would be false. There is no silent saturation or approximate membership test.
Shape/kind/axis/tolerance/arithmetic/budget failures before publication preserve
the previous outputs, while scratch fields may change. Output publication is
sequential, not transactional; interruption during it can leave a partial snapshot.
Constraint validation is O(c²) for duplicate checks, selection O(vertices*c),
projection O(visible*k); memory and invocation budgets bound actual work.

## Editing and freshness

`edit-slice-vertex` takes `vertex-index` as an original SOURCE index and the
existing `edit` array. It validates current constraints and current source
membership before calling the existing constrained edit. It never interprets
vertex-index as a cached visible row. A visible row can be mapped through
source-indices by the caller; after source reordering/removal or view changes,
refresh and resolve the selection again. Source indices are packed-array positions,
not persistent per-vertex object identities.

Only projection axes change; hidden coordinates and other source vertices are
preserved. If an edited projection axis is also constrained, the edit can move
the vertex out of the slice. The next refresh then hides it. Use ordinary
`edit-vertex` when deliberate source editing without membership restrictions is
wanted. Both edit handlers need READ|WRITE3 on source and invalidate source valid.
Membership is checked immediately before the edit in the sequential invocation;
there is no optimistic concurrency/revision protocol.

Result coordinates, mappings and masks are derived snapshots and require explicit
refresh. Geometry/topology is not copied or modified by filtering. In particular,
filtering vertices does not clip edges or faces, generate intersection vertices,
remap topology, invent normals or decide hits. [Segment sections](section.md) now generate crossing points and coplanar
segment topology separately. Triangle section topology remains future work. Coordinates remain in the source
frame; there is no implicit frame or unit conversion.

## Example in the loaded image

After installing/instantiating geometry as `g` and projection as `view` in the same
OS image (see the linked guides), use:

```text
link /workspace/view source /workspace/g 3
set /workspace/g/dimension 3
set /workspace/g/vertex-count 5
call /workspace/g initialize
set /workspace/g/vertices [10 0 5 20 1 5 30 2 5 40 3 5 50 -1 5]
set /workspace/view/axes [0]
set /workspace/view/slice-axes [1 2]
set /workspace/view/slice-values [1 5]
set /workspace/view/slice-tolerances [1 0]
call /workspace/view slice
show /workspace/view/source-indices
show /workspace/view/visibility
```

Raw result is `[10 20 30]`, map `[0 1 2]`, mask `[1 1 1 0 0]`.
Editing source vertex-index2 to `[99]` returns full vertex `[99 2 5]`.
Refresh produces `[10 20 99]`. Save the complete image; reopening retains the
constraints, source connection, edited geometry and derived outputs.

Strict Windows selftest covers masks, maps, empty selections, original-index edits,
hidden coordinates, rejected invisible edits, invalid tolerances and unfiltered
mapping refresh. Existing dimensional tests through257 pass. Optional
`python neo/packages/tests/slice_selection.py` checks157 independent outputs over50
shapes through257 dimensions, inclusive boundaries, multiple constraints, live
membership changes, malformed input/overflow/authority failures and full OS
single-image checkpoint/reopen. Existing195 projection/edit reference checks
also pass. Python is optional author/test tooling, not a runtime dependency.
