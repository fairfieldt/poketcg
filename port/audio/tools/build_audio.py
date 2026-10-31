#!/usr/bin/env python3
"""Render the original assembled GB sound driver offline into N64 WAV64 assets.

Requires RGBDS, libgme (brew install game-music-emu), libdragon audioconv64.
No Game Boy emulation is linked into the Nintendo 64 runtime.
"""
import argparse
import ctypes
import ctypes.util
import hashlib
import json
import math
from pathlib import Path
import re
import struct
import subprocess
import wave

ROOT=Path(__file__).resolve().parents[1]
REPO=ROOT.parents[1]
OUT=ROOT/'generated'
WORK=ROOT/'build'
RATE=24000
TICK_CYCLES=69632
CLOCK=4194304
SONGS=31
SFX=96

def run(cmd):
    subprocess.run(list(map(str,cmd)),cwd=REPO,check=True)

def symbols(path):
    result={}
    for line in path.read_text().splitlines():
        m=re.match(r'([0-9a-fA-F]+):([0-9a-fA-F]+) (\S+)',line)
        if m:result[m[3]]=(int(m[1],16),int(m[2],16))
    return result

def constants(path,prefix):
    return re.findall(r'\bconst\s+('+prefix+r'\w+)',path.read_text())

def harness():
    WORK.mkdir(parents=True,exist_ok=True)
    OUT.mkdir(parents=True,exist_ok=True)
    # Only the original audio WRAM section is linked. Its original address is
    # preserved, and every definition comes directly from the disassembly.
    ram=(REPO/'src/wram.asm').read_text().split('SECTION "WRAM Audio", WRAM0',1)[1].split('INCLUDE "sram.asm"',1)[0]
    ram_path=WORK/'audio_ram.asm'
    ram_path.write_text('SECTION "WRAM Audio", WRAM0[$dd80]\n'+ram+'\nDEF hBankROM EQU $ff80\nEXPORT hBankROM\n')
    callback=(REPO/'src/home/audio_callback.asm').read_text().replace('SECTION \"Audio Callback\", ROM0','SECTION \"Audio Callback\", ROM0[$0440]')
    (WORK/'audio_callback.asm').write_text(callback)
    run(['rgbasm','-I','src/','-o',WORK/'driver.o',ROOT/'tools/driver.asm'])
    run(['rgbasm','-I','src/','-o',WORK/'audio_ram.o',ram_path])
    run(['rgblink','-w','-o',WORK/'driver.gb','-n',WORK/'driver.sym',WORK/'driver.o',WORK/'audio_ram.o'])
    sym=symbols(WORK/'driver.sym');rom=(WORK/'driver.gb').read_bytes()
    # Timer mode4 is 4096 Hz / (256-188) = original 16384 Hz /68 /4.
    header=b'GBS'+bytes([1,SONGS+SFX,1])+struct.pack('<4H',0x400,sym['GBSInit'][1],sym['GBSPlay'][1],0xdff0)+bytes([188,4])
    header+=b'Pokemon Trading Card Game'.ljust(32,b'\0')
    header+=b'Original sound driver / sequences'.ljust(32,b'\0')[:32]
    header+=b'Offline extraction from disassembly'.ljust(32,b'\0')[:32]
    assert len(header)==112
    gbs=WORK/'poketcg.gbs';gbs.write_bytes(header+rom[0x400:])
    return sym,rom,gbs

