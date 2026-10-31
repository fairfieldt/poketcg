"""Read-only asset/timing census. Run from any directory; updates geometry.json beside this script."""
import json,struct,re,statistics
from PIL import Image
from pathlib import Path
R=Path(__file__).resolve().parents[3];W=R/'port/world3d/generated';w=json.loads((W/'world.json').read_text())
def parse(path):
 b=(W/path).read_bytes();n,nt=struct.unpack_from('>HH',b,4);names=[b[8+i*32:8+(i+1)*32].split(b'\0')[0].decode() for i in range(nt)];o=8+nt*32;mes=[]
 for i in range(n):
  model,part,texture,reserved,nv,ni,px,py,pz=struct.unpack_from('>HHhHIIfff',b,o);o+=28
  indices=struct.unpack_from('>'+str(ni)+'H',b,o+nv*24)
  cache=[];loads=0
  for idx in indices:
   if idx in cache: cache.remove(idx)
   else:
    loads+=1
    if len(cache)==32:cache.pop(0)
   cache.append(idx)
  o+=((nv*24+ni*2)+3)&~3
  mes.append(dict(model=model,part=part,texture=texture,vertices=nv,triangles=ni//3,unique_indices=len(set(indices)),vertex_loads=loads))
 assert o==len(b)
 return dict(file=path,bytes=len(b),batches=n,textures=names,meshes=mes)
parsed={key:parse(item['file']) for key,item in w['meshes'].items()}
actors={item['model']:dict(item) for item in w['individual_characters']}
for item in actors.values():
 data=parse(item['file'])
 item['vertex_loads']=sum(m['vertex_loads'] for m in data['meshes'])
 item['unique_indices']=sum(m['unique_indices'] for m in data['meshes'])
 item['texture_bytes']=sum(Image.open(W/'textures'/(t+'.png')).width*Image.open(W/'textures'/(t+'.png')).height*2 for t in item['textures'])

base=parse('actors.l64')
def actor(m):
 if m in actors:return actors[m]
 ms=[x for x in base['meshes'] if x['model']==m]
 return dict(triangles=sum(x['triangles'] for x in ms),vertices=sum(x['vertices'] for x in ms),batches=len(ms),textures=[],model=m,vertex_loads=sum(x['vertex_loads'] for x in ms),unique_indices=sum(x['unique_indices'] for x in ms),texture_bytes=0)
rows=[]
for mp in w['maps']:
 scene_path=w['switch_base'] if mp['id']==2 else mp['scene']
 scene=next(p for p in parsed.values() if 'rom:/world/'+p['file']==scene_path)
 roommes=[x for x in scene['meshes'] if x['model']==0]
 overlays=[]
 if mp['overlay']:
  overlay=next(p for p in parsed.values() if 'rom:/world/'+p['file']==mp['overlay']);overlays=[x for x in overlay['meshes'] if x['model']==0]
 switch_parts=[]
 if mp['id']==2:
  for sw in w['switches']:
   sp=next(p for p in parsed.values() if 'rom:/world/'+p['file']==sw['off']);switch_parts.append(sp)
  overlays += [m for sp in switch_parts for m in sp['meshes'] if m['model']==0]
 roomtri=sum(x['triangles'] for x in roommes+overlays);roomv=sum(x['vertices'] for x in roommes+overlays);roomb=len(roommes+overlays)
 roomloads=sum(x['vertex_loads'] for x in roommes+overlays);roomunique=sum(x['unique_indices'] for x in roommes+overlays)
 npcs=[x for x in mp['npcs'] if x['npc']!=255 and x['x']<mp['width'] and x['y']<mp['height']]
 minimum=[x for x in npcs if not x['conditional']]
 player=actor(7)
 counts=[]
 for label,ns in [('unconditional',minimum),('all_configured',npcs)]:
  am=[player]+[actor(n['model']) for n in ns]
  if mp['id']==0:am=[player,actor(6)]
  shadows=0 if not mp['id'] else 1+sum(n['npc']<103 for n in ns) # exact prop id threshold checked below
  counts.append(dict(kind=label,npc_count=len(ns),actors=len(am),actor_triangles=sum(x['triangles'] for x in am),actor_vertices=sum(x['vertices'] for x in am),actor_vertex_loads=sum(x['vertex_loads'] for x in am),actor_unique_indices=sum(x['unique_indices'] for x in am),actor_texture_bytes=sum(actor(m)['texture_bytes'] for m in set(x['model'] for x in am)),actor_batches=sum(x['batches'] for x in am),shadow_fans=shadows,shadow_triangles=shadows*12,total_triangles=roomtri+sum(x['triangles'] for x in am)+shadows*12,total_batches=roomb+sum(x['batches'] for x in am),unique_actor_models=sorted(set(x['model'] for x in am)),npc_names=[n['name'] for n in ns]))
 rows.append(dict(map=mp['id'],name=mp['name'],room_triangles=roomtri,room_vertices=roomv,room_vertex_loads=roomloads,room_unique_indices=roomunique,room_texture_bytes=sum(Image.open(W/'textures'/(t.replace('.sprite','')+'.png')).width*Image.open(W/'textures'/(t.replace('.sprite','')+'.png')).height*2 for sp in [scene]+switch_parts for t in sp['textures']),room_batches=roomb,room_textures=sum(len(sp['textures']) for sp in [scene]+switch_parts),room_file_bytes=sum(sp['bytes'] for sp in [scene]+switch_parts),scenarios=counts))
timings={}
for case in ['game-480x360-8mib-v3','game-320x240-4mib-v3','leads-after-480x360-8mib-v3','leads-after-320x240-4mib-v3']:
 p=R/'port/frontends/n64/adventure/build/expansion-pak'/case/'native.log';s=p.read_text(errors='replace');times={}
 stage='unknown'
 for line in s.splitlines():
  begin=re.search(r'VIDEO REVIEW (\S+) BEGIN',line)
  if begin:stage=begin.group(1)
  frame=re.search(r'VIDEO FRAME (?:map|stage)=(\S+) render_us=(\d+)',line)
  if frame:times.setdefault(stage,[]).append(int(frame.group(2)))
 timings[case]={k:dict(count=len(v),minimum_us=min(v),median_us=statistics.median(v),maximum_us=max(v)) for k,v in times.items()}
report=dict(scene_table={k:dict(file=v['file'],triangles=sum(m['triangles'] for m in v['meshes']),vertices=sum(m['vertices'] for m in v['meshes']),vertex_loads=sum(m['vertex_loads'] for m in v['meshes']),unique_indices=sum(m['unique_indices'] for m in v['meshes']),batches=v['batches'],textures=len(v['textures'])) for k,v in parsed.items()},map_scenarios=rows,timings=timings,actor_table={str(k):dict(slug=v.get('slug'),triangles=v['triangles'],vertices=v['vertices'],batches=v['batches'],vertex_loads=v['vertex_loads'],unique_indices=v['unique_indices'],texture_bytes=v['texture_bytes']) for k,v in actors.items()},notes=['All-configured rows force every in-bounds map NPC visible; conditional story preloads may not coexist.','Unconditional rows are a lower configured census, not a universal playable minimum.','No application-level frustum or backface culling: all these configured-visible actors and the whole static room are submitted.','Room counts are indexed input; RSP clipping and rejection reduce actual RDP triangles, not transform input.','Per-frame shadow triangles12 per grounded non-prop; Amy lying/standing and item-prop state can alter this.','Map2 includes dynamic base plus8off-state panels; onstate geometry countsidentical. Other event variants are in scene_table,not substituted in default map rows.'])
Path(__file__).with_name('geometry.json').write_text(json.dumps(report,indent=2)+'\n')
for x in rows:
 if x['map'] in [0,1,6,9,12,15,18,21,24,27,32,33]:
  print(x['map'],x['name'],'room',x['room_triangles'],x['room_batches'],x['room_textures'],'npc',*[f"{s['npc_count']}:{s['total_triangles']}tri/{s['total_batches']}b" for s in x['scenarios']])
print(json.dumps(timings,indent=2))
