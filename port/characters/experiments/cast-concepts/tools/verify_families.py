#!/usr/bin/env python3
"""Independent full-cast native evidence and topology checks."""
import argparse,collections,hashlib,json,sys
from pathlib import Path
from PIL import Image
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT.parent/'mark-concepts/tools'))
from audit_exports import audit

def topology(path):
    heads=[]
    for mesh in json.loads(path.read_text())['meshes']:
        if not any(s in mesh['name'].lower() for s in ('head','scalp')):continue
        edges=collections.Counter();oriented=collections.Counter()
        for f in mesh['faces']:
            for i,x in enumerate(f['idx']):
                y=f['idx'][(i+1)%len(f['idx'])];key=tuple(sorted((x,y)));edges[key]+=1;oriented[key]+=1 if x<y else -1
        faces=[f for f in mesh['faces'] if (f['mat']=='hair' or f['mat'].endswith('scalp hair'))];adj={i:set() for i in range(len(faces))}
        owners=collections.defaultdict(list)
        for i,f in enumerate(faces):
            for k,x in enumerate(f['idx']):owners[tuple(sorted((x,f['idx'][(k+1)%len(f['idx'])])))].append(i)
        for own in owners.values():
            for i in own:adj[i].update(set(own)-{i})
        seen=set();components=0
        for start in adj:
            if start in seen:continue
            components+=1;stack=[start]
            while stack:
                k=stack.pop()
                if k in seen:continue
                seen.add(k);stack.extend(adj[k]-seen)
        heads.append(dict(name=mesh['name'],closed=bool(edges) and all(n==2 for n in edges.values()),oriented=all(v==0 for v in oriented.values()),hair_components=components))
    assert heads,('No owning head found',path)
    return heads

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('slugs',nargs='*');p.add_argument('--complete',action='store_true');a=p.parse_args()
    plan=json.loads((ROOT/'full-cast-plan.json').read_text());expected=sum(plan['groups'].values(),[]);slugs=a.slugs or expected
    report=dict(passed=True,complete=False,expected_families=len(slugs),families=[],missing=[])
    for slug in slugs:
        base=ROOT/'models'/slug
        completed=sorted(p for p in base.glob('i*') if (p/'native/capture.json').is_file())
        if not all((base/f'i{n}/native/capture.json').is_file() for n in (1,2,3)):
            report['missing'].append(slug)
        records=[]
        for folder in completed:
            target=folder/slug
            if not (target/'scene.l64').is_file():continue
            rec=audit(target/'scene.l64');assert rec['passed'],rec
            cap=json.loads((folder/'native/capture.json').read_text());assert len(cap['frames'])==8,(slug,folder,len(cap['frames']))
            for name in cap['frames']:
                with Image.open(folder/'native'/f'{name}.png') as im:assert im.size==(320,240)
            work=ROOT/'work'/f'family-{slug}-{folder.name}'
            staged=work/'build/filesystem/options/candidate/maps/actor_mark.l64'
            assert staged.read_bytes()==(target/'scene.l64').read_bytes(),staged
            assert (folder/f'{slug}.blend').is_file(),folder
            assert (folder/'source.py').is_file(),folder
            assert (folder/'analysis.md').is_file(),folder
            records.append(dict(iteration=folder.name,triangles=rec['triangles'],model_sha256=rec['sha256'],rom_sha256=cap['rom_sha256'],capture_inputs_match=True,native_frames=8,topology=topology(target/'authoring.json')))
        finals=[x for x in records if x['iteration'].startswith('i3')]
        if finals:
            last=finals[-1]
            assert all(h['closed'] and h['oriented'] and h['hair_components']<=1 for h in last['topology']),(slug,last)
            assert (ROOT/'concepts'/f'{slug}.png').is_file(),slug
        report['families'].append(dict(slug=slug,rounds=records,final=finals[-1]['iteration'] if finals else None))
    report['complete']=not report['missing']
    sources=json.loads((ROOT/'review/source-baseline.json').read_text());repo=ROOT.parents[3]
    changed=[p for p,h in sources.items() if hashlib.sha256((repo/p).read_bytes()).hexdigest()!=h]
    assert not changed,changed
    report['source_runtime_files_unchanged']=len(sources)
    name='full-cast-verification.json' if not a.slugs else 'families-'+ '-'.join(a.slugs)+'.json'
    (ROOT/'review'/name).write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k!='families'},indent=2))
    if a.complete:assert report['complete'],report['missing']
if __name__=='__main__':main()
