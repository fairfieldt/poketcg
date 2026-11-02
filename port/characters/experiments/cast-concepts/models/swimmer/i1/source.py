#!/usr/bin/env python3
"""Image-led water/civilian character recipes; each numbered round is built after capture review."""
import sys,json,copy,math,collections,shutil
from pathlib import Path
HERE=Path(__file__).resolve().parent; STUDY=HERE.parent
sys.path[:0]=[str(STUDY.parent/'n64-style/tools'),str(STUDY.parent/'mark-concepts/tools')]
import builder as b
from geom import lathe,loft,dump_json,write_l643,Lighting
from paint import Canvas,eye,mouth,glasses
from audit_exports import audit
# hairstyle, pigment, shirt, garment, height, width, cap
C={
'clerk':('bob',(.28,.66,.88),(.24,.63,.86),'skirt',1.,1.,'blue'),
'lass3':('twintails',(.14,.11,.10),(.25,.76,.20),'skirt',.91,.91,None),
'swimmer':('swimcap',(.11,.10,.12),(.12,.66,.86),'swim',.92,.90,'white'),
'joshua':('swimcap',(.10,.09,.10),(.93,.93,.86),'shorts',1.,1.10,'white'),
'amy':('swept',(.23,.61,.87),(.95,.94,.86),'minidress',.96,.92,None),
'girl':('long',(.10,.08,.10),(.20,.57,.86),'skirt',.90,.89,'blue'),
'lass2':('long',(.10,.08,.10),(.90,.26,.47),'skirt',.91,.90,'band'),
'lass1':('bob',(.65,.28,.85),(.65,.28,.85),'skirt',.91,.92,None),
'woman':('bob',(.90,.21,.43),(.90,.21,.43),'dress',1.02,1.03,None),
'gal':('short',(.16,.12,.10),(.94,.83,.17),'skirt',1.,1.02,None),
'granny':('bun',(.92,.92,.84),(.94,.82,.18),'dress',.88,1.12,None),
}
slug=sys.argv[1];roundn=int(sys.argv[2]);assert roundn in (1,2,3)
assert slug in C
hairkind,hairrgb,toprgb,garment,scale,width,cap=C[slug]
OUT=STUDY/'models'/slug/f'i{roundn}';assert not OUT.exists(),OUT
s=copy.deepcopy(b.STYLES['b_pokemon']);s.id=f'w{roundn}';s.smooth=True;s.light=Lighting(.78,.22,(-.3,.6,.75));s.neck=None
s.arm_pivot=(.30*width,1.38);s.leg_pivot=(.14*width,.77)
d=copy.deepcopy(b.DESIGNS['mark']);d.slug=slug;d.backpack=False
fig=b.Figure(s,d);skin=fig.skin;hair=b.mat('connected scalp hair',hairrgb);shirt=b.mat('garment',toprgb);dark=b.mat('dark skirt shoes',(.12,.105,.14));white=b.mat('white cap',(.94,.94,.88));blue=b.mat('blue cap',(.18,.46,.72) if roundn>=2 else (.24,.61,.85));face=b.mat('face',(1,1,1),f'{fig.prefix}_face')
# Owning closed shell contains chin, cheek, nose, ears, scalp and cap.
rings=[(1.48,.16,.17,0,0),(1.59,.275,.235,0,0),(1.78,.35,.29,0,0),(1.97,.35,.29,0,0),(2.09,.33,.28,0,0),(2.20,.22,.21,0,-.01),(2.25,0,0,0,-.01)]
if hairkind in ('bob','swept','long'):rings[4]=(2.10,.375,.31,0,-.015)
if hairkind=='bun':rings[5]=(2.20,.19,.17,0,-.055);rings[6]=(2.34,0,0,0,-.09)
head=lathe(rings,12,skin,phase=0,smooth=True,name='owning head scalp ears nose')
for i,(x,y,z) in enumerate(head.verts):
 row=i//12;angle=int(round(abs(math.degrees(math.atan2(x,y)))/30))*30
 if row==2:
  if angle==90:x+=math.copysign(.04,x)
  if angle==0:y+=.04
  if angle>=120:z=1.57 if hairkind!='long' else 1.47;y*=.95
 if roundn>=3 and hairkind in ('bob','swept','long') and row in (1,2) and angle>=90:
  x*=1.30 if row==1 else 1.10;y*=1.15
 if row==3:
  z={0:1.91,30:2.00,60:1.91,90:1.94,120:1.74,150:1.69,180:1.68}[angle]
  if hairkind=='swept' and x<0:z-=.055
  if hairkind=='short':z+=.04
  if hairkind in ('long','bob') and angle>=120:z-=.05
 if roundn>=2 and hairkind in ('bob','swept','long') and row in (2,3) and angle>=90:
  y*=1.06
  if row==3 and angle==90:z=1.72
  if row==2 and angle==90:z=1.60
 if row==4 and hairkind=='twintails' and angle in (60,90,120):
  x*=1.23;z+=.035
 head.verts[i]=(x,y,z)
