#!/usr/bin/env python3
"""Run actual native captures and save editable sources for exported leader rounds."""
import json,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parent.parent
roundid=sys.argv[1];slugs=sys.argv[2:]
for slug in slugs:
 folder=ROOT/'models'/slug/f'i{roundid}'
 if not (folder/'native/capture.json').is_file():subprocess.run([sys.executable,str(ROOT/'tools/capture_family.py'),slug,roundid],check=True)
 with (folder/'blender.log').open('w') as log:subprocess.run(['/Applications/Blender.app/Contents/MacOS/Blender','--background','--python-exit-code','1','--python',str(ROOT/'tools/editable_blend.py'),'--',str(folder),slug],check=True,stdout=log,stderr=subprocess.STDOUT)
 p=ROOT/'leaders-progress.json';progress=json.loads(p.read_text());progress['families'][slug]={'concept':'generated and inspected','status':f'round {roundid} captured; visual review pending','rounds_captured':int(roundid)};p.write_text(json.dumps(progress,indent=2)+'\n')
 print('LEADERS CAPTURED',slug,roundid,flush=True)