class Sequence:
    def __init__(self,rom,bank):self.rom=rom;self.bank=bank
    def byte(self,p):return self.rom[self.bank*0x4000+p-0x4000]
    def word(self,p):return self.byte(p)|self.byte(p+1)<<8
    def music(self,pc):
        ticks=0;speed=0;main=pc;loops=[];calls=[];seen={}
        for _ in range(2000000):
            key=(pc,speed,main,tuple(loops),tuple(calls))
            if key in seen:return dict(intro_ticks=seen[key],loop_ticks=ticks-seen[key],end_ticks=ticks)
            seen[key]=ticks
            op=self.byte(pc);pc+=1
            if op<0xd0:
                assert speed,('note before speed',self.bank,hex(pc))
                ticks+=((op&15)+1)*speed %256 or 256
            elif op==0xd0:speed=self.byte(pc);pc+=1
            elif op in (0xda,0xdb) or op>=0xee:return dict(intro_ticks=ticks,loop_ticks=0,end_ticks=ticks)
            elif op==0xdd:main=pc-1
            elif op==0xde:pc=main
            elif op==0xdf:loops.append((pc+1,self.byte(pc) or 256));pc+=1
            elif op==0xe0:
                target,left=loops.pop();left-=1
                if left:loops.append((target,left));pc=target
            elif op==0xe1:pc=self.word(pc)
            elif op==0xe2:calls.append(pc+2);pc=self.word(pc)
            elif op==0xe3:pc=calls.pop()
            elif op in (0xdc,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xeb,0xec,0xed):pc+=1
        raise ValueError('Music sequence did not terminate/cycle')
    def sfx(self,pc):
        ticks=0;loop=None
        for _ in range(2000000):
            op=self.byte(pc);pc+=1;command=op>>4
            if command==0:pc+=1;ticks+=1
            elif command in (1,5,8):pc+=1
            elif command in (2,7):pass
            elif command==3:loop=(pc+1,self.byte(pc) or 256);pc+=1
            elif command==4:
                target,left=loop;left-=1;loop=(target,left)
                if left:pc=target
            elif command==6:ticks+=self.byte(pc) or 256;pc+=1
            elif command==15:return ticks
            else:raise ValueError(('Unknown SFX command',op,hex(pc-1)))
        raise ValueError('SFX sequence did not terminate')

def tracks(sym,rom):
    names=constants(REPO/'src/constants/music_constants.asm','MUSIC_')
    sfx_names=constants(REPO/'src/constants/sfx_constants.asm','SFX_')
    songbanks=sym['SongBanks1'][1];pointers=sym['SongHeaderPointers1'][1]
    seq=Sequence(rom,1);records=[]
    for song,name in enumerate(names):
        bank=seq.byte(songbanks+song);s=Sequence(rom,bank);head=s.word(pointers+song*2);mask=s.byte(head);head+=1
        channels=[]
        for channel in range(4):
            if mask&(1<<channel):
                channels.append(s.music(s.word(head)));head+=2
        periods=[c['loop_ticks'] for c in channels if c['loop_ticks']]
        period=math.lcm(*periods) if periods else 0
        intro=max([c['intro_ticks'] for c in channels] or [0])
        if period:
            # A full least-common-multiple of all channel periods preserves
            # the complete original sequence, including shorter drum loops.
            start=intro+1;end=start+period
        else:start=0;end=max([c['end_ticks'] for c in channels] or [0])+7
        records.append(dict(kind='music',id=song,name=name,track=song,loop_ticks=period,
                            loop_start_ticks=start,end_ticks=end,channels=channels))
    seq=Sequence(rom,3);pointers=sym['SFXHeaderPointers'][1]
    for sound,name in enumerate(sfx_names):
        head=seq.word(pointers+sound*2);mask=seq.byte(head);head+=1
        durations=[]
        for channel in range(4):
            if mask&(1<<channel):durations.append(seq.sfx(seq.word(head)));head+=2
        records.append(dict(kind='sfx',id=sound,name=name,track=SONGS+sound,loop_ticks=0,loop_start_ticks=0,
                            end_ticks=max(durations or [0])+7))
    assert len(records)==127
    return records

