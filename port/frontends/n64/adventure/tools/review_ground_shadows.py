#!/usr/bin/env python3
"""Private actual-renderer contact shadow/depth/occlusion gate."""
from pathlib import Path
import argparse
import hashlib
import json
import shutil
import subprocess
import time
from PIL import Image,ImageChops

ADV=Path(__file__).resolve().parents[1]
ROOT=ADV.parents[3]
OUT=ADV/'build/ground-shadow-review'

def build():
    OUT.mkdir(parents=True,exist_ok=True)
    source=ADV/'build/expansion';objects=[]
    for folder in ('engine','game'):
        for src in sorted((source/folder).rglob('*.o')):
            dst=OUT/'pinned'/src.relative_to(source);dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(src,dst);objects.append(dst)
    for src in sorted(source.glob('*.o')):
        if src.name in ('main.o','scene.o','video.o'):continue
        dst=OUT/'pinned'/src.name;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(src,dst);objects.append(dst)
    fs=OUT/'filesystem';fs.mkdir(exist_ok=True)
    for src in (source/'filesystem').iterdir():
        dst=fs/src.name
        if not dst.exists():dst.symlink_to(src,target_is_directory=src.is_dir())
    scene=(ADV/'scene.c').read_text();needle='SOM_ZMODE_DECAL|SOM_Z_COMPARE);'
    assert scene.count(needle)==1
    scene='extern int shadow_review_disable_depth;\n'+scene.replace(needle,'shadow_review_disable_depth?0:(SOM_ZMODE_DECAL|SOM_Z_COMPARE));')
    (OUT/'scene-review.c').write_text(scene)
    archive=ADV/'build/tiny3d-experiment/build/libt3d-poketcg.a'
    (OUT/'Makefile').write_text(f'''N64_INST := /opt/libdragon
export N64_INST
BUILD_DIR := .
N64_MKDFS_ROOT := filesystem
LIBDRAGON_PREVIEW := 2
include $(N64_INST)/include/n64.mk
CFLAGS += -I{ADV} -I{ROOT/'port/src'} -I{ROOT/'port/overworld'} -I{ROOT/'port/gameplay'} -I{ROOT/'port/world3d'} -I{source} -I{ADV/'build/tiny3d-experiment/src'} -DSCENE_T3D=1 -DADVENTURE_DIAGNOSTIC=9 -DADVENTURE_HIGH_RES=1 -DADVENTURE_SHOW_FPS=0 -DADVENTURE_VI_FILTER=FILTERS_RESAMPLE
all: shadow-review.z64
shadow-review.elf: bench.o scene-review.o video.o {' '.join(str(p) for p in objects)} {archive}
bench.o: {ADV/'tests/ground_shadows.c'} {ADV/'main.c'}
\t$(CC) $(CFLAGS) -c $< -o $@
scene-review.o: scene-review.c {ADV/'scene_packed.inc'}
\t$(CC) $(CFLAGS) -c $< -o $@
video.o: {ADV/'video.c'}
\t$(CC) $(CFLAGS) -c $< -o $@
shadow-review.z64: N64_ROM_TITLE = "Ground shadow review"
shadow-review.z64: N64_ROM_SAVETYPE = none
shadow-review.z64: shadow-review.dfs
-include $(wildcard *.d)
''')
    subprocess.run(['make','-C',str(OUT),'-j4','all'],check=True)
    (OUT/'pinned.json').write_text(json.dumps({str(p.relative_to(OUT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in objects},indent=2)+'\n')

def run(case,expanded):
    work=OUT/case;work.mkdir(parents=True,exist_ok=True);(work/'saves').mkdir(exist_ok=True)
    settings=work/'settings.bml';log=work/'runtime.log'
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
  ExpansionPak: {'true' if expanded else 'false'}
Paths
  Saves: {work}/saves/
''')
    ares=ADV/'build/vi-direct-capture/Poketcg VI Capture.app/Contents/MacOS/ares'
    started=time.monotonic()
    with log.open('w') as output:
        process=subprocess.Popen([str(ares),'--settings-file',str(settings),'--no-file-prompt',str(OUT/'shadow-review.z64')],stdout=output,stderr=subprocess.STDOUT)
        print('Owned shadow Ares PID',process.pid,flush=True)
        try:
            while process.poll() is None and time.monotonic()-started<120:
                time.sleep(.2);data=log.read_text(errors='replace')
                if any(marker in data for marker in ('GROUND SHADOW PASS','ASSERTION FAILED','EXCEPTION:')):break
        finally:
            process.terminate()
            try:process.wait(timeout=5)
            except subprocess.TimeoutExpired:process.kill();process.wait()
    data=log.read_text(errors='replace');passed='GROUND SHADOW PASS' in data
    frames=[];image=None
    for line in data.splitlines():
        if line.startswith('FRAMEBUFFER ') and line!='FRAMEBUFFER END':
            _,name,w,h=line.split();image=Image.new('RGB',(int(w),int(h)))
        elif line.startswith('PIXELS '):
            _,x,y,raw=line.split();x=int(x);y=int(y)
            for i in range(0,len(raw),4):
                value=int(raw[i:i+4],16);image.putpixel((x+i//4,y),tuple(((value>>shift)&31)*255//31 for shift in (11,6,1)))
        elif line=='FRAMEBUFFER END':
            path=work/(name+'.png');image.save(path);frames.append(path.name);image=None
    result={'passed':passed,'case':case,'frames':frames,'wall_seconds':round(time.monotonic()-started,2),'proof':[s for s in data.splitlines() if s.startswith(('GROUND SHADOW PASS','SHADOW COVERAGE'))]}
    (work/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2));assert passed,data[-3000:]

def verify_vi(folder):
    """Reject the preceding actor-hidden reference accidentally being scanned.

    The shadow alpha is64/255; full missing actors produce a much larger
    difference than the paired contact-only changes (including VI filtering).
    These broad bounds complement inspection, rather than replace depth tests.
    """
    pairs=[]
    for name in ('walk-0','walk-3','walk-7','lab','sam'):
        before=Image.open(folder/'post-vi'/(name+'-before.png')).convert('RGB')
        after=Image.open(folder/'post-vi'/(name+'-after.png')).convert('RGB')
        delta=ImageChops.difference(before,after)
        changed=sum(pixel!=(0,0,0) for pixel in delta.get_flattened_data())
        maximum=max(channel[1] for channel in delta.getextrema())
        assert maximum<=80 and changed<before.width*before.height*.02,(name,changed,maximum)
        pairs.append({'pair':name,'changed_post_vi_pixels':changed,'max_rgb_difference':maximum,'bounds':delta.getbbox()})
    (folder/'pair-checks.json').write_text(json.dumps(pairs,indent=2)+'\n')
    print('Contact-only VI pairs PASS:',folder)

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--build',action='store_true');parser.add_argument('--case',choices=('4mib','8mib'));parser.add_argument('--verify-vi',type=Path);args=parser.parse_args()
    if args.build:build()
    if args.case:run(args.case,args.case=='8mib')
    if args.verify_vi:verify_vi(args.verify_vi)
