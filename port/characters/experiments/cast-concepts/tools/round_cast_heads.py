#!/usr/bin/env python3
"""Shared connected cranial dome pass on retained authoring meshes.

Source selections are immutable. Body triangle streams retain their original
native positions, UVs and baked colors. Face-paint correction is a separate
follow-up: tools/correct_scientist_faces.py.
"""
import argparse,collections,copy,hashlib,json,math,shutil,struct,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT.parent/'n64-style/tools'))
sys.path.insert(0,str(ROOT.parent/'mark-concepts/tools'))
from geom import Mesh,Material,Lighting,bake,dump_json,write_l643
from audit_exports import audit

def clip_planes(mesh,planes):
    """Cut every crossed owning-shell face with shared edge intersections."""
    for height in planes:
        cache={};faces=[]
        def side(face,above):
            out=[];uv=[];ids=face['idx'];coords=face['uv']
            for k,ia in enumerate(ids):
                ib=ids[(k+1)%len(ids)];a=mesh.verts[ia];b=mesh.verts[ib]
                inside=lambda p:p[2]>=height if above else p[2]<=height
                if inside(a):
                    out.append(ia)
                    if coords:uv.append(coords[k])
                if inside(a)!=inside(b):
                    t=(height-a[2])/(b[2]-a[2]);edge=tuple(sorted((ia,ib)))
                    if edge not in cache:cache[edge]=mesh.add_vert(tuple(a[j]+t*(b[j]-a[j]) for j in range(3)))
                    out.append(cache[edge])
                    if coords:uv.append(tuple(coords[k][j]+t*(coords[(k+1)%len(ids)][j]-coords[k][j]) for j in range(2)))
            return dict(face,idx=out,uv=uv if coords else None)
        for face in mesh.faces:
            zz=[mesh.verts[i][2] for i in face['idx']]
            if min(zz)<height<max(zz):
                for above in (False,True):
                    f=side(face,above)
                    if len(f['idx'])>=3:faces.append(f)
            else:faces.append(face)
        mesh.faces=faces

def section(mesh,height):
    # Include local extrema and horizontal ring vertices. Half-open edge
    # intersections alone omit spikes/apices at exactly the queried height.
    points=[tuple(p[:2]) for p in mesh.verts if abs(p[2]-height)<1e-8]
    for face in mesh.faces:
        ids=face['idx']
        for k,ia in enumerate(ids):
            a=mesh.verts[ia];b=mesh.verts[ids[(k+1)%len(ids)]]
            if min(a[2],b[2])<=height<max(a[2],b[2]):
                t=(height-a[2])/(b[2]-a[2]);points.append(tuple(a[j]+t*(b[j]-a[j]) for j in (0,1)))
    if not points:return None
    return tuple((min(p[j] for p in points),max(p[j] for p in points)) for j in (0,1))

