"""Load round authoring meshes with packed textures into one editable .blend."""
import bpy,json,sys
from pathlib import Path
from mathutils import Vector
folder=Path(sys.argv[sys.argv.index('--')+1]).resolve()
bpy.ops.wm.read_factory_settings(use_empty=True)
scene=bpy.context.scene;scene.render.engine='BLENDER_EEVEE';scene.world=bpy.data.worlds.new('World');scene.world.color=(.18,.18,.18)
scene.view_settings.view_transform='Standard'
for offset,slug in [(-.85,'cat_tuxedo'),(.85,'cat_tabby')]:
    description=json.loads((folder/slug/'authoring.json').read_text())
    collection=bpy.data.collections.new(slug);scene.collection.children.link(collection)
    for entry in description['meshes']:
        mesh=bpy.data.meshes.new(entry['name']);mesh.from_pydata([(x,-y,z) for x,y,z in entry['verts']],[],[list(reversed(f['idx'])) for f in entry['faces']]);mesh.update()
        indices={}
        for m in entry['materials']:
            mat=bpy.data.materials.new(slug+' '+m['name']);mat.diffuse_color=(*m['rgb'],1);mat.use_nodes=True
            nodes=mat.node_tree.nodes;links=mat.node_tree.links;shader=nodes.get('Principled BSDF');shader.inputs['Base Color'].default_value=(*m['rgb'],1);shader.inputs['Roughness'].default_value=1
            if m['texture']:
                tex=nodes.new('ShaderNodeTexImage');tex.image=bpy.data.images.load(str(folder/slug/'textures'/f'{m["texture"]}.png'));tex.image.pack();tex.interpolation='Closest';links.new(tex.outputs['Color'],shader.inputs['Base Color'])
            indices[m['name']]=len(mesh.materials);mesh.materials.append(mat)
        uv=mesh.uv_layers.new(name='UVMap')
        for poly,f in zip(mesh.polygons,entry['faces']):
            poly.material_index=indices[f['mat']];poly.use_smooth=f['smooth']
            if f['uv']:
                for loop,coord in zip(poly.loop_indices,reversed(f['uv'])):uv.data[loop].uv=(coord[0],1-coord[1])
        obj=bpy.data.objects.new(entry['name'],mesh);collection.objects.link(obj);obj.location.x=offset;obj['rigid_part']=entry['part'];obj['animation_pivot']=entry['pivot']
bpy.ops.object.camera_add(location=(3.5,-9,3.5));camera=bpy.context.object;camera.rotation_euler=(Vector((0,0,1.3))-camera.location).to_track_quat('-Z','Y').to_euler();camera.data.type='ORTHO';camera.data.ortho_scale=4.6;scene.camera=camera
for pos,power,size in [((1,-4,6),450,5),((-4,-1,3),250,4)]:
    bpy.ops.object.light_add(type='AREA',location=pos);bpy.context.object.data.energy=power;bpy.context.object.data.shape='DISK';bpy.context.object.data.size=size
scene.render.resolution_x=1000;scene.render.resolution_y=800;scene.render.resolution_percentage=100;scene.render.filepath=str(folder/'blender-preview.png')
bpy.ops.wm.save_as_mainfile(filepath=str(folder/'cats.blend'));bpy.ops.render.render(write_still=True)
