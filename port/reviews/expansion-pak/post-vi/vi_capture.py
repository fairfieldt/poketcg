"""Review-only LLDB hook for ares v148 Program::video's original PNG encoder.

The isolated binary/ABI/flag and exact post-VI output were verified in
port/reviews/overworld-roofs/capture-provenance.json. No OS UI is controlled.
"""
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import time

import lldb

config=json.loads(Path(os.environ['POKETCG_VI_CAPTURE_CONFIG']).read_text())
output=Path(config['output']);output.mkdir(parents=True,exist_ok=True)
log=Path(config['log'])
wanted=set(config['wanted'])
started=time.monotonic()
callback_count=valid_count=0
captured={}
last_hash=None
stable=0
stage=None
prior_stage_hash=None
stage_started_callback=None
native_ready_callback=None
native_ready_valid_count=None
native_frame_name=None
native_frame_dimensions=None
log_offset=0
pending=''
finish_count=None
console_finished=False
pending_png=None
stage_re=re.compile(config.get('stage_pattern',r'^(?:VIDEO REVIEW|DUEL UI) ([A-Za-z0-9_-]+) BEGIN\r?$'))
native_re=re.compile(r'^FRAMEBUFFER ([A-Za-z0-9_-]+) (\d+) (\d+)\r?$')


def record(event):
    with (output/'callback-events.jsonl').open('a') as stream:
        stream.write(json.dumps(event,sort_keys=True)+'\n')
    print('VI_CALLBACK '+json.dumps(event,sort_keys=True),flush=True)


def consume_line(line):
    global stage,prior_stage_hash,console_finished,finish_count,last_hash,stable
    global stage_started_callback,native_ready_callback,native_ready_valid_count
    global native_frame_name,native_frame_dimensions
    if config.get('completion_marker') and line.strip()==config['completion_marker']:
        console_finished=True
        record(dict(event='console_finished',marker=line.strip(),callback=callback_count))
        if set(captured)==wanted:finish_count=callback_count+3
    match=stage_re.match(line)
    if match:
        current=' '.join(match.groups())
        if current!=stage:
            # Only an actual captured phase is a valid predecessor. An early
            # queued frame observed under its marker must never become proof.
            prior_stage_hash=captured.get(stage)
            stage=current
            stage_started_callback=callback_count
            last_hash=None;stable=0
            native_ready_callback=native_ready_valid_count=None
            native_frame_name=native_frame_dimensions=None
            record(dict(event='stage',stage=stage,callback=callback_count,
                        previous_captured_sha256=prior_stage_hash))
        return
    native=native_re.match(line)
    if native and stage and native_ready_callback is None:
        name,width,height=native.groups()
        # UI dumps carry a prefix; scene dumps use their exact phase slug.
        if name not in (stage,'duel-ui-'+stage):return
        native_frame_name=name;native_frame_dimensions=[int(width),int(height)]
        native_ready_callback=callback_count;native_ready_valid_count=valid_count
        last_hash=None;stable=0
        record(dict(event='native_ready',stage=stage,native_frame=name,
                    native_dimensions=native_frame_dimensions,callback=callback_count,
                    valid_frames=valid_count))


def update_stage():
    global log_offset,pending
    with log.open(errors='replace') as stream:
        stream.seek(log_offset);chunk=stream.read();log_offset=stream.tell()
    lines=(pending+chunk).split('\n');pending=lines.pop()
    for line in lines:consume_line(line)


def stage_capture_ready(digest):
    if stage not in wanted or stage in captured:return False
    if valid_count<config.get('minimum_valid_frames',16):return False
    if stable<config.get('stable_frames',6) or digest==prior_stage_hash:return False
    if config.get('require_native_frame',True):
        if native_ready_valid_count is None:return False
        if valid_count-native_ready_valid_count<config.get('native_lead_frames',6):return False
    return True


