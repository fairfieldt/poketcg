#!/usr/bin/env python3
"""Extract all eight exact original CGB medal tilemaps and palettes."""
import argparse
import hashlib
import json
from pathlib import Path
import sys
from PIL import Image

ROOT=Path(__file__).resolve().parents[5]
sys.path.insert(0,str(ROOT/'port/tools'))
import render_maps as maps
OUT=ROOT/'port/frontends/n64/adventure/assets/medals'
NAMES=('grass','science','fire','water','lightning','psychic','rock','fighting')
PALETTES=(1,1,2,3,4,6,5,5)
TILEMAPS=(0x3e,0x3f,0x40,0x41,0x42,0x45,0x44,0x43)

def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def check(out):
    data=json.loads((out/'manifest.json').read_text())
    assert len(data['assets'])==8
    for source,sha in data['sources'].items():
        assert digest(ROOT/source)==sha, f'Medal source changed: {source}; regenerate build_medals.py'
    images=[]
    for i,(name,asset) in enumerate(zip(NAMES,data['assets'])):
        assert asset['name']==name and asset['event']==8+i and asset['flag']==0x80>>i
        assert asset['tilemap_id']==TILEMAPS[i] and asset['palette']==PALETTES[i]
        path=out/asset['file'];image=Image.open(path).convert('RGBA');images.append(image.tobytes())
        assert image.size==(24,24) and digest(path)==asset['sha256']
        colors={image.getpixel((x,y)) for y in range(24) for x in range(24)}
        assert len(colors)==4 and all(c[3]==255 for c in colors)
        assert (231,231,198,255) in colors
        assert sorted([list(c) for c in colors])==asset['colors']
    assert len(set(images))==8,'Medal designs must be distinct'
    print('MEDAL ART PASS: eight exact original24x24 designs, source palettes, event bits and non-linear tilemap mapping')

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--out',type=Path,default=OUT);parser.add_argument('--check',action='store_true');args=parser.parse_args()
    if args.check:check(args.out);return
    game=maps.Game(str(ROOT));args.out.mkdir(parents=True,exist_ok=True)
    tc=game.consts['tilemap_constants.asm'];pc=game.consts['palette_constants.asm']
    loaded,bgp,count=game.bg_palettes([pc['PALETTE_MEDALS']]);assert count==6
    # DrawCollectedMedals loads these six palettes at BG palette index1.
    palettes=[loaded[0]]+loaded[:6]+[loaded[0]]
    assets=[]
    for i,name in enumerate(NAMES):
        tilemap_id=tc['TILEMAP_'+name.upper()+'_MEDAL'];assert tilemap_id==TILEMAPS[i]
        tm=game.load_tilemap(tilemap_id);assert tm['w']==tm['h']==3
        assert {a for row in tm['attrs'] for a in row}=={PALETTES[i]}
        # The assembled MedalGfx stream rearranges the source sheet. Read its
        # actual VRAM data; Psychic in particular does not use9 consecutive tiles.
        bg=maps.render_bg(tm,game.load_tileset_vram(tm['tileset']),palettes,bgp)
        image=Image.new('RGBA',(24,24));image.putdata([(*c,255) for row in bg.px for c in row])
        path=args.out/(name+'.png');image.save(path)
        assets.append({'name':name,'file':path.name,'event':8+i,'flag':0x80>>i,'tilemap_id':tilemap_id,'palette':PALETTES[i],
          'sha256':digest(path),'colors':sorted([list(c) for c in {image.getpixel((x,y)) for y in range(24) for x in range(24)}])})
    sources=['src/gfx/medals.png','src/gfx.asm','src/data/palettes2.asm','src/engine/menus/medal.asm','src/engine/menus/print_stats.asm']
    sources += [f'src/data/maps/tiles/cgb/{name}_medal.bin' for name in NAMES]
    manifest={'format':'Exact opaque CGB24x24 composition; cream background and all four palette colors retained',
      'source':'ShowMedalReceivedScreen/DrawCollectedMedals, MedalGfx, individual CGB tilemaps, MedalsPal loaded at BG1',
      'sources':{name:digest(ROOT/name) for name in sources},'assets':assets}
    (args.out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n');check(args.out)

if __name__=='__main__':main()
