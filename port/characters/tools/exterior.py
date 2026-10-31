"""Bake face, clothing and headwear layers into their owning exterior.

Keep linked authoring pieces in the blend, but export exact head/torso unions.
Capped heads also own all hair, crown, brim and badge surfaces; hidden scalp
faces are removed rather than relying on their depth separation.
Opaque face/clothing patches are backed inward before union, so their painted
surface owns the exterior instead of competing with a complete underlying body.
"""
from collections import defaultdict
import bpy
import bmesh
from mathutils import Matrix, Vector


def exterior_sources(sources,wearing_cap=False):
    bpy.context.view_layer.update()
    scene=bpy.context.scene
    groups=defaultdict(list)
    passthrough=[]
    for source in sources:
        name=source.name.lower()
        key=None
        headwear=wearing_cap and any(word in name for word in ('hair','fringe',' · cap',' · brim'))
        head=('head' in name and not any(word in name for word in ('hair','headband','hood'))) or 'face' in name
        if headwear or head:
            key='head'
        elif any(word in name for word in ('torso','shirt opening','lapel','open front panel','outer side','outer back','collar','gi neck','sailor neck')):
            key='torso'
        if key is None:passthrough.append(source)
        else:groups[(key,int(source['part']),tuple(source['pivot']))].append(source)
    outputs=[]
    for (region,part,pivot),members in groups.items():
        operands=bpy.data.collections.new('Exterior operands')
        scene.collection.children.link(operands)
        copies=[]
        for source in members:
            data=source.data.copy()
            data.transform(source.matrix_world)
            # Applying a reflected limb transform must preserve outward winding.
            if source.matrix_world.determinant()<0:
                data.flip_normals()
            for index,slot in enumerate(source.material_slots):
                data.materials[index]=slot.material
            obj=bpy.data.objects.new(source.name+' [union operand]',data)
            operands.objects.link(obj)
            bm=bmesh.new();bm.from_mesh(data)
            boundary=any(edge.is_boundary for edge in bm.edges)
            if boundary:
                # These authored open patches face outward from the torso/card.
                # Orient each connected patch before adding an inward backing.
                center=sum((v.co for v in bm.verts),Vector())/len(bm.verts)
                normal=sum((f.normal*f.calc_area() for f in bm.faces),Vector())
                if normal.dot(center-Vector((0,0,1.1)))<0:
                    bmesh.ops.reverse_faces(bm,faces=list(bm.faces))
                bm.to_mesh(data);data.update()
                backing=obj.modifiers.new('Inward patch backing','SOLIDIFY')
                backing.thickness=.12;backing.offset=-1
                backing.use_even_offset=True
                bpy.context.view_layer.objects.active=obj
                bpy.ops.object.modifier_apply(modifier=backing.name)
            bm.free()
            copies.append(obj)
        target=copies.pop(0)
        # Exclude the target from the operand collection while solving.
        operands.objects.unlink(target);scene.collection.objects.link(target)
        if copies:
            union=target.modifiers.new('Exterior union','BOOLEAN')
            union.operation='UNION';union.operand_type='COLLECTION'
            union.collection=operands;union.solver='EXACT'
            union.material_mode='TRANSFER';union.use_self=True
            bpy.context.view_layer.objects.active=target
            bpy.ops.object.modifier_apply(modifier=union.name)
        # Remove numerical zero-area faces before the native triangle export.
        bm=bmesh.new();bm.from_mesh(target.data)
        bmesh.ops.remove_doubles(bm,dist=1e-5,verts=list(bm.verts))
        bmesh.ops.dissolve_degenerate(bm,dist=1e-5,edges=list(bm.edges))
        bmesh.ops.dissolve_limit(bm,angle_limit=.001,use_dissolve_boundaries=False,verts=list(bm.verts),edges=list(bm.edges),delimit={'MATERIAL'})
        bmesh.ops.triangulate(bm,faces=list(bm.faces))
        tiny=[face for face in bm.faces if face.calc_area()<=5.1e-7]
        if tiny:bmesh.ops.delete(bm,geom=tiny,context='FACES_ONLY')
        bm.to_mesh(target.data);bm.free();target.data.update()
        target.name=members[0]['character_name']+' · exterior '+region
        for key in ('model','part','pivot','character_name'):
            target[key]=members[0][key]
        target['export_source']=True;target['exterior_shell']=True
        target['authored_pieces']=len(members)
        target.matrix_world=Matrix.Identity(4)
        outputs.append(target)
        for obj in copies:
            data=obj.data;bpy.data.objects.remove(obj,do_unlink=True)
            if not data.users:bpy.data.meshes.remove(data)
        bpy.data.collections.remove(operands)
        for source in members:
            source['export_source']=False;source['authoring_source']=True
            source.hide_render=True;source.hide_set(True)
    return outputs+passthrough
