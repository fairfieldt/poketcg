#!/usr/bin/env python3
"""Generate port/src/gen/wram.h: the Game Boy WRAM/HRAM layout as C macros.

The C port models WRAM ($c000-$dfff) and HRAM ($ff80-$fffe) as flat byte
arrays (gWRAM / gHRAM) so that every variable keeps the exact address, size
and aliasing (UNIONs) it has in the original game. Each label in
src/wram.asm / src/hram.asm becomes a macro:

  size 1         ->  #define wDuelTurns  (gWRAM[0x0c06])            (u8 lvalue)
  size != 1      ->  #define wDuelTempList (&gWRAM[0x0510])         (u8 *)
  known structs  ->  #define wLoadedCard1 (*(struct CardData *)&gWRAM[0x0c24])

Sizes come from parsing the ds directives; addresses come from the linked
poketcg.sym and are cross-checked against the parse.
"""
import os, re, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
SRC = os.path.join(ROOT, 'src')

STRUCT_MACROS = {
    'card_data_struct': 'struct CardData',
    'atk_data_struct': 'struct AtkData',
    'duel_anim_struct': 'struct DuelAnim',
}

def load_consts():
    vals = {}
    for line in open(os.path.join(ROOT, 'port', 'src', 'gen', 'constants.h')):
        m = re.match(r'#define (\w+) (0x[0-9a-f]+)', line)
        if m:
            vals[m.group(1)] = int(m.group(2), 16)
    vals.update({'OAM_SIZE': 0xa0, 'TILE_SIZE': 0x10, 'PAL_SIZE': 8, 'TILE_SIZE_1BPP': 8})
    return vals

def load_sym():
    sym = {}
    for line in open(os.path.join(ROOT, 'poketcg.sym')):
        m = re.match(r'^([0-9a-f]{2}):([0-9a-f]{4}) (\S+)$', line.strip())
        if m:
            sym[m.group(3)] = int(m.group(2), 16)
    return sym

def load_sections():
    secs = {}
    for line in open(os.path.join(ROOT, 'poketcg.map')):
        m = re.match(r'^\s*SECTION: \$([0-9a-f]{4})-\$[0-9a-f]{4} \(\$[0-9a-f]+ bytes\) \["(.*)"\]', line)
        if m:
            secs[m.group(2)] = int(m.group(1), 16)
    return secs

SECTIONS = None

def load_macros():
    macros, cur, body = {}, None, []
    for line in open(os.path.join(SRC, 'macros', 'wram.asm')):
        s = line.split(';')[0].rstrip()
        m = re.match(r'^MACRO\s+(\w+)', s)
        if m:
            cur, body = m.group(1), []
            continue
        if s.strip() == 'ENDM':
            macros[cur] = body
            cur = None
            continue
        if cur:
            body.append(s)
    return macros

def ev(expr, consts):
    expr = expr.strip()
    expr = re.sub(r'\$([0-9a-fA-F]+)', lambda m: str(int(m.group(1), 16)), expr)
    expr = re.sub(r'%([01]+)', lambda m: str(int(m.group(1), 2)), expr)
    m = re.match(r'^(.*)\s+(palettes|tiles|tiles_1bpp)$', expr)
    if m:
        unit = {'palettes': 8, 'tiles': 16, 'tiles_1bpp': 8}[m.group(2)]
        return ev(m.group(1), consts) * unit
    py = re.sub(r'[A-Za-z_]\w*', lambda m: str(consts[m.group(0)]), expr)
    py = py.replace('/', '//')
    return int(eval(py, {}, {}))

def expand(lines, macros, arg=None):
    out = []
    for s in lines:
        if arg is not None:
            s = s.replace('\\1', arg)
        t = s.strip()
        m = re.match(r'^(\w+)\s+(\S+)$', t)
        if m and m.group(1) in macros:
            out.append(('macro', m.group(1), m.group(2)))
            out.extend(expand(macros[m.group(1)], macros, m.group(2)))
            out.append(('endmacro', m.group(1)))
            continue
        # label followed by macro invocation on the same line
        m = re.match(r'^(\w+)::?\s+(\w+)\s+(\S+)$', t)
        if m and m.group(2) in macros:
            out.append(('label', m.group(1)))
            out.append(('macro', m.group(2), m.group(3)))
            out.extend(expand(macros[m.group(2)], macros, m.group(3)))
            out.append(('endmacro', m.group(2)))
            continue
        m = re.match(r'^(\w+)::?\s*(.*)$', t)
        if m and not t.startswith(('ds', 'db', 'dw', 'SECTION', 'UNION', 'NEXTU', 'ENDU', 'INCLUDE')):
            out.append(('label', m.group(1)))
            t = m.group(2).strip()
            if not t:
                continue
        if not t:
            continue
        m = re.match(r'^ds\s+(.+)$', t)
        if m:
            out.append(('ds', m.group(1)))
        elif t.startswith('db'):
            out.append(('ds', str(len(t[2:].split(',')))))
        elif t.startswith('dw'):
            out.append(('ds', str(2 * len(t[2:].split(',')))))
        elif t.startswith('SECTION'):
            out.append(('section', t))
        elif t in ('UNION', 'NEXTU', 'ENDU'):
            out.append((t.lower(),))
        elif t.startswith(('INCLUDE', 'vc_hook', 'IF', 'ENDC', 'ELSE', 'assert', 'ASSERT')):
            pass
        else:
            raise SystemExit(f'unhandled line: {t!r}')
    return out

