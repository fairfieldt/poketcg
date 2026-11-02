#!/usr/bin/env python3
"""Generate every style option: native L643 exports, textures, manifests and Blender JSON."""
import json,sys
from pathlib import Path
HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(HERE))
import builder
from geom import write_l643

ROOT=HERE.parent
OPTIONS=ROOT/'options'

def main(only=None):
    summary=[]
    for order,(sid,style) in enumerate(builder.STYLES.items()):
        if only and sid not in only:continue
        folder=OPTIONS/sid;folder.mkdir(parents=True,exist_ok=True)
        (folder/'option.json').write_text(json.dumps(dict(id=sid,label=style.label,title=style.title,order=order),indent=2)+'\n')
        for slug,design in builder.DESIGNS.items():
            fig=builder.build(style,design)
            batches,textures=fig.finish()
            out=folder/slug;(out/'textures').mkdir(parents=True,exist_ok=True)
            stats=write_l643(out/'scene.l64',batches,textures)
            for name in textures:
                fig.textures[name].save(out/'textures'/(name+'.png'))
            manifest=dict(format='L643',textures=textures,actors=[dict(name=slug,model=1,x=0,z=0,heading=0)],colliders=[],
                          model=slug,style=sid,appearance=slug,pose='standing',**stats,
                          lighting=dict(ambient=style.light.ambient,diffuse=style.light.diffuse,direction=list(style.light.direction)),
                          parts=sorted({m.part for m in fig.meshes}),
                          pivots={str(m.part):list(m.pivot) for m in fig.meshes if m.part})
            (out/'scene.json').write_text(json.dumps(manifest,indent=2)+'\n')
            summary.append((sid,slug,stats))
            print(sid,slug,stats)
    return summary

if __name__=='__main__':main(sys.argv[1:] or None)
