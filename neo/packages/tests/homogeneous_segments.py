"""Exact-rational segment clipping and generated-topology integration oracle."""
from fractions import Fraction as F
from pathlib import Path
import random,re,subprocess
Q=1000000
exe=str(Path('build/x86_64/Windows/neo.exe').resolve())
out=Path('build/x86_64/Windows');checkpoint=out/'clipped-segment-image.neo'
commands=[];expected=[]
def arr(x):return '['+' '.join(map(str,x))+']'
def put(owner,key,value):commands.append(f'set /workspace/{owner}/{key} '+(arr(value) if isinstance(value,list) else str(value)))
def call(owner,name):commands.append(f'call /workspace/{owner} {name}')
def show(owner,key,value):commands.append(f'show /workspace/{owner}/{key}');expected.append((key,value))
def rounded(x):
    sign=-1 if x<0 else 1;x=abs(x)
    q,rem=divmod(x.numerator,x.denominator)
    return sign*(q+(2*rem>=x.denominator))
def oracle(a,b,near,far,volume):
    distances=[lambda h:h[2]-near]
    if far:distances.append(lambda h:far-h[2])
    if volume:
        distances += [lambda h:h[2]+h[0],lambda h:h[2]-h[0],lambda h:h[2]+h[1],lambda h:h[2]-h[1]]
    low,high=F(0),F(1)
    for plane in distances:
        x,y=plane(a),plane(b)
        if x<0 and y<0:return [],[],[],[]
        if x<0:low=max(low,F(-x,y-x))
        elif y<0:high=min(high,F(x,x-y))
    if low>high:return [],[],[],[]
    result=[]
    for parameter in [low,high]:
        h=[F(x)+(y-x)*parameter for x,y in zip(a,b)]
        result += [rounded(h[j]*Q/h[2]) for j in range(2)]
    return result,[low.numerator,high.numerator],[low.denominator,high.denominator],[0]
for name in ['geometry','perspective','segment-clipper','line-renderer']:
    commands += [f'pkg-load neo/packages/{name}.neo','pkg install','pkg clear']
for package,export,name in [('neo.spatial.geometry','geometry','g'),('neo.spatial.perspective','view','v'),
                           ('neo.render.segment-clipper','clipper','c'),('neo.render.lines','renderer','r')]:
    commands += [f'set /package-manager/id "{package}"',f'set /package-manager/export-name "{export}"',
                 f'set /package-manager/instance-name "{name}"','pkg instance']
commands += ['link /workspace/v source /workspace/g 1','link /workspace/c input /workspace/v 1','link /workspace/r view /workspace/c 1']
put('g','dimension',3);put('g','vertex-count',2);put('g','segments',[0,1])
put('v','view-dimension',2);put('v','map',[Q,0,0,0,Q,0,0,0,Q]);put('v','offset',[0,0,0])
cases=[((-2,0,1),(2,0,1),1,0,True),((0,0,0),(0,0,2),1,0,True),
       ((0,0,-2),(0,0,-1),1,0,True),((-1,-1,1),(1,1,1),1,0,True),
       ((0,0,1),(3,0,3),1,2,True),((-2,0,1),(2,0,1),1,0,False),
       ((2,0,1),(3,0,1),1,0,True),((1,1,1),(1,1,1),1,0,True)]
rng=random.Random(20261023)
for i in range(80):
    a=tuple(rng.randrange(-5,7) for _ in range(3));b=tuple(rng.randrange(-5,7) for _ in range(3))
    cases.append((a,b,1,3 if i%3==0 else 0,i%2==0))
cases += [(b,a,near,far,volume) for a,b,near,far,volume in cases[:16]]
cases += [((1,0,128),(1,0,128),1,0,True),((-1,0,128),(-1,0,128),1,0,True)]
for a,b,near,far,volume in cases:
    put('g','vertices',[x*Q for x in a+b]);put('v','near',near*Q);put('v','far',far*Q);put('v','clip-volume',str(volume).lower())
    call('v','project');call('c','clip')
    result,nums,dens,edges=oracle(a,b,near,far,volume)
    for key,value in [('result',result),('parameter-numerators',nums),('parameter-denominators',dens),('edge-indices',edges)]:show('c',key,value)
    show('g','vertices',[x*Q for x in a+b])
# Raw-unit near crossing: quantizing the homogeneous intersection first loses x.
put('g','vertices',[-1,0,0,1,0,3]);put('v','near',1);put('v','far',0);put('v','clip-volume','true')
call('v','project');call('c','clip')
raw=oracle((-1,0,0),(1,0,3),1,0,True)
for key,value in zip(['result','parameter-numerators','parameter-denominators','edge-indices'],raw):show('c',key,value)
show('g','vertices',[-1,0,0,1,0,3])
# Both original endpoints are rejected, but their edge crosses the whole viewport.
put('g','vertices',[-2*Q,0,Q,2*Q,0,Q]);put('v','near',Q);put('v','far',0);put('v','clip-volume','true')
call('v','project');call('c','clip');put('r','width',5);put('r','height',5);put('r','origin-x',2);put('r','origin-y',2);put('r','pixels-per-unit',2)
call('r','prepare');show('r','pixel-mask',[0]*10+[1]*5+[0]*10)
commands += [f'save {checkpoint.as_posix()}','quit']
result=subprocess.run([exe,'live','neo/neo-os/image.neo'],input='\n'.join(commands)+'\n',capture_output=True,text=True,timeout=120)
(out/'homogeneous-segments.log').write_text(result.stdout+result.stderr,encoding='utf-8')
assert result.returncode==0 and 'neo:' not in result.stdout,result.stdout[-7000:]+result.stderr
actual=re.findall(r'(result|parameter-numerators|parameter-denominators|edge-indices|vertices|pixel-mask) = \[([^\]]*)\]',result.stdout)
assert len(actual)==len(expected),(len(actual),len(expected))
for i,((key,data),(want_key,want)) in enumerate(zip(actual,expected)):
    got=list(map(int,data.split()))
    assert key==want_key and got==want,(i,key,got,want)
reopened=subprocess.run([exe,'live',str(checkpoint)],input='call /workspace/c clip\ncall /workspace/r prepare\nshow /workspace/r/pixel-mask\nquit\n',capture_output=True,text=True,timeout=30)
assert reopened.returncode==0 and 'neo:' not in reopened.stdout,reopened.stdout+reopened.stderr
mask=re.findall(r'pixel-mask = \[([^\]]*)\]',reopened.stdout)
assert len(mask)==1 and list(map(int,mask[0].split()))==expected[-1][1],reopened.stdout
print(f'PASS homogeneous segments: {len(cases)+1} rational clips/provenance, unchanged source, generated lines and single-image reopen')
