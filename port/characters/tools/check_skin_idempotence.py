"""Run in Blender to check a saved model can normalize/export again unchanged."""
import bpy
import hashlib
import json
from pathlib import Path
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
sys.path.insert(0,str(ROOT.parents[1]/'port/frontends/n64/lightning-characters/tools'))
from skin import prepare_skin
import export_scene

slug=sys.argv[sys.argv.index('--')+1] if '--' in sys.argv else 'swimmer'
folder=ROOT/'assets/models'/slug
bpy.ops.wm.open_mainfile(filepath=str(folder/(slug+'.blend')))
manifest=json.loads(bpy.context.scene['lightning_manifest'])
sources=[o for o in bpy.context.scene.objects if o.type=='MESH' and o.get('export_source')]
textures=list(manifest['textures'])
before={name:hashlib.sha256((ROOT/'assets/textures'/(name+'.png')).read_bytes()).hexdigest() for name in textures}
manifest['skin_palette']=prepare_skin(sources,textures,ROOT/'assets/textures')
assert textures==manifest['textures']
assert before=={name:hashlib.sha256((ROOT/'assets/textures'/(name+'.png')).read_bytes()).hexdigest() for name in textures}
bpy.context.scene['lightning_manifest']=json.dumps(manifest)
out=ROOT/'build/skin-idempotence';out.mkdir(parents=True,exist_ok=True)
export_scene.OUT=out
export_scene.export_scene()
assert (out/'scene.l64').read_bytes()==(folder/'scene.l64').read_bytes(),'Repeated skin normalization changed native data'
print('SKIN_IDEMPOTENCE_PASS',slug,'saved-model face pixels and native mesh unchanged')
