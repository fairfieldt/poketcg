#!/usr/bin/env python3
"""Model the approved adventure sheet, retaining image-led mesh revisions."""
import copy,json,math,shutil,subprocess,sys
from pathlib import Path
HERE=Path(__file__).resolve().parent;STUDY=HERE.parent
sys.path.insert(0,str(STUDY.parent/'n64-style/tools'))
import builder as b
from geom import Mesh,lathe,loft,extrude,ellipsoid,box,dump_json,write_l643,Lighting
from paint import Canvas
from audit_exports import audit
ROUND=int(sys.argv[1]);assert ROUND in (1,2,3)
OUT=STUDY/'approved/a_adventure'/f'i{ROUND}'
assert not OUT.exists(),OUT
s=copy.deepcopy(b.STYLES['b_pokemon']);s.id=f'aa{ROUND}';s.smooth=True;s.light=Lighting(.78,.22,(-.3,.6,.75));s.limb_segs=6;s.torso_segs=8
s.head_center=(0,1.84);s.head_r=(.37,.29,.36)
s.arm_pivot=(.36,1.36);s.arm=dict(len=.61,r_top=.11,r_bot=.085,hand=(.105,.10,.12),tilt=7)
s.leg_pivot=(.165,.79);s.leg=dict(r_top=.145,r_bot=.15,shoe=(.40,.30,.17))
s.neck=None
s.torso=[(.76,.29,.19),(1.0,.265,.18),(1.34,.32,.20),(1.47,.25,.17),(1.50,.12,.10)]
d=copy.deepcopy(b.DESIGNS['mark']);d.top_rgb=(.90,.10,.18);d.shirt_rgb=(.11,.10,.12)
fig=b.Figure(s,d)
skin=fig.skin;face=b.mat('face',(1,1,1),f'{fig.prefix}_face');hair=b.mat('hair',(.065,.055,.065));red=b.mat('cap',(.92,.10,.18));captex=b.mat('cap_front',(1,1,1),f'{fig.prefix}_cap')
# One owning shell: the red crown continues the forehead without crossing surfaces.
# Project UVs onto the front plane, preserving face feature width across facets.
rings=[(1.49,.15,.17,0,0),(1.57,.285,.23,0,0),(1.77,.37,.285,0,0),
       (2.01,.375,.28,0,0),(2.13,.31,.235,0,0),(2.21,.16,.12,0,0),(2.235,0,0,0,0)]
head=lathe(rings,12,skin,phase=0,smooth=True,name='head cap ears nose')
for i,(x,y,z) in enumerate(head.verts):
    # Ears and small nose are contiguous vertices of the head, not loose pieces.
    if abs(z-1.77)<.001:
        if abs(x)>.36:x+=math.copysign(.045,x)
        if abs(x)<.01 and y>0:y+=.045
    # Broad frontal cheek plane from the reference.
    if y>0 and 1.77<=z<=2.01:y=max(y,.245)
    head.verts[i]=(x,y,z)
for f in head.faces:
    c=head.face_center(f)
    if c[2]>2.01:
        f['mat']=captex if c[1]>.13 else red
        if f['mat']==captex:
            f['uv']=[(max(0,min(1,(head.verts[i][0]+.38)/.76)),max(0,min(1,(2.235-head.verts[i][2])/.225))) for i in f['idx']]
    elif c[1]>.10:
        f['mat']=face
        f['uv']=[(max(0,min(1,(head.verts[i][0]+.37)/.74)),max(0,min(1,(2.01-head.verts[i][2])/.52))) for i in f['idx']]
    elif c[2]>1.65:f['mat']=hair
fig.add(head)
# Single connected fringe ribbon with three broad locks, sunk into the temples.
# Top edge is one continuous band beneath the cap; no individual floating spikes.
xs=[-.355,-.30,-.20,-.115,0,.10,.20,.285,.355]
zs=[1.94,1.855,1.99,1.90,1.99,1.845,1.99,1.89,1.95]
fringe=Mesh('joined three-lock fringe')
for x in xs:fringe.add_vert((x,.275+(.012 if abs(x)<.25 else -.015),2.025))
for x,z in zip(xs,zs):fringe.add_vert((x,.287+(.014 if abs(x)<.25 else -.02),z))
for k in range(8):fringe.add_face([k,k+1,9+k+1,9+k],hair,group=Mesh.new_group())
# Close the ribbon thickness against the face; broad roots are hidden by crown.
back=fringe.copy();back.translate(dy=-.035)
for f in back.faces:f['idx'].reverse()
fringe.merge(back)
for k in range(8):
    fringe.add_face([9+k,9+k+1,27+k+1,27+k],hair)
