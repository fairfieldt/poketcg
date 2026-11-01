"""Validate the complete linked cast, its owning surfaces and actual reuse."""
import bpy
import json
import os
from pathlib import Path
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from parts import definitions

catalog=json.loads((ROOT/'catalog.json').read_text())
families={family['slug']:family for family in catalog['families']}
for library in bpy.data.libraries:
    assert Path(bpy.path.abspath(library.filepath)).exists(),library.filepath
instances=[obj for obj in bpy.context.scene.objects if obj.instance_type=='COLLECTION']
assert len(instances)==len(families)==39,len(instances)
assert {obj.name for obj in instances}==set(families),'Cast families missing or duplicated'
parts=[]
unique=set()
skin_models={}
ready_models={}
face_images=set()
authored_parts=0
shared_names={'part_'+name for name in definitions()}
for instance in instances:
    objects=[obj for obj in instance.instance_collection.objects if obj.type=='MESH']
    assert objects and all(obj.get('export_source') for obj in objects),instance.name
    source=Path(instance['model_file'])
    if not source.is_absolute():source=ROOT/source
    assert source.is_file(),source
    complete=json.loads((source.parent/'complete.json').read_text())
    assert complete['model']==instance.name,(instance.name,source)
    authored_parts+=complete['linked_parts']
    for obj in objects:
        assert obj.library is not None,(instance.name,obj.name,'source object is not linked')
        assert obj.data.polygons and obj.data.vertices,(instance.name,obj.name,'empty source mesh')
        data=obj.data
        if data.library and Path(data.library.filepath).name=='parts.blend':
            assert data.name in shared_names,(instance.name,data.name)
            parts.append(obj);unique.add(data.name)
    if instance.name in ('torch','legendary_card'):continue
    assert {int(obj['part']) for obj in objects}==set(range(5)),(instance.name,'rig parts')
    owners=[obj for obj in objects if obj.get('exterior_shell')]
    ready=[obj for obj in objects if obj.get('native_exterior_ready')]
    if ready:
        # New owning recipes may build head, torso and limbs directly. Their
        # number of closed pieces is intentionally unrelated to the legacy
        # two boolean unions; both kinds retain the same five-part rig.
        assert all(obj.get('exterior_shell') for obj in ready),instance.name
        assert any(obj.get('head_component') for obj in ready),(instance.name,'owning head missing')
        assert any(int(obj['part'])==0 and not obj.get('head_component')
                   and all(abs(value)<1e-8 for value in obj['pivot'])
                   for obj in owners),(instance.name,'owning body missing')
        ready_models[instance.name]=len(ready)
    else:
        assert len(owners)==2,(instance.name,len(owners))
    mats={slot.material for obj in objects for slot in obj.material_slots if slot.material}
    skins=[mat for mat in mats if mat.get('role')=='skin' or mat.name.casefold().startswith('skin')]
    assert len(skins)==1,(instance.name,'skin materials',len(skins))
    skin=skins[0]
    pigment=tuple(round(c*255) for c in skin.diffuse_color[:3])
    faces=[mat for mat in mats if int(mat.get('texture_id',-1))>=0]
    assert faces,(instance.name,'face material missing')
    for face in faces:
        assert face.get('native_lit_texture'),face.name
        assert tuple(face.get('skin_rgb8'))==pigment,(instance.name,face.name)
        assert tuple(face.diffuse_color)==(1,1,1,1)
        image=face.f3d_mat.tex0.tex
        assert image is not None,(instance.name,face.name,'missing image')
        assert tuple(image.size)==(32,32),(instance.name,image.name,'face dimensions')
        assert image.colorspace_settings.name=='Non-Color',(instance.name,image.name)
        assert tuple(round(c*255) for c in image.pixels[:3])==pigment,(instance.name,image.name,'palette mismatch')
        face_images.add(Path(image.filepath).name)
    skin_models[instance.name]=pigment
assert unique<=shared_names
assert len(skin_models)==len(families)-2
report=dict(canonical_models=len(instances),npc_ids=len(catalog['aliases']),
            appearances=len(catalog['appearances']),shared_library_meshes=len(shared_names),
            used_shared_meshes=len(unique),linked_part_instances=len(parts),
            authored_linked_part_instances=authored_parts,textures=len(face_images),
            shared_mesh_names=sorted(unique),all_libraries_present=True,
            direct_owning_shell_models=ready_models,
            consistent_skin_models=len(skin_models),skin_rgb8_by_model=skin_models)
target=Path(os.environ.get('POKETCG_CHARACTER_BLENDER_REPORT',str(ROOT/'review/sharing.json')))
target.parent.mkdir(parents=True,exist_ok=True)
target.write_text(json.dumps(report,indent=2)+'\n')
print('BLENDER_VALID',len(instances),'unique models,',len(parts),'linked instances of',len(unique),'used shared meshes')
print('BLENDER_SKIN_VALID',len(skin_models),'models: actual linked face-image pixels/materials/colorspaces match solid skin')
