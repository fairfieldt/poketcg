#!/usr/bin/env python3
"""Fresh native-clock teaser with Ronald's dialogue and the legendary-card reveal.

Only recorded frames are held/dropped. No optical flow or motion interpolation.
The original-game audio sources are verified against their production PCM hashes.
"""
import argparse,hashlib,json,re,subprocess,wave
from pathlib import Path
from trailer_v2_clock import CaptureClock
ADV=Path(__file__).resolve().parents[1];ROOT=ADV.parents[3];OUT=ROOT/'port/reviews/trailer/v6';CAP=ADV/'build/trailer-capture'
SOURCES=('intro','ronald','clubs','amy')
def run(args):return subprocess.check_output(args,stderr=subprocess.STDOUT)
def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def grid(t):return round(t*60)/60

def native(events,contains):
 e=next(e for e in events if contains in e['line']);return int(re.search(r'\bvi=(\d+)',e['line'])[1])/60

def old_semantic(segment,contains):
 p=json.loads((ROOT/'port/reviews/trailer/v5/provenance.json').read_text())['captures'][segment]
 e=next(e for e in p['native_markers'] if contains in e['line'])
 return CaptureClock(Path('.'),tuple(p['matched_event_anchors']),(),{}).native_at(e['epoch']-p['raw_zero_epoch'])

def plan(events):
 old=json.loads((ROOT/'port/reviews/trailer/v5/timeline.json').read_text());oldshots={s['name']:s for s in old['shots']}
 table=native(events['intro'],'SHOT mark-at-first-table ');oldtable=old_semantic('intro','SHOT mark-at-first-table ')
 rko=native(events['ronald'],'SHOT ronald-knockout ');oldrko=old_semantic('ronald','SHOT ronald-knockout ')
 ako=native(events['amy'],'SHOT amy-knockout ');oldako=old_semantic('amy','SHOT amy-knockout ')
 def relative(name,anchor,oldanchor):
  s=oldshots[name];return anchor+s['native_start']-oldanchor,anchor+s['native_end']-oldanchor
 introend=table+oldshots['opening-through-dialogue']['native_end']-oldtable
 walk=relative('first-table-walk',table,oldtable);ronald=relative('ronald-attack-knockout',rko,oldrko);amy=relative('amy-final-pokemon-knockout',ako,oldako)
 dialogue=native(events['ronald'],'SHOT ronald-dialogue ')+.05
 board=native(events['ronald'],'SHOT ronald-board ')
 assert board-dialogue>=2.9,('Ronald dialogue needs approximately 3 seconds of actual reading time',board-dialogue)
 dialogue_end=min(dialogue+3,board-1/60)
 hall=native(events['amy'],'SHOT hall-reveal ')
 hall_walk=native(events['amy'],'SHOT hall-walk ')
 hall_start=max(hall_walk+.05,hall-3.0)
 glow=native(events['amy'],'SHOT hall-glow ')
 complete=native(events['amy'],'SHOT hall-glow-complete ')
 assert complete-glow>=2.4,('Legendary glow hold too short',complete-glow)
 title=native(events['amy'],'SHOT title64 ')
 cuts=[('opening-through-dialogue','intro',native(events['intro'],'INTRO_RUN ')+.15,introend),('first-table-walk','intro',*walk),('ronald-source-dialogue','ronald',dialogue,dialogue_end),('ronald-attack-knockout','ronald',*ronald)]
 for name,map_id in [('lightning-moving',15),('fire-moving-deep',27),('water-moving',12)]:
  start=native(events['clubs'],f'WALK map={map_id} ')+.1;cuts.append((name,'clubs',start,start+oldshots[name]['duration']))
 cuts += [('amy-final-pokemon-knockout','amy',*amy),('water-medal','amy',native(events['amy'],'SHOT water-medal '),native(events['amy'],'SHOT water-medal-complete ')),('hall-legendary-reveal','amy',hall_start,min(complete,title)-1/60),('title64','amy',title+.1,title+.1+oldshots['title64']['duration'])]
 fades=[0,.35,.12,.20,.08,.08,.12,.20,.25,.65];shots=[];cursor=0
 for i,(name,source,start,end) in enumerate(cuts):
  start,end=grid(start),grid(end);assert end>start,(name,start,end)
  if i:cursor-=fades[i-1]
  shots.append(dict(name=name,source=source,native_start=start,native_end=end,duration=grid(end-start),output_start=grid(cursor),frames=round((end-start)*60)));cursor+=end-start
 return dict(version=6,fps=60,size=[1440,1080],native_framebuffer=[480,360],expansion_ram_bytes=8388608,duration=grid(cursor),frames=round(cursor*60),shots=shots,dissolves=fades,clock='Fresh native VI/60 markers aligned to actual capture PTS. CFR output holds/drops recorded frames; no interpolation.',legendary_glow_native_start=glow,game_fps_claim='Output encoding rate is not native rendering FPS; inspect cadence evidence separately.')

