"""Exact-rational pixel oracle, package installation and single-image reopen."""
from fractions import Fraction
from pathlib import Path
import random
import re
import subprocess

exe = str(Path('build/x86_64/Windows/neo.exe').resolve())
out = Path('build/x86_64/Windows')
checkpoint = out / 'line-renderer-image.neo'
commands = []
expected = []

def arr(values): return '[' + ' '.join(map(str, values)) + ']'
def put(owner, name, value):
    commands.append(f'set /workspace/{owner}/{name} ' + (arr(value) if isinstance(value, list) else str(value)))
def call(owner, name): commands.append(f'call /workspace/{owner} {name}')
def check(mask):
    commands.append('show /workspace/r/pixel-mask')
    expected.append(mask)

def reference(a, b):
    """Intersect viewport grid lines with the exact segment, then quantize."""
    mask = [0] * 25
    axis = int(abs(b[1] - a[1]) > abs(b[0] - a[0]))
    if a[axis] > b[axis]: a, b = b, a
    for coordinate in range(5):
        if not a[axis] <= coordinate <= b[axis]: continue
        delta = b[axis] - a[axis]
        minor = a[1-axis]
        if delta:
            minor += int(Fraction((b[1-axis] - a[1-axis]) * (coordinate - a[axis]), delta))
        if 0 <= minor < 5:
            x, y = (coordinate, minor) if axis == 0 else (minor, coordinate)
            mask[y*5+x] = 1
    return mask

for name in ['geometry', 'projection', 'line-renderer']:
    commands += [f'pkg-load neo/packages/{name}.neo', 'pkg install', 'pkg clear']
for package, export, instance in [('neo.spatial.geometry','geometry','g'),
                                  ('neo.spatial.projection','projection','v'),
                                  ('neo.render.lines','renderer','r')]:
    commands += [f'set /package-manager/id "{package}"', f'set /package-manager/export-name "{export}"',
                 f'set /package-manager/instance-name "{instance}"', 'pkg instance']
commands += ['link /workspace/v source /workspace/g 1', 'link /workspace/r view /workspace/v 1']
put('g','dimension',2); put('g','vertex-count',2); put('g','segments',[0,1])
put('v','axes',[0,1]); put('r','width',5); put('r','height',5)
points = [(-2,-2),(0,0),(4,4),(6,6),(-2,2),(6,2),(2,-2),(2,6),
          (0,4),(4,0),(1,0),(4,2),(0,1),(2,4),(5,0),(0,5),(2,2)]
cases = [(a,b) for a in points for b in points]
rng = random.Random(20261023)
cases += [(tuple(rng.randrange(-8,13) for _ in range(2)),
           tuple(rng.randrange(-8,13) for _ in range(2))) for _ in range(64)]
cases += [((-10**9,2),(10**9,2)), ((10**9,2),(-10**9,2))]
for a,b in cases:
    put('g','vertices',[a[0]*1000000,-a[1]*1000000,b[0]*1000000,-b[1]*1000000])
    call('v','project'); call('r','prepare'); check(reference(a,b))

# A nontrivial permutation must map source topology to compact result rows.
put('g','vertex-count',3); put('g','vertices',[0,0,4000000,0,4000000,-4000000])
call('v','project')
put('v','source-indices',[2,0,1]); put('v','result',[4000000,-4000000,0,0,4000000,0])
call('r','prepare'); check([1]*5+[0]*20)
commands += [f'save {checkpoint.as_posix()}', 'quit']
result = subprocess.run([exe,'live','neo/neo-os/image.neo'], input='\n'.join(commands)+'\n',
                        capture_output=True,text=True,timeout=120)
(out/'line-renderer-oracle.log').write_text(result.stdout+result.stderr,encoding='utf-8')
assert result.returncode == 0 and 'neo:' not in result.stdout, result.stdout[-5000:]+result.stderr
actual = [[int(x) for x in item.split()] for item in re.findall(r'pixel-mask = \[([^\]]*)\]',result.stdout)]
assert len(actual) == len(expected), (len(actual),len(expected))
for i,(got,want) in enumerate(zip(actual,expected)):
    assert got == want, (i, cases[i] if i < len(cases) else 'permutation',got,want)
reopened = subprocess.run([exe,'live',str(checkpoint)],
    input='call /workspace/r prepare\nshow /workspace/r/pixel-mask\nquit\n',capture_output=True,text=True,timeout=30)
assert reopened.returncode == 0 and 'neo:' not in reopened.stdout, reopened.stdout+reopened.stderr
mask = re.findall(r'pixel-mask = \[([^\]]*)\]', reopened.stdout)
assert len(mask) == 1 and list(map(int,mask[0].split())) == expected[-1], reopened.stdout
print(f'PASS line renderer: {len(expected)} exact-rational masks, package install/instance, row permutation and image reopen')
