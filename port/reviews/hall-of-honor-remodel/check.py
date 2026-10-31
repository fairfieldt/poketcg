#!/usr/bin/env python3
"""Hall-only shape, source metadata and canonical clearance regression."""
from pathlib import Path
import hashlib,json,sys,tempfile
REPO=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(REPO/'port/world3d/tools'))
import build_world as world
from check_room_surfaces import overlaps
from check_prop_clearance import check,source_patches

def run():
 doc=json.loads((world.OUT/'world.json').read_text());hall=doc['maps'][33]
 _,source,_=world.load_metadata()
 for key in ('id','name','slug','width','height','permissions','warps'):
  assert hall[key]==source[33][key],key
 assert len(hall['npcs'])==len(source[33]['npcs'])
 for a,b in zip(hall['npcs'],source[33]['npcs']):
  for key in ('npc','x','y','dir','preload','name','conditional','heading'):assert a[key]==b[key],(key,a,b)
 assert [(n['npc'],n['x'],n['y']) for n in hall['npcs']]==[(104,10,8),(105,12,8),(106,8,10),(107,10,10),(108,12,10),(109,14,10)]
 record=doc['meshes']['hall_of_honor'];solids=record['props']
 expected=[('altar',[7.15,7.15,14.85,10.85]),('pillar',[5.15,9.15,8.85,12.85]),('pillar',[13.15,9.15,16.85,12.85]),('torch',[7.48,7.48,8.52,8.52]),('torch',[13.48,7.48,14.52,8.52])]
 bodies=[p for p in solids if p['kind'] in ('altar','pillar','torch')]
 assert len(bodies)==5
 for p,(kind,bounds) in zip(bodies,expected):
  assert p['kind']==kind
  assert all(abs(a-b)<1e-8 for a,b in zip(p['solid_bounds'],bounds)),p
 with tempfile.TemporaryDirectory(prefix='poketcg-honor-check-') as temp:
  old=world.TEX;world.TEX=Path(temp)
  try:
   scene=world.Scene('hall_of_honor');img=world.Image(REPO/'port/maps/hall_of_honor.png')
   for p in world.scene_props(scene.name,img):world.prop(scene,img,p)
   planar,competing=overlaps(scene);assert not competing,competing
  finally:world.TEX=old
 clearance=check(doc,source_patches(REPO/'port/overworld/gen/data.c'),.65)
 assert clearance['passed'],clearance['failures']
 overrides=doc['map_actor_scenes'];assert len(overrides)==4
 assert {c['npc'] for c in overrides}=={104,105,107,108}
 composite=world.Scene('hall-card-check');card_triangles=0
 for c in overrides:
  assert c['map']==33 and c['model']==doc['npc_models'][c['npc']]
  npc=next(n for n in hall['npcs'] if n['npc']==c['npc'])
  names,batches=world.read_l643(world.OUT/c['scene'].removeprefix('rom:/world/'))
  for model,part,texture,pivot,vs,inds in batches:
   assert model==c['model'] and part==0 and tuple(pivot)==(0.0,0.0,0.0)
   absolute=[(v[0]+npc['x'],v[1]+2.2,v[2]+npc['y'],*v[3:]) for v in vs]
   assert all(10.05<v[0]<11.95 and 2.30<v[1]<4.20 and 8.76<v[2]<8.91 for v in absolute)
   composite.batches[(model,part,names[texture] if texture>=0 else None,tuple(pivot))]=(absolute,inds,{})
   card_triangles+=len(inds)//3
 _,card_overlaps=overlaps(composite);assert not card_overlaps,card_overlaps
 assert card_triangles==64
 result={'passed':True,'source_metadata_unchanged':True,'four_card_components_two_sparks':True,'previous_solid_bounds_unchanged':True,
   'prop_planar_triangles':planar,'competing_planar_overlaps':len(competing),
   'all_map_clearance':clearance['totals'],'actor_radius':.65,
   'scene':{k:record[k] for k in ('vertices','triangles','batches','textures')},
   'mesh_bytes':(world.OUT/record['file']).stat().st_size,
   'mesh_sha256':hashlib.sha256((world.OUT/record['file']).read_bytes()).hexdigest(),
   'source_map_sha256':hashlib.sha256((REPO/'port/maps/hall_of_honor.png').read_bytes()).hexdigest(),
   'hall_only_card_derivatives':4,'composite_card_triangles':card_triangles,'composite_card_overlaps':len(card_overlaps)}
 (Path(__file__).parent/'validation.json').write_text(json.dumps(result,indent=2)+'\n')
 print('HALL MODEL PASS: source map/NPC/permissions/warps, previous solids, independent mesh faces, all-room clearance')
if __name__=='__main__':run()
