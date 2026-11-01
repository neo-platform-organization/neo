"""Independent unbounded-integer Gram determinant oracle; optional Python tooling."""
from pathlib import Path
from random import Random
import re
import subprocess
exe=str(Path('build/x86_64/Windows/neo.exe').resolve())
base='/contents/geometry/'
commands=[];expected=[]
def arr(x):return '['+' '.join(map(str,x))+']'
def setv(k,x):commands.append('set '+base+k+' '+(arr(x) if isinstance(x,list) else str(x)))
def show(k,x):commands.append('show '+base+k);expected.append(x)
rng=Random(5510)
for d in [0,1,2,3,7,33,100,257]+[rng.randrange(8) for _ in range(40)]:
 span=2 if d>=100 else 2000000
 verts=[[rng.randint(-span,span) for _ in range(d)] for i in range(4)]
 if rng.randrange(2):verts[2]=[2*y-x for x,y in zip(verts[0],verts[1])]
 verts[3]=verts[0][:]
 setv('dimension',d);setv('vertex-count',4);setv('vertices',[x for v in verts for x in v]);setv('points',[0,3,0]);setv('segments',[0,1,0,3]);setv('triangles',[0,1,2])
 commands.append('call '+base[:-1]+' validate')
 show('segment-degenerate',[int(verts[0]==verts[1]),1])
 u=[y-x for x,y in zip(verts[0],verts[1])];v=[y-x for x,y in zip(verts[0],verts[2])]
 uu=sum(x*x for x in u);vv=sum(x*x for x in v);uv=sum(x*y for x,y in zip(u,v))
 show('triangle-degenerate',[int(uu*vv-uv*uv==0)])
 setv('vertex-index',2);commands.append('call '+base[:-1]+' vertex');expected.append(verts[2])
run=subprocess.run([exe,'live','neo/packages/geometry.neo'],input='\n'.join(commands+['quit'])+'\n',capture_output=True,text=True,timeout=60)
outputs=re.findall(r'(?:result=|(?:segment-degenerate|triangle-degenerate) = )\[([^\r\n]*)\]',run.stdout)
assert len(outputs)==len(expected),(len(outputs),len(expected),run.stdout[-5000:])
for i,(a,b) in enumerate(zip(outputs,expected)):assert list(map(int,a.split()))==b,(i,a,b)
print(f'PASS geometry oracle: {len(expected)} degeneracy/extraction outputs; 48 shapes, dimensions0..257')
prefix=['set '+base+'dimension 2','set '+base+'vertex-count 3','set '+base+'vertices [0 0 1 0 0 1]',
 'set '+base+'points [0]','set '+base+'segments [0 1]','set '+base+'triangles [0 1 2]','call '+base[:-1]+' validate']
# The raw-unit triangle above must be nondegenerate, without scalar quantization.
failures=[('dimension -1','explicit failure'),('vertex-count -1','explicit failure'),('vertices [0 0]','explicit failure'),
 ('segments [0]','explicit failure'),('triangles [0 1]','explicit failure'),('points [-1]','explicit failure'),
 ('segments [0 3]','explicit failure'),('triangles [0 1 3]','explicit failure'),('points true','wrong kind'),
 ('vertices [-9223372036854775808 0 1 0 0 1]','integer overflow'),
 ('vertices [0 0 4000000000 0 0 4000000000]','integer overflow'),('dimension 9223372036854775807','integer overflow')]
for value,status in failures:
 r=subprocess.run([exe,'live','neo/packages/geometry.neo'],input='\n'.join(prefix+['set '+base+value,'call '+base[:-1]+' validate','show '+base+'valid','show '+base+'triangle-degenerate','quit'])+'\n',capture_output=True,text=True,timeout=20)
 assert status in r.stdout and 'valid = false' in r.stdout and 'triangle-degenerate = [0]' in r.stdout,r.stdout
print('PASS tiny triangle and shape/kind/index/overflow failures with invalidated flag and retained report')


r=subprocess.run([exe,'live','neo/packages/geometry.neo'],input='\n'.join(prefix+[
 'add /contents (frame (dimension 2))','link '+base[:-1]+' frame /contents/frame 1',
 'call '+base[:-1]+' validate-frame','set /contents/frame/dimension 3',
 'call '+base[:-1]+' validate-frame','set '+base+'vertex-count 1000000',
 'call '+base[:-1]+' initialize','quit'])+'\n',capture_output=True,text=True,timeout=20)
assert 'explicit failure' in r.stdout and 'resource or execution limit' in r.stdout,r.stdout
print('PASS explicit frame-dimension validation and bounded construction')
