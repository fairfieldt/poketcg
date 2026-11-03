"""Photo-led five-part cats. Run Python, then import authoring JSON in Blender.

The --round output is immutable review evidence; --publish copies its exports.
Authoring axes: X right, Y forward, Z up, matching the cast study's geom.py.
"""
import argparse, collections, hashlib, json, math, shutil, sys
from pathlib import Path
from PIL import Image, ImageDraw
ROOT=Path(__file__).resolve().parents[3]
STUDY=ROOT/'port/characters/experiments/cat-concepts'
sys.path.insert(0,str(STUDY.parent/'n64-style/tools'))
sys.path.insert(0,str(STUDY.parent/'mark-concepts/tools'))
from geom import Mesh,Material,Lighting,bake,dump_json,write_l643,lathe,ellipsoid,loft,wedge
from audit_exports import audit

def pigment(name,rgb):return Material(name,tuple(c/255 for c in rgb))

def textures(folder,slug,tabby,revision):
    gray=(126,123,119) if tabby else (87,94,108)
    dark=(65,63,61) if tabby else (65,71,84)
    cream=(209,203,188) if tabby else (235,234,222)
    eye=(181,185,126) if tabby else (188,201,120)
    im=Image.new('RGB',(32,32),gray);d=ImageDraw.Draw(im)
    # Face UV is a frontal projection across the owning curved skull.
    if tabby:
        d.line([(9,0),(11,7),(14,3),(16,8),(18,3),(21,7),(23,0)],fill=dark,width=2)
        for yy in (18,22):
            d.polygon([(0,yy-2),(7,yy),(10,yy+2),(2,yy+1)],fill=dark)
            d.polygon([(31,yy-2),(24,yy),(21,yy+2),(29,yy+1)],fill=dark)
    # Compact almond eyes, individually readable irises and slit pupils.
    for cx in (9,22):
        d.polygon([(cx-4,13),(cx-2,10),(cx+2,10),(cx+4,13),(cx+2,17),(cx-2,17)],fill=(29,32,36))
        d.polygon([(cx-3,13),(cx-1,11),(cx+2,11),(cx+3,13),(cx+1,16),(cx-2,16)],fill=eye)
        d.line([(cx,11),(cx,16)],fill=(25,29,33),width=1)
        d.point((cx-1,11),fill=(237,238,206))
    d.ellipse((6,21 if tabby else 24,17,29),fill=cream);d.ellipse((14,21 if tabby else 24,25,29),fill=cream)
    d.polygon([(10,26),(21,26),(20,31),(11,31)],fill=cream)
    d.polygon([(13,21),(18,21),(16,24),(15,24)],fill=(85,66,64) if tabby else (34,39,49))
    d.line([(16,24),(16,26),(13,27)],fill=(64,62,62));d.line([(16,26),(19,27)],fill=(64,62,62))
    for yy in (23,25):
        d.line([(3,yy),(10,yy+1)],fill=(190,188,177));d.line([(22,yy+1),(28,yy)],fill=(190,188,177))
    body=Image.new('RGB',(32,32),gray);b=ImageDraw.Draw(body)
    if tabby:
        for y in (4,12,21,29):
            b.polygon([(0,y),(8,y+2),(10,y+4),(0,y+3)],fill=dark)
            b.polygon([(31,y),(23,y+2),(21,y+4),(31,y+3)],fill=dark)
    else:b.polygon([(6,0),(25,0),(24,9),(21,9),(21,14),(18,14),(16,23),(13,17),(10,14),(10,9),(7,8)],fill=cream)
    out={}
    for kind,img in [('face',im),('body',body)]:
        name=slug+'_'+kind;img.save(folder/'textures'/f'{name}.png');out[kind]=Material(kind,(1,1,1),name)
    return out,gray,dark,cream

def shell_proof(mesh):
    edges=collections.Counter();direction=collections.Counter()
    for f in mesh.faces:
        ids=f['idx']
        for a,b in zip(ids,ids[1:]+ids[:1]):edges[tuple(sorted((a,b)))]+=1;direction[(a,b)]+=1
    return dict(vertices=len(mesh.verts),faces=len(mesh.faces),edges=len(edges),closed=all(n==2 for n in edges.values()),consistently_wound=all(direction[(b,a)]==n for (a,b),n in direction.items()))

