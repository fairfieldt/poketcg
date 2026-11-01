"""Prove native headwear is one exterior, with no hidden scalp/head export.

Run in Blender background mode. Actual emitted L643 head triangles above the
brim must have winding0 outside and winding1 inside the unified closed head.
Original deep scalp/head vertices must not survive export. This uses final
native geometry; authored nonplanar cap quads are not an exterior reference.
"""
import argparse
import hashlib
import os
import json
import math
from pathlib import Path
import sys
import bpy
from mathutils import Vector

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import validate
from validate import read_mesh
ASSETS=Path(os.environ.get('POKETCG_CHARACTER_ASSETS',str(ROOT/'assets'))).resolve()
EPS=1e-5


def native_vertices(obj):
    result=[]
    for vertex in obj.data.vertices:
        point=obj.matrix_world@vertex.co
        result.append(Vector((point.x,point.z,-point.y)))
    return result


def clip(points,distance):
    if not points:return []
    clipped=[];previous=points[-1];a=distance(previous)
    for point in points:
        b=distance(point)
        if (a<=0)!=(b<=0):clipped.append(previous+(point-previous)*(-a/(b-a)))
        if b<=0:clipped.append(point)
        previous=point;a=b
    return clipped


def polygon_area(points):
    if len(points)<3:return 0
    return sum((points[i]-points[0]).cross(points[i+1]-points[0]).length/2 for i in range(1,len(points)-1))


def connected_components(data):
    neighbors={v.index:set() for v in data.vertices}
    uses={}
    for edge in data.edges:
        a,b=edge.vertices;neighbors[a].add(b);neighbors[b].add(a)
    for face in data.polygons:
        for a,b in zip(face.vertices,(*face.vertices[1:],face.vertices[0])):
            edge=tuple(sorted((a,b)));uses[edge]=uses.get(edge,0)+1
    unseen=set(neighbors);components=0
    while unseen:
        components+=1;pending=[unseen.pop()]
        while pending:
            for other in neighbors[pending.pop()]:
                if other in unseen:unseen.remove(other);pending.append(other)
    return components,sum(count!=2 for count in uses.values())


def verify_legacy(slug):
    folder=ASSETS/'models'/slug
    bpy.ops.wm.open_mainfile(filepath=str(folder/(slug+'.blend')))
    bpy.context.view_layer.update()
    objects=[o for o in bpy.context.scene.objects if o.type=='MESH']
    cap=next(o for o in objects if o.name.endswith(' · cap'))
    assert cap.get('authoring_source') and not cap.get('export_source'),slug
    hair=[o for o in objects if 'hair' in o.name.lower() or 'fringe' in o.name.lower()]
    assert hair and all(o.get('authoring_source') and not o.get('export_source') for o in hair),slug
    owner=next(o for o in objects if o.name.endswith(' · exterior head'))
    assert owner.get('export_source') and owner.get('exterior_shell'),slug
    components,boundaries=connected_components(owner.data)
    assert components==1,(slug,'disconnected headwear exterior',components)
    assert boundaries==0,(slug,'open headwear exterior',boundaries)
    cap_points=native_vertices(cap);base=min(p.y for p in cap_points)
    center=sum(cap_points,Vector())/len(cap_points)
    cap.data.calc_loop_triangles();planes=[]
    for tri in cap.data.loop_triangles:
        a,b,c=[cap_points[i] for i in tri.vertices]
        normal=(b-a).cross(c-a).normalized()
        if normal.dot((a+b+c)/3-center)<0:normal=-normal
        if normal.y<-.9:continue
        planes.append((normal,a))
    owner_keys={tuple(round(c,5) for c in p) for p in native_vertices(owner)}
    native=[]
    for batch in read_mesh(folder/'scene.l64'):
        if batch['part']!=0:continue
        for offset in range(0,len(batch['indices']),3):
            tri=[Vector(tuple(v+p for v,p in zip(batch['vertices'][i][:3],batch['pivot']))) for i in batch['indices'][offset:offset+3]]
            if all(tuple(round(c,5) for c in p) in owner_keys for p in tri):native.append(tri)
    assert native,slug
    def winding(point):
        total=0
        for tri in native:
            a,b,c=[tuple(float(v)-float(p) for v,p in zip(vertex,point)) for vertex in tri]
            def dot(a,b):return sum(x*y for x,y in zip(a,b))
            la,lb,lc=[math.sqrt(dot(v,v)) for v in (a,b,c)]
            det=a[0]*(b[1]*c[2]-b[2]*c[1])-a[1]*(b[0]*c[2]-b[2]*c[0])+a[2]*(b[0]*c[1]-b[1]*c[0])
            total+=2*math.atan2(det,la*lb*lc+dot(a,b)*lc+dot(b,c)*la+dot(c,a)*lb)
        return total/(4*math.pi)
    native_keys={tuple(round(c,5) for c in p) for tri in native for p in tri}
    hidden_source_vertices=[]
    for obj in hair+[o for o in objects if o.get('authoring_source') and o.name.endswith(' · head')]:
        for point in native_vertices(obj):
            if point.y>base+EPS and all(n.dot(point-o)<-.001 for n,o in planes):
                hidden_source_vertices.append(point)
    retained=[p for p in hidden_source_vertices if tuple(round(c,5) for c in p) in native_keys]
    assert not retained,(slug,'retained hidden scalp/head vertices',len(retained))
    tested=hidden=0;maximum_outside=0;minimum_inside=1
    for tri in native:
        polygon=clip(tri,lambda p:base+EPS-p.y)
        if polygon_area(polygon)<1e-9:continue
        tested+=1
        point=sum(polygon,Vector())/len(polygon)
        normal=(tri[1]-tri[0]).cross(tri[2]-tri[0]).normalized()
        offset=min(.0001,polygon_area(polygon)/max((polygon[i]-polygon[i-1]).length for i in range(len(polygon)))/8)
        samples=sorted(abs(winding(point+normal*(side*offset))) for side in (-1,1))
        maximum_outside=max(maximum_outside,samples[0]);minimum_inside=min(minimum_inside,samples[1])
        if samples[0]>.05 or samples[1]<.95:hidden+=1
    assert tested>0,slug
    assert hidden==0,(slug,'non-exterior native headwear faces',hidden,maximum_outside,minimum_inside)
    return dict(model=slug,passed=True,owning_head_components=components,owning_head_topological_boundary_edges=boundaries,hidden_authored_hair_objects=len(hair),authored_hidden_head_scalp_vertices=len(hidden_source_vertices),exported_hidden_head_scalp_vertices=len(retained),native_head_triangles=len(native),native_triangles_above_brim=tested,non_exterior_native_headwear_faces=hidden,maximum_outside_winding=round(maximum_outside,6),minimum_inside_winding=round(minimum_inside,6),cap_base_height=base,native_sha256=hashlib.sha256((folder/'scene.l64').read_bytes()).hexdigest())



