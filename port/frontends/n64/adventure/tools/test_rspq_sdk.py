#!/usr/bin/env python3
"""Exercise the SDK guard with isolated copies; never modify the real SDK."""
import argparse
import importlib.util
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile

HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('rspq_guard',HERE/'check_rspq_sdk.py')
guard=importlib.util.module_from_spec(spec);spec.loader.exec_module(guard)

def archive(body,name='rsp_queue.o',timestamp='0'):
    header=((name+'/').ljust(16)+timestamp.ljust(12)+'0'.ljust(6)+'0'.ljust(6)+
            '644'.ljust(8)+str(len(body)).ljust(10)+'`\n').encode('ascii')
    assert len(header)==60
    return b'!<arch>\n'+header+body+(b'\n' if len(body)&1 else b'')

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--sdk',type=Path,default=Path('/opt/libdragon'))
    args=parser.parse_args();pin=json.loads((guard.VENDOR/'UPSTREAM.json').read_text());micro=pin['sdk_queue_microcode']
    originals=[guard.VENDOR/'UPSTREAM.json']+[guard.VENDOR/name for name in pin['original_files']]
    originals += [args.sdk/'mips64-elf/include'/name for name in pin['sdk_public_headers']]+[args.sdk/micro['archive']]
    hashes={path:guard.digest(path.read_bytes()) for path in originals}
    guard.check(args.sdk);image=guard.archive_member((args.sdk/micro['archive']).read_bytes(),micro['member'])
    cases=[]
    with tempfile.TemporaryDirectory(prefix='poketcg-rspq-guard-') as temporary:
        root=Path(temporary);vendor=root/'vendor';sdk=root/'sdk';shutil.copytree(guard.VENDOR,vendor)
        include=sdk/'mips64-elf/include';include.mkdir(parents=True)
        for name in pin['sdk_public_headers']:shutil.copy(args.sdk/'mips64-elf/include'/name,include/name)
        library=sdk/micro['archive'];library.parent.mkdir(parents=True);library.write_bytes(archive(image))
        def run(name,expected=None,optimized=False):
            command=[sys.executable]+(['-O'] if optimized else [])+[str(HERE/'check_rspq_sdk.py'),'--sdk',str(sdk),'--vendor',str(vendor)]
            result=subprocess.run(command,text=True,capture_output=True)
            if expected is None:
                assert result.returncode==0,result.stderr
            else:
                assert result.returncode!=0 and expected in result.stderr,(name,result.stdout,result.stderr)
                assert 'update its SDK pin' in result.stderr and 'Traceback' not in result.stderr
            cases.append(name)
        run('private-copy baseline')
        for name in pin['original_files']:
            path=vendor/name;before=path.read_bytes();path.write_bytes(before+b'\n')
            run('vendored '+name,'vendored '+name);path.write_bytes(before)
        path=include/'rspq_constants.h';before=path.read_bytes();path.write_bytes(before+b'\n')
        run('changed SDK public header','public header rspq_constants.h')
        run('guard active under python -O','public header rspq_constants.h',optimized=True);path.write_bytes(before)
        # Mutate one actual loaded microcode byte while retaining all headers.
        endian='<' if image[5]==1 else '>';bits=image[4]
        if bits==1:
            offset=struct.unpack_from(endian+'I',image,32)[0];entry,count=struct.unpack_from(endian+'HH',image,46);fmt='IIIIIIIIII'
        else:
            offset=struct.unpack_from(endian+'Q',image,40)[0];entry,count=struct.unpack_from(endian+'HH',image,58);fmt='IIQQQQIIQQ'
        sections=[struct.unpack_from(endian+fmt,image,offset+i*entry) for i in range(count)]
        loaded=next(row for row in sections if row[2]&2 and row[5] and row[1]!=8)
        changed=bytearray(image);changed[loaded[4]]^=1;library.write_bytes(archive(changed))
        run('microcode-only SDK update','rsp_queue.o loadable bytes')
        # A changed exported endpoint is also an ABI change, even if all loaded
        # instruction/data bytes remain identical.
        symbol_table=next(row for row in sections if row[1]==2);changed=bytearray(image)
        for i in range(0,symbol_table[5],symbol_table[9]):
            at=symbol_table[4]+i;info=image[at+(12 if bits==1 else 4)]
            if info>>4:
                value_at=at+(4 if bits==1 else 8);changed[value_at+(3 if bits==1 else 7)]^=4;break
        else:raise AssertionError('no public symbol in the queue object')
        library.write_bytes(archive(changed));run('changed microcode public endpoint','public symbol ABI changed')
        # Ignore archive timestamps and nonloaded ELF padding; these may vary
        # between otherwise equivalent SDK builds and contain no queue state.
        changed=bytearray(image);changed[9]^=0x5a;library.write_bytes(archive(changed,timestamp='123456789'))
        run('stable image despite object/archive metadata')
        library.write_bytes(archive(image,name='other.o'));run('missing queue member','missing rsp_queue.o')
        library.write_bytes(archive(image[:20]));run('truncated queue object','RSPQ SDK check failed')
        library.write_bytes(archive(image));run('restored private-copy baseline')
    assert all(guard.digest(path.read_bytes())==value for path,value in hashes.items()),'A real source/SDK file changed'
    print(json.dumps({'passed':True,'cases':cases,'original_files_unchanged':True,'current_sdk':guard.check(args.sdk)},indent=2))

if __name__=='__main__':main()
