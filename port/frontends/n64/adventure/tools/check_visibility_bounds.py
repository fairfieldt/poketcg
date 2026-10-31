#!/usr/bin/env python3
"""Prove authored actor rigs/shadows fit the conservative draw-culling spheres."""
from pathlib import Path
import math
import re
import sys

ADV=Path(__file__).resolve().parents[1]
WORLD=ADV.parents[2]/'world3d'
sys.path.insert(0,str(WORLD/'tools'))
from build_world import read_l643

header=(ADV/'visibility.h').read_text()
def constant(name):return float(re.search(r'#define '+name+r' ([\d.]+)f',header)[1])
center=constant('ADVENTURE_ACTOR_CENTER')
radius=constant('ADVENTURE_ACTOR_RADIUS')
tilted=constant('ADVENTURE_TILTED_ACTOR_RADIUS')
models=set();vertices=0;maximum=0;maximum_tilted=0
for path in [WORLD/'generated/actors.l64',*sorted((WORLD/'generated/maps').glob('actor_*.l64')),
             *sorted((WORLD/'generated/lod').glob('actor_*.l64'))]:
    _,meshes=read_l643(path)
    for model,part,_,pivot,points,_ in meshes:
        models.add(model)
        for vertex in points:
            vertices+=1
            x,y,z=vertex[:3];px,py,pz=pivot
            # Allow unrestricted limb rotation, broader than the actual ±24°.
            if part:
                bound=math.hypot(x+px,math.hypot(y,z)+math.hypot(py-center,pz))
                root=math.hypot(x+px,math.hypot(y,z)+math.hypot(py,pz))
            else:
                bound=math.sqrt((x+px)**2+(y+py-center)**2+(z+pz)**2)
                root=math.sqrt((x+px)**2+(y+py)**2+(z+pz)**2)
            # Lean rotates the sphere center around the root by at most 6°.
            lean=2*center*math.sin(math.radians(3))
            bound+=lean
            # Legacy tilt adds 1 unit at the root before arbitrary X rotation.
            tilt_bound=root+abs(center-1)+lean
            maximum=max(maximum,bound);maximum_tilted=max(maximum_tilted,tilt_bound)
            assert bound<radius,(path,model,part,bound,radius)
            assert tilt_bound<tilted,(path,model,part,tilt_bound,tilted)
# Ground shadows extend .56X/.40Z atY.045, and remain in the standing sphere.
assert math.sqrt(.56**2+.4**2+(center-.045)**2)<radius
print(f'ACTOR BOUNDS PASS: {len(models)} models/{vertices} vertices; unrestricted limb swing+6deg lean <= {maximum:.4f}/{radius}; legacy tilt <= {maximum_tilted:.4f}/{tilted}; shadows enclosed')
