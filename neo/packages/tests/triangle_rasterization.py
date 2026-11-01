"""Exact pixel-centre coverage and rational screen-depth oracle."""
from fractions import Fraction as F
from pathlib import Path
import random,re,subprocess
exe=str(Path('build/x86_64/Windows/neo.exe').resolve())
out=Path('build/x86_64/Windows');checkpoint=out/'triangle-renderer-image.neo'
commands=[];expected=[]
def arr(x):return '['+' '.join(map(str,x))+']'
def put(owner,key,value):commands.append(f'set /workspace/{owner}/{key} '+(arr(value) if isinstance(value,list) else str(value)))
def call(owner,name):commands.append(f'call /workspace/{owner} {name}')
def show(key,value):commands.append(f'show /workspace/r/{key}');expected.append((key,value))
def cross(a,b,p):return (b[0]-a[0])*(p[1]-a[1])-(b[1]-a[1])*(p[0]-a[0])
def rounded(v):
    sign=-1 if v<0 else 1;v=abs(v);q,r=divmod(v.numerator,v.denominator)
    return sign*(q+(2*r>=v.denominator))
def oracle(points,faces,depths):
    owners=[-1]*25;zbuffer=[0]*25;fragments=0
    for face in range(len(faces)//3):
        indices=list(faces[face*3:face*3+3]);a,b,c=[points[i] for i in indices]
        area=cross(a,b,c)
        if area==0:continue
        if area<0:indices[1],indices[2]=indices[2],indices[1];a,b,c=[points[i] for i in indices];area=-area
        edges=[(b,c),(c,a),(a,b)]
        for y in range(5):
            for x in range(5):
                p=(F(2*x+1,2),F(2*y+1,2));weights=[cross(u,v,p) for u,v in edges]
                inside=True
                for weight,(u,v) in zip(weights,edges):
                    inclusive=v[1]<u[1] or (v[1]==u[1] and v[0]>u[0])
                    if weight<0 or (weight==0 and not inclusive):inside=False;break
                if not inside:continue
                fragments+=1;z=rounded(sum(w*depths[i] for w,i in zip(weights,indices))/area);index=y*5+x
                if owners[index]<0 or z<zbuffer[index]:owners[index]=face;zbuffer[index]=z
    return owners,zbuffer,fragments

for name in ['geometry','projection','triangle-renderer']:
    commands += [f'pkg-load neo/packages/{name}.neo','pkg install','pkg clear']
for package,export,name in [('neo.spatial.geometry','geometry','g'),('neo.spatial.projection','projection','v'),('neo.render.triangles','renderer','r')]:
    commands += [f'set /package-manager/id "{package}"',f'set /package-manager/export-name "{export}"',f'set /package-manager/instance-name "{name}"','pkg instance']
commands += ['link /workspace/v source /workspace/g 1','link /workspace/r view /workspace/v 1']
put('g','dimension',2);put('v','axes',[0,1]);put('r','width',5);put('r','height',5)
triangle=[(0,0),(4,0),(0,4)]
cases=[(triangle,[0,1,2],[10]*3),(triangle,[2,1,0],[10]*3),
       ([(0,0),(4,0),(0,4),(4,4)],[0,1,2,1,3,2],[10]*4),
       (triangle*2,[0,1,2,3,4,5],[10]*3+[5]*3),
       (triangle*2,[3,4,5,0,1,2],[10]*3+[5]*3),
       (triangle*2,[0,1,2,3,4,5],[0,12,0,5,5,5]),
       (triangle*2,[0,1,2,3,4,5],[-8,-4,-8,-6,-6,-6]),
       (triangle,[0,1,2,0,1,2],[5]*3), ([(0,0),(2,2),(4,4)],[0,1,2],[3]*3),
       ([(-3,-3),(8,0),(0,8)],[0,1,2],[1,4,7]),
       ([(-10**6,-10**6),(10**6,-10**6),(0,10**6)],[0,1,2],[1]*3),
       (triangle,[],[1]*3),([],[],[])]
rng=random.Random(20261023)
for i in range(70):
    count=3 if i%2 else 6
    cases.append(([(rng.randrange(-3,9),rng.randrange(-3,9)) for _ in range(count)],list(range(count)),[rng.randrange(-12,13) for _ in range(count)]))
for points,faces,depths in cases:
    put('g','vertex-count',len(points));put('g','vertices',[v for x,y in points for v in (x*1000000,-y*1000000)])
    put('g','triangles',faces);put('r','depths',depths);call('v','project');call('r','prepare')
    owners,zbuffer,fragments=oracle(points,faces,depths)
    show('owners',owners);show('depth-buffer',zbuffer)
    commands.append('show /workspace/r/fragment-count');expected.append(('fragment-count',fragments))
# Nontrivial row reordering must not reorder source-indexed depths or topology.
points,faces,depths=cases[5]
put('g','vertex-count',6);put('g','vertices',[v for x,y in points for v in (x*1000000,-y*1000000)]);put('g','triangles',faces);put('r','depths',depths);call('v','project')
order=[5,2,0,4,1,3];put('v','source-indices',order);put('v','result',[v for i in order for v in (points[i][0]*1000000,-points[i][1]*1000000)])
call('r','prepare');owners,zbuffer,fragments=oracle(points,faces,depths)
show('owners',owners);show('depth-buffer',zbuffer);commands.append('show /workspace/r/fragment-count');expected.append(('fragment-count',fragments))
commands += [f'save {checkpoint.as_posix()}','quit']
result=subprocess.run([exe,'live','neo/neo-os/image.neo'],input='\n'.join(commands)+'\n',capture_output=True,text=True,timeout=120)
(out/'triangle-renderer-oracle.log').write_text(result.stdout+result.stderr,encoding='utf-8')
assert result.returncode==0 and 'neo:' not in result.stdout,result.stdout[-7000:]+result.stderr
actual=re.findall(r'(owners|depth-buffer|fragment-count) = (\[[^\]]*\]|-?\d+)',result.stdout)
assert len(actual)==len(expected),(len(actual),len(expected))
for index,((key,value),(want_key,want)) in enumerate(zip(actual,expected)):
    got=list(map(int,value[1:-1].split())) if value.startswith('[') else int(value)
    assert key==want_key and got==want,(index,key,got,want)
reopened=subprocess.run([exe,'live',str(checkpoint)],input='call /workspace/r prepare\nshow /workspace/r/owners\nshow /workspace/r/depth-buffer\nquit\n',capture_output=True,text=True,timeout=30)
assert reopened.returncode==0 and 'neo:' not in reopened.stdout,reopened.stdout+reopened.stderr
arrays=re.findall(r'(owners|depth-buffer) = \[([^\]]*)\]',reopened.stdout)
assert arrays and [list(map(int,v.split())) for _,v in arrays]==[owners,zbuffer],reopened.stdout
print(f'PASS triangles: {len(cases)+1} rational coverage/depth scenes, shared edges, row mapping and image reopen')
