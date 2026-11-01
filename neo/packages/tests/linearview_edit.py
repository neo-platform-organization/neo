"""Independent affine projection and explicit-lift edit checks in one OS image."""
from pathlib import Path
from random import Random
import re
import subprocess
exe=str(Path('build/x86_64/Windows/neo.exe').resolve())
checkpoint='build/x86_64/Windows/linearview-image.neo'
commands=[];expected=[]
for package in ['geometry','linearview']:commands += [f'pkg-load neo/packages/{package}.neo','pkg install','pkg clear']
for package,export,name in [('geometry','geometry','g'),('linearview','view','view')]:commands += [f'set /package-manager/id "neo.spatial.{package}"',f'set /package-manager/export-name "{export}"',f'set /package-manager/instance-name "{name}"','pkg instance']
commands+=['link /workspace/view source /workspace/g 3']
def arr(x):return '['+' '.join(map(str,x))+']'
def put(owner,k,x):commands.append(f'set /workspace/{owner}/{k} '+(arr(x) if isinstance(x,list) else str(x)))
def rnd(x):
 a,b=divmod(abs(x),1000000)
 return (-1 if x<0 else 1)*(a+(2*b>=1000000))
def project(vertices,n,m,map,t):return [rnd(sum(map[j*n+k]*vertices[i*n+k] for k in range(n)))+t[j] for i in range(2) for j in range(m)]
rng=Random(61028)
for n,m in [(0,0),(0,2),(4,0),(3,5),(7,3)]+[(rng.randrange(8),rng.randrange(5)) for _ in range(35)]:
 vertices=[rng.randrange(-20,21) for _ in range(2*n)];matrix=[rng.randrange(-2000000,2000001) for _ in range(m*n)];t=[rng.randrange(-20,21) for _ in range(m)]
 for k,x in [('dimension',n),('vertex-count',2),('vertices',vertices)]:put('g',k,x)
 for k,x in [('view-dimension',m),('map',matrix),('translation',t)]:put('view',k,x)
 commands.append('call /workspace/view project');expected.append(project(vertices,n,m,matrix,t))
for n in [0,1,2,3,4,7]*5:
 m=rng.randrange(min(3,n)+1);matrix=[0]*(m*n);lift=[0]*(n*m)
 for j in range(m):
  matrix[j*n+j]=1000000;lift[j*m+j]=1000000
  for k in range(m,n):matrix[j*n+k]=rng.randrange(-2,3)*1000000
 vertices=[rng.randrange(-9,10) for _ in range(2*n)];t=[rng.randrange(-9,10) for _ in range(m)]
 for k,x in [('dimension',n),('vertex-count',2),('vertices',vertices)]:put('g',k,x)
 for k,x in [('view-dimension',m),('map',matrix),('translation',t),('lift',lift),('edit-tolerance',0),('edit-enabled','true'),('vertex-index',1)]:put('view',k,x)
 target=[rng.randrange(-99,100) for _ in range(m)];put('view','edit',target)
 old=project(vertices,n,m,matrix,t)[m:];candidate=vertices[n:]
 for j in range(m):candidate[j]+=target[j]-old[j]
 commands.append('call /workspace/view edit-vertex');expected.append(candidate[:])
 commands.append('show /workspace/g/vertices');expected.append(vertices[:n]+candidate)
 commands.append('call /workspace/view project');expected.append(project(vertices[:n]+candidate,n,m,matrix,t))
# The caller's two valid lift choices preserve different complementary coordinates.
for lift,answer in [([1000000,0],[6,2]),([0,1000000],[1,7])]:
 for k,x in [('dimension',2),('vertex-count',1),('vertices',[1,2])]:put('g',k,x)
 for k,x in [('view-dimension',1),('map',[1000000,1000000]),('translation',[0]),('lift',lift),('edit',[8]),('vertex-index',0)]:put('view',k,x)
 commands.append('call /workspace/view edit-vertex');expected.append(answer)
