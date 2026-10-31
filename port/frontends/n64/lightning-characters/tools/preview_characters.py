"""Render a side-by-side preview without changing the saved gameplay scene."""
import bpy
from mathutils import Vector
from pathlib import Path

scene=bpy.context.scene
sources=[o for o in scene.objects if o.type=='MESH' and o.get('export_source') and o.get('model') in (1,4)]
for obj in scene.objects:
    obj.hide_render=True
for source in sources:
    obj=source.copy()
    scene.collection.objects.link(obj)
    obj.hide_render=False
    obj.hide_set(False)
    obj.location.x=-.85 if obj['model']==4 else .85
bpy.ops.object.camera_add(location=(3,-8,3.1))
camera=bpy.context.object
camera.rotation_euler=(Vector((0,0,1.2))-camera.location).to_track_quat('-Z','Y').to_euler()
camera.data.type='ORTHO'
camera.data.ortho_scale=4.25
scene.camera=camera
scene.world.color=(.12,.14,.18)
scene.render.resolution_x=1280
scene.render.resolution_y=900
scene.render.resolution_percentage=100
scene.render.filepath=str(Path(__file__).resolve().parents[1]/'assets/characters-preview.png')
bpy.ops.render.render(write_still=True)
