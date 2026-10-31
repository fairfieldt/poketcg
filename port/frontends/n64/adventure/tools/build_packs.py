#!/usr/bin/env python3
"""Extract the four original CGB booster scenes, including their OBJ logo."""
from pathlib import Path
import argparse
import hashlib
import json
import sys
from PIL import Image

ROOT=Path(__file__).resolve().parents[5]
sys.path.insert(0,str(ROOT/'port/tools'))
import render_maps as maps
from build_intro import scene_vram
OUT=ROOT/'port/frontends/n64/adventure/assets/packs'

def check(out):
    manifest=json.loads((out/'manifest.json').read_text())
    assert len(manifest['assets'])==4
    for asset in manifest['assets']:
        path=out/asset['file'];image=Image.open(path)
        assert image.size==(64,96)
        assert hashlib.sha256(path.read_bytes()).hexdigest()==asset['sha256']
        assert len(set(image.getpixel((x,y)) for y in range(96) for x in range(64)))>8
    print('BOOSTER ART PASS: four original 64x96 CGB scenes and 32-tile OBJ logos')

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--check',action='store_true');parser.add_argument('--out',type=Path,default=OUT);args=parser.parse_args()
    if args.check:check(args.out);return
    args.out.mkdir(parents=True,exist_ok=True);game=maps.Game(str(ROOT))
    tc=game.consts['tilemap_constants.asm'];pc=game.consts['palette_constants.asm'];sc=game.consts['sprite_constants.asm']
    tiles=game.sprite_tiles(sc['SPRITE_BOOSTER_PACK_OAM']);obj=game.obj_palettes(pc['PALETTE_BOOSTER_OAM'])
    bank,pointer=game.rom.addr('BoosterLogoOAM');count=game.rom.byte(bank,pointer);assert count==32
    records=[game.rom.read(bank,pointer+1+i*4,4) for i in range(count)];assets=[]
    for name in ['colosseum','evolution','mystery','laboratory']:
        key=name.upper();tm=game.load_tilemap(tc['TILEMAP_'+key+'_CGB'])
        pals,bgp,_=game.bg_palettes([pc['PALETTE_'+key+'_BOOSTER_CGB']])
        # Palette indices and tilemap attributes both receive the source's
        # +1 offset. Rendering their relative indices preserves the colors.
        bg=maps.render_bg(tm,scene_vram(game,tm['tileset'],0x80),pals,bgp)
        image=Image.new('RGBA',(bg.w,bg.h));image.putdata([(*pixel,255) for row in bg.px for pixel in row])
        for y,x,tile,attrs in reversed(records):
            pixels=maps.decode_tile(tiles[tile]);palette=obj[attrs&7]
            for dy in range(8):
                for dx in range(8):
                    index=pixels[7-dy if attrs&0x40 else dy][7-dx if attrs&0x20 else dx]
                    if index:image.putpixel((x+dx,y+dy),(*palette[index],255))
        path=args.out/(name+'.png');image.save(path)
        assets.append({'file':path.name,'size':[64,96],'tilemap':'TILEMAP_'+key+'_CGB','palette':'PALETTE_'+key+'_BOOSTER_CGB','sha256':hashlib.sha256(path.read_bytes()).hexdigest()})
    (args.out/'manifest.json').write_text(json.dumps({'source':'src/data/scenes.asm + src/engine/scenes.asm:BoosterLogoOAM','art_source':'src/gfx/booster_packs/','format':'Exact CGB tilemap/palette/OBJ composition; no redrawing','assets':assets},indent=2)+'\n');check(args.out)

if __name__=='__main__':main()
