# First reproduce standing Amy into the same new revision.
import runpy,sys
from pathlib import Path
tool=Path(__file__).resolve().parents[3]/'tools/round_cast_heads.py'
revision=sys.argv[1] if len(sys.argv)>1 else 'i3e-rebuilt'
sys.argv=[str(tool),'amy__lying','--revision',revision]
runpy.run_path(str(tool),run_name='__main__')
uvtool=tool.with_name('front_face_uv.py')
sys.argv=[str(uvtool),'amy__lying','--revision',revision]
runpy.run_path(str(uvtool),run_name='__main__')
