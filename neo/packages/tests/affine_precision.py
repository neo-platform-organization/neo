"""Independent integer affine oracle; run from repository root."""
from pathlib import Path
from random import Random
import re
import subprocess
Q=1000000
rng=Random(3301)
commands=[]
expected=[]
def array(a):return '['+' '.join(map(str,a))+']'
def rounded(x):
 a,b=divmod(abs(x),Q)
 return (-1 if x<0 else 1)*(a+(2*b>=Q))
def setv(k,v):commands.append('set /contents/matrix/'+k+' '+(array(v) if isinstance(v,list) else str(v)))
def call(k,result):commands.append('call /contents/matrix '+k);expected.append(result)
def show(k,result):commands.append('show /contents/matrix/'+k);expected.append(result)
for m,k,n in [(0,0,0),(2,0,3),(0,3,4),(3,2,0),(7,7,7)]+[(rng.randrange(5),rng.randrange(5),rng.randrange(5)) for _ in range(40)]:
 a=[rng.randint(-2*Q,2*Q) for _ in range(m*k)];b=[rng.randint(-2*Q,2*Q) for _ in range(k*n)]
 t=[rng.randint(-2*Q,2*Q) for _ in range(m)];u=[rng.randint(-2*Q,2*Q) for _ in range(k)];x=[rng.randint(-2*Q,2*Q) for _ in range(k)]
 for key,value in [('rows',m),('inner',k),('columns',n),('left-kind','"dense"'),('right-kind','"dense"'),('left',a),('right',b),('input',x),('left-translation',t),('right-translation',u)]:setv(key,value)
 apply=[rounded(sum(a[i*k+j]*x[j] for j in range(k))) for i in range(m)]
 call('point',[v+t[i] for i,v in enumerate(apply)])
 call('apply',apply)
 call('affine-compose',[rounded(sum(a[i*k+j]*b[j*n+c] for j in range(k))) for i in range(m) for c in range(n)])
 show('result-translation',[rounded(sum(a[i*k+j]*u[j] for j in range(k)))+t[i] for i in range(m)])
# Exact inverses of permutation/diagonal maps exercise translation and row swaps.
for d in range(5):
 for sample in range(4):
  perm=list(range(d));rng.shuffle(perm)
  scales=[rng.choice([-4,-2,1,2,4]) for _ in range(d)]
  a=[0]*(d*d);inv=[0]*(d*d);t=[rng.randint(-10,10)*Q for _ in range(d)]
  for i,j in enumerate(perm):a[i*d+j]=scales[i]*Q;inv[j*d+i]=Q//scales[i]
  for key,value in [('rows',d),('inner',d),('left',a),('left-translation',t)]:setv(key,value)
  call('affine-invert',inv)
  show('result-translation',[-sum(inv[i*d+j]*t[j] for j in range(d))//Q for i in range(d)])
run=subprocess.run([str(Path('build/x86_64/Windows/neo.exe').resolve()),'live','neo/packages/matrix.neo'],input='\n'.join(commands+['quit'])+'\n',capture_output=True,text=True,timeout=60)
observed=re.findall(r'(?:result=|result-translation = )\[([^\r\n]*)\]',run.stdout)
assert run.returncode==0 and len(observed)==len(expected),(len(observed),len(expected),run.stdout[-5000:],run.stderr)
for i,(a,b) in enumerate(zip(observed,expected)):assert list(map(int,a.split()))==b,(i,a,b)
print(f'PASS affine integer oracle: {len(expected)} point/displacement/composition/inverse outputs, dimensions 0..7 and rectangular/empty shapes')

# Failure checks exercise overflow, malformed translations, and affine precision.
failures=[
 ('point', 'identity', [], [1], [9223372036854775807], [0], 'integer overflow'),
 ('point', 'identity', [], [], [0], [0], 'explicit failure'),
 ('affine-compose', 'identity', [], [9223372036854775807], [0], [1], 'integer overflow'),
 ('affine-invert', 'identity', [], [-9223372036854775808], [0], [0], 'integer overflow'),
 ('affine-invert', 'dense', [3000000], [100000000], [0], [0], 'explicit failure')]
for selector,kind,left,t,x,u,failure in failures:
 cmds=['set /contents/matrix/rows 1','set /contents/matrix/inner 1','set /contents/matrix/columns 1',
       f'set /contents/matrix/left-kind "{kind}"','set /contents/matrix/right-kind "identity"',
       'set /contents/matrix/left '+array(left),'set /contents/matrix/right []',
       'set /contents/matrix/left-translation '+array(t),'set /contents/matrix/right-translation '+array(u),
       'set /contents/matrix/input '+array(x),'set /contents/matrix/result [-91]',
       'set /contents/matrix/result-translation [-92]',f'call /contents/matrix {selector}',
       'show /contents/matrix/result','show /contents/matrix/result-translation','quit']
 r=subprocess.run([str(Path('build/x86_64/Windows/neo.exe').resolve()),'live','neo/packages/matrix.neo'],input='\n'.join(cmds)+'\n',capture_output=True,text=True,timeout=20)
 assert failure in r.stdout and 'result = [-91]' in r.stdout and 'result-translation = [-92]' in r.stdout,r.stdout
print('PASS affine overflow/shape/precision rejection retains both published pieces')
