#!/usr/bin/env python3
"""Run the diagnostic ROM in ares and extract completed RDP pixels.

Uses isolated settings and 4 MiB RAM. No screenshots of the desktop or simulated
input. The resulting PNG is the N64 framebuffer before VI filtering.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT.parents[3]/'port/frontends/web/tools'))
from png import write_png

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('rom',type=Path)
parser.add_argument('--ares',default='/Applications/ares.app/Contents/MacOS/ares')
parser.add_argument('--timeout',type=float,default=90)
args=parser.parse_args()
out=ROOT/'build/capture'
out.mkdir(parents=True,exist_ok=True)
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
log=out/'ares.log'
start=time.monotonic()
with log.open('w') as stream:
    process=subprocess.Popen([args.ares,'--settings-file',str(settings),
                              '--no-file-prompt',str(args.rom.resolve())],
                             stdout=stream,stderr=subprocess.STDOUT)
    try:
        while process.poll() is None and time.monotonic()-start<args.timeout:
            time.sleep(.5)
            text=log.read_text(errors='replace')
            if any(s in text for s in ('CAPTURE DONE','ASSERTION FAILED','EXCEPTION:')):
                break
    finally:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
text=log.read_text(errors='replace')
if 'CAPTURE DONE' not in text:
    raise SystemExit('Capture failed: '+str(log)+'\n'+text[-3000:])
pixels={}
names=[]
for line in text.splitlines():
    if line.startswith('FRAMEBUFFER ') and line != 'FRAMEBUFFER END':
        _,name,w,h=line.split()
        pixels={}
    elif line.startswith('PIXELS '):
        _,x,y,packed=line.split()
        for i in range(0,len(packed),4):
            value=int(packed[i:i+4],16)
            rgb=tuple(((value>>shift)&31)*255//31 for shift in (11,6,1))+(255,)
            pixels[(int(x)+i//4,int(y))]=rgb
    elif line=='FRAMEBUFFER END':
        assert len(pixels)==320*240, 'Incomplete framebuffer'
        path=ROOT/('assets/n64-'+name+'.png')
        write_png(path,320,240,[bytes(c for x in range(320) for c in pixels[x,y]) for y in range(240)])
        names.append(name)
assert names==['entrance','walking','npc-collision','mason-front','ronald-front','characters-back','walking-pose'],names
(out/'result.json').write_text(json.dumps(dict(rom_sha256=hashlib.sha256(args.rom.read_bytes()).hexdigest(),
    ram_mib=4,frames=names,seconds=round(time.monotonic()-start,2)),indent=2)+'\n')
print('Captured',', '.join(names))