def round_head(mesh,slug,cuts=(.65,.82),phase=(0,0,0),preserve_lower=False):
    if slug=='mark':
        for i,(x,y,z) in enumerate(mesh.verts):
            angle=abs(math.atan2(x,y))
            weight=min(1.0,angle/(math.pi/4)) if y>0 else 1.0
            mesh.verts[i]=(x,y*(1+.10*weight),z)
        for face in mesh.faces:
            if face['uv'] and face['mat'].texture and 'face' in face['mat'].texture:
                face['uv']=[(max(0,min(1,(u-.5)/.76+.5)),v) for u,v in face['uv']]
        return dict(rear_side_depth_scale=1.10,nose_extension=False,front_face_uv_span=.76,cap_bill_same_transform=True)
    facevertices=[mesh.verts[i] for f in mesh.faces if f['mat'].texture and 'face' in f['mat'].texture for i in f['idx'] if mesh.verts[i][1]>0]
    lo=min(p[2] for p in facevertices) if facevertices else min(p[2] for p in mesh.verts)
    hi=max(p[2] for p in mesh.verts);height=hi-lo
    clip_planes(mesh,[lo+height*t for t in cuts])
    original=copy.deepcopy(mesh)
    # Mark A3d's broad cheek/temple envelope, with an explicit rounded dome.
    mid=section(original,lo+height*.5);halfwidth=(mid[0][1]-mid[0][0])/2
    anatomical_height=min(height,(max(p[2] for p in facevertices)+.28-lo) if facevertices else height)
    target_width=max(halfwidth*(1.10 if slug in ('dr_mason','tech') else 1.0),.405*anatomical_height/.77)
    target_depth=max(.335*anatomical_height/.77,(mid[1][1]-mid[1][0])/2)
    planes={round(p[2],9):section(original,min(hi-1e-8,max(lo+1e-8,p[2]))) for p in original.verts}
    for i,(x,y,z) in enumerate(original.verts):
        t=(z-lo)/height;bounds=planes[round(z,9)]
        if not bounds:continue
        cx=sum(bounds[0])/2;rx=(bounds[0][1]-bounds[0][0])/2
        if rx<1e-6:continue
        # Keep the jaw's identity; broaden cheeks and temples without a new shell.
        base=1.10 if slug in ('dr_mason','tech') else 1.0
        dome=math.sqrt(max(0,1-((t-.48)/.52)**2)) if t>=.48 else 1
        wanted=target_width*dome
        sx=max(base if t<.60 else 1.0,wanted/rx if t>=.38 else 1.0)
        if preserve_lower and t<.58:sx=1.0
        # The rear skull must bow outward above the nape, not form a flat slab.
        # Preserve the facial/nose projection below the upper forehead.
        rear=-target_depth*dome-.025
        if (.58 if preserve_lower else .35)<t<.98 and y<0:
            oldrear=bounds[1][0]
            if oldrear<-.01 and rear<oldrear:y*=rear/oldrear
        if .63<t<.98 and y>0:
            oldfront=bounds[1][1];front=target_depth*dome-.015
            if oldfront>.01 and front>oldfront:y*=front/oldfront
        mesh.verts[i]=(cx+(x-cx)*sx,y,z)
    if slug in ('dr_mason','tech'):
        tip=max((p for p in original.verts if abs(p[0])<1e-5 and p[1]>0),key=lambda p:p[1])
        desired=.350 if slug=='dr_mason' else .345;reduction=max(0,tip[1]-desired)
        for i,(x,y,z) in enumerate(mesh.verts):
            if y>0:
                weight=max(0,1-abs(z-tip[2])/.12)*max(0,1-abs(x)/.14)
                mesh.verts[i]=(x,y-reduction*weight,z)
    if any(phase):mesh.verts=[tuple(p[j]+phase[j] for j in range(3)) for p in mesh.verts]
    # Choose a polygon fan that survives the actual native 1/32 packing rule.
    # This changes diagonals, never removes a face or an exported triangle.
    def score(ids):
        points=[]
        for i in ids:
            x,y,z=mesh.verts[i];px,py,pz=mesh.pivot
            points.append(tuple(math.trunc(struct.unpack('f',struct.pack('f',c*32))[0]) for c in (x-px,z-pz,y-py)))
        areas=[]
        for k in range(1,len(points)-1):
            a,b,c=points[0],points[k],points[k+1];u=[b[j]-a[j] for j in range(3)];v=[c[j]-a[j] for j in range(3)]
            areas.append(sum(q*q for q in (u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0])))
        return min(areas)
    for face in mesh.faces:
        ids=face['idx'];k=max(range(len(ids)),key=lambda k:score(ids[k:]+ids[:k]))
        face['idx']=ids[k:]+ids[:k]
        if face['uv']:face['uv']=face['uv'][k:]+face['uv'][:k]
    return dict(cuts=list(cuts),preserve_lower=preserve_lower,packing_phase=list(phase),target_half_width=target_width,target_half_depth=target_depth,head_bounds=[lo,hi])

