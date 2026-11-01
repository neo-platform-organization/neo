"""Exact-rational segment/plane oracle in one OS image."""
from fractions import Fraction as F
from pathlib import Path
from random import Random
import re
import subprocess
exe=str(Path('build/x86_64/Windows/neo.exe').resolve())
checkpoint='build/x86_64/Windows/section-image.neo'
commands=[];expected=[]
for package in ['geometry','section']:commands += [f'pkg-load neo/packages/{package}.neo','pkg install','pkg clear']
for package,name in [('geometry','g'),('section','section')]:commands += [f'set /package-manager/id "neo.spatial.{package}"',f'set /package-manager/export-name "{package}"',f'set /package-manager/instance-name "{name}"','pkg instance']
commands+=['link /workspace/section source /workspace/g 1']
def arr(x):return '['+' '.join(map(str,x))+']'
def put(owner,k,x):commands.append(f'set /workspace/{owner}/{k} '+(arr(x) if isinstance(x,list) else str(x)))
def nearest(x):
 sign=-1 if x<0 else 1
 a,b=divmod(abs(x.numerator),x.denominator)
 return sign*(a+(2*b>=x.denominator))
rng=Random(8892)
fields=['result-points','result-segments','source-segments','source-vertices','parameter-numerators','parameter-denominators','segment-sources','segment-kinds']
for d in [1,2,3,4,7,33,100,257]+[rng.randrange(1,8) for _ in range(32)]:
 verts=[[rng.randrange(-4,5) for _ in range(d)] for i in range(4)]
 plane_axis=rng.randrange(d);plane=rng.randrange(-2,3)
 segments=[(0,1)] if d>=100 else [(0,1),(1,0),(0,2),(2,3),(1,3),(0,0)]
 data={k:[] for k in fields};output=[]
 for si,(a,b) in enumerate(segments):
  av,bv=verts[a][plane_axis],verts[b][plane_axis]
  if av==bv==plane:
   contacts=[(F(0),a)] if verts[a]==verts[b] else [(F(0),a),(F(1),b)]
  elif av==plane:contacts=[(F(0),a)]
  elif bv==plane:contacts=[(F(1),b)]
  elif min(av,bv)<plane<max(av,bv):contacts=[(F(plane-av,bv-av),-1)]
  else:contacts=[]
  data['segment-kinds'].append(len(contacts))
  start=len(output)//d
  if len(contacts)==1:data['result-points'].append(start)
  if len(contacts)==2:data['result-segments'] += [start,start+1];data['segment-sources'].append(si)
  for t,endpoint in contacts:
   output += [nearest(F(x)+(F(y)-x)*t) for x,y in zip(verts[a],verts[b])]
   data['source-segments'].append(si);data['source-vertices'].append(endpoint)
   tn,td=(plane-av,bv-av) if endpoint<0 else ((0,1) if endpoint==a else (1,1))
   data['parameter-numerators'].append(tn);data['parameter-denominators'].append(td)
 for k,x in [('dimension',d),('vertex-count',4),('vertices',[x for row in verts for x in row]),('segments',[x for pair in segments for x in pair])]:put('g',k,x)
 put('section','plane-axis',plane_axis);put('section','plane-value',plane)
 commands.append('call /workspace/section intersect');expected.append(output)
 for k in fields:commands.append('show /workspace/section/'+k);expected.append(data[k])
# Negative half tie must round the final coordinate away from zero.
for k,x in [('dimension',2),('vertex-count',2),('vertices',[-1,-1,1,0]),('segments',[0,1])]:put('g',k,x)
put('section','plane-axis',0);put('section','plane-value',0);commands.append('call /workspace/section intersect');expected.append([0,-1])
commands+=['save '+checkpoint,'quit']
run=subprocess.run([exe,'live','neo/neo-os/image.neo'],input='\n'.join(commands)+'\n',capture_output=True,text=True,timeout=60)
outputs=re.findall(r'(?:result=|(?:'+'|'.join(fields)+r') = )\[([^\r\n]*)\]',run.stdout)
assert len(outputs)==len(expected),(len(outputs),len(expected),run.stdout[-5000:])
for i,(out,ref) in enumerate(zip(outputs,expected)):assert list(map(int,out.split()))==ref,(i,out,ref)
r=subprocess.run([exe,'live',checkpoint],input='call /workspace/section intersect\nshow /workspace/g/vertices\nquit\n',capture_output=True,text=True,timeout=20)
assert 'result=[0 -1]' in r.stdout and 'vertices = [-1 -1 1 0]' in r.stdout,r.stdout
print(f'PASS section rational oracle: {len(expected)} geometry/topology/provenance outputs over40 shapes through257 dimensions; signed ties and full-image save/reopen')
for changes,status in [
 (['set /workspace/section/plane-axis -1'],'explicit failure'),
 (['set /workspace/section/plane-axis 2'],'explicit failure'),
 (['set /workspace/g/segments [0]'],'explicit failure'),
 (['set /workspace/g/segments [0 2]'],'explicit failure'),
 (['set /workspace/g/vertices true'],'wrong kind'),
 (['set /workspace/g/vertices [-9223372036854775808 0 9223372036854775807 1]'],'integer overflow'),
 (['set /workspace/g/vertices [-2 9223372036854775807 2 9223372036854775807]'],'integer overflow'),
 (['unlink /workspace/section source'],'unavailable')]:
 r=subprocess.run([exe,'live',checkpoint],input='\n'.join(changes+['call /workspace/section intersect','show /workspace/section/result','show /workspace/section/segment-kinds','quit'])+'\n',capture_output=True,text=True,timeout=20)
 assert status in r.stdout and 'result = [0 -1]' in r.stdout and 'segment-kinds = [1]' in r.stdout,r.stdout
print('PASS source/axis/index/kind/overflow failures retain previously published section')
