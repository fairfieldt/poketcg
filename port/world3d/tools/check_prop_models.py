#!/usr/bin/env python3
"""Audit actual authored room props before publishing cartridge/WebGL meshes.

Check exterior ownership both in authoring coordinates and after the N64
packed renderer's float32 -> trunc(position*32) conversion. Every tested prop
comes from scene_specs/source-room artwork, including installed machines and
independent medal printer screen states. No generated assets are modified.
"""
import argparse
import copy
import json
import math
from pathlib import Path
import struct
import tempfile
from dataclasses import replace

import build_world as world
from check_room_surfaces import area, intersection, overlaps
from furniture_props import build as furniture
from lab_props import build as lab,build_deck_front,deck_screen_rect
from decorative_props import build as decorative

OWNERS=(('furniture',furniture),('lab',lab),('decorative',decorative))


def packed(value):
    # scene_packed.inc: source L643 float32, then truncf(value * 32.0f).
    value=struct.unpack('>f',struct.pack('>f',value))[0]
    return math.trunc(value*32)/32


def native_scene(scene):
    result=copy.deepcopy(scene)
    for key,(vertices,indices,_) in list(result.batches.items()):
        result.batches[key]=([tuple(packed(v[i]) if i<3 else v[i] for i in range(len(v)))
                              for v in vertices],indices,{})
    return result


def transformed(source):
    x,z,w,d=world.footprint(source)
    return replace(source,x=x,y=z,w=w,h=d,depth=d)


def state_area(scene,rect,*,native=False):
    x,y,z,w,h=rect;right=x+w;top=y+h
    if native:x,y,z,right,top=map(packed,(x,y,z,right,top))
    clip=[[(x,y),(right,y),(right,top)],[(x,y),(right,top),(x,top)]]
    total=0
    for vertices,indices,_ in scene.batches.values():
        for offset in range(0,len(indices),3):
            points=[vertices[index] for index in indices[offset:offset+3]]
            if not all(abs(v[2]-z)<1e-7 for v in points):continue
            projected=[(v[0],v[1]) for v in points]
            for tri in clip:
                cut=intersection(projected,tri)
                if len(cut)>=3:total+=area(cut)
    return total,(right-x)*(top-y)


def surface_case(scene,room,source,owner,check='prop-surfaces'):
    raw_tri,raw=overlaps(scene)
    native=native_scene(scene);native_tri,quantized=overlaps(native)
    return dict(room=room,source=dict(kind=source.kind,x=source.x,y=source.y),
                owner=owner,check=check,raw_triangles=raw_tri,
                native_triangles=native_tri,raw=raw,native=quantized)


def validate():
    cases=[]
    with tempfile.TemporaryDirectory(prefix='poketcg-prop-models-') as directory:
        old_tex=world.TEX;world.TEX=Path(directory)
        try:
            sources=[(name,world.Image(world.REPO/'port/maps'/(name+'.png')),props)
                     for name,props in world.PROPS.items()]
            sources.append(('club_lobby',world.Image(world.REPO/'port/maps/fighting_club_lobby.png'),world.LOBBY))
            sources.append(('mason_laboratory',world.Image(world.REPO/'port/maps/events/mason_laboratory_challenge_machine.png'),
                            [world.P('machine',10,0,4,5,2.2,2,floor=world.F(10,2,12,2))]))
            for name,image,props in sources:
                for source in props:
                    p=transformed(source);probe=world.Scene(name);owner=None
                    for author,builder in OWNERS:
                        if builder(probe,p,source_p=source,img=image,texture=world.texture):
                            owner=author;break
                    if owner is None:continue
                    # Exercise actual production dispatch and prop transforms.
                    scene=world.Scene(name);world.prop(scene,image,source)
                    cases.append(surface_case(scene,name,source,owner))
            base=world.Image(world.REPO/'port/maps/deck_machine_room.png')
            lit=world.Image(world.REPO/'port/maps/events/deck_machine_room_all_deck_machines.png')
            switch_count=0
            for source in world.PROPS['deck_machine_room']:
                p=transformed(source)
                shell=world.Scene('deck_machine_room_dynamic');shell.switchable=True
                world.prop(shell,base,source)
                cases.append(surface_case(shell,shell.name,source,'lab','dynamic-shell'))
                switched=((source.y==1 and source.x in (6,10,14,18)) or
                          (source.y in (8,16) and source.x in (14,18)))
                if not switched:continue
                switch_count+=1;rect=deck_screen_rect(p)
                for native,test_scene in ((False,shell),(True,native_scene(shell))):
                    filled,_=state_area(test_scene,rect,native=native)
                    if filled>1e-6:raise AssertionError(('Dynamic screen already owned by shell',source,native,filled))
                fronts=[]
                for state,image in (('off',base),('on',lit)):
                    front=world.Scene('deck_machine_'+state)
                    build_deck_front(front,p,source_p=source,img=image,texture=world.texture)
                    fronts.append(front.batches)
                    combined=copy.deepcopy(shell)
                    build_deck_front(combined,p,source_p=source,img=image,texture=world.texture)
                    case=surface_case(combined,shell.name,source,'lab','dynamic-screen-'+state)
                    for native,test_scene in ((False,combined),(True,native_scene(combined))):
                        filled,expected=state_area(test_scene,rect,native=native)
                        if abs(filled-expected)>1e-6:
                            raise AssertionError(('Screen missing or multiply owned',source,state,native,filled,expected))
                    cases.append(case)
                if fronts[0]==fronts[1]:raise AssertionError(('Original off/on screen not represented',source))
            assert switch_count==8,switch_count
        finally:world.TEX=old_tex
    failures=sum(len(case['raw'])+len(case['native']) for case in cases)
    return dict(passed=failures==0,cases=len(cases),
                native_conversion='L643 float32; trunc(position * 32) / 32',
                raw_overlap_pairs=sum(len(c['raw']) for c in cases),
                native_overlap_pairs=sum(len(c['native']) for c in cases),
                independent_printer_states=switch_count,checks=cases)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report',type=Path)
    args=parser.parse_args();result=validate()
    if args.report:
        args.report.parent.mkdir(parents=True,exist_ok=True)
        args.report.write_text(json.dumps(result,indent=2)+'\n')
    print('PROP MODELS '+('PASS' if result['passed'] else 'FAIL')+
          f": {result['cases']} source/state cases, {result['raw_overlap_pairs']} raw and "
          f"{result['native_overlap_pairs']} native competing pairs; {result['independent_printer_states']} independent screens")
    for case in result['checks']:
        if case['raw'] or case['native']:print(json.dumps(case))
    if not result['passed']:raise SystemExit(1)


if __name__=='__main__':main()
