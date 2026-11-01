"""Joshua: a cheerful pool trainer with swim cap and forehead goggles.

The disassembly portrait shows a brimless cap, goggles and short dark nape.
A round, deep torso and continuously skinned limbs replace the generic sailor
collar and flat box cap. Five rigid groups retain the adventure animation rig.
"""
import math
from pathlib import Path


def _loft(ctx,name,profiles,materials,bands,*,part=0,pivot=(0,0,0),segments=8):
    from water_cast_common import mesh
    points=[(cx+rx*math.sin((i+.5)*math.tau/segments),
             cy+ry*math.cos((i+.5)*math.tau/segments),z)
            for z,rx,ry,cx,cy in profiles for i in range(segments)]
    bottom=len(points);points.append((profiles[0][3],profiles[0][4],profiles[0][0]))
    top=len(points);points.append((profiles[-1][3],profiles[-1][4],profiles[-1][0]))
    faces=[];indices=[]
    for i in range(segments):
        faces.append((bottom,i,(i+1)%segments));indices.append(bands[0])
    for row in range(len(profiles)-1):
        for i in range(segments):
            a=row*segments+i;b=row*segments+(i+1)%segments
            faces.append((a+segments,b+segments,b,a));indices.append(bands[row])
    for i in range(segments):
        faces.append((top,(len(profiles)-1)*segments+(i+1)%segments,
                      (len(profiles)-1)*segments+i));indices.append(bands[-1])
    return mesh(ctx,name,points,faces,materials,indices,part=part,pivot=pivot)


def _body(ctx):
    skin,white,accent,ink=(ctx[key] for key in ('skin','white','accent','ink'))
    # A fitted sleeveless white top, blue hem and neckline are bands of one
    # closed body, not a complete white torso hidden by decorative planes.
    _loft(ctx,'Joshua sleeveless pool top',[
        (.70,.31,.245,0,0),(.78,.31,.245,0,0),
        (.96,.295,.235,0,0),(1.19,.345,.275,0,.005),
        (1.37,.33,.245,0,0),(1.43,.26,.19,0,0),
        (1.48,.135,.13,0,0),(1.57,.12,.12,0,0)],
        [white,accent,skin],[1,0,0,0,0,1,2],segments=10)
    for side in (-1,1):
        shoulder=(side*.40,0,1.36)
        _loft(ctx,'Joshua left arm' if side<0 else 'Joshua right arm',[
            (.70,.065,.085,side*.46,.06),(.78,.105,.12,side*.46,.065),
            (.88,.080,.095,side*.45,.03),(1.08,.092,.112,side*.41,0),
            (1.29,.112,.13,side*.36,-.005),(1.42,.085,.11,side*.325,0)],
            [skin],[0,0,0,0,0],part=3 if side<0 else 4,pivot=shoulder)
        hip=(side*.17,0,.72)
        _loft(ctx,'Joshua left leg' if side<0 else 'Joshua right leg',[
            (.025,.12,.19,side*.17,.06),(.085,.13,.22,side*.17,.075),
            (.17,.12,.22,side*.17,.075),(.23,.088,.115,side*.17,0),
            (.42,.10,.13,side*.17,0),(.58,.12,.14,side*.17,0),
            (.63,.145,.17,side*.17,0),(.77,.145,.17,side*.17,0),
            (.82,.10,.11,side*.17,0)],
            [ink,white,skin,accent],[0,1,1,2,2,2,3,3],
            part=1 if side<0 else 2,pivot=hip)


