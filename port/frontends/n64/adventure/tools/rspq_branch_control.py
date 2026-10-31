#!/usr/bin/env python3
"""Create a same-layout negative control by replacing only the wakeup branch."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('fixed_object',type=Path);p.add_argument('output',type=Path)
a=p.parse_args();blob=bytearray(a.fixed_object.read_bytes())
assert blob[:6]==b'\x7fELF\x01\x02','Expected a big-endian ELF32 N64 object'
table=struct.unpack_from('>I',blob,32)[0]
entry,count,strings=struct.unpack_from('>3H',blob,46)
headers=[struct.unpack_from('>10I',blob,table+i*entry) for i in range(count)]
names=blob[headers[strings][4]:headers[strings][4]+headers[strings][5]]
found=[h for h in headers if names[h[0]:].split(b'\0',1)[0]==b'.text.rspq_flush_internal']
assert len(found)==1
offset=found[0][4]+0x18
assert blob[offset:offset+4]==bytes.fromhex('10000001'),'Wakeup branch opcode/layout changed'
original=bytes(blob);blob[offset:offset+4]=b'\0'*4
assert sum(x!=y for x,y in zip(original,blob))==2
a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_bytes(blob)
print(json.dumps({'fixed_sha256':hashlib.sha256(original).hexdigest(),
 'nop_control_sha256':hashlib.sha256(blob).hexdigest(),'changed_instruction_offset':offset,
 'control':'Same ELF layout and instructions, except the wakeup branch becomes NOP.'},indent=2))
