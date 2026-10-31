#!/usr/bin/env python3
"""Validate all six final capture cases, then build the resolution/face gallery.

Run only after every capture and both complete native validation suites finish.
Source PNGs are never edited. Review crops use the same fixed box in all cases.
"""
import argparse
import hashlib
import html
import json
from pathlib import Path
import re
import statistics
import subprocess
import unicodedata
from PIL import Image, ImageDraw, ImageFont

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
ADV=ROOT/'port/frontends/n64/adventure'
GAME_STAGES=['overworld','lab','lightning','dome','naming','dialogue','board','energy-overflow',
 'actions','hand-pokemon','hand-trainer','chosen-card','pokemon-rules-1','pokemon-rules-2',
 'pokemon-rules-3','trainer-rules','prize','result','start']
LEAD_STAGES=['mark-portrait','ronald-portrait','mason-portrait','imakuni-portrait','leads-room','leads-conversation']
FAMILIES=[('mark','Mark','Young and confident'),('ronald','Ronald','Knowing rival'),
 ('mason','Dr. Mason','Warm professor'),('imakuni','Imakuni?','Eccentric grin')]
HEAD_BOX=(75,95,200,225)
CASE_SPECS={
 'game480':('game-480x360-8mib-v3',480,360,8,GAME_STAGES),
 'game320':('game-320x240-4mib-v3',320,240,4,GAME_STAGES),
 'after480':('leads-after-480x360-8mib-v3',480,360,8,LEAD_STAGES),
 'before480':('leads-before-480x360-8mib-v3',480,360,8,LEAD_STAGES),
 'after320':('leads-after-320x240-4mib-v3',320,240,4,LEAD_STAGES),
 'before320':('leads-before-320x240-4mib-v3',320,240,4,LEAD_STAGES),
}
LABELS={'overworld':'Overworld','lab':"Mason’s lab",'lightning':'Lightning Club','dome':'Pokémon Dome',
 'naming':'Choose your name','dialogue':'Conversation','board':'Duel board','energy-overflow':'Crowded Energy',
 'actions':'Action menu','hand-pokemon':'Pokémon browser','hand-trainer':'Trainer browser',
 'chosen-card':'Chosen card','pokemon-rules-1':'Rules: page 1','pokemon-rules-2':'Rules: page 2',
 'pokemon-rules-3':'Rules: page 3','trainer-rules':'Trainer rules','prize':'Take a Prize','result':'Result','start':'Duel start',
 'leads-room':'Room distance','leads-conversation':'Conversation distance'}


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def relative(path):return str(path.resolve().relative_to(ROOT))
def escaped(value):return html.escape(str(value),quote=True)
def read_json(path):return json.loads(path.read_text())
def clean_log(text):return re.sub(r'VI_CALLBACK \{[^\n]*\}\r?\n','',text).replace('\r\n','\n')


