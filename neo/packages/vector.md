# Dimension-driven vector maths

[Image packages](README.md) · [Spatial contract](../documentation/spatial-protocol.md)

neo.maths.vector0.2.0 exports an independent vector prototype. This is ordinary
image code: no C arithmetic/square-root primitive, second evaluator or fixed
2D/3D case was added. Vectors are numeric arrays, not yet frame-aware points.
The OS seed remains generic.

## Values and operations

All components, factor, magnitude and normalized outputs use signed64-bit
fixed-point integers at scale1000000. Dimension/counts/indices are unscaled.
Configure dimension explicitly; initialize allocates zero-filled left/right/result
arrays of that length. It resets length0. There is no fixed dimension ceiling;
allocation, representation and execution budgets still apply.

| Handler | Inputs | Result |
| --- | --- | --- |
| initialize | dimension | Dimension after zero-filled operands/result are created. |
| add | left, right | Component sum in result. |
| subtract | left, right | Component left minus right in result. |
| scale | left, factor | Component product divided by1000000, in result. |
| dot | left, right | Scalar raw-product sum quantized once by1000000. |
| magnitude | left | Euclidean length, nearest raw integer, also stored in length. |
| normalize | left | Approximate unit vector in result, preserving direction. |

Binary operations require both lengths to equal dimension. Unary operations only
require left; an unused right array need not have the same shape. Factor must be
an integer even for an empty vector. Factor defaults1000000 (one). Scale and dot
round nearest with ties away from zero, including negative results. Equality
elsewhere in the language remains exact.

Empty vectors are valid: add/subtract/scale return empty, dot/magnitude return0.
Normalizing a zero vector, including an empty one, raises an explicit failure.
Negative dimensions and shape mismatches fail; payload-kind mistakes fail native
runtime kind checks. No implicit coercion, saturation or zero-direction is invented.

## Use in the OS image

```text
pkg-load neo/packages/vector.neo
pkg install
pkg clear
set /package-manager/id "neo.maths.vector"
set /package-manager/export-name "vector"
set /package-manager/instance-name "v"
pkg instance
set /workspace/v/dimension 2
call /workspace/v initialize
set /workspace/v/left [3000000 4000000]
call /workspace/v magnitude
call /workspace/v normalize
set /workspace/v/factor 500000
call /workspace/v scale
```

Magnitude returns5000000 (five). Normalize produces[600000 800000] (0.6,0.8).
Scale produces[1500000 2000000] from left; operations do not silently replace their
input with result. To reuse an output as input, write it explicitly. The length
field describes the last successful magnitude call, not every vector operation.

## Integer root and precision

Magnitude accumulates raw component squares without intermediate quantization,
then finds the floor root by binary search. Its midpoint arithmetic avoids signed
addition overflow, and root comparisons use division instead of squaring a large
midpoint. Final nearest-root rounding compares the remainder against the floor
root. It never computes an overflowing next-root square or uses host floating point.

Normalization does not divide by magnitude rounded to a component quantum. It
first multiplies the positive squared norm by powers of four and tracks the
corresponding power-of-two component scale, stopping before signed overflow.
It roots that high-precision norm and divides scaled components by the floor root.
The division helper rounds nearest, ties away from zero. For raw[1,1], this yields
[707107,707107], rather than the badly distorted result from dividing by raw length1.

The root denominator is at least1518500249 for every valid nonzero input. Its floor
error contributes less than0.000659 raw component units. Including final rounding,
each normalized component differs from the exact real-valued unit direction by
less than0.501 raw units. The result is approximate, not a promise of exact unit
length or bitwise equality with every correctly rounded irrational result. Its
Euclidean unit-length error is bounded by0.501*sqrt(dimension) raw units. A comparison
with independently rounded ideal components may differ by one raw unit near a
rounding boundary. Use explicit tolerances in later spatial code.

All raw arithmetic remains checked signed64-bit. The original squared norm must
fit INT64_MAX; even a mathematically representable final length can fail because
an intermediate product or sum overflows. A single component beyond raw3037000499
already exceeds the squaring range. Scale/dot can likewise reject representable
final quotients when raw products overflow. There is no silent rescaling down or
wide-integer arithmetic. A future numerical representation can extend that range
without changing the current literal graph syntax.

## Mutation, failure and persistence

Array operations construct pending and publish result after calculation completes.
Tested shape, zero-normalization and arithmetic-overflow failures preserve the
previous result; failed magnitude calculation preserves its previous length.
Scratch fields can change. These handlers are not transactions: allocation/budget
interruption at final publication or return may expose completed new state even
if the invocation reports failure. Inspect before retry; do not assume rollback.

prepare/check-left/check/round-div/round/squares/root-floor/next-index and their
working fields are editable implementation machinery. Instances are sequential
mutable calculators, not reentrant services. Copying the prototype duplicates
that machinery. Ordinary arrays/results and working state can be checkpointed;
loading does not execute them. OS install/instance/save/reopen/normalize is verified.

## Verification

Universal selftest checks dimensions0,1,2,3,4,7,33,100,257 and independent expected
roots/unit components; legacy addition/dot behavior remains checked. It covers
signed halfway scaling, tiny vectors, zero/negative/mismatched shapes, kind errors,
near-square limits, product/accumulator overflow and previous-result retention.

The optional80-digit Decimal oracle is independent of the integer algorithm:

```powershell
python neo/packages/tests/vector_precision.py build/x86_64/Windows/neo.exe
```

It checks101 deterministic vectors/404 operations, exact subtraction/scaling/
magnitude, component error and Euclidean unit-length bounds. Python is test tooling,
not a VM dependency. Strict Windows build/full suite pass. Transforms, frames,
geometry and projection remain subsequent milestone9 work.
