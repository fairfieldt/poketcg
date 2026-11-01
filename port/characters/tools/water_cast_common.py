"""Owning, volumetric Water Club character pieces; no boolean union required.

Author coordinates match parts.py: X right, Y front, Z up. mesh() performs the
single Y reflection/winding reversal and add() applies the family scale once.
Only Water cast modules import this helper; default character recipes retain
all their original shapes and lighting. Head components share a distinct rigid
part-0 pivot so Sara/Amanda can replace a head without touching their body rig.
"""
import hashlib
import math
from pathlib import Path
import re

HEAD_PIVOT=(0,0,1.5)


def remove_sources(ctx,predicate):
    """Keep removed authoring pieces editable, but never export/render them."""
    removed=[];keep=[]
    for obj in ctx['sources']:
        if predicate(obj):
            obj['export_source']=False;obj['authoring_source']=True
            obj.hide_render=True;obj.hide_set(True);removed.append(obj)
        else:keep.append(obj)
    ctx['sources'][:]=keep
    return removed


def _material(ctx,value):
    return ctx[value] if isinstance(value,str) else value


def mesh(ctx,name,verts,faces,materials,face_material_indices=None,uvs=None,
         part=0,pivot=(0,0,0),head_component=False):
    """Make a true multi-material mesh from author-coordinate vertices.

    uvs may be a per-face list of loop coordinates, a per-vertex coordinate
    list, or a mapping from face index to loop coordinates. No backing mesh or
    face overlay is created. Closed owning pieces bypass exterior boolean bake.
    Set head_component=True for goggles/hair additions as well as the head.
    """
    import bpy
    if not isinstance(materials,(tuple,list)):materials=[materials]
    mats=[_material(ctx,m) for m in materials]
    assert mats and all(m is not None for m in mats),name
    indices=face_material_indices or [0]*len(faces)
    assert len(indices)==len(faces),name
    assert all(0<=i<len(mats) for i in indices),name
    assert all(len(face)>=3 and len(set(face))==len(face) for face in faces),name
    assert all(all(math.isfinite(c) for c in v) for v in verts),name
    if head_component and pivot==(0,0,0):pivot=HEAD_PIVOT
    data=bpy.data.meshes.new(ctx['slug']+'_water_'+name)
    data.from_pydata([(x,-y,z) for x,y,z in verts],[],[tuple(reversed(f)) for f in faces])
    for mat in mats:data.materials.append(mat)
    layer=data.uv_layers.new()
    vertex_uv=(isinstance(uvs,(tuple,list)) and len(uvs)==len(verts) and
               bool(uvs) and len(uvs[0])==2 and isinstance(uvs[0][0],(float,int)))
    for i,poly in enumerate(data.polygons):
        poly.material_index=indices[i]
        if uvs is None:coords=[(0,0)]*len(faces[i])
        elif vertex_uv:coords=[uvs[v] for v in faces[i]]
        elif isinstance(uvs,dict):coords=uvs.get(i,[(0,0)]*len(faces[i]))
        else:coords=uvs[i]
        assert len(coords)==len(poly.loop_indices),(name,i)
        for loop,uv in zip(poly.loop_indices,reversed(coords)):layer.data[loop].uv=uv
    data.update()
    obj=ctx['add'](data,name,mats[0],part=part,pivot=pivot)
    # add() provides a slot-0 override; all remaining slots stay explicit too.
    for slot,mat in zip(obj.material_slots,mats):slot.link='OBJECT';slot.material=mat
    obj['native_exterior_ready']=True;obj['exterior_shell']=True
    obj['water_cast_piece']=True
    if head_component:obj['head_component']=True
    return obj


def _rgb8(color):
    values=tuple(color[:3])
    return tuple(round(v*255) for v in values) if max(values)<=1 else tuple(round(v) for v in values)


