#!/usr/bin/env python3
"""Generates the web frontend's assets from the disassembly (standard library only).

    gen_assets.py <dist dir>

Outputs:
  gfx/cards.png          card art sprite sheet: 64x48 tile of card id N at column N % 16, row N // 16,
                         colored with the card's .pal (the CGB palette the game uses)
  gfx/symbols.png        the TX_SYMBOL font (src/gfx/fonts/symbols.png), 8x8 tiles, tile index = SYM_*;
                         row 7 holds the 7 energy symbols (SYM_FIRE..SYM_COLORLESS) in type colors
  gfx/box_messages.png   duel box messages (BOXMSG_*), 80x32 each
  gfx/card_headers.png   TRAINER / ENERGY / POKeMON headers of the large card picture, 64x16 each
  js/gen/consts.js       constants.h (C), wram.h addresses (W), text ids (T) as an ES module
  js/gen/assets.js       card id -> art file name (for reference / debugging)

Card id -> art: CardPointers in src/data/cards.asm gives the card labels in id order, each card's
`gfx <Name>CardGfx` names its graphics label, and src/gfx.asm INCBINs gfx/cards/<file>.2bpp/.pal
under that label.
"""
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from png import read_png, write_png  # noqa: E402

ROOT = os.path.abspath(os.path.join(HERE, '..', '..', '..', '..'))
PORT = os.path.join(ROOT, 'port')
TILE_W, TILE_H, SHEET_COLS = 64, 48, 16

# energy symbol colors (dark, light), in SYM_FIRE..SYM_COLORLESS order
TYPE_COLORS = [
    ((0xb8, 0x20, 0x08), (0xf8, 0x88, 0x40)),  # fire
    ((0x18, 0x78, 0x18), (0x88, 0xd8, 0x50)),  # grass
    ((0xa8, 0x80, 0x00), (0xf8, 0xe0, 0x30)),  # lightning
    ((0x10, 0x48, 0xc0), (0x70, 0xb0, 0xf8)),  # water
    ((0x88, 0x40, 0x10), (0xd8, 0x98, 0x50)),  # fighting
    ((0x70, 0x20, 0x98), (0xc8, 0x88, 0xe8)),  # psychic
    ((0x58, 0x58, 0x58), (0xb8, 0xb8, 0xb8)),  # colorless
]


def card_gfx_files():
    """card id -> file name (without extension) in src/gfx/cards."""
    cards_asm = open(os.path.join(ROOT, 'src', 'data', 'cards.asm'), encoding='utf-8').read()
    m = re.search(r'CardPointers::(.*?)assert_table_length', cards_asm, re.S)
    labels = re.findall(r'dw (\w+)', m.group(1))
    if labels[0] != 'NULL':
        raise SystemExit('unexpected CardPointers layout')
    gfx_of_card = {}
    cur = None
    for line in cards_asm.splitlines():
        lm = re.match(r'^(\w+Card):', line)
        if lm:
            cur = lm.group(1)
            continue
        gm = re.match(r'\s+gfx (\w+)', line)
        if gm and cur and cur not in gfx_of_card:
            gfx_of_card[cur] = gm.group(1)
    gfx_asm = open(os.path.join(ROOT, 'src', 'gfx.asm'), encoding='utf-8').read()
    file_of_gfx = {}
    cur = None
    for line in gfx_asm.splitlines():
        lm = re.match(r'^(\w+)::?\s*$', line)
        if lm:
            cur = lm.group(1)
            continue
        im = re.match(r'\s+INCBIN "gfx/cards/(\w+)\.2bpp"', line)
        if im and cur:
            file_of_gfx[cur] = im.group(1)
    out = {}
    for card_id, label in enumerate(labels):
        if label == 'NULL':
            continue
        out[card_id] = file_of_gfx[gfx_of_card[label]]
    return out


