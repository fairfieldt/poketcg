"""Check that island paths and grass share one triangulated terrain surface.

Blender --background --python-exit-code 1 --python
port/world3d/tools/check_overworld_terrain.py
"""
from collections import defaultdict
import json
import math
from pathlib import Path
import bpy

ROOT=Path(__file__).resolve().parents[1]


def area(points):
    return abs(sum(a[0]*b[1]-a[1]*b[0] for a,b in zip(points,points[1:]+points[:1])))/2


def validate():
    failures=[];triangles=0;area_total=0;boundary_edges=0
    grounds=[o for o in bpy.context.scene.objects if o.type=='MESH' and o.get('ground_receiver')]
    if len(grounds)!=3:failures.append(dict(check='terrain-count',count=len(grounds)))
    for obj in grounds:
        if not obj.get('terrain_surface_owner'):
            failures.append(dict(check='separate-ground-overlay',object=obj.name));continue
        outline=json.loads(obj['terrain_outline'])
        data=obj.data;data.calc_loop_triangles();edges=defaultdict(list);seen=set();total=0
        for tri in data.loop_triangles:
            points=[obj.matrix_world@data.vertices[i].co for i in tri.vertices]
            xz=[(p.x,-p.y) for p in points]
            measure=area(xz);total+=measure;triangles+=1
            if measure<1e-10:failures.append(dict(check='degenerate-terrain-face',object=obj.name,face=tri.polygon_index))
            key=tuple(sorted(tuple(round(v,6) for v in p) for p in points))
            if key in seen:failures.append(dict(check='duplicate-terrain-face',object=obj.name,face=tri.polygon_index))
            seen.add(key)
        for poly in data.polygons:
            for a,b in zip(poly.vertices,(*poly.vertices[1:],poly.vertices[0])):
                edges[tuple(sorted((a,b)))].append((a,b))
        for key,uses in edges.items():
            if len(uses)==1:boundary_edges+=1
            elif len(uses)!=2 or uses[0]!=(uses[1][1],uses[1][0]):
                failures.append(dict(check='terrain-edge-ownership',object=obj.name,edge=list(key),uses=len(uses)))
        expected=area(outline)
        if abs(total-expected)>max(1e-5,expected*1e-6):
            failures.append(dict(check='terrain-area',object=obj.name,actual=total,expected=expected))
        area_total+=total
        for poly in data.polygons:
            for li in poly.loop_indices:
                p=obj.matrix_world@data.vertices[data.loops[li].vertex_index].co
                expected=.35+.035*math.sin(p.x*.7)+.026*math.cos(-p.y*.9)
                # Refining a triangle keeps its existing linear surface.
                if abs(p.z-expected)>.008:
                    failures.append(dict(check='terrain-height',object=obj.name,face=poly.index));break
    return dict(passed=not failures,totals=dict(shelves=len(grounds),triangles=triangles,boundary_edges=boundary_edges,area=round(area_total,6)),failures=failures)


def main():
    bpy.ops.wm.open_mainfile(filepath=str(ROOT/'generated/overworld.blend'))
    result=validate();path=ROOT/'build/terrain-check.json';path.parent.mkdir(parents=True,exist_ok=True)
    path.write_text(json.dumps(result,indent=2)+'\n')
    print('OVERWORLD TERRAIN '+('PASS' if result['passed'] else 'FAIL')+': '+json.dumps(result))
    if not result['passed']:raise SystemExit(1)

if __name__=='__main__':main()
