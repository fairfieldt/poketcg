#!/usr/bin/env python3
"""Verify the final native/VI cases and publish the N64 performance update."""
import hashlib
import html
import json
from pathlib import Path
import re
import statistics
from PIL import Image, ImageDraw, ImageFont

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
STAGES={'overworld':'Overworld','lab':"Mason's lab",'lightning':'Lightning Club',
        'dome':'Pokémon Dome','naming':'Name entry','dialogue':'Dialogue UI fixture'}
BASELINE={'overworld':114179.5,'lab':107184,'lightning':70268.5,'dome':103285}
def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()

cases={};timings={};pixels=0
for width,height,ram in [(320,240,4),(480,360,8)]:
    folder=HERE/f'cases/{width}';manifest=folder/'capture-provenance.json'
    data=json.loads(manifest.read_text())
    assert data['finished']['missing']==[]
    assert [item['stage'] for item in data['images']]==list(STAGES)
    for mode in data['runtime_video_modes']:
        assert (mode['width'],mode['height'],mode['ram'])==(width,height,ram*1024*1024)
    for item in data['images']:
        path=folder/'post-vi'/item['file'];assert sha(path)==item['png_sha256']
        image=Image.open(path).convert('RGB')
        assert sha(path)==item['png_sha256']
        assert hashlib.sha256(image.tobytes()).hexdigest()==item['rgb_sha256']
        assert image.width*image.height==item['exact_callback_rgb_pixels']
        gate=item['stage_gate'];assert gate['native_frame']==item['stage']
        assert gate['post_native_valid_callbacks']>=6 and gate['stable_callbacks']>=6
        pixels+=item['exact_callback_rgb_pixels']
    for item in data['native_frames']:
        path=folder/'native'/item['file']
        assert sha(path)==item['sha256'] and Image.open(path).size==(width,height)
    log=(ROOT/data['source_logs']['path']).read_text(errors='replace');stage='';samples={}
    for line in log.splitlines():
        match=re.search(r'VIDEO REVIEW (\S+) BEGIN',line)
        if match:stage=match[1]
        match=re.search(r'VIDEO FRAME map=(\d+) render_us=(\d+)',line)
        if match:samples.setdefault(stage,[]).append(int(match[2]))
    for name,values in samples.items():assert len(values)==24,(name,len(values))
    timings[str(width)]={name:dict(samples=len(values),median_us=statistics.median(values)) for name,values in samples.items()}
    cases[str(width)]=dict(manifest=str(manifest.relative_to(HERE)),sha256=sha(manifest),rom=data['rom'])
    sheet=Image.new('RGB',(1292,1580),'#111b29');draw=ImageDraw.Draw(sheet);font=ImageFont.load_default(size=18)
    for i,(stage,label) in enumerate(STAGES.items()):
        image=Image.open(folder/'post-vi'/(stage+'.png')).convert('RGB')
        if image.size==(640,240):image=image.resize((640,480),Image.Resampling.NEAREST)
        assert image.size==(640,480)
        x,y=i%2*652,i//2*526;sheet.paste(image,(x,y));draw.text((x+6,y+484),label,font=font,fill='white')
    sheet.save(HERE/f'contact-{width}.png')
assert cases['320']['rom']['sha256']==cases['480']['rom']['sha256']
lods=json.loads((ROOT/'port/world3d/generated/character-lods.json').read_text())
full=sum(item['full_triangles'] for item in lods['models']);low=sum(item['lod_triangles'] for item in lods['models'])
production={}
for key,name in [('standard','poketcg-adventure.z64'),('expansion','poketcg-expansion.z64')]:
    path=ROOT/'port/frontends/n64/adventure'/name
    production[key]=dict(file=str(path.relative_to(ROOT)),sha256=sha(path),bytes=path.stat().st_size)
validation=dict(passed=True,cases=cases,original_post_vi_images=12,exact_callback_rgb_pixels=pixels,
    same_review_rom_both_ram_modes=True,timings=timings,
    detail=dict(models=len(lods['models']),full_triangles=full,lod_triangles=low,textured_face_batches_unchanged=True),
    production_roms=production,
    timing_scope='Emulated CP0 world rendering intervals; not end-to-end or physical console FPS.')
(HERE/'validation.json').write_text(json.dumps(validation,indent=2)+'\n')
rows=''.join(f'<tr><td>{STAGES[name]}</td><td>{old/1000:.2f} ms</td><td>{timings["320"][name]["median_us"]/1000:.2f} ms</td><td>{timings["480"][name]["median_us"]/1000:.2f} ms</td></tr>' for name,old in BASELINE.items())
sections=[]
for width,height in [(480,360),(320,240)]:
    figures=''.join(f'<figure><a href="cases/{width}/post-vi/{stage}.png"><img src="cases/{width}/post-vi/{stage}.png" alt="{html.escape(label)}" loading="lazy"></a><figcaption>{label} · <a href="cases/{width}/native/{stage}.png">Native framebuffer</a></figcaption></figure>' for stage,label in STAGES.items())
    sections.append(f'<section><h2>{width} × {height}</h2><div class="grid">{figures}</div></section>')