def read_pal(path):
    """CGB palette: 4 little-endian BGR555 colors."""
    data = open(path, 'rb').read()
    cols = []
    for i in range(4):
        v = data[i * 2] | data[i * 2 + 1] << 8
        r, g, b = v & 31, (v >> 5) & 31, (v >> 10) & 31
        cols.append(((r << 3) | (r >> 2), (g << 3) | (g >> 2), (b << 3) | (b >> 2)))
    return cols


def png_shades(w, h, px, pal):
    """indexes of a gray/indexed PNG -> GB shade 0 (white) .. 3 (black)"""
    if pal and len(pal) >= 2 and all(c[0] == c[1] == c[2] for c in pal):
        # grayscale: shade = darkness
        return [[3 - (pal[v][0] * 3 + 127) // 255 for v in row] for row in px]
    return px


def build_card_sheet(out_dir, files):
    n = max(files) + 1
    rows = (n + SHEET_COLS - 1) // SHEET_COLS
    W, H = SHEET_COLS * TILE_W, rows * TILE_H
    sheet = [bytearray(W * 4) for _ in range(H)]
    mismatches = 0
    for card_id, name in files.items():
        w, h, px, plte = read_png(os.path.join(ROOT, 'src', 'gfx', 'cards', name + '.png'))
        if (w, h) != (TILE_W, TILE_H):
            raise SystemExit(f'{name}.png: unexpected size {w}x{h}')
        pal = read_pal(os.path.join(ROOT, 'src', 'gfx', 'cards', name + '.pal'))
        if plte and [tuple(c) for c in plte[:4]] != pal:
            mismatches += 1
        ox, oy = (card_id % SHEET_COLS) * TILE_W, (card_id // SHEET_COLS) * TILE_H
        for y in range(h):
            row = sheet[oy + y]
            for x in range(w):
                r, g, b = pal[px[y][x] & 3]
                o = (ox + x) * 4
                row[o:o + 4] = bytes((r, g, b, 255))
    write_png(os.path.join(out_dir, 'gfx', 'cards.png'), W, H, sheet)
    return mismatches


def build_symbols(out_dir):
    w, h, px, pal = read_png(os.path.join(ROOT, 'src', 'gfx', 'fonts', 'symbols.png'))
    shades = png_shades(w, h, px, pal)
    cols = w // 8
    out_rows = h + 8
    sheet = [bytearray(w * 4) for _ in range(out_rows)]
    gray = {0: None, 1: (0xa8, 0xa8, 0xa8), 2: (0x58, 0x58, 0x58), 3: (0x10, 0x10, 0x10)}
    for y in range(h):
        for x in range(w):
            c = gray[shades[y][x]]
            if c:
                sheet[y][x * 4:x * 4 + 4] = bytes(c + (255,))
    # colored energy symbols in the extra row
    for t in range(7):
        sym = 1 + t
        sx, sy = (sym % cols) * 8, (sym // cols) * 8
        dark, light = TYPE_COLORS[t]
        mid = tuple((a + b) // 2 for a, b in zip(dark, light))
        pal_t = {0: None, 1: light, 2: mid, 3: dark}
        for y in range(8):
            for x in range(8):
                c = pal_t[shades[sy + y][sx + x]]
                if c:
                    sheet[h + y][(t * 8 + x) * 4:(t * 8 + x) * 4 + 4] = bytes(c + (255,))
    write_png(os.path.join(out_dir, 'gfx', 'symbols.png'), w, out_rows, sheet)


def build_gray(src, dst, transparent_white=False):
    w, h, px, pal = read_png(src)
    shades = png_shades(w, h, px, pal)
    ramp = {0: (0xf8, 0xf8, 0xf0), 1: (0xa8, 0xa8, 0xa8), 2: (0x58, 0x58, 0x58), 3: (0x10, 0x10, 0x10)}
    rows = []
    for y in range(h):
        r = bytearray()
        for x in range(w):
            s = shades[y][x]
            if transparent_white and s == 0:
                r += b'\0\0\0\0'
            else:
                r += bytes(ramp[s] + (255,))
        rows.append(r)
    write_png(dst, w, h, rows)


def parse_defines(path, pattern):
    vals = {}
    for line in open(path, encoding='utf-8'):
        m = re.match(pattern, line)
        if m:
            vals[m.group(1)] = int(m.group(2), 16)
    return vals


def build_consts(out_dir):
    gen = os.path.join(PORT, 'src', 'gen')
    consts = parse_defines(os.path.join(gen, 'constants.h'), r'#define (\w+) (0x[0-9a-fA-F]+)\s*$')
    addrs = parse_defines(os.path.join(gen, 'wram.h'), r'#define ADDR_(\w+) (0x[0-9a-fA-F]+)\s*$')
    texts = {}
    for line in open(os.path.join(gen, 'text.h'), encoding='utf-8'):
        m = re.match(r'\s+(\w+) = (0x[0-9a-fA-F]+),', line)
        if m:
            texts[m.group(1)] = int(m.group(2), 16)
    with open(os.path.join(out_dir, 'js', 'gen', 'consts.js'), 'w') as f:
        f.write('// Generated by tools/gen_assets.py from port/src/gen/{constants,wram,text}.h. Do not edit.\n')
        f.write('export const C = ' + json.dumps(consts, separators=(',', ':')) + ';\n')
        f.write('export const W = ' + json.dumps(addrs, separators=(',', ':')) + ';\n')
        f.write('export const T = ' + json.dumps(texts, separators=(',', ':')) + ';\n')
    return len(consts), len(addrs), len(texts)


def npc_duelists(consts):
    """deck id -> {name: NPC name text label, theme: duel music} from src/data/npcs.asm"""
    out = {}
    lines = open(os.path.join(ROOT, 'src', 'data', 'npcs.asm'), encoding='utf-8').read().splitlines()
    for i, line in enumerate(lines):
        m = re.match(r'\s+tx (\w+NPCName)', line)
        if not m or i + 3 >= len(lines):
            continue
        deck = re.match(r'\s+db (\w+_DECK_ID)', lines[i + 2])
        theme = re.match(r'\s+db (MUSIC_\w+)', lines[i + 3])
        if deck and deck.group(1) in consts:
            out.setdefault(consts[deck.group(1)], {
                'name': m.group(1), 'theme': consts.get(theme.group(1), 0) if theme else 0})
    return out


def main():
    out_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, '..', 'dist')
    os.makedirs(os.path.join(out_dir, 'gfx'), exist_ok=True)
    os.makedirs(os.path.join(out_dir, 'js', 'gen'), exist_ok=True)
    files = card_gfx_files()
    mism = build_card_sheet(out_dir, files)
    build_symbols(out_dir)
    duel = os.path.join(ROOT, 'src', 'gfx', 'duel')
    build_gray(os.path.join(duel, 'box_messages.png'), os.path.join(out_dir, 'gfx', 'box_messages.png'))
    build_gray(os.path.join(duel, 'card_headers.png'), os.path.join(out_dir, 'gfx', 'card_headers.png'))
    nc, na, nt = build_consts(out_dir)
    with open(os.path.join(out_dir, 'js', 'gen', 'assets.js'), 'w') as f:
        f.write('// Generated by tools/gen_assets.py. Do not edit.\n')
        f.write(f'export const CARD_SHEET = {{ cols: {SHEET_COLS}, w: {TILE_W}, h: {TILE_H} }};\n')
        consts = parse_defines(os.path.join(PORT, 'src', 'gen', 'constants.h'), r'#define (\w+) (0x[0-9a-fA-F]+)\s*$')
        f.write('// deck id -> the NPC who duels with it (name text label, duel theme)\n')
        f.write('export const NPC_DECKS = ' + json.dumps(npc_duelists(consts), separators=(',', ':')) + ';\n')
        f.write('export const CARD_ART = ' + json.dumps({str(k): v for k, v in sorted(files.items())},
                                                       separators=(',', ':')) + ';\n')
    print(f'   {len(files)} card images ({len(set(files.values()))} files'
          f'{", %d palette mismatches" % mism if mism else ""}), {nc} constants, {na} addresses, {nt} texts')


if __name__ == '__main__':
    main()
