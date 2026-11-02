#!/usr/bin/env python3
"""Image-led Ronald model, using approved closed Mark body construction."""
import collections,json,math,shutil,sys
from pathlib import Path
HERE=Path(__file__).resolve().parent;STUDY=HERE.parent;EXPERIMENTS=STUDY.parent
sys.path.insert(0,str(EXPERIMENTS/'n64-style/tools'));sys.path.insert(0,str(EXPERIMENTS/'mark-concepts/tools'))
from geom import Mesh,Material,lathe,Lighting,bake,dump_json,write_l643
from paint import Canvas
import builder as b
from audit_exports import audit
REV=sys.argv[1];ROUND=int(REV);assert ROUND in (1,2,3)
OUT=STUDY/'models/ronald'/f'i{REV}';assert not OUT.exists(),OUT
BASE=EXPERIMENTS/'mark-concepts/approved/a_adventure/i3d'
lighting=Lighting(.78,.22,(-.3,.6,.75));skin=b.mat('skin',b.SKIN);hair=b.mat('hair',(.065,.055,.075));face=b.mat('face',(1,1,1),f'ron{ROUND}_face');blue=(.075,.20,.70)
meshes=[]
# Preserve the approved shoulder overlap, rigid-part pivots and shoes.
original=json.loads((BASE/'mark/authoring.json').read_text())
for entry in original['meshes']:
    if entry['name'] in ('head cap ears nose','backward bill','pack'):continue
    m=Mesh(entry['name'],entry['part'],entry['pivot']);m.verts=[tuple(v) for v in entry['verts']]
    materials={}
    for mat in entry['materials']:
        rgb=blue if mat['name'] in ('solid red shirt','sleeve','trousers') else mat['rgb']
        materials[mat['name']]=Material('blue shirt' if mat['name']=='solid red shirt' else mat['name'],rgb,mat['texture'],mat['lit'])
    for f in entry['faces']:m.add_face(f['idx'],materials[f['mat']],f['uv'],Mesh.new_group() if f['smooth'] else None)
    meshes.append(m)
# Owning head, ears, nose, temples, fringe and swept crown: one closed shell.
rings=[(1.46,.19,.19,0,0),(1.59,.28,.245,0,0),(1.77,.37,.315,0,0),
       (1.92,.375,.30,0,0),(2.17,.31,.25,0,-.04),(2.245,0,0,0,-.06)]
head=lathe(rings,12,skin,phase=0,smooth=True,name='ronald owning head hair ears nose')
for i,(x,y,z) in enumerate(head.verts):
    row=i//12;angle=int(round(abs(math.degrees(math.atan2(x,y)))/30)*30)
    if row==2:
        if abs(x)>.36:x+=math.copysign(.045,x)
        if y<-.1:z=1.61;y*=.83
        if abs(x)<.01 and y>0:y+=.05
    if row==3:
        z={0:1.84,30:1.98,60:1.89,90:1.965,120:1.71,150:1.67,180:1.69}[angle]
        if angle==90:x*=.95
    if row==4:
        # Six broad points are alternating owning-ring vertices, no spike objects.
        sample=i%12
        if sample%2:
            z={1:2.36,3:2.43,5:2.35,7:2.39,9:2.45,11:2.38}[sample]
            x*=1.23;y=(y+.04)*1.23-.075
        else:
            z=2.115 if sample in (0,2,10) else 2.16
            x*=.88;y=(y+.04)*.88-.045
    head.verts[i]=(x*1.1,y*1.1,z)
