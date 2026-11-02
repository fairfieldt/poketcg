# Reproduce geometry into a NEW revision, preserving the retained review artifact.
import json,runpy,sys
from pathlib import Path
folder=Path(__file__).resolve().parent
tool=folder.parents[2]/'tools/round_cast_heads.py'
setting=json.loads((folder/'audit.json').read_text())['settings'][0]
args=[str(tool),'man','--revision',sys.argv[1] if len(sys.argv)>1 else 'i3e-rebuilt']
if 'cuts' in setting:args+=['--cuts',*map(str,setting['cuts'])]
args+=['--phase',*map(str,setting.get('packing_phase',[0,0,0]))]
if setting.get('preserve_lower'):args+=['--preserve-lower']
sys.argv=args
runpy.run_path(str(tool),run_name='__main__')
new_revision=args[args.index('--revision')+1]
uvtool=tool.with_name('front_face_uv.py')
sys.argv=[str(uvtool),'man','--revision',new_revision]
runpy.run_path(str(uvtool),run_name='__main__')
# Scientist paint is a separate correction; other source PNGs remain unchanged.
