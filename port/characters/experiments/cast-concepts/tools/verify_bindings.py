#!/usr/bin/env python3
"""Resolve review candidates without changing original NPC relationships."""
import json,hashlib,os
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];REPO=ROOT.parents[3]
original=json.loads((REPO/'port/characters/bindings.json').read_text())
rows=json.loads((ROOT/'review/final-families.json').read_text());assert len(rows)==37
families={r['slug']:r for r in rows}
variants={r['slug']:r for r in json.loads((ROOT/'review/final-appearances.json').read_text())};assert len(variants)==4
out={};shared={};humans=0
for alias,binding in original.items():
    row=dict(binding);slug=binding['model']
    if slug in ('torch','legendary_card'):
        native=REPO/'port/characters'/binding['native_asset'];blend=REPO/'port/characters'/binding['blender_file'];selection='unchanged prop'
    else:
        humans+=1
        selected='amy__lying' if slug=='amy' and binding['pose']=='lying' else binding['appearance']
        target=variants.get(selected,families.get(selected));assert target,(alias,selected)
        native=ROOT/target['prefix']/selected/'scene.l64';blend=ROOT/target['prefix']/f'{selected}.blend';selection=selected
        key=(binding['model'],binding['appearance'],binding['pose'] if binding['pose']=='lying' else 'standing',binding['component'])
        pair=(str(native),str(blend));assert shared.setdefault(key,pair)==pair
    assert native.is_file(),native
    assert blend.is_file(),blend
    row.update(candidate_asset=os.path.relpath(native,ROOT),candidate_blender=os.path.relpath(blend,ROOT),candidate_selection=selection,candidate_sha256=hashlib.sha256(native.read_bytes()).hexdigest())
    assert {k:row[k] for k in binding}==binding
    out[alias]=row
report=dict(passed=True,binding_count=len(out),human_bindings=humans,unchanged_prop_bindings=len(out)-humans,all_original_fields_preserved=True,shared_alias_paths_identical=True,note='Review mapping only. Candidate L643 exports use diagnostic model ID 1; production integration must restore original runtime_model IDs. Original bindings remain unchanged.',bindings=out)
(ROOT/'review/final-bindings.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:v for k,v in report.items() if k!='bindings'},indent=2))
