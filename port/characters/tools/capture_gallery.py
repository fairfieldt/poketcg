#!/usr/bin/env python3
"""Verify every native appearance/pose/component on emulated 4 MiB hardware."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT.parents[1]/'port/frontends/web/tools'))
from png import write_png
parser=argparse.ArgumentParser()
parser.add_argument('rom',type=Path)
parser.add_argument('--ares',default='/Applications/ares.app/Contents/MacOS/ares')
args=parser.parse_args()
out=ROOT/'build/gallery_capture';out.mkdir(parents=True,exist_ok=True)
settings=out/'settings.bml'
settings.write_text('''Video
  Driver: Metal
  Quality: SD
  PixelAccuracy: true
Input
  Driver: SDL
  Defocus: Block
General
  HomebrewMode: true
  NoFilePrompt: true
Nintendo64
  ExpansionPak: false
''')
log=out/'ares.log';start=time.monotonic()
with log.open('w') as stream:
    process=subprocess.Popen([args.ares,'--settings-file',str(settings),'--no-file-prompt',str(args.rom.resolve())],stdout=stream,stderr=subprocess.STDOUT)
    try:
        while process.poll() is None and time.monotonic()-start<120:
            time.sleep(.5)
            text=log.read_text(errors='replace')
            if any(s in text for s in ('CAST DONE','ASSERTION FAILED','EXCEPTION:')):break
    finally:
        process.terminate()
        try:process.wait(timeout=5)
        except subprocess.TimeoutExpired:process.kill();process.wait()
text=log.read_text(errors='replace')
assert 'CAST DONE 49' in text,text[-2500:]
expected=json.loads((ROOT/'gallery/assets/gallery.json').read_text())['entries']
verified=[];frames=[];pixels={}
(ROOT/'review/n64').mkdir(parents=True,exist_ok=True)
for line in text.splitlines():
    if line.startswith('MODEL_OK '):
        _,index,name=line.split();verified.append((int(index),name))
    elif line.startswith('FRAMEBUFFER ') and line!='FRAMEBUFFER END':
        _,name,w,h=line.split();pixels={}
    elif line.startswith('PIXELS '):
        _,x,y,packed=line.split()
        for i in range(0,len(packed),4):
            value=int(packed[i:i+4],16)
            rgb=tuple(((value>>shift)&31)*255//31 for shift in (11,6,1))+(255,)
            pixels[(int(x)+i//4,int(y))]=rgb
    elif line=='FRAMEBUFFER END':
        assert len(pixels)==320*240
        write_png(ROOT/'review/n64'/(name+'.png'),320,240,
                  [bytes(c for x in range(320) for c in pixels[x,y]) for y in range(240)])
        frames.append(name)
assert verified==[(i,e['name']) for i,e in enumerate(expected)],verified
report=dict(rom_sha256=hashlib.sha256(args.rom.read_bytes()).hexdigest(),ram_mib=4,
            verified=[name for i,name in verified],frames=frames,seconds=round(time.monotonic()-start,2))
(ROOT/'review/n64_validation.json').write_text(json.dumps(report,indent=2)+'\n')
print('N64 verified:',len(verified),'exports, four directions and rigid limb pose each; screenshots:',len(frames))
