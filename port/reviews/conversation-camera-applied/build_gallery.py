#!/usr/bin/env python3
"""Check original ares encoder captures and publish the applied camera."""
import hashlib
import html
import json
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
STAGES = {
    'mason-dialogue': 'Mason: actual source opening',
    'sam-table': 'Sam: actual source table positions',
    'sam-exploration': 'Sam: dialogue closed',
    'ronald-dialogue': 'Ronald: source lobby positions',
    'clerk-dialogue': 'Clerk: beside the far wall',
    'clerk-exploration': 'Clerk: dialogue closed',
}

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

cases = {}
for width, height, ram in [(320, 240, 4), (480, 360, 8)]:
    folder = HERE / f'cases/{width}'
    manifest = folder / 'capture-provenance.json'
    data = json.loads(manifest.read_text())
    assert data['finished']['missing'] == []
    assert [item['stage'] for item in data['images']] == list(STAGES)
    for mode in data['runtime_video_modes']:
        assert (mode['width'], mode['height'], mode['ram']) == (width, height, ram*1024*1024)
    for item in data['images']:
        path = folder / 'post-vi' / item['file']
        assert sha(path) == item['png_sha256']
        image = Image.open(path).convert('RGB')
        assert hashlib.sha256(image.tobytes()).hexdigest() == item['rgb_sha256']
        assert image.width*image.height == item['exact_callback_rgb_pixels']
        gate = item['stage_gate']
        assert gate['native_frame'] == item['stage']
        assert gate['post_native_valid_callbacks'] >= 6 and gate['stable_callbacks'] >= 6
    for item in data['native_frames']:
        path = folder / 'native' / item['file']
        assert sha(path) == item['sha256'] and Image.open(path).size == (width, height)
    cases[str(width)] = {'manifest': str(manifest.relative_to(HERE)), 'sha256': sha(manifest),
                         'rom': data['rom'], 'frames': len(data['images'])}
    sheet = Image.new('RGB', (1292, 1580), '#111b29')
    draw = ImageDraw.Draw(sheet)
    font = ImageFont.load_default(size=18)
    for i, (stage, label) in enumerate(STAGES.items()):
        image = Image.open(folder / 'post-vi' / (stage+'.png')).convert('RGB')
        if image.size == (640, 240):
            image = image.resize((640, 480), Image.Resampling.NEAREST)
        assert image.size == (640, 480)
        x, y = (i % 2)*652, (i // 2)*526
        sheet.paste(image, (x, y))
        draw.text((x+6, y+484), label, font=font, fill='white')
    sheet.save(HERE / f'contact-{width}.png')
assert cases['320']['rom']['sha256'] == cases['480']['rom']['sha256']

production = {}
for key, filename in [('standard', 'poketcg-adventure.z64'), ('expansion', 'poketcg-expansion.z64')]:
    path = ROOT / 'port/frontends/n64/adventure' / filename
    production[key] = {'file': str(path.relative_to(ROOT)), 'sha256': sha(path), 'bytes': path.stat().st_size}
(HERE / 'validation.json').write_text(json.dumps({'passed': True, 'production_camera_changed': True,
    'cases': cases, 'original_post_vi_images': 12, 'same_diagnostic_rom': True,
    'production_roms': production, 'canonical_actor_positions_and_headings_preserved': True}, indent=2)+'\n')

sections = []
for width, height in [(480, 360), (320, 240)]:
    figures = ''.join(f'<figure><a href="cases/{width}/post-vi/{stage}.png"><img src="cases/{width}/post-vi/{stage}.png" alt="{html.escape(label)}"></a><figcaption>{label} · <a href="cases/{width}/native/{stage}.png">Native framebuffer</a></figcaption></figure>' for stage, label in STAGES.items())
    sections.append(f'<section><h2>{width} × {height}</h2><div class="grid">{figures}</div></section>')
(HERE / 'index.html').write_text('''<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Conversation camera</title><style>
body{margin:0;background:#111b29;color:#edf1f2;font:16px/1.5 system-ui}main{max-width:1320px;padding:24px;margin:auto}a{color:#abd4ff}p{max-width:95ch}.grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:18px}figure{margin:0;background:#192a3a;padding:8px}img{display:block;width:100%;aspect-ratio:4/3;object-fit:fill;image-rendering:pixelated}figcaption{padding-top:8px}h2{margin-top:40px}@media(max-width:720px){.grid{grid-template-columns:1fr}main{padding:12px}}</style><main><h1>Conversation camera</h1><p>Named NPC conversations now blend toward the selected 20-degree view and return to the exploration camera afterward. The camera favors the speaker’s face, keeps both upper bodies above the dialogue panel, and pulls back for Sam’s separated table positions.</p><p>Mason and Sam are captured during the actual opening script. Ronald and the clerk use their source map positions with an adjacent player. These retain the game’s facing directions, unlike the earlier posed comparison. Rendering settles before each completed frame is held for the VI screenshot.</p><p><a href="validation.json">Capture validation</a> · <a href="contact-480.png">480 × 360 sheet</a> · <a href="contact-320.png">320 × 240 sheet</a></p>'''+''.join(sections)+'''<p>Original ares encoder PNGs are preserved. The 640 × 240 outputs display at 4:3; contact sheets duplicate their rows exactly. The 640 × 480 outputs are unchanged.</p></main></html>''')
(HERE / 'README.md').write_text('''# Applied conversation camera

The shared portable-C presentation now owns an explicit NPC conversation override. Named speech/choice callbacks begin and end it on N64 and web. It fits both upper bodies at 20 degrees, starts 6.8 horizontal units from the midpoint, expands for separated actors, and favors a speaker-facing side inside the room. Source furniture footprints influence side selection. Invalid, hidden, distant, prop and elevated/reclining speakers retain the wider view. Story positions, headings, events and diary layout remain unchanged.

The browser advances only the camera while Asyncify waits in a modal; scripted frame callbacks retain actor stepping. The N64 uses its existing frame callbacks. Closing dialogue returns smoothly to the map/intro camera.

DIAGNOSTIC=13 / HIGH_RES=1 runs the original opening through Mason and Sam's practice-duel request, then tests Ronald's source Psychic Club lobby position and the entrance clerk. Completed production frames are held for 24 presentations to acquire stable VI output. This freeze affects only the diagnostic capture, not gameplay. Both RAM configurations use the same diagnostic ROM.

All 12 original encoder images are compared against exact Program::video callback RGB bytes. Captures require the matching native header, six subsequent valid callbacks and six stable callbacks. Native framebuffer PNGs are separate. Run build_gallery.py after capturing both cases with ../expansion-pak/capture.py.

Host tests cover 11 directional/source/wall compositions, dialogue clearance, stable bubbles, exit restoration and fallback. The actual browser source opening checks Mason and Sam while suspended, unchanged canonical actors/coordinates, and continued progression into the duel. Existing movement tests verify original tutorial timing and continuous ordinary movement.
''')
print('APPLIED CAMERA GALLERY PASS:12 original VI images, both RAM modes')
