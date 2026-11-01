# Spaces and coordinate frames

[Spatial protocol](../documentation/spatial-protocol.md) · [Affine maths](affine.md)

`neo.spatial.frames` 0.1.0 exports independent `space`, `frame` and `converter`
prototypes. Its manifest pins `neo.maths.matrix` 0.3.0. All implementation is
ordinary image behavior. No runtime primitive or OS seed example is added.

## Explicit graph identity

A space declares `dimension`, ordered `axes`, `axis-units`, `metric` and `scale`.
`initialize` generates axes `[0 ... n-1]` and n zero unit codes. The reference
contract requires metric `"euclidean"`, scalar scale 1,000,000, and nonnegative
integer unit codes, one per axis. Code zero denotes dimensionless; other codes
are caller-assigned unit identifiers, not scale factors or a unit registry.
There is no automatic metres/centimetres conversion. All frames in a space share
these declarations; local numeric scaling is an explicitly authored affine map.
Changing metadata changes the live contract without converting stored coordinates.

A frame holds `dimension`, boolean `root`, `linear-kind`, row-major `linear`,
and `translation`, plus an explicit `space` connection. Call `initialize` after
granting space: it reads its dimension and creates a compact identity map and
zero translation. It does not reset `root` or parent connections.

A nonroot frame's `parent` connection defines its map into that parent's
coordinates. Containment is irrelevant. Both must reference the identical space
object and have its dimension. Root frames define canonical coordinates; their
linear map must be compact identity and translation must be all zero. A root's
parent connection, if present, is ignored. No origin is inferred for ordinary
objects without a spatial attachment. Any authorized object can explicitly hold
a frame connection; that relationship grants only the rights actually delegated.

Names and identical metadata do not establish identity. Conversion requires
both chains to reach the identical root frame as well as the identical space.
Two distinct roots in one space are deliberately rejected as disconnected.
Connections survive copy/checkpoint remapping; integer IDs never supply identity.

## Conversion and authority

A converter has caller inputs `kind` (`"point"` or `"displacement"`) and `input`,
plus explicitly granted `source`, `target` and `calculator` connections.
`convert` validates space metadata, input length and both complete frame paths,
checks repeated frame identities to reject cycles, then transforms the input
outward along source parents and inward along target parents in reverse order.
Point operations include translations; displacement operations ignore them.
Target maps are inverted with the matrix package's checked inverse policies.
A singular source map may apply forward; a singular target map fails inversion.
Calculator `pivot-minimum` and `inverse-tolerance` remain caller policy.

Grant READ|DELEGATE (33) on source/target and parent connections because the
converter stores attenuated traversal references in owned scratch path objects.
Space connections need READ (1). Supply a private matrix instance as calculator
with READ|WRITE|ACT (67); merely installing the dependency does not grant it.
Spatial links never add rights or establish authority from a name. Missing or
stale links and insufficient grants fail through the existing VM mechanisms.

`result` is published only after the entire conversion succeeds. Calculation,
shape, identity, cycle, inverse and budget failures preserve its previous value.
Paths, traversal links, `pending`, and calculator operands/results may change.
There is no rollback; interruption during scratch-link construction can require
explicit repair (for example, removing a leftover `next-cursor` link).
Instances and calculators require sequential use. Space/frame edits are live;
there is no second authoritative world model or cached conversion graph.

The initial algorithm follows full root paths, even when frames share a nearer
ancestor or are identical. It can therefore incur extra quantization, overflow,
or inverse failure; same-frame conversion is not an unconditional identity
shortcut. Each edge rounds according to the affine contract. Inverse residuals
are checked per edge, not as an overall path-error guarantee. No least-common-
ancestor optimization, custom metric, cross-space/unit bridge, or automatic
reparenting conversion is implemented in this version.

No dimension or hierarchy ceiling is hard-coded. Dimension zero is valid.
Paths use O(depth) scratch nodes, cycle checks O(depth²) identity comparisons;
actual array, inverse, graph-allocation and execution budgets bound work. Old
paths are deleted when rebuilt. Long paths may exhaust one bounded invocation.

## Full OS example

From the repository root start `build/x86_64/Windows/neo.exe live neo/neo-os/image.neo`:

```text
pkg-load neo/packages/matrix.neo
pkg install
pkg clear
pkg-load neo/packages/frames.neo
pkg install
pkg clear
set /package-manager/id "neo.maths.matrix"
set /package-manager/export-name "matrix"
set /package-manager/instance-name "math"
pkg instance
set /package-manager/id "neo.spatial.frames"
set /package-manager/export-name "space"
set /package-manager/instance-name "space"
pkg instance
set /package-manager/export-name "frame"
set /package-manager/instance-name "root-frame"
pkg instance
set /package-manager/instance-name "local"
pkg instance
set /package-manager/export-name "converter"
set /package-manager/instance-name "conversion"
pkg instance
link /workspace/root-frame space /workspace/space 1
link /workspace/local space /workspace/space 1
set /workspace/space/dimension 2
call /workspace/space initialize
call /workspace/root-frame initialize
call /workspace/local initialize
set /workspace/local/root false
set /workspace/local/translation [3000000 4000000]
link /workspace/local parent /workspace/root-frame 33
link /workspace/conversion source /workspace/local 33
link /workspace/conversion target /workspace/root-frame 33
link /workspace/conversion calculator /workspace/math 67
set /workspace/conversion/input [1000000 2000000]
call /workspace/conversion convert
save build/x86_64/Windows/coordinate-frames.neo
```

The result is `[4000000 6000000]`. Setting kind to `"displacement"` returns
`[1000000 2000000]`. Root conversion needs no inverse, so this same calculator
can later be used for an explicitly granted target frame and inward conversion.

Strict Windows build/universal selftest covers a two-edge source hierarchy,
points/displacements at dimensions0,1,2,3,7,33,100,257, cycles and distinct spaces.
The optional `python neo/packages/tests/frames_conversion.py` independently checks
60 hierarchical results across dimensions0–7, a dense inward map requiring a row
swap, and singular/cyclic/disconnected/metadata/shape/authority failures retaining
result. Tests use explicit fixtures, not seed content. Both packages validate and
install together in the full OS; instance creation, conversion, checkpoint,
reopen and recomputation pass. Python is optional test tooling.
