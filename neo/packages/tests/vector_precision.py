"""Independent Decimal oracle for image vector0.2; run from repository root."""
import re
import subprocess
import sys
from decimal import Decimal, ROUND_HALF_UP, getcontext
from pathlib import Path
from random import Random

getcontext().prec = 80
exe = str(Path(sys.argv[1] if len(sys.argv) > 1 else 'build/x86_64/Windows/neo.exe').resolve())
rng = Random(41073)
scale = Decimal(1000000)
commands = []
expected = []


def quantize(value):
    return int(value.to_integral_value(rounding=ROUND_HALF_UP))


def array(values):
    return '[' + ' '.join(str(value) for value in values) + ']'


samples = [[1, 1], [1, -1], [1, 2, 2], [3000000, -4000000], [3037000499]]
for dimension in [1, 2, 3, 4, 7, 33, 100, 257]:
    for width in [2, 20, 1000000, 50000000]:
        for _ in range(3):
            values = [rng.randint(-width, width) for _ in range(dimension)]
            if not any(values):
                values[0] = 1
            samples.append(values)

for index, left in enumerate(samples):
    right = [rng.randint(-50000000, 50000000) for _ in left]
    factor = rng.choice([-2000000, -1500000, -500000, 0, 1, 500000, 1500000, 2000000])
    # The one near-square-limit sample still fits component scaling's int64 product.
    square_sum = sum(value * value for value in left)
    length = Decimal(square_sum).sqrt()
    commands += [f'set /contents/vector/dimension {len(left)}',
                 f'set /contents/vector/left {array(left)}',
                 f'set /contents/vector/right {array(right)}',
                 f'set /contents/vector/factor {factor}']
    for operation, result, tolerance in [
        ('subtract', [a - b for a, b in zip(left, right)], 0),
        ('scale', [quantize(Decimal(value) * factor / scale) for value in left], 0),
        ('magnitude', quantize(length), 0),
        ('normalize', [quantize(Decimal(value) * scale / length) for value in left], 1),
    ]:
        commands.append(f'call /contents/vector {operation}')
        expected.append((index, operation, result, tolerance))

run = subprocess.run([exe, 'live', 'neo/packages/vector.neo'],
                     input='\n'.join(commands + ['quit']) + '\n',
                     capture_output=True, text=True, timeout=60)
assert run.returncode == 0 and 'neo:' not in run.stdout, run.stdout[-10000:] + run.stderr
results = re.findall(r'ok;[^\r\n]*?result=([^\r\n]*)', run.stdout)
assert len(results) == len(expected), (len(results), len(expected), run.stdout[-10000:])
for output, (index, operation, reference, tolerance) in zip(results, expected):
    observed = [int(value) for value in output[1:-1].split()] if output.startswith('[') else int(output)
    if isinstance(reference, list):
        assert isinstance(observed, list) and len(observed) == len(reference), (index, operation, output)
        assert all(abs(a - b) <= tolerance for a, b in zip(observed, reference)), (index, operation, observed, reference)
        if operation == 'normalize':
            # Euclidean unit-length error follows the component precision bound.
            actual_length = Decimal(sum(value * value for value in observed)).sqrt()
            bound = Decimal('0.501') * Decimal(len(observed)).sqrt()
            assert abs(actual_length - scale) <= bound, (index, actual_length, bound)
    else:
        assert observed == reference, (index, operation, observed, reference)
print(f'PASS vector Decimal oracle: {len(samples)} vectors, {len(expected)} operations; '
      'exact subtraction/scaling/magnitude, normalized components within one raw quantum and unit-length bound')
