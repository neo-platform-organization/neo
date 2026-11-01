"""Independent homogeneous projection/clipping oracle in one image."""
from fractions import Fraction as F
from pathlib import Path
from random import Random
import re
import subprocess
exe=str(Path('build/x86_64/Windows/neo.exe').resolve())
checkpoint='build/x86_64/Windows/perspective-image.neo'
commands=[];expected=[]
for package in ['geometry','perspective']:commands += [f'pkg-load neo/packages/{package}.neo','pkg install','pkg clear']
for package,export,name in [('geometry','geometry','g'),('perspective','view','view')]:commands += [f'set /package-manager/id "neo.spatial.{package}"',f'set /package-manager/export-name "{export}"',f'set /package-manager/instance-name "{name}"','pkg instance']
commands+=['link /workspace/view source /workspace/g 1']
def arr(x):return '['+' '.join(map(str,x))+']'
def put(owner,k,x):commands.append(f'set /workspace/{owner}/{k} '+(arr(x) if isinstance(x,list) else str(x)))
def nearest(x):
 a,b=divmod(abs(x.numerator),x.denominator)
 return (-1 if x<0 else 1)*(a+(2*b>=x.denominator))
fields=['homogeneous','denominators','visibility','clip-codes','source-indices']
def case(n,m,vertices,matrix,offset,near=1,far=0,clip=True):
 count=len(vertices)//n if n else 3
 for k,x in [('dimension',n),('vertex-count',count),('vertices',vertices)]:put('g',k,x)
 for k,x in [('view-dimension',m),('map',matrix),('offset',offset),('near',near),('far',far),('clip-volume','true' if clip else 'false')]:put('view',k,x)
 h=[nearest(F(sum(matrix[j*n+k]*vertices[i*n+k] for k in range(n)),1000000))+offset[j] for i in range(count) for j in range(m+1)]
 den=[h[i*(m+1)+m] for i in range(count)];codes=[]
 for i,w in enumerate(den):codes.append(1 if w<near else 2 if far and w>far else 3 if clip and any(abs(h[i*(m+1)+j])>w for j in range(m)) else 0)
 mask=[int(c==0) for c in codes];mapping=[i for i,c in enumerate(codes) if not c]
 result=[nearest(F(h[i*(m+1)+j]*1000000,den[i])) for i in mapping for j in range(m)]
 commands.append('call /workspace/view project');expected.append(result)
 for name,value in zip(fields,[h,den,mask,codes,mapping]):commands.append('show /workspace/view/'+name);expected.append(value)
rng=Random(51182)
for n,m in [(0,0),(0,2),(3,0),(1,1),(7,3),(257,2)]+[(rng.randrange(8),rng.randrange(4)) for _ in range(44)]:
 verts=[rng.randrange(-5,6) for _ in range(2*n)]
 matrix=[rng.randrange(-2000000,2000001) for _ in range((m+1)*n)] if n<100 else [0]*((m+1)*n)
 offset=[rng.randrange(-4,5) for _ in range(m+1)]
 case(n,m,verts,matrix,offset,near=rng.randrange(1,4),far=rng.choice([0,5]),clip=bool(rng.randrange(2)))
# Signed half ties after division; canonical boundaries inclusive and denominator bands.
case(1,1,[-1,1],[1000000,0],[0,2000000])
case(3,2,[1000000,0,2000000,0,0,0,0,0,-1000000,0,0,5000000,3000000,0,2000000,-2000000,2000000,2000000,1000000,0,1000000,0,0,4000000],
 [1000000,0,0,0,1000000,0,0,0,1000000],[0,0,0],1000000,4000000)
commands+=['save '+checkpoint,'quit']
run=subprocess.run([exe,'live','neo/neo-os/image.neo'],input='\n'.join(commands)+'\n',capture_output=True,text=True,timeout=60)
outputs=re.findall(r'(?:result=|(?:'+'|'.join(fields)+r') = )\[([^\r\n]*)\]',run.stdout)
assert len(outputs)==len(expected),(len(outputs),len(expected),run.stdout[-5000:])
for i,(out,ref) in enumerate(zip(outputs,expected)):assert list(map(int,out.split()))==ref,(i,out,ref)
r=subprocess.run([exe,'live',checkpoint],input='call /workspace/view project\nshow /workspace/view/clip-codes\nquit\n',capture_output=True,text=True,timeout=20)
assert 'clip-codes = [0 1 1 2 3 0 0 0]' in r.stdout,r.stdout
print(f'PASS perspective rational oracle: {len(expected)} outputs /52 shapes through257 dimensions, signed ties/boundaries/visibility and full-image save/reopen')
for changes,status in [
 (['set /workspace/view/near 0'],'explicit failure'),
 (['set /workspace/view/far 1'],'explicit failure'),
 (['set /workspace/view/map []'],'explicit failure'),
 (['set /workspace/view/offset true'],'wrong kind'),
 (['set /workspace/g/vertices []'],'explicit failure'),
 (['set /workspace/view/map [9223372036854775807 0 0 0 1000000 0 0 0 1000000]'],'integer overflow'),
 (['set /workspace/view/offset [9223372036854775807 0 0]'],'integer overflow'),
 (['unlink /workspace/view source'],'unavailable')]:
 r=subprocess.run([exe,'live',checkpoint],input='\n'.join(changes+['call /workspace/view project','show /workspace/view/clip-codes','show /workspace/view/source-indices','quit'])+'\n',capture_output=True,text=True,timeout=20)
 assert status in r.stdout and 'clip-codes = [0 1 1 2 3 0 0 0]' in r.stdout and 'source-indices = [0 5 6 7]' in r.stdout,r.stdout
print('PASS invalid shape/bounds/kind/overflow/missing source retain published projection and visibility')
