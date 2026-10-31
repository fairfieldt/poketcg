#!/usr/bin/env python3
"""Check authored prop solids against canonical source anchors and walking edges.

Source coordinates are actor centres at even x/y, with two units per movement
step. solid_bounds is [left,north,right,south] in world units, independently of
art crops. Circle/capsule clearance defaults to .65. Permission event states
come directly from generated portable-C source. No geometry is modified.
"""
import argparse
import hashlib
from itertools import combinations
import json
import math
from pathlib import Path
import re
import sys

WORLD_ROOT=Path(__file__).resolve().parents[1]
REPO=WORLD_ROOT.parents[1]
EPS=1e-6


def source_patches(path):
    source=path.read_text()
    values={int(index):[int(v) for v in re.findall(r'\d+',body)]
            for index,body in re.findall(r'const uint8_t patch_permissions_(\d+)\[\] = \{(.*?)\};',source,re.S)}
    result=[]
    for entry in re.findall(r'\{(\d+),(\d+),(\d+),(\d+),(\d+),patch_permissions_(\d+)\}',source):
        mid,x,y,w,h,event=map(int,entry)
        data=values[event]
        assert len(data)==((w+1)//2)*((h+1)//2),(event,w,h,data)
        result.append(dict(map=mid,x=x,y=y,w=w,h=h,event=event,values=data))
    assert len(result)==11,'All eleven source permission patches must be available'
    return result


def grid_for(data,patches,enabled):
    grid=[list(row) for row in data['permissions']]
    for patch in patches:
        if patch['map']!=data['id'] or patch['event'] not in enabled:continue
        stride=(patch['w']+1)//2
        for index,value in enumerate(patch['values']):
            x=patch['x']//2+index%stride;y=patch['y']//2+index//stride
            grid[y][x]=value
    return grid


def scene_key(path):return Path(path).stem


def variants(world,data,patches):
    relevant=[patch['event'] for patch in patches if patch['map']==data['id']]
    unique={}
    for count in range(len(relevant)+1):
        for enabled in combinations(relevant,count):
            enabled=set(enabled);grid=grid_for(data,patches,enabled)
            scene=scene_key(data['scene'])
            if data['id']==2 and 'deck_machine_room_dynamic' in world['meshes']:
                scene='deck_machine_room_dynamic'
            else:
                for event in world.get('events',[]):
                    if event['map']==data['id'] and event['event'] in enabled:scene=scene_key(event['scene'])
            key=(scene,tuple(tuple(value&128 for value in row) for row in grid))
            if key not in unique:unique[key]=dict(scene=scene,grid=grid,enabled=sorted(enabled),states=0)
            unique[key]['states']+=1
    # The combined exported deck-machine room shares its bodies with the
    # dynamic production shell, but remains an independently validated asset.
    for event in world.get('events',[]):
        if data['id']==2 and event['map']==2:
            grid=grid_for(data,patches,set(relevant));scene=scene_key(event['scene'])
            key=(scene,tuple(tuple(value&128 for value in row) for row in grid))
            unique.setdefault(key,dict(scene=scene,grid=grid,enabled=sorted(relevant),states=1))
    return list(unique.values())


def rectangle_distance(bounds,start,end=None):
    """Exact distance from an axis-aligned walking segment to a rectangle."""
    if end is None:end=start
    assert abs(start[0]-end[0])<EPS or abs(start[1]-end[1])<EPS
    left,north,right,south=bounds
    low_x,high_x=sorted((start[0],end[0]));low_z,high_z=sorted((start[1],end[1]))
    dx=max(left-high_x,low_x-right,0);dz=max(north-high_z,low_z-south,0)
    return math.hypot(dx,dz)


def offstage_start(data,npc):
    # Preload_Ronald1InPokemonDome stages him behind the north wall before
    # the finale. .ows_fae9 opens the Hall door; NPCMovement_fbd2 walks west
    # along row0 from(30,0) to(16,0), then its shared tail enters south through
    # that door. The 3D north wall occludes this backstage staging position.
    return (data['id']==32 and npc['name']=='NPC_RONALD1' and
            npc['x']==30 and npc['y']==0 and npc.get('conditional',False))


def check(world,patches,radius):
    failures=[];totals=dict(maps=0,variants=0,permission_states=0,solids=0,anchors=0,edges=0,npc_starts=0,offstage_starts=0)
    def fail(data,variant,prop,index,kind,**extra):
        failures.append(dict(map=data['id'],name=data['slug'],scene=variant['scene'],events=variant['enabled'],
                             prop=index,prop_kind=prop.get('kind'),check=kind,**extra))
    for data in world['maps']:
        if not data['permissions']:continue
        totals['maps']+=1
        for variant in variants(world,data,patches):
            totals['variants']+=1;totals['permission_states']+=variant['states']
            grid=variant['grid'];points=[]
            for row,values in enumerate(grid):
                for col,value in enumerate(values):
                    if not value&128 and col*2<data['width'] and row*2<data['height']:points.append((col*2,row*2))
            point_set=set(points)
            edges=[(point,other) for point in points for other in ((point[0]+2,point[1]),(point[0],point[1]+2)) if other in point_set]
            npcs=[npc for npc in data['npcs'] if 0<=npc['x']<data['width'] and 0<=npc['y']<data['height'] and not 103<=npc['npc']<=109]
            totals['offstage_starts']+=sum(offstage_start(data,npc) for npc in npcs)
            npcs=[npc for npc in npcs if not offstage_start(data,npc)]
            totals['anchors']+=len(points);totals['edges']+=len(edges);totals['npc_starts']+=len(npcs)
            for index,prop in enumerate(world['meshes'][variant['scene']].get('props',[])):
                if prop.get('solid',True) is False:continue
                if prop.get('clearance_exempt'):
                    if data['id']!=33 or prop.get('kind') not in ('altar','pillar','torch','ball'):
                        fail(data,variant,prop,index,'invalid-clearance-exemption')
                    continue
                bounds=prop.get('solid_bounds')
                if not isinstance(bounds,list) or len(bounds)!=4 or not all(isinstance(v,(float,int)) and math.isfinite(v) for v in bounds) or bounds[2]<bounds[0] or bounds[3]<bounds[1]:
                    fail(data,variant,prop,index,'missing-or-invalid-solid-bounds');continue
                support=prop.get('supports_npcs',[])
                if support and not(data['id']==12 and prop.get('kind')=='bench' and support==[34]):
                    fail(data,variant,prop,index,'invalid-npc-support',supports_npcs=support)
                totals['solids']+=1
                for point in points:
                    distance=rectangle_distance(bounds,point)
                    if distance+EPS<radius:fail(data,variant,prop,index,'walkable-anchor',position=point,solid_bounds=bounds,distance=round(distance,5))
                for start,end in edges:
                    distance=rectangle_distance(bounds,start,end)
                    if distance+EPS<radius:fail(data,variant,prop,index,'walking-edge',start=start,end=end,solid_bounds=bounds,distance=round(distance,5))
                for npc in npcs:
                    if npc['npc'] in support:continue
                    distance=rectangle_distance(bounds,(npc['x'],npc['y']))
                    if distance+EPS<radius:fail(data,variant,prop,index,'npc-start',npc=npc['name'],position=[npc['x'],npc['y']],solid_bounds=bounds,distance=round(distance,5))
    return dict(passed=not failures,radius=radius,totals=totals,failures=failures)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--world',type=Path,default=WORLD_ROOT/'generated/world.json')
    parser.add_argument('--source',type=Path,default=REPO/'port/overworld/gen/data.c')
    parser.add_argument('--radius',type=float,default=.65)
    parser.add_argument('--report',type=Path)
    parser.add_argument('--max-errors',type=int,default=30)
    args=parser.parse_args()
    assert math.isfinite(args.radius) and args.radius>0,'Clearance radius must be positive'
    result=check(json.loads(args.world.read_text()),source_patches(args.source),args.radius)
    result['world_sha256']=hashlib.sha256(args.world.read_bytes()).hexdigest()
    result['permission_source_sha256']=hashlib.sha256(args.source.read_bytes()).hexdigest()
    if args.report:
        args.report.parent.mkdir(parents=True,exist_ok=True);args.report.write_text(json.dumps(result,indent=2)+'\n')
    for failure in result['failures'][:args.max_errors]:print(json.dumps(failure,separators=(',',':')))
    prefix='PROP CLEARANCE PASS' if result['passed'] else 'PROP CLEARANCE FAIL'
    print(f'{prefix}: radius={args.radius}; '+', '.join(f'{key}={value}' for key,value in result['totals'].items())+f'; failures={len(result["failures"])}')
    return 0 if result['passed'] else 1


if __name__=='__main__':sys.exit(main())
