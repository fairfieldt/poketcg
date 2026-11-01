"""Mark's broad trainer silhouette and a rounded, readable owning head."""
import math


def _loft(ctx,name,profiles,materials,bands=None,*,part=0,pivot=(0,0,0),segments=8):
    from water_cast_common import mesh
    vertices=[(cx+rx*math.sin((i+.5)*math.tau/segments),
               cy+ry*math.cos((i+.5)*math.tau/segments),z)
              for z,rx,ry,cx,cy in profiles for i in range(segments)]
    faces=[];slots=[]
    # Explicit centre fans survive the native packed coordinate grid.
    vertices.extend([(profiles[0][3],profiles[0][4],profiles[0][0]),
                     (profiles[-1][3],profiles[-1][4],profiles[-1][0])])
    bottom=len(vertices)-2;top=len(vertices)-1
    for i in range(segments):
        faces.append((bottom,i,(i+1)%segments));slots.append(0)
    for row in range(len(profiles)-1):
        for i in range(segments):
            a=row*segments+i;b=row*segments+(i+1)%segments
            faces.append((a+segments,b+segments,b,a))
            slots.append(bands[row] if bands else 0)
    offset=(len(profiles)-1)*segments
    for i in range(segments):
        faces.append((top,offset+(i+1)%segments,offset+i));slots.append(0)
    return mesh(ctx,name,vertices,faces,materials,slots,part=part,pivot=pivot)


def apply(ctx):
    from water_cast_common import remove_sources,head,mesh
    face=next(slot.material for obj in ctx['sources'] for slot in obj.material_slots
              if slot.material and slot.material.get('role')=='face')
    face_name=ctx['textures'][int(face['texture_id'])]
    remove_sources(ctx,lambda obj:True)
    ctx['textures'][:]=[face_name];face['texture_id']=0
    skin=ctx['skin'];white=ctx['white'];ink=ctx['ink'];red=ctx['accent']
    blue=ctx.get('trainer_blue') or ctx['material']('trainer blue',(.08,.22,.62))
    for mat in (white,red,blue):mat['native_shade_floor']=.79
    ink['native_shade_floor']=.80
    # One closed torso owns the red vest, black shirt and white placket.
    profiles=[(.72,.28,.22,0,0),(.81,.30,.23,0,0),
              (1.05,.29,.25,0,0),(1.29,.34,.27,0,-.01),
              (1.40,.30,.24,0,-.01),(1.47,.20,.16,0,0)]
    torso=_loft(ctx,'Mark trainer vest',profiles,[ink,red,white],segments=12)
    offset=12
    for poly in torso.data.polygons:
        if not offset<=poly.index<offset+(len(profiles)-1)*12:continue
        row,angular=divmod(poly.index-offset,12)
        theta=(angular+1)*math.tau/12
        front=math.cos(theta)>.80
        poly.material_index=0 if row==0 else 2 if front else 1
    _loft(ctx,'Mark neck',[(1.42,.11,.115,0,0),(1.56,.13,.13,0,0)],
          [skin],segments=8)
    for side in (-1,1):
        # Broad blue trouser legs own the shoes/soles in the same surface.
        _loft(ctx,'Mark leg '+str(side),[
            (.015,.14,.22,side*.17,.07),(.075,.14,.22,side*.17,.07),
            (.16,.13,.21,side*.17,.06),(.22,.115,.15,side*.17,.01),
            (.48,.12,.16,side*.17,0),(.72,.15,.17,side*.17,0)],
            [ink,white,blue],[0,1,2,2,2],
            part=1 if side<0 else 2,pivot=(side*.17,0,.72))
        # A relaxed elbow/rounded hand supplies a full profile silhouette.
        _loft(ctx,'Mark arm '+str(side),[
            (.73,.085,.10,side*.49,.06),(.81,.115,.125,side*.49,.05),
            (.91,.080,.095,side*.47,.025),(1.10,.10,.12,side*.43,0),
            (1.23,.13,.15,side*.39,-.01),(1.39,.125,.15,side*.355,-.01)],
            [skin,ink],[0,0,0,1,1],part=3 if side<0 else 4,
            pivot=(side*.40,0,1.36))
    pack=ctx['material']('Mark backpack',(.37,.25,.16))
    pack['native_shade_floor']=.78
    _loft(ctx,'Mark rounded backpack',[(.83,.17,.10,0,-.27),
          (.91,.23,.14,0,-.28),(1.27,.23,.14,0,-.28),
          (1.36,.16,.09,0,-.27)],[pack])
    # Keep Mark's already approved original eye/brow/grin design, over a much
    # larger curved facial region with a real nose, cheeks and jaw.
    obj=head(ctx,style='mark',palette={'skin':skin,'face':face,'hair':red},
             width=.88,depth=.78,bottom=1.44,top=2.29,
             face_low=1.53,face_high=2.05,nose=.12,hair='cap',sweep=.035)
    obj.data.materials.append(ctx['hair_mat'])
    hair_index=len(obj.data.materials)-1
    obj.material_slots[-1].link='OBJECT';obj.material_slots[-1].material=ctx['hair_mat']
    obj.data.materials.append(white)
    badge_index=len(obj.data.materials)-1
    obj.material_slots[-1].link='OBJECT';obj.material_slots[-1].material=white
    ctx['hair_mat']['native_shade_floor']=.80
    n=int(obj['water_head_segments']);start=int(obj['water_head_side_offset'])
    rows=int(obj['water_head_side_rows'])
    for poly in obj.data.polygons:
        if not start<=poly.index<start+n*rows:continue
        row,angular=divmod(poly.index-start,n)
        theta=-math.pi+(angular+.5)*math.tau/n
        # Dark nape/temples terminate at the cap line instead of a second scalp.
        if row<=3 and abs(theta)>math.pi*.5:poly.material_index=hair_index
        if row==4 and abs(theta)<math.pi/8:poly.material_index=badge_index
    # A small rear visor keeps the eyes completely open to the game camera.
    top=[(-.25,-.20,2.11),(.25,-.20,2.11),(.25,-.44,2.10),
         (.16,-.55,2.08),(-.16,-.55,2.08),(-.25,-.44,2.10)]
    bottom=[(x,y,z-.07) for x,y,z in top];vertices=top+bottom
    faces=[tuple(reversed(range(6))),tuple(range(6,12))]
    faces += [(i,(i+1)%6,(i+1)%6+6,i+6) for i in range(6)]
    mesh(ctx,'Mark rear cap visor',vertices,faces,[red],head_component=True)
    return True
