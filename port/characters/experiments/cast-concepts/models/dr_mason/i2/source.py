#!/usr/bin/env python3
"""Image-led Mason reconstruction using the accepted closed-head rigid-part pipeline."""
import copy,json,math,shutil,struct,sys,collections
from pathlib import Path
HERE=Path(__file__).resolve().parent;STUDY=HERE.parent
sys.path.insert(0,str(STUDY.parent/'n64-style/tools'));sys.path.insert(0,str(STUDY.parent/'mark-concepts/tools'))
import builder as b
from geom import lathe,loft,ellipsoid,dump_json,write_l643,Lighting
from paint import Canvas,eye,glasses,mouth
from audit_exports import audit
REV=sys.argv[1];ROUND=int(REV[0]);assert ROUND in (1,2,3)
OUT=STUDY/'models/dr_mason'/f'i{REV}';assert not OUT.exists()
s=copy.deepcopy(b.STYLES['b_pokemon']);s.id=f'mas{REV}';s.smooth=True;s.light=Lighting(.78,.22,(-.3,.6,.75));s.neck=None
s.torso=[(.64,.32,.21),(.95,.29,.205),(1.35,.31,.22),(1.53,.30,.20),(1.62,.135,.12)]
if ROUND>=2:s.torso[0]=(.64,.345,.225);s.torso[1]=(.95,.305,.215)
s.arm_pivot=(.32,1.49);s.leg_pivot=(.16,.69)
d=copy.deepcopy(b.DESIGNS['dr_mason']);fig=b.Figure(s,d);skin=fig.skin
hair=b.mat('hair',(.92,.91,.87));face=b.mat('face',(1,1,1),f'{fig.prefix}_face');coat=b.mat('coat',(.91,.90,.84));coatfront=b.mat('coat front',(1,1,1),f'{fig.prefix}_coat')
# One closed shell owns chin, cheeks, nose, ears and swept hair crown.
rings=[(1.56,.17,.17,0,0),(1.66,.29,.245,0,0),(1.84,.365,.30,0,0),(2.08,.37,.30,0,0),(2.20,.345,.29,0,0),(2.32,.265,.22,-.035,0),(2.38,.12,.12,-.07,0),(2.39,0,0,-.07,0)]
if ROUND>=2:
 rings[0]=(1.56,.195,.18,0,0);rings[1]=(1.66,.315,.25,0,0)
 rings[5]=(2.35,.27,.225,-.075,0);rings[6]=(2.43,.13,.12,-.115,0);rings[7]=(2.44,0,0,-.115,0)
head=lathe(rings,12,skin,phase=0,smooth=True,name='head swept scalp ears nose')
for i,(x,y,z) in enumerate(head.verts):
 row=i//12;aa=min((i%12)*30,360-(i%12)*30)
 if row==2:
  if aa==90:x+=math.copysign(.045,x)
  if aa>115:z=1.64;y*=.91
  if aa==0:y+=.085
 if row==3:
  # Receding front hairline, short temples, low rounded occipital hair.
  z={0:2.115,30:2.14,60:2.06,90:1.97,120:1.78,150:1.76,180:1.75}[aa]
 if row==4 and aa<65:z+=(.07 if ROUND>=2 else .035) if x<0 else 0
 head.verts[i]=(x,y,z)
for f in head.faces:
 row=min(f['idx'])//12;c=head.face_center(f);aa=math.degrees(math.atan2(abs(c[0]),c[1]))
 if row>=3:f['mat']=hair
 elif aa>115 and row>=1:f['mat']=hair
 elif aa<65:
  f['mat']=face;f['uv']=[(max(0,min(1,(head.verts[i][0]+.375)/.75)),max(0,min(1,(2.14-head.verts[i][2])/.58))) for i in f['idx']]
# Shared global ear plane: no T-junctions and no black/white-painted ear blocks.
height=1.875;cache={};refined=[]
def clip(f,above):
 ids=f['idx'];uvs=f['uv'];out=[];ouv=[]
 for k,ia in enumerate(ids):
  ib=ids[(k+1)%len(ids)];a=head.verts[ia];bb=head.verts[ib]
  ina=a[2]>=height if above else a[2]<=height;inb=bb[2]>=height if above else bb[2]<=height
  if ina:
   out.append(ia)
   if uvs:ouv.append(uvs[k])
  if ina!=inb:
   t=(height-a[2])/(bb[2]-a[2]);key=tuple(sorted((ia,ib)))
   if key not in cache:cache[key]=head.add_vert(tuple(a[j]+t*(bb[j]-a[j]) for j in range(3)))
   out.append(cache[key])
   if uvs:ouv.append(tuple(uvs[k][j]+t*(uvs[(k+1)%len(ids)][j]-uvs[k][j]) for j in range(2)))
 return out,ouv if uvs else None
for f in head.faces:
 c=head.face_center(f);aa=math.degrees(math.atan2(abs(c[0]),c[1]));side=65<=aa<=115
 if min(head.verts[i][2] for i in f['idx'])<height<max(head.verts[i][2] for i in f['idx']):
  for above in (False,True):
   ids,uv=clip(f,above);refined.append(dict(idx=ids,mat=(hair if above else skin) if side else f['mat'],uv=None if side else uv,group=f['group']))
 else:
  if side and c[2]<2.08:f['mat']=hair if c[2]>height else skin;f['uv']=None
  refined.append(f)
