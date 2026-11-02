"""Record the final changes after actual second-round native review."""
import json,copy,subprocess
from pathlib import Path
r=Path(__file__).resolve().parents[1];f=r/'general-designs.json';d=json.loads(f.read_text())
notes={
'mania':('The cone is gone and the local offset forelock reads clearly. Narrow confident eyes distinguish Chris,but the iris fills too much white and the hair reads almost black.','Broaden the upper hair crown and jaw modestly,warm the brown hair,and retain narrow eyes with shorter highlighted irises.',dict(width=.385,chin=.20,hair=[.23,.14,.10],crown_width=.80,pupil_h=6,highlight=True)),
'boy':('The lower side sweep is better and the compact body remains distinct,but the forehead part is still timid in the native portrait.','Deepen the connected left fringe and broaden the child cheeks slightly without adding loose hair.',dict(part_depth=.105,width=.405,body_width=.29)),
'man':('Back-crown brown hair is restored,but the face still looks fully bald from the front and rectangular eyes read too slitlike.','Add a small connected receding left hair wedge,soften adult eyes with visible whites,and strengthen the rounded chin/profile.',dict(receding_tuft=True,eye_h=10,eye_shape='oval',pupil_h=6,highlight=True,eye_y=17,chin=.27,nose=.12)),
'hood':('The excessive cone is gone,but the red headwear reads more like a beanie because the owning rear shell ends above the neckline.','Extend the owning red rear shell to the shoulder neckline,round the dome modestly,and clarify highlighted confident eyes.',dict(hood_neck=True,crown=.72,upper_band=.63,pupil_h=6,highlight=True)),
'lad':('The white cap badge and flat collar now survive native sampling,and cap height is improved. The face could be broader and friendlier under the cap.','Broaden the child cheeks/chin and shoulders slightly,and widen the friendly mouth while retaining brim,shorts and bare shins.',dict(width=.41,chin=.235,shoulder=.30,mouth_w=10)),
'specs':('The tidy crown and angular spectacles distinguish Adam,but the narrow irises lost their highlights and appear flat.','Restore a small painted eye highlight and white margin,slightly strengthen the profile nose while keeping the slim head.',dict(pupil_h=7,highlight=True,nose=.08,part_depth=.09)),
'chap':('The cone is gone and the offset combed peak reads well. The confident face is now too slitlike at native scale.','Keep the local peak and green waistcoat,but use slanted whites with shorter highlighted irises and a broader upper crown.',dict(eye_h=10,eye_shape='slant',pupil_h=6,highlight=True,crown_width=.78)),
'pappy':('Gray brows and mature face are clearer,but the purple sweater vest wrongly has an open white strip like a waistcoat.','Paint a closed purple sweater vest with only the white collar and belt,broaden mature cheeks,and use gentle highlighted oval eyes.',dict(vest='closed',width=.415,eye_h=10,eye_shape='oval',pupil_h=6,highlight=True)),
'butch':('Stockier legs,broader jaw and stronger brows improve the identity,but rectangular eyes are too flat and the hair crown narrow.','Broaden the short upper crown,warm the dark brown hair,and preserve a confident face with slanted white eye margins and highlighted irises.',dict(crown_width=.82,hair=[.20,.12,.08],eye_h=9,eye_shape='slant',pupil_h=6,highlight=True))}
for s,(observation,change,params) in notes.items():
 assert (r/'models'/s/'i2/native/comparison.png').is_file()
 (r/'models'/s/'i2/analysis.md').write_text('Native round2 review\n\n'+observation+'\n\nNext revision: '+change+'\n\nEvidence:native/comparison.png and8 production-renderer diagnostic frames in ares.\n')
 c=copy.deepcopy(d[s]['2']);c.update(params);d[s]['3']=c
f.write_text(json.dumps(d,indent=2)+'\n')
for s in notes:
 q=subprocess.run(['python3',str(r/'tools/general_build.py'),s,'3'],text=True,capture_output=True)
 if q.returncode:print(q.stderr[-1600:]);raise SystemExit(1)
 print(q.stdout.strip())
(r/'models/tech/i3/analysis.md').write_text('Final native round3 review\n\nClipped rectangular lens corners distinguish the technician from Mason. The lower connected lateral hair sweep and tucked nape are cleaner than round1. Broader adult jaw/head and modest nose preserve a rounded side profile. Blue tie,long white coat and joined sleeves remain clear.\n\nNative comparison and8 diagnostic production-renderer frames were inspected. Export560triangles,five rigid parts,two opaque32textures; zero raw/packed degenerates or duplicates; owning head closed and hair connected. Production assets unchanged; no physical-console gameplay claim.\n')
