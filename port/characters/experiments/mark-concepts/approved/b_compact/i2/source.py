#!/usr/bin/env python3
"""Build compact, lean, and chamfered sheets as independent native meshes."""
import copy,json,math,shutil,subprocess,sys
from pathlib import Path
HERE=Path(__file__).resolve().parent;STUDY=HERE.parent
sys.path.insert(0,str(STUDY.parent/'n64-style/tools'))
import builder as b
from geom import Mesh,lathe,loft,extrude,ellipsoid,box,dump_json,write_l643,Lighting
from paint import Canvas
from audit_exports import audit
ROUND=int(sys.argv[1]);assert ROUND in (1,2,3)
CONCEPT=sys.argv[2] if len(sys.argv)>2 else 'a_adventure'
assert CONCEPT in ('b_compact','c_lean','d_angular')
OUT=STUDY/'approved'/CONCEPT/f'i{ROUND}'
assert not OUT.exists(),OUT
s=copy.deepcopy(b.STYLES['b_pokemon']);s.id=f'{CONCEPT[0]}x{ROUND}';s.smooth=True;s.light=Lighting(.78,.22,(-.3,.6,.75));s.limb_segs=6;s.torso_segs=8
s.head_center=(0,1.84);s.head_r=(.37,.29,.36)
s.arm_pivot=(.36,1.36);s.arm=dict(len=.61,r_top=.11,r_bot=.085,hand=(.105,.10,.12),tilt=7)
s.leg_pivot=(.165,.79);s.leg=dict(r_top=.145,r_bot=.15,shoe=(.40,.30,.17))
s.neck=None
s.torso=[(.76,.29,.19),(1.0,.265,.18),(1.34,.32,.20),(1.47,.25,.17),(1.50,.12,.10)]
d=copy.deepcopy(b.DESIGNS['mark']);d.top_rgb=(.90,.10,.18);d.shirt_rgb=(.11,.10,.12)
if CONCEPT=='b_compact':
    s.arm_pivot=(.345,1.17);s.arm['len']=.49
    s.torso=[(.58,.30,.21),(.88,.30,.21),(1.17,.31,.22),(1.30,.24,.18),(1.33,.12,.10)]
    s.leg_pivot=(.165,.61)
    s.leg=dict(r_top=.15,r_bot=.14,shoe=(.36,.29,.16))
elif CONCEPT=='c_lean':
    s.arm_pivot=(.285,1.42);s.arm['len']=.68
    s.torso=[(.83,.245,.17),(1.13,.23,.16),(1.40,.28,.18),(1.51,.21,.15),(1.55,.11,.10)]
    s.leg_pivot=(.145,.91);s.leg=dict(r_top=.12,r_bot=.115,shoe=(.36,.255,.15))
elif CONCEPT=='d_angular':
    s.smooth=False;s.arm_pivot=(.365,1.37)
    s.torso=[(.76,.28,.19),(1.02,.27,.18),(1.35,.33,.21),(1.48,.24,.17),(1.51,.12,.10)]
if CONCEPT=='a_adventure' and ROUND>=2:
    s.arm_pivot=(.342,1.36)
    s.torso=[(z,rx*.95,ry) for z,rx,ry in s.torso]
fig=b.Figure(s,d)
skin=fig.skin;face=b.mat('face',(1,1,1),f'{fig.prefix}_face');hair=b.mat('hair',(.065,.055,.065));red=b.mat('cap',(.92,.10,.18));captex=b.mat('cap_front',(1,1,1),f'{fig.prefix}_cap')
# One owning shell: the red crown continues the forehead without crossing surfaces.
# Project UVs onto the front plane, preserving face feature width across facets.
rings=[(1.49,.15,.17,0,0),(1.57,.285,.23,0,0),(1.77,.37,.285,0,0),
       (2.01,.375,.28,0,0),(2.13,.31,.235,0,0),(2.21,.16,.12,0,0),(2.235,0,0,0,0)]
if CONCEPT=='a_adventure' and ROUND>=2:
    rings=[(1.46,.19,.18,0,0),(1.58,.27,.23,0,0),(1.77,.37,.285,0,0),(1.99,.375,.28,0,0),(2.12,.31,.235,0,0),(2.21,.16,.12,0,0),(2.235,0,0,0,0)]
