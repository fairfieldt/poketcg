"""Import generated authoring JSON into Blender, save .blend files and render review sheets.

blender -b --python tools/blender_preview.py -- [style ids...]
Materials reproduce the console look: baked vertex colour (lighting) times texture, unlit.
"""
import bpy,json,math,sys
from pathlib import Path
HERE=Path(__file__).resolve().parent
ROOT=HERE.parent
OPTIONS=ROOT/'options'
sys.path.insert(0,str(HERE))
import builder
from geom import bake

argv=sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else []
SLUGS=['mark','dr_mason','ronald']


def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene=bpy.context.scene
    scene.render.engine='BLENDER_EEVEE'
    scene.view_settings.view_transform='Standard'
    scene.render.film_transparent=False
    world=bpy.data.worlds.new('World');scene.world=world;world.use_nodes=True
    world.node_tree.nodes['Background'].inputs[0].default_value=(.22,.24,.28,1)
    return scene


def material_for(name,rgb,texture_path,lit):
    mat=bpy.data.materials.new(name);mat.use_nodes=True
    nodes=mat.node_tree.nodes;links=mat.node_tree.links
    nodes.clear()
    out=nodes.new('ShaderNodeOutputMaterial');em=nodes.new('ShaderNodeEmission')
    links.new(em.outputs[0],out.inputs[0])
    attr=nodes.new('ShaderNodeVertexColor');attr.layer_name='Col'
    if texture_path:
        tex=nodes.new('ShaderNodeTexImage');tex.image=bpy.data.images.load(texture_path);tex.interpolation='Linear'
        tex.image.colorspace_settings.name='sRGB'
        mix=nodes.new('ShaderNodeMixRGB');mix.blend_type='MULTIPLY';mix.inputs[0].default_value=1
        links.new(tex.outputs[0],mix.inputs[1]);links.new(attr.outputs[0],mix.inputs[2]);links.new(mix.outputs[0],em.inputs[0])
    else:
        links.new(attr.outputs[0],em.inputs[0])
    return mat


def import_figure(style,design,offset,collection):
    fig=builder.build(style,design,paint=False)
    lighting=style.light
    objects=[]
    for mesh in fig.meshes:
        normals=mesh.vertex_normals()
        data=bpy.data.meshes.new(f'{fig.prefix}_{mesh.name}')
        verts=[(x+offset[0],-(y+offset[1]),z+offset[2]) for x,y,z in mesh.verts]
        faces=[list(reversed(f['idx'])) for f in mesh.faces]
        data.from_pydata(verts,[],faces)
        mats={}
        for f in mesh.faces:
            m=f['mat']
            if m.name not in mats:
                texpath=str(OPTIONS/style.id/design.slug/'textures'/(m.texture+'.png')) if m.texture else None
                mats[m.name]=len(data.materials);data.materials.append(material_for(f'{fig.prefix}_{m.name}',m.rgb,texpath,m.lit))
        uv=data.uv_layers.new(name='UVMap');col=data.color_attributes.new('Col','BYTE_COLOR','CORNER')
        for pi,(poly,f) in enumerate(zip(data.polygons,mesh.faces)):
            poly.material_index=mats[f['mat'].name];poly.use_smooth=f['group'] is not None
            fn=mesh.face_normal(f)
            idx=list(reversed(f['idx']));uvs=list(reversed(f['uv'])) if f['uv'] else None
            for li,loop_index in enumerate(poly.loop_indices):
                vi=idx[li]
                n=normals.get((vi,f['group']),fn) if f['group'] is not None else fn
                shade=lighting.shade(n) if f['mat'].lit else 1.0
                rgb=(shade,shade,shade) if f['mat'].texture else tuple(c*shade for c in f['mat'].rgb)
                # vertex colours are stored linear; emission shows them as-is with Standard view
                col.data[loop_index].color=(rgb[0]**2.2,rgb[1]**2.2,rgb[2]**2.2,1)
                if uvs:uv.data[loop_index].uv=(uvs[li][0],1-uvs[li][1])
        obj=bpy.data.objects.new(data.name,data);collection.objects.link(obj)
        obj['part']=mesh.part;obj['pivot']=list(mesh.pivot);obj['style']=style.id;obj['character']=design.slug
        objects.append(obj)
    return objects


def render(scene,path,eye,target,ortho_scale=None,res=(960,540),fov=30):
    cam_data=bpy.data.cameras.new('cam');cam=bpy.data.objects.new('cam',cam_data);scene.collection.objects.link(cam)
    scene.camera=cam
    cam.location=eye
    direction=(target[0]-eye[0],target[1]-eye[1],target[2]-eye[2])
    import mathutils
    cam.rotation_euler=mathutils.Vector(direction).to_track_quat('-Z','Y').to_euler()
    if ortho_scale:cam_data.type='ORTHO';cam_data.ortho_scale=ortho_scale
    else:cam_data.angle=math.radians(fov)
    scene.render.resolution_x,scene.render.resolution_y=res;scene.render.resolution_percentage=100
    scene.render.filepath=str(path);bpy.ops.render.render(write_still=True)
    bpy.data.objects.remove(cam,do_unlink=True)


def main():
    styles=[builder.STYLES[s] for s in (argv or list(builder.STYLES))]
    review=ROOT/'review';review.mkdir(exist_ok=True)
    for style in styles:
        scene=reset()
        coll=bpy.data.collections.new(style.id);scene.collection.children.link(coll)
        for i,slug in enumerate(SLUGS):
            import_figure(style,builder.DESIGNS[slug],(i*1.6-1.6,0,0),coll)
        # back row copies rotated 180 for the back view
        back=bpy.data.collections.new(style.id+'_back');scene.collection.children.link(back)
        for i,slug in enumerate(SLUGS):
            objs=import_figure(style,builder.DESIGNS[slug],(0,0,0),back)
            for o in objs:
                o.rotation_euler[2]=math.pi;o.location=(i*1.6-1.6,2.4,0)
        bpy.ops.wm.save_as_mainfile(filepath=str(OPTIONS/style.id/f'{style.id}.blend'))
        render(scene,review/f'{style.id}-front.png',(-.3,-7.4,2.2),(0,1.0,1.05),fov=36,res=(1200,700))
        render(scene,review/f'{style.id}-threequarter.png',(5.0,-5.6,2.9),(0,1.0,1.0),fov=38,res=(1200,700))
        print('RENDERED',style.id)


main()
