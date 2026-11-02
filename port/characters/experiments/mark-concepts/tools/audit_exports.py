#!/usr/bin/env python3
"""Independently audit the bytes consumed by the native model loader."""
import argparse, collections, hashlib, json, math, struct
from pathlib import Path
from PIL import Image

def cross(a,b,c):
    u=[b[i]-a[i] for i in range(3)];v=[c[i]-a[i] for i in range(3)]
    return (u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0])

def audit(path):
    data=path.read_bytes();magic,nb,nt=struct.unpack_from('>4sHH',data)
    assert magic==b'L643'
    names=[data[8+i*32:40+i*32].split(b'\0')[0].decode() for i in range(nt)]
    p=8+32*nt;parts=set();count=0;errors=[];seen_raw=set();seen_packed=set()
    for batch in range(nb):
        model,part,tex,_,nv,ni,*pivot=struct.unpack_from('>HHhHII3f',data,p);p+=28
        parts.add(part)
        assert -1<=tex<nt and ni%3==0
        verts=[struct.unpack_from('>5f4B',data,p+i*24) for i in range(nv)];p+=nv*24
        assert all(math.isfinite(c) for v in verts for c in v[:5])
        if any(not -32768 <= c*32 <= 32767 for v in verts for c in v[:3]):
            errors.append(f'packed position overflow batch {batch}')
        ids=struct.unpack_from('>'+str(ni)+'H',data,p);p+=ni*2;p+=(-p)%4
        for j in range(0,ni,3):
            count+=1
            assert max(ids[j:j+3])<nv
            raw=[tuple(verts[i][:3]) for i in ids[j:j+3]]
            packed=[tuple(math.trunc(struct.unpack('f',struct.pack('f',c*32))[0]) for c in v) for v in raw]
            for label,points,seen in [('raw',raw,seen_raw),('packed',packed,seen_packed)]:
                if sum(c*c for c in cross(*points)) <= (1e-18 if label=='raw' else 0):
                    errors.append(f'{label} degenerate batch {batch} triangle {j//3}')
                key=(part,tuple(pivot),tuple(sorted(points)))
                if key in seen: errors.append(f'{label} duplicate batch {batch} triangle {j//3}')
                seen.add(key)
    assert p==len(data)
    if parts!={0,1,2,3,4}:errors.append(f'parts {sorted(parts)}')
    if count>900:errors.append(f'triangle budget exceeded: {count}>900')
    textures=[]
    for name in names:
        png=path.parent/'textures'/name.replace('.sprite','.png')
        with Image.open(png) as im:
            rgba=im.convert('RGBA');size=im.size
            if size!=(32,32):errors.append(f'{name} size {size}')
            if rgba.getextrema()[3]!=(255,255):errors.append(f'{name} not opaque')
            textures.append(dict(name=name,size=size,rgba16_bytes=size[0]*size[1]*2))
    return dict(path=str(path),sha256=hashlib.sha256(data).hexdigest(),triangles=count,parts=sorted(parts),textures=textures,errors=errors,passed=not errors)

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('paths',nargs='+',type=Path);p.add_argument('--output',type=Path)
    a=p.parse_args();results=[audit(f) for f in a.paths]
    report=dict(passed=all(r['passed'] for r in results),models=results)
    if a.output:a.output.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2));raise SystemExit(0 if report['passed'] else 1)
if __name__=='__main__':main()
