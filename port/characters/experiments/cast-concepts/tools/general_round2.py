"""Record changes after inspecting each actual first native capture."""
import json,copy,subprocess,shutil
from pathlib import Path
r=Path(__file__).resolve().parents[1];f=r/'general-designs.json';d=json.loads(f.read_text())
notes={
'mania':('The owning hair crown forms an excessive cone; the white polo and joined shoulders read clearly, but round eyes look too sweet.','Move the offset forelock into the outer crown band, lower and broaden the upper closure, narrow white eyes and angle brows.',dict(crown=.70,upper_band=.61,peak_band=4,peak_height=.18,sweep=-.065,eye_h=9,eye_shape='rect',iris_w=3,brow_end=4,mouth='smirk')),
'boy':('The large child head and short legs distinguish Ryan, but the brown hair is too bowl-shaped and the side part weak.','Lower the crown and upper band, strengthen lateral sweep and forehead part, keep the compact body.',dict(crown=.72,upper_band=.61,sweep=-.08,chin=.215,nose=.06)),
'man':('Yellow polo and round adult cheeks read clearly, but the whole upper scalp looks bald and eyes too childlike.','Restore brown hair over the back crown with a receding front, narrow eyes and modestly strengthen the nose.',dict(bald=False,receding=True,crown=.69,upper_band=.62,eye_h=9,eye_shape='rect',iris_w=3,nose=.105,large_collar=True)),
'hood':('The red owning hood is connected and identifies Nicholas, but its excessive cone crowds the silhouette.','Lower and broaden the red hood, remove the closure inflation, keep the connected dark forelocks and give a more confident narrow eye.',dict(two_points=False,crown=.69,upper_band=.61,eye_h=9,eye_shape='rect',iris_w=3,chin=.195)),
'lad':('Cap brim, green outfit and bare shins are visible; the cap is too tall and lacks the reference white badge, and collar is tiny.','Lower the cap crown, paint the white badge onto the cap shell and widen the flat collar.',dict(crown=.71,upper_band=.62,cap_badge=True,large_collar=True,mouth_w=9)),
'specs':('Purple shirt and rectangular spectacles distinguish Adam, but the crown is still too round and face similar to tech.','Lower the crown, strengthen tidy lateral part, narrow irises and widen the flat collar.',dict(crown=.69,upper_band=.61,sweep=-.07,iris_w=3,large_collar=True,chin=.185)),
'chap':('Green vest over white sleeves reads cleanly, but the hair is a cone and face too round/sweet for the original confident chap.','Move the modest offset peak into an outer band, keep a low broad closure, use narrow eyes and an angled brow/smirk.',dict(crown=.73,upper_band=.61,peak_band=4,peak_height=.21,sweep=-.045,eye_h=9,eye_shape='rect',iris_w=3,mouth='smirk',large_collar=True)),
'pappy':('Bald crown, white side/back hair and purple vest are clear, but pale brows nearly vanish and large eyes feel youthful.','Lower the bald dome, darken gray brows, use narrower mature eyes and a fuller nose/chin.',dict(crown=.67,upper_band=.59,brow=[128,129,121],eye_h=9,eye_shape='rect',iris_w=3,nose=.125,chin=.27,large_collar=True)),
'butch':('Broad head and yellow shirt read well, but the eyes are too cute and the lower body too tall for the stocky identity.','Use narrow level eyes and strong brows, broaden jaw/shoulders and shorten legs with a slightly lower shirt hem.',dict(eye_h=8,eye_shape='rect',iris_w=3,brow_end=6,chin=.30,body_width=.39,shoulder=.385,hip=.78,hem=.74,mouth='smirk',large_collar=True))}
for s,(observation,change,params) in notes.items():
 assert (r/'models'/s/'i1/native/comparison.png').is_file()
 (r/'models'/s/'i1/analysis.md').write_text('Native round1 review\n\n'+observation+'\n\nNext revision: '+change+'\n\nEvidence:native/comparison.png and8 native diagnostic frames from the production renderer in ares; not physical-console gameplay.\n')
 c=copy.deepcopy(d[s]['1']);c.update(params);d[s]['2']=c
f.write_text(json.dumps(d,indent=2)+'\n')
for s in notes:
 q=subprocess.run(['python3',str(r/'tools/general_build.py'),s,'2'],text=True,capture_output=True)
 if q.returncode:print(q.stderr[-1600:]);raise SystemExit(1)
 print(q.stdout.strip())
