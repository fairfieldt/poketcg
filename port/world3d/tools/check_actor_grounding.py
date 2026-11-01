#!/usr/bin/env python3
"""Check exact legacy-disk removal without rewriting generated actor assets.

The optional --before directory contains the pre-migration runtime exports.
Without it, synthetic fixtures still exercise recognition and strict negative
cases, and the current runtime/LOD catalog is checked for old disks.
"""
import argparse
from collections import Counter
import copy
import hashlib
import json
import math
from pathlib import Path
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
REPO = ROOT.parents[1]
sys.path.insert(0, str(ROOT/'tools'))
from build_world import read_l643
from actor_grounding import encode_l643, finish_runtime_grounding, strip_legacy_disks


def sha(data):
    return hashlib.sha256(data).hexdigest()


def fixture(lying=False):
    rx, rz, height = (.378, .272, .01692) if lying else (.47, .32, .018)
    color = (11, 5, 23, 255) if lying else (13, 6, 27, 255)
    vertices = []
    for i in range(8):
        x, z = rx*math.cos(i*math.pi/4), rz*math.sin(i*math.pi/4)
        xyz = (x, z, -height) if lying else (x, height, z)
        vertices.append((*xyz, i/8, 1-i/8, *color))
    # A separately owned body triangle proves that the whole batch survives.
    vertices.extend([(0, 1, 0, 0, 0, 100, 80, 70, 255),
                     (.1, 1, 0, 1, 0, 100, 80, 70, 255),
                     (0, 1.1, 0, 0, 1, 100, 80, 70, 255)])
    indices = tuple(i for j in range(1, 7) for i in (0, j, j+1)) + (8, 9, 10)
    return (7, 0, -1, [0, .35, .9] if lying else [0, 0, 0], vertices, indices)


def strict_cases():
    cases = 0
    for lying in (False, True):
        base = fixture(lying)
        clean, removed = strip_legacy_disks([base])
        assert len(removed) == 1 and clean[0][:5] == base[:5]
        assert clean[0][5] == (8, 9, 10)
        assert strip_legacy_disks(clean) == (clean, [])
        assert strip_legacy_disks([base], {8}) == ([base], [])
        cases += 3
        negatives = []
        for field, value in ((0, 0), (1, 1), (2, 0), (3, [0, .36, .9] if lying else [0, .01, 0])):
            bad = list(copy.deepcopy(base)); bad[field] = value; negatives.append(tuple(bad))
        for vertex, component, value in ((0, 0, base[4][0][0]+.001),
                                         (0, 2 if lying else 1, .04),
                                         (0, 5, 12), (0, 8, 254)):
            bad = list(copy.deepcopy(base)); v = list(bad[4][vertex]); v[component] = value
            bad[4][vertex] = tuple(v); negatives.append(tuple(bad))
        # Missing, duplicated, mixed-winding, degenerate and externally shared
        # triangles must all leave the entire batch unchanged.
        for indices in (base[5][3:], base[5][:3]+base[5][:3]+base[5][6:],
                        (0, 2, 1)+base[5][3:], (0, 0, 2)+base[5][3:],
                        base[5]+(0, 8, 9)):
            negatives.append((*base[:5], indices))
        # Duplicate ring positions and an incomplete ring are rejected even
        # when they carry the exact shadow color.
        bad = list(copy.deepcopy(base)); bad[4].append(bad[4][0]); negatives.append(tuple(bad))
        bad = list(copy.deepcopy(base)); v = list(bad[4][7]); v[0] += .03
        bad[4][7] = tuple(v); negatives.append(tuple(bad))
        for bad in negatives:
            assert strip_legacy_disks([bad]) == ([bad], []), (lying, bad)
            cases += 1
    return cases


def removed_geometry(old, new):
    """Independent ordered-index comparison, including every vertex field."""
    assert old[:5] == new[:5], 'Model/part/material/pivot/vertex/UV/color changed'
    old_tri = [tuple(old[5][i:i+3]) for i in range(0, len(old[5]), 3)]
    new_tri = [tuple(new[5][i:i+3]) for i in range(0, len(new[5]), 3)]
    retained = iter(new_tri)
    next_tri = next(retained, None)
    removed = []
    for triangle in old_tri:
        if triangle == next_tri:
            next_tri = next(retained, None)
        else:
            removed.append(triangle)
    assert next_tri is None, 'Retained triangles reordered or changed'
    if not removed:
        return 0
    assert len(removed) == 6
    ids = {i for tri in removed for i in tri}
    assert len(ids) == 8
    assert not ids.intersection(new[5]), 'A removed vertex is shared with body geometry'
    verts = [old[4][i] for i in ids]
    colors = {tuple(v[5:]) for v in verts}
    assert colors in ({(13, 6, 27, 255)}, {(11, 5, 23, 255)})
    axis = 2 if colors == {(11, 5, 23, 255)} else 1
    assert max(v[axis] for v in verts)-min(v[axis] for v in verts) < 1e-6
    assert abs(verts[0][axis]) < 1/32
    edges = Counter(tuple(sorted((a, b))) for tri in removed for a, b in zip(tri, tri[1:]+tri[:1]))
    assert sum(n == 1 for n in edges.values()) == 8
    assert sum(n == 2 for n in edges.values()) == 5
    return 1


