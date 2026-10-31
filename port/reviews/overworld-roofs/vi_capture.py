"""LLDB callback: request ares's own true post-VI PNG encoder, no UI actions.

Only used with the isolated, ad-hoc-signed official v148 development copy.
Verified arm64 Program::video ABI: x0=this,x2=pixels,w3=pitch,w4=w,w5=h.
Verified requestScreenshot flag at this+0xc5 from its load/clear instructions.
"""
import hashlib
import json
import os
from pathlib import Path
import struct

import lldb

config=json.loads(Path(os.environ['POKETCG_VI_CAPTURE_CONFIG']).read_text())
output=Path(config['output']);output.mkdir(parents=True,exist_ok=True)
callback_count=valid_count=0
captured=[]
last_label=None
stable_label=0
finish_count=None


def record(event):
    with (output/'callback-events.jsonl').open('a') as stream:
        stream.write(json.dumps(event,sort_keys=True)+'\n')
    print('VI_CALLBACK '+json.dumps(event,sort_keys=True),flush=True)


def capture(frame,bp_loc,internal_dict):
    global callback_count,valid_count,last_label,stable_label,finish_count
    callback_count+=1
    if finish_count is not None and callback_count>=finish_count:
        record(dict(event='finished',callbacks=callback_count,captures=len(captured)))
        return True
    if callback_count>config.get('max_callbacks',12000):
        record(dict(event='timeout',callbacks=callback_count,captures=len(captured)))
        return True
    def reg(name):return frame.FindRegister(name).GetValueAsUnsigned()
    process=frame.GetThread().GetProcess()
    program,data,pitch,width,height=reg('x0'),reg('x2'),reg('w3'),reg('w4'),reg('w5')
    if (width,height)!=(640,240) or pitch<width*4:
        return False
    error=lldb.SBError()
    probe=process.ReadMemory(data+(height//2)*pitch+(width//2)*4,4,error)
    if error.Fail() or len(probe)!=4 or not(struct.unpack('<I',probe)[0]&0xffffff):
        return False
    valid_count+=1
    # Only the stable story label is hashed; actor/cursor bob never affects it.
    label=b''.join(process.ReadMemory(data+y*pitch+18*4,348*4,error) for y in range(8,23))
    digest=hashlib.sha256(label).hexdigest()
    stable_label=stable_label+1 if digest==last_label else 1
    last_label=digest
    if digest in config.get('ignore_labels',[]):
        # Let debugserver ignore a block of VI callbacks while the unchanged
        # diagnostic runs to its natural next marker phase.
        skip=config.get('skip_on_ignored_label',300)
        bp_loc.GetBreakpoint().SetIgnoreCount(skip)
        record(dict(event='skip_unchanged_label',callbacks=callback_count,
                    breakpoint_hits=bp_loc.GetHitCount(),ignore_count=skip,label_sha256=digest))
        return False
    if valid_count<30 or stable_label<8 or digest in captured:
        return False
    if len(captured)>=config['captures']:
        return False
    raw=process.ReadMemory(data,pitch*height,error)
    if error.Fail() or len(raw)!=pitch*height:
        raise RuntimeError(str(error))
    index=len(captured)
    (output/f'callback-{index:02}.bin').write_bytes(raw)
    before=process.ReadMemory(program+0xc5,1,error)
    assert before==b'\0',before
    written=process.WriteMemory(program+0xc5,b'\1',error)
    assert written==1 and error.Success(),str(error)
    captured.append(digest)
    record(dict(event='requestScreenshot',index=index,callbacks=callback_count,breakpoint_hits=bp_loc.GetHitCount(),valid_frames=valid_count,
                program=hex(program),pixels=hex(data),pitch=pitch,width=width,height=height,
                label_sha256=digest,raw_sha256=hashlib.sha256(raw).hexdigest()))
    if len(captured)==config['captures']:
        # Return from the callback, let the original Program::video consume
        # requestScreenshot and finish encoding, then stop at a later VI frame.
        finish_count=callback_count+3
    return False