def prepare():
 OUT.mkdir(parents=True,exist_ok=True);clocks={};proof={};events={};crops={}
 for source in SOURCES:
  folder=CAP/f'v6-local-{source}';metadata=json.loads((folder/'capture.json').read_text());assert metadata.get('native_end_seen') and not metadata.get('probe_seconds'),(source,'Final capture incomplete or is only a probe');observations=json.loads((folder/'markers.json').read_text());events[source]=observations
  text=(folder/'runtime.log').read_text();assert 'VIDEO MODE width=480 height=360 ram=8388608 highres=1' in text,(source,'Wrong native resolution')
  v=next(s for s in json.loads((folder/'probe.json').read_text())['streams'] if s['codec_type']=='video');w=1440 if v['width']>=1440 else 720;h=w*3//4;x=(v['width']-w)//2;y=v['height']-h-x;crops[source]=f'crop={w}:{h}:{x}:{y},scale=1440:1080:flags=lanczos'
  log=run(['ffmpeg','-hide_banner','-i',str(folder/'proof.mov'),'-t','18','-vf',crops[source]+',blackdetect=d=0.15:pix_th=0.10:pic_th=0.995','-an','-f','null','-']).decode(errors='replace')
  (folder/'black-calibration.log').write_text(log);black=[(float(a),float(b)) for a,b in re.findall(r'black_start:([0-9.]+) black_end:([0-9.]+)',log)];initial=next((b for a,b in black if a<.2 and b>1),None);assert initial is not None,(source,black)
  first=next(e for e in observations if e['line'].startswith('TRAILER FIRST_FRAME '));origin=first['epoch']-initial;points=[]
  for e in observations:
   vi=re.search(r'\bvi=(\d+)',e['line'])
   if not vi or e['line'].startswith('TRAILER CADENCE '):continue
   point=(int(vi[1])/60,e['epoch']-origin)
   if points and (point[0]<=points[-1][0] or point[1]<=points[-1][1]):continue
   points.append(point)
  assert len(points)>=5,(source,points)
  clocks[source]=CaptureClock(folder,tuple(points),(),{})
  proof[source]=dict(**metadata,raw_zero_epoch=origin,initial_black_end_seconds=initial,crop=crops[source],native_vi_capture_anchors=points,native_markers=observations)
 timeline=plan(events);(OUT/'timeline.json').write_text(json.dumps(timeline,indent=2)+'\n');(OUT/'provenance.json').write_text(json.dumps(dict(captures=proof,native_framebuffer=[480,360],delivery=[1440,1080],interpolation=False),indent=2)+'\n')
 args=['ffmpeg','-hide_banner','-y','-filter_complex_threads','2'];filters=[]
 for source in SOURCES:args+=['-i',str(CAP/f'v6-local-{source}/proof.mov')]
 # Normalize the complete native-clock source before cuts, retaining the frame
 # preceding a cut through VFR gaps. All changes are frame holds/drops.
 counts={source:sum(s['source']==source for s in timeline['shots']) for source in SOURCES};labels={};indexes={source:0 for source in SOURCES}
 for i,source in enumerate(SOURCES):
  outputs=[f'{source}{j}' for j in range(counts[source])];labels[source]=outputs
  filters.append(f'[{i}:v]{crops[source]},settb=AVTB,setpts={clocks[source].setpts_expression()},fps=60:start_time=0,split={counts[source]}'+''.join(f'[{label}]' for label in outputs))
 for i,shot in enumerate(timeline['shots']):
  source=shot['source'];label=labels[source][indexes[source]];indexes[source]+=1
  filters.append(f'[{label}]trim=start={shot["native_start"]}:end={shot["native_end"]},setpts=PTS-STARTPTS,tpad=stop_mode=clone:stop=-1,trim=end_frame={shot["frames"]},settb=1/60,setpts=N,fps=60,format=yuv420p[v{i}]')
 last='v0'
 for i,fade in enumerate(timeline['dissolves'],1):
  filters.append(f'[{last}][v{i}]'+(f'xfade=transition=fade:duration={fade}:offset={timeline["shots"][i]["output_start"]}' if fade else 'concat=n=2:v=1:a=0,fps=60,settb=1/60')+f'[x{i}]');last=f'x{i}'
 filters.append(f'[{last}]fps=60:start_time=0,tpad=stop_mode=clone:stop=-1,trim=end_frame={timeline["frames"]},settb=1/60,setpts=N,fps=60,fade=t=out:st={timeline["duration"]-.8}:d=0.8[vout]')
 script=ADV/'build/trailer-v6-filter.txt';script.write_text(';\n'.join(filters));args+=['-filter_complex_script',str(script),'-map','[vout]','-an','-c:v','libx264','-threads','6','-preset','medium','-crf','18','-pix_fmt','yuv420p','-frames:v',str(timeline['frames']),str(ADV/'build/trailer-v6-video.mp4')];(ADV/'build/trailer-v6-render.json').write_text(json.dumps(args,indent=2)+'\n');print('Prepared v6',timeline['duration'],'seconds',timeline['frames'],'frames')

