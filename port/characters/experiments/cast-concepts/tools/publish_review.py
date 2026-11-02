#!/usr/bin/env python3
from pathlib import Path
from PIL import Image,ImageDraw,ImageFont
import json
ROOT=Path(__file__).resolve().parents[1];out=ROOT/'review';out.mkdir(exist_ok=True)
font=ImageFont.truetype('/System/Library/Fonts/Helvetica.ttc',20)
sheet=Image.new('RGB',(936,1020),(24,25,30));draw=ImageDraw.Draw(sheet)
for col,(slug,label) in enumerate([('mark','Mark'),('dr_mason','Dr. Mason'),('ronald','Ronald')]):
    for row,key in enumerate(('baseline','ensemble')):
        im=Image.open(ROOT/'ensemble/native'/f'{key}-portrait-{slug}.png')
        draw.text((col*312+10,row*510+8),f'{label} — {"Current" if row==0 else "Revised"}',font=font,fill='white')
        sheet.paste(im.crop((108,44,212,198)).resize((312,462),Image.Resampling.NEAREST),(col*312,row*510+40))
sheet.save(out/'lead-comparison.png')
page=['<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>Character models</title><style>body{max-width:1400px;margin:24px auto;padding:0 20px;background:#151719;color:#eee;font:16px system-ui}a{color:#a7d5ff}img{max-width:100%;height:auto}.native{image-rendering:pixelated}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(320px,1fr));gap:28px}h1{font-size:26px}h2{font-size:21px}nav{display:flex;gap:20px}details{margin:14px 0}</style><h1>Character models</h1><nav><a href="review/lead-comparison.png">Compare leads</a><a href="../mark-concepts/index.html">Mark concepts</a><a href="README.md">Review notes</a></nav><img class="native" src="review/lead-comparison.png" alt="Current and revised Mark, Dr. Mason and Ronald"><div class="grid">']
for slug,label,concept in [('dr_mason','Dr. Mason','dr-mason'),('ronald','Ronald','ronald')]:
    page.extend([f'<section><h2>{label}</h2>',f'<a href="concepts/{concept}.png"><img src="concepts/{concept}.png" alt="{label} generated concept"></a>',f'<img class="native" src="models/{slug}/i3/native/{slug}_i3-turn-{slug}.png" alt="{label} final turnaround">',f'<p><a href="models/{slug}/i3/{slug}.blend">Open Blender model</a> · <a href="models/{slug}/i3/{slug}/scene.l64">Native model</a></p>'])
    for n in range(1,4):
        link=f'models/{slug}/i{n}/native/comparison.png'
        page.append(f'<details><summary>Round {n}</summary><a href="{link}"><img class="native" src="{link}" alt="{label} round {n}"></a></details>')
    page.append('</section>')
page.extend(['</div><h2>In the laboratory</h2><img class="native" width="960" src="ensemble/native/ensemble-room.png" alt="Revised leads together in the laboratory"><img class="native" width="960" src="ensemble/native/ensemble-talk.png" alt="Revised leads in conversation view"></html>'])
(ROOT/'index.html').write_text('\n'.join(page)+'\n')
print(ROOT/'index.html')
