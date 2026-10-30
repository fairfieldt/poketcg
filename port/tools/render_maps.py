#!/usr/bin/env python3
"""Render the maps of Pokemon Trading Card Game (GBC) to PNG, from the ROM.

Everything is read from the built ROM (poketcg.gbc) through the addresses in
poketcg.sym, and decoded the way the engine does it:

  MapHeaders[map]            (engine/overworld/load_map_header.asm)
    -> CGB tilemap id, final CGB palette id
  Tilemaps[tilemap]          (engine/gfx/tilemaps.asm, via GfxTablePointers)
    -> pointer/bank of the tilemap, tileset id
  tilemap data               (data/maps/tilemaps.asm)
    -> width, height, permission ptr, cgb flag, then LZ-compressed rows;
       a CGB row is <width> tile ids followed by <width> attribute bytes
       (LoadTilemap / .Decompress in engine/gfx/load_gfx.asm,
        decompressor in home/decompress.asm)
  Tilesets[tileset]          (engine/gfx/tilesets.asm)
    -> uncompressed 2bpp tiles, copied by LoadTilesetGfx starting at
       VRAM0 tile $80 ($8800), then VRAM1 tile $00 ($9000), VRAM1 tile $80
  Palettes[palette]          (data/palette_pointers.asm, palettes1.asm)
    -> 8 CGB BG palettes (LoadBGPalette)
  MapOWFramesetPointers[map] (data/map_ow_framesets.asm)
    -> animated tiles; the first frame is applied as DoMapOWFrame does on load

Output (port/maps/ by default):
  <map>.png                  background of every map (overworld.png for
                             OVERWORLD_MAP, otherwise the map constant in
                             lower case), 1 tile = 8x8 px
  npcs/<map>_npcs.png        same, with the NPC sprites at their start positions
  events/<map>_<event>.png   same, with a map-event tile patch applied
  README.md                  index of the images

Map events (opened doors, switched-on deck machines) are tilemap patches
applied by SetOWMapEvent_SRAMOrVRAM (engine/overworld/map_events.asm); they
are rendered as separate images.

NPCs are drawn from the map's NPC list (MapScripts entry 0,
data/npc_map_data.asm), their NPC header (data/npcs.asm), the sprite gfx
(engine/gfx/sprites.asm) and the first frame of their CGB sprite animation
(data/sprite_animation_pointers.asm, frame tables in data/duel/animations),
placed like LoadNPC / UpdateNPCSpritePosition / DrawSpriteAnimationFrame do,
with the overworld OBJ palettes (PALETTE_OVERWORLD_OAM).

Standard library only.  Usage:

    python3 port/tools/render_maps.py            # writes port/maps/
    python3 port/tools/render_maps.py --check    # also cross-check against
                                                 # the disassembly's raw files
    python3 port/tools/render_maps.py --out DIR
"""
import argparse
import os
import re
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))      # the disassembly
DEFAULT_OUT = os.path.abspath(os.path.join(HERE, '..', 'maps'))

# GfxTablePointers offsets (constants/gfx_constants.asm)
GFXTABLE_TILEMAPS = 0
GFXTABLE_TILESETS = 2
GFXTABLE_SPRITES = 4
GFXTABLE_SPRITE_ANIMATIONS = 6
GFXTABLE_PALETTES = 8

# constants/npc_constants.asm; directions (NORTH, EAST, SOUTH, WEST = 0..3) are
# added to the NPC's base animation (*_NPC_UP, _RIGHT, _DOWN, _LEFT)
NPC_FLAG_DIRECTIONLESS = 1 << 4

DMG_SHADES = [(0xff, 0xff, 0xff), (0xaa, 0xaa, 0xaa), (0x55, 0x55, 0x55), (0x00, 0x00, 0x00)]


# --------------------------------------------------------------------------
# ROM / symbols / constants
# --------------------------------------------------------------------------

class Rom:
    def __init__(self, root):
        self.data = open(os.path.join(root, 'poketcg.gbc'), 'rb').read()
        self.sym = {}
        for line in open(os.path.join(root, 'poketcg.sym')):
            m = re.match(r'^([0-9a-fA-F]{2}):([0-9a-fA-F]{4}) (\S+)$', line.strip())
            if m:
                self.sym[m.group(3)] = (int(m.group(1), 16), int(m.group(2), 16))

    @staticmethod
    def offset(bank, addr):
        return addr if addr < 0x4000 else bank * 0x4000 + (addr - 0x4000)

    def byte(self, bank, addr):
        return self.data[self.offset(bank, addr)]

    def word(self, bank, addr):
        o = self.offset(bank, addr)
        return self.data[o] | self.data[o + 1] << 8

    def read(self, bank, addr, n):
        o = self.offset(bank, addr)
        return self.data[o:o + n]

    def addr(self, name):
        return self.sym[name]


def parse_consts(path):
    """Evaluate the const_def/const/const_skip macros of a constants file.
    Returns {name: value}."""
    out = {}
    value = 0
    for line in open(path):
        line = line.split(';')[0].strip()
        m = re.match(r'const_def(?:\s+(\S+))?', line)
        if m:
            value = int(m.group(1).replace('$', '0x'), 0) if m.group(1) else 0
            continue
        m = re.match(r'const_skip(?:\s+(\S+))?', line)
        if m:
            value += int(m.group(1).replace('$', '0x'), 0) if m.group(1) else 1
            continue
        m = re.match(r'const\s+(\w+)', line)
        if m:
            out[m.group(1)] = value
            value += 1
            continue
        m = re.match(r'DEF\s+(\w+)\s+EQU\s+const_value\b', line, re.I)
        if m:
            out[m.group(1)] = value
    return out


# --------------------------------------------------------------------------
# decompression (home/decompress.asm)
# --------------------------------------------------------------------------

