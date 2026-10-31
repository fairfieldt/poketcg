#!/usr/bin/env python3
"""Run diagnostic17's real N64 mixer/source-reward regression in private ares."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import time

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('rom',type=Path)
    parser.add_argument('--ares',default='/Applications/ares.app/Contents/MacOS/ares')
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--ram-mib',type=int,choices=(4,8),default=4)
    parser.add_argument('--timeout',type=float,default=180)
    expected=parser.add_mutually_exclusive_group()
    expected.add_argument('--expect-old-crash',action='store_true')
    expected.add_argument('--expect-truncated-stream',action='store_true')
    expected.add_argument('--nested-only',action='store_true')
    args=parser.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    saves=out/'saves';saves.mkdir(exist_ok=True)
    settings=out/'settings.bml';log=out/'ares.log'
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
  ExpansionPak: {'true' if args.ram_mib==8 else 'false'}
Paths
  Saves: {saves}/
''')
    start=time.monotonic()
    with log.open('w') as stream:
        process=subprocess.Popen([args.ares,'--settings-file',str(settings),
            '--no-file-prompt',str(args.rom.resolve())],stdout=stream,stderr=subprocess.STDOUT)
        try:
            while process.poll() is None and time.monotonic()-start<args.timeout:
                time.sleep(.25);text=log.read_text(errors='replace')
                complete='AUDIO NESTED REGRESSION PASS' if args.nested_only else 'AUDIO RESUME REGRESSION PASS'
                if any(x in text for x in (complete,'ASSERTION FAILED','EXCEPTION:')):break
        finally:
            process.terminate()
            try:process.wait(timeout=5)
            except subprocess.TimeoutExpired:process.kill();process.wait()
    text=log.read_text(errors='replace')
    markers=['AUDIO EXACT SEEK REGRESSION PASS','AUDIO LOOP REGRESSION PASS',
        'AUDIO OWNERSHIP REGRESSION PASS','AUDIO SOURCE REWARD PASS',
        'AUDIO HEAP STABILITY PASS','AUDIO MEMORY REGRESSION PASS',
        'AUDIO ALL ASSETS REGRESSION PASS','AUDIO RESUME REGRESSION PASS']
    if args.nested_only:markers=['AUDIO NESTED REGRESSION PASS']
    old_crash=bool(re.search(r'music_09\.wav64: invalid VADPCM seeking point:? 0x9f4f8',text))
    truncated='WAV64 Huffman stream truncated' in text
    memory=[{'map':int(m),'free_bytes':int(f)} for m,f in
        re.findall(r'AUDIO MEMORY map=(\d+) free=(\d+)',text)]
    passed=(old_crash if args.expect_old_crash else truncated if args.expect_truncated_stream else
        all(marker in text for marker in markers) and not any(x in text for x in
        ('ASSERTION FAILED','EXCEPTION:','ADVENTURE ERROR:')) and
        (args.nested_only or [row['map'] for row in memory]==list(range(34))))
    result={'passed':passed,'expected_old_crash':args.expect_old_crash,
        'expected_truncated_stream':args.expect_truncated_stream,'truncated_stream_rejected':truncated,
        'old_crash_reproduced':old_crash,'ram_mib':args.ram_mib,'diagnostic':23 if args.nested_only else 17,
        'rom':str(args.rom.resolve()),'rom_sha256':hashlib.sha256(args.rom.read_bytes()).hexdigest(),
        'wall_seconds':round(time.monotonic()-start,2),
        'markers':[x for x in markers if x in text],
        'exact_sample':None if args.nested_only else 652536,
        'requested_music09_offline_seconds_after_resume':None if args.nested_only else 90,
        'memory_maps':memory,
        'decoded_assets':[{'kind':int(k),'id':int(i)} for k,i in
            re.findall(r'AUDIO ASSET DECODE PASS kind=(\d+) id=(\d+)',text)],
        'source_fixture':('world -> PC -> booster -> PC -> world, 12 cycles' if args.nested_only else
            'ow_new_game -> actual Sam practice outcome -> ow_finish_duel -> original starter reward'),
        'private_save_path':str(saves)}
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))
    assert passed,text[-7000:]

if __name__=='__main__':main()
