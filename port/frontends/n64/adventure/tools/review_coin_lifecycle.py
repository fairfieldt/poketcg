#!/usr/bin/env python3
"""Private native coin-result dismissal regression at320/480."""
from pathlib import Path
import argparse,json,shutil,subprocess,time
from PIL import Image
ADV=Path(__file__).resolve().parents[1];ROOT=ADV.parents[3];OUT=ADV/'build/coin-lifecycle-review'
def build():
 OUT.mkdir(parents=True,exist_ok=True);source=ADV/'build/expansion';objects=[]
 for src in sorted((source/'engine').rglob('*.o')):
  dst=OUT/'pinned'/src.relative_to(source);dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(src,dst);objects.append(dst)
 for name in ('save_sram.o','rspq_fixed.o'):
  dst=OUT/'pinned'/name;shutil.copy2(source/name,dst);objects.append(dst)
 fs=OUT/'filesystem';fs.mkdir(exist_ok=True)
 if not (fs/'cards').exists():(fs/'cards').symlink_to(source/'filesystem/cards',target_is_directory=True)
 (fs/'buildinfo.txt').write_text('ai=1\n')
 ui=(ADV.parent/'tcgn64.c').read_text();start=ui.index('static int ui_ack(');end=ui.index('static void draw_duel_actions',start);ack=ui[start:end];needle='Keys k = keys(ports);';assert ack.count(needle)==1
 ui=ui[:start]+ack.replace(needle,'Keys k = (Keys){.a=true}; /* private automatic ACK */')+ui[end:]
 (OUT/'coin-lifecycle-source.c').write_text(ui)
 (OUT/'Makefile').write_text(f'''N64_INST := /opt/libdragon
export N64_INST
BUILD_DIR := .
N64_MKDFS_ROOT := filesystem
LIBDRAGON_PREVIEW := 2
include $(N64_INST)/include/n64.mk
CFLAGS += -I{ADV} -I{ADV.parent} -I{OUT} -I{ROOT/'port/src'} -I{source} -DADVENTURE_HIGH_RES=1 -DADVENTURE_SHOW_FPS=0 -DADVENTURE_VI_FILTER=FILTERS_RESAMPLE
all: coin-lifecycle.z64
coin-lifecycle.elf: bench.o {' '.join(str(p) for p in objects)}
bench.o: {ADV/'tests/coin_lifecycle_native.c'} coin-lifecycle-source.c
\t$(CC) $(CFLAGS) -c $< -o $@
coin-lifecycle.z64: N64_ROM_TITLE = "Coin lifecycle review"
coin-lifecycle.z64: N64_ROM_SAVETYPE = none
coin-lifecycle.z64: coin-lifecycle.dfs
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
  p=subprocess.Popen([str(ares),'--settings-file',str(settings),'--no-file-prompt',str(OUT/'coin-lifecycle.z64')],stdout=output,stderr=subprocess.STDOUT)
  try:
   while p.poll() is None and time.monotonic()-start<70:
    time.sleep(.2);s=log.read_text(errors='replace')
    if any(m in s for m in ('COIN LIFECYCLE NATIVE PASS','ASSERTION FAILED','EXCEPTION:')):break
  finally:
   p.terminate()
   try:p.wait(timeout=5)
   except subprocess.TimeoutExpired:p.kill();p.wait()
 s=log.read_text(errors='replace');assert 'COIN LIFECYCLE NATIVE PASS' in s,s[-2500:]
 for line in s.splitlines():
  if line.startswith('FRAMEBUFFER ') and line!='FRAMEBUFFER END':
   _,name,w,h=line.split();im=Image.new('RGB',(int(w),int(h)))
  elif line.startswith('PIXELS '):
   _,x,y,raw=line.split();x=int(x);y=int(y)
   for i in range(0,len(raw),4):
    v=int(raw[i:i+4],16);im.putpixel((x+i//4,y),tuple(((v>>s)&31)*255//31 for s in (11,6,1)))
  elif line=='FRAMEBUFFER END':im.save(work/(name+'.png'))
 result={'passed':True,'ram_mib':ram,'proof':[l for l in s.splitlines() if l.startswith('COIN LIFECYCLE NATIVE PASS')],'wall_seconds':round(time.monotonic()-start,2),'scope':'Real native UI/engine component fixture, automatic ACK only in private copied frontend, audio stubs; PNGs are completed raw RDP frames, not post-VI. Real-control/full-Amy proof is captured separately.'}
 (work/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--build',action='store_true');p.add_argument('--ram-mib',type=int,choices=(4,8));a=p.parse_args()
 if a.build:build()
 if a.ram_mib:run(a.ram_mib)
