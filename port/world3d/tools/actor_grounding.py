"""Remove preview shadows from runtime actors using authored mesh ownership.

Named/tagged shadow faces are excluded regardless of shape, size or color.
Exact legacy signatures also migrate preexisting runtime/LOD files. Vertex
records, rig data and every non-shadow triangle remain unchanged.
"""
from collections import Counter
import math
import struct
import hashlib
import json

STANDING=((.43,.32,.018),(.47,.32,.018),(.45,.32,.018),
          (.405,.2944,.01692),(.504,.3296,.01836),(.558,.352,.01692),
          (.423,.2944,.018),(.378,.272,.01692),(.4815,.3264,.01602),(.459,.32,.01584))
EPS=1e-6

def authored_shadow_keys(authoring):
    """Use the authoring ownership tag, independent of disk shape or color."""
    keys=Counter()
    for mesh in authoring.get('meshes',[]):
        if mesh.get('role')!='shadow' and 'shadow' not in mesh['name'].lower():continue
        px,py,pz=mesh['pivot']
        points=[(round(x-px,5),round(z-pz,5),round(y-py,5)) for x,y,z in mesh['verts']]
        pivot=tuple(round(c,5) for c in (px,pz,py))
        for face in mesh['faces']:
            ids=face['idx']
            for k in range(1,len(ids)-1):
                keys[(mesh['part'],pivot,tuple(sorted(points[i] for i in (ids[0],ids[k],ids[k+1]))))]+=1
    return keys

def native_triangle_key(batch,offset):
    return (batch[1],tuple(round(c,5) for c in batch[3]),
            tuple(sorted(tuple(round(c,5) for c in batch[4][i][:3]) for i in batch[5][offset:offset+3])))

def strip_authored_shadows(meshes,authoring):
    """Strip explicitly owned shadow faces and fail if source ownership drifts."""
    if authoring is None:return strip_legacy_disks(meshes)
    expected=authored_shadow_keys(authoring);found=Counter();output=[]
    for batch in meshes:
        kept=[]
        for k in range(0,len(batch[5]),3):
            key=native_triangle_key(batch,k)
            if key in expected:found[key]+=1
            else:kept.extend(batch[5][k:k+3])
        output.append((*batch[:5],type(batch[5])(kept)))
    assert found==expected, 'Authored shadow geometry does not match export; rebuild the source before publishing'
    output,legacy=strip_legacy_disks(output)
    return output,dict(authored_triangles=sum(found.values()),legacy=legacy)

def _near(a,b):return all(abs(x-y)<EPS for x,y in zip(a,b))

def _signature(batch,profile,lying=False):
    model,part,texture,pivot,vertices,indices=batch
    if model<=0 or part!=0 or texture!=-1:return None
    if not _near(pivot,(0,.35,.9) if lying else (0,0,0)):return None
    rx,rz,height=profile;rgba=(11,5,23,255) if lying else (13,6,27,255)
    ring=[]
    for i in range(8):
        x,z=rx*math.cos(i*math.pi/4),rz*math.sin(i*math.pi/4)
        expected=(x,z,-height) if lying else (x,height,z)
        matches=[j for j,v in enumerate(vertices) if tuple(v[5:])==rgba and _near(v[:3],expected)]
        if len(matches)!=1:return None
        ring.append(matches[0])
    owned=set(ring)
    triangles=[k for k in range(0,len(indices),3) if set(indices[k:k+3])<=owned]
    if len(triangles)!=6:return None
    if any(set(indices[k:k+3])&owned and k not in triangles for k in range(0,len(indices),3)):return None
    edges=Counter();surfaces=set();signs=set();area=0
    axes=(0,1) if lying else (0,2)
    for k in triangles:
        ids=indices[k:k+3]
        if len(set(ids))!=3:return None
        surface=tuple(sorted(ids))
        if surface in surfaces:return None
        surfaces.add(surface)
        a,b,c=[vertices[j] for j in ids];u,v=axes
        cross=(b[u]-a[u])*(c[v]-a[v])-(b[v]-a[v])*(c[u]-a[u])
        if abs(cross)<EPS:return None
        area+=abs(cross)/2;signs.add(cross>0)
        for x,y in zip(ids,ids[1:]+ids[:1]):edges[tuple(sorted((x,y)))]+=1
    boundary={tuple(sorted((ring[i],ring[(i+1)%8]))) for i in range(8)}
    if len(signs)!=1 or {e for e,n in edges.items() if n==1}!=boundary:return None
    if any(n not in (1,2) for n in edges.values()) or abs(area-2*math.sqrt(2)*rx*rz)>EPS:return None
    return triangles

