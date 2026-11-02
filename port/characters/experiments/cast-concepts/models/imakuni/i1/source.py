#!/usr/bin/env python3
"""Generate one retained, image-led leader modeling round."""
import collections,copy,json,math,shutil,sys
from pathlib import Path
HERE=Path(__file__).resolve().parent;STUDY=HERE.parent;EXP=STUDY.parent
sys.path.insert(0,str(EXP/'n64-style/tools'));sys.path.insert(0,str(EXP/'mark-concepts/tools'))
from geom import Mesh,Material,lathe,extrude,Lighting,bake,dump_json,write_l643
from paint import Canvas,eye,glasses
import builder as b
from audit_exports import audit
from leaders_recipes import RECIPES,REVISIONS,DARK,WHITE
slug=sys.argv[1];roundid=int(sys.argv[2]);assert slug in RECIPES and roundid in (1,2,3)
cfg=copy.deepcopy(RECIPES[slug]);revision=REVISIONS.get((slug,roundid),{})
if roundid>1:assert revision,'Actual prior capture review must define this revision'
cfg.update(revision);out=STUDY/'models'/slug/f'i{roundid}';assert not out.exists(),out
assert (STUDY/'concepts'/f'{slug}.png').is_file()
base=EXP/'mark-concepts/approved/a_adventure/i3d';original=json.loads((base/'mark/authoring.json').read_text())
skin=b.mat('skin',b.SKIN);hair=b.mat('hair',cfg['hair']);front=b.mat('face',(1,1,1),f'{slug[:7]}{roundid}_face');cloth=b.mat('clothing',cfg['top']);clothtex=b.mat('costume',(1,1,1),f'{slug[:7]}{roundid}_top')
lighting=Lighting(.78,.22,(-.3,.6,.75));meshes=[]
# Approved limb construction and pivots, retaining actual five-part walking rig.
for entry in original['meshes']:
 if entry['name'] in ('head cap ears nose','backward bill','pack','torso'):continue
 m=Mesh(entry['name'],entry['part'],entry['pivot']);m.verts=[tuple(v) for v in entry['verts']]
 mats={}
 for mat in entry['materials']:
  rgb=mat['rgb'];name=mat['name']
  if name=='sleeve':rgb=tuple(b.SKIN) if cfg.get('bare') else cfg['sleeve']
  if name=='trousers':rgb=cfg['pants']
  if name in ('shoe','sole'):rgb=DARK if name=='shoe' else (.07,.065,.08)
  if slug=='imakuni' and name=='skin':rgb=cfg['top']
  mats[name]=Material(name,rgb,None,mat['lit'])
 smoothgroup=Mesh.new_group()
 for f in entry['faces']:
  material=mats[f['mat']];center=tuple(sum(m.verts[i][j] for i in f['idx'])/len(f['idx']) for j in range(3))
  if cfg.get('long') and f['mat']=='skin' and entry['part'] in (3,4) and center[2]>.745:material=b.mat('long sleeve',cfg['sleeve'])
  m.add_face(f['idx'],material,None,smoothgroup if f['smooth'] else None)
 if cfg.get('bare') and entry['part'] in (3,4):m.scale(.86,.86,1,about=tuple(entry['pivot']))
 meshes.append(m)
style=cfg['style'];wide=cfg.get('wide',1)
hem=.42 if style=='dress' else .18 if style=='gown' else .55 if style=='coat' else .76
rings=[(hem,.34 if style in ('dress','gown') else .30,.215,0,0),(.83,.275,.195,0,0),(1.06,.25,.18,0,0),(1.34,.305,.20,0,0),(1.47,.235,.17,0,0),(1.50,.12,.10,0,0)]
if style in ('dress','gown'):rings[2]=(1.06,.23,.17,0,0)
rings=[(z,rx*wide,ry*wide,x,y) for z,rx,ry,x,y in rings]
torso=lathe(rings,8,cloth,smooth=True,name='torso costume')
for f in torso.faces:
 c=torso.face_center(f)
 if c[1]>.05:
  f['mat']=clothtex;f['uv']=[(max(0,min(1,(torso.verts[i][0]+.35*wide)/(.70*wide))),max(0,min(1,(1.50-torso.verts[i][2])/(1.50-hem)))) for i in f['idx']]
