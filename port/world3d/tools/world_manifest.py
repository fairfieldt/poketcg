"""Shared, atomic metadata writers for full world generation and model sync."""
import json
import os
from pathlib import Path
import tempfile


def write_if_changed(path, data):
    path=Path(path)
    if isinstance(data,str):data=data.encode()
    if path.exists() and path.read_bytes()==data:return False
    path.parent.mkdir(parents=True,exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=path.parent,prefix='.'+path.name+'.',suffix='.tmp',delete=False) as f:
        f.write(data);temp=Path(f.name)
    os.replace(temp,path)
    return True


def write_world_metadata(out,doc):
    maps=doc['maps'];models=doc['npc_models'];nodes=doc['overworld_nodes'];events=doc['events'];switches=doc['switches']
    header=['/* Generated world metadata; updated by build_world.py / sync_characters.py. */',
            '#ifndef WORLD3D_MAPS_GENERATED_H','#define WORLD3D_MAPS_GENERATED_H','#include "../world3d.h"',
            'static const W3Map w3_maps[W3_MAP_COUNT] = {']
    def fv(v):return '{'+','.join(f'{x:.6f}f' for x in v)+'}'
    for d in maps:
        c=d['camera'];overlay='"'+d['overlay']+'"' if d['overlay'] else '0'
        header.append('    {"'+d['scene']+'",'+overlay+','+fv(c['eye'])+','+fv(c['target'])+','+str(c['fov'])+'.0f,'+str(d['width'])+','+str(d['height'])+'}, /* '+d['name']+' */')
    header+=['};','static const unsigned char w3_npc_models[256] = {']
    for i in range(0,256,16):header.append('    '+','.join(map(str,models[i:i+16]))+',')
    header+=['};','static const unsigned char w3_npc_pose_models[256] = {']
    poses=doc.get('npc_pose_models',[0]*256)
    for i in range(0,256,16):header.append('    '+','.join(map(str,poses[i:i+16]))+',')
    header+=['};','static const W3WorldNode w3_world_nodes[12] = {']
    for n in nodes:header.append('    {'+','.join(str(n[k]) for k in ('selection','map','x','y'))+','+str(n['marker_x'])+'f,'+str(n['marker_z'])+'f},')
    header+=['};','static const W3EventScene w3_event_scenes[] = {']
    for e in events:header.append('    {'+str(e['map'])+','+str(e['event'])+',"'+e['scene']+'"},')
    header+=['};','#define W3_EVENT_SCENE_COUNT '+str(len(events)), '#define W3_DECK_MACHINE_BASE "rom:/world/maps/deck_machine_room_dynamic.l64"', 'static const W3SwitchScene w3_switch_scenes[] = {']
    for e in switches:header.append('    {'+str(e['map'])+','+str(e['event'])+',"'+e['off']+'","'+e['on']+'"},')
    header+=['};','#define W3_SWITCH_SCENE_COUNT 8',
             '#define W3_PLAYER_MODEL '+str(doc.get('player_model',1)),
             'static const char *const w3_actor_scenes[256] = {']
    for c in doc.get('individual_characters',[]):
        header.append('    ['+str(c['model'])+'] = "rom:/world/'+c['file']+'", /* '+c['slug']+' */')
    header+=['};','#endif']
    # Actor files and textures must already exist before publishing the header.
    write_if_changed(Path(out)/'world3d_maps.h','\n'.join(header)+'\n')
    write_if_changed(Path(out)/'world.json',json.dumps(doc,indent=2)+'\n')
    # Optional gameplay-detail exports are accepted only for the exact current
    # source mesh. New character exports keep working through the full model.
    from build_character_lods import update_header
    update_header(Path(out))
