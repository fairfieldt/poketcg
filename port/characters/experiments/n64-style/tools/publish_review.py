#!/usr/bin/env python3
"""Assemble the review package: native sheets, Blender renders, textures, index.html, validation."""
import hashlib,json,shutil,sys
from pathlib import Path
from PIL import Image
HERE=Path(__file__).resolve().parent;ROOT=HERE.parent;REPO=ROOT.parents[3]
sys.path.insert(0,str(HERE));import builder
capture=Path(sys.argv[1]).resolve();sheets=Path(sys.argv[2]).resolve();out=Path(sys.argv[3]).resolve()
out.mkdir(parents=True,exist_ok=True)
for sub in ('native','sheets','blender','textures'):(out/sub).mkdir(exist_ok=True)
cap=json.loads((capture/'capture.json').read_text())
for f in cap['frames']:shutil.copy2(capture/(f+'.png'),out/'native'/(f+'.png'))
for p in sheets.glob('*.png'):shutil.copy2(p,out/'sheets'/p.name)
for p in (ROOT/'review').glob('*-front.png'):shutil.copy2(p,out/'blender'/p.name)
for p in (ROOT/'review').glob('*-threequarter.png'):shutil.copy2(p,out/'blender'/p.name)
# texture sheet: every face and torso texture at 4x
files=sorted((ROOT/'options').glob('*/*/textures/*.png'))
cols=6;rows=(len(files)+cols-1)//cols
sheet=Image.new('RGB',(cols*140,rows*150),(30,32,40))
from PIL import ImageDraw
d=ImageDraw.Draw(sheet)
for i,f in enumerate(files):
    im=Image.open(f).convert('RGB').resize((128,128),Image.NEAREST)
    x,y=(i%cols)*140+6,(i//cols)*150+6;sheet.paste(im,(x,y));d.text((x,y+130),f.stem[:22],fill=(220,220,220))
sheet.save(out/'textures'/'texture-sheet.png')
stats={}
for sid in builder.STYLES:
    for slug in builder.DESIGNS:
        m=json.loads((ROOT/'options'/sid/slug/'scene.json').read_text())
        stats[f'{sid}/{slug}']=dict(triangles=m['triangles'],vertices=m['vertices'],batches=m['batches'],textures=m['textures'],
            sha256=hashlib.sha256((ROOT/'options'/sid/slug/'scene.l64').read_bytes()).hexdigest())
validation=dict(rom=cap['rom'],rom_sha256=cap['rom_sha256'],frames=cap['frames'],heap=cap['heap'],exports=stats,
    capture='Native 320x240 RDP RGBA16 framebuffer after the RDP completion fence, before VI filtering; ares, 4 MiB, Tiny3D packed renderer path',
    production_assets_changed=False)
(out/'validation.json').write_text(json.dumps(validation,indent=2)+'\n')
labels={'baseline':'Current production models','a_round':'A · Round chibi','b_pokemon':'B · Nintendo Pokémon 3D','c_zelda':'C · Ocarina townsfolk','d_angular':'D · Angular hard-surface'}
options=['baseline']+list(builder.STYLES)
html=['<!doctype html><meta charset="utf-8"><title>N64 character style directions</title>',
 '<style>body{font:15px/1.45 -apple-system,Helvetica,sans-serif;background:#14161c;color:#e6e6e6;margin:24px}h1,h2{font-weight:600}img{max-width:100%;image-rendering:pixelated;display:block;margin:6px 0 18px}a{color:#8fc1ff}table{border-collapse:collapse}td,th{padding:3px 10px;border-bottom:1px solid #333;text-align:left}</style>',
 '<h1>N64 character style directions · Mark, Dr. Mason, Ronald</h1>',
 '<p>Native ares captures of an isolated DIAGNOSTIC=18 ROM. Production assets unchanged. See <a href="README.md">README.md</a>.</p>']
for v,title in [('talk','Conversation camera'),('room','Laboratory room camera'),('turn-mark','Turnaround · Mark (front, side walking, back, side walking)'),('turn-dr_mason','Turnaround · Dr. Mason'),('turn-ronald','Turnaround · Ronald'),('portrait-mark','Naming portrait · Mark'),('portrait-dr_mason','Naming portrait · Dr. Mason'),('portrait-ronald','Naming portrait · Ronald')]:
    html.append(f'<h2>{title}</h2><img src="sheets/view-{v}.png">')
html.append('<h2>Per-direction sheets</h2>')
for o in options:html.append(f'<h3>{labels[o]}</h3><img src="sheets/{o}.png">')
html.append('<h2>Blender renders (same baked lighting, emission preview)</h2>')
for sid in builder.STYLES:html.append(f'<h3>{labels[sid]}</h3><img src="blender/{sid}-front.png"><img src="blender/{sid}-threequarter.png">')
html.append('<h2>Textures (32×32 RGBA16, shown 4×)</h2><img src="textures/texture-sheet.png">')
html.append('<h2>Export statistics</h2><table><tr><th>Export</th><th>Triangles</th><th>Vertices</th><th>Batches</th></tr>')
for k,v in stats.items():html.append(f'<tr><td>{k}</td><td>{v["triangles"]}</td><td>{v["vertices"]}</td><td>{v["batches"]}</td></tr>')
html.append('</table>')
(out/'index.html').write_text('\n'.join(html)+'\n')
print('published',out)
