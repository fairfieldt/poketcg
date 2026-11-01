#!/usr/bin/env python3
"""Build/run an isolated doorway-alignment A/B using the production N64 renderer.

The "before" meshes and textures are a copy of the previous generated assets
(pass --before DIR, default build/doorways-before).
"""
import argparse
import fcntl
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import time

ADV=Path(__file__).resolve().parents[1]
ROOT=ADV.parents[3]
OUT=ADV/'build/doorways-review'
BEFORE=ADV/'build/doorways-before'
sys.path.insert(0,str(ROOT/'port/reviews/expansion-pak'))
from capture import native_frames,clean_log
sys.path.insert(0,str(ROOT/'port/frontends/web/tools'))
from png import write_png


def build():
    OUT.mkdir(parents=True,exist_ok=True)
    source=ADV/'build/expansion'
    objects=[]
    for src in sorted(source.rglob('*.o')):
        relative=src.relative_to(source)
        if len(relative.parts)>1 and relative.parts[0] not in ('engine','game'):continue
        if len(relative.parts)==1 and src.name in ('main.o','video.o'):continue
        dst=OUT/'pinned'/relative
        dst.parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(src,dst);objects.append(dst)
    fs=OUT/'filesystem';fs.mkdir(exist_ok=True)
    for src in (source/'filesystem').iterdir():
        if src.name=='world':continue
        dst=fs/src.name
        if not dst.exists():dst.symlink_to(src,target_is_directory=src.is_dir())
    world=fs/'world';world.mkdir(exist_ok=True)
    generated=ROOT/'port/world3d/generated'
    for name in ('maps','lod','actors.l64'):
        dst=world/name
        if not dst.exists():dst.symlink_to(generated/name,target_is_directory=(generated/name).is_dir())
    if not (world/'textures').exists():
        (world/'textures').symlink_to(source/'filesystem/world/textures',target_is_directory=True)
    old=fs/'before';old.mkdir(exist_ok=True)
    if not (old/'maps').exists():(old/'maps').symlink_to(BEFORE/'maps',target_is_directory=True)
    oldtex=old/'textures';oldtex.mkdir(exist_ok=True)
    for src in sorted((BEFORE/'textures').glob('*.png')):
        target=oldtex/(src.stem+'.sprite')
        if not target.exists():
            subprocess.run(['/opt/libdragon/bin/mksprite','-f','RGBA16','-o',str(oldtex),str(src)],check=True,stdout=subprocess.DEVNULL)
    archive=ADV/'build/tiny3d-experiment/build/libt3d-poketcg.a'
    (OUT/'Makefile').write_text(f'''N64_INST := /opt/libdragon
export N64_INST
BUILD_DIR := .
N64_MKDFS_ROOT := filesystem
LIBDRAGON_PREVIEW := 2
include $(N64_INST)/include/n64.mk
CFLAGS += -I{ADV} -I{ROOT/'port/src'} -I{ROOT/'port/overworld'} -I{ROOT/'port/gameplay'} -I{ROOT/'port/world3d'} -I{source} -I{ADV/'build/tiny3d-experiment/src'} -DSCENE_T3D=1 -DADVENTURE_DIAGNOSTIC=9 -DADVENTURE_HIGH_RES=1 -DADVENTURE_SHOW_FPS=0 -DADVENTURE_VI_FILTER=FILTERS_RESAMPLE
all: doorways-review.z64
doorways-review.elf: bench.o video.o {' '.join(str(p) for p in objects)} {archive}
bench.o: {ADV/'tests/doorways_native.c'} {ADV/'main.c'} {generated/'world3d_maps.h'}
\t$(CC) $(CFLAGS) -c $< -o $@
video.o: {ADV/'video.c'}
\t$(CC) $(CFLAGS) -c $< -o $@
doorways-review.z64: N64_ROM_TITLE = "Doorway review"
doorways-review.z64: N64_ROM_SAVETYPE = none
doorways-review.z64: doorways-review.dfs
doorways-review.dfs: $(wildcard {generated/'maps/*.l64'})
-include $(wildcard *.d)
''')
    # The selected-capture define is a build input independent of source mtimes.
    (OUT/'bench.o').unlink(missing_ok=True)
    subprocess.run(['make','-C',str(OUT),'-j4','all'],check=True)


def run(ram):
    # The instrumented emulator's debug capture must run one instance at a time.
    lock=(OUT/'capture.lock').open('w')
    fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
    work=OUT/str(ram);work.mkdir(exist_ok=True)
    (work/'saves').mkdir(exist_ok=True)
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
  ExpansionPak: {'true' if ram==8 else 'false'}
Paths
  Saves: {work}/saves/
''')
    binary=ADV/'build/vi-direct-capture/Poketcg VI Capture.app/Contents/MacOS/ares'
    start=time.monotonic()
    with log.open('w') as stream:
        proc=subprocess.Popen([str(binary),'--settings-file',str(settings),'--no-file-prompt',str(OUT/'doorways-review.z64')],stdout=stream,stderr=subprocess.STDOUT)
        try:
            while proc.poll() is None and time.monotonic()-start<600:
                time.sleep(.3)
                with log.open('rb') as source:
                    source.seek(max(0,log.stat().st_size-4096));tail=source.read().decode(errors='replace')
                if any(s in tail for s in ('DOORWAYS NATIVE PASS','ASSERTION FAILED','EXCEPTION:')):break
        finally:
            proc.terminate()
            try:proc.wait(timeout=5)
            except subprocess.TimeoutExpired:proc.kill();proc.wait()
    text=clean_log(log.read_text(errors='replace'))
    assert 'DOORWAYS NATIVE PASS' in text,text[-3000:]
    output=ROOT/f'port/reviews/doorway-alignment/native-{ram}mib';output.mkdir(parents=True,exist_ok=True)
    expected=(320,240) if ram==4 else (480,360)
    frames=[]
    for name,w,h,rows in native_frames(text,expected):
        p=output/(name+'.png');write_png(p,w,h,rows)
        frames.append({'file':p.name,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()})
    assert len(frames)==38,len(frames)
    timings=[json.loads(line.removeprefix('DOORWAY_CASE ')) for line in text.splitlines() if line.startswith('DOORWAY_CASE ')]
    assert len(timings)==38,len(timings)
    result={'passed':True,'ram_mib':ram,'dimensions':expected,'rom_sha256':hashlib.sha256((OUT/'doorways-review.z64').read_bytes()).hexdigest(),'canonical_state_unchanged':True,'method':'Completed native RDP framebuffer using production renderer. Identical source actor poses and settled cameras; room meshes/textures differ. Timing is emulated CP0 cadence (8 samples), not physical hardware. All conditional source NPCs visible for inspection.','frames':frames,'cases':timings,'wall_seconds':round(time.monotonic()-start,2)}
    (output/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k not in ('frames','cases')},indent=2))


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build',action='store_true')
    parser.add_argument('--ram-mib',type=int,choices=(4,8))
    args=parser.parse_args()
    if args.build:build()
    if args.ram_mib:run(args.ram_mib)