class Emulator:
    def __init__(self,gbs):
        path=ctypes.util.find_library('gme') or '/opt/homebrew/lib/libgme.dylib'
        self.lib=ctypes.CDLL(path)
        self.lib.gme_open_file.argtypes=[ctypes.c_char_p,ctypes.POINTER(ctypes.c_void_p),ctypes.c_int]
        self.lib.gme_open_file.restype=ctypes.c_char_p
        self.lib.gme_start_track.argtypes=[ctypes.c_void_p,ctypes.c_int];self.lib.gme_start_track.restype=ctypes.c_char_p
        self.lib.gme_play.argtypes=[ctypes.c_void_p,ctypes.c_int,ctypes.POINTER(ctypes.c_int16)];self.lib.gme_play.restype=ctypes.c_char_p
        self.lib.gme_ignore_silence.argtypes=[ctypes.c_void_p,ctypes.c_int]
        self.lib.gme_enable_accuracy.argtypes=[ctypes.c_void_p,ctypes.c_int]
        self.lib.gme_delete.argtypes=[ctypes.c_void_p]
        self.lib.gme_warning.argtypes=[ctypes.c_void_p];self.lib.gme_warning.restype=ctypes.c_char_p
        self.emu=ctypes.c_void_p()
        self.check(self.lib.gme_open_file(str(gbs).encode(),ctypes.byref(self.emu),RATE))
        self.lib.gme_ignore_silence(self.emu,1);self.lib.gme_enable_accuracy(self.emu,1)
    def check(self,error):
        if error:raise RuntimeError(error.decode())
    def render(self,record):
        self.check(self.lib.gme_start_track(self.emu,record['track']))
        nframes=math.ceil(record['end_ticks']*TICK_CYCLES*RATE/CLOCK)
        nframes=(nframes+15)&~15
        buf=(ctypes.c_int16*(2*nframes))()
        self.check(self.lib.gme_play(self.emu,2*nframes,buf))
        warning=self.lib.gme_warning(self.emu)
        if warning:raise RuntimeError(warning.decode())
        path=WORK/(record['kind']+'_'+str(record['id']).zfill(2)+'.wav')
        with wave.open(str(path),'wb') as f:
            f.setparams((2,2,RATE,nframes,'NONE','not compressed'));f.writeframes(bytes(buf))
        record.update(samples=nframes,rate=RATE,seconds=nframes/RATE,pcm_sha256=hashlib.sha256(bytes(buf)).hexdigest())
        return path
    def close(self):self.lib.gme_delete(self.emu)

def cached_wave(record):
    """Reuse source PCM only when it matches the original driver's manifest."""
    path=WORK/(record['kind']+'_'+str(record['id']).zfill(2)+'.wav')
    try:
        with wave.open(str(path),'rb') as source:
            if source.getparams()[:3]!=(2,2,RATE) or source.getnframes()!=record['samples']:
                return None
            raw=source.readframes(source.getnframes())
        return path if hashlib.sha256(raw).hexdigest()==record['pcm_sha256'] else None
    except (OSError,wave.Error,KeyError):return None