class Decompressor:
    """Port of InitDataDecompression / DecompressData.

    A command byte supplies 8 flags (MSB first).  Flag set: copy the next
    byte literally.  Flag clear: the next byte is an offset into a 256-byte
    ring buffer (which starts empty, writing at $ef); on the first of each
    pair of back-references a length byte follows and its high nybble + 2 is
    the copy length, on the second the low nybble of that same byte is used.
    """

    def __init__(self, rom, bank, addr):
        self.rom = rom
        self.bank = bank
        self.addr = addr
        self.bits_left = 1
        self.command = 0
        self.toggle = 0
        self.lengths = 0
        self.to_repeat = 0
        self.repeat_pos = 0
        self.buf = bytearray(256)
        self.write_pos = 0xef  # LOW(wDecompressionSecondaryBufferStart)

    def _src(self):
        v = self.rom.byte(self.bank, self.addr)
        self.addr += 1
        return v

    def _put(self, v):
        self.buf[self.write_pos] = v
        self.write_pos = (self.write_pos + 1) & 0xff
        return v

    def _repeat(self):
        v = self.buf[self.repeat_pos]
        self.repeat_pos = (self.repeat_pos + 1) & 0xff
        return self._put(v)

    def next(self):
        if self.to_repeat:
            self.to_repeat -= 1
            return self._repeat()
        self.bits_left -= 1
        if self.bits_left == 0:
            self.bits_left = 8
            self.command = self._src()
        bit = self.command & 0x80
        self.command = (self.command << 1) & 0xff
        a = self._src()
        if bit:
            return self._put(a)
        self.repeat_pos = a
        if not self.toggle:
            self.toggle = 1
            self.lengths = self._src()
            n = self.lengths >> 4
        else:
            self.toggle = 0
            n = self.lengths & 0xf
        self.to_repeat = n + 1
        return self._repeat()

    def read(self, n):
        return bytes(self.next() for _ in range(n))


# --------------------------------------------------------------------------
# game data access
# --------------------------------------------------------------------------

