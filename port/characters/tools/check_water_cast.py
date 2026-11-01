#!/usr/bin/env python3
"""Check actual Water cast/player exports without writing canonical assets.

python3 tools/check_water_cast.py --assets /path/to/assets [mark amy joshua swimmer]
Only --report writes an artifact. Default assets follow POKETCG_CHARACTER_ASSETS.
"""
import argparse
from collections import Counter,defaultdict
import hashlib
import json
import math
import os
from pathlib import Path
import sys
import struct

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import validate
from designs import BODIES,DESIGNS
from skin import image_rows,features_digest

FAMILIES=('mark','amy','joshua','swimmer')
EPS=1e-6


def sub(a,b):return tuple(x-y for x,y in zip(a,b))
def dot(a,b):return sum(x*y for x,y in zip(a,b))
def cross(a,b):return (a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0])
def length(a):return math.sqrt(dot(a,a))
def packed_position(value):
    # scene_packed.inc reads IEEEfloat32 L643 coordinates RELATIVE TO each
    # rigid pivot, multiplies by32.0f, then truncf() toward zero. Pivot/model
    # matrices apply later; rounding world coordinates is a different result.
    scaled=struct.unpack('f',struct.pack('f',value*32.0))[0]
    packed=math.trunc(scaled)
    assert -32768<=packed<=32767,'packed coordinate overflow'
    return packed/32


def point(v,grid=None):return tuple(packed_position(c) if grid else round(c,6) for c in v[:3])


def triangles(batches,grid=None,world=False):
    result=[]
    for batch in batches:
        for offset in range(0,len(batch['indices']),3):
            vs=[batch['vertices'][i] for i in batch['indices'][offset:offset+3]]
            ps=[point(v,grid) for v in vs]
            if world:ps=[tuple(p[i]+batch['pivot'][i] for i in range(3)) for p in ps]
            result.append(dict(points=ps,vertices=vs,part=batch['part'],pivot=tuple(batch['pivot']),texture=batch['texture']))
    return result


def components_and_topology(records):
    """Weld material seams geometrically and check every oriented edge."""
    edges=defaultdict(list)
    for i,tri in enumerate(records):
        ps=tri['points'];assert len(set(ps))==3,('collapsed triangle',ps)
        assert length(cross(sub(ps[1],ps[0]),sub(ps[2],ps[0])))>1e-8,('degenerate triangle',ps)
        for a,b in zip(ps,ps[1:]+ps[:1]):edges[tuple(sorted((a,b)))].append((i,a,b))
    assert all(len(uses)==2 for uses in edges.values()),('open/nonmanifold edges',Counter(len(u) for u in edges.values()))
    neighbors=defaultdict(set)
    for uses in edges.values():
        (i,a,b),(j,c,d)=uses
        assert a==d and b==c,('inconsistent winding',a,b,c,d)
        neighbors[i].add(j);neighbors[j].add(i)
    unseen=set(range(len(records)));components=[]
    while unseen:
        pending=[unseen.pop()];group=[]
        while pending:
            i=pending.pop();group.append(i)
            for j in neighbors[i]:
                if j in unseen:unseen.remove(j);pending.append(j)
        volume=sum(dot(records[i]['points'][0],cross(records[i]['points'][1],records[i]['points'][2]))/6 for i in group)
        assert volume>1e-6,('inward or empty component',volume)
        components.append(dict(indices=group,triangles=len(group),signed_volume=volume))
    return components,len(edges)


def winding(records,p):
    total=0
    for tri in records:
        a,b,c=[sub(v,p) for v in tri['points']];la,lb,lc=map(length,(a,b,c))
        total+=2*math.atan2(dot(a,cross(b,c)),la*lb*lc+dot(a,b)*lc+dot(b,c)*la+dot(c,a)*lb)
    return total/(4*math.pi)