for f in head.faces:
    c=head.face_center(f);rows={i//12 for i in f['idx']}
    if min(rows)>=3:
        f['mat']=hair;f['uv']=None
    elif c[1]>.08:
        f['mat']=face;f['uv']=[(max(0,min(1,(head.verts[i][0]+.407)/.814)),max(0,min(1,(1.99-head.verts[i][2])/.53))) for i in f['idx']]
    elif c[2]>1.60:
        f['mat']=hair;f['uv']=None
# Shared split everywhere gives ear skin without T-junctions against face quads.
plane=1.81;cache={};refined=[]
def clipped(f,above):
    ids=f['idx'];uvs=f['uv'];out=[];newuv=[]
    for k,ia in enumerate(ids):
        nk=(k+1)%len(ids);ib=ids[nk];a=head.verts[ia];d=head.verts[ib]
        ina=a[2]>=plane if above else a[2]<=plane;inb=d[2]>=plane if above else d[2]<=plane
        if ina:
            out.append(ia)
            if uvs:newuv.append(uvs[k])
        if ina!=inb:
            t=(plane-a[2])/(d[2]-a[2]);key=tuple(sorted((ia,ib)))
            if key not in cache:cache[key]=head.add_vert(tuple(a[j]+t*(d[j]-a[j]) for j in range(3)))
            out.append(cache[key])
            if uvs:newuv.append(tuple(uvs[k][j]+t*(uvs[nk][j]-uvs[k][j]) for j in range(2)))
    return out,newuv if uvs else None
for f in head.faces:
    c=head.face_center(f);angle=abs(math.degrees(math.atan2(c[0],c[1])));zs=[head.verts[i][2] for i in f['idx']]
    side=60<angle<120 and max(zs)<=1.99
    for above in ((False,True) if min(zs)<plane<max(zs) else (min(zs)>=plane,)):
        ids,uv=clipped(f,above)
        if len(ids)>=3:refined.append(dict(idx=ids,mat=(hair if above else skin) if side else f['mat'],uv=None if side else uv,group=f['group']))
head.faces=refined;meshes.insert(0,head)
# Ronald has compact competitive eyes, diagonal brows and a short asymmetric smirk.
c=Canvas();white=(239,235,217);ink=(28,27,33);iris=(38,57,91)
for cx,side in ((9,-1),(23,1)):
    if side<0:shape=[(cx-4,11),(cx+4,13),(cx+4,20),(cx-3,20)]
    else:shape=[(cx-4,13),(cx+4,11),(cx+3,20),(cx-4,20)]
    c.poly(shape,white);c.rect(cx,12,cx+3,20,iris);c.rect(cx+1,13,cx+3,18,ink);c.px(cx+1,13,white)
    c.line([(cx-4,11 if side<0 else 13),(cx+4,13 if side<0 else 11)],ink,1)
    c.line([(cx-4,7 if side<0 else 10),(cx+4,10 if side<0 else 7)],ink,2)
c.line([(13,26),(17,26),(20,24)],(105,58,34),1);c.px(16,22,(198,132,69))
# Center triangulation retains every cap boundary and avoids collinear roots.
for mesh in meshes:
    faces=[]
    for f in mesh.faces:
        if len(f['idx'])<=4:faces.append(f);continue
        ids=f['idx'];ci=mesh.add_vert(tuple(sum(mesh.verts[i][j] for i in ids)/len(ids) for j in range(3)))
        uv=tuple(sum(u[j] for u in f['uv'])/len(ids) for j in range(2)) if f['uv'] else None
        for k in range(len(ids)):faces.append(dict(idx=[ci,ids[k],ids[(k+1)%len(ids)]],mat=f['mat'],uv=[uv,f['uv'][k],f['uv'][(k+1)%len(ids)]] if f['uv'] else None,group=f['group']))
    mesh.faces=faces
(OUT/'ronald/textures').mkdir(parents=True)
batches,textures=bake(meshes,lighting,model=1);stats=write_l643(OUT/'ronald/scene.l64',batches,textures)
assert textures==[f'ron{ROUND}_face'],textures
c.save(OUT/'ronald/textures'/f'ron{ROUND}_face.png')
dump_json(OUT/'ronald/authoring.json',meshes,dict(names=textures));(OUT/'ronald/scene.json').write_text(json.dumps(dict(format='L643',parts=list(range(5)),textures=textures,**stats),indent=2)+'\n')
report=audit(OUT/'ronald/scene.l64');(OUT/'audit.json').write_text(json.dumps(report,indent=2)+'\n')
edges=collections.Counter(tuple(sorted((a,d))) for f in head.faces for a,d in zip(f['idx'],f['idx'][1:]+f['idx'][:1]));hairfaces=[set(f['idx']) for f in head.faces if f['mat']==hair];seen=set();components=0
for k in range(len(hairfaces)):
    if k in seen:continue
    components+=1;stack=[k]
    while stack:
        j=stack.pop()
        if j in seen:continue
        seen.add(j);stack.extend(i for i,f in enumerate(hairfaces) if i not in seen and len(f&hairfaces[j])>=2)
topology=dict(head_edge_incidence=dict(collections.Counter(edges.values())),head_closed=all(v==2 for v in edges.values()),hair_components=components)
(OUT/'topology.json').write_text(json.dumps(topology,indent=2)+'\n')
shutil.copytree(BASE/'mark',OUT/'mark');shutil.copytree(BASE/'dr_mason',OUT/'dr_mason')
(OUT/'option.json').write_text(json.dumps(dict(id=f'ronald_i{REV}',label=f'Ronald {REV}',concept_reference='../../../concepts/ronald.png',iteration=REV),indent=2)+'\n');shutil.copy2(__file__,OUT/'source.py')
print(json.dumps(dict(audit=report,topology=topology),indent=2))
assert report['passed'] and topology['head_closed'] and components==1