def validate_case(folder,width,height,ram,stages):
    manifest=folder/'capture-provenance.json';data=read_json(manifest)
    assert data['status']=='verified-expansion-pak-trial-captures',manifest
    assert data['settings']['ram_mib']==ram and data['settings']['ExpansionPak']==(ram==8),manifest
    assert data['settings']['VI_enabled'] and not data['settings']['supersampling'],manifest
    assert data['settings']['shader']=='None',manifest
    assert data['finished']['missing']==[] and data['finished']['captures']==len(stages),manifest
    assert [item['stage'] for item in data['images']]==stages,manifest
    assert len(data['native_frames'])==len(stages),manifest
    assert data['runtime_video_modes'],manifest
    for mode in data['runtime_video_modes']:
        assert (mode['width'],mode['height'],mode['ram'],mode['highres'])==(width,height,ram*1024*1024,int(width==480)),mode
    native={item['file'][:-4]:item for item in data['native_frames']}
    count=0
    for item in data['images']:
        stage=item['stage'];path=folder/'post-vi'/item['file'];gate=item['stage_gate']
        assert sha(path)==item['png_sha256'],path
        image=Image.open(path).convert('RGB');assert list(image.size)==item['dimensions'],path
        assert image.size in ((640,240),(640,480)),(path,image.size)
        assert hashlib.sha256(image.tobytes()).hexdigest()==item['rgb_sha256'],path
        assert item['exact_callback_rgb_pixels']==image.width*image.height,path
        assert gate['native_frame'] in (stage,'duel-ui-'+stage),gate
        assert gate['native_dimensions']==[width,height],gate
        assert gate['native_ready_callback']>=gate['stage_started_callback'],gate
        assert item['callback']>gate['native_ready_callback'],gate
        assert gate['post_native_valid_callbacks']>=6 and gate['stable_callbacks']>=6,gate
        assert gate['previous_captured_sha256'] is None or re.fullmatch('[a-f0-9]{64}',gate['previous_captured_sha256']),gate
        n=native[gate['native_frame']];source=folder/'native'/n['file']
        assert sha(source)==n['sha256'] and n['dimensions']==[width,height],source
        assert Image.open(source).size==(width,height),source
        assert n['exact_native_pixels']==width*height,n
        count+=item['exact_callback_rgb_pixels']
    assert len({item['rgb_sha256'] for item in data['images']})==len(stages),('Repeated phase image',folder)
    # Timing is recorded by the emulator's N64 timer, not wall time or a
    # measurement of physical console frame rate. Preserve source provenance.
    source_log=ROOT/data['source_logs']['path'];assert sha(source_log)==data['source_logs']['sha256'],source_log
    text=clean_log(source_log.read_text(errors='replace'))
    timings={}
    for stage,micros in re.findall(r'^VIDEO FRAME stage=([\w-]+) render_us=(\d+)\s*$',text,re.M):
        timings.setdefault(stage,[]).append(int(micros))
    timing_summary={stage:dict(samples=len(values),median_us=statistics.median(values),min_us=min(values),max_us=max(values))
                    for stage,values in timings.items()}
    memory=[line for line in text.splitlines() if line.startswith(('VIDEO HEAP ','MEMORY ','DUEL UI HEAP '))]
    return dict(folder=folder,manifest=manifest,data=data,pixels=count,timings=timing_summary,memory=memory)


def display_frame(path):
    image=Image.open(path).convert('RGB')
    if image.size==(640,240):return image.resize((640,480),Image.Resampling.NEAREST)
    assert image.size==(640,480),(path,image.size)
    return image


def post_vi(case,stage):return case['folder']/'post-vi'/(stage+'.png')


