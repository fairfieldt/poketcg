#!/usr/bin/env python3
"""Native capture of one approved-image modeling iteration and baseline."""
import argparse,json,subprocess,sys
from pathlib import Path
from PIL import Image,ImageDraw,ImageFont
STUDY=Path(__file__).resolve().parents[1];GAME=STUDY.parents[3]/'port/frontends/n64/adventure'
p=argparse.ArgumentParser();p.add_argument('concept');p.add_argument('round');a=p.parse_args()
source=STUDY/'models'/a.concept/f'i{a.round}';assert (source/a.concept/'scene.l64').is_file()
work=STUDY/'work'/f'{a.concept}-i{a.round}';subset=work/'options';subset.mkdir(parents=True,exist_ok=True)
key=f'{a.concept}_i{a.round}';link=subset/key
if not link.exists():link.symlink_to(source,target_is_directory=True)
rom=f'cast-{key}'
with (work/'build.log').open('w') as log:
    subprocess.run(['make','-j8',f'BUILD_DIR={work/"build"}',f'ROM={rom}','DIAGNOSTIC=18',f'MODEL_OPTIONS={subset}','bundle'],cwd=GAME,stdout=log,stderr=subprocess.STDOUT,check=True)
capture=source/'native'
subprocess.run([sys.executable,str(STUDY.parent/'n64-style/tools/capture_options.py'),str(GAME/f'{rom}.z64'),'--output',str(capture),'--timeout','180'],check=True)
font=ImageFont.truetype('/System/Library/Fonts/Helvetica.ttc',18)
sheet=Image.new('RGB',(1280,560),(24,25,30));draw=ImageDraw.Draw(sheet)
for row,cid in enumerate(('baseline',key)):
    draw.text((10,row*280+5),cid,fill='white',font=font)
    for col,view in enumerate((f'portrait-{a.concept}',f'turn-{a.concept}','talk','room')):
        sheet.paste(Image.open(capture/f'{cid}-{view}.png').convert('RGB'),(320*col,30+280*row))
sheet.save(capture/'comparison.png');print(capture/'comparison.png')
