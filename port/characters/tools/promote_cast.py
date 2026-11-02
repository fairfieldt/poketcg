#!/usr/bin/env python3
"""Promote reviewed cast exports; preserve canonical bindings and prop assets."""
import hashlib,json,shutil,struct
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
STUDY=ROOT/'experiments/cast-concepts'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def remap(data,model):
 data=bytearray(data);_,count,nt=struct.unpack_from('>4sHH',data);offset=8+32*nt
 for _ in range(count):
  _,_,_,_,nv,ni,*_=struct.unpack_from('>HHhHII3f',data,offset)
  struct.pack_into('>H',data,offset,model);offset=(offset+28+24*nv+2*ni+3)&~3
 assert offset==len(data)
 return data

def main():
 bindings=json.loads((STUDY/'review/final-bindings.json').read_text())['bindings']
 assert json.loads((ROOT/'bindings.json').read_text())=={k:{a:b for a,b in v.items() if not a.startswith('candidate_')} for k,v in bindings.items()}
 rows={v['native_asset']:v for v in bindings.values() if v['model'] not in ('torch','legendary_card')}
 for family in json.loads((STUDY/'review/final-families.json').read_text()):
  slug=family['slug'];relative=f'assets/models/{slug}/scene.l64'
  sample=next(v for v in bindings.values() if v['model']==slug)
  rows[relative]=dict(sample,appearance=slug,pose='standing',candidate_asset=family['prefix']+f'/{slug}/scene.l64',candidate_blender=family['prefix']+f'/{slug}.blend',candidate_selection=slug,native_asset=relative)
 report=[]
 for relative,binding in rows.items():
  source=STUDY/binding['candidate_asset'];dest=ROOT/relative;meta=json.loads(dest.with_suffix('.json').read_text());candidate=json.loads(source.with_suffix('.json').read_text())
  dest.write_bytes(remap(source.read_bytes(),binding['runtime_model']))
  for k in ('textures','vertices','triangles','batches','parts'):
   if k in candidate:meta[k]=candidate[k]
  meta.update(appearance=binding['appearance'],pose='standing' if binding['pose']=='west_fixed' else binding['pose'],component=binding['component'])
  meta['actors']=[dict(name=binding['candidate_selection'],model=binding['runtime_model'],x=0,z=0,heading=0)]
  meta['design']='Reviewed cast model'
  meta['sharing']={'linked_parts':0,'authoring':'Editable rigid meshes; aliases share canonical source'}
  # Old palette/skin/surface statistics describe the replaced geometry.
  for key in ('skin_palette','surface_ownership','appearance_geometry'):meta.pop(key,None)
  meta['reviewed_source']=dict(asset=str(source.relative_to(ROOT)),sha256=sha(source),selection=binding['candidate_selection'])
  dest.with_suffix('.json').write_text(json.dumps(meta,indent=2)+'\n')
  for image in (source.parent/'textures').glob('*.png'):
   target=ROOT/'assets/textures'/image.name
   if target.exists() and sha(target)!=sha(image):raise AssertionError(('texture collision',image))
   shutil.copyfile(image,target)
  if (source.parent/'authoring.json').exists():shutil.copyfile(source.parent/'authoring.json',dest.parent/'authoring.json')
  if binding['candidate_selection']==binding['model']:
   shutil.copyfile(STUDY/binding['candidate_blender'],ROOT/binding['blender_file'])
   shutil.copyfile(source.parent/'authoring.json',dest.parent/'authoring.json')
  report.append(dict(model=binding['model'],appearance=binding['appearance'],pose=binding['pose'],source=str(source.relative_to(ROOT)),source_sha256=sha(source),target=relative,sha256=sha(dest),runtime_model=binding['runtime_model'],triangles=meta['triangles']))
 for family in sorted({v['model'] for v in rows.values()}):
  folder=ROOT/'assets/models'/family;meta=json.loads((folder/'scene.json').read_text());old=json.loads((folder/'complete.json').read_text())
  old.update({k:meta[k] for k in ('triangles','vertices','batches')});old.update(linked_parts=0,fingerprint=sha(folder/'scene.l64'),recipe='reviewed-cast-manual')
  (folder/'complete.json').write_text(json.dumps(old,indent=2)+'\n')
 out=ROOT/'review/cast-integration.json';out.write_text(json.dumps(dict(exports=report,bindings_unchanged=True,props_unchanged=True),indent=2)+'\n')
 print(f'Promoted {len(report)} exports from {len({v["model"] for v in rows.values()})} families')
if __name__=='__main__':main()
