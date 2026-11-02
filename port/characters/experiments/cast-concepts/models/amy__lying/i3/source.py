#!/usr/bin/env python3
"""Derived Amy lying pose and proof of Sara/Amanda shared body; no new authored family."""
import sys,json,hashlib,struct,copy,shutil
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def amy():
 source=ROOT/'models/amy/i3/amy';dest=source/'poses/lying';assert not dest.exists();dest.mkdir(parents=True)
 raw=(source/'scene.l64').read_bytes();data=bytearray(raw);_,nb,nt=struct.unpack_from('>4sHH',raw);p=8+32*nt;count=0;pivots=0
 for k in range(nb):
  _,_,_,_,nv,ni,px,py,pz=struct.unpack_from('>HHhHII3f',raw,p)
  struct.pack_into('>3f',data,p+16,px,pz+.35,.9-py);pivots+=1;p+=28
  for i in range(nv):
   v=p+i*24;x,y,z=struct.unpack_from('>3f',raw,v);struct.pack_into('>3f',data,v,x,z,-y)
   assert data[v+12:v+24]==raw[v+12:v+24];count+=1
  p+=nv*24+ni*2;p+=(-p)%4
 assert p==len(raw);(dest/'scene.l64').write_bytes(data);shutil.copytree(source/'textures',dest/'textures')
 desc=json.loads((source/'authoring.json').read_text())
 for mesh in desc['meshes']:
  mesh['verts']=[(x,.9-z,y+.35) for x,y,z in mesh['verts']];x,y,z=mesh['pivot'];mesh['pivot']=(x,.9-z,y+.35)
 desc['derived_from']='../../authoring.json';desc['pose']='lying';desc['source_sha256']=sha(source/'scene.l64');(dest/'authoring.json').write_text(json.dumps(desc,indent=2)+'\n')
 meta=json.loads((source/'scene.json').read_text());meta.update(pose='lying',derived_from='../../scene.l64',source_sha256=sha(source/'scene.l64'),transform_native='(x,y_up,z_front) -> (x,z_front+.35,.9-y_up)');(dest/'scene.json').write_text(json.dumps(meta,indent=2)+'\n')
 report=dict(source_sha256=sha(source/'scene.l64'),derived_sha256=sha(dest/'scene.l64'),native_vertices_checked=count,native_pivots_checked=pivots,uv_colors_preserved=True,part_indices_preserved=True,transform=meta['transform_native'])
 (dest/'derivation.json').write_text(json.dumps(report,indent=2)+'\n')
 # Capture-only alias points to derived export, not another authored character.
 cap=ROOT/'models/amy__lying/i3/amy__lying';cap.parent.mkdir(parents=True);shutil.copytree(dest,cap)
 (cap.parent/'source.py').write_text(Path(__file__).read_text());(cap.parent/'derived.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
def swimmer():
 proofs=[]
 for n in (1,2,3):
  src=ROOT/f'models/swimmer/i{n}/swimmer/authoring.json';alt=ROOT/f'models/swimmer_palette2/i{n}/swimmer_palette2/authoring.json'
  a=json.loads(src.read_text());b=json.loads(alt.read_text());a=[x for x in a['meshes'] if 'head' not in x['name']];b=[x for x in b['meshes'] if 'head' not in x['name']]
  assert a==b,(n,'Body/rig changed')
  proofs.append(dict(round=n,body_and_rig_identical=True,meshes=len(a),sha256=hashlib.sha256(json.dumps(a,sort_keys=True).encode()).hexdigest()))
 p=ROOT/'models/swimmer/i3/shared-body-proof.json';p.write_text(json.dumps(dict(canonical='swimmer',head_variant='swimmer_palette2',rounds=proofs),indent=2)+'\n');print(p)
if __name__=='__main__':globals()[sys.argv[1]]()
