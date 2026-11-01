"""Independent unbounded-integer oracle for matrix0.1; run from repo root."""
import re
import subprocess
import sys
from pathlib import Path
from random import Random

exe = str(Path(sys.argv[1] if len(sys.argv) > 1 else 'build/x86_64/Windows/neo.exe').resolve())
rng = Random(70211)
unit = 1000000
commands = []
expected = []


def array(values):
    return '[' + ' '.join(str(value) for value in values) + ']'


def rounded(value):
    quotient, remainder = divmod(abs(value), unit)
    if remainder * 2 >= unit:
        quotient += 1
    return -quotient if value < 0 else quotient


def dense_case(rows, inner, columns, left, right, vector):
    commands.extend([f'set /contents/matrix/rows {rows}',
                     f'set /contents/matrix/inner {inner}',
                     f'set /contents/matrix/columns {columns}',
                     'set /contents/matrix/left-kind "dense"',
                     'set /contents/matrix/right-kind "dense"',
                     f'set /contents/matrix/left {array(left)}',
                     f'set /contents/matrix/right {array(right)}',
                     f'set /contents/matrix/input {array(vector)}',
                     'call /contents/matrix apply', 'call /contents/matrix compose'])
    expected.append([rounded(sum(left[i * inner + k] * vector[k] for k in range(inner)))
                     for i in range(rows)])
    expected.append([rounded(sum(left[i * inner + k] * right[k * columns + j] for k in range(inner)))
                     for i in range(rows) for j in range(columns)])


# Noncommuting shear*scale and sums that distinguish final from per-term rounding.
dense_case(2, 2, 2, [unit, unit, 0, unit], [2 * unit, 0, 0, 3 * unit], [unit, 2 * unit])
dense_case(1, 2, 1, [1, 1], [500000, 500000], [500000, 500000])
dense_case(1, 2, 1, [-1, -1], [500000, 500000], [500000, 500000])
shapes = [(0, 0, 0), (2, 0, 3), (0, 3, 4), (3, 2, 0), (1, 1, 1),
          (2, 3, 4), (4, 2, 3), (3, 4, 2), (7, 7, 7)]
shapes += [(rng.randrange(8), rng.randrange(8), rng.randrange(8)) for _ in range(60)]
for rows, inner, columns in shapes:
    left = [rng.randint(-2 * unit, 2 * unit) for _ in range(rows * inner)]
    right = [rng.randint(-2 * unit, 2 * unit) for _ in range(inner * columns)]
    vector = [rng.randint(-2 * unit, 2 * unit) for _ in range(inner)]
    dense_case(rows, inner, columns, left, right, vector)

run = subprocess.run([exe, 'live', 'neo/packages/matrix.neo'],
                     input='\n'.join(commands + ['quit']) + '\n',
                     capture_output=True, text=True, timeout=60)
assert run.returncode == 0 and 'neo:' not in run.stdout, run.stdout[-10000:] + run.stderr
results = re.findall(r'ok;[^\r\n]*?result=([^\r\n]*)', run.stdout)
assert len(results) == len(expected), (len(results), len(expected), run.stdout[-10000:])
for index, (output, reference) in enumerate(zip(results, expected)):
    observed = [int(value) for value in output[1:-1].split()]
    assert observed == reference, (index, observed, reference)
print(f'PASS matrix integer oracle: {len(expected)} exact apply/compose results; '
      'rectangular/zero shapes, noncommuting order and once-per-output signed rounding')
