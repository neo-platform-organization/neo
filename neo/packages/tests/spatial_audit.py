"""Run independent milestone9 numerical/graph reference checks (optional Python)."""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import subprocess
import sys
checks=['vector_precision','matrix_precision','matrix_inverse','affine_precision','frames_conversion','geometry_precision','projection_edit','slice_selection','section_intersections','triangle_sections','linearview_edit','perspective_projection']
def run(name):
 result=subprocess.run([sys.executable,str(Path(__file__).with_name(name+'.py'))],capture_output=True,text=True,timeout=240)
 return name,result
failed=[]
with ThreadPoolExecutor(max_workers=2) as pool:
 for name,result in pool.map(run,checks):
  print(result.stdout.strip(),flush=True)
  if result.returncode:failed.append(name);print(result.stderr,flush=True)
assert not failed,failed
print(f'PASS milestone9 reference audit: {len(checks)} independent suites')
