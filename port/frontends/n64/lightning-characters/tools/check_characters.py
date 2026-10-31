#!/usr/bin/env python3
"""Verify reusable exports match the demo, rig pivots and original room geometry."""
import json
from pathlib import Path
import struct

ROOT=Path(__file__).resolve().parents[1]


def read(path):
    blob=path.read_bytes()
    magic,count,nt=struct.unpack_from('>4sHH',blob)
    assert magic==b'L643'
    textures=[blob[8+i*32:40+i*32].split(b'\0')[0].decode() for i in range(nt)]
    offset=8+nt*32
    batches=[]
    for i in range(count):
        header=struct.unpack_from('>HHhHII3f',blob,offset)
        offset+=28
        model,part,texture,reserved,nv,ni,*pivot=header
        end=offset+nv*24+ni*2
        batches.append(dict(model=model,part=part,texture=textures[texture] if texture>=0 else None,
                            vertices=nv,triangles=ni//3,pivot=pivot,data=blob[offset:end]))
        offset=(end+3)&~3
    assert offset==len(blob)
    return batches


scene=read(ROOT/'assets/scene.l64')
original=read(ROOT.parent/'lightning/assets/scene.l64')
for model in (0,2,3):
    assert [b for b in scene if b['model']==model]==[b for b in original if b['model']==model],model
for name,model,triangles in (('ronald',1,640),('dr_mason',4,577)):
    path=ROOT/'assets/models'/name
    standalone=read(path/'scene.l64')
    assert standalone==[b for b in scene if b['model']==model],name
    assert {b['part'] for b in standalone}=={0,1,2,3,4}
    assert sum(b['triangles'] for b in standalone)==triangles
    for part in (1,2,3,4):
        p=next(b['pivot'] for b in standalone if b['part']==part)
        assert p[0]<0 if part in (1,3) else p[0]>0
        assert abs(p[1]-(.72 if part<3 else 1.36))<1e-5
    manifest=json.loads((path/'scene.json').read_text())
    assert len(manifest['textures'])==1
    assert (path/(name+'.blend')).stat().st_size>10000
print('Characters OK: standalone/demo meshes identical; all rigid joints; original room and other NPCs unchanged')