class Game:
    def __init__(self, root):
        self.root = root
        self.rom = Rom(root)
        c = os.path.join(root, 'src', 'constants')
        self.consts = {}
        for f in ('map_constants.asm', 'tilemap_constants.asm', 'tileset_constants.asm',
                  'palette_constants.asm', 'npc_constants.asm', 'sprite_constants.asm',
                  'animation_constants.asm'):
            self.consts[f] = parse_consts(os.path.join(c, f))
        self.tilemap_names = {v: n for n, v in self.consts['tilemap_constants.asm'].items() if n.startswith('TILEMAP_')}
        self.tileset_names = {v: n for n, v in self.consts['tileset_constants.asm'].items() if n.startswith('TILESET_')}
        self.palette_names = {v: n for n, v in self.consts['palette_constants.asm'].items() if n.startswith('PALETTE_')}
        nc = self.consts['npc_constants.asm']
        first_npc = nc['NPC_DRMASON']
        self.npc_names = {v: n for n, v in nc.items() if n.startswith('NPC_') and not n.startswith('NPC_DATA_')
                          and not n.startswith('NPC_FLAG') and v >= first_npc}
        self.gfx_bank = self.rom.addr('GfxTablePointers')[0]
        self._tileset_cache = {}

    # GetMapDataPointer + LoadGraphicsPointerFromHL
    def gfx_entry(self, table, index):
        rom = self.rom
        bank, base = rom.addr('GfxTablePointers')
        tbl = rom.word(bank, base + table)
        e = tbl + index * 4
        ptr = rom.word(bank, e)
        b = rom.byte(bank, e + 2) + self.gfx_bank
        extra = rom.byte(bank, e + 3)
        return b, ptr, extra

    def map_header(self, m):
        bank, base = self.rom.addr('MapHeaders')
        h = self.rom.read(bank, base + m * 6, 6)
        return dict(tilemap=h[0], tilemap_cgb=h[1], initial_pal=h[2], sgb_pals=h[3],
                    pal=h[4], song=h[5])

    def load_tilemap(self, tilemap_id):
        """LoadTilemap: returns dict(w, h, tiles, attrs, tileset, cgb)."""
        bank, ptr, tileset = self.gfx_entry(GFXTABLE_TILEMAPS, tilemap_id)
        hdr = self.rom.read(bank, ptr, 5)
        w, h = hdr[0], hdr[1]
        perm = hdr[2] | hdr[3] << 8
        cgb = hdr[4]
        d = Decompressor(self.rom, bank, ptr + 5)
        row_w = w * 2 if cgb else w
        tiles, attrs, raw = [], [], b''
        for _ in range(h):
            row = d.read(row_w)
            raw += row
            tiles.append(list(row[:w]))
            attrs.append(list(row[w:2 * w]) if cgb else [0] * w)
        return dict(w=w, h=h, tiles=tiles, attrs=attrs, tileset=tileset, cgb=cgb,
                    perm=perm, raw=raw, bank=bank, ptr=ptr)

    def load_tileset_vram(self, tileset_id):
        """LoadTilesetGfx with wVRAMTileOffset = $80, VRAM0 (LoadMapTilesAndPals).
        Returns vram[bank] = bytearray($1800) covering $8000-$97ff."""
        if tileset_id in self._tileset_cache:
            return [bytearray(b) for b in self._tileset_cache[tileset_id]]
        bank, ptr, _ = self.gfx_entry(GFXTABLE_TILESETS, tileset_id)
        count = self.rom.word(bank, ptr)
        data = self.rom.read(bank, ptr + 2, count * 16)
        vram = [bytearray(0x1800), bytearray(0x1800)]
        # sequence of (VRAM bank, start address) slots of $80 tiles each,
        # as visited by .CopyGfxData when starting at tile offset $80 in VRAM0
        slots = [(0, 0x8800), (1, 0x9000), (1, 0x8800)]
        pos = 0
        for vb, start in slots:
            n = min(0x80, count - pos)
            if n <= 0:
                break
            vram[vb][start - 0x8000:start - 0x8000 + n * 16] = data[pos * 16:(pos + n) * 16]
            pos += n
        self._tileset_cache[tileset_id] = vram
        return [bytearray(b) for b in vram]

    def ow_frameset(self, m):
        """CGB entry of MapOWFramesetPointers[m] (DoMapOWFrame).  Returns the
        list of non-empty subgroups, each a list of frame groups, each a list
        of (vram tile, vram bank, rom bank, rom address of the 16-byte tile)."""
        rom = self.rom
        bank, base = rom.addr('MapOWFramesetPointers')
        fs = rom.word(bank, base + m * 4 + 2)
        subgroups = []
        for c in range(3):  # NUM_OW_FRAMESET_SUBGROUPS
            p = fs + rom.byte(bank, fs + c)
            if rom.byte(bank, p) == 0xff:
                continue
            groups = []
            while rom.byte(bank, p) != 0xff:
                group = []
                while True:  # frames with duration 0 go with the previous one
                    f = rom.read(bank, p, 8)  # OW_FRAME_STRUCT
                    src_bank = f[3] + bank
                    src = (f[4] | f[5] << 8) + (f[6] | f[7] << 8) * 16
                    group.append((f[1], f[2], src_bank, src))
                    p += 8
                    if rom.byte(bank, p) != 0:
                        break
                groups.append(group)
            subgroups.append(groups)
        return subgroups

    def map_vram(self, m, tileset_id):
        """VRAM as the map is shown after LoadMap: the tileset, then the first
        frame of each OW frameset subgroup (ProcessOWFrameset ->
        LoadOWFrameTiles runs once right after the map is loaded)."""
        vram = self.load_tileset_vram(tileset_id)
        subgroups = self.ow_frameset(m)
        for groups in subgroups:
            for vtile, vbank, src_bank, src in groups[0]:
                # .LoadTile: v0Tiles1/v1Tiles1 + (tile xor $80) * 16
                o = 0x8800 + (vtile ^ 0x80) * 16 - 0x8000
                vram[vbank & 1][o:o + 16] = self.rom.read(src_bank, src, 16)
        return vram, subgroups

    def palette_ptr(self, pal_id):
        """Palettes[pal_id] (LoadPaletteDataToBuffer): bank and address of the data."""
        bank, ptr, _size = self.gfx_entry(GFXTABLE_PALETTES, pal_id)
        return bank, ptr

    def bg_palettes(self, pal_ids):
        """LoadBGPalette for each id in order, at BG palette index 0 (wd291 = 0).
        Data: [has BGP][BGP]? [number of CGB palettes][8 bytes each].
        Returns (8 palettes of 4 RGB colours, DMG BGP or None, CGB palettes loaded)."""
        pals = [[(0, 0, 0)] * 4 for _ in range(8)]
        bgp, ncgb = None, 0
        for pid in pal_ids:
            bank, p = self.palette_ptr(pid)
            if self.rom.byte(bank, p):
                bgp = self.rom.byte(bank, p + 1)
                p += 2
            else:
                p += 1
            n = self.rom.byte(bank, p)
            p += 1
            for i in range(min(n, 8)):
                pals[i] = [rgb555(self.rom.word(bank, p + i * 8 + j * 2)) for j in range(4)]
            ncgb += n
        return pals, bgp, ncgb

    def obj_palettes(self, pal_id):
        """LoadOBPalette at OBJ palette index 0.
        Data: [number of OBP bytes][OBP0][OBP1]? [number of CGB palettes][8 bytes each]."""
        bank, p = self.palette_ptr(pal_id)
        nobp = self.rom.byte(bank, p)
        p += 1 + nobp
        n = self.rom.byte(bank, p)
        p += 1
        return [[rgb555(self.rom.word(bank, p + i * 8 + j * 2)) for j in range(4)] for i in range(min(n, 8))]

    # ---- NPCs ----

    def map_npc_list(self, m):
        bank, base = self.rom.addr('MapScripts')
        ptr = self.rom.word(bank, base + m * 16)  # MAP_SCRIPT_NPCS
        out = []
        if not ptr:
            return out
        while True:
            e = self.rom.read(bank, ptr, 6)
            if e[0] == 0:
                break
            out.append(dict(npc=e[0], x=e[1], y=e[2], dir=e[3], preload=e[4] | e[5] << 8))
            ptr += 6
        return out

    def npc_header(self, npc):
        bank, base = self.rom.addr('NPCHeaderPointers')
        p = self.rom.word(bank, base + npc * 2)
        h = self.rom.read(bank, p, 5)
        return dict(id=h[0], sprite=h[1], anim=h[2], anim_cgb=h[3], flags=h[4])

    def sprite_tiles(self, sprite_id):
        bank, ptr, ntiles = self.gfx_entry(GFXTABLE_SPRITES, sprite_id)
        return [self.rom.read(bank, ptr + 2 + i * 16, 16) for i in range(ntiles)]

    def anim_first_frame(self, anim_id):
        """First frame of a sprite animation: list of (dy, dx, tile, attr)."""
        rom = self.rom
        bank, ptr, _ = self.gfx_entry(GFXTABLE_SPRITE_ANIMATIONS, anim_id)
        frame_bank = rom.byte(bank, ptr) + rom.addr('SpriteNullAnimationPointer')[0]
        frame_table = rom.word(bank, ptr + 1)
        frame = rom.byte(bank, ptr + 3)   # first frame_data: frame index
        if frame == 0xff:
            return []
        fp = rom.word(frame_bank, frame_table + frame * 2)
        n = rom.byte(frame_bank, fp)
        out = []
        for i in range(n):
            y, x, t, a = rom.read(frame_bank, fp + 1 + i * 4, 4)
            out.append((s8(y), s8(x), t, a))
        return out


