#!/usr/bin/env python3
"""Actual source opening A/B with legacy/clean shared actor assets."""
from pathlib import Path
import argparse,json,shutil,subprocess,time
from PIL import Image
ADV=Path(__file__).resolve().parents[1];ROOT=ADV.parents[3];OUT=ADV/'build/actor-grounding-review'
def build(selected=False):
 OUT.mkdir(parents=True,exist_ok=True);source=ADV/'build/expansion';objects=[]
 for folder in ('engine','game'):
  for src in sorted((source/folder).rglob('*.o')):
   dst=OUT/'pinned'/src.relative_to(source);dst.parent.mkdir(parents=True,exist_ok=True)
   if not selected or not dst.exists():shutil.copy2(src,dst)
   objects.append(dst)
 for src in sorted(source.glob('*.o')):
  if src.name in ('main.o','video.o'):continue
  dst=OUT/'pinned'/src.name
  if not selected or not dst.exists():shutil.copy2(src,dst)
  objects.append(dst)
 main=(ADV/'main.c').read_text();needle='void game_draw(void)\n{';assert main.count(needle)==1
 (OUT/'grounding-main.c').write_text(main.replace(needle,'void grounding_original_draw(void)\n{'))
 fs=OUT/'filesystem';fs.mkdir(exist_ok=True)
 for src in (source/'filesystem').iterdir():
  if src.name=='world':continue
  dst=fs/src.name
  if not dst.exists():dst.symlink_to(src,target_is_directory=src.is_dir())
 world=fs/'world';world.mkdir(exist_ok=True);generated=ROOT/'port/world3d/generated'
 for name in ('maps','lod','actors.l64'):
  dst=world/name;src=generated/name
  if not dst.exists():dst.symlink_to(src,target_is_directory=src.is_dir())
 if not (world/'textures').exists():(world/'textures').symlink_to(source/'filesystem/world/textures',target_is_directory=True)
 (fs/'before').mkdir(exist_ok=True)
 if not (fs/'before/world').exists():(fs/'before/world').symlink_to(ADV/'build/actor-grounding-before',target_is_directory=True)
 archive=ADV/'build/tiny3d-experiment/build/libt3d-poketcg.a'
 (OUT/'Makefile').write_text(f'''N64_INST := /opt/libdragon
export N64_INST
BUILD_DIR := .
N64_MKDFS_ROOT := filesystem
LIBDRAGON_PREVIEW := 2
include $(N64_INST)/include/n64.mk
CFLAGS += -I{ADV} -I{OUT} -I{ROOT/'port/src'} -I{ROOT/'port/overworld'} -I{ROOT/'port/gameplay'} -I{ROOT/'port/world3d'} -I{source} -DSCENE_T3D=1 -DADVENTURE_DIAGNOSTIC=9 -DADVENTURE_HIGH_RES=1 -DADVENTURE_SHOW_FPS=0 -DADVENTURE_VI_FILTER=FILTERS_RESAMPLE {'-DGROUNDING_SELECTED_VI' if selected else ''}
all: actor-grounding.z64
actor-grounding.elf: bench.o video.o {' '.join(str(p) for p in objects)} {archive}
bench.o: {ADV/'tests/actor_grounding_native.c'} grounding-main.c
\t$(CC) $(CFLAGS) -c $< -o $@
video.o: {ADV/'video.c'}
\t$(CC) $(CFLAGS) -c $< -o $@
actor-grounding.z64: N64_ROM_TITLE = "Actor ground review"
actor-grounding.z64: N64_ROM_SAVETYPE = none
actor-grounding.z64: actor-grounding.dfs
.PHONY: force-filesystem
force-filesystem:
actor-grounding.dfs: force-filesystem
-include $(wildcard *.d)
''')
 subprocess.run(['make','-C',str(OUT),'-j4','all'],check=True)
def run(ram):
 work=OUT/str(ram);work.mkdir(exist_ok=True);(work/'saves').mkdir(exist_ok=True);log=work/'runtime.log';settings=work/'settings.bml'
 settings.write_text(f'''Video
  Driver: Metal
  Quality: SD
  Shader: None
  DisableVideoInterfaceProcessing: false
Input
  Driver: SDL
  Defocus: Allow
Audio
  Driver: SDL
  Blocking: true
General
  HomebrewMode: true
  NoFilePrompt: true
Nintendo64
  ExpansionPak: {'true' if ram==8 else 'false'}
Paths
  Saves: {work}/saves/
''')
 ares=ADV/'build/vi-direct-capture/Poketcg VI Capture.app/Contents/MacOS/ares';start=time.monotonic()
 with log.open('w') as output:
  p=subprocess.Popen([str(ares),'--settings-file',str(settings),'--no-file-prompt',str(OUT/'actor-grounding.z64')],stdout=output,stderr=subprocess.STDOUT)
  try:
   while p.poll() is None and time.monotonic()-start<150:
    time.sleep(.2);s=log.read_text(errors='replace')
    if any(m in s for m in ('ACTOR GROUNDING NATIVE PASS','ASSERTION FAILED','EXCEPTION:')):break
  finally:
   p.terminate()
   try:p.wait(timeout=5)
   except subprocess.TimeoutExpired:p.kill();p.wait()
 s=log.read_text(errors='replace');assert 'ACTOR GROUNDING NATIVE PASS' in s,s[-3000:]
 for line in s.splitlines():
  if line.startswith('FRAMEBUFFER ') and line!='FRAMEBUFFER END':
   _,name,w,h=line.split();im=Image.new('RGB',(int(w),int(h)))
  elif line.startswith('PIXELS '):
   _,x,y,raw=line.split();x=int(x);y=int(y)
   for i in range(0,len(raw),4):
    v=int(raw[i:i+4],16);im.putpixel((x+i//4,y),tuple(((v>>s)&31)*255//31 for s in (11,6,1)))
  elif line=='FRAMEBUFFER END':im.save(work/(name+'.png'))
 result={'passed':True,'ram_mib':ram,'proof':[l for l in s.splitlines() if l.startswith(('ACTOR GROUNDING NATIVE PASS','GROUNDING SOURCE','GROUNDING SOFT'))],'wall_seconds':round(time.monotonic()-start,2),'scope':'Actual ow_new_game script/camera/actors with old and cleaned actorassets; real audio. PNGs are completed raw RDP frames, not post-VI.'}
 (work/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--build',action='store_true');p.add_argument('--selected-captures',action='store_true');p.add_argument('--ram-mib',type=int,choices=(4,8));a=p.parse_args()
 if a.build:build(a.selected_captures)
 if a.ram_mib:run(a.ram_mib)
