#!/usr/bin/env python3
"""Render all NPC header entries and supported CGB facings (requires Pillow).

Usage: python3 port/tools/render_npc_atlas.py [--out IMAGE.png]
Reuses the ROM, palette and OAM decoding in render_maps.py. Sprites are enlarged
4x with nearest-neighbour sampling; each facing uses its first animation frame.
"""
import argparse
import json
import math
import re
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

import render_maps as maps


def font(size):
    for path in ('/System/Library/Fonts/Helvetica.ttc',
                 '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'):
        if Path(path).exists():
            return ImageFont.truetype(path, size)
    return ImageFont.load_default(size=size)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path,
                        default=Path(maps.DEFAULT_OUT) / 'npc_atlas.png')
    args = parser.parse_args()
    game = maps.Game(maps.ROOT)
    source = (Path(maps.ROOT) / 'src/data/npcs.asm').read_text()
    pointers = re.findall(r'^\s*dw (\w+NPCHeader)\s*$',
                          source.split('assert_table_length')[0], re.M)
    texts = '\n'.join(p.read_text() for p in (Path(maps.ROOT) / 'src/text').glob('*.asm'))
    names = dict(re.findall(r'^(\w+):\n\s*text "([^"]+)"', texts, re.M))
    palettes = game.obj_palettes(game.consts['palette_constants.asm']['PALETTE_OVERWORLD_OAM'])
    nc = game.consts['npc_constants.asm']
    ac = game.consts['animation_constants.asm']
    records = []
    for npc in range(1, nc['NUM_NPCS']):
        const = game.npc_names[npc]
        header = game.npc_header(npc)
        block = source.split(pointers[npc] + ':', 1)[1].split('\n\n', 1)[0]
        text = re.search(r'\btx (\w+)', block)
        name = names.get(text[1], '') if text else ''
        if not name:
            name = const.removeprefix('NPC_').replace('_', ' ').title()
        if const in ('NPC_05', 'NPC_06'):
            name = 'Dr. Mason (unused)'
        elif const in ('NPC_6E', 'NPC_6F'):
            name = 'Player (unused)'
        elif const == 'NPC_PLAYER_CREDITS':
            name = 'Player (credits)'
        if name.isupper():
            name = name.title()
        if header['flags'] & maps.NPC_FLAG_DIRECTIONLESS:
            label = 'Fixed'
            if const == 'NPC_AMY':
                label = 'Lying'
            elif const == 'NPC_MURRAY2':
                label = 'West (fixed)'
            facings = [(label, header['anim_cgb'])]
            if const == 'NPC_AMY':
                facings.append(('Standing', ac['SPRITE_ANIM_CGB_AMY_STAND']))
        else:
            facings = [(label, header['anim_cgb'] + direction)
                       for direction, label in enumerate(('North', 'East', 'South', 'West'))]
        records.append(dict(id=npc, constant=const, name=name, header=header,
                            facings=facings))

    cols, cw, ch, margin, top = 5, 296, 148, 24, 90
    width = margin * 2 + cols * cw
    height = top + math.ceil(len(records) / cols) * ch + margin
    atlas = Image.new('RGB', (width, height), '#eef1f5')
    draw = ImageDraw.Draw(atlas)
    title, label_font, name_font, id_font = font(28), font(13), font(17), font(11)
    draw.text((margin, 18), 'Pokémon TCG · NPC atlas', font=title, fill='#172536')
    draw.text((margin, 56), 'All 115 NPC entries · North / East / South / West · Fixed poses shown once · 4× pixels',
              font=font(15), fill='#526173')

    rendered = 0
    for i, rec in enumerate(records):
        x = margin + i % cols * cw
        y = top + i // cols * ch
        draw.rounded_rectangle((x, y, x + cw - 8, y + ch - 8), radius=7, fill='white')
        tiles = game.sprite_tiles(rec['header']['sprite'])
        count = len(rec['facings'])
        for j, (label, anim) in enumerate(rec['facings']):
            cx = x + (cw - 8) * (j + 0.5) / count
            frame = game.anim_first_frame(anim)
            assert frame, (rec['constant'], anim)
            assert all(t < len(tiles) for _, _, t, _ in frame), rec['constant']
            # Tight OAM bounds preserve every tile, including the wider lying pose.
            left = min(dx for dy, dx, t, a in frame)
            upper = min(dy for dy, dx, t, a in frame)
            w = max(dx + 8 for dy, dx, t, a in frame) - left
            h = max(dy + 8 for dy, dx, t, a in frame) - upper
            sprite = maps.Image(w, h)
            sprite.px = [[(255, 255, 255)] * w for _ in range(h)]
            maps.draw_sprite(sprite, tiles, frame, -left, -upper, palettes)
            image = Image.new('RGB', (w, h))
            image.putdata([p for row in sprite.px for p in row])
            image = image.resize((w * 4, h * 4), Image.Resampling.NEAREST)
            atlas.paste(image, (round(cx - w * 2), y + 30 + (64 - h * 4) // 2))
            draw.text((cx, y + 9), label, font=label_font, fill='#526173', anchor='mt')
            rendered += 1
        draw.text((x + (cw - 8) / 2, y + 101), rec['name'],
                  font=name_font, fill='#172536', anchor='mt')
        draw.text((x + (cw - 8) / 2, y + 123), rec['constant'],
                  font=id_font, fill='#697586', anchor='mt')
    args.out.parent.mkdir(parents=True, exist_ok=True)
    atlas.save(args.out)
    args.out.with_suffix('.json').write_text(json.dumps(records, indent=2) + '\n')
    print(f'{args.out}: {len(records)} NPC entries, {rendered} facing/pose images, {width}×{height}')


if __name__ == '__main__':
    main()
