#!/usr/bin/env python3
"""Check retained artwork, matching texel scale, silhouette and native arrays."""
from pathlib import Path
import hashlib
import json
import re
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[3]
ASSET = ROOT / 'assets/ui/card-back'
manifest = json.loads((ASSET / 'manifest.json').read_text())
source = Image.open(ASSET / 'source.jpg').convert('RGBA')
source_sha = hashlib.sha256((ASSET / 'source.jpg').read_bytes()).hexdigest()
assert source_sha == manifest['source']['sha256'] == '7fa2a729bc7d214b50215596a8aace9acc0fb57211c2c15b3f8747810ce10edb'
mask = Image.new('L', source.size, 0)
ImageDraw.Draw(mask).rounded_rectangle((0, 0, source.width-1, source.height-1), radius=36, fill=255)
source.putalpha(mask)

def rgba16(image):
    return [((r>>3)<<11)|((g>>3)<<6)|((b>>3)<<1)|(a>=128)
            for r,g,b,a in (image.getpixel((x,y)) for y in range(image.height) for x in range(image.width))]

def decode(values, size):
    image = Image.new('RGBA', size)
    image.putdata([(((v>>11)&31)*255//31, ((v>>6)&31)*255//31,
                    ((v>>1)&31)*255//31, 255 if v&1 else 0) for v in values])
    return image

header = (ROOT / 'frontends/n64/card_back.h').read_text()
masters = {}
native_bytes = 0
results = []
for name, size, base in [('small',(24,34),None), ('standard',(32,44),None),
                         ('small_hi',(36,51),'small'), ('high',(48,66),'standard')]:
    filtered = source.resize(size, Image.Resampling.LANCZOS)
    expected = filtered if base is None else masters[base].resize(size, Image.Resampling.NEAREST)
    expected.putalpha(filtered.getchannel('A'))
    values = rgba16(expected)
    image = Image.open(ASSET / f'native-{name}.png').convert('RGBA')
    assert image.size == size
    assert image.tobytes() == decode(values, size).tobytes(), f'{name}: changed master texels or mask'
    # Preserve the original target-size silhouette, independent of color detail.
    assert image.getchannel('A').tobytes() == decode(rgba16(filtered), size).getchannel('A').tobytes()
    array = re.search(rf'card_back_{name}\[\d+\] = \{{(.*?)\}};', header, re.S)
    assert array, name
    assert [int(v,16) for v in re.findall(r'0x([0-9a-f]{4})', array[1])] == values, name
    assert f'CARD_BACK_{name.upper()}_W={size[0]}, CARD_BACK_{name.upper()}_H={size[1]}' in header
    masters[name] = image
    native_bytes += len(values)*2
    results.append({'name':name, 'size':size, 'color_master':base or name,
                    'unique_colors':len(set(image.getpixel((x,y)) for y in range(image.height) for x in range(image.width)))})

web = Image.open(ROOT / 'frontends/web/src/img/card-back.png').convert('RGBA')
expected = masters['standard'].resize((256,357), Image.Resampling.NEAREST)
expected.putalpha(source.resize(expected.size,Image.Resampling.LANCZOS).getchannel('A'))
assert web.size == (256,357) and web.tobytes() == expected.tobytes(), 'Web differs from shared color master'
assert native_bytes == manifest['native_total_bytes'] == 14456
for output in manifest['outputs']:
    path = ROOT.parent / output['path']
    assert hashlib.sha256(path.read_bytes()).hexdigest() == output['sha256'], output['path']
print(json.dumps({'passed':True, 'source_unchanged':True, 'native_bytes':native_bytes,
                  'native':results, 'web_size':web.size}, indent=2))
