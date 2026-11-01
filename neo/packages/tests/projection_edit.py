"""Single-image projection/edit oracle and save/reopen checks."""
from pathlib import Path
from random import Random
import re
import subprocess
exe=str(Path('build/x86_64/Windows/neo.exe').resolve())
checkpoint='build/x86_64/Windows/projection-oracle.neo'
commands=[];expected=[]
for package in ['geometry','projection']:
 commands+=['pkg-load neo/packages/'+package+'.neo','pkg install','pkg clear']
for package,export,name in [('geometry','geometry','g'),('projection','projection','view'),('projection','projection','other')]:
 commands += [f'set /package-manager/id "neo.spatial.{package}"',f'set /package-manager/export-name "{export}"',f'set /package-manager/instance-name "{name}"','pkg instance']
commands+=['link /workspace/view source /workspace/g 3','link /workspace/other source /workspace/g 1']
def arr(x):return '['+' '.join(map(str,x))+']'
def setv(path,value):commands.append('set '+path+' '+(arr(value) if isinstance(value,list) else str(value)))
rng=Random(9120)
for d in [0,1,2,4,7,33,100,257]+[rng.randrange(8) for _ in range(40)]:
 count=2
 vertices=[rng.randrange(-9,10) for _ in range(count*d)]
 axes=rng.sample(range(d),min(d,rng.randrange(4)))
 other=list(reversed(axes))
 for path,value in [('g/dimension',d),('g/vertex-count',count),('g/vertices',vertices),('view/axes',axes),('other/axes',other)]:setv('/workspace/'+path,value)
 commands+=['call /workspace/view project'];expected.append([vertices[i*d+j] for i in range(count) for j in axes])
 # Live source update between projection and edit; the hidden values must survive.
 vertices=[x+1 for x in vertices];setv('/workspace/g/vertices',vertices)
 index=rng.randrange(count);edit=[rng.randrange(-99,100) for _ in axes]
 setv('/workspace/view/vertex-index',index);setv('/workspace/view/edit',edit)
 commands+=['call /workspace/view edit-vertex']
 for axis,value in zip(axes,edit):vertices[index*d+axis]=value
 expected.append(vertices[index*d:(index+1)*d])
 commands+=['call /workspace/other project'];expected.append([vertices[i*d+j] for i in range(count) for j in other])
 commands+=['show /workspace/g/vertices'];expected.append(vertices[:])
# Canonical 4D constrained edit.
for path,value in [('g/dimension',4),('g/vertex-count',1),('g/vertices',[1000000,2000000,3000000,4000000]),('view/axes',[0,2]),('view/vertex-index',0),('view/edit',[9000000,8000000]),('other/axes',[1,3])]:setv('/workspace/'+path,value)
commands+=['call /workspace/view edit-vertex'];expected.append([9000000,2000000,8000000,4000000])
commands+=['call /workspace/view project'];expected.append([9000000,8000000])
commands+=['call /workspace/other project'];expected.append([2000000,4000000])
commands+=['save '+checkpoint,'quit']
run=subprocess.run([exe,'live','neo/neo-os/image.neo'],input='\n'.join(commands)+'\n',capture_output=True,text=True,timeout=60)
observed=re.findall(r'(?:result=|vertices = )\[([^\r\n]*)\]',run.stdout)
assert run.returncode==0 and len(observed)==len(expected),(len(observed),len(expected),run.stdout[-5000:],run.stderr)
for i,(out,ref) in enumerate(zip(observed,expected)):assert list(map(int,out.split()))==ref,(i,out,ref)
r=subprocess.run([exe,'live',checkpoint],input='call /workspace/view project\ncall /workspace/other project\nquit\n',capture_output=True,text=True,timeout=20)
assert 'result=[9000000 8000000]' in r.stdout and 'result=[2000000 4000000]' in r.stdout,r.stdout
print(f'PASS projection oracle: {len(expected)} outputs across48 shapes, dimensions0..257; shared source, live edits, single-image checkpoint/reopen')
for changes,status in [
 (['set /workspace/view/axes [0 0]'],'explicit failure'),
 (['set /workspace/view/axes [-1]'],'explicit failure'),
 (['set /workspace/view/axes [4]'],'explicit failure'),
 (['set /workspace/view/axes true'],'wrong kind'),
 (['set /workspace/view/vertex-index -1'],'explicit failure'),
 (['set /workspace/view/vertex-index 1'],'explicit failure'),
 (['set /workspace/view/edit [1]'],'explicit failure'),
 (['unlink /workspace/view source','link /workspace/view source /workspace/g 1'],'authority denied'),
 (['unlink /workspace/view source'],'unavailable'),
 (['set /workspace/g/vertices []'],'explicit failure')]:
 cmds=changes+['call /workspace/view edit-vertex','show /workspace/g/vertices','quit']
 r=subprocess.run([exe,'live',checkpoint],input='\n'.join(cmds)+'\n',capture_output=True,text=True,timeout=20)
 assert status in r.stdout,r.stdout
 if 'set /workspace/g/vertices []' not in changes:assert 'vertices = [9000000 2000000 8000000 4000000]' in r.stdout,r.stdout
print('PASS invalid axes/index/shape, read-only/missing source; rejected edits preserve source coordinates')
