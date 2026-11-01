# Packed n-dimensional geometry

[Spatial protocol](../documentation/spatial-protocol.md) · [Frames](frames.md)

`neo.spatial.geometry` 0.1.0 exports an independent `geometry` prototype with no
package dependencies. It is an image artifact for import into the single loaded
image graph, not a source module evaluated or loaded during geometry operations.
The full graph, including installed packages and instances, can be checkpointed
and reopened as one image source. No primitive or boot-seed example is added.

## Representation

`dimension` and `vertex-count` are explicit nonnegative integer counts.
`vertices` is vertex-major packed signed-64-bit fixed-point data (scale1,000,000),
with exactly dimension×vertex-count entries. Vertex i, axis j is at index i*n+j.
An explicit vertex count is necessary: in dimension zero, any number of vertex
entries has an empty coordinate array. There is no fixed maximum dimension.
Memory, checked index arithmetic and invocation budgets still limit actual work.

`points` contains vertex indices. `segments` contains consecutive index pairs;
`triangles` contains consecutive triples. Every index must be in [0,vertex-count).
Empty topology is valid. Duplicates and order are preserved; topology is never
inferred from coordinate adjacency. Index counts are unscaled integers.

## Operations

- `initialize`: allocate zero vertices for dimension/vertex-count, clear topology
  and reports, and set `valid=false`. Returns true without validating topology.
- `validate`: check packed shape, topology arity and every index, then classify
  degeneracy. Publish `point-count`, `segment-count`, `triangle-count` and one
  integer flag per segment/triangle (1 degenerate, 0 nondegenerate). Return true.
- `vertex`: take `vertex-index`, validate packed shape and bounds, publish the
  corresponding coordinates in `result`. It does not validate topology.
- `validate-frame`: require an explicitly granted READ `frame` connection and
  compare its dimension to the geometry dimension, after checking packed shape.
  This does not validate the frame hierarchy or imply matching space identities;
  conversion still belongs to the frame converter.

Frame attachment is optional for standalone numeric geometry. Other objects can
refer to this geometry and its frame through ordinary explicit protected links.
Names, geometry metadata and spatial relationships grant no implicit authority.

## Degeneracy and failures

A segment is degenerate when its endpoints have identical coordinates, including
coincident entries with distinct indices. A triangle is degenerate when B−A and
C−A are linearly dependent. Classification finds a nonzero component of B−A and
checks the corresponding 2×2 minors against every axis. If B−A is zero, the
triangle is degenerate. This is O(n) per element, works in every dimension, and
uses exact checked integer differences/products/subtractions with no quantization
or tolerance. A raw-unit right triangle is nondegenerate. All segments and
triangles in dimension zero are degenerate; all triangles in dimension one are
degenerate. Higher-dimensional cells are outside this initial topology contract.

All intermediate arithmetic must fit signed64 bits. Large collinear triangles
can therefore fail overflow even when their exact minors would cancel. Failure
is explicit, with no silent fallback to a degenerate classification. Segment
comparison itself requires no coordinate subtraction. Degenerate topology remains
valid data; this package reports it rather than deleting it or inventing normals,
areas, intersections or hits. Manifoldness, winding, duplicates and self-intersection
are not validated here. Those are later geometry/rendering policies.

`validate` first writes `valid=false`. Shape/type/index/arithmetic/budget failure
leaves it false and retains previous published reports if publication has not
started; scratch arrays and counters may change. Report publication is sequential
and is not a transaction. Successful validation finally sets `valid=true`.
External edits do not automatically invalidate this flag: it describes the last
validation attempt, not a versioned proof of current data. Revalidate after edits.
Constructors may partially change data on failure. `vertex` preserves its old
result on calculation failure before publication. Instances require sequential use.

## Single-image workflow

Start `build/x86_64/Windows/neo.exe live neo/neo-os/image.neo` from the repository
root. The following imports an inert artifact into that live graph, then creates
an instance there:

```text
pkg-load neo/packages/geometry.neo
pkg install
pkg clear
set /package-manager/id "neo.spatial.geometry"
set /package-manager/export-name "geometry"
set /package-manager/instance-name "geometry"
pkg instance
set /workspace/geometry/dimension 4
set /workspace/geometry/vertex-count 3
call /workspace/geometry initialize
set /workspace/geometry/vertices [0 0 0 0 1000000 0 0 0 0 0 0 1000000]
set /workspace/geometry/points [0 1 2]
set /workspace/geometry/segments [0 1 1 2 2 0]
set /workspace/geometry/triangles [0 1 2]
call /workspace/geometry validate
show /workspace/geometry/triangle-degenerate
save build/x86_64/Windows/geometry-image.neo
```

The flag is `[0]`. Reopen with
`build/x86_64/Windows/neo.exe live build/x86_64/Windows/geometry-image.neo`.
The installed artifact and instance are already in that image; no package import
is needed again. Setting vertex-index to2 and calling vertex returns
`[0 0 0 1000000]`. This full OS import/instance/save/reopen/revalidate workflow passes.

Strict Windows build/universal selftest covers dimensions0,1,2,3,7,33,100,257,
construction, topology counts, degeneracy, extraction, index failures and checked
shape overflow. The optional `python neo/packages/tests/geometry_precision.py`
checks144 outputs across48 shapes against an independent unbounded-integer Gram
determinant oracle, plus tiny triangles, malformed shape/kind/arity/index cases,
coordinate/product overflow, retained reports, explicit frame-dimension checks and
bounded construction. Python is optional author/test tooling only. The interpreter
uses the literal graph in the loaded image; these Python files are not runtime
source loaders or a second language layer.
