"""Blender console: import apply_variant; apply_variant.apply('boy_palette2').

Also supports apply(pose='lying') for Amy or pose='west_fixed' for Murray.
Material overrides and object transforms reuse the same source mesh datablocks.
"""
import bpy
import json
import math
from mathutils import Matrix,Vector


def apply(appearance=None,pose='standing'):
    from fast64.fast64_internal.f3d.f3d_material import update_all_node_values
    scene=bpy.context.scene
    manifest=json.loads(scene['lightning_manifest'])
    appearance=appearance or manifest['model']
    colors=manifest['appearances'][appearance]['accent']
    definition=manifest['poses'][pose]
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
        obj.matrix_world=root@rest
        old=obj['rest_pivot'];p=root@Vector((old[0],-old[1],old[2]))
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
