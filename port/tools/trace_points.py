#!/usr/bin/env python3
"""List the routines that call TRACE_FN() in the port's sources.

The differential harness puts a breakpoint on each of these symbols in the
original ROM and expects the port to report entering the same routines in the
same order. Usage: trace_points.py [src_dir] > trace_points.txt
"""
import os, re, sys

src = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), '..', 'src')
func_re = re.compile(r'^(?:static\s+)?(?:inline\s+)?[A-Za-z_][\w\s\*]*?\b([A-Za-z_]\w*)\s*\([^;{]*\)\s*\{?\s*$')
names = []
for root, _, files in os.walk(src):
    for fn in sorted(files):
        if not fn.endswith('.c'):
            continue
        current = None
        for line in open(os.path.join(root, fn), encoding='utf-8'):
            m = func_re.match(line)
            if m and not line.startswith((' ', '\t')):
                current = m.group(1)
            if 'TRACE_FN();' in line and current and not line.lstrip().startswith(('#', '//')):
                if current not in names:
                    names.append(current)
for n in sorted(names):
    print(n)
