"""Independent coordinate-band selection oracle in one full OS image."""
from pathlib import Path
from random import Random
import subprocess
import re
exe=str(Path('build/x86_64/Windows/neo.exe').resolve())
checkpoint='build/x86_64/Windows/slice-image.neo'
commands=[];expected=[]
for package in ['geometry','projection']:commands += [f'pkg-load neo/packages/{package}.neo','pkg install','pkg clear']
for package,name in [('geometry','g'),('projection','view')]:commands += [f'set /package-manager/id "neo.spatial.{package}"',f'set /package-manager/export-name "{package}"',f'set /package-manager/instance-name "{name}"','pkg instance']
commands += ['link /workspace/view source /workspace/g 3']
def arr(x):return '['+' '.join(map(str,x))+']'
def put(owner,name,value):commands.append(f'set /workspace/{owner}/{name} '+(arr(value) if isinstance(value,list) else str(value)))
def show(name,value):commands.append('show /workspace/view/'+name);expected.append(value)
rng=Random(5124)
for d,count in [(0,3),(3,0),(1,5),(4,6),(7,3),(257,2)]+[(rng.randrange(8),rng.randrange(7)) for _ in range(44)]:
 vertices=[rng.randrange(-3,4) for _ in range(d*count)]
 axes=rng.sample(range(d),min(3,d))
 bands=rng.sample(range(d),min(d,rng.randrange(4)))
 values=[rng.randrange(-3,4) for _ in bands];tolerances=[rng.randrange(3) for _ in bands]
 for key,value in [('dimension',d),('vertex-count',count),('vertices',vertices)]:put('g',key,value)
 for key,value in [('axes',axes),('slice-axes',bands),('slice-values',values),('slice-tolerances',tolerances)]:put('view',key,value)
 mask=[int(all(abs(vertices[i*d+a]-v)<=t for a,v,t in zip(bands,values,tolerances))) for i in range(count)]
 mapping=[i for i,visible in enumerate(mask) if visible]
 commands.append('call /workspace/view slice');expected.append([vertices[i*d+a] for i in mapping for a in axes])
 show('source-indices',mapping);show('visibility',mask)
# Boundaries inclusive and original indices retained after filtering.
for key,value in [('dimension',3),('vertex-count',5),('vertices',[10,0,5,20,1,5,30,2,5,40,3,5,50,-1,5])]:put('g',key,value)
for key,value in [('axes',[0]),('slice-axes',[1,2]),('slice-values',[1,5]),('slice-tolerances',[1,0])]:put('view',key,value)
commands.append('call /workspace/view slice');expected.append([10,20,30]);show('source-indices',[0,1,2]);show('visibility',[1,1,1,0,0])
# Source index, not a cached visible-row number, chooses the edit target.
put('view','vertex-index',2);put('view','edit',[99]);commands.append('call /workspace/view edit-slice-vertex');expected.append([99,2,5])
commands.append('call /workspace/view slice');expected.append([10,20,99]);show('source-indices',[0,1,2]);show('visibility',[1,1,1,0,0])
commands += ['save '+checkpoint,'quit']
run=subprocess.run([exe,'live','neo/neo-os/image.neo'],input='\n'.join(commands)+'\n',capture_output=True,text=True,timeout=60)
outputs=re.findall(r'(?:result=|(?:source-indices|visibility) = )\[([^\r\n]*)\]',run.stdout)
assert len(outputs)==len(expected),(len(outputs),len(expected),run.stdout[-5000:])
for i,(out,reference) in enumerate(zip(outputs,expected)):assert list(map(int,out.split()))==reference,(i,out,reference)
r=subprocess.run([exe,'live',checkpoint],input='call /workspace/view slice\nshow /workspace/view/source-indices\nquit\n',capture_output=True,text=True,timeout=20)
assert 'result=[10 20 99]' in r.stdout and 'source-indices = [0 1 2]' in r.stdout,r.stdout
print(f'PASS slice oracle: {len(expected)} packed/mapping/mask/edit results over50 shapes, dimensions0..257; boundary inclusion and full-image checkpoint/reopen')
for changes,selector,status in [
 (['set /workspace/view/slice-tolerances [-1 0]'],'slice','explicit failure'),
 (['set /workspace/view/slice-axes [1 1]'],'slice','explicit failure'),
 (['set /workspace/view/slice-axes [3 2]'],'slice','explicit failure'),
 (['set /workspace/view/slice-values [1]'],'slice','explicit failure'),
 (['set /workspace/view/slice-axes true'],'slice','wrong kind'),
 (['set /workspace/view/vertex-index 3'],'edit-slice-vertex','explicit failure'),
 (['set /workspace/view/vertex-index -1'],'edit-slice-vertex','explicit failure'),
 (['set /workspace/g/vertices [10 0 5 20 1 5 99 9 5 40 3 5 50 -1 5]'],'edit-slice-vertex','explicit failure'),
 (['set /workspace/view/slice-values [-9223372036854775808 5]'],'slice','integer overflow'),
 (['unlink /workspace/view source','link /workspace/view source /workspace/g 1'],'edit-slice-vertex','authority denied')]:
 cmds=changes+[f'call /workspace/view {selector}','show /workspace/view/result','show /workspace/view/source-indices','show /workspace/view/visibility','quit']
 r=subprocess.run([exe,'live',checkpoint],input='\n'.join(cmds)+'\n',capture_output=True,text=True,timeout=20)
 assert status in r.stdout and 'result = [10 20 99]' in r.stdout and 'source-indices = [0 1 2]' in r.stdout and 'visibility = [1 1 1 0 0]' in r.stdout,r.stdout
print('PASS shape/kind/axis/tolerance/overflow rejection, current-membership and authority checks retain published slice')