def sheet(path,tiles,columns,width,height,title):
    gap=12;header=42;label_height=30;rows=(len(tiles)+columns-1)//columns
    image=Image.new('RGB',(columns*width+(columns-1)*gap,header+rows*(height+label_height+gap)),(17,27,41))
    draw=ImageDraw.Draw(image);font=ImageFont.load_default(size=18)
    # Pillow's bundled face has a limited glyph set. Keep review captions
    # readable instead of emitting replacement boxes for multiplication marks.
    def label(text):
        text=text.replace('×','x').replace('·',' / ').replace('’',"'")
        return unicodedata.normalize('NFKD',text).encode('ascii','ignore').decode('ascii')
    draw.text((10,9),label(title),font=font,fill=(241,242,232))
    for i,(picture,caption) in enumerate(tiles):
        assert picture.size==(width,height),(caption,picture.size)
        x=(i%columns)*(width+gap);y=header+(i//columns)*(height+label_height+gap)
        image.paste(picture,(x,y));draw.text((x+6,y+height+5),label(caption),font=font,fill=(241,242,232))
    image.save(path)


def figure(case,stage,caption=None):
    path=post_vi(case,stage).relative_to(HERE);native=next(item for item in case['data']['images'] if item['stage']==stage)['stage_gate']['native_frame']
    n=(case['folder']/'native'/(native+'.png')).relative_to(HERE)
    label=caption or LABELS.get(stage,stage.replace('-',' ').title())
    return f'<figure><a href="{escaped(path)}"><img class="full" src="{escaped(path)}" loading="lazy" alt="{escaped(label)}"></a><figcaption>{escaped(label)} · <a href="{escaped(n)}">Native RDP</a></figcaption></figure>'


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--native-8',type=Path,default=ADV/'build/expansion-native-8mib-v2/result.json')
    parser.add_argument('--native-4',type=Path,default=ADV/'build/final-standard-native-v2/result.json')
    args=parser.parse_args()
    manifests=[HERE/'cases'/spec[0]/'capture-provenance.json' for spec in CASE_SPECS.values()]
    geometry=ROOT/'port/characters/review/distinctive-leads/validation.json'
    required=manifests+[args.native_8,args.native_4,geometry]
    missing=[relative(path) for path in required if not path.is_file()]
    if missing:parser.error('Required final results are missing:\n  '+'\n  '.join(missing))
    cases={key:validate_case(HERE/'cases'/folder,w,h,ram,stages)
           for key,(folder,w,h,ram,stages) in CASE_SPECS.items()}
    same_rom={}
    for label,a,b in [('game','game320','game480'),('before','before320','before480'),('after','after320','after480')]:
        assert cases[a]['data']['rom']['sha256']==cases[b]['data']['rom']['sha256'],('RAM modes used different ROMs',label)
        same_rom[label]=cases[a]['data']['rom']['sha256']
    suites={}
    for label,file,dimensions,ram in [('expansion',args.native_8,[480,360],8),('standard',args.native_4,[320,240],4)]:
        data=read_json(file)
        assert data['passed'] and data['ram_mib']==ram and data['native_dimensions']==dimensions,file
        assert data['maps']==list(range(34)) and len(data['frames'])==42,file
        for flag in ('sram_passed','mips_story_passed','native_duel_passed','intro_passed'):assert data[flag],(file,flag)
        suites[label]=dict(source=relative(file),sha256=sha(file),rom_sha256=data['rom_sha256'],
            native_dimensions=dimensions,ram_mib=ram,maps=34,frames=42,worst_heap=data['worst_heap'],
            sram_passed=True,mips_story_passed=True,native_duel_passed=True,intro_passed=True)
    proof=read_json(geometry)
    assert proof['passed'] and proof['all_aliases_unchanged'],geometry
    assert len(proof['models'])==4 and all(model['all_mesh_records_unchanged'] for model in proof['models']),geometry
    derived=HERE/'derived';derived.mkdir(exist_ok=True)
    crops={}
    for resolution in (320,480):
        for when in ('before','after'):
            for slug,name,_ in FAMILIES:
                source=post_vi(cases[when+str(resolution)],slug+'-portrait')
                crop=display_frame(source).crop(HEAD_BOX).resize((250,260),Image.Resampling.NEAREST)
                target=derived/f'{resolution}-{when}-{slug}.png';crop.save(target)
                crops[resolution,when,slug]=crop
    sheet(HERE/'lead-faces-after.png',[(crops[480,'after',slug],name) for slug,name,_ in FAMILIES],4,250,260,'Lead faces · 480×360 · same fixed crop')
    for resolution in (320,480):
        tiles=[(crops[resolution,when,slug],f'{name} · {when}') for when in ('before','after') for slug,name,_ in FAMILIES]
        sheet(HERE/f'face-before-after-{resolution}.png',tiles,4,250,260,f'Faces · {resolution}×{240 if resolution==320 else 360} · before / after')
    selections=['overworld','lab','lightning','dome','board','hand-pokemon','pokemon-rules-1','prize']
    tiles=[(display_frame(post_vi(cases['game'+str(res)],stage)),f'{LABELS[stage]} · {res}×{240 if res==320 else 360}')
           for stage in selections for res in (320,480)]
    sheet(HERE/'resolution-comparison.png',tiles,2,640,480,'Native resolution · 320×240 / 480×360 · ares post-VI')
    production={}
    for label,file in [('standard',ADV/'poketcg-adventure.z64'),('expansion',ADV/'poketcg-expansion.z64')]:
        assert file.is_file(),file
        production[label]=dict(file=relative(file),bytes=file.stat().st_size,sha256=sha(file))
    timing={key:case['timings'].get('leads-room') for key,case in cases.items() if key.startswith(('before','after'))}
    case_summary={key:dict(source=relative(case['manifest']),sha256=sha(case['manifest']),
        rom=case['data']['rom'],runtime_video_modes=case['data']['runtime_video_modes'],
        frames=len(case['data']['images']),exact_callback_rgb_pixels=case['pixels'],
        timing=case['timings'],memory=case['memory']) for key,case in cases.items()}
    validation=dict(passed=True,source_commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
        capture_cases=case_summary,post_vi_pngs=sum(value['frames'] for value in case_summary.values()),
        exact_callback_rgb_pixels=sum(case['pixels'] for case in cases.values()),
        stage_gate=dict(matching_native_header=True,minimum_post_native_valid_callbacks=6,minimum_stable_callbacks=6),
        same_rom_across_4_and_8_mib=same_rom,full_native_suites=suites,production_roms=production,
        lead_geometry=dict(source=relative(geometry),sha256=sha(geometry),all_mesh_records_unchanged=True,
            all_aliases_unchanged=True,maximum_triangles=proof['geometry_limit']['triangles'],maximum_batches=proof['geometry_limit']['batches']),
        crops=dict(display_canvas=[640,480],source_box=list(HEAD_BOX),nearest_scale=2,
            original_pngs_unchanged=True,low_resolution_display='Exact row duplication from640x240 to640x480',
            higher_resolution_display='640x480 scanout unchanged'),
        renderer_timing=dict(source='ares emulated N64 timer_ticks around scene render and RDP completion',
            physical_console_fps_measured=False,eight_actor_room=timing))
    validation['regressions']=json.loads((HERE/'regression-checks.json').read_text())
    validation['extra_proofs']={'controlled_baseline':'baseline-assets.json',
        'face_pixel_changes':'face-pixel-proof.json','card_back_source':'authoring/card-back/manifest.json'}
    (HERE/'validation.json').write_text(json.dumps(validation,indent=2)+'\n')
    resolution_html=''.join('<h3>'+escaped(LABELS[stage])+'</h3><div class="pair">'+figure(cases['game320'],stage,'320×240 · 4 MiB')+figure(cases['game480'],stage,'480×360 · 8 MiB')+'</div>' for stage in selections)
    face_html=''.join(f'<figure><a href="{escaped(post_vi(cases["after480"],slug+"-portrait").relative_to(HERE))}"><img class="crop" src="derived/480-after-{slug}.png" alt="{escaped(name)}"><figcaption>{escaped(name)} · {escaped(note)}</figcaption></a></figure>' for slug,name,note in FAMILIES)
    distance_html=''.join('<h3>'+escaped(LABELS[stage])+'</h3><div class="pair">'+figure(cases['after320'],stage,'320×240 · new faces')+figure(cases['after480'],stage,'480×360 · new faces')+'</div>' for stage in ('leads-room','leads-conversation'))
    all_html=''.join('<details><summary>'+escaped(folder)+'</summary><p><a href="cases/'+escaped(folder)+'/capture-provenance.json">Capture record</a></p><div class="all">'+''.join(figure(cases[key],stage) for stage in stages)+'</div></details>' for key,(folder,_,_,_,stages) in CASE_SPECS.items())
    timing_text=''
    if timing['after320'] and timing['after480']:
        timing_text=f'<p>Eight-actor room render medians: 320×240 {timing["after320"]["median_us"]/1000:.1f} ms; 480×360 {timing["after480"]["median_us"]/1000:.1f} ms. These are ares emulated timer measurements, not physical-console FPS.</p>'
    page='''<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>N64 resolution and lead faces</title><style>
body{margin:0;background:#111b29;color:#edf1f2;font:16px/1.5 system-ui,sans-serif}main{max-width:1320px;margin:auto;padding:24px}a{color:#a9d4ff}h1{font-size:28px}h2{margin-top:36px}h3{font-size:18px;margin:22px 0 8px}p{max-width:95ch}nav{display:flex;gap:20px;flex-wrap:wrap}.pair,.all{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:14px}figure{margin:0;background:#192a3a;padding:8px}figcaption{padding:6px 0;font-size:14px}.full{display:block;width:100%;aspect-ratio:4/3;object-fit:fill;image-rendering:pixelated}.faces{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:12px}.crop{width:100%;max-width:250px;image-rendering:pixelated}.comparison{display:block;max-width:100%;height:auto;image-rendering:pixelated}summary{cursor:pointer;font-weight:600;padding:12px 0}.all{grid-template-columns:repeat(auto-fit,minmax(280px,1fr))}.evidence{padding:12px 18px;background:#192a3a}.small{font-size:14px;color:#bbc9d3}@media(max-width:720px){main{padding:12px}.pair{grid-template-columns:1fr}.faces{grid-template-columns:repeat(2,minmax(0,1fr))}}</style><main>
<h1>N64 resolution and lead faces</h1><p>320×240 and 480×360, captured through ares with VI processing enabled. The Expansion Pak build selects 480×360 with 8 MiB and falls back to 320×240 with 4 MiB.</p><nav><a href="#resolution">Scenes and duels</a><a href="#faces">Lead faces</a><a href="#before">Before and after</a><a href="#captures">Full captures</a><a href="#validation">Validation</a></nav>
<h2 id="faces">Lead faces</h2><p>The new face paint keeps the existing models, UVs, rig and camera. These are fixed crops from emulator captures.</p><div class="faces">'''+face_html+'''</div>'''+distance_html+'''
<h2 id="resolution">Scenes and duels</h2><p>Each pair uses the new faces. Original captures open when clicked. <a href="resolution-comparison.png">Open the comparison sheet</a>.</p>'''+resolution_html+'''
<p>The card backs use the original international artwork, sampled for each resolution. <a href="authoring/card-back/manifest.json">Source and conversion record</a> · <a href="https://archives.bulbagarden.net/wiki/File:Cardback.jpg">Published artwork</a>. Pokémon artwork © The Pokémon Company / Nintendo / Creatures / GAME FREAK.</p>
<h2 id="before">Before and after</h2><p>Identical camera, geometry, pose and crop within each resolution. Only the four lead face textures changed.</p><h3>480×360</h3><a href="face-before-after-480.png"><img class="comparison" src="face-before-after-480.png" alt="Four lead faces before and after at480×360"></a><h3>320×240</h3><a href="face-before-after-320.png"><img class="comparison" src="face-before-after-320.png" alt="Four lead faces before and after at320×240"></a><p><a href="../../../characters/review/distinctive-leads/face-paint-comparison.png">Source texture comparison</a> · <a href="../../../characters/review/distinctive-leads/blender-comparison.png">Blender inspection</a>. Those are asset inspections, not N64 screenshots.</p>
<h2 id="captures">Full captures</h2><p>Original post-VI PNGs, plus native RDP captures and capture records.</p>'''+all_html+'''
<h2 id="validation">Validation</h2><div class="evidence"><p>74 post-VI captures pass exact PNG-to-callback RGB checks. Every stage waits for its matching native framebuffer header, six later valid callbacks and six stable frames.</p><p>Both complete native suites pass: 34 maps, 42 captures, story, opening, SRAM and an actual engine duel per RAM mode. The 4 MiB and 8 MiB scene comparisons use the same diagnostic ROM.</p><p>All four lead meshes and aliases remain unchanged; the cast stays within 906 triangles and six batches per actor.</p>'''+timing_text+'''<p><a href="validation.json">Combined validation</a> · <a href="README.md">Build and capture notes</a></p></div><p class="small">Raw 640×240 VI captures are shown at 4:3; comparison sheets duplicate rows exactly. Raw 640×480 captures remain unchanged. Head crops use the same rectangle on a 640×480 display canvas, then 2× nearest scaling. Original files remain available.</p></main></html>'''
    # Asset comparisons are copied into this standalone packet; their labels
    # explicitly distinguish authoring inspection from native output.
    inspections=HERE/'authoring';inspections.mkdir(exist_ok=True)
    import shutil
    for file in ('face-paint-comparison.png','blender-comparison.png'):
        shutil.copy2(geometry.parent/file,inspections/file)
    shutil.copytree(ROOT/'port/assets/ui/card-back',inspections/'card-back',dirs_exist_ok=True)
    page=page.replace('../../../characters/review/distinctive-leads/','authoring/')
    page=page.replace('74 post-VI captures',f'{validation["post_vi_pngs"]} post-VI captures')
    (HERE/'index.html').write_text(page)
    total=validation['exact_callback_rgb_pixels']
    readme=f'''# N64 Expansion Pak and lead-face comparison

The trial renders at 480×360 with 8 MiB, with a 320×240 fallback on 4 MiB. The standard ROM remains available separately. The four lead faces are newly authored paint on unchanged models.

- [Gallery](index.html) · [resolution sheet](resolution-comparison.png) · [lead faces](lead-faces-after.png)
- [480×360 before/after](face-before-after-480.png) · [320×240 before/after](face-before-after-320.png)
- [Combined validation](validation.json)

All six final v3 cases are required by `gallery_build.py`. They contain {validation['post_vi_pngs']} original ares post-VI PNGs and {total:,} exact callback RGB pixels. Each stage is gated by its own native framebuffer header, at least six later valid callbacks, and six stable frames. The original encoder PNGs and native source frames are linked individually. The same diagnostic ROM hash is required across RAM modes.

Both full native suites pass all 34 maps, 42 captured frames, opening, story, SRAM and an actual engine duel. Per-suite ROM hashes, runtime dimensions and worst free heap are recorded in `validation.json`. Separate host/UI/browser regressions were run by the parent task; this gallery does not infer those checks from screenshots.

Raw 640×240 VI scanout is displayed at 4:3. Contact sheets duplicate rows to 640×480 exactly; 640×480 scanout is unchanged. Face crops use display box {list(HEAD_BOX)} for every person, resolution and before/after image, then 2× nearest scaling. Source texture and Blender inspection images under `authoring/` are labeled separately.

Render timings are ares emulated `timer_ticks` samples around scene rendering and RDP completion. They are not a physical-console FPS measurement or a 30 FPS promise. The eight-actor room medians and sample counts are recorded separately for both resolutions and both face sets.

Rebuild after all capture and native-suite results exist:

```sh
python3 port/reviews/expansion-pak/gallery_build.py
```

Production ROM paths, sizes and SHA256 hashes are in `validation.json`. The captures use diagnostic builds of the same renderer, with fixed poses and unchanged camera framing for each comparison.

Controlled earlier assets and identical camera/scene executable sections: [baseline-assets.json](baseline-assets.json). All eight before/after portraits differ only inside the face region: [face-pixel-proof.json](face-pixel-proof.json).
'''
    (HERE/'README.md').write_text(readme)
    print(f'EXPANSION GALLERY PASS: {validation["post_vi_pngs"]} post-VI frames, {total:,} verified RGB pixels, two full native suites; {HERE}')

if __name__=='__main__':main()