def transport_wave(path,loop_offset=None):
    """Give VADPCM complete DMA blocks without changing the source recording.

    Some audioconv64 versions pad stereo input at a scalar-sample index instead
    of a frame index, erasing 16 frames halfway through a clip. Supplying an
    already aligned input avoids that path and gives the encoded tail zeros.
    """
    with wave.open(str(path),'rb') as source:
        params=source.getparams();frames=source.getnframes()
        raw=source.readframes(frames)
    frame_bytes=params.nchannels*params.sampwidth
    # Do the converter's loop realignment before padding as well. Otherwise
    # its 16-frame loop copy can make a 32-aligned input misaligned again.
    aligned_loop=0
    if loop_offset is not None:
        aligned_loop=(loop_offset+31)&~31
        copied=aligned_loop-loop_offset
        loop=raw[loop_offset*frame_bytes:]
        assert loop and 0<=loop_offset<frames
        raw+=(loop*((copied*frame_bytes+len(loop)-1)//len(loop)))[:copied*frame_bytes]
        frames+=copied
    padded=(frames+31)&~31
    raw+=bytes((padded-frames)*frame_bytes)
    target=WORK/'transport'/path.name;target.parent.mkdir(parents=True,exist_ok=True)
    with wave.open(str(target),'wb') as output:
        output.setparams(params._replace(nframes=padded));output.writeframes(raw)
    return target,padded,frames,aligned_loop

def playback_header(path,frames,loop_start):
    """Keep the original playback duration; DMA padding is transport only."""
    with path.open('r+b') as output:
        output.seek(12)
        output.write(struct.pack('>II',frames,frames-loop_start if loop_start is not None else 0))

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--only',help='Comma-separated MUSIC_/SFX_ names for a quick validation render')
    p.add_argument('--audioconv',default='/opt/libdragon/bin/audioconv64')
    p.add_argument('--metadata-only',action='store_true')
    p.add_argument('--cached',action='store_true',help='Reuse source WAVs with manifest-matching PCM hashes')
    args=p.parse_args()
    sym,rom,gbs=harness();records=tracks(sym,rom)
    print('Timing parsed for',len(records),'original music/SFX sequences; looping music seconds:',sum(r['end_ticks']*TICK_CYCLES/CLOCK for r in records if r['kind']=='music'),flush=True)
    if args.metadata_only:
        (WORK/'timing.json').write_text(json.dumps(dict(tracks=records),indent=2)+'\n');return
    if (args.only or args.cached) and (OUT/'audio.json').exists():
        old=json.loads((OUT/'audio.json').read_text())
        same_driver=old.get('gbs_sha256')==hashlib.sha256(gbs.read_bytes()).hexdigest()
        if args.only and not same_driver:
            p.error('Sound driver changed; rebuild all tracks before using --only.')
        previous={(r['kind'],r['id']):r for r in old.get('tracks',[])} if same_driver else {}
        for r in records:
            prior=previous.get((r['kind'],r['id']),{})
            if all(prior.get(k)==r[k] for k in ('name','end_ticks','loop_start_ticks')):
                r.update(prior)
    emu=None
    for r in records:
        if args.only and r['name'] not in args.only.split(','):continue
        path=cached_wave(r) if args.cached else None
        if path is None:
            if emu is None:emu=Emulator(gbs)
            path=emu.render(r)
        loop=r['loop_ticks']>0
        offset=round(r['loop_start_ticks']*TICK_CYCLES*RATE/CLOCK)//16*16
        transport,r['transport_samples'],r['playback_samples'],aligned=transport_wave(path,offset if loop else None)
        command=[args.audioconv,'--wav-compress','1','--wav-loop',str(loop).lower()]
        if loop:command+=['--wav-loop-offset',str(aligned)]
        command+=['-o',str(OUT),str(transport)]
        run(command)
        r['file']=path.stem+'.wav64';r['loop_start_sample']=offset
        r['playback_loop_start_sample']=aligned
        playback_header(OUT/r['file'],r['playback_samples'],aligned if loop else None)
        print(r['name'],round(r['seconds'],3),'s',flush=True)
    if emu:emu.close()
    priority_bank,priority_addr=sym['Music1_SFXPriorities']
    priority_offset=priority_bank*0x4000+priority_addr-0x4000
    priorities=list(rom[priority_offset:priority_offset+SFX])
    document=dict(sfx_priorities=priorities,method='Original disassembled audio driver and sequences, offline Game Boy APU render via libgme; VADPCM WAV64 for N64',
                  sample_rate=RATE,channels=2,timer_cycles=TICK_CYCLES,timer_hz=CLOCK/TICK_CYCLES,
                  gbs_sha256=hashlib.sha256(gbs.read_bytes()).hexdigest(),tracks=records)
    (OUT/'audio.json').write_text(json.dumps(document,indent=2)+'\n')
    names=['/* Generated by port/audio/tools/build_audio.py. */','#ifndef TCG_AUDIO_ASSETS_H','#define TCG_AUDIO_ASSETS_H',
           'static const char *const tcg_music_paths[31] = {']
    for i in range(31):names.append('    "rom:/audio/music_'+str(i).zfill(2)+'.wav64",')
    names+=['};','static const char *const tcg_sfx_paths[96] = {']
    for i in range(96):names.append('    "rom:/audio/sfx_'+str(i).zfill(2)+'.wav64",')
    names+=['};','static const unsigned char tcg_sfx_priorities[96] = {',','.join(map(str,priorities)),'};','#endif']
    (OUT/'audio_assets.h').write_text('\n'.join(names)+'\n')

if __name__=='__main__':main()
