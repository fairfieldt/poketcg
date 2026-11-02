#!/usr/bin/env python3
"""Run the DIAGNOSTIC=18 ROM in ares, save native RDP frames and contact sheets."""
import argparse,hashlib,json,re,subprocess,sys,time
from pathlib import Path
from PIL import Image,ImageDraw
HERE=Path(__file__).resolve().parent
REPO=HERE.parents[4]
sys.path.insert(0,str(REPO/'port/frontends/web/tools'))
from png import write_png


def run(rom,out,ares,timeout):
    out.mkdir(parents=True,exist_ok=True)
    settings=out/'settings.bml';log=out/'ares.log';saves=out/'saves';saves.mkdir(exist_ok=True)
    settings.write_text(f'''Video
  Driver: Metal
  Quality: SD
  PixelAccuracy: true
  FixedScale: 2
Input
  Driver: SDL
  Defocus: Allow
General
  HomebrewMode: true
  NoFilePrompt: true
Nintendo64
  ExpansionPak: false
Paths
  Saves: {saves}/
''')
    start=time.monotonic()
    with log.open('w') as stream:
        process=subprocess.Popen([ares,'--settings-file',str(settings),'--no-file-prompt',str(rom.resolve())],stdout=stream,stderr=subprocess.STDOUT)
        try:
            while process.poll() is None and time.monotonic()-start<timeout:
                time.sleep(.5)
                text=log.read_text(errors='replace')
                if any(s in text for s in ('MODEL OPTIONS PASS','ASSERTION FAILED','EXCEPTION:','ADVENTURE ERROR:')):break
        finally:
            process.terminate()
            try:process.wait(timeout=5)
            except subprocess.TimeoutExpired:process.kill();process.wait()
    return log.read_text(errors='replace')


def parse(text,out):
    frames=[];name=None;pixels={};width=height=0
    for line in text.splitlines():
        if line.startswith('FRAMEBUFFER ') and line!='FRAMEBUFFER END':
            _,name,w,h=line.split();width,height=int(w),int(h);pixels={}
        elif line.startswith('PIXELS '):
            _,xs,ys,packed=line.split();x,y=int(xs),int(ys)
            for i in range(0,len(packed),4):
                v=int(packed[i:i+4],16)
                pixels[(x+i//4,y)]=tuple(((v>>shift)&31)*255//31 for shift in (11,6,1))+(255,)
        elif line=='FRAMEBUFFER END':
            assert len(pixels)==width*height,(name,len(pixels))
            path=out/(name+'.png')
            write_png(path,width,height,[bytes(c for x in range(width) for c in pixels[x,y]) for y in range(height)])
            frames.append(name);name=None
    return frames


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('rom',type=Path)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--ares',default='/Applications/ares.app/Contents/MacOS/ares')
    p.add_argument('--timeout',type=float,default=240)
    p.add_argument('--parse-only',action='store_true')
    args=p.parse_args();out=args.output.resolve()
    text=(out/'ares.log').read_text(errors='replace') if args.parse_only else run(args.rom,out,args.ares,args.timeout)
    assert 'MODEL OPTIONS PASS' in text,text[-3000:]
    assert not any(s in text for s in ('ASSERTION FAILED','EXCEPTION:','ADVENTURE ERROR:')),text[-3000:]
    frames=parse(text,out)
    heaps=re.findall(r'OPTIONS HEAP option=(\S+) total=(\d+) used=(\d+) free=(\d+)',text)
    report=dict(rom=str(args.rom),rom_sha256=hashlib.sha256(args.rom.read_bytes()).hexdigest(),frames=frames,
                heap=[dict(option=o,total=int(t),used=int(u),free=int(f)) for o,t,u,f in heaps])
    (out/'capture.json').write_text(json.dumps(report,indent=2)+'\n')
    print('captured',len(frames),'frames');print('\n'.join(frames))


if __name__=='__main__':main()
