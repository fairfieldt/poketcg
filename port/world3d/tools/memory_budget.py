#!/usr/bin/env python3
"""Report static asset residency and a deliberately conservative 4 MiB estimate.

This is not a measured peak. Native sys_get_heap_stats logs from the diagnostic
map tour remain authoritative for allocator/display-list/audio overhead.
"""
import argparse,json,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];REPO=ROOT.parents[1]
sys.path.insert(0,str(REPO/'port/frontends/web/tools'))
from png import read_png
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--elf',type=Path,default=REPO/'port/frontends/n64/adventure/build/poketcg-adventure.elf')
p.add_argument('--size',default='/opt/libdragon/bin/mips64-elf-size')
p.add_argument('--output',type=Path,default=ROOT/'generated/memory-budget.json')
a=p.parse_args();world=json.loads((ROOT/'generated/world.json').read_text());out=ROOT/'generated'
row=subprocess.check_output([a.size,str(a.elf)],text=True).splitlines()[1].split()
resident=sum(map(int,row[:3]));records=[]
for name,record in world['meshes'].items():
 texture_bytes=0
 for key in record['textures']:
  w,h,*_=read_png(out/'textures'/(key+'.png'));texture_bytes+=w*h*2+128
 mesh_bytes=(out/record['file']).stat().st_size
 # Upper estimate for retained transformed vertex records, commands, per-list
 # metadata/alignment, beyond the retained original indexed mesh arrays.
 list_estimate=record['vertices']*32+record['triangles']*16+record['batches']*2048
 records.append(dict(name=name,mesh_bytes=mesh_bytes,texture_bytes=texture_bytes,
                     asset_bytes=mesh_bytes+texture_bytes,display_list_estimate=list_estimate))
actor=next(r for r in records if r['name']=='actors')
for record in records:record['combined_estimate']=record['asset_bytes']+record['display_list_estimate']
worst=max((r for r in records if r['name']!='actors'),key=lambda r:r['combined_estimate'])
framebuffers=320*240*2*4
# Includes the persistent 24-sprite card-art LRU plus audio rings/mixer, RSP
# queues, UI allocations, allocator bookkeeping and stack reserve.
other_reserve=512*1024
estimate=resident+framebuffers+actor['combined_estimate']+worst['combined_estimate']+other_reserve
result=dict(measured=False,ram_bytes=4*1024*1024,elf_text_data_bss=resident,
 framebuffer_depth_bytes=framebuffers,actors=actor,worst_scene=worst,
 other_reserved_bytes=other_reserve,total_estimate_bytes=estimate,
 margin_estimate_bytes=4*1024*1024-estimate,
 note='Display-list/mixer/RSP/stack overhead is estimated; verify native heap statistics from the 4 MiB map tour.',
 scenes=sorted(records,key=lambda r:r['asset_bytes'],reverse=True))
a.output.write_text(json.dumps(result,indent=2)+'\n')
print(f'Static budget estimate {estimate/2**20:.2f} /4 MiB, estimated margin {result["margin_estimate_bytes"]/2**20:.2f} MiB; worst scene {worst["name"]}')
print('Runtime measurements are required to confirm this estimate.')
