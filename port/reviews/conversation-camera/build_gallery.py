#!/usr/bin/env python3
"""Publish controlled ares camera trials, preserving original encoder PNGs."""
from pathlib import Path
from PIL import Image,ImageDraw,ImageFont
import hashlib,html,json,math,shutil,subprocess
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
STAGES=['angle-current','angle-30','angle-20','angle-12','close-ronald','close-mason','hair-front','hair-side','hair-high']
LABELS=['A: Current (about 42 deg)','B: Lower camera (30 deg)','C: Lower camera (20 deg)','D: Lower camera (12 deg)','E: Closer 20 deg / Mark and Ronald','F: Closer 20 deg / Mark and Mason','Ronald / front','Ronald / side','Ronald / high view']
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def display(p):
 im=Image.open(p).convert('RGB')
 if im.size==(640,240):return im.resize((640,480),Image.Resampling.NEAREST)
 assert im.size==(640,480),(p,im.size)
 return im

def sheet(path,stages,res,cols,title):
 rows=math.ceil(len(stages)/cols);gap=12;header=42;caption=30
 im=Image.new('RGB',(cols*640+(cols-1)*gap,header+rows*(480+caption+gap)),(17,27,41))
 d=ImageDraw.Draw(im);font=ImageFont.load_default(size=18);d.text((10,9),title,font=font,fill='white')
 for n,stage in enumerate(stages):
  x=n%cols*652;y=header+n//cols*522
  im.paste(display(HERE/f'cases/{res}/post-vi/{stage}.png'),(x,y))
  d.text((x+6,y+485),LABELS[STAGES.index(stage)],font=font,fill='white')
 im.save(path)

cases={};count=rgb=0
for res,ram,height in [(320,4,240),(480,8,360)]:
 folder=HERE/f'cases/{res}';manifest=folder/'capture-provenance.json';data=json.loads(manifest.read_text())
 assert data['finished']['missing']==[] and len(data['images'])==9
 assert [i['stage'] for i in data['images']]==STAGES
 for mode in data['runtime_video_modes']:
  assert (mode['width'],mode['height'],mode['ram'])==(res,height,ram*1024*1024)
 for item in data['images']:
  p=folder/'post-vi'/item['file'];assert sha(p)==item['png_sha256']
  im=Image.open(p).convert('RGB');assert list(im.size)==item['dimensions']
  assert hashlib.sha256(im.tobytes()).hexdigest()==item['rgb_sha256']
  assert im.width*im.height==item['exact_callback_rgb_pixels']
  gate=item['stage_gate'];assert gate['native_frame']==item['stage']
  assert gate['post_native_valid_callbacks']>=6 and gate['stable_callbacks']>=6
  count+=1;rgb+=item['exact_callback_rgb_pixels']
 for item in data['native_frames']:
  p=folder/'native'/item['file'];assert sha(p)==item['sha256']
  assert Image.open(p).size==(res,height)
 cases[str(res)]=dict(source=str(manifest.relative_to(HERE)),sha256=sha(manifest),rom=data['rom'],runtime=data['runtime_video_modes'],frames=9)
 sheet(HERE/f'angles-{res}.png',STAGES[:4],res,2,f'Camera angles / {res} x {height} / ares after VI')
 sheet(HERE/f'closer-{res}.png',STAGES[4:6],res,2,f'Closer conversation framing / {res} x {height} / ares after VI')
 sheet(HERE/f'hair-{res}.png',STAGES[6:],res,3,f'Ronald hair checks / {res} x {height} / ares after VI')
assert cases['320']['rom']['sha256']==cases['480']['rom']['sha256']
hair=ROOT/'port/characters/review/ronald-hair/validation.json';proof=json.loads(hair.read_text())
assert proof['passed'] and proof['owning_head_components']==1 and proof['owning_head_boundary_edges']==0
assert proof['bright_upper_scalp_triangles']==0 and proof['non_exterior_native_head_triangles']==0
shutil.copy2(hair,HERE/'hair-validation.json')
production={}
for kind,name in [('standard','poketcg-adventure.z64'),('expansion','poketcg-expansion.z64')]:
 p=ROOT/'port/frontends/n64/adventure'/name
 production[kind]=dict(file=str(p.relative_to(ROOT)),sha256=sha(p),bytes=p.stat().st_size)
validation=dict(passed=True,source_commit=subprocess.check_output(['git','rev-parse','HEAD'],text=True,cwd=ROOT).strip(),cases=cases,
 original_post_vi_pngs=count,exact_callback_rgb_pixels=rgb,same_rom_across_ram_modes=True,
 stage_gate=dict(matching_native_header=True,post_native_valid_callbacks=6,stable_callbacks=6),
 ronald=dict(source='hair-validation.json',triangles=proof['triangles'],batches=proof['batches'],closed_head=True,scalp_skin_islands=0,
  face_paint_unchanged=proof['sly_face_paint_unchanged'],limbs_unchanged=proof['limbs_unchanged']),
 production_roms=production,production_camera_changed=False,
 display='Original 640x240 VI outputs use exact row duplication for 4:3 sheets; 640x480 outputs are unchanged. Original PNGs are preserved.')
