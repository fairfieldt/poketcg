#!/usr/bin/env python3
"""Read and check native character exports without Blender or the N64 SDK."""
import json
import math
from pathlib import Path
import struct
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT.parents[1]/'port/frontends/web/tools'))
from png import read_png


def read_mesh(path):
    data=path.read_bytes()
    magic,count,nt=struct.unpack_from('>4sHH',data)
    assert magic==b'L643' and 0<count<=512 and 0<=nt<=128,path
    offset=8
    textures=[]
    for i in range(nt):
        name=data[offset:offset+32].split(b'\0')[0].decode();offset+=32
        textures.append(name)
        w,h,_,_=read_png(ROOT/'assets/textures'/name.replace('.sprite','.png'))
        assert w*h*2<=4096 and w&(w-1)==0 and h&(h-1)==0,(path,name,w,h)
    batches=[]
    for i in range(count):
        model,part,tex,res,nv,ni,*pivot=struct.unpack_from('>HHhHII3f',data,offset);offset+=28
        assert 0<=part<=4 and 1<=model<=256 and res==0 and 0<nv<65536 and ni%3==0
        assert -1<=tex<nt and all(math.isfinite(v) for v in pivot)
        vertices=[]
        for j in range(nv):
            vertex=struct.unpack_from('>5f4B',data,offset);offset+=24
            assert all(math.isfinite(v) for v in vertex[:5])
            assert all(abs(v)<10 for v in vertex[:3])
            assert all(-.001<=v<=1.001 for v in vertex[3:5])
            vertices.append(vertex)
        indices=struct.unpack_from('>'+str(ni)+'H',data,offset);offset+=ni*2;offset=(offset+3)&~3
        assert max(indices)<nv
        for k in range(0,ni,3):
            a,b,c=(vertices[j][:3] for j in indices[k:k+3])
            ab=[b[j]-a[j] for j in range(3)];ac=[c[j]-a[j] for j in range(3)]
            cross=(ab[1]*ac[2]-ab[2]*ac[1],ab[2]*ac[0]-ab[0]*ac[2],ab[0]*ac[1]-ab[1]*ac[0])
            assert sum(x*x for x in cross)>1e-12,(path,'degenerate',i,k)
        batches.append(dict(model=model,part=part,texture=tex,pivot=pivot,vertices=vertices,indices=indices))
    assert offset==len(data)
    return batches


def validate_model(slug):
    folder=ROOT/'assets/models'/slug
    paths=[folder/'scene.l64']+sorted(folder.glob('variants/*/scene.l64'))+sorted(folder.glob('poses/*/scene.l64'))+sorted(folder.glob('components/*/scene.l64'))
    for path in paths:
        batches=read_mesh(path)
        info=json.loads(path.with_suffix('.json').read_text())
        assert sum(len(b['indices'])//3 for b in batches)==info['triangles']
        assert sum(len(b['vertices']) for b in batches)==info['vertices']
        assert {b['model'] for b in batches}=={info['runtime_model']}
        assert info['triangles']<=900,(slug,'triangle budget',info['triangles'])
        if slug not in ('torch','legendary_card'):assert {b['part'] for b in batches}==set(range(5)),slug
    assert (folder/(slug+'.blend')).stat().st_size>10000
    for a in ('front.png','back.png','complete.json'):assert (folder/a).exists()
    base=read_mesh(paths[0])
    # Palette variants must preserve geometry, UVs, topology and rig exactly.
    for path in sorted(folder.glob('variants/*/scene.l64')):
        alt=read_mesh(path)
        assert len(base)==len(alt)
        color_changes=0
        for a,b in zip(base,alt):
            for key in ('model','part','texture','pivot'):assert a[key]==b[key]
            # The exporter can merge coincident same-color vertices in one
            # palette. Compare the actual triangle stream across such seams.
            assert len(a['indices'])==len(b['indices'])
            for ia,ib in zip(a['indices'],b['indices']):
                va,vb=a['vertices'][ia],b['vertices'][ib]
                assert va[:5]==vb[:5]
                color_changes+=va[5:]!=vb[5:]
        assert color_changes>0,(path,'variant has no recoloring')
    return len(paths)


def main():
    catalog=json.loads((ROOT/'catalog.json').read_text())
    slugs=[sys.argv[1]] if len(sys.argv)>1 else [f['slug'] for f in catalog['families']]
    count=sum(validate_model(slug) for slug in slugs)
    if len(sys.argv)==1:
        assert len(catalog['aliases'])==115
        assert len(catalog['families'])==39 and len(catalog['appearances'])==42
        assert len({f['gfx_sha256'] for f in catalog['families']})==39
        for alias,ref in catalog['aliases'].items():
            assert ref['model'] in slugs
            assert ref['appearance'] in {a['slug'] for a in catalog['appearances']}
        assert not list((ROOT/'assets/models').glob('*/variants/*/*.blend'))
    print('VALID',len(slugs),'unique model(s),',count,'native exports; geometry, textures, rig and palette reuse checked')


if __name__=='__main__':main()