elif CONCEPT=='b_compact':
    rings=[(1.30,.23,.19,0,0),(1.40,.36,.27,0,0),(1.67,.445,.33,0,0),(1.99,.44,.32,0,0),(2.12,.36,.27,0,0),(2.21,.19,.15,0,0),(2.235,0,0,0,0)]
elif CONCEPT=='c_lean':
    rings=[(1.55,.115,.16,0,0),(1.65,.225,.22,0,0),(1.83,.32,.265,0,0),(2.02,.34,.26,0,0),(2.14,.28,.22,0,0),(2.21,.145,.11,0,0),(2.235,0,0,0,0)]
elif CONCEPT=='d_angular':
    rings=[(1.51,.13,.18,0,0),(1.61,.285,.23,0,0),(1.80,.385,.28,0,0),(2.01,.38,.28,0,0),(2.14,.31,.23,0,0),(2.21,.16,.12,0,0),(2.235,0,0,0,0)]
FACE_TOP=rings[3][0];FACE_BOTTOM=rings[0][0];FACE_WIDTH=rings[2][1]
if ROUND>=2:
    # Rounded cranium; projection remains UV-only. Hairline is a ring of the owning shell.
    rings=[(z,rx,ry*1.18,cx,cy) for z,rx,ry,cx,cy in rings]
    rings.insert(3,(FACE_TOP-.10,FACE_WIDTH,rings[2][2],0,0))
head=lathe(rings,8 if CONCEPT=='d_angular' else 12,skin,phase=0,smooth=s.smooth,name='head cap ears nose')
for i,(x,y,z) in enumerate(head.verts):
    # Ears and small nose are contiguous vertices of the head, not loose pieces.
    if abs(z-rings[2][0])<.001:
        if abs(x)>FACE_WIDTH*.97:x+=math.copysign(.045,x)
        if abs(x)<.01 and y>0:y+=.045
    # Broad frontal cheek plane from the reference.
    if ROUND==1 and y>0 and rings[2][0]<=z<=FACE_TOP:y=max(y,rings[2][2]*.86)
    if ROUND>=2:
        n=8 if CONCEPT=='d_angular' else 12
        ri=i//n;angle=(i%n)*360/n;aa=min(angle,360-angle)
        if ri==2 and aa>=90:z=rings[0][0]+.10
        if ri==3:
            if aa<80:z=FACE_TOP-({0:.19,30:.055,60:.13}.get(round(aa/30)*30,.10))
            elif aa<=90:z=FACE_TOP-.12
            else:z=rings[0][0]+.20
        if abs(x)<.01 and y>0 and ri==2:y+=.04
    head.verts[i]=(x,y,z)
for f in head.faces:
    c=head.face_center(f)
    n=8 if CONCEPT=='d_angular' else 12
    row=min(f['idx'])//n
    if ROUND>=2 and row==3:
        f['mat']=hair
    elif c[2]>FACE_TOP:
        f['mat']=captex if c[1]>.13 else red
        if f['mat']==captex:
            f['uv']=[(max(0,min(1,(head.verts[i][0]+.38)/.76)),max(0,min(1,(2.235-head.verts[i][2])/.225))) for i in f['idx']]
    elif c[1]>.10:
        f['mat']=face
        f['uv']=[(max(0,min(1,(head.verts[i][0]+FACE_WIDTH)/(2*FACE_WIDTH))),max(0,min(1,(FACE_TOP-head.verts[i][2])/(FACE_TOP-FACE_BOTTOM)))) for i in f['idx']]
    elif c[2]>FACE_BOTTOM+.15 and (c[2]>FACE_BOTTOM+.25 or abs(c[0])>FACE_WIDTH*.35):f['mat']=hair
