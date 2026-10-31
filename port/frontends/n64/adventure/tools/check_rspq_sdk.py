#!/usr/bin/env python3
"""Guard the pinned queue source and its installed SDK/microcode ABI."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

VENDOR=Path(__file__).resolve().parents[1]/'vendor/libdragon-rspq'

class GuardError(ValueError):
    pass

def digest(data):
    return hashlib.sha256(data).hexdigest()

def archive_member(archive,name):
    """Read one ar member without depending on host ar/toolchain versions."""
    if archive[:8]!=b'!<arch>\n':
        raise GuardError('libdragon archive has an unsupported format')
    offset=8;strings=b'';found=None
    while offset<len(archive):
        header=archive[offset:offset+60]
        if len(header)!=60 or header[58:60]!=b'`\n':
            raise GuardError('libdragon archive has an invalid member header')
        length=int(header[48:58]);start=offset+60;body=archive[start:start+length]
        if len(body)!=length:raise GuardError('libdragon archive is truncated')
        label=header[:16].decode('ascii').strip()
        if label=='//':strings=body
        elif label.startswith('#1/'):
            count=int(label[3:]);label=body[:count].decode('utf-8').rstrip('\0');body=body[count:]
        elif label.startswith('/') and label[1:].isdigit():
            label=strings[int(label[1:]):].split(b'\n',1)[0].decode('utf-8').rstrip('/')
        else:label=label.rstrip('/')
        if label==name:
            if found is not None:raise GuardError(f'libdragon archive has duplicate {name} members')
            found=body
        offset=start+length+(length&1)
    if found is not None:return found
    raise GuardError(f'libdragon archive is missing {name}')

def elf_image(data):
    """Normalize loadable bytes and public symbols, excluding debug paths."""
    if len(data)<16 or data[:4]!=b'\x7fELF' or data[4] not in (1,2) or data[5] not in (1,2):
        raise GuardError('queue microcode member is not a supported ELF object')
    bits=32 if data[4]==1 else 64;order='<' if data[5]==1 else '>'
    def unpack(fmt,offset):return struct.unpack_from(order+fmt,data,offset)
    kind,machine=unpack('HH',16)
    if bits==32:
        section_offset=unpack('I',32)[0];flags=unpack('I',36)[0]
        entry_size,count,strings_index=unpack('HHH',46);section_format='IIIIIIIIII'
    else:
        section_offset=unpack('Q',40)[0];flags=unpack('I',48)[0]
        entry_size,count,strings_index=unpack('HHH',58);section_format='IIQQQQIIQQ'
    if not count or strings_index>=count or entry_size<struct.calcsize(order+section_format):
        raise GuardError('queue microcode has an unsupported section table')
    if section_offset+entry_size*count>len(data):raise GuardError('queue microcode section table is truncated')
    rows=[unpack(section_format,section_offset+i*entry_size) for i in range(count)]
    def contents(row):
        offset,size=row[4],row[5]
        if row[1]==8:return b'' # SHT_NOBITS has no file payload.
        if offset+size>len(data):raise GuardError('queue microcode section is truncated')
        return data[offset:offset+size]
    def string(blob,offset):
        if offset>=len(blob):raise GuardError('queue microcode has an invalid string offset')
        return blob[offset:].split(b'\0',1)[0].decode('utf-8')
    names=contents(rows[strings_index]);labels=[string(names,row[0]) for row in rows]
    sections=[]
    for name,row in zip(labels,rows):
        if row[2]&2 and row[5]: # SHF_ALLOC: actual loaded microcode/data.
            sections.append({'name':name,'type':row[1],'flags':row[2],
                             'size':row[5],'alignment':row[8],'sha256':digest(contents(row))})
    symbols=[]
    for row in rows:
        if row[1]!=2:continue # SHT_SYMTAB
        if row[6]>=count:raise GuardError('queue microcode has an invalid symbol string table')
        symbol_names=contents(rows[row[6]]);table=contents(row)
        fmt='IIIBBH' if bits==32 else 'IBBHQQ';size=struct.calcsize(order+fmt)
        if row[9]<size or len(table)%row[9]:raise GuardError('queue microcode has an invalid symbol table')
        for offset in range(0,len(table),row[9]):
            values=struct.unpack_from(order+fmt,table,offset)
            if bits==32:name,value,symbol_size,info,other,index=values
            else:name,info,other,index,value,symbol_size=values
            if not name or info>>4==0:continue # Local/file/debug symbols are not ABI.
            section=labels[index] if index<count else {0xfff1:'ABS',0xfff2:'COMMON'}.get(index,str(index))
            symbols.append({'name':string(symbol_names,name),'value':value,'size':symbol_size,
                            'binding':info>>4,'type':info&15,'visibility':other&3,'section':section})
    if not sections:raise GuardError('queue microcode has no allocated sections')
    return {'class':bits,'endian':'little' if order=='<' else 'big','type':kind,'machine':machine,
            'flags':flags,'sections':sorted(sections,key=lambda item:item['name']),
            'public_symbols':sorted(symbols,key=lambda item:item['name'])}

def check(sdk,vendor=VENDOR):
    pin=json.loads((vendor/'UPSTREAM.json').read_text())
    patched=pin.get('patched_files',{})
    if not set(patched)<=set(pin['original_files']):raise GuardError('pin has an untracked patched source')
    for name,original in pin['original_files'].items():
        if digest((vendor/name).read_bytes())!=patched.get(name,original):
            raise GuardError(f'vendored {name} differs from its pinned source')
    include=sdk/'mips64-elf/include'
    for name,expected in pin['sdk_public_headers'].items():
        if digest((include/name).read_bytes())!=expected:
            raise GuardError(f'SDK public header {name} differs from its pinned ABI')
    microcode=pin['sdk_queue_microcode']
    actual=elf_image(archive_member((sdk/microcode['archive']).read_bytes(),microcode['member']))
    if actual!=microcode['image']:
        raise GuardError(f'SDK {microcode["member"]} loadable bytes or public symbol ABI changed')
    return {'passed':True,'vendored_files':len(pin['original_files']),
            'public_headers':len(pin['sdk_public_headers']),'microcode_member':microcode['member'],
            'loadable_sections':len(actual['sections']),'microcode_public_symbols':len(actual['public_symbols'])}

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk',type=Path,default=Path('/opt/libdragon'))
    parser.add_argument('--vendor',type=Path,default=VENDOR,help='Override only for isolated guard tests')
    args=parser.parse_args()
    try:check(args.sdk,args.vendor)
    except (OSError,ValueError,KeyError,struct.error) as error:
        print(f'RSPQ SDK check failed: {error}.\nReview the queue backport and update its SDK pin before rebuilding.',file=sys.stderr)
        return 1
    print('RSPQ SDK check PASS: source, private headers, public ABI and queue microcode match the pin')
    return 0

if __name__=='__main__':sys.exit(main())
