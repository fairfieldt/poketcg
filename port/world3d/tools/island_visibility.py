"""Conservative offline occlusion for the island's fixed native camera.

Coverage is polygon subtraction, not sampled visibility. A candidate expanded
by an antialiasing margin is removed only when nearer opaque triangles cover
its entire projected area. Unknown/degenerate cases remain in the export.
"""
import hashlib
import json
import math
from pathlib import Path
import struct

EPS=1e-8

def camera_hash(camera):
    return hashlib.sha256(json.dumps(camera,sort_keys=True,separators=(',',':')).encode()).hexdigest()

def cross(a,b,c):return (b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0])
def area(poly):return abs(sum(a[0]*b[1]-a[1]*b[0] for a,b in zip(poly,poly[1:]+poly[:1])))*.5
def ccw(poly):return poly if cross(*poly[:3])>=0 else list(reversed(poly))
def bounds(poly):return min(p[0] for p in poly),min(p[1] for p in poly),max(p[0] for p in poly),max(p[1] for p in poly)
def overlap(a,b):return a[0]<=b[2] and a[2]>=b[0] and a[1]<=b[3] and a[3]>=b[1]

def halfplane(poly,a,b,inside=True,offset=0):
    result=[]
    if not poly:return result
    previous=poly[-1];pd=cross(a,b,previous)+offset
    for current in poly:
        cd=cross(a,b,current)+offset
        pin=pd>=0 if inside else pd<=0;cin=cd>=0 if inside else cd<=0
        if pin!=cin:
            t=pd/(pd-cd)
            result.append(tuple(previous[i]+(current[i]-previous[i])*t for i in range(2)))
        if cin:result.append(current)
        previous,pd=current,cd
    return result

def intersection(poly,clip):
    for a,b in zip(clip,clip[1:]+clip[:1]):
        poly=halfplane(poly,a,b)
        if len(poly)<3:return []
    return poly

def difference(poly,clip):
    result=[]
    for a,b in zip(clip,clip[1:]+clip[:1]):
        outside=halfplane(poly,a,b,False)
        if len(outside)>=3 and area(outside)>EPS:result.append(outside)
        poly=halfplane(poly,a,b)
        if len(poly)<3:break
    return result

def expand(triangle,margin):
    result=[(-10000,-10000),(10000,-10000),(10000,10000),(-10000,10000)]
    for a,b in zip(triangle,triangle[1:]+triangle[:1]):
        result=halfplane(result,a,b,offset=margin*math.dist(a,b))
    return result

def normalize(v):
    size=math.sqrt(sum(x*x for x in v));return tuple(x/size for x in v)
def dot(a,b):return sum(x*y for x,y in zip(a,b))

def project_triangles(triangles,camera,width=480,height=360):
    eye=camera['eye'];forward=normalize(tuple(b-a for a,b in zip(eye,camera['target'])))
    right=normalize((-forward[2],0,forward[0]))
    up=(-right[2]*forward[1],right[2]*forward[0]-right[0]*forward[2],right[0]*forward[1])
    tangent=math.tan(math.radians(camera['fov'])/2)
    projected=[]
    for triangle in triangles:
        points=[];inverse=[]
        for vertex in triangle:
            relative=tuple(vertex[i]-eye[i] for i in range(3));depth=dot(relative,forward)
            if depth<=1.5:break
            points.append((width*.5+dot(relative,right)/depth/tangent*height*.5,
                height*.5-dot(relative,up)/depth/tangent*height*.5))
            inverse.append(1/depth)
        if len(points)!=3 or abs(cross(*points))<EPS:
            projected.append(None);continue
        if cross(*points)<0:points.reverse();inverse.reverse()
        a,b,c=points;den=cross(a,b,c)
        dx=((inverse[1]-inverse[0])*(c[1]-a[1])-(inverse[2]-inverse[0])*(b[1]-a[1]))/den
        dy=((b[0]-a[0])*(inverse[2]-inverse[0])-(c[0]-a[0])*(inverse[1]-inverse[0]))/den
        projected.append(dict(points=points,depth=(dx,dy,inverse[0]-dx*a[0]-dy*a[1]),
            bounds=bounds(points),nearest=max(inverse)))
    return projected

