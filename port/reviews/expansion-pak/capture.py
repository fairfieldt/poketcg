#!/usr/bin/env python3
"""Capture native frames and original ares post-VI PNGs for an Expansion Pak trial.

Use a fresh --case for each run. Settings, saves, LLDB logs and callback buffers
remain isolated under build/. This never changes the installed ares application.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import sys
from PIL import Image

ROOT=Path(__file__).resolve().parents[3]
REVIEW=Path(__file__).resolve().parent
ADV=ROOT/'port/frontends/n64/adventure'
BINARY=ADV/'build/vi-direct-capture/Poketcg VI Capture.app/Contents/MacOS/ares'
sys.path.insert(0,str(ROOT/'port/frontends/web/tools'))
from png import write_png


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def clean_log(text):
    """Remove callback output without dropping an interrupted isviewer prefix."""
    # The original ares PNG encoder also prints this notification. Either
    # host message can interrupt the middle of a native hexadecimal pixel.
    return re.sub(r'(?:VI_CALLBACK \{[^\n]*\}|Captured screenshot)\r?\n','',text).replace('\r\n','\n')


def native_frames(text,expected):
    """Decode any native dimensions, checking every RGBA16 pixel exactly once."""
    assert not any(mark in text for mark in ('ASSERTION FAILED','EXCEPTION:','ADVENTURE ERROR:')),text[-3000:]
    seen_names=set()
    for match in re.finditer(r'^FRAMEBUFFER ([\w-]+) (\d+) (\d+)\n(.*?)^FRAMEBUFFER END\s*$',text,re.M|re.S):
        name,width,height,body=match.groups();width,height=int(width),int(height)
        assert (width,height)==expected,(name,width,height,expected)
        assert name not in seen_names,('Duplicate native frame',name)
        seen_names.add(name);rows=[bytearray(width*4) for _ in range(height)]
        seen=bytearray(width*height);count=0
        for line in body.splitlines():
            if not line.startswith('PIXELS '):continue
            _,xs,ys,packed=line.split();x,y=int(xs),int(ys)
            assert len(packed)%4==0 and 0<=y<height,(name,line[:80])
            for i in range(0,len(packed),4):
                px=x+i//4;assert 0<=px<width,(name,px,y)
                at=y*width+px;assert not seen[at],('Duplicate pixel',name,px,y)
                seen[at]=1;count+=1;value=int(packed[i:i+4],16)
                rows[y][px*4:px*4+4]=bytes([((value>>11)&31)*255//31,((value>>6)&31)*255//31,((value>>1)&31)*255//31,255])
        assert count==width*height,(name,count,width*height)
        yield name,width,height,rows


def callback_rgb(raw,pitch,width,height):
    assert len(raw)==pitch*height and pitch>=width*4,(len(raw),pitch,width,height)
    # ares supplies packed BGRA, with optional padding after each valid row.
    output=bytearray(width*height*3)
    for y in range(height):
        row=raw[y*pitch:y*pitch+width*4];base=y*width*3
        output[base:base+width*3:3]=row[2::4]
        output[base+1:base+width*3:3]=row[1::4]
        output[base+2:base+width*3:3]=row[0::4]
    return bytes(output)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('rom',type=Path)
    parser.add_argument('stages',nargs='+',help='Expected console stage slugs, in order')
    parser.add_argument('--case',required=True,help='Safe output name, such as 480x360-8mib')
    parser.add_argument('--ram-mib',type=int,choices=(4,8),default=8)
    parser.add_argument('--width',type=int,default=480,help='Expected native framebuffer width')
    parser.add_argument('--height',type=int,default=360,help='Expected native framebuffer height')
    parser.add_argument('--diagnostic',type=int,default=10)
    parser.add_argument('--build',type=Path)
    parser.add_argument('--timeout',type=int,default=1200)
    parser.add_argument('--stable-frames',type=int,default=6)
    parser.add_argument('--label-box',type=int,nargs=4,metavar=('LEFT','TOP','RIGHT','BOTTOM'),
                        help='Optional stable scanout region; default uses the entire observed frame')
    parser.add_argument('--stage-pattern',default=r'^(?:VIDEO REVIEW|DUEL UI) ([A-Za-z0-9_-]+) BEGIN\r?$')
    parser.add_argument('--marker',default='VIDEO REVIEW PASS')
    parser.add_argument('--prepare-only',action='store_true')
    parser.add_argument('--parse-only',action='store_true')
    args=parser.parse_args()
    assert re.fullmatch(r'[A-Za-z0-9_-]+',args.case),args.case
    assert len(set(args.stages))==len(args.stages),'Duplicate requested stages'
    rom=args.rom.resolve();work=(args.build or ADV/'build/expansion-pak'/args.case).resolve()
    work.mkdir(parents=True,exist_ok=True)
    out=REVIEW/'cases'/args.case;vi=out/'post-vi';native=out/'native'
    vi.mkdir(parents=True,exist_ok=True);native.mkdir(exist_ok=True)
    log=work/'lldb.log';config_path=work/'config.json'
    config=dict(output=str(work),log=str(log),wanted=args.stages,skip_unwanted=0,
        stable_frames=args.stable_frames,minimum_valid_frames=16,timeout_seconds=args.timeout,
        require_native_frame=True,native_lead_frames=6,
        stage_pattern=args.stage_pattern,label_box=args.label_box,completion_marker=args.marker,
        rom=str(rom),rom_sha256=sha(rom),ram_mib=args.ram_mib,
        native_dimensions=[args.width,args.height],diagnostic=args.diagnostic)
    if args.parse_only:
        recorded=json.loads(config_path.read_text())
        for key in ('rom_sha256','ram_mib','native_dimensions','wanted','require_native_frame','native_lead_frames'):
            assert recorded[key]==config[key],('Configuration changed after capture',key)
    else:
        config_path.write_text(json.dumps(config,indent=2)+'\n')
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
  ExpansionPak: {'true' if args.ram_mib==8 else 'false'}
Paths
  Screenshots: {work}/
  Saves: {work}/saves/
'''
        (work/'settings.bml').write_text(settings)
        script=f'''target create "{BINARY}"
settings set target.disable-aslr false
command script import {REVIEW/'post-vi/vi_capture.py'}
breakpoint set --skip-prologue false --name Program::video
breakpoint command add -F vi_capture.capture 1
run --settings-file {work/'settings.bml'} --no-file-prompt {rom}
process kill
quit
'''
        (work/'capture.lldb').write_text(script)
        if args.prepare_only:
            print(f'Prepared isolated capture: {work}');return
        assert BINARY.is_file(),BINARY
        assert not (work/'callback-events.jsonl').exists(),'Choose a fresh --case/build directory'
        env=dict(os.environ,POKETCG_VI_CAPTURE_CONFIG=str(config_path))
        with log.open('w') as stream:
            proc=subprocess.Popen(['lldb','-b','-s',str(work/'capture.lldb')],env=env,
                stdout=stream,stderr=subprocess.STDOUT,start_new_session=True)
            try:proc.wait(timeout=args.timeout+60)
            except subprocess.TimeoutExpired:
                os.killpg(proc.pid,signal.SIGTERM);proc.wait(timeout=10);raise
    records=[json.loads(line) for line in (work/'callback-events.jsonl').read_text().splitlines()]
    finished=[record for record in records if record['event']=='finished']
    assert finished and not finished[-1]['missing'],records[-3:]
    text=clean_log(log.read_text(errors='replace'));(work/'native.log').write_text(text)
    assert args.marker in text,'Native diagnostic did not finish'
    modes=[{key:int(value) for key,value in re.findall(r'(width|height|ram|highres)=(\d+)',line)}
           for line in text.splitlines() if line.startswith('VIDEO MODE ')]
    assert modes,'Runtime video/RAM report missing'
    for mode in modes:
        assert (mode['width'],mode['height'])==(args.width,args.height),mode
        assert mode['ram']==args.ram_mib*1024*1024,mode
    frames=[]
    for name,width,height,rows in native_frames(text,(args.width,args.height)):
        file=native/(name+'.png');write_png(file,width,height,rows)
        frames.append(dict(file=file.name,dimensions=[width,height],sha256=sha(file),exact_native_pixels=width*height))
    assert len(frames)==len(args.stages),(len(frames),args.stages)
    images=[]
    for request in [record for record in records if record['event']=='requestScreenshot']:
        assert request.get('native_frame') in (request['stage'],'duel-ui-'+request['stage']),request
        assert request.get('native_dimensions')==[args.width,args.height],request
        assert request.get('post_native_valid_callbacks',0)>=config['native_lead_frames'],request
        assert request.get('stable_callbacks',0)>=config['stable_frames'],request
        encoded=next(record for record in records if record['event']=='encoded' and record['index']==request['index'])
        source=Path(encoded['file']);png=Image.open(source).convert('RGB')
        width,height,pitch=request['width'],request['height'],request['pitch']
        assert png.size==(width,height),(png.size,width,height)
        callback=work/f"callback-{request['index']:02}.bin"
        rgb=callback_rgb(callback.read_bytes(),pitch,width,height)
        assert png.tobytes()==rgb,f"Original encoder/callback mismatch: {request['stage']}"
        file=vi/(request['stage']+'.png');shutil.copy2(source,file)
        images.append(dict(file=file.name,stage=request['stage'],dimensions=[width,height],pitch=pitch,
            png_sha256=sha(file),callback_sha256=sha(callback),rgb_sha256=hashlib.sha256(rgb).hexdigest(),
            exact_callback_rgb_pixels=width*height,callback=request['callbacks'],
            stage_gate={key:request[key] for key in ('stage_started_callback','native_frame','native_dimensions',
                'native_ready_callback','native_ready_valid_frames','post_native_valid_callbacks',
                'stable_callbacks','previous_captured_sha256')}))
    assert [image['stage'] for image in images]==args.stages,('Missing or reordered stages',images)
    base=json.loads((ROOT/'port/reviews/face-readability/post-vi/capture-provenance.json').read_text())
    base['emulator']['hook'].update(stage_pattern=config['stage_pattern'],label_box=config['label_box'] or 'full observed callback dimensions',
        stable_label_callbacks=config['stable_frames'],require_native_frame=True,native_lead_frames=config['native_lead_frames'])
    base['settings'].update(ram_mib=args.ram_mib,ExpansionPak=args.ram_mib==8)
    provenance=dict(status='verified-expansion-pak-trial-captures',
        rom=dict(file=str(rom.relative_to(ROOT)),sha256=sha(rom),diagnostic=args.diagnostic),
        emulator=base['emulator'],settings=base['settings'],runtime_video_modes=modes,
        raw_format=dict(pixel_order='BGRA little endian; padding excluded from RGB proof',
            dimensions='Recorded per image from Program::video registers',display_aspect='4:3'),
        finished=finished[-1],images=images,native_frames=frames,
        memory=[line for line in text.splitlines() if line.startswith(('HEAP ','MEMORY ','DUEL UI HEAP '))],
        source_logs=dict(path=str(log.relative_to(ROOT)),sha256=sha(log)))
    (out/'capture-provenance.json').write_text(json.dumps(provenance,indent=2)+'\n')
    print(f"EXPANSION PAK CAPTURE PASS: native{args.width}x{args.height}, RAM{args.ram_mib}MiB; {len(images)} original post-VI PNGs, {sum(image['exact_callback_rgb_pixels'] for image in images):,} exact RGB pixels; {out}")

if __name__=='__main__':main()