def verify_ready(slug,objects):
    """New Water/player shells own their actual skin/face/headwear regions.

    This does not weaken the original boolean/scalp proof for other families.
    Ancillary visor/goggle objects may intersect a main shell, but each must
    be its own closed solid, not a coplanar face overlay.
    """
    from collections import Counter
    from check_water_cast import triangles,components_and_topology,surface_checks,exterior_sides,_head_batches
    folder=ASSETS/'models'/slug
    active=[o for o in objects if o.get('export_source')]
    heads=[o for o in active if o.get('head_component')]
    main=[o for o in heads if any(slot.material and slot.material.get('role')=='face' for slot in o.material_slots)]
    assert len(main)==1,(slug,'missing unique owning skull',len(main))
    main=main[0]
    assert main.get('native_exterior_ready') and main.get('exterior_shell')
    components,boundaries=connected_components(main.data)
    assert components==1 and boundaries==0,(slug,'open/disconnected owning skull')
    used=Counter(main.data.polygons[i].material_index for i in range(len(main.data.polygons)))
    roles={main.material_slots[i].material.get('role') for i in used}
    assert 'face' in roles and 'skin' in roles and len(roles)>=3,(slug,'missing skin/face/headwear regions',roles)
    assert all(0<=i<len(main.material_slots) for i in used)
    # A tagged shell may not coexist with exported generic scalp/head layers.
    words=(' · head',' · face',' · nose',' · ear','hair','fringe',' · cap',' · brim','headband')
    legacy=[o.name for o in active if any(word in o.name.lower() for word in words)
            and not(o.get('head_component') and o.get('native_exterior_ready'))]
    assert not legacy,(slug,'active legacy scalp/headwear',legacy)
    expected=Counter();owner_keys=set();piece_reports=[]
    for obj in heads:
        assert obj.get('native_exterior_ready') and obj.get('exterior_shell'),obj.name
        count,open_edges=connected_components(obj.data)
        assert count==1 and open_edges==0,(obj.name,'not individually closed',count,open_edges)
        obj.data.calc_loop_triangles();points=native_vertices(obj);records=[]
        for tri in obj.data.loop_triangles:
            ps=[tuple(round(c,6) for c in points[i]) for i in tri.vertices]
            records.append(dict(points=ps,vertices=[],part=0,pivot=(0,0,0),texture=-1))
            key=tuple(sorted(tuple(round(c,5) for c in v) for v in ps));expected[key]+=1
            if obj==main:owner_keys.add(key)
        solid,edges=components_and_topology(records)
        assert len(solid)==1,(obj.name,'disconnected triangulated solid')
        piece_reports.append(dict(object=obj.name,triangles=len(records),closed_edges=edges,
                                  outward=True,main_skull=obj==main))
    head_batches,pivot=_head_batches(read_mesh(folder/'scene.l64'))
    native=triangles(head_batches,world=True)
    emitted=Counter(tuple(sorted(tuple(round(c,5) for c in p) for p in tri['points'])) for tri in native)
    assert expected==emitted,(slug,'emitted head differs from ready source shells',sum((expected-emitted).values()),sum((emitted-expected).values()))
    skull=[tri for tri in native if tuple(sorted(tuple(round(c,5) for c in p) for p in tri['points'])) in owner_keys]
    native_components,edges=components_and_topology(skull)
    assert len(native_components)==1
    sides=exterior_sides(skull)
    raw=surface_checks(triangles(head_batches));packed=surface_checks(triangles(head_batches,32))
    raw_components,_=components_and_topology(triangles(head_batches))
    packed_head=triangles(head_batches,32);packed_components=[];packed_edges=0
    packed_skull=[packed for raw,packed in zip(triangles(head_batches),packed_head)
                  if tuple(sorted(tuple(round(c+p,5) for c,p in zip(v,raw['pivot'])) for v in raw['points'])) in owner_keys]
    assert len(packed_skull)==len(skull),'packed skull/source correspondence changed'
    packed_sides=exterior_sides(packed_skull)
    for component in raw_components:
        pieces,count=components_and_topology([packed_head[i] for i in component['indices']])
        assert len(pieces)==1,'packing split a closed head component'
        packed_components.extend(pieces);packed_edges+=count
    return dict(model=slug,passed=True,method='ready-shell native ownership',
        native_sha256=hashlib.sha256((folder/'scene.l64').read_bytes()).hexdigest(),
        head_pivot=pivot,main_skull_triangles=len(skull),owning_head_components=1,
        owning_head_topological_boundary_edges=0,active_legacy_scalp_layers=0,
        material_regions=sorted(roles),pieces=piece_reports,raw=raw,packed=packed,
        packed_closed_components=len(packed_components),packed_closed_edges=packed_edges,packed_exterior=packed_sides,**sides)


