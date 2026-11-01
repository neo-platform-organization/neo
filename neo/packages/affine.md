# Affine transforms

[Matrix contract](matrix.md) · [Spatial protocol](../documentation/spatial-protocol.md)

`neo.maths.matrix` 0.3.0 includes affine operations on the existing `matrix`
prototype. These are literal image behaviors, with no new C primitive and no
geometry in the generic OS seed. Values retain scale 1,000,000.

## Representation and order

The left transform maps K coordinates to M coordinates: `(A,t)`, where left is
M×K and `left-translation` has M entries. The right transform `(B,u)` maps N to K:
right is K×N and `right-translation` has K entries. Existing row-major,
column-vector and compact linear identity conventions apply.

```text
point:        A*x + t
 displacement: A*x
left after right: (A*B, A*u + t)
inverse:      (A^-1, -A^-1*t)
```

These are numeric point/displacement operations. They do not yet carry frame,
space, axis or unit identities. Non-spatial objects stay non-spatial. Frame-aware
conversion is the next checkpoint; this package does not infer a frame from
containment or names.

## Operations

| Selector | Inputs and result |
| --- | --- |
| `affine-initialize` | Calls dense `initialize`, then creates M zero left translations and K zero right translations. Returns true; previous published outputs remain. |
| `affine-identity` | Takes `dimension`; configures both linear identities and zero translations, publishes compact identity plus zero `result-translation`. Does not initialize input. |
| `point` | Validates left, K-element input, M-element left translation; publishes an M×1 vector `result`. |
| `apply` | Existing displacement operation: ignores translation entirely, including malformed translation fields. Publishes an M×1 vector. |
| `affine-compose` | Validates both matrices/translations; publishes linear `result` with its kind/shape, and M-element `result-translation`. |
| `affine-invert` | Validates square left and its translation; publishes the inverse linear map and inverse translation. Right/input/columns are irrelevant. |

`point`, `apply`, and ordinary linear operations leave `result-translation` alone.
Only affine transform results pair linear `result` with `result-translation`.
There is no homogeneous n+1 matrix or dimension-specific path. Zero-dimensional
transforms are valid. Rectangular composition and point/displacement application
are supported; inversion requires a square map. Compact linear identities need
no coefficient grid, but affine identities explicitly allocate n translations
and therefore have O(n) storage/work rather than constant storage.

## Numeric and failure contract

Each dot product rounds once, nearest with ties away from zero, before adding
translation. Addition and negation are checked signed-64-bit arithmetic. Inversion
uses the existing pivot and linear residual policies; it also checks each
component of `round(A * inverse-translation) + t` against `inverse-tolerance`.
The other translation residual cancels by construction using the same rounded
`A^-1*t`. This bounds rounded residuals, not coefficient errors or conditioning.
For example, a scalar map of 3 with translation 100 can fail the default affine
residual even when its linear inverse passes. Caller-controlled tolerances weaken
or strengthen this acceptance criterion. Quantized composition need not associate
or exactly equal successive point applications.

No implicit saturation, pseudoinverse or frame conversion occurs. Bad shapes,
singular/small pivots, insufficient precision, checked overflow and execution
limits fail explicitly. Scratch fields (including `translation-pending`) may change.
Calculation failures before publication retain both published pieces and shape.
Affine publication writes translation then linear result/metadata sequentially;
interruption during publication can leave a partial result. Constructors likewise
are not transactions, and can partially change inputs or publish before completion.
Instances require sequential use. Dense work/storage obey the matrix package's
budgets; no fixed dimensional ceiling is added.

## Live example

Install/instantiate as shown in the [matrix guide](matrix.md), naming the instance
`affine`, then run:

```text
set /workspace/affine/rows 2
set /workspace/affine/inner 2
set /workspace/affine/columns 2
call /workspace/affine affine-initialize
set /workspace/affine/left [1000000 1000000 0 1000000]
set /workspace/affine/right [2000000 0 0 4000000]
set /workspace/affine/left-translation [3000000 5000000]
set /workspace/affine/right-translation [7000000 11000000]
call /workspace/affine affine-compose
show /workspace/affine/result-translation
```

The linear result is `[2000000 4000000 0 4000000]`, translation
`[21000000 16000000]`. Full OS installation, instance creation, saving, reopening
and recomputation of these values pass.

Strict Windows build/universal selftest includes dynamic dimensions through257,
point/displacement distinction, noncommuting composition, pivoted affine inversion,
singular/shape/precision failures and retained outputs. The optional
`python neo/packages/tests/affine_precision.py` checks 220 independent integer
reference outputs across empty/rectangular shapes and dimensions0–7, plus
translation overflow, bad shapes and rejected affine residuals. Existing144
linear arithmetic and48 rational inverse reference checks still pass. Python is
optional test tooling, not a package dependency.
