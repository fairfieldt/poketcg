#!/usr/bin/env python3
"""Separate native frame presentation, VI speed and recorded-frame cadence."""
import argparse,json,re,statistics
from pathlib import Path
PATTERN=re.compile(r'TRAILER CADENCE vi=(\d+) us=(\d+) frames=(\d+) min_us=(\d+) max_us=(\d+)')
def analyze(directory):
 directory=Path(directory);metadata=json.loads((directory/'capture.json').read_text());events=json.loads((directory/'markers.json').read_text());log=(directory/'runtime.log').read_text();mode=re.search(r'VIDEO MODE width=(\d+) height=(\d+) ram=(\d+) highres=(\d+)',log);assert mode
 intervals=[];previous=None
 for match in PATTERN.finditer(log):
  vi,us,frames,minimum,maximum=map(int,match.groups())
  if previous:
   pv,pu=previous;seconds=(us-pu)/1e6;intervals.append(dict(vi_start=pv,vi_end=vi,native_seconds=seconds,presented_frames=frames,native_present_fps=frames/seconds,frames_per_nominal_second=frames*60/(vi-pv),minimum_interpresent_us=minimum,maximum_interpresent_us=maximum))
  previous=(vi,us)
 points=metadata.get('window_delivery_cadence',{}).get('sample_pts',[]);walks=[]
 for event in events:
  start=re.search(r'TRAILER WALK map=(\d+).*?vi=(\d+) us=(\d+)',event['line'])
  if not start:continue
  map_id,vi,us=map(int,start.groups());end=next(e for e in events if e['epoch']>event['epoch'] and e['line'].startswith(f'TRAILER WALK_COMPLETE map={map_id} '));finish=re.search(r'vi=(\d+) us=(\d+)',end['line']);end_vi,end_us=map(int,finish.groups());native=(end_us-us)/1e6;wall=end['epoch']-event['epoch']
  stable=[s for s in intervals if s['vi_start']>=vi and s['vi_end']<=end_vi];raw_start=event['epoch']-metadata['recorder_launch_epoch'];raw_end=end['epoch']-metadata['recorder_launch_epoch'];observed=[p for p in points if raw_start+.05<=p<=raw_end-.05];gaps=sorted(b-a for a,b in zip(observed,observed[1:]));percentile=lambda q:gaps[min(len(gaps)-1,round((len(gaps)-1)*q))] if gaps else None
  walks.append(dict(map=map_id,native_VI=end_vi-vi,native_seconds=native,host_seconds=wall,host_over_native_ratio=wall/native,stable_native_present_fps=statistics.median(x['native_present_fps'] for x in stable) if stable else None,recorded_samples_in_walk=len(observed),recorded_pts_gap_p50_seconds=percentile(.5),recorded_pts_gap_p95_seconds=percentile(.95),recorded_pts_gap_max_seconds=max(gaps) if gaps else None,recorded_gap_over_75ms=sum(x>.075 for x in gaps),native_windows=stable))
 return dict(directory=str(directory),native_framebuffer=[int(mode[1]),int(mode[2])],ram_bytes=int(mode[3]),high_resolution=bool(int(mode[4])),walks=walks,native_present_intervals=intervals,recorded_cadence={k:v for k,v in metadata.get('window_delivery_cadence',{}).items() if k!='sample_pts'},coverage=metadata.get('coverage'),interpretation='Native presented frames are measured in emulated CP0/VI time. Recorded PTS describe captured samples, not game FPS. A 60fps delivery file may hold recorded 30fps game states. Private BFS/map-loading boundary hitches are separate from steady walking.',physical_console_performance_claim=False)
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('directories',nargs='+',type=Path);p.add_argument('--output',type=Path);a=p.parse_args();rows=[analyze(d) for d in a.directories];report=dict(captures=rows,interpolation=False)
 if a.output:a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(report,indent=2)+'\n')
 for row in rows:
  print(row['directory'],row['native_framebuffer'])
  for w in row['walks']:print('map',w['map'],'nativeFPS',w['stable_native_present_fps'],'recorded maxgap',w['recorded_pts_gap_max_seconds'])
