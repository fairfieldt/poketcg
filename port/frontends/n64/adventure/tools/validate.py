#!/usr/bin/env python3
"""Validate a diagnostic N64 ROM in ares and capture native RDP pixels.

Protocol: MAP PASS <0..33>, FRAMEBUFFER <name> <width> <height>,
PIXELS <x> <y> <packed RGBA16 hex>, FRAMEBUFFER END,
then ADVENTURE MAP TOUR PASS. Pixels must be read after an RDP completion fence.
No desktop screenshots, input simulation, or GUI settings changes are used.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import time

HERE=Path(__file__).resolve().parent
REPO=HERE.parents[4]
sys.path.insert(0,str(REPO/'port/frontends/web/tools'))
from png import read_png,write_png


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('rom',type=Path)
    p.add_argument('--ares',default='/Applications/ares.app/Contents/MacOS/ares')
    p.add_argument('--output',type=Path,default=HERE.parent/'build/validation')
    p.add_argument('--timeout',type=float,default=180)
    p.add_argument('--marker',default='ADVENTURE MAP TOUR PASS')
    p.add_argument('--frames',type=int,default=42)
    p.add_argument('--width',type=int,default=320,help='Expected native framebuffer width')
    p.add_argument('--height',type=int,default=240,help='Expected native framebuffer height')
    p.add_argument('--ram-mib',type=int,choices=(4,8),default=4,help='Private ares RAM configuration')
    p.add_argument('--parse-only',type=Path,help='Parse an existing emulator log')
    args=p.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    log=out/'ares.log';settings=out/'settings.bml'
    start=time.monotonic()
    if args.parse_only:
        text=args.parse_only.read_text(errors='replace')
    else:
        settings.write_text(f'''Video
  Driver: Metal
  Quality: SD
  PixelAccuracy: true
  FixedScale: 2
Input
  Driver: SDL
  Defocus: Block
General
  HomebrewMode: true
  NoFilePrompt: true
Nintendo64
  ExpansionPak: {'true' if args.ram_mib==8 else 'false'}
''')
        with log.open('w') as stream:
            process=subprocess.Popen([args.ares,'--settings-file',str(settings),'--no-file-prompt',str(args.rom.resolve())],stdout=stream,stderr=subprocess.STDOUT)
            try:
                while process.poll() is None and time.monotonic()-start<args.timeout:
                    time.sleep(.5)
                    text=log.read_text(errors='replace')
                    if any(s in text for s in (args.marker,'ASSERTION FAILED','EXCEPTION:','ADVENTURE ERROR:')):break
            finally:
                process.terminate()
                try:process.wait(timeout=5)
                except subprocess.TimeoutExpired:process.kill();process.wait()
        text=log.read_text(errors='replace')
    assert args.marker in text, f'Diagnostic did not finish: {log}\n{text[-3000:]}'
    assert not any(s in text for s in ('ASSERTION FAILED','EXCEPTION:','ADVENTURE ERROR:')),text[-3000:]
    maps=[int(m[1]) for m in re.finditer(r'^MAP PASS (\d+)\s*$',text,re.M)]
    assert maps==list(range(34)),f'Incomplete/reordered map tour: {maps}'
    frames=[];name=None;pixels={};width=height=0
    for line in text.splitlines():
        if line.startswith('FRAMEBUFFER ') and line!='FRAMEBUFFER END':
            assert name is None, 'Nested framebuffer'
            _,name,w,h=line.split();width,height=int(w),int(h);pixels={}
            assert (width,height)==(args.width,args.height),(name,width,height,args.width,args.height)
            assert re.fullmatch(r'[A-Za-z0-9_-]+',name),name
        elif line.startswith('PIXELS '):
            assert name is not None,'Pixels outside framebuffer'
            _,xs,ys,packed=line.split();x,y=int(xs),int(ys)
            assert len(packed)%4==0 and 0<=y<height
            for i in range(0,len(packed),4):
                v=int(packed[i:i+4],16);pos=(x+i//4,y)
                assert 0<=pos[0]<width and pos not in pixels
                pixels[pos]=tuple(((v>>shift)&31)*255//31 for shift in (11,6,1))+(255,)
        elif line=='FRAMEBUFFER END':
            assert name is not None and len(pixels)==width*height,(name,len(pixels))
            path=out/(name+'.png')
            write_png(path,width,height,[bytes(c for x in range(width) for c in pixels[x,y]) for y in range(height)])
            colors=len(set(pixels.values()))
            assert colors>8,(name,'Blank or failed framebuffer',colors)
            frames.append(dict(name=name,file=path.name,dimensions=[width,height],colors=colors,sha256=hashlib.sha256(path.read_bytes()).hexdigest()))
            name=None
    assert name is None,'Incomplete final framebuffer'
    assert len(frames)==args.frames,f'Expected {args.frames} frames, got {len(frames)}'
    assert len({f['name'] for f in frames})==len(frames),'Duplicate frame names'
    if args.frames>=39:
        assert {'intro-charizard','intro-scyther','intro-aerodactyl','title'} <= {f['name'] for f in frames},'Opening/title frames missing'
        assert 'ADVENTURE INTRO PASS' in text,'Opening source animation check failed'
        if args.frames>=40:assert 'naming' in {f['name'] for f in frames},'Player naming frame missing'
        if args.frames>=42:
            assert {'mason-dialogue','sam-table'} <= {f['name'] for f in frames},'Opening choreography frames missing'
            assert 'ADVENTURE CINEMATIC PASS' in text,'Opening choreography check failed'
    native_duel='ADVENTURE NATIVE DUEL PASS' in text
    if any(f['name']=='duel' for f in frames):assert native_duel,'Native duel did not finish'
    # A compact map-ID-ordered sheet keeps visual QA of every native scene
    # reviewable without opening 34 separate files.
    cols=6;tile_w,tile_h=160,120;rows=(len(frames)+cols-1)//cols
    sheet=[bytearray([12,12,20,255]*(cols*tile_w)) for _ in range(rows*tile_h)]
    for i,frame in enumerate(frames):
        w,h,px,palette=read_png(out/frame['file'])
        ox,oy=(i%cols)*tile_w,(i//cols)*tile_h
        for y in range(tile_h):
            row=bytes(c for x in range(tile_w) for c in px[y*h//tile_h][x*w//tile_w])
            sheet[oy+y][ox*4:(ox+tile_w)*4]=row
    write_png(out/'all-maps-native.png',cols*tile_w,rows*tile_h,sheet)
    # Native diagnostics may add heap measurements; preserve them as evidence.
    memory=[line for line in text.splitlines() if re.match(r'^(HEAP|MEMORY|MEM) ',line)]
    audio=[line for line in text.splitlines() if re.match(r'^AUDIO ',line)]
    heap=[]
    for line in memory:
        fields={k:int(v) for k,v in re.findall(r'(map|total|used|free)=(\d+)',line)}
        if fields:
            assert fields.get('used',0)<fields.get('total',2**31),line
            heap.append(fields)
    assert 'ADVENTURE SRAM PASS' in text,'SRAM diagnostic did not complete'
    assert 'ADVENTURE MIPS STORY PASS' in text,'Native MIPS story diagnostic did not complete'
    worst=min(heap,key=lambda h:h.get('free',2**31)) if heap else None
    video_modes=[{key:int(value) for key,value in re.findall(r'(width|height|ram|highres)=(\d+)',line)}
                 for line in text.splitlines() if line.startswith('VIDEO MODE ')]
    for mode in video_modes:
        assert mode.get('width')==args.width and mode.get('height')==args.height,mode
        assert mode.get('ram')==args.ram_mib*1024*1024,mode
    result=dict(rom=str(args.rom.resolve()),rom_sha256=hashlib.sha256(args.rom.read_bytes()).hexdigest(),
                ram_mib=args.ram_mib,native_dimensions=[args.width,args.height],video_modes=video_modes,passed=True,sram_passed=True,mips_story_passed=True,native_duel_passed=native_duel,intro_passed='ADVENTURE INTRO PASS' in text,maps=maps,frames=frames,memory=memory,heap=heap,worst_heap=worst,audio=audio,
                wall_seconds=round(time.monotonic()-start,2),parse_only=bool(args.parse_only),renderer='ares/paraLLEl-RDP; framebuffer before VI filtering')
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    print(f'ADVENTURE VALIDATION PASS: 34 maps, {len(frames)} native {args.width}x{args.height} RDP captures, {args.ram_mib} MiB RAM; {out}')

if __name__=='__main__':main()
