#!/usr/bin/env python3
"""Capture every native character in four poses and every room in DIAG5.

The saved pixels are the RDP framebuffer before VI, not window screenshots.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import time
from review_pack import decode_log,write_png

HERE=Path(__file__).resolve().parent
REPO=HERE.parents[4]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('rom',type=Path)
    parser.add_argument('--output',type=Path,default=HERE.parent/'build/visual/captures')
    parser.add_argument('--timeout',type=float,default=600)
    parser.add_argument('--ares',default='/Applications/ares.app/Contents/MacOS/ares')
    parser.add_argument('--parse-only',type=Path)
    args=parser.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    start=time.monotonic();log=out/'ares.log';settings=out/'settings.bml'
    if args.parse_only:log=args.parse_only.resolve()
    else:
        settings.write_text('''Video
  Driver: Metal
  Quality: SD
  PixelAccuracy: true
  Shader: None
Input
  Driver: SDL
  Defocus: Allow
General
  HomebrewMode: true
  NoFilePrompt: true
Nintendo64
  ExpansionPak: false
''')
        with log.open('w') as stream:
            process=subprocess.Popen([args.ares,'--settings-file',str(settings),'--no-file-prompt',str(args.rom.resolve())],stdout=stream,stderr=subprocess.STDOUT)
            try:
                while process.poll() is None and time.monotonic()-start<args.timeout:
                    time.sleep(.5)
                    with log.open('rb') as source:source.seek(max(0,log.stat().st_size-16384));tail=source.read().decode(errors='replace')
                    if any(marker in tail for marker in ('ADVENTURE VISUAL SWEEP PASS','ASSERTION FAILED','EXCEPTION:','ADVENTURE ERROR:')):break
            finally:
                process.terminate()
                try:process.wait(timeout=5)
                except subprocess.TimeoutExpired:process.kill();process.wait()
    text=log.read_text(errors='replace')
    assert 'ADVENTURE VISUAL SWEEP PASS' in text,f'Visual sweep incomplete: {log}\n{text[-3000:]}'
    world=json.loads((REPO/'port/world3d/generated/world.json').read_text())
    models=set(range(1,7))|{row['model'] for row in world['individual_characters']}
    expected={f'actor-{model:03}' for model in models}|{f'map-{model:02}' for model in range(34)}|{f'inter-actor-{model:03}' for model in (7,9,31)}|{'naming'}
    captures=[]
    for name,w,h,rows in decode_log(log):
        target=out/(name+'.png');write_png(target,w,h,rows)
        captures.append(dict(file=target.name,name=name,sha256=hashlib.sha256(target.read_bytes()).hexdigest()))
    assert {row['name'] for row in captures}==expected,(expected-{row['name'] for row in captures},{row['name'] for row in captures}-expected)
    assert len(captures)==len(expected),'Duplicate captures'
    result=dict(passed=True,ram_mib=4,rom=str(args.rom.resolve()),rom_sha256=hashlib.sha256(args.rom.read_bytes()).hexdigest(),models=len(models),imported_models=len(models)-6,maps=34,views=['front','back','side','maximum walking swing'],view_layout='2x2, left to right then top to bottom',depth_comparisons=[7,9,31],capture_stage='Native RDP framebuffer before VI',frames=captures,wall_seconds=round(time.monotonic()-start,2))
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    print(f'VISUAL SWEEP PASS: {len(models)} models x4 views, 34 maps, 3 same-asset depth A/B comparisons; {len(captures)} captures; {out}')

if __name__=='__main__':main()
