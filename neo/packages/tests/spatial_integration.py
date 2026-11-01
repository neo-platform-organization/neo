"""Milestone9: all spatial packages and networking coexist in one saved OS graph."""
from pathlib import Path
import os
import re
import subprocess
exe=str(Path(os.environ.get('NEO_TEST_EXE','build/x86_64/Windows/neo.exe')).resolve())
checkpoint=Path('build/x86_64/Windows/milestone09-image.neo')
commands=[];expected=[]
packages=['vector','matrix','frames','geometry','projection','section','linearview','perspective','exposure','session']
for package in packages:commands += [f'pkg-load neo/packages/{package}.neo','pkg install','pkg clear']
instances=[('neo.maths.vector','vector','v'),('neo.maths.matrix','matrix','math'),
 ('neo.spatial.frames','space','space'),('neo.spatial.frames','frame','root-frame'),('neo.spatial.frames','frame','local'),('neo.spatial.frames','converter','conversion'),
 ('neo.spatial.geometry','geometry','g'),('neo.spatial.projection','projection','axes'),('neo.spatial.section','section','segments'),('neo.spatial.section','triangle-section','faces'),
 ('neo.spatial.linearview','view','affine'),('neo.spatial.perspective','view','perspective'),('neo.network.exposure','publisher','publisher'),('neo.network.exposure','subscriber','subscriber'),('neo.network.session','session','session')]
for package,export,name in instances:commands += [f'set /package-manager/id "{package}"',f'set /package-manager/export-name "{export}"',f'set /package-manager/instance-name "{name}"','pkg instance']
def arr(x):return '['+' '.join(map(str,x))+']'
def put(owner,k,x):commands.append(f'set /workspace/{owner}/{k} '+(arr(x) if isinstance(x,list) else str(x)))
def call(owner,selector,output=None):
 commands.append(f'call /workspace/{owner} {selector}')
 if output is not None:expected.append(output)