def verify(slug):
    folder=ASSETS/'models'/slug
    bpy.ops.wm.open_mainfile(filepath=str(folder/(slug+'.blend')))
    bpy.context.view_layer.update()
    objects=[o for o in bpy.context.scene.objects if o.type=='MESH']
    ready=any(o.get('export_source') and o.get('head_component') and o.get('native_exterior_ready')
              and any(slot.material and slot.material.get('role')=='face' for slot in o.material_slots)
              for o in objects)
    if slug in ('mark','amy','joshua','swimmer') and ready:return verify_ready(slug,objects)
    # The exact original cap half-space / winding / hidden scalp algorithm
    # remains authoritative for lad, Steve, girl and any untouched legacy cap.
    return verify_legacy(slug)


def main():
    global ASSETS
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--assets',type=Path,default=ASSETS)
    parser.add_argument('--report',type=Path)
    parser.add_argument('slugs',nargs='*')
    args=parser.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
    ASSETS=args.assets.resolve();validate.ASSETS=ASSETS
    slugs=args.slugs or ['mark','lad','joshua','steve','girl']
    models=[verify(slug) for slug in slugs]
    report=dict(passed=True,method='Legacy capped families retain the original boolean/scalp half-space and native winding proof. Ready Water/player heads separately prove one closed outward skull with disjoint material regions and no active legacy scalp; ancillary visor/goggles are individually closed and packed coplanar overlays are rejected.',height_clip_epsilon=EPS,assets=str(ASSETS),models=models)
    default=(ROOT/('review/cap_validation.json' if len(slugs)==5 else 'build/cap-validation-'+slugs[0]+'.json')
             if ASSETS==(ROOT/'assets').resolve() else ASSETS/'cap_validation.json')
    target=args.report or default;target.parent.mkdir(parents=True,exist_ok=True)
    target.write_text(json.dumps(report,indent=2)+'\n')
    print('CAPS PASS',json.dumps(models))


if __name__=='__main__':main()
