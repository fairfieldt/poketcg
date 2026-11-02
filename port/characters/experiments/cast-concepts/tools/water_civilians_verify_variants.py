#!/usr/bin/env python3
"""Independently inspect derived native world positions and shared-body authoring."""
import struct,json,hashlib,math
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def read(p):
 data=p.read_bytes();_,nb,nt=struct.unpack_from('>4sHH',data);tables=data[8:8+32*nt];o=8+32*nt;out=[]
 for k in range(nb):
  model,part,tex,pad,nv,ni,*pivot=struct.unpack_from('>HHhHII3f',data,o);o+=28
  vertices=[struct.unpack_from('>5f4B',data,o+i*24) for i in range(nv)];o+=nv*24;indices=data[o:o+2*ni];o+=2*ni;o+=(-o)%4
  out.append((model,part,tex,pad,pivot,vertices,indices))
 assert o==len(data);return tables,out
standing=ROOT/'models/amy/i3/amy';lying=standing/'poses/lying';a,aa=read(standing/'scene.l64');b,bb=read(lying/'scene.l64');assert a==b and len(aa)==len(bb);count=0
for x,y in zip(aa,bb):
 assert x[:4]==y[:4] and x[6]==y[6] and len(x[5])==len(y[5]);px,py,pz=x[4];qx,qy,qz=y[4]
 assert all(math.isclose(v,w,abs_tol=1e-6) for v,w in zip((px,pz+.35,.9-py),(qx,qy,qz)))
 for sv,dv in zip(x[5],y[5]):
  assert sv[3:]==dv[3:]
  world=(sv[0]+px,sv[1]+py,sv[2]+pz);actual=(dv[0]+qx,dv[1]+qy,dv[2]+qz);expected=(world[0],world[2]+.35,.9-world[1]);assert all(math.isclose(v,w,abs_tol=1e-6) for v,w in zip(actual,expected));count+=1
for png in (standing/'textures').glob('*.png'):assert png.read_bytes()==(lying/'textures'/png.name).read_bytes()
shared=[]
for sara,amanda in [('i1','i1'),('i2','i2'),('i3','i3'),('i3b','i3')]:
 def body(slug,revision):return [m for m in json.loads((ROOT/f'models/{slug}/{revision}/{slug}/authoring.json').read_text())['meshes'] if 'head' not in m['name']]
 one=body('swimmer',sara);two=body('swimmer_palette2',amanda);assert one==two;shared.append(dict(sara=sara,amanda=amanda,identical_meshes=len(one),body_rig_materials_identical=True))
# Final Amanda changes only the source outfit pigment; retain exact shared body geometry/rig.
one=body('swimmer','i3b');two=body('swimmer_palette2','i3b');diff=[]
for a,b in zip(one,two):
 assert len(a['materials'])==len(b['materials'])
 for ma,mb in zip(a['materials'],b['materials']):
  if ma!=mb:
   assert ma['name']==mb['name']=='garment';diff.append(dict(mesh=a['name'],material='garment',sara=ma['rgb'],amanda=mb['rgb']));ma['rgb']=mb['rgb']
 assert a==b
assert diff
shared.append(dict(sara='i3b',amanda='i3b',identical_geometry_rig=True,only_material_change='garment blue -> original yellow',material_changes=diff))
r=dict(passed=True,amy=dict(native_world_vertices_checked=count,batches_and_pivots_checked=len(aa),uvs_colors_indices_parts_texture_bytes_preserved=True,canonical_source='models/amy/i3/amy',derived_pose='models/amy/i3/amy/poses/lying',native_capture='models/amy__lying/i3/native'),swimmer=dict(canonical='models/swimmer/i3b/swimmer.blend',head_variant='Amanda',shared_body=shared))
(ROOT/'review/water-civilians-variants.json').write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r,indent=2))
