"""Create a shared, editable Blender scene for every exported map.

blender -b --python port/world3d/tools/blender_world.py [-- --render]
The N64 consumes L643 files; this file is an editing/inspection artifact.
"""
import bpy
import json
import math
from mathutils import Vector
from pathlib import Path
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from build_world import read_l643
OUT=ROOT/'generated'
world=json.loads((OUT/'world.json').read_text())
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)
base=bpy.context.scene
base.name='Scene sources'
materials={}
meshes={}

def linear(v):
    x=v/255
    return x/12.92 if x<=.04045 else ((x+.055)/1.055)**2.4

def mat(tex):
    key=tex or 'Vertex colors'
    if key in materials:return materials[key]
    material=bpy.data.materials.new(key)
    material.use_nodes=True
    nodes=material.node_tree.nodes;nodes.clear()
    out=nodes.new('ShaderNodeOutputMaterial')
    shader=nodes.new('ShaderNodeEmission')
    material.node_tree.links.new(shader.outputs[0],out.inputs[0])
    col=nodes.new('ShaderNodeVertexColor');col.layer_name='Color'
    if tex:
        image=bpy.data.images.load(str(OUT/'textures'/tex.replace('.sprite','.png')),check_existing=True)
        image.pack()
        node=nodes.new('ShaderNodeTexImage');node.image=image;node.interpolation='Linear'
        multiply=nodes.new('ShaderNodeMixRGB');multiply.blend_type='MULTIPLY';multiply.inputs[0].default_value=1
        material.node_tree.links.new(node.outputs['Color'],multiply.inputs[1])
        material.node_tree.links.new(col.outputs['Color'],multiply.inputs[2])
        material.node_tree.links.new(multiply.outputs[0],shader.inputs['Color'])
    else:material.node_tree.links.new(col.outputs['Color'],shader.inputs['Color'])
    materials[key]=material
    return material

def load(path):
    if path in meshes:return meshes[path]
    textures,records=read_l643(OUT/path)
    result=[]
    for i,(model,part,t,pivot,vs,inds) in enumerate(records):
        data=bpy.data.meshes.new(Path(path).stem+' '+str(i))
        positions=[(v[0],-v[2],v[1]) for v in vs]
        faces=[tuple(inds[i:i+3]) for i in range(0,len(inds),3)]
        data.from_pydata(positions,[],faces)
        data.materials.append(mat(textures[t] if t>=0 else None))
        uv=data.uv_layers.new(name='UVMap')
        color=data.color_attributes.new(name='Color',type='FLOAT_COLOR',domain='CORNER')
        for li,loop in enumerate(data.loops):
            v=vs[loop.vertex_index]
            uv.data[li].uv=(v[3],1-v[4])
            color.data[li].color=tuple(linear(c) for c in v[5:8])+(v[8]/255,)
        result.append((data,model,part,pivot))
    meshes[path]=result
    return result

actors=load('actors.l64')
individual_models={c['model']:c['file'] for c in world.get('individual_characters',[])}

def spawn_actor(scene,npc,position=None,heading=None):
    mid=npc['model'];x=npc['x'];z=npc['y']
    pose_model=world.get('npc_pose_models',[0]*256)[npc.get('npc',0)]
    if pose_model:mid=pose_model
    if position:x,z=position
    if heading is None:heading=npc.get('heading',180)
    parent=bpy.data.objects.new(npc.get('name','Actor'),None)
    scene.collection.objects.link(parent)
    # The source lying pose rests on the Water Club bench.
    support=.66 if npc.get('npc')==34 and (x,z)==(22,4) and pose_model else 0
    if 104<=npc.get('npc',0)<=109:support=2.2
    parent.location=(x,-z,support)
    parent.rotation_euler.z=math.radians(heading)
    override=next((c['scene'].removeprefix('rom:/world/') for c in world.get('map_actor_scenes',[])
                   if c['map']==scene.get('map_id') and c['model']==mid),None)
    actor_meshes=load(override or individual_models[mid]) if override or mid in individual_models else actors
    for data,model,part,pivot in actor_meshes:
        if model!=mid:continue
        obj=bpy.data.objects.new(parent.name+' part '+str(part),data)
        scene.collection.objects.link(obj)
        obj.parent=parent
        obj.location=(pivot[0],-pivot[2],pivot[1])

hall_only='--hall-only' in sys.argv
selected_maps=[m for m in world['maps'] if not hall_only or m['slug']=='hall_of_honor']
for m in selected_maps:
    scene=bpy.data.scenes.new(m['slug'])
    scene['map_id']=m['id']
    scene.render.engine='BLENDER_EEVEE'
    scene.render.resolution_x=320;scene.render.resolution_y=240;scene.render.resolution_percentage=100
    scene.render.image_settings.file_format='PNG'
    scene.view_settings.view_transform='Standard'
    scene.world=bpy.data.worlds.new(m['slug']+' ambient')
    scene.world.color=(.008,.008,.018)
    for path in (m['scene'],m['overlay']):
        if not path:continue
        for data,model,part,pivot in load(path.removeprefix('rom:/world/')):
            obj=bpy.data.objects.new(data.name,data)
            scene.collection.objects.link(obj)
    if m['id']==0:
        node=world['overworld_nodes'][0]
        spawn_actor(scene,dict(model=6,name='Selected location',x=node['marker_x'],y=node['marker_z']))
    else:
        for npc in m['npcs']:
            if npc['npc']==255 or not (0<=npc['x']<m['width'] and 0<=npc['y']<m['height']):continue
            spawn_actor(scene,npc)
        south=[w for w in m['warps'] if w['y']>=m['height']-4]
        if south:
            px=sum(w['x'] for w in south)/len(south)+1;pz=max(w['y'] for w in south)-2
        elif m['warps']:
            w=m['warps'][0];px=w['x']-2 if w['x']>m['width']/2 else w['x']+2;pz=w['y']+1
        else:px,pz=m['width']/2,m['height']-4
        spawn_actor(scene,dict(model=world.get('player_model',1),name='Player entrance',x=px,y=pz,heading=180))
    c=m['camera']
    camera=bpy.data.cameras.new(m['slug']+' camera')
    camera.lens_unit='FOV';camera.angle=2*math.atan(math.tan(math.radians(c['fov'])/2)*4/3)
    obj=bpy.data.objects.new('Camera',camera)
    obj.location=(c['eye'][0],-c['eye'][2],c['eye'][1])
    target=Vector((c['target'][0],-c['target'][2],c['target'][1]))
    obj.rotation_euler=(target-obj.location).to_track_quat('-Z','Y').to_euler()
    scene.collection.objects.link(obj);scene.camera=obj
    scene['map_id']=m['id'];scene['source_map']=m['name']
    scene['npc_state_note']='All conditional NPCs shown for inspection; gameplay filters them through original preload logic.'
    scene.render.filepath=str(OUT/'previews'/(m['slug']+'.png'))
    if '--render' in sys.argv or ('--render-overworld' in sys.argv and m['id']==0):
        (OUT/'previews').mkdir(exist_ok=True)
        bpy.ops.render.render(write_still=True,scene=scene.name)

bpy.data.scenes.remove(base)
bpy.context.window.scene=bpy.data.scenes['hall_of_honor' if hall_only else 'mason_laboratory']
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/('hall_of_honor.blend' if hall_only else 'all_maps.blend')))
print(f'Created {len(selected_maps)} editable Blender scenes with shared mesh/texture datablocks')
