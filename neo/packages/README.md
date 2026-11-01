# Image packages

The VM loads one image source into one graph. These files are inert package
artifacts imported into that graph, not runtime source modules. Checkpointing
preserves the installed packages and instances in the saved image source.

[Scheduled sessions](session.md): bounded connection/transfer ticks, explicit retry,
optional automatic backoff, and full-state resynchronization after reconnect.

[Exposure services](exposure.md): ordinary-object publications, revisioned snapshot
updates, explicit acceptance/rejection, withdrawals and opt-in correlated
receipt/decision replies, implemented in neo. Both packages now install and instantiate in the full OS seed under the shared
65536-object/4MiB profile. See [capacity/restore](../../documentation/reference/network-restore.md).

`vector.neo` exports an independent `vector` prototype from `neo.maths.vector`
version 0.2.0. Values use raw signed integers at scale 1,000,000.
[Vector maths guide](vector.md) documents subtraction, scaling, magnitude,
normalization, numerical precision and failure boundaries. This is a numeric
vector foundation; frame/unit-aware points and displacements are still pending.

From the repository root, start `build/x86_64/Windows/neo.exe live neo/neo-os/image.neo`:

```text
pkg-load neo/packages/vector.neo
pkg install
set /package-manager/id "neo.maths.vector"
set /package-manager/export-name "vector"
set /package-manager/instance-name "v"
pkg instance
set /workspace/v/dimension 2
call /workspace/v initialize
set /workspace/v/left [1000000 2000000]
set /workspace/v/right [3000000 4000000]
call /workspace/v add
call /workspace/v dot
```

Addition returns `[4000000 6000000]`; dot returns `11000000` (11).
`initialize` creates zero-filled operands and result for the explicit dimension.
`add` and `dot` reject operand lengths differing from that dimension. No dimension
ceiling is imposed; allocation and execution budgets bound actual work.
Dot accumulates checked raw products in index order and quantizes once, nearest
with ties away from zero. Intermediate overflow fails explicitly.

Operands and dimension are caller inputs. `pending`, `index`, `sum`, `quotient`,
`remainder`, `check` and `round` are editable working machinery. Array operations publish `result` after calculation completes. A failure can leave working fields
changed; execution does not roll back arbitrary image state. Instances are
sequential, mutable calculators, not reentrant services. The OS seed stays generic.

The built-in `selftest` checks construction/addition/dot at dimensions
0, 1, 2, 3, 4, 7, 33, 100 and 257, plus zero/tiny vectors, signed rounding,
normalization precision, shape/kind errors, overflow and result retention. The
optional Decimal oracle checks 101 vectors/404 operations. See [details](vector.md).

[Matrix guide](matrix.md): `neo.maths.matrix` 0.3.0 adds compact identities,
explicit dense construction, rectangular vector application and left×right
composition at the same scalar scale. The guide includes live installation.
Pivoted inversion checks both rounded identity residuals before publication.
[Affine transforms](affine.md) now provide point/displacement application,
translated composition and residual-checked inversion. [Spaces and frames](frames.md) provide explicit identity, parent hierarchies and
checked point/displacement conversion using a granted matrix calculator.

[Geometry](geometry.md): `neo.spatial.geometry`0.1 adds packed n-dimensional
vertices, explicit point/segment/triangle topology, exact degeneracy reports and
checked shape/index validation. [Axis projections](projection.md) now retain source identity and support edits
that preserve hidden coordinates. [Coordinate-band slices](slices.md) now supply visibility masks, source-row mapping
and current-membership edits. [Segment sections](section.md) now generate coordinate-plane intersection points,
coplanar segment topology and exact rational provenance. [Triangle sections](triangle-sections.md) now preserve coplanar faces, reduce
degenerate hulls and report exact provenance/rounded collapse. [General affine views](linearview.md) now support explicit right-inverse edit
constraints. [Perspective views](perspective.md) now supply homogeneous projection, explicit
denominator bounds, per-vertex clipping codes and source mappings. Primitive
clipping remains renderer work; milestone9 acceptance audit is next.

Milestone9 passes its [acceptance audit](../../documentation/reference/spatial-audit.md).
All8 spatial and2 networking artifacts coexist with15 instances in one checked
OS image (50968 objects/2623370 bytes). Current host profile is65536 objects/4MiB.
Next is presentation/input milestone10; no GUI completion is implied.
