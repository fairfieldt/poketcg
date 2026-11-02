#!/usr/bin/env python3
"""Build and capture one retained round through the existing native harness."""
import argparse,json,subprocess,sys
from pathlib import Path
from PIL import Image,ImageDraw,ImageFont
STUDY=Path(__file__).resolve().parents[1]
REPO=STUDY.parents[3]
GAME=REPO/'port/frontends/n64/adventure'
p=argparse.ArgumentParser();p.add_argument('round',type=int,choices=(1,2,3));a=p.parse_args()
work=STUDY/'work';subset=work/f'round{a.round}-options';subset.mkdir(parents=True,exist_ok=True)
ids=['baseline']
for folder in sorted((STUDY/'options').glob(f'*_i{a.round}')):
    link=subset/folder.name
    if not link.exists():link.symlink_to(folder,target_is_directory=True)
    ids.append(folder.name)
assert len(ids)==5
rom=f'mark-concepts-round{a.round}'
with (work/f'round{a.round}-build.log').open('w') as log:
    subprocess.run(['make','-j8',f'BUILD_DIR={work/f"round{a.round}-build"}',f'ROM={rom}','DIAGNOSTIC=18',f'MODEL_OPTIONS={subset}','bundle'],cwd=GAME,stdout=log,stderr=subprocess.STDOUT,check=True)
capture=STUDY/'review'/f'round{a.round}'
subprocess.run([sys.executable,str(STUDY.parent/'n64-style/tools/capture_options.py'),str(GAME/f'{rom}.z64'),'--output',str(capture),'--timeout','240'],check=True)
# Rows compare actual native naming, walking turnarounds, conversation and room.
font=ImageFont.truetype('/System/Library/Fonts/Helvetica.ttc',16)
width=320*4+30;height=270*5+30
sheet=Image.new('RGB',(width,height),(24,25,30));draw=ImageDraw.Draw(sheet)
for row,cid in enumerate(ids):
    draw.text((10,row*270+5),cid,fill='white',font=font)
    for col,view in enumerate(('portrait-mark','turn-mark','talk','room')):
        im=Image.open(capture/f'{cid}-{view}.png').convert('RGB')
        sheet.paste(im,(10+320*col,25+270*row))
sheet.save(capture/'comparison.png')
print(capture/'comparison.png')
