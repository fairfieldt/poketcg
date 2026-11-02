"""Verify editable canonical models, palette/head reuse and Amy pose controls."""
import bpy,json,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools'))
import apply_variant
results=[]
for row in json.loads((ROOT/'experiments/cast-concepts/review/final-families.json').read_text()):
 slug=row['slug'];bpy.ops.wm.open_mainfile(filepath=str(ROOT/'assets/models'/slug/f'{slug}.blend'))
 scene=bpy.context.scene;manifest=json.loads(scene['cast_manifest'])
 assert manifest['model']==slug
 objects=[o for o in scene.objects if o.get('cast_source')];assert objects
 assert {o['rigid_part'] for o in objects}==set(range(5))
 base={o.name:(o.data.as_pointer(),tuple(v for r in o.matrix_world for v in r)) for o in objects if not o.get('cast_head')}
 for appearance in manifest['appearances']:
  apply_variant.apply(appearance)
  assert all((o.data.as_pointer(),tuple(v for r in o.matrix_world for v in r))==base[o.name] for o in objects if not o.get('cast_head'))
  if slug=='swimmer':
   heads=[o for o in objects if o.get('cast_head') and not o.hide_render]
   assert heads and all(o['cast_head_appearance']==appearance for o in heads)
   garment=[s.material for o in objects if not o.get('cast_head') for s in o.material_slots if s.material.get('cast_role')=='garment']
   assert garment and (garment[0].diffuse_color[0]>.8)==(appearance!='swimmer')
 apply_variant.apply(slug)
 if slug=='amy':
  apply_variant.apply(pose='lying')
  from mathutils import Vector
  for obj in objects:
   for v in obj.data.vertices:
    x,y,z=v.co;actual=obj.matrix_world@v.co
    assert (actual-Vector((x,.9-z,y+.35))).length<1e-5
  apply_variant.apply()
 results.append(dict(model=slug,parts=5,appearances=list(manifest['appearances']),passed=True))
(ROOT/'review/cast-blender-verification.json').write_text(json.dumps(dict(passed=True,models=results),indent=2)+'\n')
print('CAST BLENDER PASS',len(results),'canonical sources; palettes, shared body, final heads, Amy pose')
