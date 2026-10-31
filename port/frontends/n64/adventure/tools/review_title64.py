#!/usr/bin/env python3
"""Isolated native title64 art, original-scene/footer parity and input/audio gate."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import time
from PIL import Image

ADV=Path(__file__).resolve().parents[1];ROOT=ADV.parents[3];OUT=ADV/'build/title64-review'
REFERENCE_COMMIT='1ededb5d53411f26d5741af81337262552edebc6'

def build(reference_commit):
    OUT.mkdir(parents=True,exist_ok=True);objects=[]
    for source in sorted((ADV/'build/engine').rglob('*.o')):
        dest=OUT/'pinned'/source.relative_to(ADV/'build');dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,dest);objects.append(dest)
    for name in ('audio.o','wav64_vadpcm_fixed.o','rspq_fixed.o'):
        source=ADV/'build'/name;dest=OUT/'pinned'/name;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,dest);objects.append(dest)
    original=subprocess.check_output(['git','show',f'{reference_commit}:port/frontends/n64/adventure/intro.c'],cwd=ROOT)
    assert b'title64' not in original,'Reference HEAD already contains new title; use the original pre-integration commit for parity'
    (OUT/'intro-reference.c').write_bytes(original)
    fs=OUT/'filesystem';fs.mkdir(exist_ok=True)
    for name in ('audio','intro'):
        target=fs/name
        if not target.exists():target.symlink_to(ADV/'build/filesystem'/name,target_is_directory=True)
    (fs/'title64').mkdir(exist_ok=True)
    for name in ('logo-320.png','logo-480.png'):
        subprocess.run(['/opt/libdragon/bin/mksprite','-f','RGBA16','-o',str(fs/'title64'),str(ADV/'assets/title64'/name)],check=True)
    assert not (fs/'title64/source.sprite').exists()
    mk=f'''N64_INST := /opt/libdragon
export N64_INST
BUILD_DIR := .
N64_MKDFS_ROOT := filesystem
LIBDRAGON_PREVIEW := 2
include $(N64_INST)/include/n64.mk
CFLAGS += -I{ADV} -I{ROOT/'port/src'} -DADVENTURE_DIAGNOSTIC=9 -DADVENTURE_SHOW_FPS=0
N64_LDFLAGS += --wrap=joypad_get_buttons_pressed --wrap=adventure_audio_event
all: title64-review.z64
title64-review.elf: bench.o intro-reference.o {' '.join(str(p) for p in objects)}
bench.o: {ADV/'tests/title64_native.c'}
\t$(CC) $(CFLAGS) -c $< -o $@
intro-reference.o: intro-reference.c
\t$(CC) $(CFLAGS) -Dadventure_intro_play=title64_reference_play -Dadventure_intro_preview=title64_reference_preview -Dadventure_intro_check=title64_reference_check -c $< -o $@
title64-review.z64: N64_ROM_TITLE = "Pokemon TCG64 review"
title64-review.z64: N64_ROM_SAVETYPE = none
title64-review.z64: title64-review.dfs
title64-review.dfs: $(wildcard filesystem/title64/*.sprite)
-include $(wildcard *.d)
'''
    (OUT/'Makefile').write_text(mk);subprocess.run(['make','-C',str(OUT),'-j4','all'],check=True)
    hashes={str(p.relative_to(OUT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in objects}
    hashes['original_intro_sha256']=hashlib.sha256(original).hexdigest();hashes['reference_commit']=reference_commit
    (OUT/'pinned.json').write_text(json.dumps(hashes,indent=2)+'\n')

def run(case):
    work=OUT/case;work.mkdir(parents=True,exist_ok=True);(work/'saves').mkdir(exist_ok=True);settings=work/'settings.bml';log=work/'runtime.log'
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
  ExpansionPak: {'true' if case=='8mib' else 'false'}
Paths
  Saves: {work}/saves/
''')
    ares=ADV/'build/vi-direct-capture/Poketcg VI Capture.app/Contents/MacOS/ares';start=time.monotonic()
    with log.open('w') as output:
        process=subprocess.Popen([str(ares),'--settings-file',str(settings),'--no-file-prompt',str(OUT/'title64-review.z64')],stdout=output,stderr=subprocess.STDOUT)
        print('Owned title64 Ares PID',process.pid,flush=True)
        try:
            while process.poll() is None and time.monotonic()-start<70:
                time.sleep(.2);data=log.read_text(errors='replace')
                if any(marker in data for marker in ('TITLE64 NATIVE PASS','ASSERTION FAILED','EXCEPTION:')):break
        finally:
            process.terminate()
            try:process.wait(timeout=5)
            except subprocess.TimeoutExpired:process.kill();process.wait()
    data=log.read_text(errors='replace');assert 'TITLE64 NATIVE PASS' in data,data[-3500:]
    frames=[]
    for line in data.splitlines():
        if line.startswith('FRAMEBUFFER ') and line!='FRAMEBUFFER END':
            _,name,w,h=line.split();image=Image.new('RGBA',(int(w),int(h)))
        elif line.startswith('PIXELS '):
            _,x,y,raw=line.split();x=int(x);y=int(y)
            for i in range(0,len(raw),4):
                value=int(raw[i:i+4],16);image.putpixel((x+i//4,y),tuple(((value>>shift)&31)*255//31 for shift in (11,6,1))+(255,))
        elif line=='FRAMEBUFFER END':
            path=work/(name+'.png');image.save(path);frames.append({'file':path.name,'dimensions':list(image.size),'sha256':hashlib.sha256(path.read_bytes()).hexdigest()})
    assert len(frames)==3
    proof=re.search(r'TITLE64 NATIVE PASS ram=(\d+) width=(\d+) opening_pixels=(\d+) footer_pixels=(\d+) input_cases=3 source_orbs=1 audio=1 readonly=1 free_heap=(\d+)',data);assert proof
    result={'passed':True,'case':case,'ram':int(proof[1]),'width':int(proof[2]),'opening_exact_pixels':int(proof[3]),'copyright_exact_pixels':int(proof[4]),
      'input_cases':3,'source_orbs':True,'real_audio':True,'canonical_memory_unchanged':True,'free_heap':int(proof[5]),'frames':frames,'wall_seconds':round(time.monotonic()-start,2),
      'method':'Completed raw N64 RDP framebuffer after fences in private Ares. Original3 opening scenes and copyright region compare pixel-exact against prior intro.c in same ELF. PNGs are before VI output filtering.'}
    (work/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--build',action='store_true');parser.add_argument('--case',choices=('4mib','8mib'));parser.add_argument('--reference',default=REFERENCE_COMMIT);args=parser.parse_args()
    if args.build:build(args.reference)
    if args.case:run(args.case)
