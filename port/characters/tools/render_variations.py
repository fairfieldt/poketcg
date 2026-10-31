"""Render one material/pose override; never save a duplicate model."""
import bpy
import sys
from pathlib import Path
from mathutils import Vector
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from apply_variant import apply
appearance,pose=sys.argv[sys.argv.index('--')+1:][:2]
apply(appearance,pose)
scene=bpy.context.scene
if pose=='lying':
    scene.camera.location=(3,-4,3.5)
    scene.camera.rotation_euler=(Vector((0,0,.4))-scene.camera.location).to_track_quat('-Z','Y').to_euler()
    scene.camera.data.ortho_scale=2.7
folder=ROOT/'review/variations';folder.mkdir(parents=True,exist_ok=True)
scene.render.filepath=str(folder/(appearance+'_'+pose+'.png'))
bpy.ops.render.render(write_still=True)