meshes.append(torso)
# Closed owning face/scalp, frontal UV projection only; geometry remains convex.
cut=cfg['haircut'];longhair=cut in ('bob','longbob','longrecede')
rings=[(1.46,.19,.19,0,0),(1.59,.28,.245,0,0),(1.77,.37,.315,0,0),(1.92,.375,.30,0,0),(2.13,.31,.25,0,-.03),(2.235,0,0,0,-.04)]
if cut=='hood':rings.insert(-1,(2.22,.17,.14,0,0))
head=lathe(rings,12,skin,phase=0,smooth=True,name=f'{slug} owning head scalp')
face_top=2.02
for i,(x,y,z) in enumerate(head.verts):
 row=i//12;angle=int(round(abs(math.degrees(math.atan2(x,y)))/30)*30);sample=i%12
 if row==2:
  if abs(x)>.36:x+=math.copysign(.045,x)
  if y<-.1:z=1.61;y*=.85
  if abs(x)<.01 and y>0:y+=cfg.get('nose',.05)
 if row==3:
  z={0:1.91,30:2.00,60:1.94,90:1.965,120:1.74,150:1.69,180:1.70}[angle]
  if cut in ('recede','longrecede'):z={0:2.105,30:2.09,60:2.02,90:1.965,120:1.75,150:1.70,180:1.72}[angle]
  if cut in ('sweep','part','bob','longbob') and y>0:
   z=1.86 if sample in (1,2) else 2.01 if sample in (10,11) else 1.94
  if cut=='hood':z=2.02 if y>0 else 1.70
 if longhair and row<3 and y<.06:
  z=([1.30,1.43,1.62][row] if cut!='longbob' else [1.13,1.36,1.60][row]);x*=1.35 if row==0 else 1.18
  if y<0:y*=1.20
 if row==4:
  if cut=='spike':
   z={0:2.39,1:2.16,2:2.14,3:2.31,4:2.17,5:2.32,6:2.17,7:2.32,8:2.17,9:2.31,10:2.14,11:2.16}[sample];x*=1.30;y-=.09
  if cut in ('sweep','part'):z+=.04 if sample in (9,10,11) else 0
  if cut=='hood':
   z=2.39 if sample in (3,9) else 2.31 if sample in (2,4,8,10) else 2.15
   if sample in (2,3,4,8,9,10):x*=1.35
 if cut=='hood' and row==5:
  z=2.43 if sample in (3,9) else 2.35 if sample in (2,4,8,10) else 2.22
  if sample in (2,3,4,8,9,10):x*=2.15
 if cut=='spike' and row==5:z=2.15;y=-.11
 if cut=='cap' and row>=4:z+=.04
 x*=cfg.get('head_width',1.10);y*=cfg.get('head_depth',1.10)
 head.verts[i]=(x,y,z)