(HERE/'validation.json').write_text(json.dumps(validation,indent=2)+'\n')
def figures(res,stages):
 return ''.join(f'<figure><a href="cases/{res}/post-vi/{s}.png"><img src="cases/{res}/post-vi/{s}.png" alt="{html.escape(LABELS[STAGES.index(s)])}" loading="lazy"></a><figcaption>{html.escape(LABELS[STAGES.index(s)])} · <a href="cases/{res}/native/{s}.png">Native framebuffer</a></figcaption></figure>' for s in stages)
content=[]
for res in [480,320]:
 content.append(f'<section id="r{res}"><h2>{res} x {360 if res==480 else 240}</h2><h3>Height only</h3><div class="grid">{figures(res,STAGES[:4])}</div><h3>Closer two-person framing</h3><div class="grid">{figures(res,STAGES[4:6])}</div><h3>Ronald hair checks</h3><div class="grid hair">{figures(res,STAGES[6:])}</div></section>')
(HERE/'index.html').write_text('''<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Conversation camera trials</title><style>
body{margin:0;background:#111b29;color:#edf1f2;font:16px/1.5 system-ui}main{max-width:1320px;padding:24px;margin:auto}a{color:#abd4ff}p{max-width:95ch}nav{display:flex;gap:20px}.grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:18px}figure{margin:0;background:#192a3a;padding:8px}img{display:block;width:100%;aspect-ratio:4/3;object-fit:fill;image-rendering:pixelated}figcaption{padding-top:8px}h2{margin-top:40px}h3{margin-top:28px}.hair{grid-template-columns:repeat(3,minmax(0,1fr))}@media(max-width:720px){.grid,.hair{grid-template-columns:1fr}main{padding:12px}}</style><main><h1>Conversation camera trials</h1><p>A–D keep the same four actors, camera X/Z position, target and 50-degree field of view. Only camera height changes, so the distance also decreases as the camera lowers. E–F are separate closer two-person compositions with three-quarter poses.</p><p><b>My pick: 20 degrees with closer framing.</b> The angle-only 20-degree view improves faces while keeping room depth. The closer views make Mark’s eyes, Ronald’s brow and smirk, and Mason’s glasses easier to read. The 12-degree view reveals more empty space above the back wall.</p><p>These are staged comparisons using the production room, models, renderer and dialogue panel. The production camera is unchanged. Ronald’s hair repair is included in both playable ROMs and the web build.</p><nav><a href="#r480">480 x 360</a><a href="#r320">320 x 240</a><a href="camera-settings.json">Camera values</a><a href="validation.json">Validation</a></nav>'''+''.join(content)+'''<p>Original ares screenshots are linked above. The 640 x 240 outputs are displayed at 4:3. Comparison sheets duplicate those rows exactly; 640 x 480 outputs stay unchanged. <a href="angles-480.png">Angle sheet</a> · <a href="closer-480.png">Closer sheet</a> · <a href="hair-validation.json">Ronald surface proof</a></p></main></html>''')
(HERE/'README.md').write_text('''# Conversation camera trials

The four angle-only views use the same room, actors, positions, headings, target, camera X/Z and 50-degree FOV. Camera height changes from the prior roughly 42-degree view to 30, 20 and 12 degrees; its distance to the target consequently changes. Two closer 20-degree views additionally change actor spacing and headings for a two-person composition. See camera-settings.json for exact values.

The dialogue panel is the production compositor, with existing Ronald and Mason text excerpts. These scenes are staged camera comparisons, not changed story cutscenes. No production camera values changed.

Ronald’s repair unions the head, hair cap, spikes and fringe into a closed owning exterior. The upper scalp takes the intended hair material, closing real skin gaps. A numerical sliver is collapsed rather than deleted to avoid a pinhole. Face paint, all four animated limbs and the lower torso are preserved. All other 48 runtime records are unchanged. Native winding, skin/exterior checks and front/side/high renders pass. See hair-validation.json and the canonical character report.

DIAGNOSTIC=12 / HIGH_RES=1 builds poketcg-camera-trials.z64. The same ROM runs at 480x360 / 8 MiB and 320x240 / 4 MiB. All 18 original post-VI PNGs are checked against exact callback RGB bytes and gated after their matching native framebuffer headers, six valid callbacks and six stable callbacks. Original PNG bytes are preserved. The source framebuffer is separately available at its native resolution.

Run build_gallery.py after both capture cases exist. The capture command uses ../expansion-pak/capture.py with --output-dir pointing at cases/480 or cases/320. Raw logs/settings and callback buffers remain under the ignored adventure build/expansion-pak/conversation-camera-* directories. The installed ares application and user settings are unchanged.
''')
print(f'CAMERA GALLERY PASS: {count} original VI images, {rgb:,} exact RGB pixels, closed Ronald head; both RAM modes')
