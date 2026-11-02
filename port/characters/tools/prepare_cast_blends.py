"""Attach production identity and shared appearance controls to reviewed Blender sources."""
import bpy,json,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];STUDY=ROOT/'experiments/cast-concepts'
variants={r['slug']:r for r in json.loads((STUDY/'review/final-appearances.json').read_text())}
for row in json.loads((STUDY/'review/final-families.json').read_text()):
 slug=row['slug'];folder=ROOT/'assets/models'/slug;path=folder/f'{slug}.blend'
 bpy.ops.wm.open_mainfile(filepath=str(path));scene=bpy.context.scene
 scene['cast_manifest']=(folder/'scene.json').read_text();scene['cast_appearance']=slug;scene['cast_pose']='standing'
 authoring=json.loads((folder/'authoring.json').read_text());by_name={m['name']:m for m in authoring['meshes']}
 for obj in list(scene.objects):
  if obj.type!='MESH':continue
  if obj.hide_render or obj.hide_viewport:
   bpy.data.objects.remove(obj,do_unlink=True);continue
  obj['cast_source']=True;obj['rest_matrix']=[v for row in obj.matrix_world for v in row]
  obj['cast_head']='head' in obj.name
  entry=by_name.get(obj.name)
  if entry:
   for slot,mat in zip(obj.material_slots,entry['materials']):
    slot.material['cast_role']=mat['name'];slot.material['cast_rest_color']=mat['rgb']
 if slug in ('mania','boy','swimmer'):
  alt=slug+'_palette2';selection=variants[alt];data=json.loads((STUDY/selection['prefix']/alt/'authoring.json').read_text())
  palette={m['name']:m['rgb'] for mesh in data['meshes'] if 'head' not in mesh['name'] for m in mesh['materials']}
  scene['cast_variant_palette']=json.dumps(palette)
  if slug=='swimmer':
   # Load the final yellow Amanda selection, keeping only her head.
   source=STUDY/selection['prefix']/f'{alt}.blend'
   with bpy.data.libraries.load(str(source),link=False) as (src,dst):dst.objects=[n for n in src.objects if 'head' in n]
   for obj in dst.objects:
    scene.collection.objects.link(obj);obj['cast_source']=True;obj['cast_head']=True;obj['cast_head_appearance']=alt;obj['rest_matrix']=[v for row in obj.matrix_world for v in row];obj.hide_render=True;obj.hide_viewport=True
   for obj in scene.objects:
    if obj.get('cast_head') and not obj.get('cast_head_appearance'):obj['cast_head_appearance']=slug
 bpy.ops.wm.save_as_mainfile(filepath=str(path))
