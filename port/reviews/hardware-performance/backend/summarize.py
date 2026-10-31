"""Summarize the fixed-geometry cached-GL/Tiny3D capture pair."""
from pathlib import Path
import hashlib
import json
import re
import statistics

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[3]
ADV=ROOT/'port/frontends/n64/adventure'

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()

rows={}
cases={}
for backend in ('gl','t3d'):
    work=ADV/f'build/expansion-pak/backend-{backend}-480-v1'
    text=(work/'native.log').read_text()
    times={}
    for stage,value in re.findall(r'VIDEO FRAME stage=(\S+) render_us=(\d+)',text):
        times.setdefault(stage,[]).append(int(value))
    for stage,values in times.items():
        assert len(values)==24,(stage,len(values))
        rows.setdefault(stage,{})[backend]={
            'count':len(values),'minimum_us':min(values),'median_us':statistics.median(values),'maximum_us':max(values)}
    heap=re.search(r'VIDEO HEAP stage=leads total=(\d+) used=(\d+) free=(\d+)',text)
    cases[backend]={'heap':dict(zip(('total','used','free'),map(int,heap.groups()))),
                    'capture_provenance':f'cases/{backend}-480-v1/capture-provenance.json',
                    'native_log':str((work/'native.log').relative_to(ROOT))}

for values in rows.values():values['median_speedup']=values['gl']['median_us']/values['t3d']['median_us']

gl=ADV/'build/backend-gl/filesystem/world'
native=ADV/'build/backend-t3d/filesystem/world'
files=[p for p in gl.rglob('*') if p.is_file() and 'lod' not in p.relative_to(gl).parts]
for p in files:assert sha(p)==sha(native/p.relative_to(gl)),p

result={'resolution':[480,360],'ram_mib':8,'samples_per_stage':24,
 'timer':'emulated CP0 elapsed around attach/GL/render/RDP completion; not hardware FPS',
 'tiny3d_revision':'f2eccc3998967f1915be58d52f1c5bb6d5598a8c',
 'same_non_LOD_world_asset_files':len(files),'stages':rows,'cases':cases,
 'scope':'unchanged high-detail room/actor meshes, same camera/materials/rigid part poses; no LOD or application culling in fixture'}
(HERE/'results.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