if CONCEPT=='a_adventure' and ROUND>=2:head.scale(1.13,1.1,1)
fig.add(head)
# Single connected fringe ribbon with three broad locks, sunk into the temples.
# Top edge is one continuous band beneath the cap; no individual floating spikes.
xs=[-.355,-.30,-.20,-.115,0,.10,.20,.285,.355]
if CONCEPT=='b_compact':xs=[x*1.18 for x in xs]
if CONCEPT=='c_lean':xs=[x*.90 for x in xs]
zs=[FACE_TOP-.07,FACE_TOP-.155,FACE_TOP-.02,FACE_TOP-.11,FACE_TOP-.02,FACE_TOP-.165,FACE_TOP-.02,FACE_TOP-.12,FACE_TOP-.06]
if CONCEPT=='d_angular':zs=[1.96,1.84,1.99,1.89,1.99,1.81,1.99,1.90,1.96]
fringe=Mesh('joined three-lock fringe')
for x in xs:fringe.add_vert((x,.275+(.012 if abs(x)<.25 else -.015),FACE_TOP+.015))
for x,z in zip(xs,zs):fringe.add_vert((x,.287+(.014 if abs(x)<.25 else -.02),z))
for k in range(8):fringe.add_face([k,k+1,9+k+1,9+k],hair,group=Mesh.new_group())
# Close the ribbon thickness against the face; broad roots are hidden by crown.
back=fringe.copy();back.translate(dy=-.035)
for f in back.faces:f['idx'].reverse()
fringe.merge(back)
for k in range(8):
    fringe.add_face([9+k,9+k+1,27+k+1,27+k],hair)
if CONCEPT=='a_adventure' and ROUND>=2:fringe.scale(1.13,1.1,1)
if ROUND==1:fig.add(fringe)
# Short, centered backward bill. Thick enough to survive 1/32 packing.
fig.add(extrude([(-.22,-.20),(-.22,-.40),(-.13,-.48),(.13,-.48),(.22,-.40),(.22,-.20)],FACE_TOP-.035,FACE_TOP+.035,red,name='backward bill'))
if ROUND==1:fig.torso(s.torso,f'{fig.prefix}_torso')
else:fig.add(lathe([(z,rx,ry,0,0) for z,rx,ry in s.torso],8,b.mat('solid red shirt',d.top_rgb),smooth=s.smooth,name='solid shirt'))
# Coarse limb shells avoid the prior subpixel ring details.
for side in (-1,1):
    px,pz=s.arm_pivot
    if ROUND>=2:px-=.025
    x=side*px;bottom=pz-s.arm['len'];cut=pz-s.arm['len']*.4
    arm=lathe([(bottom,.085,.085,x,0),(cut,.105,.105,x,0),(pz+.035,.15 if ROUND>=2 else .12,.14 if ROUND>=2 else .12,x,0)],6,b.mat('sleeve',d.top_rgb if ROUND>=2 else d.sleeve_rgb),phase=0,smooth=True,name='arm mitten')
    arm.remap_material(lambda c,m:skin if c[2]<cut else m)
    arm.merge(ellipsoid((x,.015,bottom-.065),(.11,.105,.12),6,2,skin,True,'hand'))
    arm.rotate_y(-side*7,about=(x,pz));fig.add(arm,3 if side<0 else 4,(x,0,pz))
    x=side*s.leg_pivot[0];hip=s.leg_pivot[1];radius=s.leg['r_top']
    leg=lathe([(.15,s.leg['r_bot'],s.leg['r_bot']*1.08,x,0),(hip*.50,radius*.86,radius*.90,x,0),(hip+.05,radius,radius*1.07,x,0)],6,b.mat('trousers',d.trousers_rgb),phase=0,smooth=True,name='loose trousers shoe')
    # Shoe rounded with one octagonal bevel at a real 0.07-unit scale.
    shoe=loft([[(x+dx,y,z) for dx,y in [(-.13,-.13),(.13,-.13),(.155,-.06),(.155,.20),(.10,.25),(-.10,.25),(-.155,.20),(-.155,-.06)]] for z in (.0,.075)],b.mat('sole',(.12,.12,.14)),smooth=False,cap_top=False,name='sole')
    shoe.merge(loft([[(x+dx,y,z) for dx,y in [(-.13,-.13),(.13,-.13),(.155,-.06),(.155,.20),(.10,.25),(-.10,.25),(-.155,.20),(-.155,-.06)]] for z in (.075,.17)],b.mat('shoe',(.95,.95,.92)),smooth=True,cap_bottom=False,name='white shoe'))
    leg.merge(shoe);fig.add(leg,1 if side<0 else 2,(x,0,hip))
