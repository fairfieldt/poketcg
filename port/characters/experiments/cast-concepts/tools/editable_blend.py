"""Import retained native authoring meshes into an editable Blender file."""
import bpy,json,sys
from pathlib import Path
folder=Path(sys.argv[sys.argv.index('--')+1]).resolve()
slug=sys.argv[sys.argv.index('--')+2]
bpy.ops.wm.read_factory_settings(use_empty=True)
description=json.loads((folder/slug/'authoring.json').read_text())
for entry in description['meshes']:
    mesh=bpy.data.meshes.new(entry['name']);mesh.from_pydata(entry['verts'],[],[f['idx'] for f in entry['faces']]);mesh.update()
    indices={}
    for m in entry['materials']:
        mat=bpy.data.materials.new(m['name']);mat.diffuse_color=(*m['rgb'],1);mat.use_nodes=True
        nodes=mat.node_tree.nodes;links=mat.node_tree.links;nodes.clear();out=nodes.new('ShaderNodeOutputMaterial');em=nodes.new('ShaderNodeEmission');links.new(em.outputs[0],out.inputs[0])
        if m['texture']:
            tex=nodes.new('ShaderNodeTexImage');tex.image=bpy.data.images.load(str(folder/slug/'textures'/f'{m["texture"]}.png'));tex.image.pack();tex.interpolation='Closest';links.new(tex.outputs[0],em.inputs[0])
        else:em.inputs[0].default_value=(*m['rgb'],1)
        indices[m['name']]=len(mesh.materials);mesh.materials.append(mat)
    uv=mesh.uv_layers.new(name='UVMap')
    for poly,f in zip(mesh.polygons,entry['faces']):
        poly.material_index=indices[f['mat']];poly.use_smooth=f['smooth']
        if f['uv']:
            for loop,coord in zip(poly.loop_indices,f['uv']):uv.data[loop].uv=(coord[0],1-coord[1])
    obj=bpy.data.objects.new(entry['name'],mesh);bpy.context.collection.objects.link(obj);obj['rigid_part']=entry['part'];obj['animation_pivot']=entry['pivot']
bpy.ops.wm.save_as_mainfile(filepath=str(folder/f'{slug}.blend'))