for f in head.faces:
 c=head.face_center(f);rows={i//12 for i in f['idx']}
 if min(rows)>=3:f['mat']=hair
 elif c[1]>.08:
  f['mat']=front;f['uv']=[(max(0,min(1,(head.verts[i][0]+.407)/.814)),max(0,min(1,(face_top-head.verts[i][2])/.56))) for i in f['idx']]
 elif c[2]>1.60 or longhair:f['mat']=hair
 if cut=='hood' and min(rows)==0:f['mat']=hair;f['uv']=None
 if cut=='cap' and min(rows)>=4:f['mat']=b.mat('cap',cfg['top']);f['uv']=None
# Split every crossed polygon and share intersections, preserving UV continuity.
for plane in (((1.65 if slug=='courtney' else 1.63),1.81) if longhair else (1.81,)):
 cache={};refined=[]
 def clip(f,above):
  ids=f['idx'];uvs=f['uv'];result=[];newuv=[]
  for k,ia in enumerate(ids):
   nk=(k+1)%len(ids);ib=ids[nk];a=head.verts[ia];d=head.verts[ib];ina=a[2]>=plane if above else a[2]<=plane;inb=d[2]>=plane if above else d[2]<=plane
   if ina:
    result.append(ia)
    if uvs:newuv.append(uvs[k])
   if ina!=inb:
    t=(plane-a[2])/(d[2]-a[2]);key=tuple(sorted((ia,ib)))
    if key not in cache:cache[key]=head.add_vert(tuple(a[j]+t*(d[j]-a[j]) for j in range(3)))
    result.append(cache[key])
    if uvs:newuv.append(tuple(uvs[k][j]+t*(uvs[nk][j]-uvs[k][j]) for j in range(2)))
  return result,newuv if uvs else None
 for f in head.faces:
  zs=[head.verts[i][2] for i in f['idx']]
  for above in ((False,True) if min(zs)<plane<max(zs) else (min(zs)>=plane,)):
   ids,uv=clip(f,above)
   if len(ids)>=3:refined.append(dict(idx=ids,mat=f['mat'],uv=uv,group=f['group']))
 head.faces=refined
for f in head.faces:
 c=head.face_center(f);angle=abs(math.degrees(math.atan2(c[0],c[1])));zs=[head.verts[i][2] for i in f['idx']]
 if 60<angle<120 and max(zs)<=1.99:
  f['mat']=skin if c[2]<1.81 and (not longhair or c[2]>1.63) else hair;f['uv']=None
if cut=='hood':
 for f in head.faces:
  c=head.face_center(f);angle=abs(math.degrees(math.atan2(c[0],c[1])))
  if 60<angle<120:f['mat']=hair;f['uv']=None
meshes.insert(0,head)
if cut=='cap':meshes.append(extrude([(-.24,.20),(-.24,.40),(-.13,.49),(.13,.49),(.24,.40),(.24,.20)],1.995,2.065,b.mat('cap',cfg['top']),name='attached forward cap bill'))
# Unique face painting, traced from each concept's expression and accessories.
c=Canvas();ink=(34,28,27);white=(242,237,222);brown=(110,65,40)
mode=cfg['eyes'];eh=12 if mode in ('gentle','surprised') else 9;ew=8
for cx,side in ((9,-1),(23,1)):
 eye(c,cx,14,ew,eh,4,eh-2,(38,43,56),True,shape='oval')
 if mode in ('competitive','firm'):
  c.line([(cx-4,7 if side<0 else 9),(cx+4,9 if side<0 else 7)],ink,2)
 else:c.line([(cx-4,6),(cx+3,6)],ink,1)
if mode=='glasses':glasses(c,16,14,9,11,1,ink,'rect')
if mode=='monocle':
 c.d.ellipse((18,7,29,22),outline=ink+(255,),width=1)
 c.poly([(5,23),(10,21),(16,23),(22,21),(28,23),(26,26),(19,26),(16,25),(12,26),(6,26)],(65,62,62))
if mode=='surprised':c.ellipse(10,24,23,30,(68,43,31));c.rect(12,24,21,26,white)
elif mode in ('firm','competitive'):c.line([(13,27),(19,27)],brown,1)
else:c.line([(13,26),(16,27),(20,25)],brown,1)
c.px(16,22,(198,132,69))
# Costume panel painting: broad flat panels, no detached collars/lapels/buttons.
top8=tuple(round(x*255) for x in cfg['top']);costume=Canvas(background=top8)
if style in ('vest','jacket'):
 under=(240,238,230) if style=='vest' else (31,29,34)
 costume.rect(11,0,21,29,under);costume.poly([(8,0),(14,0),(16,5)],(247,245,234));costume.poly([(18,0),(24,0),(16,5)],(247,245,234));costume.rect(0,28,32,32,(35,32,37));costume.rect(14,28,18,31,(151,151,144))
if style=='open':costume.poly([(9,0),(23,0),(16,13)],(231,173,99));costume.rect(0,28,32,32,(29,29,35))
if style=='coat':
 shirt=tuple(round(x*255) for x in cfg.get('shirt',(.2,.2,.2)));costume.poly([(10,0),(22,0),(16,14)],shirt);costume.line([(9,1),(12,10),(16,14),(20,10),(23,1)],(190,188,180),1);costume.line([(16,14),(16,32)],(205,203,195),1)
 for y in (18,24):costume.rect(15,y,17,y+2,(174,173,167))
if style in ('dress','gown'):
 costume.rect(0,13,32,16,(29,28,32));costume.rect(13,0,20,12,tuple(min(255,x+15) for x in top8))
# Center-triangulate polygon caps, and make winding consistent across owning head.
for mesh in meshes:
 faces=[]
 for f in mesh.faces:
  if len(f['idx'])<=4:faces.append(f);continue
  ids=f['idx'];center=mesh.add_vert(tuple(sum(mesh.verts[i][j] for i in ids)/len(ids) for j in range(3)));uv=tuple(sum(u[j] for u in f['uv'])/len(ids) for j in range(2)) if f['uv'] else None
  for k in range(len(ids)):faces.append(dict(idx=[center,ids[k],ids[(k+1)%len(ids)]],mat=f['mat'],uv=[uv,f['uv'][k],f['uv'][(k+1)%len(ids)]] if f['uv'] else None,group=f['group']))
 mesh.faces=faces
# Orient neighbors consistently by shared owning-vertex edges.
edgemap=collections.defaultdict(list)
for fi,f in enumerate(head.faces):
 for a,d in zip(f['idx'],f['idx'][1:]+f['idx'][:1]):edgemap[tuple(sorted((a,d)))].append((fi,a,d))
assert all(len(v)==2 for v in edgemap.values())
flip={0:False};stack=[0]
while stack:
 fi=stack.pop();f=head.faces[fi]
 for a,d in zip(f['idx'],f['idx'][1:]+f['idx'][:1]):
  for other,oa,od in edgemap[tuple(sorted((a,d)))]:
   if other==fi:continue
   need=flip[fi]^(a==oa)
   if other not in flip:flip[other]=need;stack.append(other)
   else:assert flip[other]==need
for fi,value in flip.items():
 if value:
  head.faces[fi]['idx'].reverse()
  if head.faces[fi]['uv']:head.faces[fi]['uv'].reverse()
(out/slug/'textures').mkdir(parents=True)
batches,textures=bake(meshes,lighting,model=1);stats=write_l643(out/slug/'scene.l64',batches,textures)
for name in textures:(c if name.endswith('_face') else costume).save(out/slug/'textures'/f'{name}.png')
dump_json(out/slug/'authoring.json',meshes,dict(names=textures));(out/slug/'scene.json').write_text(json.dumps(dict(format='L643',model=slug,parts=list(range(5)),textures=textures,**stats),indent=2)+'\n')
report=audit(out/slug/'scene.l64');(out/'audit.json').write_text(json.dumps(report,indent=2)+'\n')
hairfaces=[set(f['idx']) for f in head.faces if f['mat']==hair];seen=set();components=0
for k in range(len(hairfaces)):
 if k in seen:continue
 components+=1;todo=[k]
 while todo:
  j=todo.pop()
  if j in seen:continue
  seen.add(j);todo.extend(i for i,f in enumerate(hairfaces) if i not in seen and len(f&hairfaces[j])>=2)
topology=dict(head_edge_incidence=dict(collections.Counter(len(v) for v in edgemap.values())),head_closed=True,hair_components=components,consistent_winding=True)
(out/'topology.json').write_text(json.dumps(topology,indent=2)+'\n');(out/'option.json').write_text(json.dumps(dict(id=f'{slug}_i{roundid}',label=f'{slug} {roundid}',concept_reference=f'../../../concepts/{slug}.png',iteration=roundid,revision=revision),indent=2)+'\n');shutil.copy2(__file__,out/'source.py');shutil.copy2(HERE/'leaders_recipes.py',out/'leaders_recipes.py')
print(slug,roundid,stats,report['errors'],topology)
assert report['passed'] and components<=1
