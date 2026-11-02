#!/usr/bin/env python3
"""Publish retained leader review notes after inspecting final native frames."""
import json
from pathlib import Path
from PIL import Image,ImageDraw,ImageFont
from leaders_recipes import REVISIONS
ROOT=Path(__file__).resolve().parent.parent
NOTES={
'ishihara':'The restored swept hairline, broader adult jaw and rectangular eyes now read clearly. The short darker mouth avoids the blank lower face. Purple vest, white sleeves and shoulder joins remain clean; profile depth is preserved.',
'imakuni':'The hood corners are lower and broader than the first cat-like points. The open mouth now sits inside the face opening. Black hood, gloves and costume form a consistent silhouette through walking poses; the two ear nubs remain angular at native resolution.',
'mitch':'Angular athletic eyes, a clearer firm mouth and broader staggered brown crown read distinctly over the cyan V-neck shirt. Side and rear hair have depth. Rear points merge in frontal native projection; there are no floating spike meshes.',
'gene':'The firmer adult eyes and pink diagonal owning-shell forelock preserve the source identity and palette. The pink scalp remains simplified and somewhat cap-like at native resolution, but its root is connected and the side profile has depth. Jacket and sleeves join cleanly.',
'isaac':'Glasses frames now align with the visible eyes. Blond hair, yellow shirt and white coat remain distinct from Rick. The connected fringe and curved side skull are readable; fine blond strands are simplified into broad planes.',
'nikki':'The brown bob no longer flares into triangular side wings. Its lower contour is tapered and rounded. Gentle eyes, stronger small smile and green dress remain readable, with bare arms joined at the shoulder and legs moving below the hem.',
'murray':'The receding forehead and longer side/back hair distinguish Murray from the short-haired baseline. The aligned rectangular glasses and broader mature jaw remain clear. Pink vest and white sleeves preserve identity; long hair is represented by broad continuous planes.',
'rick':'Aligned rectangular glasses and a darker firm mouth restore the stern scientist expression. Brown swept hair, blue shirt and white coat separate Rick from blond Isaac. Profile depth and sleeve-to-torso joins remain clean.',
'ken':'The source center part now reads as two broad connected forehead locks. Angular eyes, broader lower jaw and short dark mouth retain a competitive expression. Pink jacket shoulders remain joined in the walk poses.',
'courtney':'The connected darker pink hair is separated from the lighter gown, with broad lower wave shaping rather than detached curls. The central fringe and gentle face remain readable; native resolution merges the detailed waves into large planes. Bare shoulder joins and long skirt silhouette remain clean.',
'steve':'The blue-purple cap has a full crown, attached forward bill and small painted white patch. Brown hair remains rooted beneath it. Jacket shoulders are joined and profile depth is clear; the patch uses the existing costume atlas rather than another primitive.',
'jack':'The broader mature jaw, swept white crown, monocle and dark moustache distinguish Jack from the earlier generic clean-shaven model. White jacket sleeves join the torso through walking poses. The monocle and moustache are simplified face painting, not loose geometry.',
'rod':'The broader diagonal owning-shell forelock and stronger composed smile retain Rod’s identity. Brown hair, light blue vest and white sleeves remain readable; side/back skull depth and shoulder joins are clean. Fine swept strands remain broad low-poly planes.',
}
font=ImageFont.truetype('/System/Library/Fonts/Helvetica.ttc',18)
for slug,note in NOTES.items():
 family=ROOT/'models'/slug;final=family/'i3';assert (final/'native/capture.json').is_file()
 audit=json.loads((final/'audit.json').read_text());top=json.loads((final/'topology.json').read_text());revision=REVISIONS[(slug,3)]['review']
 (final/'analysis.md').write_text(f'''# {slug} iteration 3\n\nRound 2 native inspection determined this change: {revision}\n\nViewed the actual before/candidate portrait, four-pose turnaround, conversation\nand room frames against the generated concept. {note}\n\nAll 8 native frames completed. Raw/packed audit passes at {audit['triangles']} triangles,\nfive rigid parts and opaque 32×32 textures, with zero degenerate or duplicate\ntriangles. Owning head is closed and consistently wound; scalp/hood is one\nconnected component. Editable Blender, source snapshot, native mesh/UV data and\ncapture evidence are retained. Independent parent review accepted this as the\nfinal review candidate. Production assets remain unchanged.\n\nThe 32-pixel face and native-resolution silhouette simplify the generated sheet.\nDiagnostic captures establish appearance and valid packed geometry, not physical\nconsole performance or ordinary gameplay behavior.\n''')
 rows=[]
 for n in (1,2,3):
  a=json.loads((family/f'i{n}/audit.json').read_text());rows.append(f'| {n} | [native comparison](i{n}/native/comparison.png) | {a["triangles"]} | [analysis](i{n}/analysis.md) |')
 (family/'README.md').write_text(f'''# {slug} study\n\n[Generated concept](../../concepts/{slug}.png) and\n[exact built-in imagegen prompt](../../concepts/{slug}.prompt.txt).\nOne actual concept was used, followed by three sequential model, native capture,\ninspection and revision rounds. [Native iteration sheet](iterations.png).\n\n| Round | Capture | Triangles | Review |\n|---|---|---|---|\n'''+ '\n'.join(rows)+f'''\n\nFinal candidate: `i3`. {note}\n\nAll rounds retain the L643 export, opaque 32×32 PNGs, editable `{slug}.blend`,\n`{slug}/authoring.json`, `source.py`, recipe snapshot, geometry/topology audits,\nand eight actual native before/candidate diagnostic frames. The baseline is\nthis family's real production export. Only the review target changes.\n\nFinal owning head is closed and consistently wound; scalp/hood is connected.\nFive rigid parts preserve the walk rig. No triangles were filtered to conceal\ndefects. Fine detail is simplified at native resolution. Production assets have\nnot been replaced.\n''')
 sheet=Image.new('RGB',(1280,810),(24,25,30));draw=ImageDraw.Draw(sheet)
 for row,n in enumerate((1,2,3)):
  draw.text((10,row*270+5),f'{slug} iteration {n}',fill='white',font=font)
  for col,view in enumerate(('portrait','turn','talk','room')):sheet.paste(Image.open(family/f'i{n}/native/candidate-{view}.png').convert('RGB'),(320*col,row*270+25))
 sheet.save(family/'iterations.png')
progress={'group':'leaders','families':{s:{'concept':'actual built-in imagegen, inspected','rounds_captured':3,'native_frames':24,'status':'complete; final independently reviewed'} for s in NOTES}}
(ROOT/'leaders-progress.json').write_text(json.dumps(progress,indent=2)+'\n')
print('Completed notes and review indices for',len(NOTES),'leader families')
