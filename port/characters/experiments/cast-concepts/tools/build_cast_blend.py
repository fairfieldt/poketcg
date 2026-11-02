"""Build an editable overview from retained final Blender models."""
import bpy,json
from mathutils import Vector
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
rows=json.loads((ROOT/'review/final-families.json').read_text());assert len(rows)==37
rows+=json.loads((ROOT/'review/final-appearances.json').read_text())
bpy.ops.wm.read_factory_settings(use_empty=True)
for i,row in enumerate(rows):
    path=(ROOT/row['prefix']/f'{row["slug"]}.blend').resolve()
    collection=bpy.data.collections.new(row['name']);bpy.context.scene.collection.children.link(collection)
    parent=bpy.data.objects.new(row['name'],None);collection.objects.link(parent);parent.location=((i%7)*3,(i//7)*3.4,0)
    parent['source_model']=str(path)
    with bpy.data.libraries.load(str(path),link=False) as (source,dest):dest.objects=source.objects
    for obj in dest.objects:
        if obj:collection.objects.link(obj);obj.parent=parent
for screen in bpy.data.screens:
    for area in screen.areas:
        if area.type=='VIEW_3D':
            area.spaces.active.shading.type='MATERIAL'
            region=area.spaces.active.region_3d
            region.view_location=(9,8.5,1)
            region.view_rotation=Vector((0,1,-.8)).to_track_quat('-Z','Y')
            region.view_distance=30
            region.view_perspective='ORTHO'
bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/'cast.blend'))
print(json.dumps(dict(families=37,appearances_and_poses=len(rows)-37,collections=len(bpy.data.collections),meshes=len(bpy.data.meshes),images=len(bpy.data.images),unpacked_images=[im.name for im in bpy.data.images if im.source=='FILE' and not im.packed_file])))