head.faces=refined;fig.add(head)
body=lathe([(z,rx,ry,0,0) for z,rx,ry in s.torso],8,coat,smooth=True,name='long lab coat')
for f in body.faces:
 if body.face_center(f)[1]>.10:
  f['mat']=coatfront;f['uv']=[((body.verts[i][0]+.33)/.66,(1.62-body.verts[i][2])/.98) for i in f['idx']]
fig.add(body)
for side in (-1,1):
 x=side*.32;pz=1.49
 arm=lathe([(.86,.11,.11,x,0),(1.13,.115,.115,x,0),(1.53,.155,.145,x,0)],6,coat,phase=0,smooth=True,name='joined long sleeve hand')
 arm.merge(ellipsoid((x,.015,.79),(.11,.10,.115),6,2,skin,True,'hand'));arm.rotate_y(-side*7,about=(x,pz));fig.add(arm,3 if side<0 else 4,(x,0,pz))
 x=side*.16;hip=.69
 leg=lathe([(.13,.12,.13,x,0),(.39,.115,.12,x,0),(.73,.14,.145,x,0)],6,b.mat('brown trousers',(.26,.20,.15)),phase=0,smooth=True,name='trousers black shoe')
 outline=[(-.13,-.13),(.13,-.13),(.155,-.05),(.155,.21),(.105,.25),(-.105,.25),(-.155,.21),(-.155,-.05)]
 shoe=loft([[(x+dx,y,z) for dx,y in outline] for z in (.0,.075,.17)],b.mat('black shoe',(.065,.06,.06)),smooth=False,name='shoe')
 leg.merge(shoe);fig.add(leg,1 if side<0 else 2,(x,0,hip))
fig.shadow()
c=Canvas();white=(247,239,220);ink=(47,33,24)
for cx in (9,23):
 eye(c,cx,16,8,11,4,9,iris=(42,58,77))
 c.line([(cx-4,7),(cx+3,6)],(139,140,132),3)
glasses(c,16,16,11,14,gap=3,color=ink)
mouth(c,16,27,8,color=(105,60,35));c.px(16,22,(191,124,67));fig.textures[f'{fig.prefix}_face']=c
c=Canvas(background=(231,230,214));c.poly([(12,0),(20,0),(16,8)],(96,65,43));c.line([(16,9),(16,31)],(213,212,196),1);fig.textures[f'{fig.prefix}_coat']=c
# Cap n-gons from their centers; packed mesh audit remains independent.
for mesh in fig.meshes:
 fs=[]
 for f in mesh.faces:
  if len(f['idx'])<=4:fs.append(f);continue
  ids=f['idx'];ci=mesh.add_vert(tuple(sum(mesh.verts[i][k] for i in ids)/len(ids) for k in range(3)));uc=tuple(sum(uv[k] for uv in f['uv'])/len(ids) for k in range(2)) if f['uv'] else None
  for k in range(len(ids)):fs.append(dict(idx=[ci,ids[k],ids[(k+1)%len(ids)]],mat=f['mat'],group=f['group'],uv=[uc,f['uv'][k],f['uv'][(k+1)%len(ids)]] if f['uv'] else None))
 mesh.faces=fs
folder=OUT/'dr_mason';(folder/'textures').mkdir(parents=True)
batches,textures=fig.finish();stats=write_l643(folder/'scene.l64',batches,textures)
for name in textures:fig.textures[name].save(folder/'textures'/f'{name}.png')
dump_json(folder/'authoring.json',fig.meshes,dict(names=textures));(folder/'scene.json').write_text(json.dumps(dict(format='L643',parts=list(range(5)),textures=textures,**stats),indent=2)+'\n')
report=audit(folder/'scene.l64');(OUT/'audit.json').write_text(json.dumps(report,indent=2)+'\n');assert report['passed'],report
edges=collections.Counter(tuple(sorted((f['idx'][k],f['idx'][(k+1)%len(f['idx'])]))) for f in head.faces for k in range(len(f['idx'])));assert set(edges.values())=={2}
# Retain agreed Mark as control; Ronald stays production while his separate experiment proceeds.
shutil.copytree(STUDY.parent/'mark-concepts/approved/a_adventure/i3d/mark',OUT/'mark')
generated=STUDY.parents[3]/'port/world3d/generated';actor=OUT/'ronald';(actor/'textures').mkdir(parents=True)
raw=(generated/'maps/actor_ronald.l64').read_bytes();(actor/'scene.l64').write_bytes(raw);_,_,nt=struct.unpack_from('>4sHH',raw)
for k in range(nt):
 name=raw[8+32*k:40+32*k].split(b'\0')[0].decode().replace('.sprite','.png');shutil.copy2(generated/'textures'/name,actor/'textures'/name)
(OUT/'option.json').write_text(json.dumps(dict(id=f'dr_mason_i{REV}',label=f'Mason {REV}',concept_reference='../../../concepts/dr-mason.png',iteration=REV),indent=2)+'\n');shutil.copy2(__file__,OUT/'source.py');print(json.dumps(report,indent=2))
