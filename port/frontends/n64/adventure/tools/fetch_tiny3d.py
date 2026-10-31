#!/usr/bin/env python3
"""Fetch a pinned Tiny3D core for the opt-in renderer; leave the SDK unchanged."""
import os
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[1]
SOURCE=ROOT/'build/tiny3d-experiment'
REVISION='f2eccc3998967f1915be58d52f1c5bb6d5598a8c'
SDK=Path(os.environ.get('N64_INST','/opt/libdragon'))

def run(*args):
    subprocess.run([str(a) for a in args],check=True)

if not (SOURCE/'.git').exists():
    SOURCE.mkdir(parents=True,exist_ok=True)
    run('git','-C',SOURCE,'init')
    run('git','-C',SOURCE,'remote','add','origin','https://github.com/HailToDodongo/tiny3d.git')
    run('git','-C',SOURCE,'fetch','--depth','1','origin',REVISION)
    run('git','-C',SOURCE,'checkout','--detach',REVISION)
actual=subprocess.check_output(['git','-C',str(SOURCE),'rev-parse','HEAD'],text=True).strip()
assert actual==REVISION,(actual,REVISION)
# Core only: our L643 adapter supplies meshes/materials and rigid animation.
# Tiny3D's optional streamed animation module requires newer SDK DMA APIs.
objects=['t3d.o','t3dmath.o','rsp/rsp_tiny3d.o','rsp/rsp_tiny3d_clipping.o']
run('make','-C',SOURCE,'-j4',f'N64_INST={SDK}',*[f'build/{p}' for p in objects])
run(SDK/'bin/mips64-elf-ld','-r','-o',SOURCE/'build/libt3d-poketcg.a',*[SOURCE/'build'/p for p in objects])
print('Tiny3D core ready:',REVISION)
