"""Amy's source-led Water Club silhouette and five-part animated body.

A white sleeveless dress, swept blue bob and a calm, readable face replace the
generic sheet-hair/flat-face recipe. Canonical standing geometry is reused by
the existing lying-pose export; no pose-specific duplicate is authored here.
"""
import math


def _tube(ctx,name,profiles,materials,*,bands=None,segments=8,part=0,pivot=(0,0,0),caps=True):
    """Owning ring skin; profiles are (z,rx,ry,cx,cy)."""
    from water_cast_common import mesh
    verts=[(cx+rx*math.sin(i*math.tau/segments),
            cy+ry*math.cos(i*math.tau/segments),z)
           for z,rx,ry,cx,cy in profiles for i in range(segments)]
    faces=[];slots=[]
    if caps:
        bottom=len(verts);verts.append((profiles[0][3],profiles[0][4],profiles[0][0]))
        top=len(verts);verts.append((profiles[-1][3],profiles[-1][4],profiles[-1][0]))
        for i in range(segments):
            faces.append((bottom,i,(i+1)%segments));slots.append(0)
    for row in range(len(profiles)-1):
        for i in range(segments):
            a=row*segments+i;b=row*segments+(i+1)%segments
            faces.append((a+segments,b+segments,b,a))
            slots.append(bands[row] if bands else 0)
    if caps:
        offset=(len(profiles)-1)*segments
        for i in range(segments):
            faces.append((top,offset+(i+1)%segments,offset+i));slots.append(0)
    return mesh(ctx,name,verts,faces,materials,face_material_indices=slots,part=part,pivot=pivot)


def _body(ctx):
    from water_cast_common import mesh
    # Broad hem, fitted waist and rounded shoulders form one solid dress. The
    # blue binding and waistband own their bands rather than overlaying a body.
    _tube(ctx,'Amy white sleeveless dress',[
        (.49,.40,.255,0,-.01),(.55,.40,.255,0,-.01),
        (.79,.285,.205,0,0),(.85,.285,.205,0,0),
        (.91,.265,.195,0,0),(1.18,.315,.235,0,.005),
        (1.38,.285,.205,0,0),(1.46,.245,.175,0,-.005)],
        [ctx['white'],ctx['accent']],bands=[1,0,1,0,0,0,0],segments=10)
    # A real neck and a narrow blue choker connect head and dress from side
    # views; the shared head starts at1.45 and owns the chin itself.
    _tube(ctx,'Amy neck',[(1.40,.115,.11,0,0),(1.51,.13,.12,0,0)],
          [ctx['skin']],segments=8)
    # Sleeveless shoulder/upper-arm volumes taper through a subtle elbow.
    # The arm and hand are one skin silhouette per original animation joint.
    for side in (-1,1):
        joint=3 if side<0 else 4;pivot=(side*.40,0,1.36)
        _tube(ctx,'Amy arm '+str(side),[
            (.72,.075,.085,side*.465,0),(.79,.10,.105,side*.465,.025),
            (.89,.085,.095,side*.445,.012),(1.09,.095,.105,side*.405,0),
            (1.34,.105,.125,side*.355,0),(1.43,.075,.095,side*.325,0)],
            [ctx['skin']],part=joint,pivot=pivot,segments=8)
        leg_joint=1 if side<0 else 2;leg_pivot=(side*.17,0,.72)
        _tube(ctx,'Amy leg '+str(side),[
            (.12,.080,.095,side*.17,.005),(.29,.086,.105,side*.17,0),
            (.48,.102,.115,side*.17,0),(.62,.115,.12,side*.17,0)],
            [ctx['skin']],part=leg_joint,pivot=leg_pivot,segments=8)
        # Small rounded white pool shoes with a blue sole replace the black
        # rectangular generic shoes. Their toe volume reads in profile.
        _tube(ctx,'Amy pool shoe '+str(side),[
            (0,.125,.18,side*.17,.055),(.060,.125,.18,side*.17,.055),
            (.15,.115,.165,side*.17,.045),(.18,.075,.105,side*.17,.015)],
            [ctx['white'],ctx['accent']],bands=[1,0,0],part=leg_joint,
            pivot=leg_pivot,segments=8)


