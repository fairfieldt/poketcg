#!/usr/bin/env python3
"""Check special pool shapes and the transformed prototype's actual triangles."""
import json
from pathlib import Path

from build_world import Scene, read_l643, rgb
from check_prop_clearance import rectangle_distance
from solid_terrain import align_lightning_room, swimming_pool
from scene_specs import PROPS, footprint


ROOT = Path(__file__).resolve().parents[1]
REPO = ROOT.parents[1]
world = json.loads((ROOT/'generated/world.json').read_text())


def legal_paths(slug):
    data = next(data for data in world['maps'] if data['slug'] == slug)
    points = {(x*2, z*2) for z, row in enumerate(data['permissions'])
              for x, value in enumerate(row) if not value & 128}
    return [(point, point) for point in points]+[
        (point, other) for point in points
        for other in ((point[0]+2, point[1]), (point[0], point[1]+2))
        if other in points]


def clear(bounds, paths):
    minimum = min(rectangle_distance(bound, start, end)
                  for bound in bounds for start, end in paths)
    assert minimum >= .65-1e-6, minimum
    return minimum


pool = Scene('water_club')
swimming_pool(pool, lambda rows: 'water', rgb, rgb('efebda'))
pool_clearance = clear([prop['solid_bounds'] for prop in pool.props],
                       legal_paths('water_club'))
for prop in PROPS['water_club']:
    if prop.kind != 'shrub':
        continue
    x, z, width, depth = footprint(prop)
    center = (x+width/2, z+depth/2)
    assert min(rectangle_distance(row['solid_bounds'], center)
               for row in pool.props) >= .65-1e-6, 'Pool covers shrub ledge'

room = Scene('lightning_club')
_, meshes = read_l643(REPO/'port/frontends/n64/lightning/assets/scene.l64')
for model, part, texture, pivot, vertices, indices in meshes:
    room.batches[(model, part, texture, tuple(pivot))] = (list(vertices), list(indices), {})
actors = {key: value for key, value in room.batches.items() if key[0]}
align_lightning_room(room)
assert actors == {key: value for key, value in room.batches.items() if key[0]}, 'Actor geometry changed'
bounds = []
triangles = 0
for key, (vertices, indices, _) in room.batches.items():
    if key[0]:
        continue
    for offset in range(0, len(indices), 3):
        triangle = [vertices[i] for i in indices[offset:offset+3]]
        a, b, c = triangle
        ab = [b[i]-a[i] for i in range(3)]
        ac = [c[i]-a[i] for i in range(3)]
        cross = [ab[1]*ac[2]-ab[2]*ac[1], ab[2]*ac[0]-ab[0]*ac[2], ab[0]*ac[1]-ab[1]*ac[0]]
        assert sum(v*v for v in cross) > 1e-13, (key, offset, triangle)
        triangles += 1
        if max(v[1] for v in triangle) < .08 or min(v[1] for v in triangle) > 2.2:
            continue
        bounds.append((min(v[0] for v in triangle), min(v[2] for v in triangle),
                       max(v[0] for v in triangle), max(v[2] for v in triangle)))
lightning_clearance = clear(bounds, legal_paths('lightning_club'))
clear([prop['solid_bounds'] for prop in room.props], legal_paths('lightning_club'))
print(f'SOLID TERRAIN PASS: pool clearance {pool_clearance:.3f}, actual Lightning triangles {lightning_clearance:.3f}; '
      f'{triangles} nondegenerate room triangles, actor meshes unchanged')
