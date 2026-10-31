#!/usr/bin/env python3
"""Resolve every NPC ID to a model, appearance, pose and native asset."""
import json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
catalog=json.loads((ROOT/'catalog.json').read_text())
families={f['slug']:f for f in catalog['families']}
bindings={}
for name,ref in catalog['aliases'].items():
    model=ref['model'];folder=ROOT/'assets/models'/model
    if ref['component']:folder=folder/'components'/ref['component']
    elif ref['pose']=='lying':folder=folder/'poses/lying'
    elif ref['appearance']!=model:folder=folder/'variants'/ref['appearance']
    asset=folder/'scene.l64';assert asset.exists(),name
    bindings[name]=dict(ref,runtime_model=families[model]['runtime_model'],
                       native_asset=str(asset.relative_to(ROOT)),
                       blender_file=f'assets/models/{model}/{model}.blend',
                       heading=270 if ref['pose']=='west_fixed' else None)
(ROOT/'bindings.json').write_text(json.dumps(bindings,indent=2)+'\n')
print('BINDINGS',len(bindings),'NPC IDs,',len({b['blender_file'] for b in bindings.values()}),'authored files')
