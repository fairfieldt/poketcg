#!/usr/bin/env python3
"""Private lossless copies of manifest-verified original-driver PCM renders."""
from pathlib import Path
import hashlib,json,shutil,subprocess,wave
ROOT=Path(__file__).resolve().parents[5]
SOURCE=ROOT/'port/audio'
OUT=Path(__file__).resolve().parents[1]/'build/trailer-audio'
OUT.mkdir(parents=True,exist_ok=True)
manifest=json.loads((SOURCE/'generated/audio.json').read_text())
gbs=SOURCE/'build/poketcg.gbs'
assert hashlib.sha256(gbs.read_bytes()).hexdigest()==manifest['gbs_sha256']
music={1,4,9,12,13,14,17,18,29}
sfx={1,2,3,8,11,16,17,22,23,84,85,87,88,89,90,91}
selected=[t for t in manifest['tracks'] if (t['kind']=='music' and t['id'] in music) or (t['kind']=='sfx' and t['id'] in sfx)]
records=[]
for t in selected:
 stem=f"{t['kind']}_{t['id']:02d}"
 source=SOURCE/'build'/(stem+'.wav')
 with wave.open(str(source),'rb') as wav:
  params=wav.getparams();raw=wav.readframes(wav.getnframes())
 assert (params.nchannels,params.sampwidth,params.framerate,params.nframes)==(2,2,24000,t['samples'])
 assert hashlib.sha256(raw).hexdigest()==t['pcm_sha256'],stem
 dest=OUT/(stem+'.wav');shutil.copy2(source,dest)
 flac=OUT/(stem+'.flac')
 subprocess.run(['ffmpeg','-v','error','-y','-i',str(dest),'-map_metadata','-1','-c:a','flac',str(flac)],check=True)
 decoded=subprocess.run(['ffmpeg','-v','error','-i',str(flac),'-f','s16le','-acodec','pcm_s16le','-'],stdout=subprocess.PIPE,check=True).stdout
 assert decoded==raw,stem+' FLAC decoded PCM differs'
 r=dict(t);r.update(wav=str(dest),flac=str(flac),wav_sha256=hashlib.sha256(dest.read_bytes()).hexdigest(),flac_sha256=hashlib.sha256(flac.read_bytes()).hexdigest(),source_pcm_verified=True,flac_pcm_verified=True)
 r['duration_seconds']=params.nframes/params.framerate
 r['source_channel_intro_ticks']=[c['intro_ticks'] for c in t.get('channels',[])]
 if t['loop_ticks']:
  r['source_loop_start_seconds']=t['loop_start_sample']/24000
  r['source_loop_end_sample']=t['samples']
  r['source_loop_end_seconds']=t['samples']/24000
  r['source_loop_length_samples']=t['samples']-t['loop_start_sample']
  r['source_loop_length_seconds']=(t['samples']-t['loop_start_sample'])/24000
  r['driver_loop_start_seconds']=t['loop_start_ticks']*manifest['timer_cycles']/4194304
  r['driver_loop_length_seconds']=t['loop_ticks']*manifest['timer_cycles']/4194304
  r['intro_kind']='driver warm-up only; no separate composed intro' if max(r['source_channel_intro_ticks'],default=0)==0 else 'full original channel intros plus driver warm-up; see per-channel ticks'
 else:r['intro_kind']='finite sequence; no loop'
 r['musical_intro_bars']=None
 r['bars_note']='Original bytecode has speed/note lengths, no explicit meter or bar labels; exact driver ticks and sample boundaries supplied without inferred bar counts.'
 records.append(r)
output={'provenance':'Unmodified pre-VADPCM stereo16-bit24kHz PCM rendered by the original disassembled Game Boy sound driver/APU pipeline. Every source PCM hash equals production audio.json; FLAC decodes byte-identically. No WAV64 re-decode, composition, imitation, normalization, resampling or fades.','gbs_sha256':manifest['gbs_sha256'],'sample_rate':24000,'channels':2,'sample_width_bits':16,'source_timer_hz':manifest['timer_hz'],'title_id':1,'id18_is':'MUSIC_CREDITS, optional outro; not the title','montage_music_id':12,'montage_note':'Use MUSIC_CLUB_1 continuously across club cuts; full intro then original complete loop, or enter at source_loop_start_sample. No montage edit is baked into these stems.','records':records}
(OUT/'audio.json').write_text(json.dumps(output,indent=2)+'\n')
print('TRAILER_AUDIO_PASS tracks='+str(len(records))+' original PCM hashes and FLAC roundtrips verified')