def build(slug,model,tabby,folder,revision):
    folder.mkdir(parents=True);(folder/'textures').mkdir()
    tex,gray,dark,cream=textures(folder,slug,tabby,revision)
    fur=pigment('fur',gray);stripe=pigment('stripe',dark);pale=pigment('pale',cream);ear=pigment('inner ear',(151,113,113))
    meshes=[]
    body=lathe([(.47,.22,.17,0,0),(.62,.34,.24,0,0),(1.02,.39,.27,0,0),(1.38,.34,.22,0,0),(1.52,.22,.18,0,0)],12,fur,phase=0,name='body',smooth=False)
    for f in body.faces:
        x,y,z=body.face_center(f)
        if y>.08:
            f['mat']=tex['body'];f['uv']=[(.5+body.verts[i][0]/.8,(1.52-body.verts[i][2])/1.05) for i in f['idx']]
        elif tabby and int(z*9)%3==0:f['mat']=stripe
    meshes.append(body)
    for sign,part in [(-1,3),(1,4)]:
        leg=lathe([(.12,.16,.19,sign*.20,.05),(.29,.17,.18,sign*.20,0),(.55,.17,.17,sign*.20,0),(.70,.15,.15,sign*.20,0)],8,fur,name='leg',smooth=False)
        if tabby:leg.remap_material(lambda c,m:stripe if .30<c[2]<.5 else m)
        foot=lathe([(.025,.18,.22,sign*.20,.10),(.12,.19,.25,sign*.20,.11),(.23,.16,.20,sign*.20,.07)],8,pale if not tabby else fur,name='paw',smooth=False)
        leg.merge(foot);leg.name=f'leg {part}';leg.part=part;leg.pivot=(sign*.20,0,.65);meshes.append(leg)
    for sign,part in [(-1,1),(1,2)]:
        arm=lathe([(.69,.13,.13,sign*.49,.02),(.87,.14,.14,sign*.47,0),(1.14,.14,.15,sign*.41,0),(1.38,.16,.17,sign*.32,0)],8,fur,name='arm',smooth=False)
        arm.remap_material(lambda c,m:pale if not tabby and c[2]<.80 else stripe if tabby and .94<c[2]<1.10 else m)
        arm.name=f'arm {part}';arm.part=part;arm.pivot=(sign*.33,0,1.36);meshes.append(arm)
    # The head is one shared, consistently-wound shell. Its front rings form
    # the nose/muzzle projection; there is no pasted face card or eye geometry.
    hw=.48 if tabby else .51
    rings=[(1.46,0,0,0,0),(1.54,.27 if tabby else .31,.24,0,.035),(1.68,hw*(.82 if tabby else .92),.33,0,.02),(1.84,hw,.36,0,0),(2.02,hw*.96,.34,0,0),(2.18,hw*.82,.29,0,-.015),(2.30,hw*.52,.19,0,-.025),(2.36,0,0,0,-.025)]
    head=lathe(rings,16,fur,phase=0,name='head closed skull',smooth=False)
    head.verts=[(x,y+(.18*max(0,1-abs(x)/.36) if y>0 and 1.54<=z<=1.84 else 0),z) for x,y,z in head.verts]
    for f in head.faces:
        x,y,z=head.face_center(f)
        if y>.06:
            f['mat']=tex['face'];f['uv']=[(.5+head.verts[i][0]/1.02,(2.36-head.verts[i][2])/.90) for i in f['idx']]
        elif tabby and 1.68<z<2.02 and abs(x)>.2:f['mat']=stripe if z<1.82 else fur
    proof=shell_proof(head);assert proof['closed'] and proof['consistently_wound'];meshes.append(head)
    for sign in (-1,1):
        e=wedge([(sign*.19,.15,2.19),(sign*.53,.10,2.16),(sign*.50,-.13,2.16),(sign*.22,-.13,2.19)],(sign*.45,-.01,2.60),fur,name='head ear',smooth=False)
        # Split the owning front ear triangle around an inset pink triangle.
        front=max((f for f in e.faces if len(f['idx'])==3),key=lambda f:e.face_center(f)[1]);e.faces.remove(front)
        ids=front['idx'];pts=[e.verts[i] for i in ids];c=tuple(sum(p[k] for p in pts)/3 for k in range(3))
        inside=[e.add_vert(tuple(c[k]+.65*(p[k]-c[k]) for k in range(3))) for p in pts]
        e.add_face(inside,ear)
        for j in range(3):e.add_face([ids[j],ids[(j+1)%3],inside[(j+1)%3],inside[j]],fur)
        meshes.append(e)
    # Closed low-sided bent tail attached behind the torso.
    tailrings=[]
    for x,y,z,r in [(.12,-.19,.59,.11),(.28,-.39,.65,.105),(.43,-.49,.82,.10),(.48,-.49,1.05,.095),(.44,-.47,1.24,.08),(.33,-.44,1.33,.06)]:
        tailrings.append([(x+r*math.cos(j*math.tau/6),y+r*math.sin(j*math.tau/6),z) for j in range(6)])
    tail=loft(tailrings,fur,name='tail',smooth=False)
    if tabby:tail.remap_material(lambda c,m:stripe if int(c[2]*12)%3==0 else m)
    meshes.append(tail)
    batches,names=bake(meshes,Lighting(.72,.28),model=model);record=write_l643(folder/'scene.l64',batches,names)
    record.update(format='L643',model=slug,appearance=slug,pose='standing',actors=[dict(name=slug,model=model)],textures=names)
    (folder/'scene.json').write_text(json.dumps(record,indent=2)+'\n');(folder/'complete.json').write_text(json.dumps(dict(model=slug,appearances=[slug]))+'\n')
    dump_json(folder/'authoring.json',meshes,[dict(name=n,width=32,height=32) for n in names])
    check=audit(folder/'scene.l64');check['head_topology']=proof
    (folder/'audit.json').write_text(json.dumps(check,indent=2)+'\n')
    assert check['passed'],check['errors'];return check

def main():
    p=argparse.ArgumentParser();p.add_argument('--round',type=int,required=True);p.add_argument('--publish',action='store_true');a=p.parse_args()
    out=STUDY/'rounds'/f'i{a.round}';out.mkdir(parents=True,exist_ok=False)
    shutil.copyfile(__file__,out/'build_cats.py');checks=[]
    for slug,model,tabby in [('cat_tuxedo',200,False),('cat_tabby',201,True)]:
        checks.append(build(slug,model,tabby,out/slug,a.round))
        if a.publish:shutil.copytree(out/slug,ROOT/'port/characters/assets/models'/slug,dirs_exist_ok=True)
    (out/'audit.json').write_text(json.dumps(dict(passed=True,models=checks),indent=2)+'\n')
    print(out);print([(c['triangles'],c['passed']) for c in checks])
if __name__=='__main__':main()
