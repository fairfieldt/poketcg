#!/usr/bin/env python3
"""Validate binary ranges, finite vertex data, TMEM budgets and map placements."""
import json
import math
from pathlib import Path
import struct
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT.parents[3]/'port/frontends/web/tools'))
from png import read_png

blob=(ROOT/'assets/scene.l64').read_bytes()
manifest=json.loads((ROOT/'assets/scene.json').read_text())
magic,mesh_count,texture_count=struct.unpack_from('>4sHH',blob)
assert magic==b'L643'
offset=8
for i in range(texture_count):
    name=blob[offset:offset+32].split(b'\0')[0].decode()
    offset+=32
    width,height,_,_=read_png(ROOT/'assets/textures'/name.replace('.sprite','.png'))
    assert width*height*2<=4096, (name,'RGBA16 exceeds TMEM')
    assert width & (width-1)==0 and height & (height-1)==0

vertices=triangles=0
models=set()
for _ in range(mesh_count):
    assert offset%4==0
    model,part,texture,reserved,nv,ni,*pivot=struct.unpack_from('>HHhHII3f',blob,offset)
    offset+=28
    assert 0<=model<=4 and 0<=part<=4
    assert -1<=texture<texture_count and reserved==0
    assert 0<nv<65536 and ni%3==0
    models.add(model)
    vs=[]
    for i in range(nv):
        v=struct.unpack_from('>5f4B',blob,offset)
        offset+=24
        assert all(math.isfinite(x) for x in v[:5])
        assert all(abs(x)<100 for x in v[:3])
        vs.append(v)
    indices=struct.unpack_from('>'+str(ni)+'H',blob,offset)
    offset+=ni*2
    offset=(offset+3)&~3
    assert max(indices)<nv
    for i in range(0,ni,3):
        a,b,c=(vs[j][:3] for j in indices[i:i+3])
        ab=[b[j]-a[j] for j in range(3)]
        ac=[c[j]-a[j] for j in range(3)]
        cross=[ab[1]*ac[2]-ab[2]*ac[1],ab[2]*ac[0]-ab[0]*ac[2],ab[0]*ac[1]-ab[1]*ac[0]]
        assert sum(x*x for x in cross)>1e-12, 'Degenerate triangle'
    vertices+=nv
    triangles+=ni//3
assert offset==len(blob)
assert models=={0,1,2,3,4}
assert (vertices,triangles,mesh_count)==tuple(manifest[k] for k in ('vertices','triangles','batches'))
expected={'Mark':(14,28,180),'Isaac':(12,4,180),'Nicholas':(6,10,0),'Brandon':(22,12,180),'Jennifer':(14,18,0)}
assert {a['name']:(a['x'],a['z'],a['heading']) for a in manifest['actors']}==expected
print(f'Scene OK: {triangles} triangles; {vertices} vertices; {texture_count} TMEM-safe textures; all five placements verified')