def hydrate(entry):
    m=Mesh(entry['name'],entry['part'],entry['pivot']);m.verts=[tuple(v) for v in entry['verts']]
    mats={a['name']:Material(a['name'],a['rgb'],a['texture'],a.get('lit',True)) for a in entry['materials']};group=Mesh.new_group()
    for f in entry['faces']:m.add_face(f['idx'],mats[f['mat']],f['uv'],group if f['smooth'] else None)
    return m

def signature(points):return tuple(sorted(tuple(round(c,5) for c in p) for p in points))

def read_native(path):
    data=path.read_bytes();_,nb,nt=struct.unpack_from('>4sHH',data);o=8;names=[]
    for _ in range(nt):names.append(data[o:o+32].split(b'\0')[0].decode().removesuffix('.sprite'));o+=32
    batches=[]
    for _ in range(nb):
        model,part,tex,_,nv,ni,*p=struct.unpack_from('>HHhHII3f',data,o);o+=28
        vs=[struct.unpack_from('>5f4B',data,o+i*24) for i in range(nv)];o+=nv*24
        ids=list(struct.unpack_from('>'+str(ni)+'H',data,o));o=(o+ni*2+3)&~3
        batches.append(((1,part,tex,(p[0],p[2],p[1])),vs,ids))
    assert o==len(data);return names,batches

def native_body(source,originalheads):
    signatures=set()
    for m in originalheads:
        for f in m.faces:
            for k in range(1,len(f['idx'])-1):
                points=[]
                for i in (f['idx'][0],f['idx'][k],f['idx'][k+1]):
                    x,y,z=m.verts[i];px,py,pz=m.pivot;points.append((x-px,z-pz,y-py))
                signatures.add(signature(points))
    names,records=read_native(source);body={};removed=0
    for key,vs,ids in records:
        keep=[]
        for k in range(0,len(ids),3):
            tri=ids[k:k+3]
            if signature([vs[i][:3] for i in tri]) in signatures:removed+=1
            else:keep.extend(tri)
        if keep:body[key]=(vs,keep,{})
    expected=sum(len(f['idx'])-2 for m in originalheads for f in m.faces)
    assert removed==expected,('head triangle match',removed,expected)
    return names,body,removed

def topology(mesh):
    edges=collections.Counter();oriented=collections.Counter();hair=[];owners=collections.defaultdict(list)
    for f in mesh.faces:
        for k,a in enumerate(f['idx']):
            b=f['idx'][(k+1)%len(f['idx'])];key=tuple(sorted((a,b)));edges[key]+=1;oriented[key]+=1 if a<b else -1
        if 'hair' in f['mat'].name or 'scalp' in f['mat'].name:
            i=len(hair);hair.append(f)
            for k,a in enumerate(f['idx']):owners[tuple(sorted((a,f['idx'][(k+1)%len(f['idx'])])))].append(i)
    adjacency=[set() for f in hair]
    for ids in owners.values():
        for i in ids:adjacency[i].update(set(ids)-{i})
    seen=set();components=0
    for i in range(len(hair)):
        if i in seen:continue
        components+=1;pending=[i]
        while pending:
            j=pending.pop()
            if j in seen:continue
            seen.add(j);pending.extend(adjacency[j]-seen)
    report=dict(closed=all(n==2 for n in edges.values()),oriented=all(n==0 for n in oriented.values()),hair_components=components)
    assert report['closed'] and report['oriented'] and components<=1,report
    return report

