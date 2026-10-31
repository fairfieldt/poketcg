#!/usr/bin/env python3
"""Diagnostic-only bounded allowance for cancelled Amnesia's unused E byte.

No engine state is modified. The normal harness still fails the control seed;
this probe asks whether ignoring only this stale byte exposes any subsequent
request, RNG, or gameplay-state difference before it is overwritten.
"""
from pathlib import Path
import difflib
import subprocess

ROOT = Path(__file__).resolve().parents[3]
PORT = ROOT / 'port'
BUILD = PORT / 'build/effects-audit'
OUT = PORT / 'reviews/special-effects-audit'
original = (PORT / 'harness/difftest.c').read_text()
modified = original.replace('static int verbose, strict, human, random_decks, prizes;',
    'static int verbose, strict, human, random_decks, prizes;\n'
    'static int cancelled_amnesia_scratch;\nstatic unsigned scratch_differences;')
modified = modified.replace('u8 p = *gbptr((u16)a), o = *ref_byte_ptr(ev, (u16)a);',
    'u8 p = *gbptr((u16)a), o = *ref_byte_ptr(ev, (u16)a);\n'
    '\t\t\tif (a == ADDR_hTemp_ffa0 && cancelled_amnesia_scratch) {\n'
    '\t\t\t\tif (p != o && p == 0 && o == 13 &&\n'
    '\t\t\t\t    (!strcmp(where, "UseAttackOrPokemonPower") ||\n'
    '\t\t\t\t     !strcmp(where, "PoliwhirlAmnesia_CheckAttacks") ||\n'
    '\t\t\t\t     !strcmp(where, "PoliwhirlAmnesia_PlayerSelectEffect")))\n'
    '\t\t\t\t    { scratch_differences++; continue; }\n'
    '\t\t\t\tcancelled_amnesia_scratch = 0;\n\t\t\t}')
modified = modified.replace('int c = choose(r, &ev.input);',
    'int c = choose(r, &ev.input);\n'
    '\tif (c == TCG_CANCEL && r->type == TCG_REQ_MENU && r->menu == TCG_MENU_ATTACKS &&\n'
    '\t    rd16(wLoadedAttackName) == AmnesiaName) cancelled_amnesia_scratch = 1;')
modified = modified.replace('printf("%d/%d duels identical\\n", runs - fails, runs);',
    'printf("State/RNG agree except documented discarded UI scratch: %d/%d duels\\n", runs - fails, runs);\n'
    '\tprintf("AMNESIA SCRATCH comparator-only differences=%u outstanding=%d\\n",\n'
    '\t       scratch_differences, cancelled_amnesia_scratch);\n'
    '\tif (scratch_differences != 3 || cancelled_amnesia_scratch) return 2;')
assert modified != original and 'scratch_differences++' in modified
(OUT / 'amnesia-scratch-probe.patch').write_text(''.join(difflib.unified_diff(
    original.splitlines(True), modified.splitlines(True),
    fromfile='harness/difftest.c', tofile='diagnostic-only/difftest_scratch.c')))
modified = modified.replace('"../src/engine.h"', f'"{PORT}/src/engine.h"')
modified = modified.replace('"../src/gen/cards.h"', f'"{PORT}/src/gen/cards.h"')
modified = modified.replace('"ref.h"', f'"{PORT}/harness/ref.h"')
path = BUILD / 'difftest_scratch.c'
path.write_text(modified)
sources = sorted((PORT / 'src').glob('*.c')) + sorted((PORT / 'src/gen').glob('*.c'))
sources += sorted((PORT / 'src/effects').glob('*.c')) + sorted((PORT / 'src/ai').glob('*.c'))
binary = BUILD / 'difftest_scratch'
subprocess.run(['cc', '-std=c11', '-O2', '-g', f'-I{PORT}/src', '-DTCG_NO_DEFAULT_FATAL',
    '-DTCG_TRACE', '-o', str(binary), str(path), str(PORT / 'harness/ref.c'),
    str(PORT / 'harness/gb/gb.c'), *map(str, sources)], check=True)
with (OUT / 'amnesia-scratch-probe.log').open('w') as log:
    subprocess.run([str(binary), str(ROOT / 'poketcg.gbc'), str(ROOT / 'poketcg.sym'),
        str(BUILD / 'harness/trace_points.txt'), '--human', '--focus',
        str(OUT / 'focused/47.txt'), '--seeds', '9071', '1', '--verbose'],
        stdout=log, stderr=subprocess.STDOUT, timeout=20, check=True)