def render_video():
 with (ADV/'build/trailer-v6-render.log').open('w') as log:subprocess.run(json.loads((ADV/'build/trailer-v6-render.json').read_text()),stdout=log,stderr=subprocess.STDOUT,check=True)

def render_audio():
 timeline=json.loads((OUT/'timeline.json').read_text());shots={s['name']:s for s in timeline['shots']};old={s['name']:s for s in json.loads((ROOT/'port/reviews/trailer/v5/timeline.json').read_text())['shots']};manifest=json.loads((ROOT/'port/audio/generated/audio.json').read_text());build=ADV/'build/trailer-v6-audio';build.mkdir(parents=True,exist_ok=True);verified=[]
 for kind,number in [('music',1),('music',9),('music',12),('music',15),('music',17),('music',29),('sfx',16)]:
  track=next(t for t in manifest['tracks'] if t['kind']==kind and t['id']==number);path=ROOT/f'port/audio/build/{kind}_{number:02}.wav'
  with wave.open(str(path),'rb') as wav:assert (wav.getnchannels(),wav.getsampwidth(),wav.getframerate())==(2,2,24000);pcm=wav.readframes(wav.getnframes())
  assert hashlib.sha256(pcm).hexdigest()==track['pcm_sha256'],path
  verified.append(dict(name=track['name'],path=str(path.relative_to(ROOT)),pcm_sha256=track['pcm_sha256'],wav_sha256=sha(path)))
 cases=[]
 def add(name,path,start,duration,offset,gain,fadein=0,fadeout=0):cases.append(dict(name=name,path=str(path),start=start,duration=duration,offset=offset,gain_db=gain,fadein=fadein,fadeout=fadeout))
 stem=lambda n:ROOT/f'port/audio/build/music_{n:02}.wav'
 opening_end=shots['first-table-walk']['output_start']+shots['first-table-walk']['duration'];add('opening',stem(9),1.5,opening_end,0,-4.2,.005,.35)
 dialogue=shots['ronald-source-dialogue'];add('ronald-dialogue',stem(15),0,dialogue['duration'],dialogue['output_start'],-5.5,.15,.12)
 for name in ['ronald-attack-knockout','amy-final-pokemon-knockout']:
  shot=shots[name];add(name,ROOT/'port/reviews/trailer/v5/poketcg64-trailer.mp4',old[name]['output_start'],shot['duration'],shot['output_start'],0)
 first,last=shots['lightning-moving'],shots['water-moving'];add('clubs',stem(12),.016,last['output_start']+last['duration']-first['output_start'],first['output_start'],-3.7,.2,.12)
 medal=shots['water-medal'];add('full-medal-fanfare',stem(29),0,medal['duration'],medal['output_start'],-5.2)
 hall=shots['hall-legendary-reveal'];hall_audio_start=max(hall['output_start']+.25,medal['output_start']+5.396);hall_duration=hall['output_start']+hall['duration']-hall_audio_start;add('hall',stem(17),0,hall_duration,hall_audio_start,2.6,.1,.65)
 cue=ROOT/'port/audio/build/sfx_16.wav'
 with wave.open(str(cue),'rb') as wav:cue_length=wav.getnframes()/wav.getframerate()
 cue_start=hall['output_start']+timeline['legendary_glow_native_start']-hall['native_start'];add('original-legendary-cards-accent',cue,0,cue_length,cue_start,8,.005,.03)
 title=shots['title64'];add('resolved-title',stem(1),24.636719,title['duration'],title['output_start'],-5.5,.65,.12)
 args=['ffmpeg','-hide_banner','-y'];filters=[]
 for i,c in enumerate(cases):
  args+=['-i',c['path']];chain=f'[{i}:a]atrim=start={c["start"]}:duration={c["duration"]},asetpts=PTS-STARTPTS,aresample=48000,aformat=sample_fmts=fltp:channel_layouts=stereo,volume={c["gain_db"]}dB,apad,atrim=duration={c["duration"]}'
  if c['fadein']:chain+=f',afade=t=in:d={c["fadein"]}'
  if c['fadeout']:chain+=f',afade=t=out:st={max(0,c["duration"]-c["fadeout"])}:d={c["fadeout"]}'
  filters.append(chain+f',adelay={round(c["offset"]*48000)}S:all=1[a{i}]')
 filters.append(''.join(f'[a{i}]' for i in range(len(cases)))+f'amix=inputs={len(cases)}:duration=longest:normalize=0,atrim=duration={timeline["duration"]}[aout]');script=build/'mix.txt';script.write_text(';\n'.join(filters));args+=['-filter_complex_script',str(script),'-map','[aout]','-c:a','aac','-b:a','192k','-ar','48000','-t',str(timeline['duration']),str(ADV/'build/trailer-v6-audio.m4a')]
 with (build/'render.log').open('w') as log:subprocess.run(args,stdout=log,stderr=subprocess.STDOUT,check=True)
 (OUT/'audio-recipe.json').write_text(json.dumps(dict(cases=cases,verified_original_driver_PCM=verified,duel_audio_source=dict(file='port/reviews/trailer/v5/poketcg64-trailer.mp4',sha256=sha(ROOT/'port/reviews/trailer/v5/poketcg64-trailer.mp4')),processing='Original-game sources; fixed linear gains/fades. No generated music, time stretch, compressor or limiter.'),indent=2)+'\n')

def mux():
 subprocess.run(['ffmpeg','-v','error','-y','-i',str(ADV/'build/trailer-v6-video.mp4'),'-i',str(ADV/'build/trailer-v6-audio.m4a'),'-map','0:v','-map','1:a','-c','copy','-movflags','+faststart',str(OUT/'poketcg64-trailer.mp4')],check=True);print(sha(OUT/'poketcg64-trailer.mp4'))
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('action',choices=('prepare','video','audio','mux','render'));a=p.parse_args()
 if a.action=='prepare':prepare()
 elif a.action=='video':render_video()
 elif a.action=='audio':render_audio()
 elif a.action=='mux':mux()
 else:render_video();render_audio();mux()