for f in head.faces:
 rows={i//12 for i in f['idx']};r=min(rows);c=head.face_center(f);ang=abs(math.degrees(math.atan2(c[0],c[1])))
 if r>=3:f['mat']=hair
 elif c[1]<.08 and r>=1:f['mat']=hair
 if r<3 and ang<65:
  f['mat']=face;f['uv']=[(max(0,min(1,(head.verts[i][0]+.35)/.70)),max(0,min(1,(2.00-head.verts[i][2])/.55))) for i in f['idx']]
 if cap and r>=4:f['mat']=white if cap=='white' else blue if cap=='blue' else b.mat('red headband',(.86,.12,.22))
 if cap=='band' and r>=5:f['mat']=hair
# Global ear split. Shared edge intersections make all incidences exactly two.
plane=1.85;cache={};new=[]
def clip(f,above):
 ids=f['idx'];out=[];uv=[];src=f['uv']
 for k,ia in enumerate(ids):
  ib=ids[(k+1)%len(ids)];a=head.verts[ia];bb=head.verts[ib];ina=a[2]>=plane if above else a[2]<=plane;inb=bb[2]>=plane if above else bb[2]<=plane
  if ina:
   out.append(ia)
   if src:uv.append(src[k])
  if ina!=inb:
   t=(plane-a[2])/(bb[2]-a[2]);key=tuple(sorted((ia,ib)))
   if key not in cache:cache[key]=head.add_vert(tuple(a[j]+t*(bb[j]-a[j]) for j in range(3)))
   out.append(cache[key])
   if src:uv.append(tuple(src[k][j]+t*(src[(k+1)%len(ids)][j]-src[k][j]) for j in range(2)))
 return out,uv if src else None
for f in head.faces:
 c=head.face_center(f);ang=abs(math.degrees(math.atan2(c[0],c[1])));lo=min(head.verts[i][2] for i in f['idx']);hi=max(head.verts[i][2] for i in f['idx']);side=65<ang<115 and hi<2.10
 for above in ((False,True) if lo<plane<hi else (lo>=plane,)):
  ids,uv=clip(f,above)
  if len(ids)>=3:new.append(dict(idx=ids,mat=(hair if above else skin) if side else f['mat'],uv=None if side else uv,group=f['group']))
head.faces=new
if roundn>=2:
 # Actual clerk i1 review: head reads small and the bob is too short.
 for i,(x,y,z) in enumerate(head.verts):head.verts[i]=(x*1.10,y*1.10,1.48+(z-1.48)*1.06)
fig.add(head)
# Waist and skirt/dress are one closed shell with a material boundary.
long=garment=='dress';bottom=.20 if long else .62;top=1.49
body=lathe([(bottom,.38 if long else .32,.225,0,0),(.80,.28,.19,0,0),(1.05,.235,.17,0,0),(1.31,.29,.19,0,0),(1.43,.265,.17,0,0),(top,.12,.11,0,0)],8,shirt,phase=0,smooth=True,name='continuous garment joined shoulders')
for f in body.faces:
 c=body.face_center(f)
 if garment=='skirt' and c[2]<.80:f['mat']=dark
if roundn>=2 and slug=='clerk':
 uniform=b.mat('painted uniform',(1,1,1),f'{fig.prefix}_uniform');tex=Canvas(background=(62,157,215))
 tex.poly([(4,0),(12,0),(12,24),(5,26)],(33,95,160));tex.poly([(20,0),(28,0),(27,26),(20,24)],(33,95,160));tex.poly([(10,0),(16,6),(13,10),(8,1)],(231,238,231));tex.poly([(22,0),(16,6),(19,10),(24,1)],(231,238,231));fig.textures[f'{fig.prefix}_uniform']=tex
 for f in body.faces:
  if body.face_center(f)[1]>.06 and body.face_center(f)[2]>.80:
   f['mat']=uniform;f['uv']=[((body.verts[i][0]+.32)/.64,(1.49-body.verts[i][2])/.69) for i in f['idx']]
if roundn>=3:body.scale(1.10,1,1)
fig.add(body)
# Single exterior per limb: hand and sleeve meet at shared ring boundaries.
for side in (-1,1):
 x=side*(.33 if roundn>=3 else .30);part=3 if side<0 else 4
 arm=lathe([(.72,.075,.075,x,0),(.79,.10,.09,x,0),(1.02,.075,.08,x,0),(1.18,.085,.085,x,0),(1.37,.12,.115,x-side*.015,0),(1.45,.085,.09,x-side*.06,0),(1.48,0,0,x-side*.10,0)],6,skin,phase=0,smooth=True,name='joined arm hand sleeve')
 for f in arm.faces:
  c=arm.face_center(f)
  if c[2]>(.85 if slug=='granny' else 1.20) and slug not in ('amy','swimmer','joshua','woman'):f['mat']=shirt
 arm.rotate_y(-side*5,about=(x,1.38));fig.add(arm,part,(x,0,1.38))
 x=side*.14
 leg=lathe([(0,.10,.15,x,.05),(.07,.12,.18,x,.06),(.15,.09,.12,x,.02),(.22,.075,.085,x,0),(.52,.08,.085,x,0),(.81,.105,.105,x,0)],6,skin,phase=0,smooth=True,name='joined leg shoe')
 if roundn>=3:
  for i,(lx,ly,lz) in enumerate(leg.verts):
   if lz<.16:leg.verts[i]=(x+(lx-x)*1.28,ly*1.18,lz*1.20)
 for f in leg.faces:
  if leg.face_center(f)[2]<.15:f['mat']=white if slug in ('swimmer','joshua') else blue if slug=='amy' else dark
 fig.add(leg,1 if side<0 else 2,(x,0,.77))
c=Canvas();
for cx in (9,23):
 if slug=='joshua':c.line([(cx-3,15),(cx,13),(cx+3,15)],(38,30,30),2)
 else:eye(c,cx,15,8,10,4,8,iris=(48,74,110) if slug!='granny' else (85,73,66))
 c.line([(cx-3,7),(cx+3,7)],tuple(int(q*255) for q in hairrgb),2)
if slug=='granny':glasses(c,16,15,11,12,gap=3,color=(70,58,44))
mouth(c,16,27,7,color=(118,57,35));fig.textures[f'{fig.prefix}_face']=c
# Scale all authored shapes and pivots coherently, including rigid axes.
for mesh in fig.meshes:
 mesh.scale(width,1,scale);mesh.pivot=(mesh.pivot[0]*width,mesh.pivot[1],mesh.pivot[2]*scale)
# Center-fan large n-gon caps to avoid packed fan slivers.
for mesh in fig.meshes:
 fs=[]
 for f in mesh.faces:
  if len(f['idx'])<=4:fs.append(f);continue
  ids=f['idx'];ci=mesh.add_vert(tuple(sum(mesh.verts[i][k] for i in ids)/len(ids) for k in range(3)))
  for k in range(len(ids)):fs.append(dict(idx=[ci,ids[k],ids[(k+1)%len(ids)]],mat=f['mat'],group=f['group'],uv=None))
 mesh.faces=fs
folder=OUT/slug;(folder/'textures').mkdir(parents=True)
batches,textures=fig.finish();stats=write_l643(folder/'scene.l64',batches,textures)
for n in textures:fig.textures[n].save(folder/'textures'/f'{n}.png')
dump_json(folder/'authoring.json',fig.meshes,dict(names=textures));(folder/'scene.json').write_text(json.dumps(dict(format='L643',parts=list(range(5)),textures=textures,concept=f'../../../concepts/{slug}.png',**stats),indent=2)+'\n')
r=audit(folder/'scene.l64');edges=collections.Counter(tuple(sorted((f['idx'][k],f['idx'][(k+1)%len(f['idx'])]))) for f in head.faces for k in range(len(f['idx'])));r['head_edge_incidence']=dict(collections.Counter(edges.values()));r['head_closed']=set(edges.values())=={2};(OUT/'audit.json').write_text(json.dumps(r,indent=2)+'\n');shutil.copy2(__file__,OUT/'source.py');assert r['passed'] and r['head_closed'],r
print(json.dumps(r,indent=2))
