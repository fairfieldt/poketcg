"""Render current canonical Blender source front and back previews."""
import bpy,json,math
from pathlib import Path
from mathutils import Vector
ROOT=Path(__file__).resolve().parents[1]
for row in json.loads((ROOT/'experiments/cast-concepts/review/final-families.json').read_text()):
 slug=row['slug'];folder=ROOT/'assets/models'/slug
 bpy.ops.wm.open_mainfile(filepath=str(folder/f'{slug}.blend'))
 scene=bpy.context.scene;scene.render.engine='BLENDER_EEVEE';scene.eevee.taa_render_samples=8
 scene.render.resolution_x=256;scene.render.resolution_y=256;scene.render.resolution_percentage=100
 scene.view_settings.view_transform='Standard';scene.world=bpy.data.worlds.new('Review background');scene.world.color=(.055,.065,.085)
 bpy.ops.object.camera_add();camera=bpy.context.object;camera.data.type='ORTHO';camera.data.ortho_scale=2.8;scene.camera=camera
 for name,sign in [('front',1),('back',-1)]:
  camera.location=(0,sign*7,1.3);camera.rotation_euler=(Vector((0,0,1.2))-camera.location).to_track_quat('-Z','Y').to_euler()
  scene.render.filepath=str(folder/f'{name}.png');bpy.ops.render.render(write_still=True)
