#!/usr/bin/env python3
"""Build private playable capture segments without touching game main/Makefile."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import time

ADV=Path(__file__).resolve().parents[1];ROOT=ADV.parents[3]
SEGMENTS=('intro','ronald','clubs','ending','amy')
BASE='2dda70b3'
TAG=''
PIN_SOURCE=None
RONALD_SHA='990334314b7bbec9a49b393fb6bd8bd2638e1326f01b383cf5ea42a2f9aba5b4'
AMY_SHA='8ae3915c0523ebf59238aa1302caf75a7b3c88ae9325eb3a24e19e6b537044ba'

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def build(segment,preroll,ack):
    work=ADV/'build'/('trailer-'+(TAG+'-' if TAG else '')+segment);work.mkdir(parents=True,exist_ok=True);source=PIN_SOURCE or ADV/'build/expansion';objects=[]
    main_snapshot=work/'main-snapshot.c';shutil.copy2(ADV/'main.c',main_snapshot)
    (work/'generated').mkdir(exist_ok=True)
    for header in (ROOT/'port/world3d/generated').glob('*.h'):
        shutil.copy2(header,work/'generated'/header.name)
    shutil.copy2(ROOT/'port/world3d/world3d.h',work/'world3d.h')
    for folder in ('engine','game'):
        for src in sorted((source/folder).rglob('*.o')):
            dst=work/'pinned'/src.relative_to(source);dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(src,dst);objects.append(dst)
    for src in sorted(source.glob('*.o')):
        if src.name in ('main.o','video.o'):continue
        dst=work/'pinned'/src.name;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(src,dst);objects.append(dst)
    assert objects,'Build the normal expansion ROM before the private capture segments'
    fs=work/'filesystem';fs.mkdir(exist_ok=True)
    for src in (source/'filesystem').iterdir():
        dst=fs/src.name
        if not dst.exists():dst.symlink_to(src,target_is_directory=src.is_dir())
    character_files={}
    for directory in ('maps','lod'):
        for original in sorted((ROOT/'port/world3d/generated'/directory).glob('actor_*.l64')):
            staged=fs/'world'/directory/original.name
            assert staged.is_file() and sha(staged)==sha(original), ('stale capture model',staged)
            character_files[str(staged.relative_to(fs))]=sha(staged)
            data=original.read_bytes();count=struct.unpack_from('>H',data,6)[0]
            for i in range(count):
                name=data[8+32*i:40+32*i].split(b'\0')[0].decode()
                texture=fs/'world/textures'/name
                character_files[str(texture.relative_to(fs))]=sha(texture)
    (fs/'trailer').mkdir(exist_ok=True)
    checkpoint=ADV/'build/trailer-ronald/ronald.duel';assert checkpoint.stat().st_size==1660 and sha(checkpoint)==RONALD_SHA
    # The staging directory is also named trailer-ronald, but the verified
    # source checkpoint stays at its existing root. The DFS copy is separate.
    shutil.copy2(checkpoint,fs/'trailer/ronald.duel')
    amy_checkpoint=ADV/'build/trailer-amy/amy.duel';assert amy_checkpoint.stat().st_size==1660 and sha(amy_checkpoint)==AMY_SHA
    shutil.copy2(amy_checkpoint,fs/'trailer/amy.duel')
    archive=ADV/'build/tiny3d-experiment/build/libt3d-poketcg.a';assert archive.is_file()
    index=SEGMENTS.index(segment);rom='trailer-'+segment
    mk=f'''N64_INST := /opt/libdragon
export N64_INST
BUILD_DIR := .
N64_MKDFS_ROOT := filesystem
LIBDRAGON_PREVIEW := 2
include $(N64_INST)/include/n64.mk
CFLAGS += -I{work} -I{ADV} -I{ROOT/'port/src'} -I{ROOT/'port/overworld'} -I{ROOT/'port/gameplay'} -I{ROOT/'port/world3d'} -I{source} -I{ADV/'build/tiny3d-experiment/src'} -DTRAILER_MAIN_SOURCE=\\\"{main_snapshot}\\\" -DSCENE_T3D=1 -DADVENTURE_DIAGNOSTIC=0 -DADVENTURE_HIGH_RES=1 -DADVENTURE_SHOW_FPS=0 -DADVENTURE_VI_FILTER=FILTERS_RESAMPLE -DTRAILER_SEGMENT={index} -DTRAILER_PREROLL_VI={preroll} -DTRAILER_ACK_VI={ack}
N64_LDFLAGS += --wrap=adventure_intro_play --wrap=n64_video_present --wrap=tcg_set_host --wrap=joypad_get_inputs --wrap=joypad_get_buttons_pressed --wrap=joypad_get_buttons_held --wrap=joypad_is_connected
all: {rom}.z64
{rom}.elf: trailer-main.o video.o trailer-ronald.o trailer-amy.o {' '.join(str(p) for p in objects)} {archive}
trailer-main.o: {ADV/'tests/trailer_main.c'} {main_snapshot}
\t$(CC) $(CFLAGS) -c $< -o $@
video.o: {ADV/'video.c'}
\t$(CC) $(CFLAGS) -c $< -o $@
trailer-ronald.o: {ADV/'tests/trailer_ronald.c'}
\t$(CC) $(CFLAGS) -c $< -o $@
trailer-amy.o: {ADV/'tests/trailer_amy.c'}
\t$(CC) $(CFLAGS) -c $< -o $@
{rom}.z64: N64_ROM_TITLE = "TCG64 trailer {segment}"
{rom}.z64: N64_ROM_SAVETYPE = sram256k
{rom}.z64: {rom}.dfs
.PHONY: force-capture-filesystem
force-capture-filesystem:
{rom}.dfs: force-capture-filesystem $(wildcard filesystem/trailer/*)
-include $(wildcard *.d)
'''
    path=work/'Makefile';old=path.read_text() if path.exists() else ''
    if old!=mk:
        path.write_text(mk)
        for filename in ('trailer-main.o','video.o'):
            obj=work/filename
            if obj.exists():obj.unlink()
    subprocess.run(['make','-C',str(work),'-j4','all'],check=True)
    # Verify the packed ROM, not just the staging symlink: a cached DFS can
    # otherwise silently retain the previous cast while rebuilding the ROM.
    for relative,expected in character_files.items():
        extracted=subprocess.check_output(['/opt/libdragon/bin/dumpdfs','-e',str(work/(rom+'.z64')),relative])
        assert hashlib.sha256(extracted).hexdigest()==expected, ('stale ROM asset',relative)
    manifest={'segment':segment,'rom':str(work/(rom+'.z64')),'rom_sha256':sha(work/(rom+'.z64')),
      'production_base_commit':subprocess.check_output(['git','rev-parse',BASE],cwd=ROOT,text=True).strip(),
      'runtime':'Production main.c initialization/private hosts; DIAGNOSTIC0;480x360;SHOW_FPS0; original engine and artwork',
      'silent_preroll_vi':preroll,'story_ack_vi':ack,'ronald_untouched_checkpoint_sha256':RONALD_SHA,'amy_untouched_checkpoint_sha256':AMY_SHA,
      'private_source':{str(p.relative_to(ROOT)):sha(p) for p in (ADV/'tests/trailer_main.c',ADV/'tests/trailer_ronald.c',ADV/'tests/trailer_amy.c',main_snapshot,ADV/'video.c')},
      'generated_headers':{str(p.relative_to(work)):sha(p) for p in (work/'generated').glob('*') if p.is_file()},
      'character_files':character_files,
      'embedded_character_files_verified':True,
      'pinned_objects':{str(p.relative_to(work)):sha(p) for p in objects}}
    (work/'build.json').write_text(json.dumps(manifest,indent=2)+'\n');print(json.dumps({k:v for k,v in manifest.items() if k!='pinned_objects'},indent=2))

def probe(segment):
    work=ADV/'build'/('trailer-'+(TAG+'-' if TAG else '')+segment)/'flow-local';work.mkdir(parents=True,exist_ok=True);(work/'saves').mkdir(exist_ok=True)
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
  ExpansionPak: true
Paths
  Saves: {work}/saves/
''')
    ares=ADV/'build/vi-direct-capture/Poketcg VI Capture.app/Contents/MacOS/ares';start=time.monotonic()
    with log.open('w') as output:
        process=subprocess.Popen([str(ares),'--settings-file',str(settings),'--no-file-prompt',str(work.parent/('trailer-'+segment+'.z64'))],stdout=output,stderr=subprocess.STDOUT)
        print('Owned trailer Ares PID',process.pid,flush=True)
        try:
            while process.poll() is None and time.monotonic()-start<(210 if segment=='intro' else 100):
                time.sleep(.25);data=log.read_text(errors='replace')
                if any(marker in data for marker in ('TRAILER END '+segment,'ASSERTION FAILED','EXCEPTION:','ADVENTURE ERROR:')):break
        finally:
            process.terminate()
            try:process.wait(timeout=5)
            except subprocess.TimeoutExpired:process.kill();process.wait()
    data=log.read_text(errors='replace');passed='TRAILER END '+segment in data
    result={'passed':passed,'segment':segment,'wall_seconds':round(time.monotonic()-start,2),'rom_sha256':sha(work.parent/('trailer-'+segment+'.z64')),
      'markers':[line for line in data.splitlines() if line.startswith('TRAILER ')]}
    (work/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2));assert passed,data[-4000:]

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--segment',choices=(*SEGMENTS,'all'),default='all');parser.add_argument('--preroll-vi',type=int,default=480);parser.add_argument('--ack-vi',type=int,default=180);parser.add_argument('--probe',choices=SEGMENTS);parser.add_argument('--tag',default='');parser.add_argument('--base-ref',default=BASE);parser.add_argument('--pin-source',type=Path);args=parser.parse_args()
    assert not args.tag or args.tag.replace('-','').isalnum()
    TAG=args.tag;BASE=args.base_ref;PIN_SOURCE=args.pin_source.resolve() if args.pin_source else None
    assert args.preroll_vi>=0 and args.ack_vi>=60
    if args.probe:probe(args.probe)
    else:
        for name in SEGMENTS if args.segment=='all' else (args.segment,):build(name,args.preroll_vi,args.ack_vi)