def exterior_sides(records):
    """Every main-skull triangle must separate its own exterior/interior."""
    outside=0;inside=1
    for tri in records:
        a,b,c=tri['points'];normal=cross(sub(b,a),sub(c,a));size=length(normal)
        assert size>1e-8
        center=tuple((a[i]+b[i]+c[i])/3 for i in range(3))
        # Below the smallest triangle's altitude, avoiding nearby boundaries.
        altitude=size/max(length(sub(a,b)),length(sub(b,c)),length(sub(c,a)))
        delta=min(.0001,altitude/12)
        out=abs(winding(records,tuple(center[i]+normal[i]*delta/size for i in range(3))))
        within=abs(winding(records,tuple(center[i]-normal[i]*delta/size for i in range(3))))
        outside=max(outside,out);inside=min(inside,within)
        assert out<.05 and within>.95,('nonexterior skull face',out,within)
    return dict(maximum_outside_winding=outside,minimum_inside_winding=inside)


def area2(poly):return abs(sum(a[0]*b[1]-a[1]*b[0] for a,b in zip(poly,poly[1:]+poly[:1])))/2

def _cross2(a,b,c):return (b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0])

def _clip2(subject,clip):
    clip=list(clip) if _cross2(*clip)>0 else list(reversed(clip))
    for a,b in zip(clip,clip[1:]+clip[:1]):
        if not subject:break
        out=[];prev=subject[-1];pd=_cross2(a,b,prev)
        for cur in subject:
            cd=_cross2(a,b,cur)
            if (pd>=-1e-9)!=(cd>=-1e-9):
                t=pd/(pd-cd);out.append(tuple(prev[i]+t*(cur[i]-prev[i]) for i in range(2)))
            if cd>=-1e-9:out.append(cur)
            prev,pd=cur,cd
        subject=out
    return subject


def surface_checks(records):
    """Catch duplicate and competing coplanar faces across material seams."""
    seen=set();planes=defaultdict(list)
    for tri in records:
        ps=tri['points'];key=(tri['part'],tri['pivot'],tuple(sorted(ps)))
        assert key not in seen,('duplicate triangle',key);seen.add(key)
        normal=cross(sub(ps[1],ps[0]),sub(ps[2],ps[0]));size=length(normal)
        assert size>1e-8,('packed degenerate',ps)
        normal=tuple(c/size for c in normal);distance=dot(normal,ps[0])
        dominant=max(range(3),key=lambda i:abs(normal[i]));axes=[i for i in range(3) if i!=dominant]
        # Keep orientation: opposite joint caps do not compete for an exterior.
        key=(tri['part'],tri['pivot'],tuple(round(c,4) for c in normal),round(distance,4))
        projected=[tuple(p[i] for i in axes) for p in ps]
        for other,old_points in planes[key]:
            if any(abs(dot(normal,p)-distance)>2e-5 for p in old_points):continue
            if any(max(p[i] for p in projected)<=min(p[i] for p in other)+1e-8 or
                   max(p[i] for p in other)<=min(p[i] for p in projected)+1e-8 for i in range(2)):continue
            overlap=_clip2(projected,other)
            assert len(overlap)<3 or area2(overlap)<2e-7,('competing coplanar faces',key,area2(overlap),ps,old_points)
        planes[key].append((projected,ps))
    return dict(triangles=len(records),duplicate_triangles=0,competing_coplanar_pairs=0,degenerate_triangles=0)


def _head_batches(batches):
    faces=[b for b in batches if b['texture']>=0]
    assert faces and all(b['part']==0 for b in faces),'missing part-0 face'
    pivots={tuple(b['pivot']) for b in faces};assert len(pivots)==1
    pivot=next(iter(pivots));assert any(abs(c)>1e-4 for c in pivot),'head still shares the body pivot'
    return [b for b in batches if b['part']==0 and tuple(b['pivot'])==pivot],pivot


