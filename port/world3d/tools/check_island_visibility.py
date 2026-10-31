#!/usr/bin/env python3
"""Check exact coverage/depth guards and stale-bake fallback without Blender."""
import hashlib
import json
from pathlib import Path
import struct
import tempfile
from island_visibility import visible_triangles,camera_hash,optimize_mesh,apply_validated_optimization

camera=dict(eye=[0,0,10],target=[0,0,0],fov=60)
back=((-1,-1,0),(1,-1,0),(0,1,0))
front=((-4,-4,3),(4,-4,3),(0,4,3))
keep,result=visible_triangles([back,front],camera)
assert keep==[1] and result['occluded_triangles']==1
assert visible_triangles([back,tuple((x*.1,y*.1,z) for x,y,z in front)],camera)[0]==[0,1]
assert visible_triangles([back,back],camera)[0]==[0,1],'coplanar surfaces must remain'
assert visible_triangles([back,tuple((x,y,.01) for x,y,z in front)],camera)[0]==[0,1],'depth clearance'

# Exact subtraction must accept coverage across a shared diagonal, and retain
# even an arbitrarily thin gap that sampled ray visibility could miss.
square=[((-4,-4,3),(4,-4,3),(4,4,3)),((-4,-4,3),(4,4,3),(-4,4,3))]
assert visible_triangles([back,*square],camera)[0]==[1,2]
gap=.00001
split=[((-4,-4,3),(-gap,-4,3),(-gap,4,3)),((-4,-4,3),(-gap,4,3),(-4,4,3)),
       ((gap,-4,3),(4,-4,3),(4,4,3)),((gap,-4,3),(4,4,3),(gap,4,3))]
assert 0 in visible_triangles([back,*split],camera)[0],'visible gap must remain'
outside=tuple((x+15,y,z) for x,y,z in back)
assert visible_triangles([outside],camera)[0]==[0],'wide browser viewport geometry must remain'
assert camera_hash(camera)!=camera_hash({**camera,'fov':61})

with tempfile.TemporaryDirectory(prefix='island-visibility-test-') as scratch:
    scratch=Path(scratch);source=scratch/'full.l64';target=scratch/'filtered.l64'
    vertices=[(*v,0,0,255,255,255,255) for triangle in (back,front) for v in triangle]
    blob=struct.pack('>4sHH',b'L643',1,0)+struct.pack('>HHhHII3f',0,0,-1,0,6,6,0,0,0)
    blob+=b''.join(struct.pack('>5f4B',*v) for v in vertices)+struct.pack('>6H',*range(6))
    source.write_bytes(blob)
    report=optimize_mesh(source,target,camera)
    assert report['kept_triangles']==1
    protected=optimize_mesh(source,scratch/'protected.l64',camera,[0])
    assert protected['kept_triangles']==2,'native coverage contributors must remain'
    bake=scratch/'bake.json'
    valid=dict(source_sha256=hashlib.sha256(blob).hexdigest(),output_sha256=report['output_sha256'],
        camera_sha256=camera_hash(camera),native_protected_indices=[])
    for changed in ({'source_sha256':'stale'},{'camera_sha256':'stale'},{'output_sha256':'stale'}):
        bake.write_text(json.dumps({**valid,**changed}))
        assert apply_validated_optimization(source,camera,bake) is None
        assert source.read_bytes()==blob,'stale bake must retain complete geometry'
    bake.write_text(json.dumps(valid));applied=apply_validated_optimization(source,camera,bake)
    assert applied and source.read_bytes()==target.read_bytes()

print('ISLAND VISIBILITY PASS: exact union coverage; tiny-gap retention; depth/AA guards; wide-viewport retention; protected triangles; source/camera/output SHA invalidation')
