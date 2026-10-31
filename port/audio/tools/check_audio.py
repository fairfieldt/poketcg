#!/usr/bin/env python3
"""Validate coverage, WAV64 headers, original sequence timing and PCM identity."""
from pathlib import Path
import hashlib
import json
import array
import math
import os
import struct
import subprocess
import sys
import tempfile
import wave
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from build_audio import Emulator,harness,tracks,cached_wave,transport_wave,playback_header,WORK
out=ROOT/'generated'
data=json.loads((out/'audio.json').read_text())
assert len(data['tracks'])==127
assert {t['id'] for t in data['tracks'] if t['kind']=='music'}==set(range(31))
assert {t['id'] for t in data['tracks'] if t['kind']=='sfx'}==set(range(96))
assert data['sample_rate']==24000 and data['channels']==2
assert len(data['sfx_priorities'])==96
size=0
# A clean checkout has generated WAV64s and the manifest, but no ignored WAV
# cache. Reconstruct any missing original PCM before checking the transport.
sym,rom,gbs=harness()
assert hashlib.sha256(gbs.read_bytes()).hexdigest()==data['gbs_sha256']
emu=Emulator(gbs)
for track in data['tracks']:
    path=out/track['file'];blob=path.read_bytes();size+=len(blob)
    assert blob[:8]==b'WV64\x04\x01\x02\x10',(path,'format')
    rate,length,loop_length=struct.unpack_from('>III',blob,8)
    assert rate==24000 and 0<=length-track['samples']<=32,(path,rate,length,track['samples'])
    assert length==track['playback_samples']
    assert track['transport_samples']%32==0 and 0<=track['transport_samples']-length<32
    assert 0<=loop_length<=length
    if track['loop_ticks']:
        assert loop_length>0
        assert abs((length-loop_length)-track['loop_start_sample'])<32,(path,'loop offset')
        assert length-loop_length==track['playback_loop_start_sample']
        assert (length-loop_length)%32==0,(path,'unaligned decoder loop point')
    else:assert loop_length==0,(path,'finite clip loops')
    # Transport PCM preserves every source frame, performs the original loop
    # realignment, and pads only its tail. Original hashes exclude this padding.
    source=cached_wave(track)
    if source is None:
        rendered=dict(track);source=emu.render(rendered)
        assert rendered['pcm_sha256']==track['pcm_sha256'],(path,'source PCM hash')
    transport,count,playback,aligned=transport_wave(source,track['loop_start_sample'] if track['loop_ticks'] else None)
    assert (count,playback,aligned)==(track['transport_samples'],track['playback_samples'],track['playback_loop_start_sample'])
    with wave.open(str(source),'rb') as f:raw=f.readframes(f.getnframes())
    with wave.open(str(transport),'rb') as f:
        assert f.getnframes()==track['transport_samples']
        padded=f.readframes(f.getnframes())
    assert padded[:len(raw)]==raw,(path,'source PCM changed')
    copied=length-track['samples']
    if copied:
        start=track['loop_start_sample']*4
        assert padded[len(raw):len(raw)+copied*4]==raw[start:start+copied*4]
    assert not any(padded[length*4:]),(path,'uninitialized DMA tail')

def rms(samples):return math.sqrt(sum(v*v for v in samples)/max(1,len(samples)))

# Exercise the installed converter and its real reference decoder. A stereo
# recording16 modulo32 previously lost16 frames halfway through; a misaligned
# loop point could reintroduce that bug after an otherwise correct input pad.
converter=os.environ.get('AUDIOCONV64','/opt/libdragon/bin/audioconv64')
with tempfile.TemporaryDirectory(prefix='poketcg-audio-check-') as temp:
    temp=Path(temp)
    samples=array.array('h',(int(10000+3000*math.sin(i*.11)) if channel==0 else
        int(-6000+2200*math.cos(i*.17)) for i in range(3216) for channel in range(2)))
    source=temp/'stereo.wav'
    with wave.open(str(source),'wb') as f:
        f.setparams((2,2,24000,3216,'NONE','not compressed'));f.writeframes(samples.tobytes())
    for offset in (None,16):
        transport,count,playback,aligned=transport_wave(source,offset)
        encoded=temp/('finite' if offset is None else 'loop');encoded.mkdir()
        command=[converter,'-d','--wav-compress','1','--wav-loop',str(offset is not None).lower()]
        if offset is not None:command+=['--wav-loop-offset',str(aligned)]
        subprocess.run(command+['-o',str(encoded),str(transport)],check=True)
        with wave.open(str(encoded/'stereo.vadpcm.wav'),'rb') as f:
            decoded=array.array('h',f.readframes(f.getnframes()))
        middle=decoded[1608*2:1624*2]
        assert rms(middle)>rms(samples[1608*2:1624*2])*.8,'mid-recording stereo dropout'
        assert rms(decoded[playback*2:])<1000,'non-silent encoded transport tail'
        assert count%32==0

    title=next(t for t in data['tracks'] if t['name']=='MUSIC_TITLESCREEN')
    transport=WORK/'transport/music_01.wav'
    subprocess.run([converter,'-d','--wav-loop','false','-o',str(temp),str(transport)],check=True)
    playback_header(temp/title['file'],title['playback_samples'],None)
    assert (temp/title['file']).read_bytes()==(out/title['file']).read_bytes(),'title encoding is stale'
    with wave.open(str(temp/'music_01.vadpcm.wav'),'rb') as f:
        decoded=array.array('h',f.readframes(f.getnframes()))
    with wave.open(str(WORK/'music_01.wav'),'rb') as f:
        source=array.array('h',f.readframes(f.getnframes()))
    half=title['samples']//2
    assert rms(decoded[half*2:(half+16)*2])>rms(source[half*2:(half+16)*2])*.7,'title midpoint dropout'
# Reassemble and compare sequence timing with every exported track.
current=tracks(sym,rom)
for a,b in zip(current,data['tracks']):
    assert all(a[k]==b[k] for k in ('id','kind','name','loop_ticks','loop_start_ticks','end_ticks'))
# Repeat representative source-driver renders and compare raw PCM hashes.
for name in ('MUSIC_TITLESCREEN','MUSIC_OVERWORLD','MUSIC_CLUB_1','SFX_CURSOR','SFX_INTRO_ORB'):
    original=next(t for t in data['tracks'] if t['name']==name)
    record=dict(original);path=emu.render(record)
    assert record['pcm_sha256']==original['pcm_sha256'],name+' not deterministic'
    with wave.open(str(path),'rb') as f:
        raw=f.readframes(f.getnframes())
    assert any(raw),name+' rendered silence'
emu.close()
print(f'AUDIO PASS: 31 songs, 96 SFX; all headers/loops/timings; unchanged source PCM; stereo finite/loop/title decoder regression; {size/2**20:.2f} MiB WAV64')