def capture(frame,bp_loc,internal_dict):
    global callback_count,valid_count,last_hash,stable,finish_count,pending_png
    callback_count+=1
    if pending_png is not None:
        # ares filenames have second resolution. Preserve each synchronous
        # encoder result before a consecutive short phase could overwrite it.
        choices=list((output/'Nintendo 64').glob('*.png'))
        assert choices,'Original ares encoder did not write a PNG'
        newest=max(choices,key=lambda path:path.stat().st_mtime_ns)
        assert newest.stat().st_mtime_ns>=pending_png['requested_ns'],'No new encoder result'
        encoded=output/'encoded';encoded.mkdir(exist_ok=True)
        name=f"shot-{pending_png['index']:02}-{pending_png['stage'].lower().replace(' ','-')}.png"
        target=encoded/name;shutil.copy2(newest,target)
        record(dict(event='encoded',index=pending_png['index'],stage=pending_png['stage'],file=str(target),sha256=hashlib.sha256(target.read_bytes()).hexdigest()))
        pending_png=None
    if finish_count is not None and callback_count>=finish_count:
        record(dict(event='finished',callbacks=callback_count,captures=len(captured),missing=sorted(wanted-set(captured))))
        return True
    if time.monotonic()-started>config.get('timeout_seconds',1200):
        record(dict(event='timeout',callbacks=callback_count,captures=len(captured),missing=sorted(wanted-set(captured))))
        return True
    update_stage()
    def reg(name):return frame.FindRegister(name).GetValueAsUnsigned()
    process=frame.GetThread().GetProcess()
    program,data,pitch,width,height=reg('x0'),reg('x2'),reg('w3'),reg('w4'),reg('w5')
    # VI scanout dimensions are independent of native framebuffer size. High
    # resolution/interlace may expose 640x480 instead of the old 640x240.
    if not (320<=width<=2048 and 200<=height<=1152) or pitch<width*4:return False
    expected=config.get('expected_vi_dimensions')
    if expected and [width,height]!=expected:return False
    error=lldb.SBError()
    probe=process.ReadMemory(data+(height//2)*pitch+(width//2)*4,4,error)
    if error.Fail() or len(probe)!=4 or not(struct.unpack('<I',probe)[0]&0xffffff):return False
    valid_count+=1
    label_box=config.get('label_box') or [0,0,width,height]
    lx,ly,rx,ry=label_box
    if not (0<=lx<rx<=width and 0<=ly<ry<=height):raise RuntimeError(f'Invalid capture box {label_box} for {width}x{height}')
    region=process.ReadMemory(data+ly*pitch,(ry-ly)*pitch,error)
    if error.Fail() or len(region)!=(ry-ly)*pitch:return False
    label=b''.join(region[y*pitch+lx*4:y*pitch+rx*4] for y in range(ry-ly))
    digest=hashlib.sha256(struct.pack('<II',width,height)+label).hexdigest()
    stable=stable+1 if digest==last_hash else 1
    last_hash=digest
    if stage not in wanted or stage in captured:
        bp_loc.GetBreakpoint().SetIgnoreCount(config.get('skip_unwanted',4))
        return False
    # The matching native FRAMEBUFFER header is emitted after rendering frame
    # four. Flush at least six subsequent valid VI callbacks, then require six
    # identical hashes. Console marker arrival alone cannot identify scanout.
    if not stage_capture_ready(digest):return False
    raw=process.ReadMemory(data,pitch*height,error)
    if error.Fail() or len(raw)!=pitch*height:raise RuntimeError(str(error))
    index=len(captured);(output/f'callback-{index:02}.bin').write_bytes(raw)
    assert process.ReadMemory(program+0xc5,1,error)==b'\0'
    assert process.WriteMemory(program+0xc5,b'\1',error)==1 and error.Success()
    pending_png=dict(index=index,stage=stage,requested_ns=time.time_ns())
    captured[stage]=digest
    record(dict(event='requestScreenshot',index=index,stage=stage,callbacks=callback_count,
                breakpoint_hits=bp_loc.GetHitCount(),valid_frames=valid_count,
                width=width,height=height,pitch=pitch,label_sha256=digest,
                stage_started_callback=stage_started_callback,
                native_frame=native_frame_name,native_dimensions=native_frame_dimensions,
                native_ready_callback=native_ready_callback,native_ready_valid_frames=native_ready_valid_count,
                post_native_valid_callbacks=valid_count-native_ready_valid_count if native_ready_valid_count is not None else None,
                stable_callbacks=stable,previous_captured_sha256=prior_stage_hash,
                raw_sha256=hashlib.sha256(raw).hexdigest()))
    if set(captured)==wanted and (not config.get('completion_marker') or console_finished):finish_count=callback_count+3
    return False