# One compact backpack shell; straps stay painted into the garment.
fig.add(box((0,-.27,(s.torso[0][0]+s.torso[-1][0])/2),(.42 if CONCEPT=='b_compact' else .36 if CONCEPT=='c_lean' else .40,.23,.43),b.mat('pack',(.44,.28,.12)),name='pack'))
fig.shadow()
# 32px face places 12px-tall eyes centrally; no tiny high-eye design is reused.
c=Canvas();white=(247,239,220);ink=(29,28,34);iris=(47,76,106)
for cx in (9,23):
    c.poly([(cx-4,10),(cx-2,8),(cx+2,8),(cx+4,11),(cx+4,20),(cx+2,22),(cx-3,22),(cx-4,19)],white)
    c.rect(cx-1,10,cx+(4 if ROUND>=2 else 3),21,iris);c.rect(cx,10,cx+(4 if ROUND>=2 else 3),18,ink)
    c.rect(cx,10,cx+2,13,white);c.px(cx+2,19,(101,134,163))
    c.line([(cx-4,9),(cx-2,7),(cx+2,7),(cx+4,10)],ink,1)
    c.line([(cx-4,5),(cx+3,4 if CONCEPT=='b_compact' else 3)],ink,2)
c.line([(13,26),(15,27),(18,27),(20,26)],(105,58,34),1)
c.px(16,22,(198,132,69))
fig.textures[f'{fig.prefix}_face']=c
costume=b.paint_torso(s,d,.74)
costume.poly([(10,0),(14,0),(16,3)],(240,238,228));costume.poly([(18,0),(22,0),(16,3)],(240,238,228))
if ROUND>=2:
    costume.poly([(10,0),(14,0),(16,3)],(240,238,228));costume.poly([(18,0),(22,0),(16,3)],(240,238,228))
if ROUND==1:fig.textures[f'{fig.prefix}_torso']=costume
cap=Canvas(background=(231,25,49));cap.rect(14,17,18,24,(238,235,219));fig.textures[f'{fig.prefix}_cap']=cap
# Triangulate n-gon caps from their true center, avoiding collinear fan roots.
for mesh in fig.meshes:
    faces=[]
    for f in mesh.faces:
        if len(f['idx'])<=4:faces.append(f);continue
        ids=f['idx'];center=tuple(sum(mesh.verts[i][k] for i in ids)/len(ids) for k in range(3));ci=mesh.add_vert(center)
        uvcenter=tuple(sum(uv[k] for uv in f['uv'])/len(ids) for k in range(2)) if f['uv'] else None
        for k in range(len(ids)):
            faces.append(dict(idx=[ci,ids[k],ids[(k+1)%len(ids)]],mat=f['mat'],group=f['group'],uv=[uvcenter,f['uv'][k],f['uv'][(k+1)%len(ids)]] if f['uv'] else None))
    mesh.faces=faces
(OUT/'mark/textures').mkdir(parents=True)
batches,textures=fig.finish();stats=write_l643(OUT/'mark/scene.l64',batches,textures)
for name in textures:fig.textures[name].save(OUT/'mark/textures'/f'{name}.png')
dump_json(OUT/'mark/authoring.json',fig.meshes,dict(names=textures))
(OUT/'mark/scene.json').write_text(json.dumps(dict(format='L643',parts=list(range(5)),textures=textures,**stats),indent=2)+'\n')
report=audit(OUT/'mark/scene.l64');(OUT/'audit.json').write_text(json.dumps(report,indent=2)+'\n')
# Supporting actors are production controls.
root=STUDY.parents[3];generated=root/'port/world3d/generated'
for slug in ('dr_mason','ronald'):
    actor=OUT/slug;(actor/'textures').mkdir(parents=True)
    raw=(generated/'maps'/f'actor_{slug}.l64').read_bytes();(actor/'scene.l64').write_bytes(raw)
    import struct
    _,_,nt=struct.unpack_from('>4sHH',raw)
    for k in range(nt):
        name=raw[8+32*k:40+32*k].split(b'\0')[0].decode().replace('.sprite','.png');shutil.copy2(generated/'textures'/name,actor/'textures'/name)
(OUT/'option.json').write_text(json.dumps(dict(id=f'{CONCEPT}_i{ROUND}',label=f'{CONCEPT[0].upper()} {ROUND}',concept_reference=f'../../../concepts/{CONCEPT.replace("_","-")}.png',method='approved raster reference; image-led 3D modeling',iteration=ROUND),indent=2)+'\n')
shutil.copy2(__file__,OUT/'source.py')
print(json.dumps(report,indent=2))
