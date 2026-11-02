"""Assemble the unique models as linked instances, without copying geometry."""
import bpy
import json
import math
from pathlib import Path
from mathutils import Vector

ROOT=Path(__file__).resolve().parents[1]
catalog=json.loads((ROOT/'catalog.json').read_text())
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
scene=bpy.context.scene
for i,family in enumerate(catalog['families']):
    slug=family['slug'];path=ROOT/'assets/models'/slug/(slug+'.blend')
    with bpy.data.libraries.load(str(path),link=True) as (src,dst):dst.objects=src.objects
    collection=bpy.data.collections.new(slug+' · linked source')
    for obj in dst.objects:
        if obj.type=='MESH' and (obj.get('export_source') or obj.get('cast_source')) and not obj.hide_render:collection.objects.link(obj)
    obj=bpy.data.objects.new(slug,None);scene.collection.objects.link(obj)
    obj.instance_type='COLLECTION';obj.instance_collection=collection
    obj.location=((i%7)*2.3,-(i//7)*3.1,0)
    obj['npc_aliases']=json.dumps(family['aliases'])
    obj['model_file']=str(path.relative_to(ROOT))
scene['npc_catalog']=json.dumps(catalog)
scene['sharing']='Each collection links one canonical model; linked mesh parts come from parts.blend.'
bpy.ops.object.camera_add(location=(12,-22,18))
camera=bpy.context.object
camera.rotation_euler=(Vector((7,-7,1))-camera.location).to_track_quat('-Z','Y').to_euler()
camera.data.type='ORTHO';camera.data.ortho_scale=24;scene.camera=camera
scene.render.engine='BLENDER_EEVEE';scene.view_settings.view_transform='Standard'
scene.world.color=(.12,.14,.18)
for area in bpy.context.screen.areas:
    if area.type=='VIEW_3D':
        area.spaces.active.region_3d.view_distance=25
        area.spaces.active.region_3d.view_location=(7,-7,1)
bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/'assets/cast.blend'),relative_remap=True)
print('LINKED_CAST',len(catalog['families']),'models')
