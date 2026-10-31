#!/usr/bin/env python3
"""Create/review/validate one unique family at a time in atlas order."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from validate import validate_model

parser=argparse.ArgumentParser()
parser.add_argument('--blender',default=os.environ.get('BLENDER','/Applications/Blender.app/Contents/MacOS/Blender'))
parser.add_argument('--only')
parser.add_argument('--force',action='store_true')
args=parser.parse_args()
catalog=json.loads((ROOT/'catalog.json').read_text())
families=[f for f in catalog['families'] if not args.only or f['slug']==args.only]
assert families
logs=ROOT/'build/logs';logs.mkdir(parents=True,exist_ok=True)
recipe_files=[ROOT/'tools'/f for f in ('build_model.py','parts.py','designs.py','skin.py','exterior.py')]
recipe_files.append(ROOT.parents[1]/'port/frontends/n64/lightning-characters/tools/export_scene.py')
fingerprint=hashlib.sha256(b''.join(p.read_bytes() for p in recipe_files)).hexdigest()
for i,family in enumerate(families):
    slug=family['slug'];done=ROOT/'assets/models'/slug/'complete.json'
    if not args.force and done.exists() and json.loads(done.read_text()).get('fingerprint')==fingerprint:
        validate_model(slug);print('SKIP verified',slug,flush=True);continue
    print(f'MODEL {i+1}/{len(families)} {slug} ({len(family["aliases"])} NPC IDs)',flush=True)
    with (logs/(slug+'.log')).open('w') as log:
        result=subprocess.run([args.blender,'--background','--python-exit-code','1',
            '--python',str(ROOT/'tools/build_model.py'),'--',slug],stdout=log,stderr=subprocess.STDOUT)
    if result.returncode:
        print((logs/(slug+'.log')).read_text()[-4500:]);raise SystemExit(result.returncode)
    validate_model(slug)
    report=json.loads(done.read_text());report['fingerprint']=fingerprint
    done.write_text(json.dumps(report,indent=2)+'\n')
    print('DONE',slug,report['triangles'],'triangles;',report['linked_parts'],'linked pieces',flush=True)
