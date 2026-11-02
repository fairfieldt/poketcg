#!/usr/bin/env python3
"""Keep painted eyes on front cheeks, without changing approved texture pixels."""
import argparse,copy,hashlib,json,math,shutil,struct,sys
from pathlib import Path
from PIL import Image
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT.parent/'mark-concepts/tools'))
from audit_exports import audit

def eye_angles(doc,model,factor):
 angles=[]
 for mesh in doc['meshes']:
  if 'head' not in mesh['name']:continue
  materials={m['name']:m for m in mesh['materials']}
  for face in mesh['faces']:
   if face['mat']!='face' or not face['uv']:continue
   texture=materials['face']['texture'];im=Image.open(model/'textures'/f'{texture}.png').convert('RGB')
   uv=[(max(0,min(1,(u-.5)/factor+.5)),v) for u,v in face['uv']]
   for k in range(1,len(face['idx'])-1):
    ids=(0,k,k+1)
    for i in range(21):
     for j in range(21-i):
      weights=(i/20,j/20,(20-i-j)/20)
      u,v=(sum(weights[q]*uv[ids[q]][axis] for q in range(3)) for axis in range(2))
      if v>=.75:continue
      color=im.getpixel((min(31,max(0,int(u*32))),min(31,max(0,int(v*32)))))
      if model.name=='joshua':
       if not (13/32<=v<17/32 and color==(24,24,24)):continue
      elif min(color)<=210:continue
      x,y=(sum(weights[q]*mesh['verts'][face['idx'][ids[q]]][axis] for q in range(3)) for axis in range(2))
      angles.append(math.degrees(math.atan2(abs(x),y)))
 return angles

def correct(slug,revision='i3e',destination=None):
 source=ROOT/'models'/slug/revision;folder=ROOT/'models'/slug/(destination or revision)
 if destination:
  assert not folder.exists();shutil.copytree(source,folder,ignore=shutil.ignore_patterns('native','*.blend1'))
 model=folder/slug;doc=json.loads((model/'authoring.json').read_text());before=eye_angles(doc,model,1)
 prior=json.loads((folder/'front-face.json').read_text()) if (folder/'front-face.json').exists() else None
 inherited=json.loads((ROOT/'models/amy'/revision/'front-face.json').read_text()) if slug=='amy__lying' else None
 if inherited:before=[inherited['before_max_eye_white_angle']]
 if not before:return False
 factor=1.0
 for n in range(18):
  if inherited:
   factor=inherited['additional_horizontal_span'];after=[inherited['after_max_eye_white_angle']];break
  factor=round(1-.02*n,2);after=eye_angles(doc,model,factor)
  if after and max(after)<=43:break
 assert after and max(after)<=43,(slug,factor,max(after))
 originalhash=hashlib.sha256((model/'scene.l64').read_bytes()).hexdigest();texnames=set()
 for mesh in doc['meshes']:
  if 'head' not in mesh['name']:continue
  for material in mesh['materials']:
   if material['name']=='face':texnames.add(material['texture']+'.sprite')
  for face in mesh['faces']:
   if face['mat']=='face' and face['uv']:face['uv']=[(max(0,min(1,(u-.5)/factor+.5)),v) for u,v in face['uv']]
 (model/'authoring.json').write_text(json.dumps(doc,indent=2)+'\n')
 raw=bytearray((model/'scene.l64').read_bytes());_,nb,nt=struct.unpack_from('>4sHH',raw);names=[raw[8+32*k:40+32*k].split(b'\0')[0].decode() for k in range(nt)];offset=8+32*nt;changed=0
 for _ in range(nb):
  _,part,tex,_,nv,ni,*_=struct.unpack_from('>HHhHII3f',raw,offset);offset+=28
  if tex>=0 and names[tex] in texnames:
   assert part==0
   for i in range(nv):
    at=offset+i*24+12;u=struct.unpack_from('>f',raw,at)[0];struct.pack_into('>f',raw,at,max(0,min(1,(u-.5)/factor+.5)));changed+=1
  offset+=nv*24+ni*2;offset+=(-offset)%4
 assert offset==len(raw);(model/'scene.l64').write_bytes(raw)
 note=dict(sampled_feature='closed black eyelids (no eye whites in Joshua source)' if slug=='joshua' else 'painted eye whites',before_max_eye_white_angle=prior['before_max_eye_white_angle'] if prior else max(before),after_max_eye_white_angle=max(after),additional_horizontal_span=factor*(prior['additional_horizontal_span'] if prior else 1),target_degrees=43,sampling='21-step barycentric samples of actual painted eye feature; no sampled feature beyond43deg; Joshua uses closed eyelids; lying inherits standing mapping',face_vertices=changed,original_native_sha256=originalhash,texture_png_bytes_unchanged=True,positions_indices_pivots_colors_unchanged=True)
 (folder/'front-face.json').write_text(json.dumps(note,indent=2)+'\n');shutil.copyfile(__file__,folder/'front-face-source.py')
 proof=json.loads((folder/'audit.json').read_text());proof['native']=audit(model/'scene.l64');assert proof['native']['passed'];proof['front_face']=note;(folder/'audit.json').write_text(json.dumps(proof,indent=2)+'\n')
 print(slug,folder.name,'UV span',factor,'eye angle',round(max(before),1),'->',round(max(after),1),flush=True)
 return factor<1
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('slugs',nargs='+');p.add_argument('--revision',default='i3e');p.add_argument('--destination');a=p.parse_args()
 for slug in a.slugs:correct(slug,a.revision,a.destination)
