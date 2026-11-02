#!/usr/bin/env python3
"""Validate optional native character detail exports without Blender."""
from collections import Counter
import hashlib
import json
import math
from pathlib import Path
import struct
import sys
import tempfile
import shutil

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
from build_world import read_l643
from build_character_lods import update_header, head_triangles, triangle_key
OUT = ROOT/'generated'
manifest = json.loads((OUT/'character-lods.json').read_text())
world = json.loads((OUT/'world.json').read_text())
assert len(manifest['models']) == len(world['individual_characters'])

def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()
def boundaries(batches):
    edges = Counter()
    for model, part, texture, pivot, vertices, indices in batches:
        for k in range(0, len(indices), 3):
            triangle = [tuple(round(v,5) for v in vertices[i][:3]) for i in indices[k:k+3]]
            for a,b in zip(triangle, triangle[1:]+triangle[:1]):
                edges[(part, tuple(pivot), *sorted((a,b)))] += 1
    return {edge for edge,count in edges.items() if count == 1}, sum(count>2 for count in edges.values())

before = after = protected = head_count = 0
characters = {item['slug']:item for item in world['individual_characters']}
for record in manifest['models']:
    source, target = OUT/record['source'], OUT/record['file']
    assert sha(source) == record['source_sha256'] and sha(target) == record['sha256'], record['slug']
    names, full = read_l643(source); lod_names, lod = read_l643(target)
    heads = head_triangles(characters[record['slug']])
    def head_stream(batches):
        result = Counter()
        for model,part,texture,pivot,vertices,indices in batches:
            for k in range(0,len(indices),3):
                tri = [vertices[i] for i in indices[k:k+3]]
                if (part,triangle_key([v[:3] for v in tri])) in heads:
                    result[(part,texture,tuple(pivot),tuple(sorted(tri)))]+=1
        return result
    original_heads = head_stream(full)
    assert original_heads == head_stream(lod), (record['slug'],'head geometry or paint changed')
    assert len({(part,triangle_key([v[:3] for v in tri])) for part,texture,pivot,tri in original_heads}) == len(heads), (record['slug'],'unmatched authored head')
    head_count += sum(original_heads.values())
    assert names == lod_names and len(full) == len(lod)
    assert boundaries(full) == boundaries(lod), (record['slug'], 'changed open seams/nonmanifold edges')
    for a,b in zip(full,lod):
        assert a[:4] == b[:4], 'Model, rigid part, texture and pivot must match'
        assert len(b[5]) <= len(a[5]) and max(b[5]) < len(b[4])
        if a[2] >= 0:
            assert a == b, (record['slug'], 'textured face changed'); protected += len(a[5])//3
        for vertex in b[4]:
            assert all(math.isfinite(v) for v in vertex[:5]) and vertex[8] == 255
        surfaces = set()
        for k in range(0,len(b[5]),3):
            tri = [b[4][i][:3] for i in b[5][k:k+3]]
            ab,ac = [[tri[j][i]-tri[0][i] for i in range(3)] for j in (1,2)]
            cross = (ab[1]*ac[2]-ab[2]*ac[1],ab[2]*ac[0]-ab[0]*ac[2],ab[0]*ac[1]-ab[1]*ac[0])
            assert sum(v*v for v in cross)>1e-13,(record['slug'],'degenerate')
            key=tuple(sorted(tuple(round(v,6) for v in p) for p in tri))
            assert key not in surfaces,(record['slug'],'duplicate surface')
            surfaces.add(key)
    before += sum(len(batch[5])//3 for batch in full)
    after += sum(len(batch[5])//3 for batch in lod)
assert after-head_count < (before-head_count)*.75, 'Body simplification must retain at least 25% savings'
# Header publication must be idempotent and source-bound, including when a
# completed character export arrives after the last optional Blender build.
header = OUT/'character_lods.h'; prior = header.read_bytes(); update_header(); assert header.read_bytes() == prior
with tempfile.TemporaryDirectory(prefix='poketcg-lod-header-') as temporary:
    out = Path(temporary); record = manifest['models'][0]
    (out/'maps').mkdir(); (out/'lod').mkdir()
    for name in ('world.json','character-lods.json'): shutil.copy2(OUT/name,out/name)
    for name in ('source','file'): shutil.copy2(OUT/record[name],out/record[name])
    update_header(out); assert f'[{record["model"]}]' in (out/'character_lods.h').read_text()
    (out/record['source']).write_bytes(b'new character export')
    update_header(out); assert f'[{record["model"]}]' not in (out/'character_lods.h').read_text()
print(f'CHARACTER LOD PASS: {len(manifest["models"])} exports, {before}->{after} triangles; {protected} textured face triangles and {head_count} complete head triangles exact; rigid pivots/open seams preserved')
