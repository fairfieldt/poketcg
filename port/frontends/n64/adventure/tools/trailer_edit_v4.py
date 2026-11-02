#!/usr/bin/env python3
"""Edit fresh local post-VI recordings to the existing v3 teaser and audio timeline."""
import argparse,bisect,difflib,hashlib,json,re,subprocess
from pathlib import Path
from trailer_v2_clock import CaptureClock,load_clock
ADV=Path(__file__).resolve().parents[1];ROOT=ADV.parents[3];VERSION=4;OUT=ROOT/f'port/reviews/trailer/v{VERSION}';CAP=ADV/'build/trailer-capture'
SOURCES=('intro','ronald','clubs','amy')
def run(args):return subprocess.check_output(args,stderr=subprocess.STDOUT)
def key(line):return re.sub(r' (?:vi|us)=\d+','',line)
def prep():
 OUT.mkdir(parents=True,exist_ok=True);plan=json.loads((ROOT/'port/reviews/trailer/v3/timeline.json').read_text());oldprofiles=json.loads((ROOT/'port/reviews/trailer/v3/capture-sources.json').read_text())['crt'];clocks={};proofs={};crops={}
 for name in SOURCES:
  d=CAP/f'v{VERSION}-local-{name}';metadata=json.loads((d/'capture.json').read_text());observations=json.loads((d/'markers.json').read_text());probe=json.loads((d/'probe.json').read_text());v=next(s for s in probe['streams'] if s['codec_type']=='video');contentw=1440 if v['width']>=1440 else 720;contenth=contentw*3//4;x=(v['width']-contentw)//2;y=v['height']-contenth-x;crops[name]=f'crop={contentw}:{contenth}:{x}:{y},scale=1440:1080:flags=lanczos'
  # Calibrate the recorder startup using the first nonblack native frame, not
  # the wall-clock launch of screencapture or a rounded QuickTime date field.
  log=run(['ffmpeg','-hide_banner','-i',str(d/'proof.mov'),'-t','18','-vf',crops[name]+',blackdetect=d=0.15:pix_th=0.10:pic_th=0.995','-an','-f','null','-']).decode(errors='replace')
  (d/'black-calibration.log').write_text(log)
  black=[(float(a),float(b)) for a,b in re.findall(r'black_start:([0-9.]+) black_end:([0-9.]+)',log)];initial=next((b for a,b in black if a<.2 and b>1),None);assert initial is not None,(name,black)
  first=next(e for e in observations if e['line'].startswith('TRAILER FIRST_FRAME '));origin=first['epoch']-initial
  old=load_clock(CAP/oldprofiles[name]);matching=difflib.SequenceMatcher(a=[key(e['line']) for e in old.events],b=[key(e['line']) for e in observations],autojunk=False);anchors=[]
  for block in matching.get_matching_blocks():
   for j in range(block.size):
    a=old.events[block.a+j];b=observations[block.b+j];point=(a['native_seconds'],b['epoch']-origin)
    if anchors and (point[0]<=anchors[-1][0] or point[1]<=anchors[-1][1]):continue
    anchors.append(point)
  assert len(anchors)>=5,(name,anchors)
  clocks[name]=CaptureClock(d,tuple(anchors),(),{})
  proofs[name]=dict(**metadata,initial_black_end_seconds=initial,raw_zero_epoch=origin,crop=crops[name],reference_profile=oldprofiles[name],matched_event_anchors=anchors,matched_events=len(anchors),old_events=len(old.events),new_events=len(observations),native_markers=observations)
 plan.update(version=VERSION,existing_footage_only=False,capture='Fresh local Ares post-VI Metal output, no CRT shader. Window chrome/shadow cropped.',clock='Matched ordered native script/event markers map fresh recorded PTS to v3 semantic timeline; held/dropped frames only, no motion interpolation.',audio={'source':'v3 encoded AAC copied unchanged; original game soundtrack/effects','note':'Existing edit and event timing retained through semantic marker alignment.'})
 (OUT/'timeline.json').write_text(json.dumps(plan,indent=2)+'\n');(OUT/'provenance.json').write_text(json.dumps(dict(captures=proofs,model_integration_sha256=hashlib.sha256((ROOT/'port/characters/review/cast-integration.json').read_bytes()).hexdigest(),limitations='Local capture replaces CRT output at user request; initial presentation-phase alignment is approximate to one captured frame.'),indent=2)+'\n')
 args=['ffmpeg','-hide_banner','-y','-filter_complex_threads','2']
 for n in SOURCES:args+=['-i',str(CAP/f'v{VERSION}-local-{n}/proof.mov')]
 filters=[]
 for i,n in enumerate(SOURCES):filters.append(f'[{i}:v]{crops[n]},settb=AVTB,setpts={clocks[n].setpts_expression()}[clock{i}]')
 filters+=['[clock0]split=2[intro0][intro1]','[clock2]split=3[club0][club1][club2]','[clock3]split=4[amy0][amy1][amy2][amy3]']
 # Window recorders can omit unchanged frames. Fill a short final gap before
 # trimming each shot so a following dissolve never starts beyond its EOF.
 for i,(shot,src) in enumerate(zip(plan['shots'],['intro0','intro1','clock1','club0','club1','club2','amy0','amy1','amy2','amy3'])):
  filters.append(f'[{src}]trim=start={shot["native_start"]}:end={shot["native_end"]},setpts=PTS-STARTPTS,fps=60,tpad=stop_mode=clone:stop_duration=1,trim=duration={shot["duration"]},settb=1/60,format=yuv420p[v{i}]')
 last='v0'
 for i,fade in enumerate(plan['dissolves'],1):
  filters.append(f'[{last}][v{i}]'+(f'xfade=transition=fade:duration={fade}:offset={plan["shots"][i]["output_start"]}' if fade else 'concat=n=2:v=1:a=0,fps=60,settb=1/60')+f'[x{i}]');last=f'x{i}'
 filters.append(f'[{last}]fps=60:start_time=0,tpad=stop_mode=clone:stop_duration=1,trim=end_frame={round(plan["duration"]*60)},setpts=N/(60*TB),fade=t=out:st={plan["duration"]-.8}:d=0.8[vout]')
 script=ADV/f'build/trailer-v{VERSION}-filter.txt';script.write_text(';\n'.join(filters))
 args+=['-filter_complex_script',str(script),'-map','[vout]','-an','-c:v','libx264','-threads','6','-preset','medium','-crf','18','-pix_fmt','yuv420p','-t',str(plan['duration']),str(ADV/f'build/trailer-v{VERSION}-video.mp4')]
 (ADV/f'build/trailer-v{VERSION}-render.json').write_text(json.dumps(args,indent=2))
 print(f'Prepared v{VERSION}',plan['duration'])
def render():
 args=json.loads((ADV/f'build/trailer-v{VERSION}-render.json').read_text())
 with (ADV/f'build/trailer-v{VERSION}-render.log').open('w') as log:subprocess.run(args,stdout=log,stderr=subprocess.STDOUT,check=True)
 subprocess.run(['ffmpeg','-v','error','-y','-i',str(ADV/f'build/trailer-v{VERSION}-video.mp4'),'-i',str(ROOT/'port/reviews/trailer/v3/poketcg64-trailer-no-crt.mp4'),'-map','0:v','-map','1:a','-c','copy','-movflags','+faststart',str(OUT/'poketcg64-trailer.mp4')],check=True)
 print(OUT/'poketcg64-trailer.mp4')
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('action',choices=('prepare','render'));p.add_argument('--version',type=int,default=4);a=p.parse_args();VERSION=a.version;OUT=ROOT/f'port/reviews/trailer/v{VERSION}';prep() if a.action=='prepare' else render()
