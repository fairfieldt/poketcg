#!/usr/bin/env python3
"""Validate coverage, WAV64 headers, original sequence timing and PCM identity."""
from pathlib import Path
import hashlib
import json
import struct
import sys
import wave
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from build_audio import Emulator,harness,tracks
out=ROOT/'generated'
data=json.loads((out/'audio.json').read_text())
assert len(data['tracks'])==127
assert {t['id'] for t in data['tracks'] if t['kind']=='music'}==set(range(31))
assert {t['id'] for t in data['tracks'] if t['kind']=='sfx'}==set(range(96))
assert data['sample_rate']==24000 and data['channels']==2
assert len(data['sfx_priorities'])==96
size=0
for track in data['tracks']:
    path=out/track['file'];blob=path.read_bytes();size+=len(blob)
    assert blob[:4]==b'WV64' and blob[6]==2 and blob[7]==16,(path,'format')
    rate,length,loop_length=struct.unpack_from('>III',blob,8)
    assert rate==24000 and 0<=length-track['samples']<=32,(path,rate,length,track['samples'])
    assert 0<=loop_length<=length
    if track['loop_ticks']:
        assert loop_length>0
        assert abs((length-loop_length)-track['loop_start_sample'])<32,(path,'loop offset')
    else:assert loop_length==0,(path,'finite clip loops')
# Reassemble and compare sequence timing with every exported track.
sym,rom,gbs=harness()
assert hashlib.sha256(gbs.read_bytes()).hexdigest()==data['gbs_sha256']
current=tracks(sym,rom)
for a,b in zip(current,data['tracks']):
    assert all(a[k]==b[k] for k in ('id','kind','name','loop_ticks','loop_start_ticks','end_ticks'))
# Repeat three representative source-driver renders and compare raw PCM hashes.
emu=Emulator(gbs)
for name in ('MUSIC_OVERWORLD','MUSIC_CLUB_1','SFX_CURSOR'):
    original=next(t for t in data['tracks'] if t['name']==name)
    record=dict(original);path=emu.render(record)
    assert record['pcm_sha256']==original['pcm_sha256'],name+' not deterministic'
    with wave.open(str(path),'rb') as f:
        raw=f.readframes(f.getnframes())
    assert any(raw),name+' rendered silence'
emu.close()
print(f'AUDIO PASS: 31 songs, 96 SFX; all headers/loops/timings; deterministic source driver PCM; {size/2**20:.2f} MiB WAV64')