def source_wrapper(slug):
    return """# Reproduce geometry into a NEW revision, preserving the retained review artifact.
import json,runpy,sys
from pathlib import Path
folder=Path(__file__).resolve().parent
tool=folder.parents[2]/'tools/round_cast_heads.py'
setting=json.loads((folder/'audit.json').read_text())['settings'][0]
args=[str(tool),SLUG,'--revision',sys.argv[1] if len(sys.argv)>1 else 'i3e-rebuilt']
if 'cuts' in setting:args+=['--cuts',*map(str,setting['cuts'])]
args+=['--phase',*map(str,setting.get('packing_phase',[0,0,0]))]
if setting.get('preserve_lower'):args+=['--preserve-lower']
sys.argv=args
runpy.run_path(str(tool),run_name='__main__')
new_revision=args[args.index('--revision')+1]
uvtool=tool.with_name('front_face_uv.py')
sys.argv=[str(uvtool),SLUG,'--revision',new_revision]
runpy.run_path(str(uvtool),run_name='__main__')
# Scientist paint is a separate correction; other source PNGs remain unchanged.
""".replace('SLUG',repr(slug))

def build(slug,revision,phase=(0,0,0),preserve_lower=False,cuts_override=None):
    rows=json.loads((ROOT/'review/roundness-sources.json').read_text())
    source=ROOT/rows[slug]['prefix']/slug;
    assert hashlib.sha256((source/'scene.l64').read_bytes()).hexdigest()==rows[slug]['asset_sha256']
    assert hashlib.sha256((source/'authoring.json').read_bytes()).hexdigest()==rows[slug]['authoring_sha256']
    folder=ROOT/'models'/slug/revision;target=folder/slug
    assert not folder.exists(),folder
    folder.mkdir(parents=True);target.mkdir();shutil.copytree(source/'textures',target/'textures')
    doc=json.loads((source/'authoring.json').read_text());original=[hydrate(m) for m in doc['meshes']];heads=[m for m in original if 'head' in m.name or (slug=='mark' and 'bill' in m.name)]
    assert heads
    names,body,removed=native_body(source/'scene.l64',heads)
    attempts=[]
    choices=[tuple(cuts_override)] if cuts_override is not None else [*((a,b) for a in (.66,.67,.68,.69,.70,.64,.65,.62,.60) for b in (.83,.84,.85,.86,.87,.88,.82,.80,.78,.76)),(.83,),(.80,),(.76,),(.88,),()]
    for cuts in choices:
        changed=copy.deepcopy(heads);settings=[round_head(m,slug,cuts,phase,preserve_lower) for m in changed]
        try:proof=[topology(m) for m in changed]
        except AssertionError as error:
            attempts.append(dict(cuts=cuts,errors=[str(error)]));continue
        baked,headnames=bake(changed,Lighting(.78,.22,(-.3,.6,.75)),model=1);textures=list(names);result=copy.deepcopy(body)
        for key,(vs,ids,cache) in baked.items():
            model,part,tex,pivot=key
            if tex>=0:
                name=headnames[tex]
                if name not in textures:textures.append(name)
                tex=textures.index(name)
            key=(model,part,tex,pivot)
            if key in result:
                old,indices,_=result[key];base=len(old);old.extend(vs);indices.extend(i+base for i in ids)
            else:result[key]=(vs,ids,{})
        stats=write_l643(target/'scene.l64',result,textures);check=audit(target/'scene.l64');attempts.append(dict(cuts=cuts,errors=check['errors']))
        if check['passed']:break
    assert check['passed'],attempts
    exported={key:(vs,ids) for key,vs,ids in read_native(target/'scene.l64')[1]}
    for key,(vertices,indices,_) in body.items():
        newvertices,newindices=exported[key]
        assert newvertices[:len(vertices)]==vertices,('body vertex bytes changed',key)
        assert newindices[:len(indices)]==indices,('body triangle indices changed',key)
    meshes=[next((h for h in changed if h.name==m.name),m) for m in original]
    dump_json(target/'authoring.json',meshes,dict(names=textures))
    meta=dict(format='L643',parts=list(range(5)),textures=textures,**stats);(target/'scene.json').write_text(json.dumps(meta,indent=2)+'\n')
    report=dict(passed=True,source=str(source.relative_to(ROOT)),source_sha256=hashlib.sha256((source/'scene.l64').read_bytes()).hexdigest(),settings=settings,topology=proof,original_head_triangles=removed,body_native_triangle_stream_unchanged=True,attempts=attempts,native=check,face_paint='Original retained; follow-up tools/correct_scientist_faces.py')
    (folder/'audit.json').write_text(json.dumps(report,indent=2)+'\n');(folder/'option.json').write_text(json.dumps(dict(id=slug+'_'+revision,label=slug+' round dome',iteration=revision))+'\n')
    (folder/'source.py').write_text(source_wrapper(slug))
    print(slug,stats,'closed owning head; body native triangles preserved')
    return folder

