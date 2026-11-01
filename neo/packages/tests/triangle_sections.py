"""Independent exact-rational triangle section oracle in one live image."""
from fractions import Fraction as F
from pathlib import Path
from random import Random
import re
import subprocess
exe=str(Path('build/x86_64/Windows/neo.exe').resolve())
checkpoint='build/x86_64/Windows/triangle-section-image.neo'
commands=[];cases=[]
for package in ['geometry','section']:commands += [f'pkg-load neo/packages/{package}.neo','pkg install','pkg clear']
for package,export,name in [('geometry','geometry','g'),('section','triangle-section','faces')]:commands += [f'set /package-manager/id "neo.spatial.{package}"',f'set /package-manager/export-name "{export}"',f'set /package-manager/instance-name "{name}"','pkg instance']
commands+=['link /workspace/faces source /workspace/g 1']
def arr(x):return '['+' '.join(map(str,x))+']'
def put(owner,k,x):commands.append(f'set /workspace/{owner}/{k} '+(arr(x) if isinstance(x,list) else str(x)))
def nearest(x):
 a,b=divmod(abs(x.numerator),x.denominator)
 return (-1 if x<0 else 1)*(a+(2*b>=x.denominator))
def rank(points):
 u=[b-a for a,b in zip(points[0],points[1])];v=[b-a for a,b in zip(points[0],points[2])]
 uu=sum(x*x for x in u);vv=sum(x*x for x in v);uv=sum(x*y for x,y in zip(u,v))
 return 2 if uu*vv!=uv*uv else (1 if uu or vv else 0)
def contacts(points,axis,plane):
 r=rank(points)
 if r==0:return (r,[tuple(map(F,points[0]))] if points[0][axis]==plane else [])
 if r==1:
  p,q=min(points),max(points)
  if p[axis]==q[axis]==plane:return r,[tuple(map(F,p)),tuple(map(F,q))]
  if p[axis]==plane:return r,[tuple(map(F,p))]
  if q[axis]==plane:return r,[tuple(map(F,q))]
  if min(p[axis],q[axis])<plane<max(p[axis],q[axis]):
   t=F(plane-p[axis],q[axis]-p[axis]);return r,[tuple(F(a)+(b-a)*t for a,b in zip(p,q))]
  return r,[]
 result=[tuple(map(F,p)) for p in points if p[axis]==plane]
 for p,q in zip(points,points[1:]+points[:1]):
  if min(p[axis],q[axis])<plane<max(p[axis],q[axis]):
   t=F(plane-p[axis],q[axis]-p[axis]);result.append(tuple(F(a)+(b-a)*t for a,b in zip(p,q)))
 return r,result
fields=['result-points','result-segments','result-triangles','source-triangles','source-vertices','source-edge-vertices','parameter-numerators','parameter-denominators','segment-sources','triangle-sources','triangle-ranks','triangle-kinds','segment-degenerate','triangle-degenerate']
def case(verts,tris,axis=0,plane=0):
 d=len(verts[0]);cases.append((verts,tris,axis,plane))
 for k,x in [('dimension',d),('vertex-count',len(verts)),('vertices',[x for v in verts for x in v]),('triangles',[x for t in tris for x in t])]:put('g',k,x)
 put('faces','plane-axis',axis);put('faces','plane-value',plane);commands.append('call /workspace/faces intersect')
 for k in fields:commands.append('show /workspace/faces/'+k)
# All sign/on-plane patterns, coplanar face, degeneracy, and rounding collapse.
for xs in [(-2,2,0),(-2,2,2),(0,0,2),(0,2,2),(2,2,2),(0,0,0),(-2,-2,-2)]:case([[xs[0],0,0],[xs[1],4,0],[xs[2],0,4]],[[0,1,2]])
case([[0,0],[0,2],[0,4]],[[0,1,2],[2,1,0],[1,1,1]])
case([[-2,0],[2,0],[0,0]],[[0,1,2]])
case([[0,1],[0,1],[0,1]],[[0,1,2]])
case([[-100,0],[1,0],[1,1]],[[0,1,2]]) # distinct exact contacts can round together
rng=Random(31918)
for d in [1,2,3,4,7,33,100,257]+[rng.randrange(1,8) for _ in range(32)]:
 verts=[[rng.randrange(-4,5) for _ in range(d)] for i in range(3)]
 if d<33 and rng.randrange(3)==0:verts[2]=[2*b-a for a,b in zip(verts[0],verts[1])]
 case(verts,[[0,1,2]],rng.randrange(d),rng.randrange(-2,3))
