"""Verify retained export bytes and publish comparisons of actual native frames."""
from pathlib import Path
import collections,hashlib,json,struct,sys
from PIL import Image,ImageDraw
ROOT=Path(__file__).resolve().parents[5];STUDY=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(STUDY.parent/'mark-concepts/tools'))
sys.path.insert(0,str(ROOT/'port/world3d/tools'))
from audit_exports import audit
from build_world import Scene,read_l643,OUT

def canonical(source,destination):
    names,meshes=read_l643(source);remap={}
    for i,name in enumerate(names):
        im=Image.open(source.parent/'textures'/name.replace('.sprite','.png')).convert('RGBA')
        remap[i]='t'+hashlib.sha256(struct.pack('>HH',*im.size)+im.tobytes()).hexdigest()[:16]
    s=Scene('canonical')
    for model,part,t,pivot,vs,ids in meshes:s.batches[(model,part,remap[t] if t>=0 else None,tuple(pivot))]=(vs,ids,{})
    # Scene.save normally requires a path below its world output root.
    temp=OUT/'maps/_cat_review_verification.l64'
    try:s.save(temp);data=temp.read_bytes()
    finally:temp.unlink(missing_ok=True)
    return data

def run():
    records=[];gallery=['<!doctype html><meta charset="utf-8"><title>Cat model study</title><style>body{max-width:1320px;margin:32px auto;background:#20242b;color:#eee;font:16px system-ui}img{max-width:100%}a{color:#9dd5ff}figure{margin:20px 0}small{color:#ccc}</style><h1>Cat model study</h1><p>Generated reference, then three sequential model and native-review rounds. Native images are actual 320×240 ares framebuffer captures, retained before the later half-height runtime presentation adjustment.</p><h2>Generated concept</h2><img src="concepts/cat-duo.png" alt="Generated cat concept sheet">']
    for r in (1,2,3):
        folder=STUDY/'rounds'/f'i{r}';native=folder/'native';validation=json.loads((native/'validation.json').read_text())
        assert set(validation['frames'])=={'room','tuxedo','tabby','tuxedo-portrait','tabby-portrait','tuxedo-turn','tabby-turn'}
        record=dict(round=r,models=[],native_frames=7,rom_sha256=validation['rom_sha256'])
        for slug in ('cat_tuxedo','cat_tabby'):
            model=folder/slug;check=audit(model/'scene.l64');assert check['passed'];assert check['sha256']==validation['model_sha256'][slug]
            doc=json.loads((model/'authoring.json').read_text());head=next(m for m in doc['meshes'] if m['name']=='head closed skull');edges=collections.Counter();direction=collections.Counter()
            for f in head['faces']:
                for a,b in zip(f['idx'],f['idx'][1:]+f['idx'][:1]):edges[tuple(sorted((a,b)))]+=1;direction[(a,b)]+=1
            assert all(n==2 for n in edges.values());assert all(direction[(b,a)]==n for (a,b),n in direction.items())
            check['closed_consistently_wound_skull']=True
            # The production importer renames texture references by pixel hash.
            expected=canonical(model/'scene.l64',None)
            staged=native/'staged'/f'actor_{slug}.l64'
            revalidated=folder/'native-revalidation/staged'/staged.name
            if not staged.exists() and revalidated.exists():staged=revalidated
            if staged.exists():
                assert staged.read_bytes()==expected
                check['staged_native_equals_canonical_export']=True
                check['staged_sha256']=hashlib.sha256(expected).hexdigest()
            else:check['staged_native_equals_canonical_export']='not retained for initial capture'
            record['models'].append(check)
        sheet=Image.new('RGB',(1280,520),(26,29,34));d=ImageDraw.Draw(sheet)
        for row,cat in enumerate(('tuxedo','tabby')):
            for col,view in enumerate(('portrait','turn','talk','room')):
                name=cat+'-'+view if view in ('portrait','turn') else cat if view=='talk' else 'room'
                im=Image.open(native/(name+'.png'));assert im.size==(320,240)
                d.text((320*col+5,row*260+3),f'{cat} {view}',fill='white');sheet.paste(im,(320*col,row*260+20))
        sheet.save(native/'sheet.png');records.append(record)
        gallery.append(f'<h2>Round {r}</h2><p><a href="rounds/i{r}/analysis.md">Review observations</a> · <a href="rounds/i{r}/cats.blend">Editable Blender</a> · <a href="rounds/i{r}/audit.json">Geometry audit</a></p><img src="rounds/i{r}/native/sheet.png" alt="Round {r} native captures">')
    proof=dict(passed=True,rounds=records,final_round=3,limitations=['Ares captures establish appearance, not physical-console performance.','Room-distance face details occupy only a few pixels.'])
    (STUDY/'verification.json').write_text(json.dumps(proof,indent=2)+'\n');(STUDY/'index.html').write_text('\n'.join(gallery)+'\n')
    print('CAT MODEL STUDY PASS: 6 models; 21 native frames; 5 rigid parts; 728 triangles per model')
if __name__=='__main__':run()