def s8(v):
    return v - 256 if v >= 128 else v


def rgb555(c):
    r, g, b = c & 31, (c >> 5) & 31, (c >> 10) & 31
    return ((r << 3) | (r >> 2), (g << 3) | (g >> 2), (b << 3) | (b >> 2))


def decode_tile(data16):
    """2bpp tile -> 8 rows of 8 colour indices."""
    rows = []
    for y in range(8):
        lo, hi = data16[y * 2], data16[y * 2 + 1]
        rows.append([((lo >> (7 - x)) & 1) | (((hi >> (7 - x)) & 1) << 1) for x in range(8)])
    return rows


def bg_tile_data(vram, bank, tile_id):
    """BG tile data in $8800 ("signed") addressing mode, LCDC bit 4 = 0."""
    addr = 0x9000 + tile_id * 16 if tile_id < 0x80 else 0x8800 + (tile_id - 0x80) * 16
    o = addr - 0x8000
    return vram[bank][o:o + 16]


# --------------------------------------------------------------------------
# image
# --------------------------------------------------------------------------

class Image:
    def __init__(self, w, h):
        self.w, self.h = w, h
        self.px = [[(0, 0, 0)] * w for _ in range(h)]
        self.bgidx = [[0] * w for _ in range(h)]   # BG colour index (for OBJ priority)

    def save(self, path):
        raw = bytearray()
        for row in self.px:
            raw.append(0)
            for r, g, b in row:
                raw += bytes((r, g, b))

        def chunk(t, body):
            return struct.pack('>I', len(body)) + t + body + struct.pack('>I', zlib.crc32(t + body) & 0xffffffff)

        out = b'\x89PNG\r\n\x1a\n'
        out += chunk(b'IHDR', struct.pack('>IIBBBBB', self.w, self.h, 8, 2, 0, 0, 0))
        out += chunk(b'IDAT', zlib.compress(bytes(raw), 9))
        out += chunk(b'IEND', b'')
        with open(path, 'wb') as f:
            f.write(out)


def apply_tilemap_patch(tm, patch, x0, y0):
    """LoadTilemap at coordinates (x0, y0) on top of an existing tilemap."""
    for y in range(patch['h']):
        for x in range(patch['w']):
            if 0 <= y0 + y < tm['h'] and 0 <= x0 + x < tm['w']:
                tm['tiles'][y0 + y][x0 + x] = patch['tiles'][y][x]
                tm['attrs'][y0 + y][x0 + x] = patch['attrs'][y][x]


def render_bg(tm, vram, pals, bgp):
    """Draw the BG tilemap.  pals = CGB BG palettes, or None for DMG shades."""
    img = Image(tm['w'] * 8, tm['h'] * 8)
    cache = {}
    use_cgb = tm['cgb'] and pals is not None
    for ty in range(tm['h']):
        for tx in range(tm['w']):
            t = tm['tiles'][ty][tx]
            a = tm['attrs'][ty][tx] if use_cgb else 0
            bank = (a >> 3) & 1
            key = (t, bank)
            if key not in cache:
                cache[key] = decode_tile(bg_tile_data(vram, bank, t))
            tile = cache[key]
            if use_cgb:
                colours = pals[a & 7]
            else:
                b = bgp if bgp is not None else 0xe4
                colours = [DMG_SHADES[(b >> (2 * i)) & 3] for i in range(4)]
            xflip, yflip = a & 0x20, a & 0x40
            for y in range(8):
                src = tile[7 - y if yflip else y]
                prow = img.px[ty * 8 + y]
                irow = img.bgidx[ty * 8 + y]
                for x in range(8):
                    ci = src[7 - x if xflip else x]
                    prow[tx * 8 + x] = colours[ci]
                    irow[tx * 8 + x] = ci
    return img


def draw_sprite(img, tiles, frame, ox, oy, obj_pals):
    """DrawSpriteAnimationFrame with sprite attributes 0: each OAM entry is
    (dy, dx, tile, attr) relative to the sprite's top-left pixel."""
    # lower OAM index wins: draw in reverse so the first entry ends up on top
    for dy, dx, t, a in reversed(frame):
        if t >= len(tiles):
            continue
        tile = decode_tile(tiles[t])
        colours = obj_pals[a & 7]
        xflip, yflip, prio = a & 0x20, a & 0x40, a & 0x80
        for y in range(8):
            py = oy + dy + y
            if not 0 <= py < img.h:
                continue
            src = tile[7 - y if yflip else y]
            for x in range(8):
                px = ox + dx + x
                if not 0 <= px < img.w:
                    continue
                ci = src[7 - x if xflip else x]
                if ci == 0:
                    continue
                if prio and img.bgidx[py][px] != 0:
                    continue
                img.px[py][px] = colours[ci]


# --------------------------------------------------------------------------
# what to render
# --------------------------------------------------------------------------

