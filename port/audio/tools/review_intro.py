#!/usr/bin/env python3
"""Run diagnostic14 with private ares settings and no debugger/frame hooks."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import time

ROOT=Path(__file__).resolve().parents[3]
ADV=ROOT/'port/frontends/n64/adventure'

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rom',type=Path,default=ADV/'poketcg-audio-review.z64')
    parser.add_argument('--binary',type=Path,default=ADV/'build/vi-direct-capture/Poketcg VI Capture.app/Contents/MacOS/ares')
    parser.add_argument('--work',type=Path,default=ADV/'build/audio-review-runtime')
    parser.add_argument('--timeout',type=float,default=240)
    args=parser.parse_args()
    work=args.work.resolve();work.mkdir(parents=True,exist_ok=True)
    (work/'saves').mkdir(parents=True,exist_ok=True)
    settings=work/'settings.bml'
    settings.write_text(f'''Video
  Driver: Metal
  Quality: SD
  Shader: None
  Supersampling: false
  DisableVideoInterfaceProcessing: false
Input
  Driver: SDL
  Defocus: Allow
Audio
  Driver: SDL
  Blocking: true
General
  HomebrewMode: true
  NoFilePrompt: true
Nintendo64
  ExpansionPak: true
Paths
  Saves: {work}/saves/
''')
    log=work/'runtime.log';started=time.monotonic()
    with log.open('w') as output:
        process=subprocess.Popen([str(args.binary.resolve()),'--settings-file',str(settings),
            '--no-file-prompt',str(args.rom.resolve())],stdout=output,stderr=subprocess.STDOUT)
        try:
            while 'AUDIO REVIEW PASS' not in log.read_text(errors='replace'):
                if process.poll() is not None:raise RuntimeError('ares exited before completion')
                if time.monotonic()-started>args.timeout:raise TimeoutError('Audio review timed out')
                time.sleep(.2)
        finally:
            process.terminate()
            try:process.wait(timeout=10)
            except subprocess.TimeoutExpired:process.kill();process.wait()
    text=log.read_text(errors='replace')
    assert not any(error in text for error in ('ASSERTION FAILED','EXCEPTION:','ADVENTURE ERROR:'))
    phases=[dict(zip(('phase','polls','buffers','underruns','max_poll_us','max_mix_us'),
        [phase,*map(int,values)])) for phase,*values in re.findall(
        r'AUDIO REVIEW phase=(\w+) polls=(\d+) buffers=(\d+) underruns=(\d+) max_poll_us=(\d+) max_mix_us=(\d+)',text)]
    assert [phase['phase'] for phase in phases]==['opening','title'],text[-3000:]
    report=dict(rom_sha256=hashlib.sha256(args.rom.read_bytes()).hexdigest(),
        method='Unmodified ares v148 executable code, private settings, diagnostic14, no debugger/frame breakpoints',
        wall_seconds=round(time.monotonic()-started,3),phases=phases,
        scope='AI busy state measures the emulated N64 output queue. Host audio-device underruns and listening quality are not measured.')
    (work/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))

if __name__=='__main__':main()
