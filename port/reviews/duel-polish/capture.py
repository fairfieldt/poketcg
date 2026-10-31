#!/usr/bin/env python3
"""Capture DIAG9 duel UI via ares's original post-VI PNG encoder.

Uses the already verified, isolated debug-entitled ares copy. Never changes the
installed application or the user's settings. Raw callbacks/logs stay in build/.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time
from PIL import Image, ImageDraw, ImageFont

ROOT=Path(__file__).resolve().parents[3]
REVIEW=Path(__file__).resolve().parent
ADV=ROOT/'port/frontends/n64/adventure'
BINARY=ADV/'build/vi-direct-capture/Poketcg VI Capture.app/Contents/MacOS/ares'
sys.path.insert(0,str(ADV/'tools'))
from review_pack import decode_log
sys.path.insert(0,str(ROOT/'port/frontends/web/tools'))
from png import write_png


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('rom',type=Path)
    p.add_argument('stages',nargs='+')
    p.add_argument('--build',type=Path,default=ADV/'build/duel-ui-vi')
    p.add_argument('--timeout',type=int,default=600)
    p.add_argument('--parse-only',action='store_true')
    args=p.parse_args();rom=args.rom.resolve();work=args.build.resolve();work.mkdir(parents=True,exist_ok=True)
    out=REVIEW/'post-vi';native=REVIEW/'native';native.mkdir(exist_ok=True)
    log=work/'lldb.log'
    config=dict(output=str(work),log=str(log),wanted=args.stages,skip_unwanted=0,
                stable_frames=6,timeout_seconds=args.timeout,
                stage_pattern=r'^DUEL UI ([A-Za-z0-9_-]+) BEGIN\r?$',
                label_box=[0,0,640,240],completion_marker='DUEL UI PREVIEW PASS',
                rom=str(rom),rom_sha256=sha(rom))
    (work/'config.json').write_text(json.dumps(config,indent=2)+'\n')
    if not args.parse_only:
        assert BINARY.is_file(),BINARY
        assert not (work/'callback-events.jsonl').exists(),'Choose a fresh build directory'
        settings=f'''Video
  Driver: Metal
  Quality: SD
  Shader: None
  Supersampling: false
  DisableVideoInterfaceProcessing: false
  PixelAccuracy: false
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
  Screenshots: {work}/
  Saves: {work}/saves/
'''
        (work/'settings.bml').write_text(settings)
        script=f'''target create "{BINARY}"
settings set target.disable-aslr false
command script import {out/'vi_capture.py'}
breakpoint set --skip-prologue false --name Program::video
breakpoint command add -F vi_capture.capture 1
run --settings-file {work/'settings.bml'} --no-file-prompt {rom}
process kill
quit
'''
        (work/'capture.lldb').write_text(script)
        env=dict(os.environ,POKETCG_VI_CAPTURE_CONFIG=str(work/'config.json'))
        with log.open('w') as stream:
            proc=subprocess.Popen(['lldb','-b','-s',str(work/'capture.lldb')],env=env,stdout=stream,stderr=subprocess.STDOUT,start_new_session=True)
            try:proc.wait(timeout=args.timeout+60)
            except subprocess.TimeoutExpired:
                import signal
                os.killpg(proc.pid,signal.SIGTERM);proc.wait(timeout=10)
                raise
    records=[json.loads(line) for line in (work/'callback-events.jsonl').read_text().splitlines()]
    finished=[r for r in records if r['event']=='finished']
    assert finished and not finished[-1]['missing'],records[-3:]
    text=log.read_text(errors='replace')
    # LLDB callbacks can interrupt an isviewer PIXELS line; remove only callback
    # output, preserving the partial line on either side, then validate all pixels.
    cleaned=re.sub(r'VI_CALLBACK \{[^\n]*\}\r?\n','',text).replace('\r\n','\n')
    serial=work/'native.log';serial.write_text(cleaned)
    assert 'DUEL UI PREVIEW PASS' in cleaned
    frames=[]
    for name,w,h,rows in decode_log(serial):
        target=native/(name+'.png');write_png(target,w,h,rows)
        frames.append(dict(file=target.name,dimensions=[w,h],sha256=sha(target)))
    assert len(frames)==len(args.stages),(len(frames),args.stages)
    images=[]
    for request in [r for r in records if r['event']=='requestScreenshot']:
        encoded=next(r for r in records if r['event']=='encoded' and r['index']==request['index'])
        source=Path(encoded['file']);png=Image.open(source).convert('RGB')
        assert png.size==(640,240),png.size
        callback=(work/f"callback-{request['index']:02}.bin").read_bytes()
        pitch=request['pitch']
        rgb=bytes(channel for y in range(240) for x in range(640)
                  for channel in (callback[y*pitch+x*4+2],callback[y*pitch+x*4+1],callback[y*pitch+x*4]))
        assert png.tobytes()==rgb,f"Encoder/callback mismatch: {request['stage']}"
        file=request['stage']+'.png';shutil.copy2(source,out/file)
        images.append(dict(file=file,stage=request['stage'],dimensions=[640,240],png_sha256=sha(out/file),
                    callback_sha256=sha(work/f"callback-{request['index']:02}.bin"),
                    rgb_sha256=hashlib.sha256(rgb).hexdigest(),exact_callback_rgb_pixels=153600,
                    callback=request['callbacks']))
    assert {i['stage'] for i in images}==set(args.stages)
    base=json.loads((ROOT/'port/reviews/face-readability/post-vi/capture-provenance.json').read_text())
    provenance=dict(status='verified-duel-polish-captures',rom=dict(file=str(rom.relative_to(ROOT)),sha256=sha(rom),diagnostic=9),
       emulator=base['emulator'],settings=base['settings'],raw_format=base['raw_format'],
       finished=finished[-1],images=images,native_frames=frames,
       native_memory=[line for line in cleaned.splitlines() if line.startswith(('HEAP ','MEMORY ','DUEL UI HEAP '))])
    provenance['emulator']['hook'].update(stage_pattern=config['stage_pattern'],label_box=config['label_box'],stable_label_callbacks=config['stable_frames'])
    (out/'capture-provenance.json').write_text(json.dumps(provenance,indent=2)+'\n')
    print(f"DUEL UI CAPTURE PASS: {len(images)} original post-VI PNGs, {len(frames)} native frames, {len(images)*153600:,} exact RGB pixels")

if __name__=='__main__':main()
