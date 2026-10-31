#!/usr/bin/env python3
"""Verify individual-model ID isolation, aliases, exact mesh preservation and sync."""
from pathlib import Path
import copy
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
ROOT=Path(__file__).resolve().parents[1];REPO=ROOT.parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from build_world import read_l643,Image
from sync_characters import apply_characters,source_export
OUT=ROOT/'generated'
doc=json.loads((OUT/'world.json').read_text())
characters={c['slug']:c for c in doc['individual_characters']}
assert characters['ronald']['model']==8 and characters['dr_mason']['model']==9
assert characters['ronald']['source_model'] in (1,2) and characters['dr_mason']['source_model'] in (3,4)
assert doc['player_model']==characters.get('mark',{}).get('model',1)
assert doc['player_model']!=characters['ronald']['model'] and all(c['model']>=7 for c in characters.values())
# Legacy demo IDs must never overwrite player1 or fallback archetypes2..6.
_,fallback=read_l643(OUT/'actors.l64')
assert {m[0] for m in fallback}==set(range(1,7))
_,approved=read_l643(REPO/'port/frontends/n64/lightning/assets/scene.l64')
for expected in (m for m in approved if m[0] in range(1,5)):
    assert any(m==expected for m in fallback),('Fallback mesh changed',expected[:4])
for c in characters.values():
    source=REPO/c['source'];src_names,original=read_l643(source/'scene.l64')
    dst_names,converted=read_l643(OUT/c['file'])
    assert {m[0] for m in converted}=={c['model']}
    assert {m[1] for m in converted}=={m[1] for m in original}
    if c['slug'] in ('dr_mason','ronald'):assert {m[1] for m in converted}=={0,1,2,3,4}
    def texture_pixels(names,base):
        result={}
        for i,name in enumerate(names):
            path=base/name.replace('.sprite','.png')
            if not path.exists():path=REPO/'port/characters/assets/textures'/name.replace('.sprite','.png')
            result[i]=Image(path).rows
        return result
    src_tex=texture_pixels(src_names,source/'textures')
    dst_tex=texture_pixels(dst_names,OUT/'textures')
    for _,part,t,pivot,vertices,indices in original:
        image=src_tex[t] if t>=0 else None
        matches=[m for m in converted if m[1]==part and m[3]==pivot and (dst_tex[m[2]] if m[2]>=0 else None)==image]
        assert len(matches)==1 and matches[0][4:]==(vertices,indices),(c['slug'],part,'Geometry/UV/rig changed')
    for npc in c['npc_ids']:assert doc['npc_models'][npc]==c['model']
# Repeating a no-op sync must preserve all output mtimes and room/blend files.
paths=[OUT/'actors.l64',OUT/'world.json',OUT/'world3d_maps.h',OUT/'all_maps.blend',*list((OUT/'maps').glob('*.l64')),*list((OUT/'textures').glob('*.png'))]
before={str(p):(p.stat().st_mtime_ns,hashlib.sha256(p.read_bytes()).hexdigest()) for p in paths}
subprocess.run([sys.executable,str(ROOT/'tools/sync_characters.py'),'--strict'],check=True)
assert before=={str(p):(p.stat().st_mtime_ns,hashlib.sha256(p.read_bytes()).hexdigest()) for p in paths},'No-op sync rewrote assets'
# Automatically discover completed exports; ignore partial new exports; keep
# the last coherent import while an existing export is being replaced.
(ROOT/'build').mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(dir=ROOT/'build',prefix='character-test-') as temp:
    root=Path(temp)/'models';root.mkdir()
    shutil.copytree(REPO/'port/characters/assets/textures',Path(temp)/'textures')
    shutil.copytree(REPO/characters['dr_mason']['source'],root/'dr_mason')
    # Central exports additionally need their producer's last-written marker.
    marker=root/'dr_mason/complete.json'
    valid_marker=marker.read_bytes()
    assert source_export(root/'dr_mason','dr_mason',Path(temp)/'textures',marker,'dr_mason','dr_mason')
    marker.unlink()
    assert source_export(root/'dr_mason','dr_mason',Path(temp)/'textures',marker,'dr_mason','dr_mason') is None
    marker.write_bytes(valid_marker);os.utime(marker,ns=(1,1))
    try:source_export(root/'dr_mason','dr_mason',Path(temp)/'textures',marker,'dr_mason','dr_mason')
    except AssertionError:pass
    else:raise AssertionError('Stale completion marker was accepted')
    marker.write_bytes(valid_marker)
    base=copy.deepcopy(doc);base['individual_characters']=[]
    base['npc_models']=list(base['fallback_npc_models'])
    only_mason=apply_characters(base,roots=[root],strict=True)
    assert {c['slug'] for c in only_mason['individual_characters']}=={'dr_mason'}
    shutil.copytree(REPO/characters['ronald']['source'],root/'ronald')
    both=apply_characters(only_mason,roots=[root],strict=True)
    assert {c['slug'] for c in both['individual_characters']}=={'dr_mason','ronald'}
    partial=root/'ishihara';partial.mkdir();(partial/'scene.l64').write_bytes(b'L643')
    (root/'dr_mason/scene.l64').write_bytes(b'L643')
    retained=apply_characters(both,roots=[root])
    assert {c['slug'] for c in retained['individual_characters']}=={'dr_mason','ronald'}
    assert retained['npc_models']==both['npc_models']
print('CHARACTERS PASS: source geometry/UV/colors/pivots preserved; reserved player/fallback IDs; all aliases; idempotent auto-discovery; incomplete-export retention')
