#!/usr/bin/env python3
"""Bounded per-card lockstep follow-up; does not modify the engine or ROM."""
from pathlib import Path
import json
import re
import subprocess
import time

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / 'port/reviews/special-effects-audit/focused'
PORT = ROOT / 'port'


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    plan = json.loads((OUT.parent / 'focus-plan.json').read_text())
    start = time.monotonic()
    runs = []
    for cid, name in plan:
        for human, count in ((True, 2), (False, 1)):
            if time.monotonic() - start > 180:
                break
            label = f'{cid:02x}-{name}-' + ('human' if human else 'ai')
            focus = OUT / f'{cid:02x}.txt'
            focus.write_text(f'0x{cid:02x}\n')
            command = [str(PORT / 'build/effects-audit/harness/difftest'),
                       str(ROOT / 'poketcg.gbc'), str(ROOT / 'poketcg.sym'),
                       str(PORT / 'build/effects-audit/harness/trace_points.txt'),
                       '--focus', str(focus), '--seeds', str(9000 + cid), str(count),
                       '--coverage', str(OUT / f'{label}-coverage.txt')]
            if human:
                command += ['--human']
            log = OUT / f'{label}.log'
            began = time.monotonic()
            with log.open('w') as stream:
                try:
                    result = subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT,
                                            timeout=20)
                    code = result.returncode
                except subprocess.TimeoutExpired:
                    code = 'timeout'
            run = {'card_id': cid, 'card': name, 'human': human, 'duels': count,
                   'exit_code': code, 'wall_seconds': round(time.monotonic() - began, 3),
                   'log': str(log.relative_to(ROOT)),
                   'coverage': str((OUT / f'{label}-coverage.txt').relative_to(ROOT))}
            runs.append(run)
            print(json.dumps(run), flush=True)
    summaries = [tuple(map(int, m)) for run in runs for m in re.findall(
        r'(?m)^(\d+)/(\d+) duels identical$', (ROOT / run['log']).read_text())]
    result = {'wall_seconds': round(time.monotonic() - start, 3),
              'planned_cards': len(plan), 'planned_duels': len(plan) * 3,
              'duels_attempted': sum(n for _, n in summaries),
              'duels_strictly_matching': sum(n for n, _ in summaries),
              'all_planned_runs_completed': len(runs) == len(plan) * 2,
              'failures': [r for r in runs if r['exit_code'] != 0], 'runs': runs}
    (OUT / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    assert not result['failures'], result['failures']


if __name__ == '__main__':
    main()
