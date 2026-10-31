"""Validate the saved island's actual roof shells, using the native camera.

Run with Blender --background --python-exit-code 1 --python
port/world3d/tools/check_overworld_roofs.py.
No meshes, materials or saved artifacts are modified. Roof helpers identify
their geometry with roof_role='shell', roof_site and integer roof_tier.
"""
import argparse
from collections import defaultdict
import json
import math
from pathlib import Path
import sys

import bpy
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[1]
EPS = 1e-7


def cross2(a, b, c):
    return (b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0])


def area(points):
    return abs(sum(a[0]*b[1]-a[1]*b[0]
                   for a, b in zip(points, points[1:]+points[:1])))/2


def intersect(a, b):
    """Convex projected triangle intersection, without raster approximations."""
    subject = list(a)
    clip = list(b) if cross2(*b) > 0 else list(reversed(b))
    for start, end in zip(clip, clip[1:]+clip[:1]):
        result = []
        if not subject:
            break
        previous = subject[-1]
        pd = cross2(start, end, previous)
        for current in subject:
            cd = cross2(start, end, current)
            if (pd >= -EPS) != (cd >= -EPS):
                t = pd/(pd-cd)
                result.append(tuple(previous[i]+(current[i]-previous[i])*t for i in range(2)))
            if cd >= -EPS:
                result.append(current)
            previous, pd = current, cd
        subject = result
    return subject


def depth_at(triangle, point):
    a, b, c = triangle['screen']
    total = cross2(a, b, c)
    weights = (cross2(b, c, point)/total,
               cross2(c, a, point)/total,
               cross2(a, b, point)/total)
    # Screen-space barycentrics interpolate reciprocal camera depth.
    return 1/sum(weight/depth for weight, depth in zip(weights, triangle['depth']))


def vertex_key(vertex):
    return tuple(round(value, 6) for value in vertex)


def native_camera(camera):
    eye = Vector(camera['eye'])
    forward = (Vector(camera['target'])-eye).normalized()
    right = forward.cross(Vector((0, 1, 0))).normalized()
    up = right.cross(forward).normalized()
    focal = 120/math.tan(math.radians(camera['fov'])/2)

    def project(vertex):
        delta = vertex-eye
        depth = delta.dot(forward)
        assert depth > 0, 'Roof is behind the native camera'
        return ((160+delta.dot(right)*focal/depth,
                 120-delta.dot(up)*focal/depth), depth)
    return eye, project


