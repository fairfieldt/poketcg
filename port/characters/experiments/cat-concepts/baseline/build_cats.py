"""Author the two port-only cats, their five-part native meshes and editable sources.

Run with Blender --background --python this_file. No existing cast is rewritten.
"""
import json, math, sys, shutil
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'port/world3d/tools'))
from build_world import Scene, OUT, read_l643
ASSETS=ROOT/'port/characters/assets/models'


def build(slug,model,tabby):
    s=Scene(slug)
    gray=(116,120,128,255) if tabby else (82,89,102,255)
    dark=(62,66,76,255); white=(228,229,221,255); muzzle=(195,191,179,255) if tabby else white
    pink=(153,110,116,255); ink=(30,34,41,255); eye=(174,193,116,255)
    def face(points,color,part=0,pivot=(0,0,0)):
        shade=.82+.18*max(0,sum(p[1] for p in points)/len(points)/2.5)
        c=tuple(round(v*shade) for v in color[:3])+(255,)
        s.polygon([tuple(p[k]-pivot[k] for k in range(3)) for p in points],c,model=model,part=part,pivot=pivot)
    def ellipsoid(center,radii,color,segments=10,rings=5,part=0,pivot=(0,0,0),paint=None):
        def point(i,j):
            a=math.pi*i/rings;b=2*math.pi*j/segments
            return (center[0]+radii[0]*math.sin(a)*math.cos(b),center[1]+radii[1]*math.cos(a),center[2]+radii[2]*math.sin(a)*math.sin(b))
        for i in range(rings):
            for j in range(segments):
                pts=[point(i,j),point(i+1,j),point(i+1,j+1),point(i,j+1)]
                mid=tuple(sum(p[k] for p in pts)/4 for k in range(3))
                face(pts,paint(mid,i,j) if paint else color,part,pivot)
    def bodypaint(p,i,j):
        x,y,z=p
        if not tabby and z>.13 and abs(x)<.28 and y>1.03:return white
        if tabby and i in (1,3) and abs(x)>.18:return dark
        return gray
    ellipsoid((0,1.03,0),(.38,.61,.27),gray,12,6,paint=bodypaint)
    # Compact legs with the original human rig's opposing gait pivots.
    for sign,part in [(-1,3),(1,4)]:
        pivot=(sign*.19,.63,0)
        ellipsoid((sign*.19,.37,.015),(.16,.34,.18),gray,8,4,part,pivot,
                  lambda p,i,j: dark if tabby and i==1 else gray)
        ellipsoid((sign*.19,.13,.11),(.19,.13,.25),gray if tabby else white,8,3,part,pivot)
    for sign,part in [(-1,1),(1,2)]:
        pivot=(sign*.36,1.39,0)
        ellipsoid((sign*.43,1.08,.025),(.14,.36,.15),gray,8,4,part,pivot,
                  lambda p,i,j:dark if tabby and i in (1,3) else gray)
        ellipsoid((sign*.44,.79,.065),(.15,.15,.17),gray if tabby else white,8,3,part,pivot)
    # Rounded feline skull, broad cheeks, large ears and separate white chin.
    def headpaint(p,i,j):
        x,y,z=p
        if tabby and y>1.95 and j in (1,3,5,7,9,11):return dark
        return gray
    ellipsoid((0,1.91,.015),(.49,.46,.34),gray,12,7,paint=headpaint)
    for sign in (-1,1):
        a=(sign*.23,2.18,-.10);b=(sign*.50,2.16,.03);c=(sign*.43,2.67,-.045);d=(sign*.23,2.18,.16)
        for tri in [(a,c,b),(a,d,c),(d,b,c),(a,b,d)]:face(tri,gray)
        face([(sign*.29,2.24,.125),(sign*.445,2.24,.063),(sign*.414,2.54,.025)],pink)
    ellipsoid((0,1.64,.25),(.30,.15,.16),muzzle,10,3)
    for sign in (-1,1):
        ellipsoid((sign*.115,1.76,.305),(.18,.13,.125),muzzle,8,3)
        # The eyes sit on the skull and have dark rims, pale green irises,
        # narrow pupils and a small highlight visible in conversation views.
        ellipsoid((sign*.235,1.99,.282),(.164,.157,.055),ink,8,4)
        ellipsoid((sign*.235,1.99,.317),(.139,.128,.034),eye,8,4)
        ellipsoid((sign*.235,1.99,.345),(.025,.108,.012),ink,6,3)
        ellipsoid((sign*.263,2.035,.349),(.023,.028,.012),white,6,3)
        # Three tapered whiskers on each cheek.
        for k in range(3):
            y=1.745+k*.035
            face([(sign*.205,y,.397),(sign*.58,y+(k-1)*.075,.35),(sign*.205,y+.012,.397)],white)
    face([(-.073,1.80,.433),(.073,1.80,.433),(0,1.735,.452)],pink if tabby else ink)
    face([(-.009,1.735,.451),(.009,1.735,.451),(0,1.67,.42)],ink)
    # Upright curved tail behind the body, with a dark tabby ring pattern.
    centers=[(.12,.59,-.23),(.31,.68,-.43),(.46,.90,-.48),(.49,1.15,-.48),(.44,1.37,-.47),(.30,1.48,-.43)]
    rings=[]
    for i,(x,y,z) in enumerate(centers):
        r=.09 if i<len(centers)-1 else .045
        rings.append([(x+r*math.cos(j*math.pi/3),y,z+r*math.sin(j*math.pi/3)) for j in range(6)])
    for i in range(len(rings)-1):
        for j in range(6):face([rings[i][j],rings[i+1][j],rings[i+1][(j+1)%6],rings[i][(j+1)%6]],dark if tabby and i%2 else gray)
    face(rings[-1],dark if tabby else gray)
    folder=ASSETS/slug;folder.mkdir(parents=True,exist_ok=True)
    temp=OUT/'maps'/('_'+slug+'.l64');record=s.save(temp)
    shutil.copyfile(temp,folder/'scene.l64');temp.unlink()
    record.update(format='L643',model=slug,appearance=slug,pose='standing',actors=[dict(name=slug,model=model)])
    record.pop('file');(folder/'scene.json').write_text(json.dumps(record,indent=2)+'\n')
    (folder/'complete.json').write_text(json.dumps(dict(model=slug,appearances=[slug]))+'\n')
    return folder,record

