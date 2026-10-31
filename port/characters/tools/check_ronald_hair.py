"""Verify Ronald's head and hair form one actual native exterior.

Run in Blender: blender -b --python tools/check_ronald_hair.py [-- BEFORE_L64]
The optional prior mesh also checks the four animated limbs remain exact.
"""
import hashlib,json,math,sys
from pathlib import Path
import bpy
from mathutils import Vector
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from validate import read_mesh

def connected_components(data):
 neighbors={v.index:set() for v in data.vertices};uses={}
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

folder=ROOT/'assets/models/ronald'
bpy.ops.wm.open_mainfile(filepath=str(folder/'ronald.blend'))
objects=[o for o in bpy.context.scene.objects if o.type=='MESH']
owner=next(o for o in objects if o.name.endswith(' · exterior head'))
hair=[o for o in objects if any(word in o.name.lower() for word in ('hair','spike','fringe'))]
assert len(hair)==12 and all(o.get('authoring_source') and not o.get('export_source') for o in hair)
components,boundaries=connected_components(owner.data)
assert components==1 and boundaries==0,(components,boundaries)
keys={tuple(round(c,5) for c in (owner.matrix_world@v.co).xzy) for v in owner.data.vertices}
# Blender front is +Y; native +Z faces the other way.
keys={(p[0],p[1],-p[2]) for p in keys}
mesh=read_mesh(folder/'scene.l64');native=[];bright_scalp=0
for batch in mesh:
 if batch['part']!=0:continue
 for i in range(0,len(batch['indices']),3):
  vs=[batch['vertices'][j] for j in batch['indices'][i:i+3]]
  tri=[Vector(tuple(c+p for c,p in zip(v[:3],batch['pivot']))) for v in vs]
  if all(tuple(round(c,5) for c in v) in keys for v in tri):native.append(tri)
  # Even a triangle crossing the upper hairline must not leave positive-area
  # skin islands. The separate textured face ends at1.89 and is preserved.
  if batch['texture']<0 and max(v.y for v in tri)>1.89+1e-5:
   if sum(sum(v[5:8]) for v in vs)/9>90:bright_scalp+=1
assert bright_scalp==0,bright_scalp
assert len(native)==len(owner.data.polygons),(len(native),len(owner.data.polygons))

def winding(point):
 total=0
 for tri in native:
  a,b,c=[v-point for v in tri];la,lb,lc=a.length,b.length,c.length
  total+=2*math.atan2(a.dot(b.cross(c)),la*lb*lc+a.dot(b)*lc+b.dot(c)*la+c.dot(a)*lb)
 return total/(4*math.pi)

hidden=0;maximum_outside=0;minimum_inside=1
for tri in native:
 a,b,c=tri;normal=(b-a).cross(c-a);area=normal.length/2
 normal.normalize();point=(a+b+c)/3
 offset=min(.00005,area/max((b-a).length,(c-b).length,(a-c).length)/10)
 sides=sorted(abs(winding(point+normal*(side*offset))) for side in (-1,1))
 maximum_outside=max(maximum_outside,sides[0]);minimum_inside=min(minimum_inside,sides[1])
 if sides[0]>.05 or sides[1]<.95:hidden+=1
assert hidden==0,(hidden,maximum_outside,minimum_inside)
limbs_unchanged=lower_body_unchanged=None
if '--' in sys.argv and sys.argv.index('--')+1<len(sys.argv):
 before=read_mesh(Path(sys.argv[sys.argv.index('--')+1]))
 limbs_unchanged=[b for b in before if b['part']>0]==[b for b in mesh if b['part']>0]
 assert limbs_unchanged
 def lower_body(batches):
  triangles=[]
  for batch in batches:
   if batch['part']!=0:continue
   for i in range(0,len(batch['indices']),3):
    tri=tuple(batch['vertices'][j] for j in batch['indices'][i:i+3])
    if max(v[1]+batch['pivot'][1] for v in tri)<1.45:
     triangles.append((batch['texture'],tuple(sorted(tri))))
  return sorted(triangles)
 lower_body_unchanged=lower_body(before)==lower_body(mesh)
 assert lower_body_unchanged
report=dict(passed=True,model='ronald',triangles=sum(len(b['indices'])//3 for b in mesh),batches=len(mesh),
 owning_head_components=components,owning_head_boundary_edges=boundaries,hidden_authored_hair_objects=len(hair),
 native_head_triangles=len(native),non_exterior_native_head_triangles=hidden,
 maximum_outside_winding=round(maximum_outside,6),minimum_inside_winding=round(minimum_inside,6),
 bright_upper_scalp_triangles=bright_scalp,scalp_hairline=1.88,face_paint_top=1.89,
 limbs_unchanged=limbs_unchanged,lower_body_triangles_unchanged=lower_body_unchanged,
 native_sha256=hashlib.sha256((folder/'scene.l64').read_bytes()).hexdigest())
(ROOT/'review/ronald-hair').mkdir(exist_ok=True)
(ROOT/'review/ronald-hair/validation.json').write_text(json.dumps(report,indent=2)+'\n')
print('RONALD HAIR PASS',json.dumps(report))
