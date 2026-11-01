#!/usr/bin/env python3
"""Discover finished individual character exports without regenerating rooms.

Character directories contain scene.l64, scene.json and textures/*.png. The
catalog supplies canonical families and source NPC aliases. Models are remapped
out of reserved fallback IDs1..6 and exported as independent lazy-load scenes.
Incomplete updates retain their last known finished import, when available.
"""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import re
import sys
import struct
import tempfile

ROOT=Path(__file__).resolve().parents[1]
REPO=ROOT.parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from build_world import Scene,Image,read_l643,texture,TEX
from world_manifest import write_if_changed,write_world_metadata
from actor_grounding import strip_legacy_disks,finish_runtime_grounding

OUT=ROOT/'generated'
CENTRAL_ROOT=REPO/'port/characters/assets/models'
DEFAULT_ROOTS=[CENTRAL_ROOT,REPO/'port/characters/models',REPO/'port/characters/generated/models',
               REPO/'port/frontends/n64/lightning-characters/assets/models']
CATALOG=REPO/'port/characters/catalog.json'


def source_export(folder,slug,shared_textures=None,marker=None,family=None,appearance=None,pose=None,component=None):
    paths=[folder/'scene.l64',folder/'scene.json']
    if not all(p.is_file() for p in paths):return None
    marker_stat=None
    if marker is not None:
        if not marker.is_file():return None
        marker_stat=(marker.stat().st_mtime_ns,marker.stat().st_size)
        complete=json.loads(marker.read_text())
        assert complete['model']==family
        assert (appearance or family) in complete['appearances']
        assert marker_stat[0]>=max(p.stat().st_mtime_ns for p in paths),'Completion marker predates export'
    before=[(p.stat().st_mtime_ns,p.stat().st_size) for p in paths]
    meta=json.loads(paths[1].read_text())
    names,meshes=read_l643(paths[0])
    source_ids={m[0] for m in meshes}
    declared={a['model'] for a in meta.get('actors',[]) if a.get('name')==slug}
    if len(declared)==1:source_id=declared.pop()
    elif len(source_ids)==1:source_id=next(iter(source_ids))
    else:raise ValueError('Standalone export has ambiguous model IDs')
    assert source_id>0 and source_ids=={source_id},'Standalone character must contain exactly its declared model'
    assert meta.get('format')=='L643'
    if marker is not None:
        assert meta.get('model')==family
        assert meta.get('appearance')==(appearance or family)
        assert meta.get('pose','standing')==(pose or 'standing')
        assert meta.get('component')==component
    assert meta.get('vertices')==sum(len(m[4]) for m in meshes)
    assert meta.get('triangles')==sum(len(m[5])//3 for m in meshes)
    assert meta.get('batches')==len(meshes)
    assert all(0<=m[1]<=4 for m in meshes),'Unsupported rigid part'
    texture_names=[]
    images=[]
    digest=hashlib.sha256(paths[0].read_bytes()+paths[1].read_bytes())
    for name in names:
        assert re.fullmatch(r'[A-Za-z0-9_.-]+\.sprite',name),name
        imagepath=folder/'textures'/name.replace('.sprite','.png')
        if not imagepath.exists() and shared_textures is not None:imagepath=shared_textures/name.replace('.sprite','.png')
        image_stat=(imagepath.stat().st_mtime_ns,imagepath.stat().st_size)
        image=Image(imagepath)
        assert image.width*image.height*2<=4096,'Character texture exceeds TMEM'
        assert image.width&(image.width-1)==0 and image.height&(image.height-1)==0
        digest.update(imagepath.read_bytes());images.append(image)
        assert image_stat==(imagepath.stat().st_mtime_ns,imagepath.stat().st_size),'Character texture changed while being read'
    after=[(p.stat().st_mtime_ns,p.stat().st_size) for p in paths]
    assert before==after,'Character export changed while being read'
    if marker is not None:assert marker_stat==(marker.stat().st_mtime_ns,marker.stat().st_size),'Completion marker changed during read'
    return source_id,meshes,images,digest.hexdigest()


def imported_record(folder,slug,model,export):
    source_id,meshes,images,digest=export
    # Authored previews keep their old opaque disk; runtime actors use the
    # renderer's single ground-contact pass, independent of gait/bob/lean.
    meshes,_=strip_legacy_disks(meshes)
    remap={i:texture(img.rows) for i,img in enumerate(images)}
    scene=Scene('actor_'+slug)
    for _,part,t,pivot,vs,indices in meshes:
        key=(model,part,remap[t] if t>=0 else None,tuple(pivot))
        if key not in scene.batches:scene.batches[key]=(list(vs),list(indices),{})
        else:
            oldvs,oldindices,_=scene.batches[key];base=len(oldvs)
            oldvs.extend(vs);oldindices.extend(i+base for i in indices)
    target=OUT/'maps'/('actor_'+slug+'.l64')
    with tempfile.NamedTemporaryFile(dir=target.parent,prefix='.actor_',suffix='.tmp',delete=False) as f:
        temporary=Path(f.name)
    try:
        record=scene.save(temporary)
        write_if_changed(target,temporary.read_bytes())
    finally:temporary.unlink(missing_ok=True)
    record['file']=str(target.relative_to(OUT))
    record.update(character=True,slug=slug,model=model,source_model=source_id,
                  source=str(folder.relative_to(REPO)),source_sha256=digest)
    return record


def apply_characters(doc,roots=None,catalog_path=CATALOG,strict=False):
    doc=copy.deepcopy(doc)
    previous_meshes={}
    for c in doc.get('individual_characters',[]):
        path=OUT/c['file']
        if path.exists():
            names,meshes=read_l643(path);previous_meshes[c['file']]=(path.read_bytes(),names,meshes)
    if not catalog_path.is_file():
        doc.setdefault('individual_characters',[])
        return doc
    catalog=json.loads(catalog_path.read_text());roots=roots or DEFAULT_ROOTS
    fallback=list(doc.get('fallback_npc_models',doc['npc_models']))
    doc['fallback_npc_models']=fallback
    models=list(fallback)
    old={c['slug']:c for c in doc.get('individual_characters',[])}
    families={f['slug']:f for f in catalog['families']}
    requests=[]
    for family in catalog['families']:
        # Source sprite IDs are stable in the ROM and independent of the
        # character demo's private model numbering (Ronald happens to be1).
        requests.append((family['slug'],7+family['sprite'],family['slug'],family['slug'],'canonical',None))
    extras=sorted(a['slug'] for a in catalog.get('appearances',[]) if a['slug']!=a['model'])
    for index,slug in enumerate(extras):
        appearance=next(a for a in catalog['appearances'] if a['slug']==slug)
        requests.append((slug,128+index,appearance['model'],appearance['model']+'/variants/'+slug,'appearance',None))
    poses=sorted({(a['model'],a['pose']) for a in catalog['aliases'].values() if a.get('pose') not in (None,'standing','west_fixed')})
    for index,(family,pose) in enumerate(poses):
        requests.append((family+'__'+pose,160+index,family,family+'/poses/'+pose,'pose',pose))
    components=sorted({(a['model'],a['component']) for a in catalog['aliases'].values() if a.get('component')})
    for index,(family,component) in enumerate(components):
        requests.append((family+'__'+component,176+index,family,family+'/components/'+component,'component',component))
    assert len({r[1] for r in requests})==len(requests),'Character runtime ID collision'
    available={};warnings=[]
    for slug,model,family,relative,kind,selector in requests:
        assert re.fullmatch(r'[a-z0-9_]+',slug),slug
        assert 7<=model<256
        candidate=None
        for root in roots:
            root=Path(root)
            central=root.resolve()==CENTRAL_ROOT.resolve()
            folder=root/(relative if central else slug)
            if not (folder/'scene.l64').exists():continue
            try:
                export=source_export(folder,slug,shared_textures=root.parent/'textures',
                                     marker=root/family/'complete.json' if central else None,
                                     family=family,appearance=slug if kind=='appearance' else family,
                                     pose=selector if kind=='pose' else None,component=selector if kind=='component' else None)
                if export is not None:candidate=imported_record(folder,slug,model,export);break
            except (OSError,ValueError,AssertionError,KeyError,IndexError,TypeError,struct.error) as error:
                if strict:raise
                warnings.append(f'{slug}: incomplete export skipped ({error})')
        if candidate is None and slug in old:
            previous=old[slug]
            if (OUT/previous['file']).is_file():candidate=previous
        if candidate:
            candidate['family']=family;candidate['kind']=kind;candidate['selector']=selector;available[slug]=candidate
    for name,alias in catalog['aliases'].items():
        component=alias.get('component')
        character=available.get(alias['model']+'__'+component) if component else None
        character=character or available.get(alias.get('appearance')) or available.get(alias['model'])
        if character:
            npc=alias['npc_id'];assert 0<npc<256
            models[npc]=character['model']
    for c in available.values():
        c['aliases']=[name for name,alias in catalog['aliases'].items() if models[alias['npc_id']]==c['model']]
        c['npc_ids']=[catalog['aliases'][name]['npc_id'] for name in c['aliases']]
    pose_models=[0]*256
    for alias in catalog['aliases'].values():
        pose=alias.get('pose')
        character=available.get(alias['model']+'__'+pose) if pose else None
        if character:pose_models[alias['npc_id']]=character['model']
    doc['npc_pose_models']=pose_models
    doc['npc_models']=models
    doc['player_model']=available.get('mark',{}).get('model',1)
    doc['individual_characters']=sorted(available.values(),key=lambda c:c['model'])
    doc['character_catalog']=str(catalog_path.relative_to(REPO))
    for name in list(doc['meshes']):
        if doc['meshes'][name].get('character'):del doc['meshes'][name]
    for c in available.values():doc['meshes']['actor_'+c['slug']]=c
    for m in doc['maps']:
        for npc in m['npcs']:npc['model']=models[npc['npc']]
    keys={t for record in doc['meshes'].values() for t in record['textures']}
    doc['texture_count']=len(keys)
    for warning in warnings:print('Character sync:',warning,file=sys.stderr)
    finish_runtime_grounding(OUT,doc,previous_meshes)
    return doc


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root',action='append',type=Path,help='Additional/override source models root (repeatable)')
    p.add_argument('--strict',action='store_true',help='Fail instead of retaining prior data for incomplete exports')
    args=p.parse_args()
    path=OUT/'world.json'
    assert path.exists(),'Run build_world.py once to generate base world metadata'
    doc=apply_characters(json.loads(path.read_text()),roots=args.root,strict=args.strict)
    write_world_metadata(OUT,doc)
    ready=doc.get('individual_characters',[])
    families={c['family'] for c in ready if c['kind']=='canonical'}
    aliases=sum(len(c['aliases']) for c in ready)
    print(f"Character sync: {len(ready)} exports, {len(families)} families, {aliases} NPC aliases; player model{doc.get('player_model',1)}")

if __name__=='__main__':main()