cats=[build('cat_tuxedo',200,False),build('cat_tabby',201,True)]
print([(p.name,r['triangles']) for p,r in cats])

# Editable ordinary Blender meshes, kept independently from the original cast.
import bpy
from mathutils import Vector
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
scene=bpy.context.scene;scene.render.engine='BLENDER_EEVEE'
scene.world.color=(.25,.25,.25)
scene.view_settings.view_transform='Standard'
for index,(folder,record) in enumerate(cats):
    root=bpy.data.objects.new(folder.name,None);scene.collection.objects.link(root)
    root.location.x=index*1.7-.85
    names,meshes=read_l643(folder/'scene.l64')
    for k,(model,part,t,pivot,vs,inds) in enumerate(meshes):
        mesh=bpy.data.meshes.new(folder.name+' part '+str(part))
        mesh.from_pydata([(v[0],-v[2],v[1]) for v in vs],[],[inds[i:i+3] for i in range(0,len(inds),3)])
        colors=mesh.color_attributes.new(name='Color',type='FLOAT_COLOR',domain='CORNER')
        for li,loop in enumerate(mesh.loops):colors.data[li].color=tuple((c/255)**2.2 for c in vs[loop.vertex_index][5:8])+(1,)
        mat=bpy.data.materials.new(folder.name+' fur');mat.use_nodes=True
        node=mat.node_tree.nodes.new('ShaderNodeVertexColor');node.layer_name='Color'
        mat.node_tree.links.new(node.outputs['Color'],mat.node_tree.nodes.get('Principled BSDF').inputs['Base Color'])
        mesh.materials.append(mat)
        obj=bpy.data.objects.new(folder.name+' rigid '+str(part),mesh);scene.collection.objects.link(obj);obj.parent=root
        obj.location=(pivot[0],-pivot[2],pivot[1]);obj['rigid_part']=part;obj['animation_pivot']=list(pivot)
    root['native_model']=200+index
# Soft studio preview, front three-quarter view.
bpy.ops.object.camera_add(location=(3.5,-8,3.3));camera=bpy.context.object
camera.rotation_euler=(Vector((0,0,1.3))-camera.location).to_track_quat('-Z','Y').to_euler();camera.data.type='ORTHO';camera.data.ortho_scale=4.5;scene.camera=camera
for pos,power,size in [((1,-4,6),450,5),((-4,-1,3),250,4)]:
    bpy.ops.object.light_add(type='AREA',location=pos);bpy.context.object.data.energy=power;bpy.context.object.data.shape='DISK';bpy.context.object.data.size=size
review=ROOT/'port/reviews/cat-duo';review.mkdir(parents=True,exist_ok=True)
scene.render.resolution_x=1000;scene.render.resolution_y=800;scene.render.resolution_percentage=100
scene.render.filepath=str(review/'models.png')
bpy.ops.wm.save_as_mainfile(filepath=str(review/'cats.blend'))
bpy.ops.render.render(write_still=True)
