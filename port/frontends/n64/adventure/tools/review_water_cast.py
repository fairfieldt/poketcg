#!/usr/bin/env python3
"""Stage and capture an isolated Water cast A/B with production N64 rendering.

No canonical model, world asset or production ROM is written. Each option and
its generated gameplay LOD is pinned privately before serial emulator runs.
"""
import argparse
import copy
import fcntl
import hashlib
import json
import re
from pathlib import Path
import shutil
import subprocess
import sys
import time

ADV=Path(__file__).resolve().parents[1]
ROOT=ADV.parents[3]
OUT=ADV/'build/water-cast-review'
BEFORE=ADV/'build/water-cast-before'
OPTIONS=ADV/'build/water-cast-options'
AFTER=OUT/'after'
FAMILIES=('mark','amy','joshua','swimmer')
SLUGS=('mark','amy','joshua','swimmer','swimmer_palette2','amy__lying')
sys.path.insert(0,str(ROOT/'port/reviews/expansion-pak'))
from capture import native_frames,clean_log
sys.path.insert(0,str(ROOT/'port/world3d/tools'))
import build_world as world
import sync_characters as sync
sys.path.insert(0,str(ROOT/'port/frontends/web/tools'))
from png import write_png


def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def stage():
    OUT.mkdir(parents=True,exist_ok=True)
    (AFTER/'maps').mkdir(parents=True,exist_ok=True)
    (AFTER/'textures').mkdir(exist_ok=True)
    frozen={}
    for family in FAMILIES:
        source=OPTIONS/(family+'-assets')
        target=OUT/'inputs'/family
        for relative in [Path('models')/family/'scene.l64',Path('models')/family/'scene.json',
                         Path('models')/family/'complete.json']:
            assert (source/relative).exists(),source/relative
        if target.exists():shutil.rmtree(target)
        paths=[p for p in sorted(source.rglob('*')) if p.is_file() and
               ((p.relative_to(source).parts[0]=='models' and p.name in ('scene.l64','scene.json','complete.json')) or
                (p.relative_to(source).parts[0]=='textures' and p.suffix=='.png'))]
        frozen[family]={str(p.relative_to(source)):digest(p) for p in paths}
        for src in paths:
            dst=target/src.relative_to(source);dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(src,dst)
        assert all(digest(target/rel)==sha for rel,sha in frozen[family].items()),family
    baseline=json.loads((BEFORE/'world.json').read_text())
    mapping={c['slug']:c for c in baseline['individual_characters']}
    old_out,old_tex,old_sync=world.OUT,world.TEX,sync.OUT
    world.OUT=AFTER;world.TEX=AFTER/'textures';sync.OUT=AFTER;world.textures.clear()
    records=[]
    try:
        for slug in SLUGS:
            old=mapping[slug];family=old['family'];assets=OUT/'inputs'/family
            folder=assets/'models'/family
            if slug=='swimmer_palette2':folder=folder/'variants'/slug
            elif slug=='amy__lying':folder=folder/'poses/lying'
            export=sync.source_export(folder,slug,shared_textures=assets/'textures',
                       marker=assets/'models'/family/'complete.json',family=family,
                       appearance=slug if slug=='swimmer_palette2' else family,
                       pose='lying' if slug=='amy__lying' else None)
            assert export is not None,slug
            record=sync.imported_record(folder,slug,old['model'],export)
            current=dict(old,**record);records.append(current)
            baseline['meshes']['actor_'+slug]=record
        replacements={r['slug']:r for r in records}
        baseline['individual_characters']=[replacements.get(c['slug'],c) for c in baseline['individual_characters']]
        (AFTER/'world.json').write_text(json.dumps(baseline,indent=2)+'\n')
    finally:world.OUT,world.TEX,sync.OUT=old_out,old_tex,old_sync
    worker=OUT/'lod_worker.py'
    worker.write_text(f'''import sys,json,hashlib
from pathlib import Path
sys.path.insert(0,{str(ROOT/'port/world3d/tools')!r})
import build_character_lods as lod
out=Path({str(AFTER)!r})
world=json.loads((out/'world.json').read_text())
slugs={SLUGS!r}
records=[]
for character in world['individual_characters']:
 if character['slug'] not in slugs:continue
 source=out/character['file'];textures,batches=lod.read_l643(source)
 reduced=[lod.simplify(batch,.45) for batch in batches]
 target=out/'lod'/source.name;target.parent.mkdir(exist_ok=True)
 lod.write_mesh(target,textures,reduced)
 before=sum(len(b[5])//3 for b in batches);after=sum(len(b[5])//3 for b in reduced)
 records.append(dict(model=character['model'],slug=character['slug'],source=character['file'],source_sha256=lod.digest(source),file=str(target.relative_to(out)),sha256=lod.digest(target),full_triangles=before,lod_triangles=after,full_vertices=sum(len(b[4]) for b in batches),lod_vertices=sum(len(b[4]) for b in reduced)))
 print('PRIVATE LOD',character['slug'],before,'->',after,flush=True)
(out/'character-lods.json').write_text(json.dumps(dict(version=1,ratio=.45,models=records),indent=2)+'\\n')
lod.update_header(out)
''')
    with (OUT/'lod.log').open('w') as stream:
        subprocess.run(['/Applications/Blender.app/Contents/MacOS/Blender','--background','--factory-startup',
                        '--python-exit-code','1','--python',str(worker)],stdout=stream,stderr=subprocess.STDOUT,check=True)
    sys.path.insert(0,str(ROOT/'port/characters/tools'))
    from check_water_cast import surface_checks,triangles,components_and_topology
    def as_batches(meshes):
        return [dict(model=m,part=p,texture=t,pivot=pivot,vertices=vs,indices=ids)
                for m,p,t,pivot,vs,ids in meshes]
    surface_reports=[]
    for slug in SLUGS:
        _,full=world.read_l643(AFTER/'maps'/('actor_'+slug+'.l64'))
        _,low=world.read_l643(AFTER/'lod'/('actor_'+slug+'.l64'))
        full,low=as_batches(full),as_batches(low)
        head=lambda b:b['part']==0 and any(abs(v)>1e-6 for v in b['pivot'])
        assert [b for b in full if head(b)]==[b for b in low if head(b)],('Head LOD changed',slug)
        report={'model':slug,'head_batches_exact':True,
                'raw':surface_checks(triangles(low)),
                'packed':surface_checks(triangles(low,32))}
        groups={}
        for batch in low:groups.setdefault((batch['part'],tuple(batch['pivot'])),[]).append(batch)
        components=0
        for key,group in groups.items():
            raw=triangles(group);packed=triangles(group,32)
            connected,_=components_and_topology(raw)
            for component in connected:
                try:components_and_topology([packed[i] for i in component['indices']])
                except AssertionError as error:raise AssertionError((slug,key,'packed component topology',str(error)))
                components+=1
        report['closed_outward_packed_components']=components;surface_reports.append(report)
    (OUT/'lod-surface-checks.json').write_text(json.dumps({'passed':True,'method':'Persistent actual raw/packed surface checks; per-raw-component topology after exact native packing; full head batches preserved byte-exact.','models':surface_reports},indent=2)+'\n')
    actor_rows=[mapping[s] for s in SLUGS[:5]]
    names={'mark':'mark','amy':'amy','joshua':'joshua','swimmer':'sara','swimmer_palette2':'amanda'}
    (OUT/'water_review_assets.h').write_text('/* Private pinned runtime IDs. */\n#define WATER_ACTOR_COUNT 5\n'+
       'static const struct {const char *slug,*name;unsigned model;} water_actors[5]={\n'+
       ''.join(' {'+json.dumps(r['slug'])+','+json.dumps(names[r['slug']])+','+str(r['model'])+'},\n' for r in actor_rows)+'};\n')
    manifest={'source_commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
              'inputs':frozen,'full':{s:digest(AFTER/'maps'/('actor_'+s+'.l64')) for s in SLUGS},
              'lod':json.loads((AFTER/'character-lods.json').read_text()),
              'baseline_manifest_sha256':digest(BEFORE/'before-hashes.json'),
              'lod_builder_sha256':digest(ROOT/'port/world3d/tools/build_character_lods.py')}
    (OUT/'staged-inputs.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print(json.dumps({'staged':str(OUT),'source_commit':manifest['source_commit'],
                      'frozen_full':manifest['full'],'lods':[{k:r[k] for k in ('slug','full_triangles','lod_triangles')} for r in manifest['lod']['models']]},indent=2))


