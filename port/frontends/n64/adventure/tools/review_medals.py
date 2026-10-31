#!/usr/bin/env python3
"""Build/run an isolated original-art/input/audio medal gate in private Ares."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import shutil
import subprocess
import time
from PIL import Image

ADV=Path(__file__).resolve().parents[1]
ROOT=ADV.parents[3]
OUT=ADV/'build/medal-review'

def build():
    OUT.mkdir(parents=True,exist_ok=True)
    objects=[]
    for source in sorted((ADV/'build/engine').rglob('*.o')):
        dest=OUT/'pinned'/source.relative_to(ADV/'build');dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,dest);objects.append(dest)
    for name in ('audio.o','wav64_vadpcm_fixed.o','rspq_fixed.o','save_sram.o','game/overworld/overworld.o','game/overworld/gen/data.o'):
        source=ADV/'build'/name;dest=OUT/'pinned'/name;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,dest);objects.append(dest)
    fs=OUT/'filesystem';fs.mkdir(exist_ok=True)
    for name in ('audio','cards'):
        target=fs/name
        if not target.exists():target.symlink_to(ADV/'build/filesystem'/name,target_is_directory=True)
    (fs/'medals').mkdir(exist_ok=True);(fs/'buildinfo.txt').write_text('ai=1\n')
    for source in sorted((ADV/'assets/medals').glob('*.png')):
        subprocess.run(['/opt/libdragon/bin/mksprite','-f','RGBA16','-o',str(fs/'medals'),str(source)],check=True)
    mk=f'''N64_INST := /opt/libdragon
export N64_INST
BUILD_DIR := .
N64_MKDFS_ROOT := filesystem
LIBDRAGON_PREVIEW := 2
include $(N64_INST)/include/n64.mk
CFLAGS += -I{ADV.parent} -I{ADV} -I{ROOT/'port/src'} -I{ROOT/'port/overworld'} -I{ROOT/'port/gameplay'} -I{ADV/'build'} -DADVENTURE_DIAGNOSTIC=9
N64_LDFLAGS += --wrap=joypad_get_inputs --wrap=joypad_is_connected --wrap=n64_video_present
all: medal-review.z64
medal-review.elf: bench.o medal.o {' '.join(str(p) for p in objects)}
bench.o: {ADV/'tests/medal_native.c'}
\t$(CC) $(CFLAGS) -c $< -o $@
medal.o: {ADV/'medal.c'} {ADV/'medal.h'} {ADV/'medal_flow.h'}
\t$(CC) $(CFLAGS) -c $< -o $@
medal-review.z64: N64_ROM_TITLE = "Master Medal review"
medal-review.z64: N64_ROM_SAVETYPE = none
medal-review.z64: medal-review.dfs
medal-review.dfs: $(wildcard filesystem/medals/*.sprite) filesystem/buildinfo.txt
-include $(wildcard *.d)
'''
    (OUT/'Makefile').write_text(mk)
    subprocess.run(['make','-C',str(OUT),'-j4','all'],check=True)
    (OUT/'pinned.json').write_text(json.dumps({str(p.relative_to(OUT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in objects},indent=2)+'\n')

def run(case,expanded):
    work=OUT/case;work.mkdir(parents=True,exist_ok=True);(work/'saves').mkdir(exist_ok=True)
    settings=work/'settings.bml';log=work/'runtime.log'
    settings.write_text(f'''Video
  Driver: Metal
  Quality: SD
  Shader: None
  Supersampling: false
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
  ExpansionPak: {'true' if expanded else 'false'}
Paths
  Saves: {work}/saves/
''')
    ares=ADV/'build/vi-direct-capture/Poketcg VI Capture.app/Contents/MacOS/ares'
    start=time.monotonic()
    with log.open('w') as output:
        process=subprocess.Popen([str(ares),'--settings-file',str(settings),'--no-file-prompt',str(OUT/'medal-review.z64')],stdout=output,stderr=subprocess.STDOUT)
        print('Owned medal Ares PID',process.pid,flush=True)
        try:
            while process.poll() is None and time.monotonic()-start<80:
                time.sleep(.2);data=log.read_text(errors='replace')
                if any(marker in data for marker in ('MEDAL NATIVE PASS','ASSERTION FAILED','EXCEPTION:')):break
        finally:
            process.terminate()
            try:process.wait(timeout=5)
            except subprocess.TimeoutExpired:process.kill();process.wait()
    data=log.read_text(errors='replace');assert 'MEDAL NATIVE PASS' in data,data[-3500:]
    frames=[];image=None
    for line in data.splitlines():
        if line.startswith('FRAMEBUFFER ') and line!='FRAMEBUFFER END':
            _,name,w,h=line.split();image=Image.new('RGBA',(int(w),int(h)))
        elif line.startswith('PIXELS '):
            _,x,y,raw=line.split();x=int(x);y=int(y)
            for i in range(0,len(raw),4):
                value=int(raw[i:i+4],16);image.putpixel((x+i//4,y),tuple(((value>>shift)&31)*255//31 for shift in (11,6,1))+(255,))
        elif line=='FRAMEBUFFER END':
            path=work/(name+'.png');image.save(path);frames.append({'file':path.name,'dimensions':list(image.size),'sha256':hashlib.sha256(path.read_bytes()).hexdigest()});image=None
    assert len(frames)==4,len(frames)
    proof=re.search(r'MEDAL NATIVE PASS ram=(\d+) width=(\d+) designs=(\d+) hero_texels=(\d+) collection_texels=(\d+) read_only=(\d+) held_fresh=(\d+) frames=(\d+) vi=(\d+) free_heap=(\d+)',data)
    assert proof,data[-2000:]
    result={'passed':True,'case':case,'method':'Completed N64 RDP framebuffer after fence; native input and real audio; private Ares with VI processing enabled. PNGs are raw framebuffer, not post-VI output.',
      'ram':int(proof[1]),'width':int(proof[2]),'designs':int(proof[3]),'exact_source_texels':int(proof[4]),'exact_collection_texels':int(proof[5]),'read_only':bool(int(proof[6])),
      'held_then_fresh_input':bool(int(proof[7])),'live_frames':int(proof[8]),'live_vi':int(proof[9]),'free_heap':int(proof[10]),'frames':frames,'wall_seconds':round(time.monotonic()-start,2)}
    (work/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--build',action='store_true');parser.add_argument('--case',choices=('4mib','8mib'));args=parser.parse_args()
    if args.build:build()
    if args.case:run(args.case,args.case=='8mib')
