#!/usr/bin/env python3
"""Capture the final three-lead ensemble with the production renderer."""
import json,shutil,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];GAME=ROOT.parents[3]/'port/frontends/n64/adventure'
source=ROOT/'ensemble';source.mkdir(exist_ok=True)
models={'mark':ROOT.parent/'mark-concepts/approved/a_adventure/i3d/mark'}
for slug in ('dr_mason','ronald'):
    latest=sorted(p for p in (ROOT/'models'/slug).glob('i3*') if (p/'native/capture.json').is_file())[-1]
    models[slug]=latest/slug
for slug,folder in models.items():
    shutil.copytree(folder,source/slug,dirs_exist_ok=True)
(source/'option.json').write_text(json.dumps({'id':'ensemble','label':'Revised','source_models':{s:str(p) for s,p in models.items()}},indent=2)+'\n')
work=ROOT/'work/ensemble';options=work/'options';options.mkdir(parents=True,exist_ok=True)
link=options/'ensemble'
if not link.exists():link.symlink_to(source,target_is_directory=True)
rom='cast-ensemble'
with (work/'build.log').open('w') as log:
    subprocess.run(['make','-j8',f'BUILD_DIR={work/"build"}',f'ROM={rom}','DIAGNOSTIC=18',f'MODEL_OPTIONS={options}','bundle'],cwd=GAME,stdout=log,stderr=subprocess.STDOUT,check=True)
subprocess.run([sys.executable,str(ROOT.parent/'n64-style/tools/capture_options.py'),str(GAME/f'{rom}.z64'),'--output',str(source/'native'),'--timeout','180'],check=True)
