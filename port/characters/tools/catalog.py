#!/usr/bin/env python3
"""Audit NPC aliases against actual ROM sprite graphics and animation palettes."""
import hashlib
import json
from pathlib import Path
import sys
from collections import defaultdict
from PIL import Image, ImageDraw, ImageFont

ROOT=Path(__file__).resolve().parents[3]
OUT=ROOT/'port/characters'
sys.path.insert(0,str(ROOT/'port/tools'))
import render_maps as maps

SLUGS={0:'mark',1:'ronald',2:'dr_mason',3:'ishihara',4:'imakuni',5:'nikki',6:'rick',7:'ken',
       8:'amy',9:'isaac',10:'mitch',11:'gene',12:'murray',13:'courtney',14:'steve',15:'jack',
       16:'rod',17:'boy',18:'lad',19:'specs',20:'butch',21:'mania',22:'joshua',23:'hood',
       24:'tech',25:'chap',26:'man',27:'pappy',28:'girl',29:'lass1',30:'lass2',31:'lass3',
       32:'swimmer',33:'clerk',34:'gal',35:'woman',36:'granny',38:'torch',39:'legendary_card'}


def main():
    game=maps.Game(str(ROOT))
    entries=json.loads((ROOT/'port/maps/npc_atlas.json').read_text())
    families={}
    appearances={}
    aliases={}
    palettes=game.obj_palettes(game.consts['palette_constants.asm']['PALETTE_OVERWORLD_OAM'])
    for entry in entries:
        h=entry['header']; sprite=h['sprite']; slug=SLUGS[sprite]
        gfx_hash=hashlib.sha256(b''.join(game.sprite_tiles(sprite))).hexdigest()
        families.setdefault(slug,dict(slug=slug,sprite=sprite,runtime_model=sprite+1,
                                     gfx_sha256=gfx_hash,first_npc=entry['constant'],aliases=[]))
        families[slug]['aliases'].append(entry['constant'])
        anim=h['anim_cgb']
        pose='standing'
        component=None
        if sprite==8:
            pose='lying'
        if sprite==12 and h['flags']&16:
            anim-=3
            pose='west_fixed'
        if sprite==39:
            component=entry['constant'].removeprefix('NPC_LEGENDARY_CARD_').lower()
            anim=65
        key=(slug,anim)
        if key not in appearances:
            count=sum(a['model']==slug for a in appearances.values())
            appearance=slug if not count else slug+'_palette'+str(count+1)
            appearances[key]=dict(slug=appearance,model=slug,anim=anim,
                                  representative=entry,aliases=[])
        a=appearances[key]
        a['aliases'].append(entry['constant'])
        aliases[entry['constant']]=dict(npc_id=entry['id'],model=slug,appearance=a['slug'],
                                      pose=pose,component=component)
    result=dict(families=list(families.values()),appearances=list(appearances.values()),aliases=aliases)
    (OUT/'catalog.json').write_text(json.dumps(result,indent=2)+'\n')
    # Render the ROM itself as a design reference, one appearance per row.
    font=ImageFont.truetype('/System/Library/Fonts/Helvetica.ttc',16)
    rows=list(appearances.values())
    sheet=Image.new('RGB',(1000,((len(rows)+2)//3)*148),'#e9edf3')
    draw=ImageDraw.Draw(sheet)
    for i,a in enumerate(rows):
        entry=a['representative']; h=entry['header']
        sprite=h['sprite']; x=i%3*333; y=i//3*148
        draw.text((x+8,y+4),a['slug']+' / '+entry['name'],font=font,fill='#162336')
        if sprite==8:
            anims=[46,47]
        elif sprite in (38,39):
            anims=[h['anim_cgb']]
        else:
            anims=[a['anim']+k for k in range(4)]
        for j,anim in enumerate(anims):
            frame=game.anim_first_frame(anim)
            im=maps.Image(32,24)
            im.px=[[(233,237,243)]*32 for _ in range(24)]
            maps.draw_sprite(im,game.sprite_tiles(sprite),frame,8,4,palettes)
            pic=Image.new('RGB',(32,24));pic.putdata([p for row in im.px for p in row])
            pic=pic.resize((96,72),Image.Resampling.NEAREST)
            sheet.paste(pic,(x+j*78,y+32))
        draw.text((x+8,y+112),' · '.join(a['aliases'][:2]),font=ImageFont.truetype('/System/Library/Fonts/Helvetica.ttc',11),fill='#465166')
    (OUT/'references').mkdir(exist_ok=True)
    sheet.save(OUT/'references/unique_sprites.png')
    print(f'{len(entries)} NPC IDs → {len(families)} geometry families, {len(appearances)} appearances')


if __name__=='__main__': main()
