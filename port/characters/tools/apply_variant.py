"""Blender console: import apply_variant; apply_variant.apply('boy_palette2').

Also supports apply(pose='lying') for Amy or pose='west_fixed' for Murray.
Material overrides and object transforms reuse the same source mesh datablocks.
Swimmer appearances select their saved portrait-specific head components while
keeping one shared body and its rigid pivots.
"""
import bpy
import json
import math
from mathutils import Matrix,Vector


def select_head_preset(scene,manifest,appearance):
    """Activate a saved head and its own face table before pose/export work."""
    encoded=scene.get('character_appearance_head_presets')
    if not encoded:return False
    presets=json.loads(encoded)
    assert appearance in presets,('Missing saved head preset',appearance)
    preset=presets[appearance]
    selected=set(preset['objects'])
    names={name for entry in presets.values() for name in entry['objects']}
    objects={name:scene.objects.get(name) for name in names}
    assert all(obj is not None and obj.type=='MESH' and obj.get('head_component')
               for obj in objects.values()),'Saved head preset references missing geometry'
    assert selected and all(objects[name].get('part')==0 for name in selected)
    for name,obj in objects.items():
        active=name in selected
        obj['export_source']=active
        obj.hide_render=not active
        obj.hide_set(not active)
    # Both faces use texture slot zero, but refer to different packed images.
    # Recoloring or visibility alone would export Amanda with Sara's face.
    for key in ('textures','skin_palette','surface_ownership'):
        manifest[key]=preset[key]
    manifest.update(preset['statistics'])
    return True


def apply_cast(appearance=None,pose='standing'):
    """Reviewed editable meshes use ordinary Blender materials and one shared body."""
    scene=bpy.context.scene
    manifest=json.loads(scene['cast_manifest']);slug=manifest['model']
    appearance=appearance or slug
    assert appearance in manifest['appearances'],appearance
    assert pose in manifest['poses'],pose
    palette=json.loads(scene.get('cast_variant_palette','{}')) if appearance!=slug else {}
    definition=manifest['poses'][pose]
    if slug=='amy' and pose=='lying':definition={'rotation':[90,0,0],'translation':[0,.9,.35]}
    # Reviewed authoring uses X east, Y south, Z up.
    rotation=Matrix.Identity(4)
    for axis,angle in zip('XYZ',definition['rotation']):
        rotation=rotation@Matrix.Rotation(math.radians(angle),4,axis)
    root=Matrix.Translation(definition['translation'])@rotation
    for obj in scene.objects:
        if not obj.get('cast_source'):continue
        selected=obj.get('cast_head_appearance',appearance)==appearance
        obj.hide_render=not selected;obj.hide_viewport=not selected
        values=obj['rest_matrix'];obj.matrix_world=root@Matrix([values[i:i+4] for i in range(0,16,4)])
        if obj.get('cast_head'):continue
        for slot in obj.material_slots:
            mat=slot.material;role=mat.get('cast_role')
            if not role:continue
            rgb=palette.get(role,list(mat['cast_rest_color']))
            mat.diffuse_color=(*rgb,1)
            for node in mat.node_tree.nodes:
                if node.type=='EMISSION' and not node.inputs[0].is_linked:node.inputs[0].default_value=(*rgb,1)
    scene['cast_appearance']=appearance;scene['cast_pose']=pose
    bpy.context.view_layer.update()


def apply(appearance=None,pose='standing'):
    scene=bpy.context.scene
    if 'cast_manifest' in scene:return apply_cast(appearance,pose)
    from fast64.fast64_internal.f3d.f3d_material import update_all_node_values
    manifest=json.loads(scene['lightning_manifest'])
    appearance=appearance or manifest['model']
    colors=manifest['appearances'][appearance]['accent']
    definition=manifest['poses'][pose]
    head_preset=select_head_preset(scene,manifest,appearance)
    rotation=Matrix.Identity(4)
    for axis,angle in zip('XYZ',definition['rotation']):
        rotation=rotation@Matrix.Rotation(math.radians(angle),4,axis)
    root=Matrix.Translation(definition['translation'])@rotation
    sources=[o for o in scene.objects if o.type=='MESH' and o.get('export_source')]
    for obj in sources:
        if 'rest_matrix' not in obj:
            obj['rest_matrix']=[v for row in obj.matrix_world for v in row]
            obj['rest_pivot']=list(obj['pivot'])
        values=obj['rest_matrix'];rest=Matrix([values[i:i+4] for i in range(0,16,4)])
        old=obj['rest_pivot']
        if head_preset and not any(definition['rotation']+definition['translation']):
            # Head selection is not a pose change. Preserve the saved double
            # precision pivots instead of rounding them through mathutils'
            # float32 Vector, which would change native export bytes.
            obj.matrix_world=rest
            obj['pivot']=list(old)
        else:
            obj.matrix_world=root@rest
            p=root@Vector((old[0],-old[1],old[2]))
            obj['pivot']=(p.x,-p.y,p.z)
    for mat in {slot.material for obj in sources for slot in obj.material_slots}:
        if mat.get('role')!='accent':continue
        mat.diffuse_color=(*colors,1);mat.f3d_mat.prim_color=(*colors,1)
        mat.f3d_mat.default_light_color=(*colors,1)
        mat.f3d_mat.ambient_light_color=(*(c*.55 for c in colors),1)
        with bpy.context.temp_override(material=mat):update_all_node_values(mat,bpy.context)
        mat.update_tag()
    manifest.update(appearance=appearance,pose=pose)
    scene['lightning_manifest']=json.dumps(manifest)
    bpy.context.view_layer.update()
