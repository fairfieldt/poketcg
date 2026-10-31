"""Run Blender assets/lightning_characters.blend -b --python this.py -- 1|4.

Creates a standalone editable Fast64 .blend and compact native libdragon L643
asset with just that character's texture, five rigid joints and local origin.
"""
import bpy
import json
import math
import shutil
import sys
from pathlib import Path
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(Path(__file__).resolve().parent))
import export_scene

model = int(sys.argv[sys.argv.index('--')+1])
name = {1:'ronald',4:'dr_mason'}[model]
out = ROOT/'assets/models'/name
out.mkdir(parents=True,exist_ok=True)
scene = bpy.context.scene
manifest = json.loads(scene['lightning_manifest'])
sources = [o for o in scene.objects if o.type=='MESH' and o.get('export_source') and o.get('model')==model]
for obj in list(bpy.data.objects):
    if obj not in sources:
        bpy.data.objects.remove(obj,do_unlink=True)
for obj in sources:
    obj.hide_render=False
    obj.hide_set(False)
materials={mat for obj in sources for mat in obj.data.materials}
old_ids=sorted({mat['texture_id'] for mat in materials if mat['texture_id']>=0})
names=[manifest['textures'][i] for i in old_ids]
for mat in materials:
    if mat['texture_id']>=0:
        mat['texture_id']=old_ids.index(mat['texture_id'])
        mat.update_tag()
bpy.context.view_layer.update()
for texture in names:
    (out/'textures').mkdir(exist_ok=True)
    shutil.copy2(ROOT/'assets/textures'/(texture+'.png'),out/'textures'/(texture+'.png'))
manifest.update(textures=names,actors=[dict(name=name,model=model,x=0,z=0,heading=0)],colliders=[])
manifest['camera']=dict(eye=[3,2.3,5],target=[0,1.15,0],fov=35)
scene['lightning_manifest']=json.dumps(manifest)
export_scene.OUT=out
result=export_scene.export_scene()
(out/'world_data.h').unlink()  # Standalone model has no room collision or NPC list.

bpy.ops.object.camera_add(location=(3,-5,2.3))
camera=bpy.context.object
camera.name='Model preview camera'
camera.rotation_euler=(Vector((0,0,1.15))-camera.location).to_track_quat('-Z','Y').to_euler()
camera.data.type='ORTHO'
camera.data.ortho_scale=3.0
scene.camera=camera
scene.render.resolution_x=720
scene.render.resolution_y=720
scene.render.resolution_percentage=100
scene.render.filepath=str(out/'preview.png')
scene.world.color=(.16,.18,.22)
scene.view_settings.view_transform='Standard'
scene.render.engine='BLENDER_EEVEE'
for area in bpy.context.screen.areas:
    if area.type=='VIEW_3D':
        area.spaces.active.region_3d.view_distance=4
        area.spaces.active.region_3d.view_location=(0,0,1.1)
bpy.ops.wm.save_as_mainfile(filepath=str(out/(name+'.blend')))
bpy.ops.render.render(write_still=True)
assert result['triangles']==manifest['character_triangles'][str(model)]
print('STANDALONE',name,result['triangles'],'triangles',result['batches'],'batches')
