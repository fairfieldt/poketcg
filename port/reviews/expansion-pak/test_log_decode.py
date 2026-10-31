#!/usr/bin/env python3
"""Host screenshot notifications may split a native RGBA16 hexadecimal word."""
import importlib.util
from pathlib import Path
p=Path(__file__).resolve().with_name('capture.py')
spec=importlib.util.spec_from_file_location('expansion_capture',p)
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
hexadecimal='ffff0001f80107c1'
interleaved=hexadecimal[:7]+'VI_CALLBACK {"event":"encoded"}\n'+hexadecimal[7:11]+'Captured screenshot\n'+hexadecimal[11:]
text='FRAMEBUFFER probe 4 1\nPIXELS 0 0 '+interleaved+'\nFRAMEBUFFER END\n'
frames=list(module.native_frames(module.clean_log(text),(4,1)))
assert len(frames)==1
assert bytes(frames[0][3][0])==bytes([255,255,255,255,0,0,0,255,255,0,0,255,0,255,0,255])
print('LOG DECODE PASS: callback and original encoder notifications preserve every interleaved RGBA16 pixel')
