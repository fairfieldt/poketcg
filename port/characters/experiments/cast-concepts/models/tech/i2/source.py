#!/usr/bin/env python3
"""Build a reviewed general-family design; round values live in explicit saved design records."""
import copy,json,math,shutil,sys,collections
from pathlib import Path
HERE=Path(__file__).resolve().parent;STUDY=HERE.parent
sys.path.insert(0,str(STUDY.parent/'n64-style/tools'));sys.path.insert(0,str(STUDY.parent/'mark-concepts/tools'))
import builder as b
from geom import lathe,loft,ellipsoid,extrude,dump_json,write_l643,Lighting
from paint import Canvas,eye,glasses,mouth
from audit_exports import audit
SLUG,REV=sys.argv[1:3];C=json.loads((STUDY/'general-designs.json').read_text())[SLUG][REV]
OUT=STUDY/'models'/SLUG/f'i{REV}';assert not OUT.exists()
s=copy.deepcopy(b.STYLES['b_pokemon']);s.id=f'g{SLUG}{REV}';s.smooth=True;s.light=Lighting(.78,.22,(-.3,.6,.75));s.neck=None
H=C['head_bottom'];W=C['width'];HIP=C['hip'];SH=H-.04;HEM=C['hem'];COL=C['shirt'];HAIR=C.get('hair',[.10,.075,.06]);skinrgb=C.get('skin',[231,173,99])
s.torso=[(HEM,C['body_width'],.21),((HEM+SH)/2,C['body_width']*.94,.20),(SH-.14,C['shoulder'],.22),(SH,C['shoulder']*.85,.18),(H+.035,.13,.12)]
d=copy.deepcopy(b.DESIGNS['dr_mason']);d.slug=SLUG;fig=b.Figure(s,d);skin=b.mat('skin',tuple(x/255 for x in skinrgb));fig.skin=skin
hair=b.mat('hair',HAIR);face=b.mat('face',(1,1,1),f'{fig.prefix}_face');shirt=b.mat('shirt',COL);front=b.mat('shirt front',(1,1,1),f'{fig.prefix}_shirt')
# Closed owning shell; all hair boundaries are mesh-ring/material boundaries.
rings=[(H,C['chin'],.18,0,0),(H+.10,W*.83,.25,0,0),(H+.28,W,.30,0,0),(H+.53,W*.99,.30,0,0),(H+C.get('upper_band',.65),W*.92,.28,0,0),(H+C['crown'],W*.72,.22,C.get('sweep',0),0),(H+C['crown']+.07,W*.35,.12,C.get('sweep',0),0),(H+C['crown']+.08,0,0,C.get('sweep',0),0)]
head=lathe(rings,12,skin,phase=0,smooth=True,name='head scalp ears nose')
for i,(x,y,z) in enumerate(head.verts):
 row=i//12;a=(i%12)*30;aa=min(a,360-a)
 if row==2:
  if aa==90:x+=math.copysign(.045,x)
  if aa>115:z=H+C.get('rear_cheek',.08);y*=C.get('nape_depth',.91)
  if aa==0:y+=C.get('nose',.07)
 if row==3:
  z=H+C['hairline'][str(aa)]
  if C.get('sidepart') and a==330:z-=.075
 if row==5 and C.get('two_points') and aa==60:z+=.14
 if row==5 and C.get('forelock') and a==330:z+=.09
 if C.get('forelock') or C.get('two_points'):
  if row==6:z=H+C['crown']+.21
  if row==7:z=H+C['crown']+.25
 if C.get('hood') and row>=3 and aa>65:x*=1.10;y*=1.12
 head.verts[i]=(x,y,z)
for f in head.faces:
 row=min(f['idx'])//12;c=head.face_center(f);aa=math.degrees(math.atan2(abs(c[0]),c[1]))
 if row>=3:f['mat']=hair
 elif aa>115 and row>=1:f['mat']=hair
 elif aa<65:
  f['mat']=face;f['uv']=[(max(0,min(1,(head.verts[i][0]+W)/(2*W))),max(0,min(1,(H+.59-head.verts[i][2])/.59))) for i in f['idx']]
 if C.get('bald') and (row>=4 or (row==3 and aa<70)):f['mat']=skin;f['uv']=None
 if C.get('cap') and row>=3:f['mat']=b.mat('green cap',COL)
 if C.get('hood') and (row>=4 or (aa>65 and row>=1)):f['mat']=shirt;f['uv']=None
# Global shared cut restores localized ears without T-junctions.
height=H+C.get('ear_plane',.315);cache={};refined=[]
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
   ids,uv=clip(f,above);refined.append(dict(idx=ids,mat=((shirt if C.get('hood') else hair) if above else skin) if side else f['mat'],uv=None if side else uv,group=f['group']))
 else:
  if side and c[2]<H+.53:f['mat']=(shirt if C.get('hood') else hair) if c[2]>height else skin;f['uv']=None
  refined.append(f)
head.faces=refined;fig.add(head)
if C.get('cap'):fig.add(extrude([(-.27,.22),(-.28,.43),(-.18,.49),(.18,.49),(.28,.43),(.27,.22)],H+.50,H+.56,b.mat('cap bill',COL),name='attached short cap bill'))
body=lathe([(z,rx,ry,0,0) for z,rx,ry in s.torso],8,shirt,smooth=True,name='joined garment')
for f in body.faces:
 if body.face_center(f)[1]>.10:
  f['mat']=front;f['uv']=[((body.verts[i][0]+C['body_width'])/(2*C['body_width']),(H+.035-body.verts[i][2])/(H+.035-HEM)) for i in f['idx']]
