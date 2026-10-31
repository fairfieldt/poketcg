#!/usr/bin/env python3
import json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
catalog=json.loads((ROOT/'catalog.json').read_text())
lines=['# Model index','','One row per authored model. Repeated NPC IDs use the same file.','',
       '| Model | Triangles | Blender source | NPC IDs |','|---|---:|---|---|']
for family in catalog['families']:
    slug=family['slug'];stats=json.loads((ROOT/'assets/models'/slug/'complete.json').read_text())
    aliases=', '.join('`'+a.removeprefix('NPC_')+'`' for a in family['aliases'])
    lines.append(f'| {slug.replace("_"," ").title()} | {stats["triangles"]} | [{slug}.blend](assets/models/{slug}/{slug}.blend) | {aliases} |')
lines += ['','## Material variants','','| Appearance | Shared model | Native export |','|---|---|---|']
for a in catalog['appearances']:
    if a['slug']==a['model']:continue
    slug=a['model'];appearance=a['slug']
    lines.append(f'| {appearance} | {slug} | [scene.l64](assets/models/{slug}/variants/{appearance}/scene.l64) |')
lines += ['','![Material and pose variants](review/variations.png)','','The complete NPC-to-asset lookup is [bindings.json](bindings.json).','']
(ROOT/'MODELS.md').write_text('\n'.join(lines))
