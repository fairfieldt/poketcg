# Reproduce into a NEW revision; never overwrite retained review artifacts.
import runpy,sys
from pathlib import Path
tool=Path(__file__).resolve().parents[3]/'tools/round_cast_heads.py'
sys.argv=[str(tool),'mark','--revision',sys.argv[1] if len(sys.argv)>1 else 'round-rebuild']
runpy.run_path(str(tool),run_name='__main__')
# Face follow-up: tools/correct_scientist_faces.py
