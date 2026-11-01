"""Independent exact-rational inverse checks; optional Python test tooling."""
from fractions import Fraction as F
from pathlib import Path
from random import Random
import subprocess
import re
Q=1000000
rng=Random(8202)
commands=[]
references=[]
for size in range(1,5):
 for sample in range(12):
  a=[[rng.randint(-Q,Q) for j in range(size)] for i in range(size)]
  for i in range(size): a[i][i]+=(size+1)*Q
  if sample%2: a[0],a[-1]=a[-1],a[0]
  work=[[F(x,Q) for x in row]+[F(i==j) for j in range(size)] for i,row in enumerate(a)]
  for k in range(size):
   pivot=next(i for i in range(k,size) if work[i][k])
   work[k],work[pivot]=work[pivot],work[k]
   d=work[k][k];work[k]=[x/d for x in work[k]]
   for i in range(size):
    if i!=k:
     f=work[i][k];work[i]=[x-f*y for x,y in zip(work[i],work[k])]
  references.append([x*Q for row in work for x in row[size:]])
  commands += [f'set /contents/matrix/rows {size}',f'set /contents/matrix/inner {size}',
   'set /contents/matrix/left-kind "dense"','set /contents/matrix/left ['+' '.join(str(x) for row in a for x in row)+']','call /contents/matrix invert']
run=subprocess.run([str(Path('build/x86_64/Windows/neo.exe').resolve()),'live','neo/packages/matrix.neo'],input='\n'.join(commands+['quit'])+'\n',capture_output=True,text=True,timeout=60)
results=re.findall(r'ok;[^\r\n]*?result=\[([^\r\n]*)\]',run.stdout)
assert run.returncode==0 and len(results)==len(references),(len(results),run.stdout[-5000:],run.stderr)
worst=F(0)
for out,ref in zip(results,references):
 observed=list(map(int,out.split()));assert len(observed)==len(ref)
 for x,y in zip(observed,ref):worst=max(worst,abs(x-y))
assert worst<10,worst
# A coarse scalar inverse must fail a zero-residual requirement, retaining its old result.
cmd='\n'.join(['set /contents/matrix/rows 1','set /contents/matrix/inner 1','set /contents/matrix/left-kind "dense"','set /contents/matrix/left [3000000]','set /contents/matrix/inverse-tolerance 0','set /contents/matrix/result [99]','call /contents/matrix invert','show /contents/matrix/result','quit'])+'\n'
r=subprocess.run([str(Path('build/x86_64/Windows/neo.exe').resolve()),'live','neo/packages/matrix.neo'],input=cmd,capture_output=True,text=True,timeout=20)
assert 'explicit failure' in r.stdout and 'result = [99]' in r.stdout,r.stdout
print(f'PASS: {len(results)} exact-rational inverse comparisons, max coefficient error {float(worst):.4f} raw; strict residual rejection retains result')