fig.add(fringe)
# Short, centered backward bill. Thick enough to survive 1/32 packing.
fig.add(extrude([(-.22,-.20),(-.22,-.40),(-.13,-.48),(.13,-.48),(.22,-.40),(.22,-.20)],1.975,2.045,red,name='backward bill'))
fig.torso(s.torso,f'{fig.prefix}_torso')
# Coarse limb shells avoid the prior subpixel ring details.
for side in (-1,1):
    px,pz=s.arm_pivot;x=side*px;bottom=pz-.61;cut=pz-.24
    arm=lathe([(bottom,.085,.085,x,0),(cut,.105,.105,x,0),(pz+.035,.12,.12,x,0)],6,b.mat('sleeve',d.sleeve_rgb),phase=0,smooth=True,name='arm mitten')
    arm.remap_material(lambda c,m:skin if c[2]<cut else m)
    arm.merge(ellipsoid((x,.015,bottom-.065),(.11,.105,.12),6,2,skin,True,'hand'))
    arm.rotate_y(-side*7,about=(x,pz));fig.add(arm,3 if side<0 else 4,(x,0,pz))
    x=side*.165
    leg=lathe([(.15,.145,.16,x,0),(.40,.125,.13,x,0),(.84,.145,.155,x,0)],6,b.mat('trousers',d.trousers_rgb),phase=0,smooth=True,name='loose trousers shoe')
    # Shoe rounded with one octagonal bevel at a real 0.07-unit scale.
    shoe=loft([[(x+dx,y,z) for dx,y in [(-.13,-.13),(.13,-.13),(.155,-.06),(.155,.20),(.10,.25),(-.10,.25),(-.155,.20),(-.155,-.06)]] for z in (.0,.075)],b.mat('sole',(.12,.12,.14)),smooth=False,cap_top=False,name='sole')
    shoe.merge(loft([[(x+dx,y,z) for dx,y in [(-.13,-.13),(.13,-.13),(.155,-.06),(.155,.20),(.10,.25),(-.10,.25),(-.155,.20),(-.155,-.06)]] for z in (.075,.17)],b.mat('shoe',(.95,.95,.92)),smooth=True,cap_bottom=False,name='white shoe'))
    leg.merge(shoe);fig.add(leg,1 if side<0 else 2,(x,0,.79))
# One compact backpack shell; straps stay painted into the garment.
fig.add(box((0,-.27,1.13),(.40,.23,.43),b.mat('pack',(.44,.28,.12)),name='pack'))
fig.shadow()
# 32px face places 12px-tall eyes centrally; no tiny high-eye design is reused.
c=Canvas();white=(247,239,220);ink=(29,28,34);iris=(47,76,106)
for cx in (9,23):
    c.poly([(cx-4,10),(cx-2,8),(cx+2,8),(cx+4,11),(cx+4,20),(cx+2,22),(cx-3,22),(cx-4,19)],white)
    c.rect(cx-1,10,cx+3,21,iris);c.rect(cx,10,cx+3,18,ink)
    c.rect(cx,10,cx+2,13,white);c.px(cx+2,19,(101,134,163))
    c.line([(cx-4,9),(cx-2,7),(cx+2,7),(cx+4,10)],ink,1)
    c.line([(cx-4,5),(cx+3,4)],ink,2)
c.line([(13,26),(15,27),(18,27),(20,26)],(105,58,34),1)
c.px(16,22,(198,132,69))
fig.textures[f'{fig.prefix}_face']=c
costume=b.paint_torso(s,d,.74);fig.textures[f'{fig.prefix}_torso']=costume
cap=Canvas(background=(231,25,49));cap.rect(14,17,20,32,(23,21,24));cap.rect(14,24,20,32,(206,199,180));fig.textures[f'{fig.prefix}_cap']=cap
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
(OUT/'option.json').write_text(json.dumps(dict(id=f'a_approved_i{ROUND}',label=f'A {ROUND}',concept_reference='../../../concepts/a-adventure.png',method='approved raster reference; image-led 3D modeling',iteration=ROUND),indent=2)+'\n')
shutil.copy2(__file__,OUT/'source.py')
print(json.dumps(report,indent=2))
