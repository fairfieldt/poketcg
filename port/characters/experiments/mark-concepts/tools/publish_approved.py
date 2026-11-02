#!/usr/bin/env python3
"""Assemble retained native captures and editable models for local review."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
import html,json
ROOT=Path(__file__).resolve().parents[1]
CONCEPTS=[('a_adventure','Adventure','a-adventure'),('b_compact','Compact','b-compact'),('c_lean','Lean','c-lean'),('d_angular','Angular','d-angular')]
font=ImageFont.truetype('/System/Library/Fonts/Helvetica.ttc',18)
portraits=Image.new('RGB',(1040,340),(24,25,30));pd=ImageDraw.Draw(portraits)
turns=Image.new('RGB',(640,1300),(24,25,30));td=ImageDraw.Draw(turns)
baseline=ROOT/'approved/a_adventure/i1/native'
frames=[('Current',baseline/'baseline-portrait-mark.png',baseline/'baseline-turn-mark.png')]
for cid,label,concept in CONCEPTS:
    it=sorted(p.name for p in (ROOT/'approved'/cid).glob('i3*') if (p/'native/capture.json').is_file())[-1]
    d=ROOT/'approved'/cid/it/'native'
    frames.append((label,d/f'{cid}_{it}-portrait-mark.png',d/f'{cid}_{it}-turn-mark.png'))
for k,(label,portrait,turn) in enumerate(frames):
    pd.text((k*208+8,5),label,font=font,fill='white')
    portraits.paste(Image.open(portrait).crop((108,44,212,198)).resize((208,308),Image.Resampling.NEAREST),(k*208,32))
    td.text((10,k*260+5),label,font=font,fill='white')
    turns.paste(Image.open(turn).crop((0,55,320,170)).resize((640,230),Image.Resampling.NEAREST),(0,k*260+30))
portraits.save(ROOT/'review/final-portraits.png');turns.save(ROOT/'review/final-turnarounds.png')
page=['<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>Mark models</title><style>body{margin:24px auto;max-width:1400px;padding:0 20px;background:#151719;color:#eee;font:16px system-ui}a{color:#a7d5ff}img{max-width:100%;height:auto} .grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(310px,1fr));gap:28px} .native{image-rendering:pixelated}h1{font-size:26px}h2{font-size:21px}nav{display:flex;gap:20px;flex-wrap:wrap}figure{margin:14px 0}details{margin:12px 0}</style><h1>Mark models</h1><nav><a href="review/final-portraits.png">Compare faces</a><a href="review/final-turnarounds.png">Compare silhouettes</a><a href="README.md">Review notes</a></nav><figure><img class="native" src="review/final-portraits.png" alt="Current Mark and four final model portraits"></figure><div class="grid">']
manifest=[]
for cid,label,concept in CONCEPTS:
    final=sorted(p.name for p in (ROOT/'approved'/cid).glob('i3*') if (p/'native/capture.json').is_file())[-1]
    prefix=f'approved/{cid}/{final}'
    page.extend([f'<section><h2>{label}</h2>',f'<a href="concepts/{concept}.png"><img src="concepts/{concept}.png" alt="{label} generated concept"></a>',f'<img class="native" src="{prefix}/native/{cid}_{final}-turn-mark.png" alt="{label} final in-game turnaround">'])
    blend=next((ROOT/prefix).rglob('mark.blend'),None)
    if blend:page.append(f'<p><a href="{blend.relative_to(ROOT)}">Open Blender model</a></p>')
    page.append(f'<p><a href="{prefix}/mark/scene.l64">Native model</a> · <a href="{prefix}/audit.json">Geometry checks</a></p>')
    for iteration in ['i1','i2']+sorted(p.name for p in (ROOT/'approved'/cid).glob('i3*') if (p/'native/capture.json').is_file()):
        link=f'approved/{cid}/{iteration}/native/comparison.png'
        page.append(f'<details><summary>{"Final correction" if len(iteration)>2 else "Round "+iteration[1:]}</summary><a href="{link}"><img class="native" src="{link}" alt="{label} {iteration} native comparison"></a></details>')
    page.append('</section>')
    manifest.append(dict(concept=cid,final=final,blend=str(blend.relative_to(ROOT)) if blend else None))
page.extend(['</div><h2>Shirt and hair reference</h2><img src="concepts/a-simple-shirt.png" alt="Updated solid-red shirt and continuous hair reference"></html>'])
(ROOT/'index.html').write_text('\n'.join(page)+'\n');(ROOT/'review/final-models.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(ROOT/'index.html')
