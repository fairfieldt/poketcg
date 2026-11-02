#!/usr/bin/env python3
"""Paint the smaller adult eyes and compact rectangular glasses from the revised reference."""
import argparse,json,shutil,struct,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT.parent/'n64-style/tools'));sys.path.insert(0,str(ROOT.parent/'mark-concepts/tools'))
from paint import Canvas,eye,glasses,mouth
from audit_exports import audit

def correct(slug,revision):
 assert slug in ('dr_mason','tech')
 folder=ROOT/'models'/slug/revision;model=folder/slug;path=model/'authoring.json';doc=json.loads(path.read_text())
 names={m['texture'] for mesh in doc['meshes'] for m in mesh['materials'] if m['name']=='face'};assert len(names)==1,names;old=names.pop();name=f'round_{slug}_face'
 skin=next(m['rgb'] for mesh in doc['meshes'] for m in mesh['materials'] if m['name']=='skin')
 c=Canvas(background=tuple(round(x*255) for x in skin));ink=(42,33,26);brow=(124,125,117) if slug=='dr_mason' else (42,33,27)
 for cx in (9,23):
  eye(c,cx,17,6,7,3,5,iris=(42,49,57),shape='rect',highlight=False)
  c.line([(cx-3,10),(cx+3,10)],brow,2)
 glasses(c,16,17,9,9,gap=5,color=ink,shape='rect')
 mouth(c,16,27,8,color=(105,60,35));c.px(16,23,(191,124,67))
 c.save(model/'textures'/f'{name}.png')
 for mesh in doc['meshes']:
  for mat in mesh['materials']:
   if mat['texture']==old:mat['texture']=name
 if isinstance(doc.get('textures'),dict):doc['textures']['names']=[name if n==old else n for n in doc['textures']['names']]
 path.write_text(json.dumps(doc,indent=2)+'\n')
 raw=bytearray((model/'scene.l64').read_bytes());_,_,nt=struct.unpack_from('>4sHH',raw);changes=0
 for k in range(nt):
  offset=8+32*k;label=raw[offset:offset+32].split(b'\0')[0].decode()
  if label==old+'.sprite':raw[offset:offset+32]=(name+'.sprite').encode().ljust(32,b'\0');changes+=1
 assert changes==1; (model/'scene.l64').write_bytes(raw)
 meta=json.loads((model/'scene.json').read_text());meta['textures']=[name if n==old else n for n in meta['textures']];meta['face_recipe']='tools/correct_scientist_faces.py';(model/'scene.json').write_text(json.dumps(meta,indent=2)+'\n')
 if old!=name:(model/'textures'/f'{old}.png').unlink()
 recipe=dict(concept='concepts/round-heads/mason-sam.png',reference='Earlier Mason face, before aa9017e1 integration',eye=[6,7],pupil=[3,5],glasses=[9,9],glasses_shape='rectangular',prior_eye=[8,11],prior_glasses=[11,14],texture=[32,32],skin_color_preserved=True)
 (folder/'face-correction.json').write_text(json.dumps(recipe,indent=2)+'\n');shutil.copyfile(__file__,folder/'face-source.py');print(slug,revision,name)
 proof=json.loads((folder/'audit.json').read_text());proof['native']=audit(model/'scene.l64');assert proof['native']['passed'];proof['face_paint']='correct_scientist_faces.py: compact adult eyes and rectangular glasses';(folder/'audit.json').write_text(json.dumps(proof,indent=2)+'\n')
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('slug',choices=('dr_mason','tech'));p.add_argument('revision',default='i3e',nargs='?');a=p.parse_args();correct(a.slug,a.revision)