def derive_amy_lying(revision='i3e'):
    source=ROOT/'models/amy'/revision/'amy';folder=ROOT/'models/amy__lying'/revision;target=folder/'amy__lying'
    assert not folder.exists(),folder
    target.mkdir(parents=True);shutil.copytree(source/'textures',target/'textures')
    names,records=read_native(source/'scene.l64');batches={}
    for (model,part,tex,pivot),vs,ids in records:
        px,py,pz=pivot
        # Authoring pivots are X/south/up; native vertex local axes are X/up/south.
        newpivot=(px,.9-pz,py+.35)
        vertices=[(v[0],v[2],-v[1],*v[3:]) for v in vs]
        batches[(model,part,tex,newpivot)]=(vertices,ids,{})
    stats=write_l643(target/'scene.l64',batches,names)
    doc=json.loads((source/'authoring.json').read_text())
    for mesh in doc['meshes']:
        mesh['verts']=[(x,.9-z,y+.35) for x,y,z in mesh['verts']]
        x,y,z=mesh['pivot'];mesh['pivot']=(x,.9-z,y+.35)
    (target/'authoring.json').write_text(json.dumps(doc)+'\n')
    (target/'scene.json').write_text(json.dumps(dict(format='L643',parts=list(range(5)),textures=names,**stats),indent=2)+'\n')
    check=audit(target/'scene.l64');assert check['passed'],check
    _,result=read_native(target/'scene.l64');count=0
    for (key,vs,ids),(newkey,newvs,newids) in zip(records,result):
        assert key[:3]==newkey[:3] and ids==newids
        for a,b in zip(vs,newvs):
            assert a[3:]==b[3:]
            ap=key[3];bp=newkey[3]
            world=(a[0]+ap[0],a[1]+ap[2],a[2]+ap[1]);actual=(b[0]+bp[0],b[1]+bp[2],b[2]+bp[1])
            assert all(abs(x-y)<1e-6 for x,y in zip(actual,(world[0],world[2]+.35,.9-world[1])))
            count+=1
    (folder/'audit.json').write_text(json.dumps(dict(passed=True,derived_from='models/amy/'+revision+'/amy',exact_rigid_native_pose=True,vertices_checked=count,UV_colors_indices_parts_textures_preserved=True,native=check),indent=2)+'\n')
    (folder/'option.json').write_text(json.dumps(dict(id='amy__lying_'+revision,label='Amy lying rounded head'))+'\n')
    (folder/'source.py').write_text('# Derived by tools/round_cast_heads.py derive_amy_lying from new standing Amy.\n')
    print('amy__lying',stats,'exact derived native transform')
    return folder

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('slugs',nargs='+');p.add_argument('--revision',default='i3e');p.add_argument('--cuts',type=float,nargs='*');p.add_argument('--preserve-lower',action='store_true');p.add_argument('--phase',type=float,nargs=3,default=(0,0,0));a=p.parse_args()
    for slug in a.slugs:
        if slug=='amy__lying':derive_amy_lying(a.revision)
        else:build(slug,a.revision,a.phase,a.preserve_lower,a.cuts)