def face_material(ctx,style='friendly',eye_color=(43,75,121),eyebrows='gentle',
                  smile=True,skin=None,**options):
    """Paint broad native-readable features, keeping the opaque skin pigment.

    Large 9x12-pixel eyes occupy substantially more of the head than the old
    3-strip face. Low-contrast cheek noise and one-pixel nostrils are omitted;
    nose/chin form is geometry. Every appearance gets a content-specific name.
    """
    import bpy
    skin=_material(ctx,skin or 'skin')
    pigment=_rgb8(skin.diffuse_color)
    pixels=[[(*pigment,255) for _ in range(32)] for _ in range(32)]
    ink=(29,31,38);white=(246,242,218);iris=_rgb8(eye_color)
    def rect(a,b,c,d,color):
        for y in range(max(0,b),min(32,d)):
            for x in range(max(0,a),min(32,c)):pixels[y][x]=(*color,255)
    stern=eyebrows=='stern' or style=='stern'
    sly=eyebrows=='sly' or style=='amanda'
    lashes=bool(options.get('eyelashes',style in ('amy','sara','amanda')))
    for side,left in enumerate((4,19)):
        rect(left+1,9,left+8,21,ink)
        rect(left,11,left+9,19,ink)
        rect(left+1,11,left+8,20,white)
        rect(left+3,11,left+7,20,iris)
        rect(left+4,12,left+7,19,ink)
        rect(left+4,11,left+6,13,white)
        if stern:
            for x in range(8):
                y=6+(x//3 if side==0 else (7-x)//3)
                rect(left+x,y,left+x+1,y+2,ink)
        elif sly:
            brow=5 if side==0 else 7
            rect(left+1,brow,left+8,brow+2,ink)
        else:rect(left+1,6,left+8,8,ink)
        if lashes:
            outer=left-1 if side==0 else left+7
            rect(outer,9,outer+3,11,ink)
    if smile=='smirk':
        rect(12,26,20,28,ink);rect(19,25,22,27,ink)
    elif smile:
        rect(12,25,20,27,ink);rect(14,27,18,28,ink)
        rect(14,25,18,26,white)
    else:rect(12,26,20,28,ink)
    raw=b''.join(bytes(c for pixel in row for c in pixel) for row in pixels)
    digest=hashlib.sha256(raw).hexdigest()
    name='face_wt_'+ctx['slug'][:3]+'_'+digest[:4]
    asset_root=Path(ctx.get('ASSET_ROOT',Path(ctx['ROOT'])/'assets'))
    path=asset_root/'textures'/(name+'.png')
    path.parent.mkdir(parents=True,exist_ok=True)
    ctx['write_png'](path,32,32,[bytes(c for pixel in row for c in pixel) for row in pixels])
    image=bpy.data.images.get(name)
    if image is None:
        image=bpy.data.images.load(str(path),check_existing=False);image.name=name
        image.pack()
    mat=ctx['material']('face',(1,1,1),image)
    mat['role']='face';mat['native_shade_floor']=.86
    mat['water_face_style']=style;mat['water_face_sha256']=digest
    return mat


def prune_textures(ctx):
    """Drop unused generic facial paint after a complete owning replacement.

    Invoke after apply/apply_appearance and before prepare_skin/export. Only
    materials used by current sources are reindexed; preserved authoring parts
    remain hidden and keep their original editable materials.
    """
    textures=ctx['textures']
    materials={slot.material for obj in ctx['sources'] for slot in obj.material_slots
               if slot.material is not None}
    used=sorted({int(mat.get('texture_id',-1)) for mat in materials if int(mat.get('texture_id',-1))>=0})
    assert all(index<len(textures) for index in used),used
    names=[];remap={}
    for index in used:
        name=textures[index]
        if name not in names:names.append(name)
        remap[index]=names.index(name)
    for mat in materials:
        index=int(mat.get('texture_id',-1))
        if index>=0:mat['texture_id']=remap[index]
    textures[:]=names
    return names


def _head_source(obj):
    if obj.get('head_component',False):return True
    label=obj.name.lower().split(' · ')[-1]
    words=set(re.findall(r'[a-z]+',label))
    return bool(words & {'head','face','nose','ear','ears','hair','fringe','cap','brim','headband','goggles'})


def head_geometry(*,width=.86,depth=.70,bottom=1.45,top=2.26,
                  face_low=1.53,face_high=2.05,nose=.10,hair='bob',
                  sweep=0,segments=16,chin=.68,ear=.035,
                  face_wrap_degrees=120,feature_height=.80,**options):
    """One closed shell whose skin, face and hair own disjoint polygons.

    The UV face wraps from the front to both temples. Profile silhouette has a
    chin, projecting nose, rounded cheeks and a full rear cranium. The material
    boundary is a real shared edge, so hair never exposes a hidden scalp.
    Cap means a brimless swimming-cap envelope; a trainer brim is separate.
    """
    assert segments>=12 and segments%4==0
    assert top>face_high>face_low>bottom
    assert 60<=face_wrap_degrees<=180 and .5<=feature_height<=1
    feature_span=math.radians(face_wrap_degrees)
    feature_low=(face_low+face_high)/2-(face_high-face_low)*feature_height/2
    rx=width/2;ry=depth/2
    # Relative widths/depths are deliberately broad and calm, not many tiny
    # cheek facets. The nose tip is a real central vertex at the nose row.
    middle=face_low+(face_high-face_low)*.44
    eyes=face_low+(face_high-face_low)*.74
    levels=[(bottom,.36,.51),
            (face_low,chin,.75),
            (middle,.97,.94),
            (eyes,1.00,1.00),
            (face_high,.93,.96),
            (face_high+(top-face_high)*.66,.68,.73),
            (top,.24,.28)]
    angles=[-math.pi+i*math.tau/segments for i in range(segments)]
    verts=[]
    for row,(z,wide,deep) in enumerate(levels):
        for theta in angles:
            front=max(0,math.cos(theta));side=abs(math.sin(theta))
            xx=rx*wide*math.sin(theta)
            yy=ry*deep*math.cos(theta)
            zz=z
            if row==1:yy+=.018*front
            if row==2:
                yy+=nose*front**18
                xx+=math.copysign(ear*side**16,xx) if abs(xx)>1e-8 else 0
            if row==3:yy+=nose*.18*front**12
            if row==4:zz+=sweep*math.sin(theta)*front
            verts.append((xx,yy,zz))
    bottom_center=len(verts);verts.append((0,0,bottom))
    top_center=len(verts);verts.append((0,0,top))
    faces=[];mats=[];uvs=[]
    for i in range(segments):
        faces.append((bottom_center,i,(i+1)%segments));mats.append(0);uvs.append([(0,0)]*3)
    for row in range(len(levels)-1):
        for i in range(segments):
            j=(i+1)%segments
            face=(row*segments+i+segments,row*segments+j+segments,row*segments+j,row*segments+i)
            # Angle midpoint accounts for the rear seam across +/-pi.
            theta=angles[i]+math.pi/segments
            is_front=abs(theta)<=math.pi*.50001
            if hair=='none':hair_region=False
            elif row>=4:hair_region=True
            elif hair=='cap':hair_region=(not is_front and row>=2)
            elif hair in ('bob','long'):hair_region=not is_front
            else:hair_region=(not is_front and row>=2)
            material=2 if hair_region else 1 if is_front and 1<=row<=3 else 0
            faces.append(face);mats.append(material)
            loop=[]
            for vertex in face:
                ring,angular=divmod(vertex,segments)
                theta_v=angles[angular]
                # No textured polygon crosses the rear seam.
                # Features occupy the front120 degrees and 80% of the
                # face height. Owning skin still wraps to both temples;
                # the outer cheek strips sample plain edge pigment.
                u=max(0,min(1,.5+theta_v/feature_span))
                v=max(0,min(1,(verts[vertex][2]-feature_low)/((face_high-face_low)*feature_height)))
                loop.append((u,v))
            uvs.append(loop)
    for i in range(segments):
        faces.append((top_center,(len(levels)-1)*segments+(i+1)%segments,(len(levels)-1)*segments+i))
        mats.append(0 if hair=='none' else 2);uvs.append([(0,0)]*3)
    return verts,faces,mats,uvs


def head(ctx,style='friendly',palette=None,replace=True,**shapeoptions):
    """Replace only head sources, and return its single owning shell object."""
    palette=palette or {}
    skin=_material(ctx,palette.get('skin','skin'))
    hair=_material(ctx,palette.get('hair','hair_mat'))
    face=palette.get('face')
    if face is None:
        face=face_material(ctx,style,skin=skin,
             eye_color=palette.get('eye_color',(43,75,121)),
             eyebrows=palette.get('eyebrows','gentle'),smile=palette.get('smile',True))
    else:face=_material(ctx,face)
    skin['native_shade_floor']=.86;face['native_shade_floor']=.86
    hair['native_shade_floor']=.78
    if replace:remove_sources(ctx,_head_source)
    verts,faces,indices,uvs=head_geometry(**shapeoptions)
    obj=mesh(ctx,'Water owning head',verts,faces,[skin,face,hair],indices,uvs,
             part=0,pivot=HEAD_PIVOT,head_component=True)
    obj['water_head_style']=style
    obj['water_face_wrap_degrees']=float(shapeoptions.get('face_wrap_degrees',120))
    obj['water_face_owning_degrees']=180
    obj['water_feature_height']=float(shapeoptions.get('feature_height',.80))
    segments=int(shapeoptions.get('segments',16))
    obj['water_head_segments']=segments
    obj['water_head_side_offset']=segments
    obj['water_head_side_rows']=6
    return obj


def torso(ctx,profiles,material='accent',name='Water torso',segments=12,
          part=0,pivot=(0,0,0),materials=None,face_material_indices=None):
    """A closed volumetric ring body; profiles are (z,rx,ry,cx,cy)."""
    from parts import rings
    verts,faces=rings(profiles,segments)
    return mesh(ctx,name,verts,faces,materials or material,face_material_indices,
                part=part,pivot=pivot)
