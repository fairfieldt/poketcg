#!/usr/bin/env python3
"""Stage isolated face variants; never modify production assets or exports."""
import argparse,hashlib,json,shutil,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[5]
GENERATED=ROOT/'port/world3d/generated'
EXPERIMENT=ROOT/'port/characters/experiments/face-readability'

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--mksprite',default='/opt/libdragon/bin/mksprite')
    args=parser.parse_args();manifest=json.loads((EXPERIMENT/'manifest.json').read_text())
    world=json.loads((GENERATED/'world.json').read_text())
    records=[next(row for row in world['individual_characters'] if row['model']==model) for model in (7,9,12,8,14,43)]
    evidence=[]
    for option in manifest['options']:
        base=args.output/option['id'];(base/'maps').mkdir(parents=True,exist_ok=True);(base/'textures').mkdir(exist_ok=True)
        for record in records:
            source=GENERATED/record['file'];dest=base/'maps'/Path(record['file']).name
            shutil.copy2(source,dest);assert source.read_bytes()==dest.read_bytes()
        for token in sorted({token for record in records for token in record['textures']}):
            source=EXPERIMENT/option['native_replacements'][token]
            # The original mesh's texture name is scoped by the option root.
            png=base/'textures'/(token+'.png');shutil.copy2(source,png)
            subprocess.run([args.mksprite,'-f','RGBA16','-o',str(png.parent),str(png)],check=True,stdout=subprocess.DEVNULL)
            png.unlink()
            if option['id']=='baseline':assert source.read_bytes()==(GENERATED/'textures'/(token+'.png')).read_bytes()
        evidence.append(dict(option=option['id'],models=[dict(model=r['model'],file=r['file'],sha256=hashlib.sha256((GENERATED/r['file']).read_bytes()).hexdigest()) for r in records]))
    (args.output/'geometry-proof.json').write_text(json.dumps(dict(passed=True,geometry_unchanged=True,baseline_textures_exact=True,options=evidence),indent=2)+'\n')
    print('FACE PREVIEW ASSETS: 4 options, 6 unchanged models, isolated RGBA16 textures')

if __name__=='__main__':main()
