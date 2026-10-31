#!/usr/bin/env python3
"""Refresh the labelled native review gallery from a DIAGNOSTIC=3 log.

Run from the repository root after `make -C port/frontends/n64/adventure
review-diagnostic` and launch the review ROM in ares with stdout redirected to
port/reviews/n64-polish/before/diagnostic/ares.log. All captures are original
320x240 native RDP pixels before VI filtering. Screenshot labels stay outside
image pixels. The HTML gallery is usable without Pillow; Pillow adds a sheet.
"""
from pathlib import Path
import hashlib
import html
import json
import re
import sys

REPO=Path(__file__).resolve().parents[5]
sys.path.insert(0,str(REPO/'port/frontends/web/tools'))
from png import write_png

root=REPO/'port/reviews/n64-polish/before'
log=root/'diagnostic/ares.log'
manifest=json.loads((root/'manifest.json').read_text())
rom=REPO/'port/frontends/n64/adventure/poketcg-review.z64'
rom_hash=hashlib.sha256(rom.read_bytes()).hexdigest()
labels={'duel-card-list':'Duel: choose a card from the hand',
        'duel-card-detail':'Duel: card detail',
        'duel-coin':'Duel: coin toss',
        'duel-action-menu':'Duel: action menu'}
for m in re.finditer(r'FRAMEBUFFER ([\w-]+) (\d+) (\d+)\n(.*?)FRAMEBUFFER END',log.read_text(errors='replace'),re.S):
    name,w,h,body=m.groups();w,h=int(w),int(h)
    rows=[bytearray(w*4) for _ in range(h)];count=0
    for line in body.splitlines():
        if not line.startswith('PIXELS '):continue
        _,x,y,data=line.split();x,y=int(x),int(y)
        for i in range(0,len(data),4):
            v=int(data[i:i+4],16);at=(x+i//4)*4
            rows[y][at:at+4]=bytes([((v>>11)&31)*255//31,((v>>6)&31)*255//31,((v>>1)&31)*255//31,255]);count+=1
    assert count==w*h,(name,count)
    file=root/(name+'.png');write_png(file,w,h,rows)
    item=dict(file=file.name,label=labels.get(name,name),source=str(log.relative_to(REPO)),
              sha256=hashlib.sha256(file.read_bytes()).hexdigest(),
              capture='Actual duel engine request, existing native UI renderer; RDP completion fenced, before VI filtering',
              diagnostic_mode=3,rom_sha256=rom_hash,dimensions=[w,h])
    manifest['images']=[i for i in manifest['images'] if i['file']!=file.name]+[item]
manifest['baseline_commit']='04c5b482'
manifest['review_rom_sha256']=rom_hash
(root/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
items=manifest['images']
(root/'index.html').write_text('''<!doctype html><meta charset="utf-8"><title>N64 baseline review</title>
<style>body{background:#171923;color:#eee;font:16px system-ui;margin:24px}main{display:grid;grid-template-columns:repeat(auto-fit,minmax(640px,1fr));gap:24px}figure{margin:0}img{width:640px;height:480px;image-rendering:pixelated;max-width:100%;object-fit:contain}figcaption{padding:8px 0}p{max-width:900px;line-height:1.5}</style>
<h1>N64 baseline review</h1><p>Native 320 × 240 RDP captures before VI antialiasing and dedithering. Labels are outside the game images. Assess art direction, composition, lighting, shadows, material and texture variety, character scale, camera, UI hierarchy, and how to make the game feel finished. Compare final VI scanouts separately when present.</p><main>'''+''.join('<figure><a href="'+html.escape(i['file'])+'"><img src="'+html.escape(i['file'])+'"></a><figcaption>'+html.escape(i['label'])+'</figcaption></figure>' for i in items)+'</main>')
try:
    from PIL import Image,ImageDraw
    width=1280;tile_h=266
    sheet=Image.new('RGB',(width,((len(items)+3)//4)*tile_h),(20,22,30));draw=ImageDraw.Draw(sheet)
    for n,item in enumerate(items):
        x=(n%4)*320;y=(n//4)*tile_h
        sheet.paste(Image.open(root/item['file']).convert('RGB'),(x,y))
        draw.text((x+5,y+244),item['label'],fill=(238,238,245))
    sheet.save(root/'contact-sheet.png')
except ImportError:
    pass
print(f'{len(items)} labelled native screenshots: {root}')