def _profile(head_batches):
    records=triangles(head_batches);ps=[p for t in records for p in t['points']]
    spans=[max(p[i] for p in ps)-min(p[i] for p in ps) for i in range(3)]
    assert spans[0]>.55 and spans[1]>.50 and spans[2]>.45,('flat or undersized head',spans)
    assert .5<spans[2]/spans[0]<1.65,('flat profile',spans)
    # Use real central UV vertices: a convex nose must project beyond the
    # straight chin-to-forehead profile, rather than merely a deep rear skull.
    front=sorted({(round(v[1],6),round(v[2],6)) for b in head_batches if b['texture']>=0
                  for v in b['vertices'] if abs(v[3]-.5)<1e-5})
    assert len(front)>=4,('insufficient curved face rows',front)
    low,high=front[0],front[-1]
    bulge=max(x-(low[1]+(high[1]-low[1])*(h-low[0])/(high[0]-low[0])) for h,x in front[1:-1])
    assert bulge>.025,('no projecting nose/profile',bulge)
    uvs=[v[3] for b in head_batches if b['texture']>=0 for v in b['vertices']]
    assert max(uvs)-min(uvs)>=.70,('flat narrow face UV patch',min(uvs),max(uvs))
    return dict(width=spans[0],height=spans[1],depth=spans[2],nose_bulge=bulge,face_u_span=max(uvs)-min(uvs))


def _skin_and_textures(info,batches,assets):
    skin=info['skin_palette'];pigment=tuple(skin['rgb8']);levels={(v<<3)|(v>>2) for v in range(32)}
    assert all(v in levels for v in pigment)
    texture_hashes={}
    for name in info['textures']:
        texture_hashes[name]=hashlib.sha256((assets/'textures'/(name+'.png')).read_bytes()).hexdigest()
        width,height,rows=image_rows(assets/'textures'/(name+'.png'))
        assert width*height*2<=4096 and width&(width-1)==0 and height&(height-1)==0
        assert all(p[3]==255 for row in rows for p in row),'transparent cast texture'
    solids={v[5:8] for b in batches if b['texture']<0 for v in b['vertices']
            if all(abs(v[5+i]-pigment[i]*v[5]/pigment[0])<2 for i in (1,2))}
    assert len(solids)>5,'skin meshes missing'
    worst=0;count=0;light=(-.3,1,-.5);light_len=length(light)
    for face in skin['faces']:
        assert tuple(face['background_rgb8'])==pigment and face['colorspace']=='Non-Color' and face['directional_shade']
        width,height,rows=image_rows(assets/'textures'/(face['texture']+'.png'))
        assert width==height==32 and rows[0][0][:3]==pigment
        assert features_digest(rows,pigment+(255,))==face['feature_sha256'],'facial features changed'
        index=info['textures'].index(face['texture']);ambient=face.get('shade_floor',.68)
        assert .68<=ambient<=.9
        for tri in triangles([b for b in batches if b['texture']==index]):
            normal=cross(sub(tri['points'][1],tri['points'][0]),sub(tri['points'][2],tri['points'][0]))
            shade=ambient+(1-ambient)*max(0,dot(normal,light)/(length(normal)*light_len));expected=round(shade*255)
            for v in tri['vertices']:
                assert max(v[5:8])==min(v[5:8]) and abs(v[5]-expected)<=1,'unlit/mis-shaded face'
                painted=tuple(round(p*v[5]/255) for p in pigment)
                difference=min(max(abs(a-b) for a,b in zip(painted,c)) for c in solids)
                assert difference<=8,('face/limb pigment mismatch',difference)
                worst=max(worst,difference);count+=1
    assert count>0
    # The distal hands are unambiguously skin in all four new recipes. Check
    # them directly instead of allowing a matching nose/head colour to hide
    # a differently pigmented arm. Undo Amy's canonical lying transform first.
    sz=BODIES[DESIGNS[info['model']]['body']][2];hand_vertices=0
    for batch in batches:
        if batch['part'] not in (3,4):continue
        assert batch['texture']<0
        for v in batch['vertices']:
            world=tuple(v[i]+batch['pivot'][i] for i in range(3))
            height=.9-world[2] if info.get('pose')=='lying' else world[1]
            if height>.90*sz:continue
            shades=[v[5+i]/pigment[i] for i in range(3)]
            assert max(shades)-min(shades)<.025,('hand skin hue mismatch',v[5:8],pigment)
            assert min(shades)>=skin.get('shade_floor',.68)-.012 and max(shades)<=1.012,('hand skin pigment/shade mismatch',shades)
            assert v[8]==255
            hand_vertices+=1
    assert hand_vertices>=12,'distal skin samples missing'
    return dict(face_vertices=count,hand_vertices=hand_vertices,maximum_skin_difference=worst,textures=len(info['textures']),texture_sha256=texture_hashes,opaque=True)