def visible_triangles(triangles,camera,width=480,height=360,margin=1.0,depth_margin=.04):
    projected=project_triangles(triangles,camera,width,height)
    buckets={};cell=16
    def cells(box):
        left,top,right,bottom=box
        for y in range(math.floor(max(-cell,top)/cell),math.floor(min(height+cell,bottom)/cell)+1):
            for x in range(math.floor(max(-cell,left)/cell),math.floor(min(width+cell,right)/cell)+1):yield x,y
    for i,tri in enumerate(projected):
        if tri:
            for key in cells(tri['bounds']):buckets.setdefault(key,[]).append(i)
    keep=[];removed=[];offscreen=[]
    for index,tri in enumerate(projected):
        if tri is None:keep.append(index);continue
        poly=expand(tri['points'],margin)
        # The same mesh is used by a wider browser viewport. Never discard
        # viewport-only geometry or test only its cropped reference portion.
        box=bounds(poly)
        if box[0]<-margin or box[1]<-margin or box[2]>width+margin or box[3]>height+margin:
            keep.append(index);continue
        box=bounds(poly);possible=set()
        for key in cells(box):possible.update(buckets.get(key,()))
        possible.discard(index)
        fragments=[poly];ax,ay,ac=tri['depth']
        for other in sorted(possible,key=lambda i:-projected[i]['nearest']):
            occluder=projected[other]
            if not overlap(box,occluder['bounds']):continue
            bx,by,bc=occluder['depth']
            remaining=[]
            for fragment in fragments:
                if not overlap(bounds(fragment),occluder['bounds']):remaining.append(fragment);continue
                covered=intersection(fragment,occluder['points'])
                # Reciprocal depth is affine in screen space; checking every
                # intersection vertex proves the complete overlap is nearer.
                if len(covered)<3:
                    remaining.append(fragment);continue
                clearance=min((bx-ax)*x+(by-ay)*y+bc-ac for x,y in covered)
                product=max(ax*x+ay*y+ac for x,y in covered)*max(bx*x+by*y+bc for x,y in covered)
                if clearance<depth_margin*product:
                    remaining.append(fragment);continue
                remaining.extend(difference(fragment,occluder['points']))
            fragments=remaining
            if not fragments:break
            if len(fragments)>64:break
        if fragments:keep.append(index)
        else:removed.append(index)
    return keep,dict(input_triangles=len(triangles),kept_triangles=len(keep),
        occluded_triangles=len(removed),offscreen_triangles=len(offscreen),
        camera_sha256=camera_hash(camera),width=width,height=height,aa_margin_pixels=margin,depth_margin_world=depth_margin,
        removed_indices=removed,offscreen_indices=offscreen)

def optimize_mesh(source,target,camera,protected=()):
    from build_world import read_l643
    names,meshes=read_l643(source)
    triangles=[tuple(vs[j][:3] for j in indices[i:i+3])
        for _,_,_,_,vs,indices in meshes for i in range(0,len(indices),3)]
    keep,report=visible_triangles(triangles,camera)
    keep=set(keep)|set(protected)
    output=[];offset=0
    for model,part,texture,pivot,vertices,indices in meshes:
        surviving=[index for i in range(0,len(indices),3) if offset+i//3 in keep
            for index in indices[i:i+3]]
        offset+=len(indices)//3
        # Retain original vertex records/order to avoid attribute conversion
        # changes. Only hidden triangle index triples are removed.
        if surviving:output.append((model,part,texture,pivot,vertices,surviving))
    blob=bytearray(struct.pack('>4sHH',b'L643',len(output),len(names)))
    for name in names:blob+=name.encode().ljust(32,b'\0')
    for model,part,texture,pivot,vertices,indices in output:
        blob+=struct.pack('>HHhHII3f',model,part,texture,0,len(vertices),len(indices),*pivot)
        for vertex in vertices:blob+=struct.pack('>5f4B',*vertex)
        blob+=struct.pack('>'+str(len(indices))+'H',*indices)
        while len(blob)%4:blob+=b'\0'
    Path(target).write_bytes(blob)
    report.update(kept_triangles=len(keep),geometric_occlusion_candidates=report['occluded_triangles'],
        occluded_triangles=len(triangles)-len(keep),
        removed_indices=[i for i in range(len(triangles)) if i not in keep],
        native_protected_triangles=len(set(protected)),
        source_sha256=hashlib.sha256(Path(source).read_bytes()).hexdigest(),
        output_sha256=hashlib.sha256(blob).hexdigest())
    return report

def apply_validated_optimization(path,camera,bake_path=None):
    """Apply only the exact source/camera/output proven by native pixel A/B.

    Authoring changes safely retain the complete mesh until a new validation
    bake is published. The editable Blender scene is never simplified.
    """
    bake_path=Path(bake_path) if bake_path else Path(__file__).with_name('island_visibility_bake.json')
    if not bake_path.exists():return None
    bake=json.loads(bake_path.read_text())
    if camera_hash(camera)!=bake['camera_sha256']:return None
    if hashlib.sha256(Path(path).read_bytes()).hexdigest()!=bake['source_sha256']:return None
    temporary=Path(path).with_suffix('.visibility.l64')
    report=optimize_mesh(path,temporary,camera,bake['native_protected_indices'])
    if report['output_sha256']!=bake['output_sha256']:
        temporary.unlink();return None
    temporary.replace(path)
    return {key:report[key] for key in ('input_triangles','kept_triangles','occluded_triangles',
        'camera_sha256','source_sha256','output_sha256','aa_margin_pixels','depth_margin_world',
        'native_protected_triangles')}
