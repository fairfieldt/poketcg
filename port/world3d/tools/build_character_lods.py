#!/usr/bin/env python3
"""Build optional gameplay-distance character meshes with Blender.

The authored models and textured faces remain untouched. Open seams and volume
anchors are locked, with a color-boundary bias when simplifying solid surfaces.
Ordinary builds need only the checked-in output; a stale LOD is never selected.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import shutil

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'generated'
sys.path.insert(0, str(ROOT/'tools'))
from build_world import read_l643
from world_manifest import write_if_changed

def digest(path): return hashlib.sha256(path.read_bytes()).hexdigest()

def write_mesh(path, textures, meshes):
    data = bytearray(struct.pack('>4sHH', b'L643', len(meshes), len(textures)))
    for name in textures: data.extend(name.encode().ljust(32, b'\0'))
    for model, part, texture, pivot, vertices, indices in meshes:
        data.extend(struct.pack('>HHhHII3f', model, part, texture, 0, len(vertices), len(indices), *pivot))
        for vertex in vertices: data.extend(struct.pack('>5f4B', *vertex))
        data.extend(struct.pack('>'+str(len(indices))+'H', *indices))
        data.extend(bytes((-len(data)) % 4))
    write_if_changed(path, data)

def simplify(batch, ratio):
    import bpy
    import bmesh
    model, part, texture, pivot, vertices, indices = batch
    # Keep every facial texel, UV coordinate and owning patch edge exact.
    if texture >= 0 or len(indices) < 120: return batch
    positions, remap, lookup = [], [], {}
    for vertex in vertices:
        key = tuple(round(c, 6) for c in vertex[:3])
        if key not in lookup:
            lookup[key] = len(positions); positions.append(vertex[:3])
        remap.append(lookup[key])
    faces = [tuple(remap[i] for i in indices[k:k+3]) for k in range(0, len(indices), 3)]
    mesh = bpy.data.meshes.new('lod')
    mesh.from_pydata(positions, [], faces); mesh.update()
    colors = mesh.color_attributes.new(name='Color', type='BYTE_COLOR', domain='CORNER')
    uv = mesh.uv_layers.new(name='UV')
    for polygon in mesh.polygons:
        for corner, loop in enumerate(polygon.loop_indices):
            vertex = vertices[indices[polygon.index*3+corner]]
            colors.data[loop].color_srgb = tuple(c/255 for c in vertex[5:])
            uv.data[loop].uv = vertex[3:5]
    obj = bpy.data.objects.new('lod', mesh); bpy.context.scene.collection.objects.link(obj)
    bpy.context.view_layer.objects.active = obj
    # A zero decimation weight locks both ends of every incident collapse.
    # Protect native open seams and skin/hair/clothing color discontinuities.
    bm = bmesh.new(); bm.from_mesh(mesh); bm.verts.ensure_lookup_table()
    layer = bm.loops.layers.color['Color']; protected = set(); color_edges = set()
    for edge in bm.edges:
        if len(edge.link_faces) != 2:
            protected.update(v.index for v in edge.verts); continue
        averages = []
        for face in edge.link_faces:
            averages.append(tuple(sum(loop[layer][c] for loop in face.loops)/len(face.loops) for c in range(3)))
        if sum((a-b)**2 for a,b in zip(*averages)) > .20**2:
            color_edges.update(v.index for v in edge.verts)
    # Preserve each disconnected garment/body component's volume. A global
    # ratio alone can flatten a small strap or torso into two opposing faces.
    remaining = set(bm.verts)
    while remaining:
        pending = [remaining.pop()]; component = set(pending)
        while pending:
            vertex = pending.pop()
            for edge in vertex.link_edges:
                other = edge.other_vert(vertex)
                if other in remaining:
                    remaining.remove(other); component.add(other); pending.append(other)
        faces = {face for vertex in component for face in vertex.link_faces}
        component = sorted(component, key=lambda vertex: vertex.index)
        if len(faces) < 16:
            protected.update(vertex.index for vertex in component)
        else:
            center = [(min(v.co[c] for v in component)+max(v.co[c] for v in component))/2 for c in range(3)]
            for axis in range(3):
                protected.add(min(component, key=lambda v:(v.co[axis],v.index)).index)
                protected.add(max(component, key=lambda v:(v.co[axis],-v.index)).index)
            # One outer anchor per octant preserves volume without pinning
            # entire rings of fingers, sleeves and hair at their extreme axis.
            octants = {}
            for vertex in component:
                octant = tuple(vertex.co[c] >= center[c] for c in range(3))
                distance = sum((vertex.co[c]-center[c])**2 for c in range(3))
                if octant not in octants or distance > octants[octant][0]:
                    octants[octant] = (distance,vertex.index)
            protected.update(value[1] for value in octants.values())
    bm.free()
    group = obj.vertex_groups.new(name='Simplify')
    available = [i for i in range(len(positions)) if i not in protected]
    if not available:
        bpy.data.objects.remove(obj, do_unlink=True); bpy.data.meshes.remove(mesh)
        return batch
    group.add(available, 1, 'REPLACE')
    biased = [i for i in available if i in color_edges]
    if biased: group.add(biased, .2, 'REPLACE')
    modifier = obj.modifiers.new('Gameplay detail', 'DECIMATE')
    modifier.ratio = ratio; modifier.use_collapse_triangulate = True
    modifier.vertex_group = group.name; modifier.vertex_group_factor = 5
    evaluated = obj.evaluated_get(bpy.context.evaluated_depsgraph_get())
    result = evaluated.to_mesh(); result.calc_loop_triangles()
    reduced, elements, unique = [], [], {}
    color = result.color_attributes['Color']; uv = result.uv_layers['UV']
    for triangle in result.loop_triangles:
        for loop in triangle.loops:
            position = result.vertices[result.loops[loop].vertex_index].co
            rgba = tuple(max(0, min(255, round(v*255))) for v in color.data[loop].color_srgb)
            vertex = (*position, *uv.data[loop].uv, *rgba)
            if vertex not in unique: unique[vertex] = len(reduced); reduced.append(vertex)
            elements.append(unique[vertex])
    evaluated.to_mesh_clear(); bpy.data.objects.remove(obj, do_unlink=True); bpy.data.meshes.remove(mesh)
    assert len(elements) <= len(indices)
    # Never trade a tiny detached solid for opposing coincident triangles.
    # Retain the original batch if Blender cannot simplify it safely.
    surfaces = set(); valid = True
    for k in range(0,len(elements),3):
        triangle = [reduced[i][:3] for i in elements[k:k+3]]
        a,b,c = triangle
        ab,ac = [[point[i]-a[i] for i in range(3)] for point in (b,c)]
        cross=(ab[1]*ac[2]-ab[2]*ac[1],ab[2]*ac[0]-ab[0]*ac[2],ab[0]*ac[1]-ab[1]*ac[0])
        surface=tuple(sorted(tuple(round(v,6) for v in p) for p in triangle))
        if surface in surfaces or sum(v*v for v in cross)<=1e-13: valid=False; break
        surfaces.add(surface)
    if not valid:
        print(f'LOD retains solid batch: model{model} part{part}',flush=True)
        return batch
    # Protected seam coordinates must survive exactly, avoiding cracks against
    # the unchanged textured face batch or a neighboring rigid component.
    actual = {tuple(round(c, 5) for c in v[:3]) for v in reduced}
    assert all(tuple(round(c, 5) for c in positions[i]) in actual for i in protected), (model,part,len(available),[positions[i] for i in protected if tuple(round(c,5) for c in positions[i]) not in actual][:4])
    return (model, part, texture, pivot, reduced, elements)

def update_header(out=OUT):
    world = json.loads((out/'world.json').read_text())
    manifest = out/'character-lods.json'
    records = json.loads(manifest.read_text()).get('models', []) if manifest.exists() else []
    current = {item['model']: item for item in world['individual_characters']}
    header = ['/* Generated optional character detail; stale inputs fall back to full models. */',
              '#ifndef WORLD3D_CHARACTER_LODS_H', '#define WORLD3D_CHARACTER_LODS_H',
              'static const char *const w3_actor_lod_scenes[256] = {']
    for item in records:
        if item['lod_triangles'] >= item['full_triangles']: continue
        source = current.get(item['model'])
        if not source or source['file'] != item['source']: continue
        target = out/item['file']
        if not (out/source['file']).exists() or not target.exists(): continue
        if digest(out/source['file']) != item['source_sha256'] or digest(target) != item['sha256']: continue
        header.append(f' [{item["model"]}] = "rom:/world/{item["file"]}", /* {item["slug"]} */')
    header.extend(['};', '#endif'])
    write_if_changed(out/'character_lods.h', '\n'.join(header)+'\n')

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--only', help='Comma-separated character slugs')
    parser.add_argument('--ratio', type=float, default=.45)
    parser.add_argument('--header-only', action='store_true')
    parser.add_argument('--worker', action='store_true')
    parser.add_argument('--blender', default=shutil.which('blender') or '/Applications/Blender.app/Contents/MacOS/Blender')
    args = parser.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else None)
    if args.header_only: update_header(); return
    if not args.worker:
        command = [args.blender, '--background', '--factory-startup',
                   '--python-exit-code', '1', '--python', str(Path(__file__).resolve()), '--', '--worker', '--ratio', str(args.ratio)]
        if args.only: command.extend(['--only', args.only])
        subprocess.run(command, check=True); return
    world = json.loads((OUT/'world.json').read_text())
    manifest = OUT/'character-lods.json'
    records = {item['model']: item for item in json.loads(manifest.read_text()).get('models', [])} if manifest.exists() else {}
    for character in world['individual_characters']:
        if args.only and character['slug'] not in args.only.split(','): continue
        source = OUT/character['file']; textures, batches = read_l643(source)
        reduced = [simplify(batch, args.ratio) for batch in batches]
        path = OUT/'lod'/source.name; path.parent.mkdir(exist_ok=True)
        write_mesh(path, textures, reduced)
        before = sum(len(b[5])//3 for b in batches); after = sum(len(b[5])//3 for b in reduced)
        records[character['model']] = dict(model=character['model'], slug=character['slug'],
            source=character['file'], source_sha256=digest(source), file=str(path.relative_to(OUT)),
            sha256=digest(path), full_triangles=before, lod_triangles=after,
            full_vertices=sum(len(b[4]) for b in batches), lod_vertices=sum(len(b[4]) for b in reduced))
        print(f'LOD {character["slug"]}: {before} -> {after} triangles', flush=True)
    write_if_changed(manifest, json.dumps(dict(version=1, ratio=args.ratio, models=sorted(records.values(), key=lambda r:r['model'])), indent=2)+'\n')
    update_header()

if __name__ == '__main__': main()