def parse(path, macros, consts, sym):
    lines = [l.split(';')[0].rstrip() for l in open(path)]
    toks = expand(lines, macros)
    labels = []          # (name, addr, size, ctype)
    addr = None
    pending = []
    union_stack = []     # (start, max_end)
    macro_stack = []
    struct_label = {}
    for tok in toks:
        kind = tok[0]
        if kind == 'section':
            secname = re.match(r'SECTION\s+"([^"]+)"', tok[1]).group(1)
            addr = SECTIONS[secname]
            pending = []
        elif kind == 'label':
            name = tok[1]
            if name in sym:
                if addr is not None and addr != sym[name]:
                    raise SystemExit(f'address mismatch at {name}: parsed {addr:04x} sym {sym[name]:04x}')
                addr = sym[name]
            pending.append(name)
        elif kind == 'macro':
            if tok[1] in STRUCT_MACROS and pending:
                for p in pending:
                    struct_label[p] = STRUCT_MACROS[tok[1]]
            macro_stack.append((tok[1], pending[:], addr))
            pending = []
        elif kind == 'endmacro':
            name, owners, start = macro_stack.pop()
            for o in owners:
                labels.append((o, start, addr - start, STRUCT_MACROS.get(name)))
        elif kind == 'ds':
            size = ev(tok[1], consts)
            for p in pending:
                labels.append((p, addr, size, None))
            pending = []
            addr += size
        elif kind == 'union':
            union_stack.append([addr, addr])
        elif kind == 'nextu':
            u = union_stack[-1]
            u[1] = max(u[1], addr)
            for p in pending:
                labels.append((p, addr, 0, None))
            pending = []
            addr = u[0]
        elif kind == 'endu':
            u = union_stack.pop()
            for p in pending:
                labels.append((p, addr, 0, None))
            pending = []
            addr = max(u[1], addr)
    for p in pending:
        labels.append((p, addr, 0, None))
    # dedupe (struct owners were also given a ds size from their first field)
    best = {}
    for name, a, size, ctype in labels:
        if name in best:
            continue
        best[name] = (name, a, size, ctype)
    return list(best.values())

def main():
    global SECTIONS
    SECTIONS = load_sections()
    consts = load_consts()
    sym = load_sym()
    macros = load_macros()
    w = parse(os.path.join(SRC, 'wram.asm'), macros, consts, sym)
    h = parse(os.path.join(SRC, 'hram.asm'), macros, consts, sym)
    dst = os.path.join(ROOT, 'port', 'src', 'gen', 'wram.h')
    with open(dst, 'w') as f:
        f.write('/* Generated by port/tools/gen_wram.py from src/wram.asm and src/hram.asm. Do not edit. */\n')
        f.write('#ifndef TCG_GEN_WRAM_H\n#define TCG_GEN_WRAM_H\n\n')
        for arr, base, items in (('gWRAM', 0xc000, w), ('gHRAM', 0xff80, h)):
            for name, a, size, ctype in sorted(items, key=lambda x: (x[1], x[0])):
                off = a - base
                f.write(f'#define ADDR_{name} 0x{a:04x}\n')
                f.write(f'#define SIZEOF_{name} 0x{size:x}\n')
                if ctype:
                    f.write(f'#define {name} (*({ctype} *)&{arr}[0x{off:04x}])\n')
                elif size == 1:
                    f.write(f'#define {name} ({arr}[0x{off:04x}])\n')
                else:
                    f.write(f'#define {name} (&{arr}[0x{off:04x}])\n')
        f.write('\n#endif\n')
    print(f'wrote {len(w)} wram + {len(h)} hram labels to {dst}')
    missing = [n for n in sym if (n.startswith('w') or n.startswith('h')) and 0xc000 <= sym[n] < 0x10000
               and n not in {x[0] for x in w + h} and '.' not in n]
    if missing:
        print('labels in sym but not parsed:', missing[:20], file=sys.stderr)

if __name__ == '__main__':
    main()
