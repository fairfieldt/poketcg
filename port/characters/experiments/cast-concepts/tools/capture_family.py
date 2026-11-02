#!/usr/bin/env python3
"""Before/after family at production cameras; target occupies diagnostic slot 0."""
import argparse,hashlib,json,shutil,struct,subprocess,sys
from pathlib import Path
from PIL import Image,ImageDraw,ImageFont
ROOT=Path(__file__).resolve().parents[1];GAME=ROOT.parents[3]/'port/frontends/n64/adventure';GENERATED=ROOT.parents[3]/'port/world3d/generated'
p=argparse.ArgumentParser(description=__doc__);p.add_argument('slug');p.add_argument('round');p.add_argument('--baseline',type=Path);a=p.parse_args()
source=ROOT/'models'/a.slug/f'i{a.round}';target=source/a.slug;assert (target/'scene.l64').is_file(),target
work=ROOT/'work'/f'family-{a.slug}-i{a.round}';options=work/'options';options.mkdir(parents=True,exist_ok=True)

def copy_native(path,out,textures):
 out.mkdir(parents=True,exist_ok=True);(out/'textures').mkdir(exist_ok=True)
 raw=path.read_bytes();shutil.copy2(path,out/'scene.l64');_,_,nt=struct.unpack_from('>4sHH',raw)
 for k in range(nt):
  name=raw[8+32*k:40+32*k].split(b'\0')[0].decode().replace('.sprite','.png');shutil.copy2(textures/name,out/'textures'/name)

for key in ('before','candidate'):
 folder=options/key;folder.mkdir(exist_ok=True)
 if key=='before':
  baseline=a.baseline or GENERATED/'maps'/f'actor_{a.slug}.l64'
  pngs=baseline.parent/'textures' if a.baseline and (baseline.parent/'textures').is_dir() else GENERATED/'textures'
  copy_native(baseline,folder/'mark',pngs)
 else:copy_native(target/'scene.l64',folder/'mark',target/'textures')
 for control in ('dr_mason','ronald'):
  controlpath=ROOT/'models'/control/'i3'/control
  copy_native(controlpath/'scene.l64',folder/control,controlpath/'textures')
 (folder/'option.json').write_text(json.dumps({'label':f'{a.slug} {"before" if key=="before" else "round "+a.round}'})+'\n')
rom=f'family-{a.slug}-i{a.round}'
with (work/'build.log').open('w') as log:
 subprocess.run(['make','-f','Makefile','-f',str(ROOT/'tools/family_preview.mk'),'-j4',f'BUILD_DIR={work/"build"}',f'ROM={rom}','DIAGNOSTIC=18',f'MODEL_OPTIONS={options}','bundle'],cwd=GAME,stdout=log,stderr=subprocess.STDOUT,check=True)
capture=source/'native'
subprocess.run([sys.executable,str(ROOT.parent/'n64-style/tools/capture_options.py'),str(GAME/f'{rom}.z64'),'--output',str(capture),'--timeout','240'],check=True)
record=json.loads((capture/'capture.json').read_text());assert len(record['frames'])==8,record['frames']
record.update(subject=a.slug,diagnostic_slot='mark',views=['portrait','turn','talk','room'],input_sha256={key:hashlib.sha256((options/key/'mark/scene.l64').read_bytes()).hexdigest() for key in ('before','candidate')})
(capture/'capture.json').write_text(json.dumps(record,indent=2)+'\n')
font=ImageFont.truetype('/System/Library/Fonts/Helvetica.ttc',18);sheet=Image.new('RGB',(1280,560),(24,25,30));draw=ImageDraw.Draw(sheet)
for row,key in enumerate(('before','candidate')):
 draw.text((10,row*280+5),f'{a.slug} — {key}',font=font,fill='white')
 for col,view in enumerate(('portrait','turn','talk','room')):sheet.paste(Image.open(capture/f'{key}-{view}.png').convert('RGB'),(320*col,30+280*row))
sheet.save(capture/'comparison.png');print(capture/'comparison.png')
