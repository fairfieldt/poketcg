#!/usr/bin/env python3
"""Check owning room surfaces without changing any generated assets.

Exercise every source floor/doorway layout and the layered prop recipes that
previously emitted competing faces. Intentionally touching opposite sides of
a joint are not competing exterior surfaces. Source ROM metadata is required.
"""
import argparse
from collections import defaultdict
import json
from pathlib import Path
import tempfile

import build_world as world

EPS=1e-7


def cross(a,b,c):
    return (b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0])


def area(points):
    return abs(sum(a[0]*b[1]-a[1]*b[0]
                   for a,b in zip(points,points[1:]+points[:1])))/2


def intersection(a,b):
    subject=list(a)
    clip=list(b) if cross(*b)>0 else list(reversed(b))
    for start,end in zip(clip,clip[1:]+clip[:1]):
        result=[]
        if not subject:break
        previous=subject[-1];pd=cross(start,end,previous)
        for current in subject:
            cd=cross(start,end,current)
            if (pd>=-EPS)!=(cd>=-EPS):
                t=pd/(pd-cd)
                result.append(tuple(previous[i]+(current[i]-previous[i])*t for i in range(2)))
            if cd>=-EPS:result.append(current)
            previous,pd=current,cd
        subject=result
    return subject


def overlaps(scene,*,floor_only=False,level=None):
    groups=defaultdict(list)
    for key,(vertices,indices,_) in scene.batches.items():
        for offset in range(0,len(indices),3):
            points=[vertices[index] for index in indices[offset:offset+3]]
            for axis in range(3):
                if floor_only and (axis!=1 or key[2] is None):continue
                value=points[0][axis]
                if floor_only and abs(value)>EPS:continue
                if level is not None and (axis!=1 or abs(value-level)>EPS):continue
                if max(p[axis] for p in points)-min(p[axis] for p in points)>EPS:continue
                keep=[i for i in range(3) if i!=axis]
                projected=[tuple(p[i] for i in keep) for p in points]
                if area(projected)<EPS:continue
                groups[axis,round(value,6)].append(projected)
    failures=[];triangles=0
    for (axis,value),polygons in groups.items():
        triangles+=len(polygons)
        for i,a in enumerate(polygons):
            for b in polygons[i+1:]:
                # Opposite-facing joint caps do not compete for an exterior.
                if cross(*a)*cross(*b)<0:continue
                if any(max(p[k] for p in a)<=min(p[k] for p in b)+EPS or
                       max(p[k] for p in b)<=min(p[k] for p in a)+EPS for k in range(2)):continue
                polygon=intersection(a,b)
                size=area(polygon) if len(polygon)>=3 else 0
                if size>1e-5:failures.append(dict(axis=axis,plane=value,area=round(size,6)))
    return triangles,failures


def validate():
    cases=[]
    # Keep temporary texture cache outside generated/, even on failure.
    with tempfile.TemporaryDirectory(prefix='poketcg-room-surfaces-') as directory:
        old_tex=world.TEX;world.TEX=Path(directory)
        try:
            _,maps,_=world.load_metadata()
            for data in maps[1:]:
                if data['slug']=='lightning_club':continue  # Imported prototype floor is checked separately.
                slug=data['slug'];image=world.Image(world.REPO/'port/maps'/(slug+'.png'))
                name='club_lobby' if slug.endswith('_lobby') else 'club_foyer' if slug.endswith('_club_entrance') or slug=='challenge_hall_entrance' else slug
                props=world.LOBBY if name=='club_lobby' else world.FOYER if name=='club_foyer' else world.scene_props(slug,image)
                scene=world.Scene(name);scene.floor(image,props,exclude_emblem=name=='club_foyer')
                world.walls(scene,image,data['warps'])
                triangles,failures=overlaps(scene,floor_only=True)
                cases.append(dict(name=slug,check='floor-and-doorway-ownership',triangles=triangles,failures=failures))
            for kind,height in [('table',.85),('steps',.5),('machine',2.2),('altar',1.8)]:
                scene=world.Scene('fixture')
                image=world.Image(world.REPO/'port/maps/mason_laboratory.png')
                world.prop(scene,image,world.P(kind,4,4,6,4,height,4))
                triangles,failures=overlaps(scene)
                cases.append(dict(name=kind,check='prop-exterior-ownership',triangles=triangles,failures=failures))
            scene=world.Scene('water_club');world.swimming_pool(scene)
            triangles,failures=overlaps(scene,level=.07)
            cases.append(dict(name='water_club',check='coping-ownership',triangles=triangles,failures=failures))
        finally:
            world.TEX=old_tex
    failures=sum(len(case['failures']) for case in cases)
    return dict(passed=failures==0,cases=len(cases),triangles=sum(case['triangles'] for case in cases),
                overlap_pairs=failures,checks=cases)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report',type=Path)
    args=parser.parse_args()
    result=validate()
    if args.report:
        args.report.parent.mkdir(parents=True,exist_ok=True)
        args.report.write_text(json.dumps(result,indent=2)+'\n')
    print('ROOM SURFACES '+('PASS' if result['passed'] else 'FAIL')+
          f": {result['cases']} cases, {result['triangles']} planar triangles, {result['overlap_pairs']} competing overlaps")
    for case in result['checks']:
        if case['failures']:print(json.dumps(case))
    if not result['passed']:raise SystemExit(1)


if __name__=='__main__':main()
