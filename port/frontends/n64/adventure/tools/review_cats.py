"""Build an isolated cat encounter review and capture the production N64 renderer."""
from pathlib import Path
import subprocess,sys,time,json
ADV=Path(__file__).resolve().parents[1];ROOT=ADV.parents[3];OUT=ADV/'build/cat-review'
sys.path.insert(0,str(ROOT/'port/reviews/expansion-pak'))
from capture import native_frames,clean_log
sys.path.insert(0,str(ROOT/'port/frontends/web/tools'))
from png import write_png

def main():
    subprocess.run(['make','-C',str(ADV),'-j6'],check=True)
    OUT.mkdir(parents=True,exist_ok=True)
    source=ADV/'build';fs=OUT/'filesystem'
    if not fs.exists():fs.symlink_to(source/'filesystem',target_is_directory=True)
    objects=[p for folder in ('engine','game') for p in sorted((source/folder).rglob('*.o'))]
    objects += [p for p in sorted(source.glob('*.o')) if p.name!='main.o']
    archive=source/'tiny3d-experiment/build/libt3d-poketcg.a'
    (OUT/'Makefile').write_text(f'''N64_INST := /opt/libdragon
export N64_INST
BUILD_DIR := .
N64_MKDFS_ROOT := filesystem
LIBDRAGON_PREVIEW := 2
include /opt/libdragon/include/n64.mk
CFLAGS += -I{ADV} -I{ROOT/'port/src'} -I{ROOT/'port/overworld'} -I{ROOT/'port/gameplay'} -I{ROOT/'port/world3d'} -I{source} -I{source/'tiny3d-experiment/src'} -DSCENE_T3D=1 -DADVENTURE_DIAGNOSTIC=0 -DADVENTURE_HIGH_RES=0 -DADVENTURE_SHOW_FPS=1 -DADVENTURE_VI_FILTER=FILTERS_RESAMPLE
all: cats.z64
cats.elf: bench.o {' '.join(map(str,objects))} {archive}
bench.o: {ADV/'tests/cat_duo_native.c'} {ADV/'main.c'} {ROOT/'port/world3d/generated/world3d_maps.h'}
\t$(CC) $(CFLAGS) -c $< -o $@
cats.z64: N64_ROM_TITLE = "Cat encounter review"
cats.z64: N64_ROM_SAVETYPE = none
cats.z64: cats.dfs
.PHONY: force-filesystem
force-filesystem:
cats.dfs: force-filesystem
-include $(wildcard *.d)
''')
    subprocess.run(['make','-C',str(OUT),'-j6'],check=True)
    (OUT/'saves').mkdir(exist_ok=True)
    settings=OUT/'settings.bml'
    settings.write_text(f'''Video
  Driver: Metal
  Quality: SD
  Shader: None
Input
  Driver: SDL
  Defocus: Allow
Audio
  Driver: SDL
General
  HomebrewMode: true
  NoFilePrompt: true
Nintendo64
  ExpansionPak: false
Paths
  Saves: {OUT}/saves/
''')
    binary=source/'vi-direct-capture/Poketcg VI Capture.app/Contents/MacOS/ares'
    log=OUT/'runtime.log';start=time.monotonic()
    with log.open('w') as stream:
        proc=subprocess.Popen([str(binary),'--settings-file',str(settings),'--no-file-prompt',str(OUT/'cats.z64')],stdout=stream,stderr=subprocess.STDOUT)
        try:
            while proc.poll() is None and time.monotonic()-start<180:
                time.sleep(.25)
                with log.open('rb') as f:f.seek(max(0,log.stat().st_size-4096));tail=f.read().decode(errors='replace')
                if any(s in tail for s in ('CAT DUO NATIVE PASS','ASSERTION FAILED','EXCEPTION:','ADVENTURE ERROR:')):break
        finally:
            proc.terminate()
            try:proc.wait(timeout=5)
            except subprocess.TimeoutExpired:proc.kill();proc.wait()
    text=clean_log(log.read_text(errors='replace'));assert 'CAT DUO NATIVE PASS' in text,text[-3000:]
    review=ROOT/'port/reviews/cat-duo/native';review.mkdir(exist_ok=True)
    names=[]
    for name,w,h,rows in native_frames(text,(320,240)):
        write_png(review/(name+'.png'),w,h,rows);names.append(name)
    assert set(names)=={'room','tuxedo','tabby'},names
    (review/'validation.json').write_text(json.dumps(dict(renderer='production N64 Tiny3D in ares',ram_mib=4,frames=names),indent=2)+'\n')
    print('Cat native PASS:',', '.join(names))
if __name__=='__main__':main()
