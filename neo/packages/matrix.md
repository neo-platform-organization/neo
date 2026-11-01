# Dimension-driven linear matrices

`matrix.neo` exports `matrix` from `neo.maths.matrix` version 0.3.0.
All operations are ordinary image behavior; no native maths primitive is added.
Scalars are signed-64-bit integers at scale 1,000,000. Counts are unscaled.

## Shapes and order

Dense arrays use row-major order and column vectors. `rows=M`, `inner=K`,
`columns=N` describe left M×K and right K×N matrices. `apply` computes left×input
for a K-element input. `compose` computes left×right, so right acts first.
Applying B after A requires B as left and A as right. Singular forward maps are
valid. [Affine operations](affine.md) supply translation. Units and frame conversion remain future work.

`left-kind` and `right-kind` are `"dense"` or `"identity"`. A compact identity
must be square and have an empty coefficient array. Unknown kinds and malformed
shapes fail explicitly. `apply` only validates its left operand and input.

## Construction and results

- Set `dimension`, then call `identity`: configure both operands as compact n×n
  identities, clear their coefficient arrays, and publish a compact identity.
  It allocates no n² grid and does not initialize `input`.
- Set `rows`, `inner`, `columns`, then call `initialize`: allocate zero-filled
  dense operands and a K-element input. It returns true without publishing a result.
- Set operands and call `apply` or `compose`.
- Call `materialize` to publish a dense representation of left. It leaves the
  left operand's representation unchanged.

`result-kind` is `"vector"`, `"dense"` or `"identity"`; `result-rows` and
`result-columns` preserve shape, including empty arrays. Vector results are M×1.
Two compact identities compose to a compact identity. Other identity compositions
copy the dense operand. Compact identity application copies input exactly.

No fixed dimension ceiling exists. Even a very large compact identity can be
represented without allocating coefficients; materialization and actual input
arrays remain subject to memory and execution budgets. Dimension zero is valid.
Dense M×0 application yields M zeros; M×0 times 0×N yields an M×N zero array.

## Numeric and mutation contract

Dense calculations accumulate checked raw products in index order, then divide
by 1,000,000 once per output, nearest with ties away from zero. Every intermediate
product and sum must fit signed 64 bits, even if a final rounded answer would fit.
Compact identity shortcuts avoid those products and preserve input exactly.
Composition is quantized, so associativity and equality with separately applied
transforms are not guaranteed. Shape/type failures and overflow are explicit.

Instances are sequential mutable calculators. `pending`, indices, sums and rounding
fields are scratch state. Calculation failures before publication retain the
previous result and metadata; scratch fields may change. Publication writes
result and metadata sequentially: interruption there is not a transaction.
Constructors can partially change operands on failure. Callers must provide their
own scheduling/authority discipline when sharing an instance.

## Live package example

From the repository root, run `build/x86_64/Windows/neo.exe live neo/neo-os/image.neo`:

```text
pkg-load neo/packages/matrix.neo
pkg install
pkg clear
set /package-manager/id "neo.maths.matrix"
set /package-manager/export-name "matrix"
set /package-manager/instance-name "m"
pkg instance
set /workspace/m/rows 2
set /workspace/m/inner 3
set /workspace/m/columns 2
call /workspace/m initialize
set /workspace/m/left [1000000 2000000 3000000 4000000 5000000 6000000]
set /workspace/m/input [1000000 2000000 3000000]
call /workspace/m apply
set /workspace/m/right [7000000 8000000 9000000 10000000 11000000 12000000]
call /workspace/m compose
```

Application returns `[14000000 32000000]`; composition returns
`[58000000 64000000 139000000 154000000]`. Installing, instantiating, saving,
reopening and recomputing this example were verified in the full OS seed.
No demo objects are installed in the generic seed.

## Verification

The strict Windows build and universal `selftest` cover computed dimensions
0, 1, 2, 3, 4, 7, 33, 100, 257 and compact INT64_MAX identities, dense paths,
rectangular/empty products, noncommuting order, singular forward maps, signed
rounding, malformed shapes/kinds, checked overflow, budget failures and retention.
`python neo/packages/tests/matrix_precision.py` optionally checks 72 cases / 144
apply and compose outputs against an independent unbounded-integer oracle.
Python is test tooling only. Dense apply costs O(MK), dense composition O(MKN);
large operations can exhaust a bounded invocation before completion.

## Inversion (0.2.0)

`invert` reads the square left operand; right/input/columns are irrelevant.
Compact identities invert without allocating a grid, including dimension zero.
Dense matrices use dimension-driven Gauss–Jordan elimination with maximum-absolute
partial pivoting (first row wins ties), row swaps, signed nearest division with
ties away from zero, and checked signed-64-bit intermediates. Left is preserved;
`work` and `pending` are scratch arrays. Dense empty matrices invert to dense empty.

`pivot-minimum` defaults to 1 raw unit: pivots with absolute value at or below
that threshold fail. It is a nonnegative caller policy, not a condition estimate.
`inverse-tolerance` defaults to 10 raw units. Before publishing, both A×X and X×A
are calculated with checked raw products/sums and once-rounded outputs. Each
component must differ from the corresponding identity component by at most that
nonnegative tolerance. This bounds the rounded residual, not coefficient error
or distance to an exact inverse; callers can loosen it and thereby weaken the
check. Even an invertible matrix may fail because of quantization, a small pivot,
overflow or execution budget. A singular/unsuitable matrix fails explicitly;
there is no pseudoinverse or implicit success fallback. The default residual
threshold is below one identity unit and rejects singular maps. Failure status
does not distinguish singularity from insufficient precision (`explicit failure`).

An invertible example requiring a row swap is `[0 2000000 1000000 3000000]`;
its inverse is `[-1500000 1000000 500000 0]`. A scalar `[3000000]` produces
`[333333]` with the default tolerance; a zero tolerance rejects its rounded
residual of one raw unit. Pre-publication failures retain old result metadata;
publication remains sequential, with the same interruption caveat as above.
Dense inversion uses O(n³) work and O(n²) scratch storage; bounded CLI calls can
run out of steps at modest sizes. There is no dimension-specific implementation.

The strict build/universal selftest covers compact identities through INT64_MAX,
empty dense matrices, pivot swaps, negative inverses, singular/small pivots,
rectangular rejection, overflow and failure retention. The optional
`python neo/packages/tests/matrix_inverse.py` compares 48 matrices of dimensions
1–4 against an independent exact-rational inverse (observed maximum coefficient
error 1.2055 raw, a test observation rather than a general bound), and verifies
strict residual rejection/retention. The previous 144 arithmetic oracle checks
still pass. Full OS package installation, instance creation, save/reopen and
inversion recomputation also pass. No C runtime primitive or seed content added.