def _reviewed_signature(batch,lying=False):
    """Exact 12-sided center fan used by the reviewed cast, not shoe soles."""
    model,part,texture,pivot,vertices,indices=batch
    if model<=0 or part!=0 or texture!=-1:return None
    if not _near(pivot,(0,.35,.9) if lying else (0,0,0)):return None
    points=[(0,0)]+[(.45*math.cos(i*math.pi/6),.32*math.sin(i*math.pi/6)) for i in range(12)]
    owned=[]
    for x,z in points:
        expected=(round(x,5),round(z,5),-.018) if lying else (round(x,5),.018,round(z,5))
        matches=[i for i,v in enumerate(vertices) if tuple(v[5:])==(16,8,24,255) and _near(v[:3],expected)]
        if len(matches)!=1:return None
        owned.append(matches[0])
    center,*ring=owned;owned=set(owned)
    triangles=[k for k in range(0,len(indices),3) if set(indices[k:k+3])<=owned]
    if len(triangles)!=12:return None
    if any(set(indices[k:k+3])&owned and k not in triangles for k in range(0,len(indices),3)):return None
    expected={tuple(sorted((center,ring[i],ring[(i+1)%12]))) for i in range(12)}
    if {tuple(sorted(indices[k:k+3])) for k in triangles}!=expected:return None
    signs=set();u,v=(0,1) if lying else (0,2)
    for k in triangles:
        a,b,c=[vertices[i] for i in indices[k:k+3]]
        cross=(b[u]-a[u])*(c[v]-a[v])-(b[v]-a[v])*(c[u]-a[u])
        if abs(cross)<EPS:return None
        signs.add(cross>0)
    return triangles if len(signs)==1 else None

def strip_legacy_disks(meshes,models=None):
    output=[];removed=[]
    for batch in meshes:
        match=None;kind=None
        if models is None or batch[0] in models:
            for profile in STANDING:
                match=_signature(batch,profile)
                if match is not None:kind='standing';break
            if match is None:
                match=_signature(batch,(.378,.272,.01692),lying=True)
                if match is not None:kind='amy-lying'
            if match is None:
                for lying in (False,True):
                    match=_reviewed_signature(batch,lying)
                    if match is not None:
                        kind='reviewed-lying' if lying else 'reviewed-standing';break
        if match is None:output.append(batch);continue
        forbidden=set(match);indices=type(batch[5])(i for k in range(0,len(batch[5]),3) if k not in forbidden for i in batch[5][k:k+3])
        assert len(indices)==len(batch[5])-3*len(match)
        output.append((*batch[:5],indices))
        removed.append({'model':batch[0],'part':batch[1],'kind':kind,'triangles':len(match),'newly_unreferenced_vertices':13 if kind.startswith('reviewed') else 8})
    return output,removed

def encode_l643(textures,meshes):
    data=bytearray(struct.pack('>4sHH',b'L643',len(meshes),len(textures)))
    for name in textures:data.extend(name.encode().ljust(32,b'\0'))
    for model,part,texture,pivot,vertices,indices in meshes:
        data.extend(struct.pack('>HHhHII3f',model,part,texture,0,len(vertices),len(indices),*pivot))
        for vertex in vertices:data.extend(struct.pack('>5f4B',*vertex))
        data.extend(struct.pack('>'+str(len(indices))+'H',*indices));data.extend(bytes((-len(data))%4))
    return bytes(data)

def finish_runtime_grounding(out,doc,previous):
    """Keep old LOD geometry exact, and never bless an unrelated stale LOD."""
    from build_world import read_l643
    def sha(data):return hashlib.sha256(data).hexdigest()
    def write(path,data):
        if path.read_bytes()!=data:path.write_bytes(data)
    fallback=out/'actors.l64'
    if fallback.exists():
        names,meshes=read_l643(fallback);clean,removed=strip_legacy_disks(meshes,{1,2,3,4})
        if removed:write(fallback,encode_l643(names,clean))
        if 'actors' in doc.get('meshes',{}):doc['meshes']['actors']['triangles']=sum(len(m[5])//3 for m in clean)
    manifest=out/'character-lods.json'
    if not manifest.exists():return
    lods=json.loads(manifest.read_text());current={c['model']:c for c in doc['individual_characters']}
    for record in lods['models']:
        character=current.get(record['model'])
        if not character:continue
        source,target=out/character['file'],out/record['file']
        if not source.exists() or not target.exists():continue
        source_data=source.read_bytes();target_data=target.read_bytes()
        if sha(target_data)!=record['sha256']:continue
        prior=previous.get(character['file'])
        same=sha(source_data)==record['source_sha256']
        # read_l643 takes a path; previous holds its parsed meshes too.
        surgical=prior and sha(prior[0])==record['source_sha256'] and encode_l643(prior[1],strip_legacy_disks(prior[2])[0])==source_data
        if not(same or surgical):continue
        names,meshes=read_l643(target);clean,_=strip_legacy_disks(meshes)
        data=encode_l643(names,clean);write(target,data)
        _,full=read_l643(source)
        record.update(source_sha256=sha(source_data),sha256=sha(data),
                      full_triangles=sum(len(m[5])//3 for m in full),lod_triangles=sum(len(m[5])//3 for m in clean),
                      full_vertices=sum(len(m[4]) for m in full),lod_vertices=sum(len(m[4]) for m in clean))
    write(manifest,(json.dumps(lods,indent=2)+'\n').encode())
