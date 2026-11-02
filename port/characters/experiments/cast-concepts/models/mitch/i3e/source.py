# Reproduce geometry into a NEW revision, preserving the retained review artifact.
import json,runpy,sys
from pathlib import Path
folder=Path(__file__).resolve().parent
tool=folder.parents[2]/'tools/round_cast_heads.py'
setting=json.loads((folder/'audit.json').read_text())['settings'][0]
args=[str(tool),'mitch','--revision',sys.argv[1] if len(sys.argv)>1 else 'i3e-rebuilt']
if 'cuts' in setting:args+=['--cuts',*map(str,setting['cuts'])]
args+=['--phase',*map(str,setting.get('packing_phase',[0,0,0]))]
if setting.get('preserve_lower'):args+=['--preserve-lower']
sys.argv=args
runpy.run_path(str(tool),run_name='__main__')
# Face-paint and adaptive UV follow-ups are recorded separately.