def _goggles(ctx):
    from water_cast_common import mesh
    from parts import box
    rim=ctx['ink'];lens=ctx['material']('Joshua blue goggle glass',(.48,.70,.76))
    rim['native_shade_floor']=.79;lens['native_shade_floor']=.88
    for side in (-1,1):
        cx=side*.135;cy=.43;cz=2.16;n=6
        outer=[(cx+.115*math.cos(i*math.tau/n),cy,cz+.083*math.sin(i*math.tau/n)) for i in range(n)]
        inner=[(cx+.077*math.cos(i*math.tau/n),cy,cz+.050*math.sin(i*math.tau/n)) for i in range(n)]
        back=[(x,.27,z) for x,_,z in outer]
        points=outer+inner+back;faces=[];indices=[]
        for i in range(n):
            j=(i+1)%n
            # The rim and lens partition the front plane. Side and rear
            # winding is outward too; no complete dark disk hides the lens.
            faces.extend(((i,n+i,n+j,j),(i,j,2*n+j,2*n+i)))
            indices.extend((0,0))
        faces.extend((tuple(reversed(range(n,2*n))),tuple(range(2*n,3*n))))
        indices.extend((1,0))
        mesh(ctx,'Joshua forehead goggle '+str(side),points,faces,[rim,lens],indices,head_component=True)
    points,faces=box((.075,.09,.045))
    points=[(x,y+.35,z+2.16) for x,y,z in points]
    mesh(ctx,'Joshua goggle bridge',points,faces,[rim],head_component=True)


def _face(ctx):
    """Joshua's portrait smile: closed happy eyes and one broad open grin."""
    import bpy
    skin=tuple(round(v*255) for v in ctx['skin'].diffuse_color[:3])
    pixels=[[(*skin,255) for _ in range(32)] for _ in range(32)]
    ink=(30,31,35);cream=(246,241,216)
    def rect(a,b,c,d,color):
        for y in range(b,d):
            for x in range(a,c):pixels[y][x]=(*color,255)
    for left in (4,18):
        rect(left+1,7,left+10,9,ink)
        # Two-texel steps form an unambiguous upward-curved closed eye.
        rect(left,15,left+3,18,ink)
        rect(left+2,13,left+5,16,ink)
        rect(left+4,12,left+8,14,ink)
        rect(left+7,13,left+10,16,ink)
        rect(left+9,15,left+12,18,ink)
    rect(10,23,23,27,ink);rect(12,27,21,29,ink)
    rect(12,23,21,25,cream)
    asset=Path(ctx.get('ASSET_ROOT',Path(ctx['ROOT'])/'assets'))
    name='face_wt_jos_happy';path=asset/'textures'/(name+'.png')
    path.parent.mkdir(parents=True,exist_ok=True)
    ctx['write_png'](path,32,32,[bytes(c for pixel in row for c in pixel) for row in pixels])
    image=bpy.data.images.load(str(path),check_existing=False);image.name=name;image.pack()
    mat=ctx['material']('face',(1,1,1),image)
    mat['role']='face';mat['native_shade_floor']=.86
    mat['water_face_style']='joshua_happy'
    return mat


def apply(ctx):
    if ctx.get('slug')!='joshua':return False
    from water_cast_common import remove_sources,head,prune_textures
    remove_sources(ctx,lambda obj:True)
    _body(ctx)
    face=_face(ctx)
    obj=head(ctx,style='joshua',palette={'skin':'skin','hair':'white','face':face},
             width=.86,depth=.74,bottom=1.45,top=2.29,face_low=1.53,
             face_high=2.05,hair='cap',nose=.11,chin=.75)
    # A short dark nape and broad sideburns belong to the same head shell.
    # The smooth white swimming cap owns everything above those boundaries.
    obj.data.materials.append(ctx['hair_mat'])
    obj.material_slots[-1].link='OBJECT';obj.material_slots[-1].material=ctx['hair_mat']
    ctx['hair_mat']['native_shade_floor']=.78
    offset=int(obj['water_head_side_offset']);segments=int(obj['water_head_segments'])
    for poly in obj.data.polygons:
        if not offset<=poly.index<offset+int(obj['water_head_side_rows'])*segments:continue
        row,angular=divmod(poly.index-offset,segments)
        theta=-math.pi+(angular+.5)*math.tau/segments
        if row<=1 and abs(theta)>math.pi*.5:poly.material_index=3
        elif row==2 and math.pi*.375<abs(theta)<=math.pi*.5:poly.material_index=3
    _goggles(ctx)
    prune_textures(ctx)
    return True
