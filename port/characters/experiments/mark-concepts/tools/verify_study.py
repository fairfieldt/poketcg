#!/usr/bin/env python3
"""Verify retained exports against staged capture inputs and owning head topology."""
import collections,hashlib,json
from pathlib import Path
from PIL import Image
from audit_exports import audit
ROOT=Path(__file__).resolve().parents[1]
CONCEPTS=['a_adventure','b_compact','c_lean','d_angular']
results=[]
for concept in CONCEPTS:
    base=ROOT/'approved'/concept
    assert all((base/f'i{r}/native/capture.json').is_file() for r in (1,2,3)),concept
    for folder in sorted(base.glob('i*')):
        if not (folder/'native/capture.json').is_file():continue
        key=f'{concept}_{folder.name}';work=ROOT/'work'/f'{concept}-{folder.name}'
        model=folder/'mark/scene.l64';r=audit(model);assert r['passed'],r
        staged=work/'build/filesystem/options'/key/'maps/actor_mark.l64'
        assert staged.read_bytes()==model.read_bytes(),staged
        cap=json.loads((folder/'native/capture.json').read_text());assert len(cap['frames'])==16
        for frame in cap['frames']:
            with Image.open(folder/'native'/f'{frame}.png') as im:assert im.size==(320,240)
        assert (folder/'mark.blend').is_file(),folder
        for control in ('dr_mason','ronald'):
            expected=ROOT.parents[3]/'port/world3d/generated/maps'/f'actor_{control}.l64'
            assert (folder/control/'scene.l64').read_bytes()==expected.read_bytes()
        head_info=[]
        for mesh in json.loads((folder/'mark/authoring.json').read_text())['meshes']:
            if 'head' not in mesh['name']:continue
            edges=collections.Counter(tuple(sorted((f['idx'][i],f['idx'][(i+1)%len(f['idx'])]))) for f in mesh['faces'] for i in range(len(f['idx'])))
            faces=[f for f in mesh['faces'] if f['mat']=='hair'];adj={i:set() for i in range(len(faces))}
            for i,f in enumerate(faces):
                for j,g in enumerate(faces[:i]):
                    if len(set(f['idx'])&set(g['idx']))>=2:adj[i].add(j);adj[j].add(i)
            seen=set();components=0
            for start in adj:
                if start in seen:continue
                components+=1;stack=[start]
                while stack:
                    k=stack.pop()
                    if k in seen:continue
                    seen.add(k);stack.extend(adj[k]-seen)
            head_info.append(dict(name=mesh['name'],closed=all(n==2 for n in edges.values()),hair_components=components))
        results.append(dict(concept=concept,iteration=folder.name,triangles=r['triangles'],model_sha256=r['sha256'],rom_sha256=cap['rom_sha256'],native_frames=len(cap['frames']),staging_matches=True,controls_unchanged=True,export_passed=True,head=head_info))
    final=sorted(p for p in base.glob('i3*') if (p/'native/capture.json').is_file())[-1]
    result=next(r for r in results if r['concept']==concept and r['iteration']==final.name)
    assert all(h['closed'] and h['hair_components']==1 for h in result['head']),result
report=dict(passed=True,primary_rounds=12,retained_captured_versions=len(results),native_frames=sum(r['native_frames'] for r in results),models=results)
(ROOT/'review/verification.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:v for k,v in report.items() if k!='models'},indent=2))