def lod_binding_cases():
    """Only a byte-exact old->grounded source migration may rebind a LOD."""
    names = ['sample.sprite']
    meshes = [fixture()]
    raw = encode_l643(names, meshes)
    clean = encode_l643(names, strip_legacy_disks(meshes)[0])
    count = 0
    for scenario in ('migration', 'changed-body', 'changed-lod', 'wrong-prior'):
        with tempfile.TemporaryDirectory(prefix='actor-grounding-') as directory:
            out = Path(directory); (out/'maps').mkdir(); (out/'lod').mkdir()
            source = clean
            if scenario == 'changed-body':
                changed = copy.deepcopy(strip_legacy_disks(meshes)[0]); v = list(changed[0][4][8]); v[1] += .1
                changed[0][4][8] = tuple(v); source = encode_l643(names, changed)
            (out/'maps/a.l64').write_bytes(source)
            target = raw if scenario != 'changed-lod' else clean
            (out/'lod/a.l64').write_bytes(target)
            record = {'model': 7, 'file': 'lod/a.l64', 'source_sha256': sha(raw), 'sha256': sha(raw)}
            (out/'character-lods.json').write_text(json.dumps({'models': [record]}))
            prior = raw if scenario != 'wrong-prior' else b'not the bound source'
            previous = {'maps/a.l64': (prior, names, meshes)}
            finish_runtime_grounding(out, {'individual_characters': [{'model': 7, 'file': 'maps/a.l64'}]}, previous)
            after = json.loads((out/'character-lods.json').read_text())['models'][0]
            if scenario == 'migration':
                assert (out/'lod/a.l64').read_bytes() == clean
                assert after['source_sha256'] == sha(clean) and after['sha256'] == sha(clean)
            else:
                assert (out/'lod/a.l64').read_bytes() == target and after == record
            count += 1
    return count


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--before', type=Path)
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    out = ROOT/'generated'
    world = json.loads((out/'world.json').read_text())
    lods = json.loads((out/'character-lods.json').read_text())['models']
    report = {'status': 'PASS', 'negative_and_positive_cases': strict_cases(), 'lod_binding_cases': lod_binding_cases()}
    records = [('full', c['file']) for c in world['individual_characters']]
    records += [('lod', c['file']) for c in lods]
    records += [('fallback', 'actors.l64')]
    counts = Counter(); details = []
    for kind, filename in records:
        names, meshes = read_l643(out/filename)
        assert not strip_legacy_disks(meshes)[1], (filename, 'runtime disk remains')
        detail = {'file': filename, 'sha256': sha((out/filename).read_bytes()), 'removed_disks': 0}
        if args.before:
            old_names, old_meshes = read_l643(args.before/filename)
            assert names == old_names and len(meshes) == len(old_meshes)
            detail['before_sha256'] = sha((args.before/filename).read_bytes())
            detail['removed_disks'] = sum(removed_geometry(a, b) for a, b in zip(old_meshes, meshes))
            expected, removed = strip_legacy_disks(old_meshes, {1, 2, 3, 4} if kind == 'fallback' else None)
            assert meshes == expected and detail['removed_disks'] == len(removed)
        counts[kind+'_files'] += 1
        counts[kind+'_disks'] += detail['removed_disks']
        details.append(detail)
    if args.before:
        assert counts['full_disks'] == 41 and counts['lod_disks'] == 41 and counts['fallback_disks'] == 4, counts
        authored = json.loads((args.before/'authored-hashes.json').read_text())
        for filename, expected in authored.items():
            assert sha((REPO/filename).read_bytes()) == expected, ('Canonical source changed', filename)
        report['authored_exports_unchanged'] = len(authored)
    # Manifest hashes must bind both ends; no stale LOD becomes selectable.
    for record in lods:
        assert sha((out/record['source']).read_bytes()) == record['source_sha256']
        assert sha((out/record['file']).read_bytes()) == record['sha256']
    report.update(counts=dict(counts), removed_triangles=sum(counts[k] for k in ('full_disks', 'lod_disks', 'fallback_disks'))*6,
                  comparison='exact vertices/UV/colors/pivots and retained triangle order',
                  pose_review='standing part-0/pivot-zero and transformed Amy lying part-0/pivot-(0,.35,.9); all pivots and non-shadow geometry unchanged',
                  recognition='eight unique canonical ring points, exact RGBA, rigid part/material/pivot, six exclusive nondegenerate uniformly wound triangles, full boundary and polygon area',
                  files=details)
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2)+'\n')
    print(f"ACTOR GROUNDING PASS: {len(records)} runtime files; {report['negative_and_positive_cases']} strict cases; "
          f"{report['lod_binding_cases']} LOD binding cases; {report['removed_triangles']} legacy triangles removed")


if __name__ == '__main__':
    main()
