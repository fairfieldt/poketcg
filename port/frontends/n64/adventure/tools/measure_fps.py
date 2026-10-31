#!/usr/bin/env python3
"""Collect diagnostic20's warmed production-frame CP0 timings in private ares."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import time

STAGES=('cadence','work','draw','acquire','present','audio')

def parse(text):
    rows=[json.loads(line.removeprefix('FPS_CASE ')) for line in text.splitlines() if line.startswith('FPS_CASE ')]
    names=[row['case'] for row in rows]
    assert len(names)==len(set(names)),'Duplicate case names'
    for row in rows:
        assert row['samples'] in (32,96) and row['warmup']==32,row
        for stage in STAGES:
            metric=row[stage]
            assert 0<=metric['min_us']<=metric['median_us']<=metric['p95_us']<=metric['max_us'],row
        row['mean_game_fps']=round(1e6/max(1,row['cadence']['mean_us']),2)
        row['work_over_30fps_budget_us']=max(0,row['work']['mean_us']-33333)
    return rows

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('rom',type=Path)
    parser.add_argument('--ares',default='/Applications/ares.app/Contents/MacOS/ares')
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--ram-mib',type=int,choices=(4,8),default=4)
    parser.add_argument('--timeout',type=float,default=900)
    parser.add_argument('--parse-only',type=Path)
    parser.add_argument('--focus',action='store_true')
    parser.add_argument('--naming',action='store_true')
    args=parser.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    log=out/'ares.log';start=time.monotonic()
    if args.parse_only:text=args.parse_only.read_text(errors='replace')
    else:
        saves=out/'saves';saves.mkdir(exist_ok=True);settings=out/'settings.bml'
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
        with log.open('w') as stream:
            process=subprocess.Popen([args.ares,'--settings-file',str(settings),
                '--no-file-prompt',str(args.rom.resolve())],stdout=stream,stderr=subprocess.STDOUT)
            try:
                while process.poll() is None and time.monotonic()-start<args.timeout:
                    time.sleep(.5);text=log.read_text(errors='replace')
                    if any(x in text for x in ('FPS_SURVEY_PASS','ASSERTION FAILED','EXCEPTION:','ADVENTURE ERROR:')):break
            finally:
                process.terminate()
                try:process.wait(timeout=5)
                except subprocess.TimeoutExpired:process.kill();process.wait()
        text=log.read_text(errors='replace')
    rows=parse(text);match=re.search(r'FPS_SURVEY_PASS cases=(\d+)',text)
    required={f'map_{i:02d}_entrance' for i in range(34)}|{
        'source_mason_conversation_world','source_sam_table_exploration',
        'source_sam_table_conversation_world','ronald_lobby_conversation_world',
        'intro-charizard','intro-scyther','intro-aerodactyl','intro-title-arrival',
        'title-idle','naming-keyboard'}
    if args.focus:
        required={'source_sam_conversation_world','source_sam_uncached-dialogue-full',
            'source_sam_cached-dialogue-full','source_sam_uncached-dialogue-reveal',
            'source_sam_cached-dialogue-reveal','source_sam_uncached-choice-2',
            'source_sam_cached-choice-2','source_sam_uncached-choice-3',
            'source_sam_cached-choice-3','naming-keyboard'}
    names={r['case'] for r in rows}
    if 'naming-keyboard' not in names:
        required.discard('naming-keyboard');required.add('naming-cached-idle')
    if args.naming:
        required={'naming-uncached-idle','naming-cached-idle','naming-uncached-busy',
            'naming-cached-busy','naming-cached-editing'}
    passed=bool(match and int(match[1])==len(rows) and required<={r['case'] for r in rows} and
        not any(x in text for x in ('ASSERTION FAILED','EXCEPTION:','ADVENTURE ERROR:')))
    dimensions=(480,360) if args.ram_mib==8 else (320,240)
    passed=passed and all((r['width'],r['height'])==dimensions and r['ram_mib']==args.ram_mib for r in rows)
    if args.focus:passed=passed and 'DIALOGUE CACHE PIXELS PASS' in text
    if args.naming:passed=passed and 'NAMING CACHE PIXELS PASS' in text
    result={'passed':passed,'diagnostic':20,'focus':'naming' if args.naming else args.focus,'ram_mib':args.ram_mib,
        'native_dimensions':dimensions,'target_game_fps':30,'timing':'emulated N64 CP0 ticks',
        'rom':str(args.rom.resolve()),'rom_sha256':hashlib.sha256(args.rom.read_bytes()).hexdigest(),
        'wall_seconds':round(time.monotonic()-start,2),'cases':rows,
        'method':f'32 warmup +{32 if args.focus or args.naming else 96} sampled frames per case; persistent scene/UI assets; production draw/present/audio; linker wraps measure display acquisition, presentation and audio; no framebuffer dumps or per-frame logs.',
        'population':'Generic map cases use the full authored NPC definition population. Mason/Sam use actual original opening VM states; Ronald is a controlled source-position lobby fixture.',
        'limits':'Emulator CP0 timings include emulated RSP and host-backed RDP wait behavior; they are not physical-console FPS. Steady frame callbacks exclude controller polling/VM work outside rendering. Work subtracts only display_get acquisition/pacing wait.'}
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    worst=sorted(rows,key=lambda r:r['cadence']['mean_us'],reverse=True)[:8]
    print(json.dumps({'passed':passed,'cases':len(rows),'wall_seconds':result['wall_seconds'],
        'slowest':[{'case':r['case'],'fps':r['mean_game_fps'],'work_us':r['work']['mean_us'],
        'draw_us':r['draw']['mean_us'],'audio_us':r['audio']['mean_us']} for r in worst]},indent=2))
    assert passed,text[-5000:]

if __name__=='__main__':main()