(HERE/'index.html').write_text('''<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>N64 renderer update</title><style>body{margin:0;background:#111b29;color:#edf1f2;font:16px/1.5 system-ui}main{max-width:1320px;padding:24px;margin:auto}a{color:#abd4ff}p{max-width:95ch}.grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:18px}figure{margin:0;background:#192a3a;padding:8px}img{display:block;width:100%;aspect-ratio:4/3;object-fit:fill;image-rendering:pixelated}figcaption{padding-top:8px}h2{margin-top:36px}table{border-collapse:collapse;max-width:900px;width:100%}td,th{text-align:left;padding:9px;border-bottom:1px solid #4b5d70}@media(max-width:720px){.grid{grid-template-columns:1fr}main{padding:12px}table{font-size:13px}}</style><main><h1>N64 renderer update</h1><p>The N64 now uses Tiny3D with libdragon, conservative actor visibility, simpler distant character meshes, and a cached duel background. Conversation speakers and Mark retain their full-detail models during dialogue. Their textured face geometry is unchanged in every detail level.</p><p>All 34 maps, the native duel, opening choreography, story and SRAM checks passed in both RAM modes. The new input path captures short taps at VI rate, and consumes each press once. Actual N64 callbacks were exercised in both modes.</p><h2>World rendering intervals</h2><p>Each new median uses 24 frames. These are ares CP0 rendering measurements, excluding final UI, display pacing and unmodeled physical raster/memory costs. They are not console frame rates. The baseline used the earlier cached GL implementation; the new figures include backend, character-detail and visibility changes.</p><table><thead><tr><th>Scene</th><th>Earlier GL / 320</th><th>Current / 320</th><th>Current / 480</th></tr></thead><tbody>'''+rows+'''</tbody></table><p>The controlled unchanged-model backend test improved the crowded lead scene about 3×. Cached felt rows reduced the isolated duel-background CPU interval about 82%, with identical pixels.</p><p><a href="../hardware-performance/backend/">Backend comparison</a> · <a href="../hardware-performance/felt-optimization/README.md">Duel background</a> · <a href="../hardware-performance/performance-applied-full/">Full native checks</a> · <a href="validation.json">Capture and ROM hashes</a></p><h2>Presentation fixes</h2><p><a href="../practice-reading/">Tutorial reading panels</a> · <a href="../coin-animation/">Coin toss animation</a> · <a href="../intro-orbs/">Rounded Energy symbols</a> · <a href="../audio-padding/">Intro audio repair</a> · <a href="../conversation-camera-applied/">Conversation camera</a></p>'''+''.join(sections)+'''<p>These screenshots come from the original ares VI encoder. Progressive 640 × 240 images display at 4:3; contact sheets duplicate those rows exactly. Original PNGs and separate native framebuffer pixels are preserved.</p><p><a href="contact-480.png">480 × 360 sheet</a> · <a href="contact-320.png">320 × 240 sheet</a></p></main></html>''')
(HERE/'README.md').write_text(f'''# Applied N64 performance and presentation update

The default renderer is now the pinned Tiny3D core integrated with libdragon. Cached GL remains selectable with SCENE_T3D=0; legacy standalone renderer diagnostics retain that default until explicitly wired. The production world, complete presented actor transforms, cursor depth reset and naming viewport have explicit camera lifetimes. The scissor is restored before full-canvas UI blits.

There are {len(lods['models'])} optional character exports: {full:,} high-detail triangles versus {low:,} gameplay-detail triangles across the catalog. Textured face batches, rigid pivots, open seams, opacity and source assets remain intact. Blender generation locks seams and volume anchors, biases color edges and retains any batch that would produce degenerate/duplicate surfaces. Normal builds use checked-in data; changed source meshes invalidate their LOD automatically. Close conversation subjects/player force full detail, while 48/60 logical-pixel hysteresis avoids threshold flicker. These catalog totals are not per-frame submissions.

The shared camera frustum conservatively rejects only complete offscreen actor/shadow spheres. The island drops 252 proven hidden triangles (5511→5259), with native and VI byte equality at both resolutions under both backends. All viewport-only triangles remain for wider browser windows. Geometry/camera hashes protect the export bake.

The new duel felt cache retains19,200/43,200bytes and preserves every pixel, existing completed canvas, cache writeback, RDP copy and completion fence. A controlled actual N64 CPU-only benchmark reports about82% less felt time; it is not a complete-duel frame-rate claim.

The capture cases in this directory use the same DIAG15/HIGH_RES1 ROM in both RAM modes. All12 original VI PNGs pass exact callback-RGB verification, matching native headers, six later valid callbacks and six stable callbacks. The gallery records emulated CP0 medians and links separate full native34-map/42-frame gates, unchanged-geometry backend A/B, intro/orb/audio and coin proofs. Re-run build_gallery.py after replacing both cases.
''')
print(f'PERFORMANCE GALLERY PASS:12 original VI images, {pixels:,} exact callback pixels, both RAM modes')