READABLE = {
    'OVERWORLD_MAP': 'Overworld map (TCG Island)',
    'MASON_LABORATORY': "Mason Laboratory (Dr. Mason's lab, main room)",
    'DECK_MACHINE_ROOM': 'Mason Laboratory: Deck Machine room',
    'ISHIHARAS_HOUSE': "Ishihara's House",
    'FIGHTING_CLUB_ENTRANCE': 'Fighting Club: entrance',
    'FIGHTING_CLUB_LOBBY': 'Fighting Club: lobby',
    'FIGHTING_CLUB': 'Fighting Club: club room',
    'ROCK_CLUB_ENTRANCE': 'Rock Club: entrance',
    'ROCK_CLUB_LOBBY': 'Rock Club: lobby',
    'ROCK_CLUB': 'Rock Club: club room',
    'WATER_CLUB_ENTRANCE': 'Water Club: entrance',
    'WATER_CLUB_LOBBY': 'Water Club: lobby',
    'WATER_CLUB': 'Water Club: club room',
    'LIGHTNING_CLUB_ENTRANCE': 'Lightning Club: entrance',
    'LIGHTNING_CLUB_LOBBY': 'Lightning Club: lobby',
    'LIGHTNING_CLUB': 'Lightning Club: club room',
    'GRASS_CLUB_ENTRANCE': 'Grass Club: entrance',
    'GRASS_CLUB_LOBBY': 'Grass Club: lobby',
    'GRASS_CLUB': 'Grass Club: club room',
    'PSYCHIC_CLUB_ENTRANCE': 'Psychic Club: entrance',
    'PSYCHIC_CLUB_LOBBY': 'Psychic Club: lobby',
    'PSYCHIC_CLUB': 'Psychic Club: club room',
    'SCIENCE_CLUB_ENTRANCE': 'Science Club: entrance',
    'SCIENCE_CLUB_LOBBY': 'Science Club: lobby',
    'SCIENCE_CLUB': 'Science Club: club room',
    'FIRE_CLUB_ENTRANCE': 'Fire Club: entrance',
    'FIRE_CLUB_LOBBY': 'Fire Club: lobby',
    'FIRE_CLUB': 'Fire Club: club room',
    'CHALLENGE_HALL_ENTRANCE': 'Challenge Hall: entrance',
    'CHALLENGE_HALL_LOBBY': 'Challenge Hall: lobby',
    'CHALLENGE_HALL': 'Challenge Hall: hall (Challenge Cup)',
    'POKEMON_DOME_ENTRANCE': 'Pokémon Dome: entrance',
    'POKEMON_DOME': 'Pokémon Dome: Grand Masters hall',
    'HALL_OF_HONOR': 'Pokémon Dome: Hall of Honor',
}

# Map event -> map whose MAP_SCRIPT_CLOSE_TEXTBOX applies it
# (scripts/mason_laboratory.asm, deck_machine_room.asm,
#  pokemon_dome_entrance.asm, pokemon_dome.asm)
EVENT_VARIANTS = [
    ('MASON_LABORATORY', 'challenge_machine', ['MAP_EVENT_CHALLENGE_MACHINE'],
     'Challenge Machine installed (MAP_EVENT_CHALLENGE_MACHINE, after receiving the legendary cards)'),
    ('DECK_MACHINE_ROOM', 'all_deck_machines', [
        'MAP_EVENT_FIGHTING_DECK_MACHINE', 'MAP_EVENT_ROCK_DECK_MACHINE',
        'MAP_EVENT_WATER_DECK_MACHINE', 'MAP_EVENT_LIGHTNING_DECK_MACHINE',
        'MAP_EVENT_GRASS_DECK_MACHINE', 'MAP_EVENT_PSYCHIC_DECK_MACHINE',
        'MAP_EVENT_SCIENCE_DECK_MACHINE', 'MAP_EVENT_FIRE_DECK_MACHINE'],
     'all eight club Deck Machines switched on (MAP_EVENT_*_DECK_MACHINE, one per medal)'),
    ('POKEMON_DOME_ENTRANCE', 'door_open', ['MAP_EVENT_POKEMON_DOME_DOOR'],
     'Pokémon Dome door open (MAP_EVENT_POKEMON_DOME_DOOR)'),
    ('POKEMON_DOME', 'hall_of_honor_door_open', ['MAP_EVENT_HALL_OF_HONOR_DOOR'],
     'Hall of Honor door open (MAP_EVENT_HALL_OF_HONOR_DOOR)'),
]


def map_event_patch(game, event_id):
    """SetOWMapEvent_SRAMOrVRAM.TilemapPointers entry: x, y, gb tilemap, cgb tilemap."""
    bank, base = game.rom.addr('SetOWMapEvent_SRAMOrVRAM.TilemapPointers')
    p = game.rom.word(bank, base + event_id * 2)
    x, y, _gb, cgb = game.rom.read(bank, p, 4)
    return x, y, cgb


def map_file_name(const):
    return 'overworld' if const == 'OVERWORLD_MAP' else const.lower()


def build_map(game, m):
    """Background of map m on a CGB: LoadMapHeader (CGB tilemap if it has
    one), LoadMapTilesAndPals (tileset, default palette then map palette),
    DoMapOWFrame (first animation frame)."""
    hdr = game.map_header(m)
    tilemap_id = hdr['tilemap_cgb'] or hdr['tilemap']
    tm = game.load_tilemap(tilemap_id)
    pal_ids = [hdr['initial_pal']] + ([hdr['pal']] if hdr['pal'] else [])
    pals, bgp, ncgb = game.bg_palettes(pal_ids)
    vram, ow = game.map_vram(m, tm['tileset'])
    return dict(hdr=hdr, tilemap_id=tilemap_id, tm=tm, vram=vram, animated=bool(ow),
                pals=pals if ncgb else None, bgp=bgp)


