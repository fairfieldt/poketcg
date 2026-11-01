#!/usr/bin/env python3
"""Check opaque face pigments and actual native directional-shade vertices."""
import hashlib
import argparse
import json
import math
from pathlib import Path
import struct
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from skin import image_rows,features_digest
from validate import read_mesh,ASSETS


def geometry_digest(path):
    h=hashlib.sha256()
    for batch in read_mesh(path):
        h.update(struct.pack('>HHh3f',batch['model'],batch['part'],batch['texture'],*batch['pivot']))
        for i in batch['indices']:h.update(struct.pack('>5f',*batch['vertices'][i][:5]))
    return h.hexdigest()


def verify(slug):
    folder=ASSETS/'models'/slug
    paths=[folder/'scene.l64']+sorted(folder.glob('variants/*/scene.l64'))+sorted(folder.glob('poses/*/scene.l64'))
    count=0;worst=0
    for path in paths:
        info=json.loads(path.with_suffix('.json').read_text())
        skin=info['skin_palette'];pigment=tuple(skin['rgb8'])
        levels={(v<<3)|(v>>2) for v in range(32)}
        assert all(c in levels for c in pigment),(slug,pigment)
        face_indices=[]
        for face in skin['faces']:
            assert tuple(face['background_rgb8'])==pigment
            assert face['colorspace']=='Non-Color' and face['directional_shade']
            width,height,rows=image_rows(ASSETS/'textures'/(face['texture']+'.png'))
            assert (width,height)==(32,32)
            assert all(p[3]==255 for row in rows for p in row)
            assert rows[0][0][:3]==pigment,(slug,'face edge pigment',rows[0][0],pigment)
            assert features_digest(rows,pigment+(255,))==face['feature_sha256'],(slug,'facial art changed')
            face_indices.append(info['textures'].index(face['texture']))
        batches=read_mesh(path)
        solid_colors={v[5:8] for b in batches if b['texture']<0 for v in b['vertices']
                      if all(abs(v[5+i]-pigment[i]*v[5]/pigment[0])<2 for i in range(1,3))}
        assert len(solid_colors)>5,(slug,'skin meshes missing')
        light=(-.3,1,-.5);ln=math.sqrt(sum(v*v for v in light))
        for batch in batches:
            if batch['texture'] not in face_indices:continue
            assert batch['part']==0
            for offset in range(0,len(batch['indices']),3):
                vertices=[batch['vertices'][i] for i in batch['indices'][offset:offset+3]]
                a,b,c=[v[:3] for v in vertices]
                ab=[b[k]-a[k] for k in range(3)];ac=[c[k]-a[k] for k in range(3)]
                normal=(ab[1]*ac[2]-ab[2]*ac[1],ab[2]*ac[0]-ab[0]*ac[2],ab[0]*ac[1]-ab[1]*ac[0])
                length=math.sqrt(sum(v*v for v in normal))
                ambient=next(f.get('shade_floor',.68) for f in skin['faces']
                             if info['textures'].index(f['texture'])==batch['texture'])
                assert 0<=ambient<=1
                shade=ambient+(1-ambient)*max(0,sum(normal[k]*light[k] for k in range(3))/(length*ln))
                expected=round(shade*255)
                for vertex in vertices:
                    assert max(vertex[5:8])-min(vertex[5:8])==0
                    assert abs(vertex[5]-expected)<=1,(slug,'unlit face patch',vertex[5:8],expected)
                    # Compare the real texture*vertex background with solid
                    # skin colors. One RGB5 step allows adjacent faceted normals.
                    painted=tuple(round(p*vertex[5]/255) for p in pigment)
                    difference=min(max(abs(a-b) for a,b in zip(painted,color)) for color in solid_colors)
                    assert difference<=8,(slug,'face/solid skin seam',painted,difference)
                    worst=max(worst,difference)
                    count+=1
    return count,worst


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline',type=Path,help='Compare this run with an explicit pre-edit geometry snapshot')
    parser.add_argument('slugs',nargs='*')
    args=parser.parse_args()
    catalog=json.loads((ROOT/'catalog.json').read_text())
    humans=[f['slug'] for f in catalog['families'] if f['slug'] not in ('torch','legendary_card')]
    slugs=args.slugs or humans
    vertices=worst=0
    for slug in slugs:
        count,difference=verify(slug);vertices+=count;worst=max(worst,difference)
    if args.baseline:
        for relative,digest in json.loads(args.baseline.read_text()).items():
            path=ROOT/relative
            if path.parts[len(ROOT.parts)+2] not in slugs:continue
            assert geometry_digest(path)==digest,(relative,'geometry/UVs/rig changed')
    suffix='; geometry/UV/rig preserved' if args.baseline else ''
    print(f'SKIN PASS: {len(slugs)} human families; {vertices} native face vertices lit; opaque palette backgrounds, original facial detail; maximum adjacent-skin difference {worst}/255{suffix}')


if __name__=='__main__':main()