def _rig(slug,batches,lying=False):
    sx,_,sz=BODIES[DESIGNS[slug]['body']]
    expected={0:[(0,0,0),(0,1.5*sz,0)],1:[(-.17*sx,.72*sz,0)],2:[(.17*sx,.72*sz,0)],
              3:[(-.40*sx,1.36*sz,0)],4:[(.40*sx,1.36*sz,0)]}
    if lying:expected={part:[(x,z+.35,.9-y) for x,y,z in ps] for part,ps in expected.items()}
    assert {b['part'] for b in batches}==set(range(5))
    for b in batches:
        assert any(all(abs(a-c)<2e-6 for a,c in zip(b['pivot'],p)) for p in expected[b['part']]),('changed animation pivot',slug,b['part'],b['pivot'])
    return dict(parts=5,expected_pivots=True)


def verify_export(slug,path,assets):
    validate.ASSETS=assets;batches=validate.read_mesh(path);info=json.loads(path.with_suffix('.json').read_text())
    assert {b['model'] for b in batches}=={info['runtime_model']},'wrong runtime actor model'
    assert sum(len(b['indices'])//3 for b in batches)==info['triangles']<=1000
    assert len(batches)==info['batches']<=12
    assert sum(len(b['vertices']) for b in batches)==info['vertices']
    raw=surface_checks(triangles(batches));packed=surface_checks(triangles(batches,32))
    head,pivot=_head_batches(batches);raw_head=triangles(head)
    components,edges=components_and_topology(raw_head)
    # Accessory solids can touch/intersect one another. Preserve their raw
    # component identities when checking packed topology; welding unrelated
    # goggle/bridge edges together would invent a nonmanifold-shell failure.
    packed_head=triangles(head,32);packed_edges=0;packed_components=[]
    for component in components:
        pieces,nedges=components_and_topology([packed_head[i] for i in component['indices']])
        assert len(pieces)==1,'packing split a closed head component'
        packed_components.extend(pieces);packed_edges+=nedges
    primary=[c for c in components if any(raw_head[i]['texture']>=0 for i in c['indices'])]
    assert len(primary)==1 and primary[0]['triangles']>=150,'missing single owning skull'
    main=[raw_head[i] for i in primary[0]['indices']];sides=exterior_sides(main)
    packed_sides=exterior_sides([packed_head[i] for i in primary[0]['indices']])
    lying=info.get('pose')=='lying'
    return dict(file=str(path),sha256=hashlib.sha256(path.read_bytes()).hexdigest(),triangles=info['triangles'],batches=len(batches),
        raw=raw,packed=packed,rig=_rig(slug,batches,lying),skin=_skin_and_textures(info,batches,assets),
        head=dict(pivot=pivot,components=len(components),closed_edges=edges,packed_closed_edges=packed_edges,
                  packed_components=len(packed_components),outward=True,main_triangles=len(main),packed_exterior=packed_sides,**sides),
        profile=None if lying else _profile(head)),batches


def _stream(batches,head=False):
    hb,pivot=_head_batches(batches)
    chosen=[b for b in batches if (b['part']==0 and tuple(b['pivot'])==pivot)==head]
    return [(b['part'],tuple(round(c,6) for c in b['pivot']),b['texture'],
             tuple(tuple(v[:5]) for i in b['indices'] for v in [b['vertices'][i]])) for b in chosen]


def verify_family(slug,assets):
    folder=assets/'models'/slug
    paths=[folder/'scene.l64']+sorted(folder.glob('poses/*/scene.l64'))+sorted(folder.glob('variants/*/scene.l64'))
    assert paths[0].exists(),paths[0]
    reports=[];exports=[]
    for path in paths:
        report,batches=verify_export(slug,path,assets);reports.append(report);exports.append(batches)
    special={}
    if slug=='amy':
        assert len(paths)==2 and paths[1].parent.name=='lying','Amy pose missing'
        # Sorting batches changes when the lying transform changes pivots.
        # Match the actual rigid group first, then compare expanded triangles.
        used=set()
        for original in exports[0]:
            x,y,z=original['pivot'];target=(x,z+.35,.9-y)
            candidates=[(i,b) for i,b in enumerate(exports[1]) if b['part']==original['part']
                        and b['texture']==original['texture']
                        and all(abs(a-c)<2e-6 for a,c in zip(b['pivot'],target))]
            assert len(candidates)==1,('lying pivot group missing',original['part'],target)
            index,posed=candidates[0];used.add(index)
            base=triangles([original],world=True);pose=triangles([posed],world=True)
            assert len(base)==len(pose)
            for a,b in zip(base,pose):
                for p,q,v,w in zip(a['points'],b['points'],a['vertices'],b['vertices']):
                    expected=(p[0],p[2]+.35,.9-p[1])
                    assert all(abs(x-y)<3e-6 for x,y in zip(expected,q)),('lying geometry drift',expected,q)
                    assert v[3:5]==w[3:5],'lying UV drift'
        assert len(used)==len(exports[1])
        special['lying_reuses_canonical_geometry']=True
    if slug=='swimmer':
        assert len(paths)==2 and paths[1].parent.name=='swimmer_palette2','Amanda appearance missing'
        info=json.loads(paths[1].with_suffix('.json').read_text());exception=info.get('appearance_geometry',{})
        assert exception.get('scope')=='head' and exception.get('shared_body')
        portraits=['src/gfx/duelists/sara.png','src/gfx/duelists/amanda.png']
        assert exception.get('source_portraits')==portraits
        assert all((ROOT.parents[1]/p).exists() for p in portraits)
        assert _stream(exports[0])==_stream(exports[1]),'Sara/Amanda body geometry/UV/rig drift'
        assert _stream(exports[0],True)!=_stream(exports[1],True),'Sara/Amanda heads not distinct'
        before=[v[5:] for b in exports[0] if b['texture']<0 and (b['part']!=0 or tuple(b['pivot'])==(0,0,0)) for i in b['indices'] for v in [b['vertices'][i]]]
        after=[v[5:] for b in exports[1] if b['texture']<0 and (b['part']!=0 or tuple(b['pivot'])==(0,0,0)) for i in b['indices'] for v in [b['vertices'][i]]]
        changes=sum(a!=b for a,b in zip(before,after));assert len(before)==len(after) and changes>20,'pool palette did not change'
        special=dict(shared_body_exact=True,distinct_head_geometry=True,body_color_vertices_changed=changes,
                     source_portraits={p:hashlib.sha256((ROOT.parents[1]/p).read_bytes()).hexdigest() for p in portraits})
    return dict(model=slug,passed=True,exports=reports,**special)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--assets',type=Path,default=Path(os.environ.get('POKETCG_CHARACTER_ASSETS',str(ROOT/'assets'))))
    parser.add_argument('--report',type=Path)
    parser.add_argument('slugs',nargs='*')
    args=parser.parse_args();assets=args.assets.resolve()
    if set(args.slugs)-set(FAMILIES):parser.error('Use mark, amy, joshua or swimmer')
    models=[verify_family(slug,assets) for slug in (args.slugs or FAMILIES)]
    report=dict(passed=True,assets=str(assets),packing='scene_packed.inc: float32 local coordinate times32, truncf towardzero; pivot/model matrix later',method='Actual L643 raw/packed surfaces, outward closed head components, profile/nose geometry, opaque TMEM textures, face/limb pigment, rig/pose and source-justified variant sharing.',models=models)
    if args.report:
        args.report.parent.mkdir(parents=True,exist_ok=True);args.report.write_text(json.dumps(report,indent=2)+'\n')
    print('WATER CAST PASS:',len(models),'families,',sum(len(m['exports']) for m in models),'exports')


if __name__=='__main__':main()