def npc_sprites_for_map(game, m, tm):
    """Returns (drawn, skipped): lists of dicts describing NPCs."""
    drawn, skipped = [], []
    for e in game.map_npc_list(m):
        name = game.npc_names.get(e['npc'], '$%02x' % e['npc'])
        info = dict(e, name=name, conditional=bool(e['preload']))
        if e['npc'] == 0xff:
            info['why'] = 'chosen at run time by its pre-load function'
            skipped.append(info)
            continue
        if e['x'] >= tm['w'] or e['y'] >= tm['h']:
            info['why'] = 'starts outside the map, at (%d,%d)' % (s8(e['x']), s8(e['y']))
            skipped.append(info)
            continue
        h = game.npc_header(e['npc'])
        anim = h['anim_cgb']
        if not h['flags'] & NPC_FLAG_DIRECTIONLESS:
            anim += e['dir']
        info.update(sprite=h['sprite'], anim=anim)
        drawn.append(info)
    return drawn, skipped


def render_all(out_dir, check=False, verbose=True):
    game = Game(ROOT)
    os.makedirs(out_dir, exist_ok=True)
    os.makedirs(os.path.join(out_dir, 'npcs'), exist_ok=True)
    os.makedirs(os.path.join(out_dir, 'events'), exist_ok=True)
    mapc = game.consts['map_constants.asm']
    map_const = {}
    for n, v in mapc.items():
        if n in READABLE:
            map_const[v] = n
    nmaps = mapc['NUM_MAPS']
    obj_pals = game.obj_palettes(game.consts['palette_constants.asm']['PALETTE_OVERWORLD_OAM'])
    anim_c = game.consts['animation_constants.asm']
    sprite_c = game.consts['sprite_constants.asm']
    ev_c = mapc
    sprite_cache = {}
    records = []
    problems = []

    for m in range(nmaps):
        const = map_const[m]
        bm = build_map(game, m)
        hdr, tilemap_id, tm, vram, pals, bgp = (bm['hdr'], bm['tilemap_id'], bm['tm'], bm['vram'],
                                                bm['pals'], bm['bgp'])
        if check:
            problems += check_tilemap(game, tilemap_id, tm)
        img = render_bg(tm, vram, pals, bgp)
        fname = map_file_name(const) + '.png'
        img.save(os.path.join(out_dir, fname))
        rec = dict(m=m, const=const, file=fname, w=tm['w'], h=tm['h'],
                   tilemap=game.tilemap_names.get(tilemap_id, tilemap_id),
                   tileset=game.tileset_names.get(tm['tileset'], tm['tileset']),
                   palette=game.palette_names.get(hdr['pal'], hdr['pal']),
                   cgb=pals is not None, animated=bm['animated'],
                   npcs_file=None, drawn=[], skipped=[], events=[])

        # sprites
        if m == mapc['OVERWORLD_MAP']:
            # OverworldMap_InitVolcanoSprite: SPRITE_OW_MAP_OAM at OAM ($80, $10)
            simg = render_bg(tm, vram, pals, bgp)
            sid = sprite_c['SPRITE_OW_MAP_OAM']
            frame = game.anim_first_frame(anim_c['SPRITE_ANIM_CGB_VOLCANO_SMOKE'])
            draw_sprite(simg, game.sprite_tiles(sid), frame, 0x80 - 8, 0x10 - 16, obj_pals)
            nf = 'overworld_volcano_smoke.png'
            simg.save(os.path.join(out_dir, 'npcs', nf))
            rec['npcs_file'] = 'npcs/' + nf
            rec['drawn'] = [dict(name='volcano smoke (SPRITE_OW_MAP_OAM, SPRITE_ANIM_CGB_VOLCANO_SMOKE)',
                                 x=0x78 // 8, y=0, conditional=False)]
        else:
            drawn, skipped = npc_sprites_for_map(game, m, tm)
            rec['skipped'] = skipped
            if drawn:
                simg = render_bg(tm, vram, pals, bgp)
                # later NPCs get later OAM slots, so draw them first
                for n in reversed(drawn):
                    if n['sprite'] not in sprite_cache:
                        sprite_cache[n['sprite']] = game.sprite_tiles(n['sprite'])
                    frame = game.anim_first_frame(n['anim'])
                    draw_sprite(simg, sprite_cache[n['sprite']], frame, n['x'] * 8, n['y'] * 8, obj_pals)
                nf = map_file_name(const) + '_npcs.png'
                simg.save(os.path.join(out_dir, 'npcs', nf))
                rec['npcs_file'] = 'npcs/' + nf
                rec['drawn'], rec['skipped'] = drawn, skipped

        # map events
        for ev_map, suffix, events, desc in EVENT_VARIANTS:
            if ev_map != const:
                continue
            etm = dict(tm, tiles=[r[:] for r in tm['tiles']], attrs=[r[:] for r in tm['attrs']])
            for ev in events:
                x, y, cgb_tilemap = map_event_patch(game, ev_c[ev])
                patch = game.load_tilemap(cgb_tilemap)
                if check:
                    problems += check_tilemap(game, cgb_tilemap, patch)
                apply_tilemap_patch(etm, patch, x, y)
            eimg = render_bg(etm, vram, pals, bgp)
            ef = map_file_name(const) + '_' + suffix + '.png'
            eimg.save(os.path.join(out_dir, 'events', ef))
            rec['events'].append(dict(file='events/' + ef, desc=desc))
        records.append(rec)
        if verbose:
            print('%-24s %2dx%-2d -> %s' % (const, tm['w'], tm['h'], fname))

    write_readme(out_dir, records, game)
    if check:
        if problems:
            print('CHECK FAILED:')
            for p in problems:
                print('  ' + p)
            return 1
        print('check: all decoded tilemaps match the disassembly\'s uncompressed .bgmap files,'
              ' and all tilesets match their .2bpp files')
    return 0


# --------------------------------------------------------------------------
# cross-checks against the disassembly's uncompressed source files
# --------------------------------------------------------------------------

def tilemap_source_file(game, tilemap_id):
    """Find the INCBIN'd .lz of a tilemap label in data/maps/tilemaps.asm."""
    bank, ptr, _ = game.gfx_entry(GFXTABLE_TILEMAPS, tilemap_id)
    label = None
    for n, (b, a) in game.rom.sym.items():
        if b == bank and a == ptr and '.' not in n:
            label = n
    if label is None:
        return None, None
    for f in ('data/maps/tilemaps.asm', 'gfx.asm'):
        src = open(os.path.join(game.root, 'src', f)).read()
        m = re.search(re.escape(label) + r'::\s*\n(?:.*\n){3}\s*INCBIN "([^"]+)\.lz"', src)
        if m:
            return label, os.path.join(game.root, 'src', m.group(1))
    return label, None


def check_tilemap(game, tilemap_id, tm):
    problems = []
    label, path = tilemap_source_file(game, tilemap_id)
    if not path or not os.path.exists(path):
        problems.append('%s: no uncompressed source file found' % label)
        return problems
    ref = open(path, 'rb').read()
    if ref != tm['raw']:
        problems.append('%s: decoded tilemap differs from %s' % (label, os.path.relpath(path, game.root)))
    # tileset gfx vs its .2bpp file
    bank, ptr, ntiles = game.gfx_entry(GFXTABLE_TILESETS, tm['tileset'])
    count = game.rom.word(bank, ptr)
    tlabel = [n for n, (b, a) in game.rom.sym.items() if b == bank and a == ptr and '.' not in n]
    gfx = open(os.path.join(game.root, 'src', 'gfx.asm')).read()
    for tl in tlabel:
        mm = re.search(re.escape(tl) + r'::\s*\n\s*dw (\d+)\s*\n\s*INCBIN "([^"]+)"\s*\n', gfx)
        if mm:
            f = os.path.join(game.root, 'src', mm.group(2))
            if os.path.exists(f):
                if open(f, 'rb').read()[:count * 16] != game.rom.read(bank, ptr + 2, count * 16):
                    problems.append('%s: ROM tiles differ from %s' % (tl, mm.group(2)))
            if int(mm.group(1)) != count or ntiles != count:
                problems.append('%s: tile count mismatch' % tl)
    return problems


# --------------------------------------------------------------------------
# README
# --------------------------------------------------------------------------

def write_readme(out_dir, records, game):
    L = []
    L.append('# Pokémon Trading Card Game (GBC) map renders')
    L.append('')
    L.append('Rendered from the original ROM data (`poketcg.gbc` + `poketcg.sym`) by')
    L.append('[`port/tools/render_maps.py`](../tools/render_maps.py). Every image is at native')
    L.append('resolution: one map tile = 8x8 pixels, no scaling. Rerun with')
    L.append('`python3 port/tools/render_maps.py` (add `--check` to cross-check the decoded')
    L.append('tilemaps and tilesets against the disassembly\'s uncompressed files).')
    L.append('')
    L.append('## Maps')
    L.append('')
    L.append('One image per entry of the map list (`constants/map_constants.asm`); every one')
    L.append('of them is reachable (`data/warps.asm`, `data/overworld_map/overworld_warps.asm`).')
    L.append('')
    L.append('| Map constant | Name | Image | Size (tiles) | Tilemap / tileset / palette |')
    L.append('|---|---|---|---|---|')
    for r in records:
        L.append('| `%s` | %s | [%s](%s) | %dx%d | `%s` / `%s` / `%s` |' % (
            r['const'], READABLE[r['const']], r['file'], r['file'], r['w'], r['h'],
            r['tilemap'], r['tileset'], r['palette']))
    L.append('')
    L.append('Notes:')
    L.append('')
    L.append('* **Overworld.** The overworld (`OVERWORLD_MAP`) is not a scrolling map in this')
    L.append('  game: it is a single-screen map of the island (20x18 tiles = 160x144 px) on which')
    L.append('  the player picks a destination, so `overworld.png` is the whole of it. In game the')
    L.append('  name of the selected place is printed in the empty box at the top left')
    L.append('  (`OverworldMap_PrintMapName`), the player sprite walks along the paths and smoke')
    L.append('  rises from the volcano. The text and the player are not drawn; the volcano smoke')
    L.append('  (an OBJ sprite) is drawn in')
    L.append('  [npcs/overworld_volcano_smoke.png](npcs/overworld_volcano_smoke.png).')
    L.append('* **Club lobbies.** All eight club lobbies and the Challenge Hall lobby use the same')
    L.append('  tilemap (`TILEMAP_CLUB_LOBBY_CGB`) and palette (`PALETTE_CLUB_LOBBY`), so their')
    L.append('  background images are identical; they differ only by NPCs (see below). The')
    L.append('  **Card Pop! / Gift Center** is not a separate map: it is the counter at the top of')
    L.append('  every lobby (`NPC_CLERK10` and `NPC_GIFT_CENTER_CLERK` stand behind it).')
    L.append('* The Challenge Hall entrance uses the Fighting Club entrance palette')
    L.append('  (`PALETTE_FIGHTING_CLUB_ENTRANCE`), as in `data/map_headers.asm`.')
    L.append('* `OWMAP_MYSTERY_HOUSE` is only a name ("Mystery House") the overworld shows for')
    L.append('  Ishihara\'s House before you have heard of it; it is the same map.')
    L.append('')
    L.append('## Map event variants')
    L.append('')
    L.append('Some maps have tile patches that the game applies when an event flag is set')
    L.append('(`SetOWMapEvent_SRAMOrVRAM` in `engine/overworld/map_events.asm`, applied from')
    L.append('each map\'s close-text-box script). The base images above show the maps with no')
    L.append('event applied; these show them with the patch:')
    L.append('')
    L.append('| Map | Variant | Image |')
    L.append('|---|---|---|')
    for r in records:
        for e in r['events']:
            L.append('| `%s` | %s | [%s](%s) |' % (r['const'], e['desc'], e['file'], e['file']))
    L.append('')
    L.append('## NPCs')
    L.append('')
    L.append('`npcs/` holds the same backgrounds with the NPCs drawn at their start positions')
    L.append('(`data/npc_map_data.asm`), facing their start direction, using the first frame of')
    L.append('their CGB sprite animation and the overworld OBJ palettes (`PALETTE_OVERWORLD_OAM`).')
    L.append('Every NPC of the map\'s list that starts on the map is drawn, **including the ones')
    L.append('whose pre-load function only loads them in some story states** (marked *cond.*');
    L.append('below), so an image can show NPCs that never appear together (e.g. Ronald, Imakuni,')
    L.append('the Challenge Hall crowd, the Grand Masters). Scripted NPCs are drawn where their')
    L.append('entry puts them even when a script walks them in from there (Ronald in')
    L.append('`POKEMON_DOME` starts inside the top-right wall). The player is not drawn.')
    L.append('')
    L.append('| Map | Image | NPCs drawn (tile x,y) | Not drawn |')
    L.append('|---|---|---|---|')
    for r in records:
        if not r['npcs_file'] and not r['skipped']:
            continue
        drawn = ', '.join('`%s` (%d,%d)%s' % (n['name'], n['x'], n['y'], ' *cond.*' if n['conditional'] else '')
                          for n in r['drawn']) or '-'
        skipped = ', '.join('`%s`: %s' % (n['name'], n['why']) for n in r['skipped']) or '-'
        link = '[%s](%s)' % (r['npcs_file'], r['npcs_file']) if r['npcs_file'] else '(none: no NPC on the map)'
        L.append('| `%s` | %s | %s | %s |' % (r['const'], link, drawn, skipped))
    L.append('')
    L.append('## How it is rendered')
    L.append('')
    L.append('* **Map format.** `MapHeaders` (`data/map_headers.asm`) gives each map a DMG and a')
    L.append('  CGB tilemap and a CGB palette. A tilemap (`data/maps/tilemaps.asm`) is a 5-byte')
    L.append('  header (width, height in 8x8 tiles, pointer to the 16x16 permission map, CGB flag)')
    L.append('  followed by LZ-compressed rows; each CGB row is `width` tile ids then `width` CGB')
    L.append('  attribute bytes. There are no 2x2 metatiles: the map is stored tile by tile.')
    L.append('  The decompressor (`home/decompress.asm`) is ported as-is.')
    L.append('* **Tiles.** The tileset named by the tilemap (`engine/gfx/tilesets.asm`) is plain')
    L.append('  2bpp; `LoadTilesetGfx` copies its first 128 tiles to VRAM0 tiles $80-$FF, the next')
    L.append('  128 to VRAM1 tiles $00-$7F and the rest to VRAM1 $80-$FF, and the BG uses $8800')
    L.append('  addressing; the attribute byte\'s bank bit selects the VRAM bank.')
    L.append('* **Colours.** All maps are rendered in CGB colours: the attribute byte selects one of')
    L.append('  the 8 BG palettes of the map\'s palette (`data/palettes1.asm`), loaded over')
    L.append('  `PALETTE_DEFAULT_CGB` as `LoadMapTilesAndPals` does. RGB555 is expanded to 8 bits')
    L.append('  per channel without any LCD colour correction. The attribute flip bits are')
    L.append('  honoured (no map tile actually uses them).')
    dmg = [r['const'] for r in records if not r['cgb']]
    if dmg:
        L.append('* **DMG fallback.** These maps have no CGB tilemap/palette data and are drawn with')
        L.append('  their DMG BGP mapped to a 4-shade grey ramp: %s.' % ', '.join('`%s`' % c for c in dmg))
    else:
        L.append('* Every map has a CGB tilemap and CGB palettes, so none needed the DMG')
        L.append('  grey-ramp fallback that the script has for that case.')
    L.append('* **Animated tiles.** Some maps animate a few BG tiles by copying other tileset')
    L.append('  tiles into VRAM (`data/map_ow_framesets.asm`, `engine/overworld/animation.asm`):')
    L.append('  %s.' % ', '.join('`%s`' % r['const'] for r in records if r['animated']))
    L.append('  They are drawn as one still frame: the first frame of each animation, which')
    L.append('  `DoMapOWFrame` applies right after the map is loaded (for every map but')
    L.append('  `HALL_OF_HONOR` that is also exactly what the tileset itself holds).')
    L.append('* Sprites use the frame layout of `DrawSpriteAnimationFrame` (OAM entries relative')
    L.append('  to the NPC\'s tile position, colour 0 transparent, OBJ priority honoured).')
    L.append('')
    L.append('## Gallery')
    L.append('')
    for r in records:
        L.append('### %s (`%s`)' % (READABLE[r['const']], r['const']))
        L.append('')
        imgs = ['![%s](%s)' % (r['const'], r['file'])]
        if r['npcs_file']:
            imgs.append('![%s with NPCs](%s)' % (r['const'], r['npcs_file']))
        imgs += ['![%s: %s](%s)' % (r['const'], e['desc'], e['file']) for e in r['events']]
        L.append(' '.join(imgs))
        L.append('')
    with open(os.path.join(out_dir, 'README.md'), 'w', encoding='utf-8') as f:
        f.write('\n'.join(L))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--out', default=DEFAULT_OUT, help='output directory (default: port/maps)')
    ap.add_argument('--check', action='store_true',
                    help='cross-check decoded tilemaps/tilesets against the disassembly\'s raw files')
    ap.add_argument('-q', '--quiet', action='store_true')
    args = ap.parse_args()
    sys.exit(render_all(args.out, check=args.check, verbose=not args.quiet))


if __name__ == '__main__':
    main()
