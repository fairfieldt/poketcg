#!/usr/bin/env python3
"""Check that the runtime cursor and fallback card each own one closed shell."""
from collections import Counter,defaultdict
import json,math
from pathlib import Path
from build_world import read_l643

ROOT=Path(__file__).resolve().parents[1]

def main():
    _,batches=read_l643(ROOT/'generated/actors.l64')
    geometry=defaultdict(list)
    for model,part,tex,pivot,vs,indices in batches:
        if model not in (5,6):continue
        assert part==0 and not any(pivot) and tex==-1
        for offset in range(0,len(indices),3):
            geometry[model].append([vs[index][:3] for index in indices[offset:offset+3]])
    records=[]
    for model in (5,6):
        edges=Counter();directed=Counter();faces=set();vertices=set()
        for points in geometry[model]:
            keys=[tuple(round(value,6) for value in point) for point in points]
            assert len(set(keys))==3,(model,'Degenerate runtime prop triangle')
            key=tuple(sorted(keys));assert key not in faces,(model,'Duplicate runtime prop triangle');faces.add(key)
            for point in points:
                if model==6:
                    assert abs(math.sqrt(point[0]**2+(point[1]-.8)**2+point[2]**2)-.7)<1e-6,'Nested cursor shell'
                else:
                    assert abs(abs(point[2])-.08)<1e-6,'Layered card surface'
            vertices.update(keys)
            for a,b in zip(keys,keys[1:]+keys[:1]):
                edges[tuple(sorted((a,b)))]+=1;directed[a,b]+=1
        assert faces and all(count==2 for count in edges.values()),(model,'Open or overlapping runtime prop surface')
        assert all(count==directed[b,a]==1 for (a,b),count in directed.items()),(model,'Inconsistent shell winding')
        assert len(vertices)-len(edges)+len(faces)==2,(model,'Runtime prop shell topology')
        records.append(dict(model=model,vertices=len(vertices),edges=len(edges),triangles=len(faces),shells=1,duplicate_triangles=0))
    result=dict(passed=True,models=records)
    output=ROOT/'build/cursor-check.json';output.parent.mkdir(parents=True,exist_ok=True)
    output.write_text(json.dumps(result,indent=2)+'\n');print('RUNTIME PROP SHELLS PASS: '+json.dumps(result))

if __name__=='__main__':main()