def show(owner,k,output):commands.append(f'show /workspace/{owner}/{k}');expected.append(output)
Q=1000000
for frame in ['root-frame','local']:commands += [f'link /workspace/{frame} space /workspace/space 1']
put('space','dimension',4);call('space','initialize');call('root-frame','initialize');call('local','initialize')
put('local','root','false');put('local','translation',[3*Q,4*Q,5*Q,6*Q]);commands += ['link /workspace/local parent /workspace/root-frame 33','link /workspace/conversion source /workspace/local 33','link /workspace/conversion target /workspace/root-frame 33','link /workspace/conversion calculator /workspace/math 67']
put('g','dimension',4);put('g','vertex-count',3);call('g','initialize')
put('g','vertices',[Q,2*Q,3*Q,4*Q,2*Q,3*Q,4*Q,5*Q,3*Q,4*Q,5*Q,6*Q]);put('g','points',[0,1,2]);put('g','segments',[0,1,1,2]);put('g','triangles',[0,1,2]);commands += ['link /workspace/g frame /workspace/local 1']
call('g','validate-frame');call('g','validate');show('g','triangle-degenerate',[1])
commands += ['link /workspace/axes source /workspace/g 3','link /workspace/affine source /workspace/g 3','link /workspace/perspective source /workspace/g 1','link /workspace/segments source /workspace/g 1','link /workspace/faces source /workspace/g 1']
put('axes','axes',[0,2]);put('axes','vertex-index',0);put('axes','edit',[9*Q,8*Q]);call('axes','edit-vertex',[9*Q,2*Q,8*Q,4*Q])
put('affine','view-dimension',2);put('affine','map',[Q,0,0,0,0,Q,0,0]);put('affine','translation',[0,0]);put('affine','lift',[Q,0,0,Q,0,0,0,0]);put('affine','edit-enabled','true');put('affine','edit-tolerance',0);put('affine','vertex-index',0);put('affine','edit',[7*Q,6*Q]);call('affine','edit-vertex',[7*Q,6*Q,8*Q,4*Q]);call('g','validate');show('g','triangle-degenerate',[0])
call('axes','project',[7*Q,8*Q,2*Q,4*Q,3*Q,5*Q])
put('axes','slice-axes',[1]);put('axes','slice-values',[3*Q]);put('axes','slice-tolerances',[0]);call('axes','slice',[2*Q,4*Q]);show('axes','source-indices',[1])
for owner in ['segments','faces']:put(owner,'plane-axis',1);put(owner,'plane-value',3*Q)
call('segments','intersect',[2*Q,3*Q,4*Q,5*Q]*2);call('faces','intersect',[2*Q,3*Q,4*Q,5*Q])
put('perspective','view-dimension',2);put('perspective','map',[Q,0,0,0,0,Q,0,0,0,0,0,Q]);put('perspective','offset',[0,0,0]);put('perspective','near',Q);call('perspective','project',[400000,600000,500000,666667]);show('perspective','source-indices',[1,2])
put('conversion','input',[7*Q,6*Q,8*Q,4*Q]);call('conversion','convert',[10*Q,10*Q,13*Q,10*Q])
commands += ['unlink /workspace/conversion source','unlink /workspace/conversion target','link /workspace/conversion source /workspace/root-frame 33','link /workspace/conversion target /workspace/local 33']
put('conversion','input',[10*Q,10*Q,13*Q,10*Q]);call('conversion','convert',[7*Q,6*Q,8*Q,4*Q])
put('v','dimension',4);call('v','initialize');put('v','left',[Q,2*Q,3*Q,4*Q]);put('v','right',[Q,2*Q,3*Q,4*Q]);call('v','dot')
commands += ['save '+checkpoint.as_posix(),'quit']
r=subprocess.run([exe,'live','neo/neo-os/image.neo'],input='\n'.join(commands)+'\n',capture_output=True,text=True,timeout=120)
log=Path('build/x86_64/Windows/milestone09-integration.log');log.write_text(r.stdout+r.stderr,encoding='utf-8')
assert r.returncode==0 and 'neo:' not in r.stdout and checkpoint.is_file(),r.stdout[-10000:]+r.stderr
outputs=re.findall(r'(?:result=|(?:triangle-degenerate|source-indices) = )\[([^\r\n]*)\]',r.stdout)
# Constructor results are scalar; only checked array results are collected.
assert len(outputs)==len(expected),(len(outputs),len(expected),r.stdout[-7000:])
for i,(out,ref) in enumerate(zip(outputs,expected)):assert list(map(int,out.split()))==ref,(i,out,ref)
assert 'result=30000000' in r.stdout,r.stdout[-3000:]
reopen=['call /workspace/g validate','call /workspace/g validate-frame','call /workspace/axes slice','show /workspace/axes/source-indices','call /workspace/affine project','call /workspace/perspective project','call /workspace/segments intersect','call /workspace/faces intersect','call /workspace/conversion convert','call /workspace/v dot','pkg-list','quit']
r=subprocess.run([exe,'live',str(checkpoint)],input='\n'.join(reopen)+'\n',capture_output=True,text=True,timeout=120)
assert r.returncode==0 and 'neo:' not in r.stdout,r.stdout[-10000:]+r.stderr
for package,export,name in instances:assert package in r.stdout,package
assert 'source-indices = [1]' in r.stdout and 'result=30000000' in r.stdout and 'result=[7000000 6000000 8000000 4000000]' in r.stdout,r.stdout
source=checkpoint.read_text(encoding='utf-8');nodes=sum(t=='(' for t in re.findall(r'"(?:\\.|[^"\\])*"|//[^\n]*|[()]',source))
print(f'PASS milestone9 integration: {len(packages)} packages/{len(instances)} instances, {nodes} graph objects, {checkpoint.stat().st_size} checkpoint bytes; shared geometry/views, exact frame roundtrip, topology change, source provenance and full-image reopen')
