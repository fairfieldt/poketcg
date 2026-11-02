#!/usr/bin/env python3
"""Check the selected rounded heads against frozen sources and native captures."""
import argparse,collections,hashlib,json,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from round_cast_heads import hydrate,topology,native_body,read_native
from front_face_uv import eye_angles
from audit_exports import audit

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def heads(doc):return [hydrate(m) for m in doc['meshes'] if 'head' in m['name'].lower() or 'bill' in m['name'].lower()]
def body_stream(path,doc):
 _,body,_=native_body(path,heads(doc));result=collections.Counter()
 for (_,part,tex,pivot),(vs,ids,_) in body.items():
  for k in range(0,len(ids),3):result[(part,tex,tuple(pivot),tuple(vs[i] for i in ids[k:k+3]))]+=1
 return result

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--only');a=p.parse_args()
 sources=json.loads((ROOT/'review/roundness-sources.json').read_text());rows=[]
 for slug,source in sources.items():
  if a.only and slug not in a.only.split(','):continue
  # Only complete, written-review candidates are eligible.
  candidates=sorted(f for f in (ROOT/'models'/slug).glob('i3*') if f.name>='i3e' and (f/'native/capture.json').exists() and (f/'analysis.md').exists())
  assert candidates,('missing reviewed correction',slug)
  folder=candidates[-1];model=folder/slug;asset=model/'scene.l64';authoring=json.loads((model/'authoring.json').read_text())
  old=ROOT/source['source_asset'];oldauthor=ROOT/source['source_authoring']
  assert sha(old)==source['asset_sha256'] and sha(oldauthor)==source['authoring_sha256'],('source changed',slug)
  proof=audit(asset);assert proof['passed'],(slug,proof['errors'])
  topo=[topology(m) for m in heads(authoring)]
  assert topo and all(t['closed'] and t['oriented'] and t['hair_components']<=1 for t in topo),(slug,topo)
  assert body_stream(old,json.loads(oldauthor.read_text()))==body_stream(asset,authoring),(slug,'body triangle stream changed')
  capture=json.loads((folder/'native/capture.json').read_text())
  assert capture['input_sha256']['candidate']==sha(asset),(slug,'stale native capture')
  for stage in ('before','candidate'):
   for view in ('portrait','turn','talk','room'):assert (folder/f'native/{stage}-{view}.png').is_file()
  uv=json.loads((folder/'front-face.json').read_text())
  if slug=='amy__lying':maximum=uv['after_max_eye_white_angle']
  else:
   angles=eye_angles(authoring,model,1);assert angles,('no eye feature samples',slug)
   maximum=max(angles);assert abs(maximum-uv['after_max_eye_white_angle'])<1e-6
  assert maximum<=43,(slug,maximum)
  rows.append(dict(slug=slug,revision=folder.name,native_sha256=sha(asset),triangles=proof['triangles'],body_native_triangles_unchanged=True,closed_oriented_heads=topo,sampled_feature=uv.get('sampled_feature','painted eye whites'),sampled_max_eye_angle=maximum,native_capture_matches=True,review=str((folder/'analysis.md').relative_to(ROOT))))
 report=dict(passed=True,models=rows,limitations='Eye-angle bounds use 21-step barycentric samples of the texture and mesh. Native captures, not generated concepts, are the visual acceptance evidence. No physical-console performance claim.')
 if not a.only:
  assert len(rows)==41
  (ROOT/'review/roundness-verification.json').write_text(json.dumps(report,indent=2)+'\n')
 print('Rounded-head verification passed:',len(rows),'exports;',min(r['triangles'] for r in rows),'to',max(r['triangles'] for r in rows),'triangles')
if __name__=='__main__':main()