fig.add(body)
for side in (-1,1):
 x=side*(C['shoulder']-.01);pz=SH-.04;bottom=HEM+.12
 cut=bottom+.09 if C.get('long_sleeve') else pz-.23
 arm=lathe([(bottom,.095,.10,x,0),(cut,.115,.115,x,0),(pz+.05,.155,.145,x,0)],6,b.mat('sleeve',C.get('sleeve',COL)),phase=0,smooth=True,name='joined sleeve hand')
 if not C.get('long_sleeve'):arm.remap_material(lambda c,m:skin if c[2]<cut else m)
 arm.merge(ellipsoid((x,.015,bottom-.07),(.11,.10,.115),6,2,skin,True,'hand'));arm.rotate_y(-side*7,about=(x,pz));fig.add(arm,3 if side<0 else 4,(x,0,pz))
 x=side*C.get('leg_spacing',.16);hip=HIP
 leg=lathe([(.13,.12,.13,x,0),(hip*.55,.115,.12,x,0),(hip+.04,C.get('leg_width',.14),.145,x,0)],6,b.mat('trousers',C['trousers']),phase=0,smooth=True,name='trousers shoe')
 if C.get('shorts'):leg.remap_material(lambda c,m:skin if c[2]<hip*.55 else m)
 outline=[(-.13,-.13),(.13,-.13),(.155,-.05),(.155,.21),(.105,.25),(-.105,.25),(-.155,.21),(-.155,-.05)]
 leg.merge(loft([[(x+dx,y,z) for dx,y in outline] for z in (.0,.075,.17)],b.mat('shoe',(.065,.06,.06)),smooth=False,name='shoe'));fig.add(leg,1 if side<0 else 2,(x,0,hip))
fig.shadow()
c=Canvas(background=skinrgb);ink=(35,29,25)
for cx in (9,23):
 eye(c,cx,C.get('eye_y',16),8,C.get('eye_h',11),C.get('iris_w',4),9,iris=(42,58,77))
 c.line([(cx-4,7),(cx+3,C.get('brow_end',6))],C.get('brow',[42,33,27]),C.get('brow_width',2))
if C.get('glasses'):glasses(c,16,16,11,14,gap=3,color=ink,shape=C['glasses'])
mouth(c,16,27,C.get('mouth_w',8),kind=C.get('mouth','smile'),color=(105,60,35));c.px(16,22,(191,124,67));fig.textures[f'{fig.prefix}_face']=c
rgb=tuple(round(v*255) for v in COL);c=Canvas(background=rgb)
if C.get('collar'):c.poly([(10,0),(15,0),(16,4),(12,5)],(241,239,225));c.poly([(17,0),(22,0),(20,5),(16,4)],(241,239,225))
if C.get('tie'):c.poly([(14,2),(18,2),(20,5),(16,15),(12,5)],(43,135,207))
if C.get('vest'):c.poly([(12,0),(20,0),(18,28),(14,28)],(241,239,225));c.rect(0,28,32,32,(35,32,29));c.rect(15,28,18,31,(180,181,173))
if C.get('coat'):c.line([(16,15),(16,31)],(211,211,202),1)
if C.get('hood'):c.line([(10,0),(13,4),(16,5),(19,4),(22,0)],tuple(max(0,v-35) for v in rgb),2)
fig.textures[f'{fig.prefix}_shirt']=c
for mesh in fig.meshes:
 fs=[]
 for f in mesh.faces:
  if len(f['idx'])<=4:fs.append(f);continue
  ids=f['idx'];ci=mesh.add_vert(tuple(sum(mesh.verts[i][k] for i in ids)/len(ids) for k in range(3)));uc=tuple(sum(uv[k] for uv in f['uv'])/len(ids) for k in range(2)) if f['uv'] else None
  for k in range(len(ids)):fs.append(dict(idx=[ci,ids[k],ids[(k+1)%len(ids)]],mat=f['mat'],group=f['group'],uv=[uc,f['uv'][k],f['uv'][(k+1)%len(ids)]] if f['uv'] else None))
 mesh.faces=fs
folder=OUT/SLUG;(folder/'textures').mkdir(parents=True)
batches,textures=fig.finish();stats=write_l643(folder/'scene.l64',batches,textures)
for name in textures:fig.textures[name].save(folder/'textures'/f'{name}.png')
dump_json(folder/'authoring.json',fig.meshes,dict(names=textures));(folder/'scene.json').write_text(json.dumps(dict(format='L643',parts=list(range(5)),textures=textures,**stats),indent=2)+'\n')
report=audit(folder/'scene.l64');(OUT/'audit.json').write_text(json.dumps(report,indent=2)+'\n');assert report['passed'],report
edges=collections.Counter(tuple(sorted((f['idx'][k],f['idx'][(k+1)%len(f['idx'])]))) for f in head.faces for k in range(len(f['idx'])));assert set(edges.values())=={2}
(OUT/'option.json').write_text(json.dumps(dict(id=f'{SLUG}_i{REV}',label=f'{SLUG.title()} {REV}',concept_reference=f'../../../concepts/{SLUG}.png',iteration=REV),indent=2)+'\n');(OUT/'design.json').write_text(json.dumps(C,indent=2)+'\n');shutil.copy2(__file__,OUT/'source.py');print(SLUG,REV,report['triangles'],'passed')
