#!/usr/bin/env python3
"""Contact sheets from native captures: one per option, plus per-view cross-option sheets."""
import json,sys
from pathlib import Path
from PIL import Image,ImageDraw,ImageFont
capture=Path(sys.argv[1]).resolve();out=Path(sys.argv[2]).resolve();out.mkdir(parents=True,exist_ok=True)
frames=json.loads((capture/'capture.json').read_text())['frames']
options=[];[options.append(f.rsplit('-',1)[0].split('-')[0]) for f in frames if f.split('-')[0] not in options]
views=['room','talk','turn-mark','turn-dr_mason','turn-ronald','portrait-mark','portrait-dr_mason','portrait-ronald']
labels={'baseline':'Current','a_round':'A Round chibi','b_pokemon':'B Pokemon 3D','c_zelda':'C Ocarina townsfolk','d_angular':'D Angular'}
try:font=ImageFont.truetype('/System/Library/Fonts/Helvetica.ttc',18)
except Exception:font=ImageFont.load_default()
def load(name,scale=2):
    p=capture/(name+'.png')
    im=Image.open(p).convert('RGB');return im.resize((im.width*scale,im.height*scale),Image.NEAREST)
S=2;W,H=320*S,240*S
for opt in options:
    sheet=Image.new('RGB',(W*3+40,H*2+90),(16,18,24));d=ImageDraw.Draw(sheet)
    d.text((12,8),labels.get(opt,opt)+' — native 320x240 RDP frames, 2x nearest',fill=(230,230,230),font=font)
    for i,v in enumerate(['room','talk','turn-mark','turn-dr_mason','turn-ronald']):
        im=load(f'{opt}-{v}');x=(i%3)*(W+10)+10;y=40+(i//3)*(H+10);sheet.paste(im,(x,y))
    # portraits: crop the 104x154 naming viewport (x 108..212, y 42..196) and enlarge 3x
    px=W*2+20;py=40+H+10
    for j,slug in enumerate(['mark','dr_mason','ronald']):
        im=Image.open(capture/f'{opt}-portrait-{slug}.png').convert('RGB').crop((108,42,212,196)).resize((104*3,154*3),Image.NEAREST)
        sheet.paste(im,(px+j*(104*3+6),py))
    sheet.save(out/f'{opt}.png')
for v in views:
    n=len(options);cols=min(3,n);rows=(n+cols-1)//cols
    if v.startswith('portrait'):
        tile=(104*3,154*3);imgs=[Image.open(capture/f'{o}-{v}.png').convert('RGB').crop((108,42,212,196)).resize(tile,Image.NEAREST) for o in options]
    else:
        tile=(W,H);imgs=[load(f'{o}-{v}') for o in options]
    sheet=Image.new('RGB',(tile[0]*cols+10*(cols+1),(tile[1]+30)*rows+10),(16,18,24));d=ImageDraw.Draw(sheet)
    for i,(o,im) in enumerate(zip(options,imgs)):
        x=10+(i%cols)*(tile[0]+10);y=10+(i//cols)*(tile[1]+30)
        d.text((x,y),f'{labels.get(o,o)} · {v}',fill=(230,230,230),font=font);sheet.paste(im,(x,y+24))
    sheet.save(out/f'view-{v}.png')
print('sheets in',out)