def sprites(source,meshes,destination):
    destination.mkdir(parents=True,exist_ok=True)
    names=set()
    for path in meshes:
        textures,_=world.read_l643(path);names.update(textures)
    for name in sorted(names):
        target=destination/name
        png=source/name.replace('.sprite','.png')
        assert png.is_file(),png
        if target.exists() and target.stat().st_mtime_ns>=png.stat().st_mtime_ns:continue
        subprocess.run(['/opt/libdragon/bin/mksprite','-f','RGBA16','-o',str(destination),str(png)],check=True,stdout=subprocess.DEVNULL)


def elf_footprint(path):
    size=subprocess.check_output(['/opt/libdragon/bin/mips64-elf-size',str(path)],text=True).splitlines()[1].split()
    sections=dict(zip(('text','data','bss'),map(int,size[:3])))
    headers=subprocess.check_output(['/opt/libdragon/bin/mips64-elf-readelf','-lW',str(path)],text=True)
    loads=[]
    for line in headers.splitlines():
        fields=line.split()
        if fields and fields[0]=='LOAD':loads.append((int(fields[2],16),int(fields[5],16)))
    assert loads,path
    start=min(a for a,n in loads);end=max(a+n for a,n in loads)
    return dict(file=str(path),sha256=digest(path),sections=sections,resident_sections=sum(sections.values()),
                load_base=start,load_end=end,resident_load_span=end-start)


