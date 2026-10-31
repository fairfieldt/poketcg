"""Prove native headwear is one exterior, with no hidden scalp/head export.

Run in Blender background mode. Actual emitted L643 head triangles above the
brim must have winding0 outside and winding1 inside the unified closed head.
Original deep scalp/head vertices must not survive export. This uses final
native geometry; authored nonplanar cap quads are not an exterior reference.
"""
import hashlib
import json
import math
from pathlib import Path
import sys
import bpy
from mathutils import Vector

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from validate import read_mesh
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


def verify(slug):
    folder=ROOT/'assets/models'/slug
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



slugs=sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else ['mark','lad','joshua','steve','girl']
models=[verify(slug) for slug in slugs]
report=dict(passed=True,method='Actual emitted native headwear triangles above the brim have one side outside (winding0) and one side inside (winding1) the unified closed head surface. Original hidden scalp/head vertices do not survive export. Authored cap half-spaces identify deep hidden source vertices only; nonplanar cap retessellation does not cause false positives.',height_clip_epsilon=EPS,models=models)
target=ROOT/('review/cap_validation.json' if len(slugs)==5 else 'build/cap-validation-'+slugs[0]+'.json')
target.write_text(json.dumps(report,indent=2)+'\n')
print('CAPS PASS',json.dumps(models))
