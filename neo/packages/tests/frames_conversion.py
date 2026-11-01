"""Independent frame hierarchy oracle and malformed-graph checks; optional Python."""
from pathlib import Path
from random import Random
import re
import subprocess
Q=1000000
exe=str(Path('build/x86_64/Windows/neo.exe').resolve())
scene=Path('build/x86_64/Windows/frames-oracle.neo')
def renamed(path,name):return re.sub(r'\(package\b','('+name,Path(path).read_text(encoding='utf-8'),count=1)
scene.write_text('(scene '+renamed('neo/packages/frames.neo','f')+renamed('neo/packages/matrix.neo','m')+')',encoding='utf-8')
def array(a):return '['+' '.join(map(str,a))+']'
def rnd(x):
 a,b=divmod(abs(x),Q)
 return (-1 if x<0 else 1)*(a+(b*2>=Q))
def mat(a,x,d):return [rnd(sum(a[i*d+j]*x[j] for j in range(d))) for i in range(d)]
commands=[];expected=[]
def setv(path,v):commands.append('set '+path+' '+(array(v) if isinstance(v,list) else str(v)))
base='/f/contents/'
for name in ['source','middle','target']:
 commands+=['copy '+base+'frame '+base[:-1]+' '+name,'link '+base+name+' space '+base+'space 1']
commands+=['link '+base+'frame space '+base+'space 1','link '+base+'source parent '+base+'middle 33','link '+base+'middle parent '+base+'frame 33','link '+base+'target parent '+base+'frame 33','link '+base+'converter source '+base+'source 33','link '+base+'converter target '+base+'target 33','link '+base+'converter calculator /m/contents/matrix 67']
rng=Random(4049)
for d in [0,1,2,3,4,7]*5:
 setv(base+'space/dimension',d);commands+=['call '+base+'space initialize']
 for name in ['frame','source','middle','target']:
  commands+=['call '+base+name+' initialize'];setv(base+name+'/root','true' if name=='frame' else 'false')
 a=[rng.randint(-Q,Q) for _ in range(d*d)];b=[0]*(d*d)
 for i in range(d):b[i*d+i]=rng.choice([-2,-1,1,2])*Q
 t=[rng.randint(-10,10)*1000 for _ in range(d)];u=[rng.randint(-10,10)*1000 for _ in range(d)];v=[rng.randint(-10,10)*1000 for _ in range(d)]
 x=[rng.randint(-10,10)*Q for _ in range(d)]
 for name,linear,translation in [('source',a,t),('middle',b,u),('target',[],v)]:
  setv(base+name+'/linear-kind','"identity"' if name=='target' else '"dense"');setv(base+name+'/linear',linear);setv(base+name+'/translation',translation)
 setv(base+'converter/input',x)
 for kind in ['point','displacement']:
  setv(base+'converter/kind','"'+kind+'"')
  commands+=['call '+base+'converter convert']
  y=mat(a,x,d)
  if kind=='point':y=[z+t[i] for i,z in enumerate(y)]
  y=mat(b,y,d)
  if kind=='point':y=[z+u[i]-v[i] for i,z in enumerate(y)]
  expected.append(y)
run=subprocess.run([exe,'live',str(scene)],input='\n'.join(commands+['quit'])+'\n',capture_output=True,text=True,timeout=60)
observed=re.findall(r'ok;[^\r\n]*?result=\[([^\r\n]*)\]',run.stdout)
assert run.returncode==0 and len(observed)==len(expected),(len(observed),len(expected),run.stdout[-5000:],run.stderr)
for i,(out,reference) in enumerate(zip(observed,expected)):assert list(map(int,out.split()))==reference,(i,out,reference)
print(f'PASS frames oracle: {len(expected)} point/displacement hierarchical conversions, dimensions0..7')
# Independent 2D dense target map requires a row swap when inverted.
prefix=['link '+base+'frame space '+base+'space 1','set '+base+'space/dimension 2','call '+base+'space initialize','call '+base+'frame initialize',
 'copy '+base+'frame '+base[:-1]+' target','set '+base+'target/root false','link '+base+'target parent '+base+'frame 33',
 'link '+base+'converter source '+base+'frame 33','link '+base+'converter target '+base+'target 33','link '+base+'converter calculator /m/contents/matrix 67',
 'set '+base+'converter/input [4000000 6000000]','set '+base+'target/linear-kind "dense"','set '+base+'target/linear [0 2000000 1000000 3000000]',
 'set '+base+'target/translation [2000000 4000000]']
def check(extra,needle):
 r=subprocess.run([exe,'live',str(scene)],input='\n'.join(prefix+extra+['quit'])+'\n',capture_output=True,text=True,timeout=30)
 assert needle in r.stdout,(needle,r.stdout)
check(['call '+base+'converter convert'],'result=[-1000000 1000000]')
for extra,status in [
 (['set '+base+'target/linear [1000000 0 0 0]'],'explicit failure'),
 (['unlink '+base+'target parent','link '+base+'target parent '+base+'target 33'],'explicit failure'),
 (['copy '+base+'space '+base[:-1]+' other-space','unlink '+base+'target space','link '+base+'target space '+base+'other-space 1'],'explicit failure'),
 (['copy '+base+'frame '+base[:-1]+' other-root','unlink '+base+'target parent','link '+base+'target parent '+base+'other-root 33'],'explicit failure'),
 (['set '+base+'space/axis-units []'],'explicit failure'),
 (['set '+base+'space/metric "custom"'],'explicit failure'),
 (['set '+base+'target/dimension 3'],'explicit failure'),
 (['set '+base+'frame/translation [1 0]'],'explicit failure'),
 (['unlink '+base+'target parent'],'unavailable'),
 (['unlink '+base+'converter target','link '+base+'converter target '+base+'target 1'],'authority denied')]:
 check(extra+['set '+base+'converter/result [-91]','call '+base+'converter convert','show '+base+'converter/result'],status)
 check(extra+['set '+base+'converter/result [-91]','call '+base+'converter convert','show '+base+'converter/result'],'result = [-91]')
print('PASS dense inward map; singular/cycle/space/root/unit/metric/shape/authority failures retain result')