def build(selected=False):
    assert (OUT/'staged-inputs.json').exists(),'Run --stage first'
    source=ADV/'build/expansion';objects=[]
    for src in sorted(source.rglob('*.o')):
        relative=src.relative_to(source)
        if len(relative.parts)>1 and relative.parts[0] not in ('engine','game'):continue
        if len(relative.parts)==1 and src.name in ('main.o','video.o'):continue
        dst=OUT/'pinned'/relative;dst.parent.mkdir(parents=True,exist_ok=True)
        if not dst.exists():shutil.copy2(src,dst)
        objects.append(dst)
    fs=OUT/'filesystem';fs.mkdir(exist_ok=True)
    for src in (source/'filesystem').iterdir():
        if src.name=='world':continue
        dst=fs/src.name
        if not dst.exists():dst.symlink_to(src,target_is_directory=src.is_dir())
    current=fs/'world';(current/'maps').mkdir(parents=True,exist_ok=True)
    generated=ROOT/'port/world3d/generated'
    shutil.copy2(generated/'maps/water_club.l64',current/'maps/water_club.l64')
    shutil.copy2(BEFORE/'actors.l64',current/'actors.l64')
    sprites(generated/'textures',[current/'maps/water_club.l64',current/'actors.l64'],current/'textures')
    for variant,root in [('before',BEFORE),('after',AFTER)]:
        staged=fs/'cast'/variant;meshes=[]
        for kind in ('maps','lod'):
            (staged/kind).mkdir(parents=True,exist_ok=True)
            for slug in SLUGS:
                path=root/kind/('actor_'+slug+'.l64');assert path.is_file(),path
                shutil.copy2(path,staged/kind/path.name);meshes.append(path)
        sprites(root/'textures',meshes,staged/'textures')
    (OUT/'generated').mkdir(exist_ok=True)
    for name in ('world3d_maps.h','character_lods.h'):shutil.copy2(BEFORE/name,OUT/'generated'/name)
    shutil.copy2(ROOT/'port/world3d/world3d.h',OUT/'world3d.h')
    archive=ADV/'build/tiny3d-experiment/build/libt3d-poketcg.a'
    pad_header=OUT/'water_resident_pad.h'
    if not pad_header.exists():pad_header.write_text('#define WATER_RESIDENT_PAD_BYTES 0\n')
    (OUT/'Makefile').write_text(f'''N64_INST := /opt/libdragon
export N64_INST
BUILD_DIR := .
N64_MKDFS_ROOT := filesystem
LIBDRAGON_PREVIEW := 2
include $(N64_INST)/include/n64.mk
N64_LDFLAGS += --undefined=production_entry --undefined=tcg_run_duel --undefined=ow_new_game --undefined=n64_duel_play
CFLAGS += -I{OUT} -I{ADV} -I{ROOT/'port/src'} -I{ROOT/'port/overworld'} -I{ROOT/'port/gameplay'} -I{ROOT/'port/world3d'} -I{source} -I{ADV/'build/tiny3d-experiment/src'} -DSCENE_T3D=1 -DADVENTURE_DIAGNOSTIC=0 -DADVENTURE_HIGH_RES=1 -DADVENTURE_SHOW_FPS=0 -DADVENTURE_VI_FILTER=FILTERS_RESAMPLE {'-DWATER_SELECTED_VI' if selected else ''}
all: water-cast.z64
water-cast.elf: bench.o video.o {' '.join(str(p) for p in objects)} {archive}
bench.o: {ADV/'tests/water_cast_native.c'} {ADV/'main.c'} {OUT/'water_review_assets.h'} {OUT/'water_resident_pad.h'}
\t$(CC) $(CFLAGS) -c $< -o $@
video.o: {ADV/'video.c'}
\t$(CC) $(CFLAGS) -c $< -o $@
water-cast.z64: N64_ROM_TITLE = "Water cast review"
water-cast.z64: N64_ROM_SAVETYPE = none
water-cast.z64: water-cast.dfs
water-cast.dfs: $(shell find filesystem -type f) $(shell find filesystem -type l)
-include $(wildcard *.d)
''')
    (OUT/'bench.o').unlink(missing_ok=True)
    subprocess.run(['make','-C',str(OUT),'-j4','all'],check=True)
    production=elf_footprint(source/'poketcg-expansion.elf')
    first=elf_footprint(OUT/'water-cast.elf')
    # Retain the actual normal production entry graph. Cover remaining
    # configuration/pinned-object differences conservatively, plus 16 KiB
    # for the private bench's extra code, labels and capture state.
    bench_margin=16*1024
    current_pad=int(re.search(r'WATER_RESIDENT_PAD_BYTES (\d+)',pad_header.read_text()).group(1))
    if not current_pad:shutil.copy2(OUT/'water-cast.elf',OUT/'resident-unpadded.elf')
    deficit=max(production['resident_sections']+bench_margin-first['resident_sections'],
                production['load_end']+bench_margin-first['load_end'],0)
    if deficit:
        current_pad+=((deficit+63)//64)*64
        pad_header.write_text(f'#define WATER_RESIDENT_PAD_BYTES {current_pad}\n')
        # macOS GNU make may compare only whole-second mtimes.
        (OUT/'bench.o').unlink(missing_ok=True)
        subprocess.run(['make','-C',str(OUT),'-j4','all'],check=True)
    resident=elf_footprint(OUT/'water-cast.elf')
    nm=subprocess.check_output(['/opt/libdragon/bin/mips64-elf-nm','-S','--defined-only',str(OUT/'water-cast.elf')],text=True)
    required=('production_entry','tcg_run_duel','ow_new_game','n64_duel_play','adventure_intro_play','adventure_name_player')
    symbols={name:next(line.strip() for line in nm.splitlines() if line.split()[-1]==name) for name in required}
    assert resident['resident_sections']>=production['resident_sections']+bench_margin
    assert resident['load_end']>=production['load_end']+bench_margin
    memory_proof=dict(method='Actual normal production main entry graph (DIAGNOSTIC=0) and duel/story roots retained. Any measured resident deficit plus a 16 KiB bench allowance is reserved as retained static BSS before rendering. Both allocatable section sum and LOAD end must meet the conservative floor.',
                      production=production,unpadded_reference=elf_footprint(OUT/'resident-unpadded.elf'),review_before_new_padding=first,review=resident,
                      required_symbols=symbols,bench_allowance_bytes=bench_margin,
                      static_resident_padding_bytes=current_pad,passed=True)
    (OUT/'resident-footprint.json').write_text(json.dumps(memory_proof,indent=2)+'\n')
    archive_name='selected-vi.z64' if selected else 'full-review.z64'
    shutil.copy2(OUT/'water-cast.z64',OUT/archive_name)
    (OUT/'build-config.json').write_text(json.dumps({'selected_vi':selected,'rom_file':str(OUT/archive_name),'rom_sha256':digest(OUT/'water-cast.z64'),'pinned_objects':{str(p.relative_to(OUT)):digest(p) for p in objects},'water_room_sha256':digest(current/'maps/water_club.l64'),'fallback_actors_sha256':digest(current/'actors.l64'),'resident_footprint':memory_proof},indent=2)+'\n')


def run(ram):
    OUT.mkdir(parents=True,exist_ok=True)
    # Share the established local native capture lock: never race Ares debug logs.
    lock=(ADV/'build/prop-remodel-review/capture.lock').open('w')
    fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
    work=OUT/str(ram);work.mkdir(exist_ok=True);(work/'saves').mkdir(exist_ok=True)
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
    config=json.loads((OUT/'build-config.json').read_text())
    rom_hash=digest(OUT/'water-cast.z64');input_hash=digest(OUT/'staged-inputs.json')
    assert rom_hash==config['rom_sha256']
    start=time.monotonic()
    with log.open('w') as stream:
        proc=subprocess.Popen([str(binary),'--settings-file',str(settings),'--no-file-prompt',str(OUT/'water-cast.z64')],stdout=stream,stderr=subprocess.STDOUT)
        try:
            while proc.poll() is None and time.monotonic()-start<900:
                time.sleep(.3)
                with log.open('rb') as source:
                    source.seek(max(0,log.stat().st_size-4096));tail=source.read().decode(errors='replace')
                if any(s in tail for s in ('WATER CAST NATIVE PASS','ASSERTION FAILED','EXCEPTION:')):break
        finally:
            proc.terminate()
            try:proc.wait(timeout=5)
            except subprocess.TimeoutExpired:proc.kill();proc.wait()
    text=clean_log(log.read_text(errors='replace'))
    expected=(320,240) if ram==4 else (480,360)
    count=6 if config['selected_vi'] else 30
    marker=f'WATER CAST NATIVE PASS frames={count} width={expected[0]} canonical_state_unchanged=1'
    assert rom_hash==digest(OUT/'water-cast.z64') and input_hash==digest(OUT/'staged-inputs.json'),'Pinned inputs changed during capture'
    assert marker in text,text[-3000:]
    modes=[{'width':int(w),'height':int(h),'ram':int(r),'highres':int(hi)} for w,h,r,hi in re.findall(r'^VIDEO MODE width=(\d+) height=(\d+) ram=(\d+) highres=(\d+)',text,re.M)]
    assert modes and all((m['width'],m['height'],m['ram'])==(*expected,ram*1024*1024) for m in modes),modes
    output=ROOT/f'port/reviews/water-cast/native-{ram}mib';output.mkdir(parents=True,exist_ok=True)
    accepted=None
    if (OUT/'accepted-art-baseline.json').exists():
        accepted=next(m['accepted_pixel_sha256'] for m in json.loads((OUT/'accepted-art-baseline.json').read_text())['modes'] if m['ram_mib']==ram)
    frames=[];pixel_matches=0
    for name,w,h,rows in native_frames(text,expected):
        p=output/(name+'.png');write_png(p,w,h,rows)
        pixel_sha=hashlib.sha256(b''.join(bytes(row) for row in rows)).hexdigest()
        if accepted is not None:
            assert pixel_sha==accepted[p.name],('Resident-code sweep changed accepted pixels',ram,p.name,pixel_sha,accepted[p.name])
            pixel_matches+=1
        frames.append({'file':p.name,'sha256':digest(p),'pixel_sha256':pixel_sha})
    timings=[json.loads(line.removeprefix('WATER_CASE ')) for line in text.splitlines() if line.startswith('WATER_CASE ')]
    expected_names={f'{name}-{detail}-{variant}' for name in ('mark','amy','joshua','sara','amanda') for detail in ('full','lod') for variant in ('before','after')}
    expected_names.update(f'{name}-{variant}' for name in ('water-lying','amy-dialogue','joshua-dialogue','sara-dialogue','amanda-dialogue') for variant in ('before','after'))
    if config['selected_vi']:
        expected_names={f'{name}-{variant}' for name in ('mark-full','amanda-lod','amy-dialogue') for variant in ('before','after')}
    assert len(frames)==len(timings)==count,(len(frames),len(timings))
    assert {Path(f['file']).stem for f in frames}=={t['name'] for t in timings}==expected_names
    result={'passed':True,'ram_mib':ram,'dimensions':expected,'rom_file':config.get('rom_file',str(OUT/'water-cast.z64')),'rom_sha256':rom_hash,'selected_vi':config['selected_vi'],
            'staged_inputs_sha256':input_hash,'resident_footprint':config.get('resident_footprint'),'accepted_art_pixel_matches':pixel_matches,'runtime_video_modes':modes,'canonical_state_unchanged':True,
            'method':'Actual completed production N64 RDP framebuffers. Six views per character show front/quarter/both profiles/back/walk. Full and actual gameplay LOD assets are separate cases. Source Water NPC anchors, standing/lying Amy and production conversation cameras are used. Timings are emulated CP0 cadence, not hardware measurements.',
            'frames':frames,'cases':timings,'wall_seconds':round(time.monotonic()-start,2)}
    (output/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k not in ('frames','cases')},indent=2))


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--stage',action='store_true');parser.add_argument('--build',action='store_true')
    parser.add_argument('--selected-vi',action='store_true');parser.add_argument('--ram-mib',type=int,choices=(4,8))
    args=parser.parse_args()
    if args.stage:stage()
    if args.build:build(args.selected_vi)
    if args.ram_mib:run(args.ram_mib)