def validate(world, clearance):
    failures = []
    shells = [obj for obj in bpy.context.scene.objects
              if obj.type == 'MESH' and obj.get('roof_role') == 'shell']
    required = {prop['name'] for prop in world['meshes']['overworld']['props']}
    sites = {str(obj.get('roof_site', '')) for obj in shells}
    for site in sorted(required-sites):
        failures.append(dict(check='missing-roof-site', site=site))
    for site in sorted(sites-required):
        failures.append(dict(check='unknown-roof-site', site=site))
    eye, project = native_camera(world['maps'][0]['camera'])
    ownership = defaultdict(list)
    duplicates = {}
    triangles = []
    near_faces = {}
    checked_faces = checked_edges = 0
    for obj in shells:
        site = str(obj.get('roof_site', ''))
        tier = obj.get('roof_tier')
        if not isinstance(tier, int) or isinstance(tier, bool) or tier < 0:
            failures.append(dict(check='invalid-roof-tier', object=obj.name, site=site, tier=tier))
            tier = -1
        ownership[site, tier].append(obj.name)
        data = obj.data
        edges = defaultdict(list)
        adjacent = defaultdict(set)
        for face in data.polygons:
            checked_faces += 1
            for a, b in zip(face.vertices, (*face.vertices[1:], face.vertices[0])):
                edges[tuple(sorted((a, b)))].append((face.index, a, b))
        for edge, uses in edges.items():
            checked_edges += 1
            if len(uses) != 2:
                failures.append(dict(check='nonmanifold-shell-edge', object=obj.name, site=site,
                                     edge=list(edge), face_count=len(uses)))
                continue
            (fa, a, b), (fb, c, d) = uses
            if a != d or b != c:
                failures.append(dict(check='inconsistent-shell-winding', object=obj.name, site=site,
                                     edge=list(edge), faces=[fa, fb]))
            adjacent[fa].add(fb)
            adjacent[fb].add(fa)
        unseen = set(range(len(data.polygons)))
        components = 0
        while unseen:
            components += 1
            pending = [unseen.pop()]
            while pending:
                for neighbor in adjacent[pending.pop()]:
                    if neighbor in unseen:
                        unseen.remove(neighbor)
                        pending.append(neighbor)
        if components != 1:
            failures.append(dict(check='disconnected-shell', object=obj.name, site=site, components=components))
        # A legitimate raised/recessed cap is connected through one wall.
        # Such neighboring faces can occlude one another without being layers.
        for face in data.polygons:
            close = {face.index, *adjacent[face.index]}
            for neighbor in adjacent[face.index]:
                close.update(adjacent[neighbor])
            near_faces[obj.name, face.index] = close
        data.calc_loop_triangles()
        vertices = [obj.matrix_world@vertex.co for vertex in data.vertices]
        vertices = [Vector((v.x, v.z, -v.y)) for v in vertices]
        for triangle in data.loop_triangles:
            points = [vertices[index] for index in triangle.vertices]
            key = tuple(sorted(vertex_key(point) for point in points))
            face = data.polygons[triangle.polygon_index]
            material = data.materials[face.material_index].name if data.materials else None
            record = dict(object=obj.name, site=site, tier=tier, face=face.index, material=material)
            if key in duplicates:
                failures.append(dict(check='duplicate-roof-triangle', first=duplicates[key], second=record))
            else:
                duplicates[key] = record
            normal = (points[1]-points[0]).cross(points[2]-points[0])
            if normal.length_squared < 1e-13:
                failures.append(dict(check='degenerate-roof-triangle', **record))
                continue
            normal.normalize()
            # Only upward, camera-facing exterior slopes/caps can form the
            # competing roof layers. Downward bottom caps remain manifold.
            if normal.y <= .05 or normal.dot(eye-sum(points, Vector((0, 0, 0)))/3) <= EPS:
                continue
            projected = [project(point) for point in points]
            screen = [value[0] for value in projected]
            if area(screen) <= 1e-5:
                continue
            triangles.append(dict(**record, screen=screen, depth=[value[1] for value in projected],
                                  normal=normal, keys=set(key)))
    for (site, tier), objects in ownership.items():
        if len(objects) != 1:
            failures.append(dict(check='multiple-shell-owners', site=site, tier=tier, objects=objects))
    samples = overlaps = 0
    for index, a in enumerate(triangles):
        for b in triangles[index+1:]:
            if (a['site'], a['tier']) != (b['site'], b['tier']):
                continue
            same_object = a['object'] == b['object']
            if same_object and b['face'] in near_faces[a['object'], a['face']]:
                continue
            if a['normal'].dot(b['normal']) < .65:
                continue
            polygon = intersect(a['screen'], b['screen'])
            if len(polygon) < 3 or area(polygon) < .1:
                continue
            overlaps += 1
            center = tuple(sum(point[i] for point in polygon)/len(polygon) for i in range(2))
            # Keep samples away from shared boundaries and top-left raster ties.
            points = [center]+[tuple(center[i]*.15+point[i]*.85 for i in range(2)) for point in polygon]
            differences = [depth_at(a, point)-depth_at(b, point) for point in points]
            samples += len(points)
            minimum = min(abs(value) for value in differences)
            crosses = min(differences) < -EPS and max(differences) > EPS
            if minimum < clearance or crosses:
                def label(value):
                    return {key:value[key] for key in ('object', 'site', 'tier', 'face', 'material')}
                failures.append(dict(check='near-facing-roof-overlap', first=label(a), second=label(b),
                                     area_pixels=round(area(polygon), 4),
                                     minimum_view_depth=round(minimum, 6), crossing=crosses))
    untagged = sorted(obj.name for obj in bpy.context.scene.objects
                      if obj.type == 'MESH' and obj.get('native_export') and not obj.get('roof_role')
                      and (any(word in obj.name.lower() for word in ('roof', 'eaves', 'crown'))
                           or obj.name.startswith(('Dome Pokeball badge', 'Dome badge', 'Science Club vent inset')))
                      and 'finial' not in obj.name.lower())
    for name in untagged:
        failures.append(dict(check='untagged-roof-surface', object=name))
    return dict(passed=not failures, camera=world['maps'][0]['camera'],
                minimum_view_depth=clearance,
                totals=dict(sites=len(sites), shells=len(shells), faces=checked_faces,
                            edges=checked_edges, facing_triangles=len(triangles),
                            overlap_pairs=overlaps, depth_samples=samples), failures=failures)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--blend', type=Path, default=ROOT/'generated/overworld.blend')
    parser.add_argument('--world', type=Path, default=ROOT/'generated/world.json')
    parser.add_argument('--report', type=Path, default=ROOT/'build/roof-check.json')
    parser.add_argument('--clearance', type=float, default=.12)
    args = parser.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
    assert math.isfinite(args.clearance) and args.clearance > 0
    bpy.ops.wm.open_mainfile(filepath=str(args.blend.resolve()))
    result = validate(json.loads(args.world.read_text()), args.clearance)
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(result, indent=2)+'\n')
    for failure in result['failures'][:30]:
        print(json.dumps(failure, separators=(',', ':')))
    prefix = 'OVERWORLD ROOFS PASS' if result['passed'] else 'OVERWORLD ROOFS FAIL'
    print(f'{prefix}: '+', '.join(f'{key}={value}' for key,value in result['totals'].items())+
          f'; failures={len(result["failures"])}; {args.report}')
    if not result['passed']:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
