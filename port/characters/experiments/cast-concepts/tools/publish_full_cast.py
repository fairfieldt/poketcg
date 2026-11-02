#!/usr/bin/env python3
"""Local review gallery for the completed cast, generated from retained evidence."""
import argparse,html,json,math
from pathlib import Path
from PIL import Image,ImageDraw,ImageFont
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--complete',action='store_true');a=p.parse_args()
rows=json.loads((ROOT/'catalog.json').read_text());rows=[r for r in rows if r['slug'] not in ('torch','legendary_card')]
font=ImageFont.truetype('/System/Library/Fonts/Helvetica.ttc',18)
models=[]
for row in rows:
    slug=row['slug'];name='Mark' if slug=='mark' else row['name']
    finals=sorted(f for f in (ROOT/'models'/slug).glob('i3*') if (f/'native/capture.json').is_file())
    if slug=='mark' and not finals:
        folder=ROOT.parent/'mark-concepts/approved/a_adventure/i3d';prefix='../mark-concepts/approved/a_adventure/i3d';portrait=folder/'native/a_adventure_i3d-portrait-mark.png';turn=folder/'native/a_adventure_i3d-turn-mark.png';concept='../mark-concepts/concepts/a-simple-shirt.png';history='../mark-concepts/index.html'
    else:
        if not finals:
            if a.complete:raise ValueError(f'Missing final {slug}')
            continue
        folder=finals[-1];prefix=str(folder.relative_to(ROOT));history=f'models/{slug}'
        lead=slug in ('dr_mason','ronald') and not (folder/'native/candidate-portrait.png').exists();portrait=folder/'native'/(f'{slug}_{folder.name}-portrait-{slug}.png' if lead else 'candidate-portrait.png');turn=folder/'native'/(f'{slug}_{folder.name}-turn-{slug}.png' if lead else 'candidate-turn.png');concept='../mark-concepts/concepts/a-simple-shirt.png' if slug=='mark' else f'concepts/{"dr-mason" if slug=="dr_mason" else slug}.png'
        if slug in ('dr_mason','tech') and folder.name>='i3e':concept='concepts/round-heads/mason-sam.png'
    audit=json.loads((folder/'audit.json').read_text());assert audit['passed'];audit=audit.get('native',audit)
    models.append(dict(slug=slug,name=name,folder=folder,prefix=prefix,portrait=portrait,turn=turn,concept=concept,history=history,triangles=audit['triangles'],runtime_model=row['runtime_model'],aliases=row['aliases']))
if a.complete:assert len(models)==37
sheet=Image.new('RGB',(7*208,math.ceil(len(models)/7)*340),(24,25,30));draw=ImageDraw.Draw(sheet)
for i,m in enumerate(models):
    x=i%7*208;y=i//7*340;draw.text((x+6,y+5),m['name'],font=font,fill='white')
    sheet.paste(Image.open(m['portrait']).crop((108,44,212,198)).resize((208,308),Image.Resampling.NEAREST),(x,y+30))
(ROOT/'review').mkdir(exist_ok=True);sheet.save(ROOT/'review/full-cast-portraits.png')
page=['<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>Character models</title><style>body{max-width:1440px;margin:24px auto;padding:0 20px;background:#151719;color:#eee;font:16px system-ui}a{color:#a7d5ff}img{max-width:100%;height:auto}.native{image-rendering:pixelated}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(310px,1fr));gap:28px}h1{font-size:26px}h2{font-size:21px}nav{display:flex;gap:20px;flex-wrap:wrap}details{margin:14px 0}input{font:inherit;padding:10px;margin:20px 0;width:min(90%,420px);background:#24282c;color:white;border:1px solid #59636b;border-radius:4px}section[hidden]{display:none}</style><h1>Character models</h1><nav><a href="review/full-cast-portraits.png">View full cast</a><a href="cast.blend">Open Blender cast</a><a href="README.md">Review notes</a><a href="../mark-concepts/index.html">Mark concepts</a></nav><label for="search">Find a character</label><br><input id="search" type="search" placeholder="Name or model family"><div class="grid">']
for m in models:
    slug=m['slug'];label=html.escape(m['name']);prefix=m['prefix'];key=html.escape((slug+' '+m['name']).lower())
    page.extend([f'<section data-search="{key}"><h2>{label}</h2>',f'<a href="{m["concept"]}"><img loading="lazy" src="{m["concept"]}" alt="{label} generated concept"></a>',f'<img class="native" loading="lazy" src="{prefix}/native/{m["turn"].name}" alt="{label} native turnaround">',f'<p><a href="{prefix}/{slug}.blend">Blender model</a> · <a href="{prefix}/{slug}/scene.l64">Native model</a></p>'])
    if slug=='mark':page.append('<a href="../mark-concepts/index.html">Review original Mark rounds</a>')
    for folder in sorted((ROOT/'models'/slug).glob('i*')):
        if not (folder/'native/capture.json').is_file():continue
        link=str(folder.relative_to(ROOT))+'/native/comparison.png'
        page.append(f'<details><summary>Round {html.escape(folder.name[1:])}</summary><a href="{link}"><img loading="lazy" class="native" src="{link}" alt="{label} round {folder.name[1:]}"></a></details>')
    page.append('</section>')
page.append('</div><h2>Appearances and poses</h2><div class="grid">')
variants=[]
for slug,label,concept in [('mania_palette2','Mania — alternate colors','concepts/mania.png'),('boy_palette2','Boy — alternate colors','concepts/boy.png'),('swimmer_palette2','Amanda','concepts/swimmer_palette2.png'),('amy__lying','Amy — lying pose','concepts/amy.png')]:
    finals=sorted(f for f in (ROOT/'models'/slug).glob('i3*') if (f/'native/capture.json').is_file())
    if not finals:
        if a.complete:raise ValueError(f'Missing appearance {slug}')
        continue
    folder=finals[-1];prefix=str(folder.relative_to(ROOT))
    assert (folder/f'{slug}.blend').is_file()
    variants.append(dict(slug=slug,name=label,prefix=prefix,concept=concept))
    page.extend([f'<section data-search="{html.escape((slug+" "+label).lower())}"><h2>{label}</h2>',f'<a href="{prefix}/native/comparison.png"><img class="native" loading="lazy" src="{prefix}/native/comparison.png" alt="{label} comparison"></a>',f'<p><a href="{prefix}/{slug}.blend">Blender model</a> · <a href="{prefix}/{slug}/scene.l64">Native model</a> · <a href="{concept}">Concept</a></p></section>'])
page.append('</div><script>document.getElementById("search").addEventListener("input",e=>{const q=e.target.value.toLowerCase().trim();document.querySelectorAll("section[data-search]").forEach(s=>s.hidden=!s.dataset.search.includes(q));});</script></html>')
(ROOT/'index.html').write_text('\n'.join(page)+'\n')
serial=[{k:(str(v.relative_to(ROOT)) if isinstance(v,Path) and v.is_relative_to(ROOT) else str(v)) if isinstance(v,Path) else v for k,v in m.items()} for m in models]
(ROOT/'review/final-families.json').write_text(json.dumps(serial,indent=2)+'\n')
(ROOT/'review/final-appearances.json').write_text(json.dumps(variants,indent=2)+'\n')
print('Published',len(models),'families')
