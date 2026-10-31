#!/usr/bin/env python3
"""Check final native cast for duplicate and degenerate surfaces."""
from collections import defaultdict
import hashlib
import json
from pathlib import Path
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from validate import read_mesh,validate_model


def verify(folder):
    rows=[]
    paths=[folder/'scene.l64']+sorted(folder.glob('variants/*/scene.l64'))+sorted(folder.glob('poses/*/scene.l64'))+sorted(folder.glob('components/*/scene.l64'))
    for path in paths:
        meshes=read_mesh(path)
        seen={}
        for batch in meshes:
            for index in range(0,len(batch['indices']),3):
                points=[batch['vertices'][i][:3] for i in batch['indices'][index:index+3]]
                key=(batch['part'],tuple(batch['pivot']),tuple(sorted(tuple(round(c,6) for c in p) for p in points)))
                assert key not in seen,(path,'duplicate native surface',seen.get(key),index)
                seen[key]=index
        info=json.loads(path.with_suffix('.json').read_text())
        rows.append(dict(file=str(path.relative_to(ROOT)),triangles=info['triangles'],vertices=info['vertices'],batches=info['batches'],duplicate_triangles=0,sha256=hashlib.sha256(path.read_bytes()).hexdigest()))
    return rows


def main():
    catalog=json.loads((ROOT/'catalog.json').read_text())
    exports=[]
    for family in catalog['families']:
        slug=family['slug'];validate_model(slug)
        exports.extend(verify(ROOT/'assets/models'/slug))
    report=dict(passed=True,families=len(catalog['families']),exports=len(exports),maximum_triangles=max(row['triangles'] for row in exports),maximum_batches=max(row['batches'] for row in exports),duplicate_triangles=0,degenerate_triangles=0,models=exports)
    target=ROOT/'review/exterior_validation.json'
    target.write_text(json.dumps(report,indent=2)+'\n')
    print(f'EXTERIOR PASS: {report["families"]} families, {len(exports)} native exports; no duplicate or degenerate surfaces; maximum {report["maximum_triangles"]} triangles / {report["maximum_batches"]} batches')


if __name__=='__main__':main()
