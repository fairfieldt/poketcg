#!/usr/bin/env python3
"""Inventory all story files and non-duel engine modules from the disassembly.

The inventory counts source constructs; it does not claim runtime coverage.
"""
from collections import Counter
import hashlib
import json
from pathlib import Path
import re

ROOT=Path(__file__).resolve().parents[2]
macro_source=(ROOT/'src/macros/scripts.asm').read_text()
macros={}
for match in re.finditer(r'MACRO (\w+)\n(.*?)ENDM',macro_source,re.S):
    command=re.search(r'run_command (ScriptCommand_\w+)',match[2])
    if command:macros[match[1]]=command[1]
scripts=[]
all_usage=Counter()
for path in sorted((ROOT/'src/scripts').glob('*.asm')):
    source=path.read_text()
    lines=[line.split(';')[0].strip() for line in source.splitlines()]
    usage=Counter(line.split()[0] for line in lines if line and line.split()[0] in macros)
    all_usage.update(usage)
    scripts.append(dict(path=str(path.relative_to(ROOT)),sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
        lines=len(lines),script_labels=re.findall(r'^(Script_\w+):',source,re.M),
        preload_labels=re.findall(r'^(Preload_\w+):',source,re.M),command_usage=dict(usage)))
modules=[]
for path in sorted((ROOT/'src/engine').rglob('*.asm')):
    if '/duel/' in str(path):continue
    source=path.read_text()
    modules.append(dict(path=str(path.relative_to(ROOT)),lines=len(source.splitlines()),
        labels=re.findall(r'^(\w+)::?',source,re.M)))
inventory=dict(script_file_count=len(scripts),script_command_count=len(re.findall(r'const ScriptCommand_\w+_index',macro_source)),
    story_label_count=sum(len(s['script_labels']) for s in scripts),
    npc_preload_count=sum(len(s['preload_labels']) for s in scripts),
    used_script_macros=dict(sorted(all_usage.items())),scripts=scripts,non_duel_modules=modules)
out=ROOT/'port/notes/overworld_inventory.json'
out.write_text(json.dumps(inventory,indent=2)+'\n')
print(json.dumps({k:inventory[k] for k in ('script_file_count','script_command_count','story_label_count','npc_preload_count')},indent=2))
print(out)
