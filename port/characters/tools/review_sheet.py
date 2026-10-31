#!/usr/bin/env python3
"""Contact sheet of actual Blender front/back renders and original sprites."""
import argparse
import json
from pathlib import Path
import sys
from PIL import Image,ImageDraw,ImageFont

ROOT=Path(__file__).resolve().parents[1]
REPO=ROOT.parents[1]
sys.path.insert(0,str(REPO/'port/tools'))
import render_maps as maps

parser=argparse.ArgumentParser();parser.add_argument('--partial',action='store_true');args=parser.parse_args()
catalog=json.loads((ROOT/'catalog.json').read_text())
families=[f for f in catalog['families'] if (ROOT/'assets/models'/f['slug']/'complete.json').exists()]
assert args.partial or len(families)==39
font_path='/System/Library/Fonts/Helvetica.ttc'
def font(size):return ImageFont.truetype(font_path,size)
game=maps.Game(str(REPO))
palettes=game.obj_palettes(game.consts['palette_constants.asm']['PALETTE_OVERWORLD_OAM'])
cols,cw,ch,top=4,448,330,76
sheet=Image.new('RGB',(cols*cw,top+((len(families)+cols-1)//cols)*ch+16),'#e9edf3')
draw=ImageDraw.Draw(sheet)
draw.text((20,13),'Pokémon TCG · 3D character models',font=font(28),fill='#172536')
draw.text((20,49),'39 distinct models · 115 NPC entries · Front / back · Original sprite shown below',font=font(16),fill='#526173')
report=[]
for i,f in enumerate(families):
    slug=f['slug'];folder=ROOT/'assets/models'/slug
    x=i%cols*cw;y=top+i//cols*ch
    draw.rounded_rectangle((x+5,y+5,x+cw-5,y+ch-5),radius=7,fill='white')
    for j,name in enumerate(('front','back')):
        pic=Image.open(folder/(name+'.png')).convert('RGB').resize((216,216),Image.Resampling.LANCZOS)
        sheet.paste(pic,(x+8+j*216,y+8))
    a=next(a for a in catalog['appearances'] if a['model']==slug)
    entry=a['representative']
    title={'mark':'Mark','legendary_card':'Legendary card'}.get(slug,entry['name'])
    draw.text((x+12,y+232),title,font=font(19),fill='#172536')
    stats=json.loads((folder/'complete.json').read_text())
    line=f"{stats['triangles']} triangles · {len(f['aliases'])} NPC ID"+('s' if len(f['aliases'])!=1 else '')
    draw.text((x+12,y+259),line,font=font(13),fill='#526173')
    aliases=', '.join(s.removeprefix('NPC_') for s in f['aliases'][:4])
    while draw.textlength(aliases,font=font(11))>310:aliases=aliases[:-4]+'…'
    draw.text((x+12,y+283),aliases,font=font(11),fill='#526173')
    anim=a['anim']+(2 if f['sprite'] not in (8,38,39) else 0)
    frame=game.anim_first_frame(anim)
    native=maps.Image(32,24);native.px=[[(255,255,255)]*32 for _ in range(24)]
    maps.draw_sprite(native,game.sprite_tiles(f['sprite']),frame,8,4,palettes)
    reference=Image.new('RGB',(32,24));reference.putdata([p for row in native.px for p in row])
    reference=reference.resize((96,72),Image.Resampling.NEAREST)
    sheet.paste(reference,(x+cw-110,y+240))
    report.append(dict(f,**stats))
(ROOT/'review').mkdir(exist_ok=True)
sheet.save(ROOT/'review/character_models.png')
(ROOT/'review/build_report.json').write_text(json.dumps(report,indent=2)+'\n')
print('Review sheet:',len(families),'models;',sum(r['linked_parts'] for r in report),'linked part instances')
