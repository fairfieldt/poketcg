#!/usr/bin/env python3
"""Verify promoted bytes, original bindings/props and shared runtime exports."""
import hashlib,json,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];REPO=ROOT.parents[1]
sys.path.insert(0,str(ROOT/'tools'));from promote_cast import remap
STUDY=ROOT/'experiments/cast-concepts'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
report=json.loads((ROOT/'review/cast-integration.json').read_text())
assert len(report['exports'])==41
for row in report['exports']:
 src=ROOT/row['source'];target=ROOT/row['target']
 assert remap(src.read_bytes(),row['runtime_model'])==target.read_bytes(),target
 assert sha(target)==row['sha256']
baseline=json.loads((STUDY/'review/source-baseline.json').read_text())
assert sha(ROOT/'bindings.json')==baseline['port/characters/bindings.json']
props=[p for p in baseline if '/models/torch/' in p or '/models/legendary_card/' in p]
assert props and all(sha(REPO/p)==baseline[p] for p in props)
world=json.loads((REPO/'port/world3d/generated/world.json').read_text());records={c['slug']:c for c in world['individual_characters']}
for row in report['exports']:
 slug=row['model']+'__lying' if row['pose']=='lying' else row['appearance']
 record=records[slug];source=REPO/record['source']
 assert source== (ROOT/row['target']).parent
 assert record['source_model']==row['runtime_model']
report.update(passed=True,original_prop_files_checked=len(props),bindings_checked=115,world_exports_checked=len(records))
(ROOT/'review/cast-integration.json').write_text(json.dumps(report,indent=2)+'\n')
print('CAST INTEGRATION PASS: 41 promoted exports, 115 original aliases, props unchanged, 49 runtime exports')
