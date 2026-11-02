#!/usr/bin/env python3
"""Check the fresh local teaser and retain its playable current copy."""
from pathlib import Path
import hashlib,json,shutil,subprocess
from check_trailer_pair import boxes
ADV=Path(__file__).resolve().parents[1];ROOT=ADV.parents[3];BASE=ROOT/'port/reviews/trailer';OUT=BASE/'v4'
def main():
 video=OUT/'poketcg64-trailer.mp4';reference=BASE/'v3/poketcg64-trailer-no-crt.mp4'
 probe=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_streams','-show_format','-of','json',str(video)]));(OUT/'ffprobe.json').write_text(json.dumps(probe,indent=2)+'\n')
 v=next(s for s in probe['streams'] if s['codec_type']=='video');a=next(s for s in probe['streams'] if s['codec_type']=='audio')
 assert (v['width'],v['height'],v['r_frame_rate'],int(v['nb_frames']))==(1440,1080,'60/1',3874),v
 assert abs(float(v['duration'])-float(a['duration']))<1/60
 hashes=[]
 for p in (video,reference):
  audio=subprocess.check_output(['ffmpeg','-v','error','-i',str(p),'-map','0:a','-c:a','copy','-f','adts','-']);hashes.append(hashlib.sha256(audio).hexdigest())
 assert hashes[0]==hashes[1]
 r=subprocess.run(['ffmpeg','-v','error','-i',str(video),'-f','null','-'],capture_output=True,text=True);assert r.returncode==0 and not r.stderr,r.stderr
 b=boxes(video);assert next(x['offset'] for x in b if x['type']=='moov')<next(x['offset'] for x in b if x['type']=='mdat')
 result=dict(passed=True,width=1440,height=1080,fps=60,frames=3874,duration=float(v['duration']),audio_duration=float(a['duration']),audio_identical_to_v3=True,compressed_audio_sha256=hashes[0],decode_errors=0,faststart=True,video_sha256=hashlib.sha256(video.read_bytes()).hexdigest(),bytes=video.stat().st_size,capture='fresh local native post-VI; no CRT')
 (OUT/'validation.json').write_text(json.dumps(result,indent=2)+'\n')
 subprocess.run(['ffmpeg','-v','error','-y','-ss','24','-i',str(video),'-frames:v','1',str(OUT/'poster.png')],check=True)
 for name in ('poketcg64-trailer.mp4','poketcg64-trailer-no-crt.mp4'):shutil.copyfile(video,BASE/name)
 for name in ('poster.png','poster-no-crt.png'):shutil.copyfile(OUT/'poster.png',BASE/name)
 for name in ('timeline.json','validation.json'):shutil.copyfile(OUT/name,BASE/name)
 provenance=OUT/'provenance';provenance.mkdir(exist_ok=True)
 for s in ('intro','ronald','clubs','amy'):
  for src,dst in [(ADV/f'build/trailer-cast-{s}/build.json',provenance/f'{s}-build.json'),(ADV/f'build/trailer-capture/v4-local-{s}/runtime.log',provenance/f'{s}-runtime.log')]:shutil.copyfile(src,dst)
 print(json.dumps(result,indent=2))
if __name__=='__main__':main()
