#!/usr/bin/env python3
"""Compare retained native iterations without changing the capture pixels."""
import argparse
from pathlib import Path
from PIL import Image,ImageDraw,ImageFont
p=argparse.ArgumentParser();p.add_argument('concept');a=p.parse_args()
root=Path(__file__).resolve().parents[1]/'approved'/a.concept
rounds=[d for d in sorted(root.glob('i*')) if (d/'native/capture.json').is_file()]
font=ImageFont.truetype('/System/Library/Fonts/Helvetica.ttc',18)
sheet=Image.new('RGB',(1280,270*len(rounds)),(24,25,30));draw=ImageDraw.Draw(sheet)
for row,folder in enumerate(rounds):
    draw.text((10,270*row+5),f'{a.concept} {folder.name}',fill='white',font=font)
    for col,view in enumerate(('portrait-mark','turn-mark','talk','room')):
        sheet.paste(Image.open(folder/'native'/f'{a.concept}_{folder.name}-{view}.png').convert('RGB'),(320*col,270*row+25))
sheet.save(root/'iterations.png');print(root/'iterations.png')