# Stable saved fixture: shear-like 3D->2D map, preserving the selected hidden coordinate.
for k,x in [('dimension',3),('vertex-count',1),('vertices',[1000000,2000000,5000000])]:put('g',k,x)
for k,x in [('view-dimension',2),('map',[1000000,1000000,0,0,1000000,1000000]),('translation',[3000000,4000000]),('lift',[1000000,-1000000,0,1000000,0,0]),('edit',[9000000,8000000]),('vertex-index',0)]:put('view',k,x)
commands.append('call /workspace/view edit-vertex');expected.append([7000000,-1000000,5000000])
commands.append('call /workspace/view project');expected.append([9000000,8000000])
commands+=['save '+checkpoint,'quit']
run=subprocess.run([exe,'live','neo/neo-os/image.neo'],input='\n'.join(commands)+'\n',capture_output=True,text=True,timeout=60)
outputs=re.findall(r'(?:result=|vertices = )\[([^\r\n]*)\]',run.stdout)
assert len(outputs)==len(expected),(len(outputs),len(expected),run.stdout[-5000:])
for i,(out,ref) in enumerate(zip(outputs,expected)):assert list(map(int,out.split()))==ref,(i,out,ref)
r=subprocess.run([exe,'live',checkpoint],input='call /workspace/view project\nshow /workspace/g/vertices\nquit\n',capture_output=True,text=True,timeout=20)
assert 'result=[9000000 8000000]' in r.stdout and 'vertices = [7000000 -1000000 5000000]' in r.stdout,r.stdout
print(f'PASS affine view oracle: {len(expected)} projection/edit/source outputs; 40 forward maps,30 constrained edits, two explicit lift choices and full-image save/reopen')
for changes,status in [
 (['set /workspace/view/edit-enabled false'],'explicit failure'),
 (['set /workspace/view/lift [0 0 0 0 0 0]'],'explicit failure'),
 (['set /workspace/view/map [1000000 1000000 0 0 0 0]'],'explicit failure'),
 (['set /workspace/view/translation [0]'],'explicit failure'),
 (['set /workspace/view/edit [1]'],'explicit failure'),
 (['set /workspace/view/edit-tolerance -1'],'explicit failure'),
 (['set /workspace/view/vertex-index 1'],'explicit failure'),
 (['unlink /workspace/view source','link /workspace/view source /workspace/g 1'],'authority denied')]:
 r=subprocess.run([exe,'live',checkpoint],input='\n'.join(changes+['call /workspace/view edit-vertex','show /workspace/g/vertices','quit'])+'\n',capture_output=True,text=True,timeout=20)
 assert status in r.stdout and 'vertices = [7000000 -1000000 5000000]' in r.stdout,r.stdout
# A mathematically valid lift can still fail the final quantized reprojection check.
cmds=['set /workspace/g/dimension 1','set /workspace/g/vertices [0]','set /workspace/view/view-dimension 1','set /workspace/view/map [2000000]','set /workspace/view/translation [0]','set /workspace/view/lift [500000]','set /workspace/view/edit [1]','call /workspace/view validate-lift','call /workspace/view edit-vertex','show /workspace/g/vertices','quit']
r=subprocess.run([exe,'live',checkpoint],input='\n'.join(cmds)+'\n',capture_output=True,text=True,timeout=20)
assert 'result=true' in r.stdout and 'explicit failure' in r.stdout and 'vertices = [0]' in r.stdout,r.stdout
print('PASS missing/invalid edit policy, shape/rank/index/authority and final precision rejection retain source')

cmds=['set /workspace/view/map [9223372036854775807 0 0 0 1000000 1000000]',
 'call /workspace/view project','show /workspace/view/result','show /workspace/g/vertices','quit']
r=subprocess.run([exe,'live',checkpoint],input='\n'.join(cmds)+'\n',capture_output=True,text=True,timeout=20)
assert 'integer overflow' in r.stdout and 'result = [9000000 8000000]' in r.stdout and 'vertices = [7000000 -1000000 5000000]' in r.stdout,r.stdout
cmds=['set /workspace/view/view-dimension 4','set /workspace/view/map [1000000 0 0 0 1000000 0 0 0 1000000 1000000 0 0]',
 'set /workspace/view/translation [0 0 0 0]','call /workspace/view validate-lift','quit']
r=subprocess.run([exe,'live',checkpoint],input='\n'.join(cmds)+'\n',capture_output=True,text=True,timeout=20)
assert 'explicit failure' in r.stdout,r.stdout
print('PASS checked projection overflow retains outputs/source; overcomplete edit constraint rejected')
