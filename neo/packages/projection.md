# Axis projections and constrained edits

[Geometry](geometry.md) · [Spatial protocol](../documentation/spatial-protocol.md)

`neo.spatial.projection`0.2.0 exports `projection` and pins geometry0.1.0.
It is an inert artifact imported into the single live image graph. Source geometry,
views and their explicit connections remain in that graph and its saved checkpoint.
No runtime module loader, primitive, GUI demo or second authoritative scene is added.

## View contract

Grant a `source` connection to a geometry instance. READ (1) permits projection;
READ|WRITE (3) permits editing. `axes` is an ordered packed array of unique source
axis indices. Every axis must be within the source dimension. Reordered subsets,
all axes, and an empty subset are valid. Duplicate axes fail because an inverse
edit would otherwise assign competing values to one source coordinate.

Set `view-dimension`, then call `initialize` to generate the first k axes. This
validates source shape and requires0<=k<=source dimension. It does not project or
reset published outputs. Afterward `axes` is authoritative; view-dimension is only
a factory input. No dimension-specific cases or fixed maximum dimension are added.
Unique-axis checking costs O(k²); actual allocations and execution budgets apply.

`project` reads current source dimension, explicit vertex count and packed vertices,
checks shape and axes, and publishes vertex-major projected `result`,
`result-dimension` and `result-vertex-count`. Vertex order and count are retained;
empty-axis views have an empty payload even when vertices exist. Source is never
changed. Results are derived snapshots: call project again after any source or
axis edit. There is no automatic dirty notification or second editable vertex store.

Topology remains with the source geometry; the view does not duplicate or validate
it. Projection can collapse previously nondegenerate elements, so consumers must
classify degeneracy in projected coordinates before generating normals or hits.
Overlapping vertices retain their separate source indices; no picking policy or
unique inverse from a screen position is invented. Coordinates are in the source
frame, without implicit coordinate-frame conversion or unit conversion.

## Editing

Set `vertex-index` to an existing source vertex and `edit` to exactly k values,
then call `edit-vertex`. It rereads the latest source data, changes only the selected
axes of that vertex, and writes the updated packed source vertices. All hidden
coordinates, other vertices and topology remain as currently stored. `edit-result`
is the full edited source vertex. Projected `result` is unchanged until project is
called again. Multiple views can share the same explicit source identity and see
one another's changes on refresh.

An empty-axis edit is valid for an existing vertex and preserves every coordinate.
It still invalidates the source validation flag. A zero-vertex geometry has no
editable vertex even when dimension is zero.

All shape/axis/index/length validation and scratch calculation precede source writes.
`edit-vertex` writes source `valid=false`, then writes the new vertex payload, then
publishes edit-result. Ordinary validation and read-only authority failures preserve
source coordinates. Budget/allocation failure during publication can leave only
the invalidated flag or an applied edit without its published response. This is
sequential graph mutation, not a transaction or automatic rollback. Revalidate
geometry after edits, since topology may become degenerate. Scratch fields may
change on failure; instances require sequential use. No stale-projection comparison
is imposed: edits deliberately apply their selected coordinates to the latest source.

## Single-image example

Start `build/x86_64/Windows/neo.exe live neo/neo-os/image.neo`. Import geometry and
projection artifacts using pkg-load/pkg install/pkg clear. Instantiate geometry
as `g` and projection as `view` through package-manager (as in the geometry guide).
Then run:

```text
link /workspace/view source /workspace/g 3
set /workspace/g/dimension 4
set /workspace/g/vertex-count 1
call /workspace/g initialize
set /workspace/g/vertices [1000000 2000000 3000000 4000000]
set /workspace/view/axes [0 2]
call /workspace/view project
set /workspace/view/vertex-index 0
set /workspace/view/edit [9000000 8000000]
call /workspace/view edit-vertex
call /workspace/view project
save build/x86_64/Windows/projected-image.neo
```

Projection first returns `[1000000 3000000]`. Editing returns the full vertex
`[9000000 2000000 8000000 4000000]`; refreshed projection is `[9000000 8000000]`.
Reopening the saved image retains the installed artifacts, source, view and grants.
No artifact reload is required.

Strict Windows universal selftest covers construction, projection, editing and
preservation through dimensions0,1,2,4,7,33,100,257. The optional
`python neo/packages/tests/projection_edit.py` checks195 independent outputs over48
shapes, two views sharing one source, updates between projection and edit, duplicate/
negative/out-of-range axes, malformed shapes, index/length errors, missing/read-only
connections and full OS single-image save/reopen. Python is optional test tooling.

This checkpoint provides axis selection and its explicit editing constraint only.
[Coordinate-band slices](slices.md) now supply explicit visibility and source-row
mapping. [Geometric sections](triangle-sections.md) and [general affine views](linearview.md)
are now available separately. Perspective, clipping, picking and GUI viewports
remain later work; milestone9 is not complete yet.

Version0.2 also publishes source-indices, visibility and result-source-count for
ordinary project results, selecting every source vertex. See the slice guide
for mapping freshness and membership-constrained edits.
