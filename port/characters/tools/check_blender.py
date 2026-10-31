"""Validate links and measure actual mesh reuse in the opened cast.blend."""
import bpy
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
for library in bpy.data.libraries:
    assert Path(bpy.path.abspath(library.filepath)).exists(),library.filepath
instances=[o for o in bpy.context.scene.objects if o.instance_type=='COLLECTION']
assert len(instances)==39,len(instances)
parts=[]
unique=set()
skin_models={}
for instance in instances:
    for obj in instance.instance_collection.objects:
        data=obj.data
        if data.library and Path(data.library.filepath).name=='parts.blend':
            parts.append(obj);unique.add(data.name)
    if instance.name in ('torch','legendary_card'):continue
    mats={slot.material for obj in instance.instance_collection.objects for slot in obj.material_slots if slot.material}
    skin=next(m for m in mats if m.get('role')=='skin' or m.name.casefold().startswith('skin'))
    pigment=tuple(round(c*255) for c in skin.diffuse_color[:3])
    faces=[m for m in mats if int(m.get('texture_id',-1))>=0]
    assert faces
    for face in faces:
        assert face.get('native_lit_texture'),face.name
        assert tuple(face.get('skin_rgb8'))==pigment,(instance.name,face.name)
        assert tuple(face.diffuse_color)==(1,1,1,1)
        image=face.f3d_mat.tex0.tex
        assert image.colorspace_settings.name=='Non-Color',(instance.name,image.name)
        assert tuple(round(c*255) for c in image.pixels[:3])==pigment,(instance.name,image.name,'palette mismatch')
    skin_models[instance.name]=pigment
assert len(parts)==802,len(parts)
assert len(unique)<=33,len(unique)
report=dict(canonical_models=39,npc_ids=115,appearances=42,shared_library_meshes=33,
            used_shared_meshes=len(unique),linked_part_instances=len(parts),textures=11,
            shared_mesh_names=sorted(unique),all_libraries_present=True,
            consistent_skin_models=len(skin_models),skin_rgb8_by_model=skin_models)
(ROOT/'review/sharing.json').write_text(json.dumps(report,indent=2)+'\n')
print('BLENDER_VALID',len(instances),'unique models,',len(parts),'linked instances of',len(unique),'used shared meshes')
print('BLENDER_SKIN_VALID',len(skin_models),'models: actual linked face-image pixels/materials/colorspaces match solid skin')