def _amy_face(ctx):
    """Large blue eyes, one lifted brow and a soft closed smile."""
    import bpy
    import hashlib
    from pathlib import Path
    pigment=tuple(round(c*255) for c in ctx['skin'].diffuse_color[:3])
    pixels=[[(*pigment,255) for _ in range(32)] for _ in range(32)]
    ink=(31,35,49);iris=(38,81,128);light=(246,242,220);warm=(103,61,43)
    def rect(a,b,c,d,color):
        for y in range(b,d):
            for x in range(a,c):pixels[y][x]=(*color,255)
    for left in (4,19):
        rect(left+1,10,left+8,21,ink)
        rect(left,12,left+9,19,ink)
        rect(left+1,12,left+8,20,light)
        rect(left+3,11,left+7,20,iris)
        rect(left+4,12,left+7,20,ink)
        rect(left+4,11,left+6,14,light)
    rect(3,10,6,12,ink);rect(26,10,29,12,ink)
    rect(5,6,9,8,ink);rect(9,7,12,9,ink)
    rect(20,6,24,8,ink);rect(24,5,27,7,ink)
    rect(12,26,19,28,warm);rect(18,25,21,27,warm)
    raw=bytes(c for row in pixels for pixel in row for c in pixel)
    name='face_wt_amy_'+hashlib.sha256(raw).hexdigest()[:4]
    root=Path(ctx.get('ASSET_ROOT',Path(ctx['ROOT'])/'assets'))
    path=root/'textures'/(name+'.png');path.parent.mkdir(parents=True,exist_ok=True)
    ctx['write_png'](path,32,32,[bytes(c for pixel in row for c in pixel) for row in pixels])
    image=bpy.data.images.load(str(path),check_existing=False);image.name=name;image.pack()
    mat=ctx['material']('face',(1,1,1),image)
    mat['role']='face';mat['native_shade_floor']=.86
    mat['water_face_style']='amy';mat['water_face_sha256']=hashlib.sha256(raw).hexdigest()
    return mat


def _amy_head(ctx,face):
    """One closed head/hair shell with an actual broad bob-shaped nape.

    The common owning surface is reshaped at the rear and temples, rather
    than adding sheet locks on top of a complete skull or a swimming cap.
    Front chin/nose and the big eyes remain visible in side views.
    """
    from water_cast_common import head_geometry,mesh
    width=.86;depth=.72;segments=16
    verts,faces,mats,uvs=head_geometry(width=width,depth=depth,bottom=1.45,top=2.28,
         face_low=1.53,face_high=2.03,hair='bob',nose=.105,sweep=.18,segments=segments)
    for row,(wide,deep) in enumerate(((.88,.92),(.98,1.0))):
        for i in range(segments):
            theta=-math.pi+i*math.tau/segments
            if abs(theta)<math.pi*3/8-1e-6:continue
            x,y,z=verts[row*segments+i]
            verts[row*segments+i]=(width*.5*wide*math.sin(theta),
                                    depth*.5*deep*math.cos(theta),z)
    # Temple locks are the owning head side, never independent jagged patches.
    for i in range(segments):
        theta=-math.pi+(i+.5)*math.tau/segments
        if abs(theta)>math.pi*3/8:
            mats[i]=2  # underside of the bob, not exposed skin under blue hair
            for row in range(4):mats[segments+row*segments+i]=2
    for material,floor in ((ctx['skin'],.86),(face,.86),(ctx['accent'],.78)):
        material['native_shade_floor']=floor
    obj=mesh(ctx,'Amy swept bob head',verts,faces,[ctx['skin'],face,ctx['accent']],
             face_material_indices=mats,uvs=uvs,head_component=True)
    obj['water_head_style']='amy swept bob'
    obj['water_face_wrap_degrees']=120
    obj['water_face_owning_degrees']=135
    obj['water_feature_height']=.80
    return obj


def apply(ctx):
    """Replace Amy's generic parts before exterior/skin/export processing."""
    if ctx.get('slug')!='amy':return False
    from water_cast_common import remove_sources
    # Keep only the exact legacy shadow signature for the established runtime
    # stripping pass. No new shadow geometry is added by this recipe.
    remove_sources(ctx,lambda obj:'shadow' not in obj.name.lower())
    ctx['textures'].clear()
    _body(ctx)
    _amy_head(ctx,_amy_face(ctx))
    return True