# Save a simple coplanar face to verify topology after reopening.
case([[0,0,0],[0,4,0],[0,0,4]],[[0,1,2]])
commands+=['save '+checkpoint,'quit']
run=subprocess.run([exe,'live','neo/neo-os/image.neo'],input='\n'.join(commands)+'\n',capture_output=True,text=True,timeout=90)
outputs=re.findall(r'(?:result=|(?:'+'|'.join(fields)+r') = )\[([^\r\n]*)\]',run.stdout)
assert len(outputs)==len(cases)*(len(fields)+1),(len(outputs),len(cases),run.stdout[-5000:])
for ci,(verts,tris,axis,plane) in enumerate(cases):
 group=outputs[ci*(len(fields)+1):(ci+1)*(len(fields)+1)];data={k:list(map(int,out.split())) for k,out in zip(['result']+fields,group)}
 d=len(verts[0]);coords=[tuple(data['result'][i:i+d]) for i in range(0,len(data['result']),d)]
 ideal=[contacts([verts[v] for v in t],axis,plane) for t in tris]
 assert data['triangle-ranks']==[r for r,c in ideal],(ci,data,ideal)
 assert data['triangle-kinds']==[len(c) for r,c in ideal],(ci,data,ideal)
 for ti,(r,c) in enumerate(ideal):
  observed=[coord for coord,owner in zip(coords,data['source-triangles']) if owner==ti]
  assert sorted(observed)==sorted(tuple(nearest(x) for x in p) for p in c),(ci,observed,c)
 for j,p in enumerate(coords):
  owner=data['source-triangles'][j];a,b=data['source-edge-vertices'][2*j:2*j+2]
  assert a in tris[owner] and b in tris[owner]
  t=F(data['parameter-numerators'][j],data['parameter-denominators'][j]);assert 0<=t<=1
  exact=[F(x)+(y-x)*t for x,y in zip(verts[a],verts[b])]
  assert p==tuple(nearest(x) for x in exact) and p[axis]==plane,(ci,j,p,exact)
  endpoint=data['source-vertices'][j]
  if endpoint>=0:assert tuple(verts[endpoint])==p and a==b==endpoint
 for row,owner in enumerate(data['segment-sources']):
  a,b=data['result-segments'][2*row:2*row+2]
  assert data['triangle-kinds'][owner]==2 and data['source-triangles'][a]==owner and data['source-triangles'][b]==owner
  assert data['segment-degenerate'][row]==int(coords[a]==coords[b])
 for row,owner in enumerate(data['triangle-sources']):
  indices=data['result-triangles'][3*row:3*row+3]
  assert data['triangle-kinds'][owner]==3 and [coords[i] for i in indices]==[tuple(verts[i]) for i in tris[owner]]
  assert data['triangle-degenerate'][row]==0
 assert len(data['result-points'])==sum(len(c)==1 for r,c in ideal)
r=subprocess.run([exe,'live',checkpoint],input='call /workspace/faces intersect\nshow /workspace/faces/result-triangles\nquit\n',capture_output=True,text=True,timeout=20)
assert 'result-triangles = [0 1 2]' in r.stdout,r.stdout
print(f'PASS triangle rational oracle: {len(cases)} cases / {len(outputs)} output arrays through257 dimensions, rank/topology/provenance/rounding collapse and full-image save/reopen')
for changes,status in [
 (['set /workspace/faces/plane-axis 3'],'explicit failure'),
 (['set /workspace/g/triangles [0 1]'],'explicit failure'),
 (['set /workspace/g/triangles [0 1 3]'],'explicit failure'),
 (['set /workspace/g/triangles true'],'wrong kind'),
 (['set /workspace/g/vertices [0 0 0 0 4000000000 0 0 0 4000000000]'],'integer overflow'),
 (['unlink /workspace/faces source'],'unavailable')]:
 r=subprocess.run([exe,'live',checkpoint],input='\n'.join(changes+['call /workspace/faces intersect','show /workspace/faces/result-triangles','show /workspace/faces/triangle-ranks','quit'])+'\n',capture_output=True,text=True,timeout=20)
 assert status in r.stdout and 'result-triangles = [0 1 2]' in r.stdout and 'triangle-ranks = [2]' in r.stdout,r.stdout
print('PASS invalid axis/topology/kind/rank overflow/missing source retain published output')
