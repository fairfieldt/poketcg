#!/usr/bin/env python3
"""Extract exact story bytecode and compile source-native control flow metadata."""
from pathlib import Path
import re,sys,json,ast
ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'port/tools'))
from romsym import Rom
from render_maps import Game,Decompressor,map_event_patch
rom=Rom(); game=Game(str(ROOT)); OUT=ROOT/'port/overworld/gen'; OUT.mkdir(exist_ok=True)
const={}
for line in (ROOT/'port/src/gen/constants.h').read_text().splitlines():
 m=re.match(r'#define (\w+) (0x[0-9a-fA-F]+|\d+)$',line)
 if m: const[m[1]]=int(m[2],0)
for line in (ROOT/'port/src/gen/text.h').read_text().splitlines():
 m=re.match(r'#define (\w+) (0x[0-9a-fA-F]+|\d+)$',line)
 if m: const[m[1]]=int(m[2],0)

for line in (ROOT/'port/src/gen/text.h').read_text().splitlines():
 m=re.search(r'\b(\w+) = (0x[0-9a-fA-F]+|\d+)',line)
 if m:const[m[1]]=int(m[2],0);const[m[1]+'_']=int(m[2],0)

def val(s,scope=''):
 s=s.strip()
 if s in const:return const[s]
 if s.startswith('.'):s=scope+s
 if s in rom.sym:return rom.sym[s][1]
 s=re.sub(r'\$([0-9a-fA-F]+)',r'0x\1',s);s=re.sub(r'%([01]+)',r'0b\1',s)
 def symbol(match):
  token=match.group(0);name=scope+token if token.startswith('.') else token
  return str(const[token]) if token in const else str(rom.sym[name][1]) if name in rom.sym else token
 s=re.sub(r'(?<![\w.])[A-Za-z_.][\w.]*',symbol,s)
 s=re.sub(r'\b(HIGH|LOW)\(([^()]*)\)',lambda m:str((val(m[2],scope)>>8)&255 if m[1]=='HIGH' else val(m[2],scope)&255),s)
 return int(eval(s,{'__builtins__':{}},{}))
def lit(v):return json.dumps(v)
def arr(name,data):return 'const uint8_t '+name+'[] = {\n'+''.join(' '+','.join(str(x) for x in data[i:i+24])+',\n' for i in range(0,len(data),24))+'};\n'
# Script macro byte lengths follow src/macros/scripts.asm, not guessed opcodes.
lengths=[1,1,3,3,5,4,5,5,3,1,3,1,4,4,4,2,2,3,1,5,11,2,1,2,3,3,1,3,1,2,3,2,2,4,4,3,2,5,5,3,1,4,2,2,2,4,7,3,1,3,1,1,1,1,2,1,2,1,2,1,6,4,1,2,2,1,2,1,1,1,2,2,2,2,1,1,1,1,2,5,1,1,1,1,1,1,1,1,3,4,4,5,5,5,5,2,2,4,4,2,1,1,1,1]
lengths[58:64]=[6,4,1,2,2,1]
macro={}; current=None
for line in (ROOT/'src/macros/scripts.asm').read_text().splitlines():
 m=re.match(r'MACRO (\w+)',line)
 if m:current=m[1]
 m=re.search(r'run_command (ScriptCommand_\w+)',line)
 if m and current:
  cmd=m[1]+'_index'; indices={x:i for i,x in enumerate(re.findall(r'const (ScriptCommand_\w+_index)',(ROOT/'src/macros/scripts.asm').read_text()))};macro[current]=lengths[indices[cmd]]
for k in ('zero','nonzero','true','false'):macro['test_if_event_'+k]=4
for k in ('equal','not_equal','greater_or_equal','less_than'):macro['test_if_event_'+k]=5
macro['start_script']=1
ops={'ld','lb','cp','or','xor','and','add','sub','inc','dec','srl','rlca','scf','ccf','call','farcall','jp','jr','ret','push','pop','debug_nop','get_event_value','set_event_value','set_event_false','set_event_zero','max_event_value'}

def native_size(op,args):
 if op in ('get_event_value','set_event_value','set_event_false','set_event_zero','max_event_value','farcall'):return 4
 if op in ('call','jp','lb'):return 3
 if op in ('jr','srl'):return 2
 if op=='ld':
  a,b=args
  if a in ('bc','de','hl','sp'):return 3
  if ('[' in a and a not in ('[hl]','[hli]','[hld]')) or ('[' in b and b not in ('[hl]','[hli]','[hld]')):return 3
  return 1 if b in ('a','b','c','d','e','h','l','[hl]','[hli]','[hld]') else 2
 if op in ('cp','or','xor','and','sub'):return 1 if args[0] in ('a','b','c','d','e','h','l','[hl]') else 2
 if op=='add':return 1 if args[-1] in ('a','b','c','d','e','h','l','bc','de','hl') else 2
 return 1
natives=[];mismatches=[];unresolved=[]
files=list((ROOT/'src/scripts').glob('*.asm'))
# Include only the native map wrapper tail; command implementations are ported C.
tail=(ROOT/'src/engine/overworld/scripting.asm').read_text().split('LoadOverworld:\n',1)[1].split('INCLUDE "scripts/')[0]
files += [('LoadOverworld:\n'+tail)]
for source in files:
 text=source.read_text() if isinstance(source,Path) else source
 scope=''; pc=None
 lines=text.replace('\\\n',' ').splitlines()
 for raw in lines:
  s=raw.split(';')[0].strip()
  if not s:continue
  if not raw[0].isspace():
   name=s.rstrip(':')
   if name.startswith('.'):name=scope+name
   else:scope=name
   if name in rom.sym:
    bank,address=rom.sym[name]
    if pc is not None and pc!=address:mismatches.append((name,pc,address))
    pc=address if bank==3 else None
   continue
  if pc is None:continue
  bits=s.split(None,1);op=bits[0];args=[x.strip() for x in bits[1].split(',')] if len(bits)>1 else []
  if op in macro:pc+=macro[op];continue
  if op in ('db','dw','tx'):
   pc+=len(args)*(1 if op=='db' else 2);continue
  if op not in ops:continue
  n=native_size(op,args); converted=[]
  for arg in args:
   if arg in ('a','b','c','d','e','h','l','bc','de','hl','af','z','nz','nc','[hl]','[hli]','[hld]') or '[' in arg:converted.append(arg)
   else:
    try:converted.append(str(val(arg,scope)))
    except Exception:converted.append(arg);unresolved.append((pc,op,arg))
  natives.append((pc,pc+n,op,*(converted+['','',''])[:3]))
  pc+=n
# Native list mappings exact source labels let runtime enter a known bytecode block.
header='''#ifndef OW_DATA_H
#define OW_DATA_H
#include "../overworld.h"
typedef struct {uint8_t id,x,y,dir;uint16_t preload;} OwNpcDef;
typedef struct {uint8_t x,y,map,tx,ty;} OwWarp;
typedef struct {uint16_t script,name;uint8_t sprite,deck,flags,music,animation;} OwNpcHeader;
typedef struct {uint16_t pc,next;const char *op,*a,*b,*c;} OwNative;
extern const uint8_t ow_script_bank[16384],ow_event_masks[];
extern const OwNpcHeader ow_npc_headers[];
extern const OwNpcDef *const ow_map_npcs[];
extern const OwWarp *const ow_map_warps[];
extern const uint16_t ow_map_handlers[][8];
extern const OwNative ow_native[];
extern const unsigned ow_native_count;
extern const uint8_t ow_location_warps[][4],ow_location_positions[][2];
extern const uint8_t ow_script_lengths[];
typedef struct {uint8_t map,x,y,w,h;const uint8_t *permissions;} OwPermissionPatch;
extern const OwPermissionPatch ow_permission_patches[11];
extern const uint8_t ow_world_neighbors[13][4];
extern const uint8_t *const ow_world_paths[12][12];
#endif
'''
(OUT/'data.h').write_text(header)
out=['#include "data.h"\n',arr('ow_script_bank',rom.read(3,0x4000,0x4000)),arr('ow_script_lengths',lengths)]
b,p=rom.addr('EventVarMasks');out.append(arr('ow_event_masks',rom.read(b,p,const['NUM_EVENT_FLAGS']*2)))
out.append('const OwNative ow_native[] = {\n')
for r in sorted(natives):out.append('{'+str(r[0])+','+str(r[1])+','+','.join(lit(v) for v in r[2:])+'},\n')
out.append('};\nconst unsigned ow_native_count=sizeof(ow_native)/sizeof(ow_native[0]);\n')
npc_ptrs=[];warp_ptrs=[];maprows=[];handlers=[]
mb,mp=rom.addr('MapScripts'); wb,wp=rom.addr('WarpDataPointers')
for m in range(34):
 h=[rom.word(mb,mp+m*16+i*2) for i in range(8)];handlers.append(h)
 defs=game.map_npc_list(m)
 out.append('static const OwNpcDef npcs_'+str(m)+'[]={'+','.join('{'+','.join(str(d[k]) for k in ('npc','x','y','dir','preload'))+'}' for d in defs)+',{0,0,0,0,0}};\n');npc_ptrs.append('npcs_'+str(m))
 w=rom.word(wb,wp+m*2);rows=[]
 if w:
  while rom.word(wb,w):rows.append(list(rom.read(wb,w,5)));w+=5
 out.append('static const OwWarp warps_'+str(m)+'[]={'+','.join('{'+','.join(map(str,r))+'}' for r in rows)+',{0,0,0,0,0}};\n');warp_ptrs.append('warps_'+str(m))
 tm=game.load_tilemap(game.map_header(m)['tilemap_cgb']);w,h=tm['w'],tm['h']
 d=Decompressor(game.rom,tm['bank'],tm['perm']);perm=list(d.read(((w+1)//2)*((h+1)//2))) if tm['perm'] else [0]*(((w+1)//2)*((h+1)//2))
 out.append(arr('permissions_'+str(m),perm))
 name=next((n for n,v in game.consts['map_constants.asm'].items() if v==m and not n.startswith(('OWMAP','MAP_'))),'MAP')
 maprows.append('{'+lit(name)+','+str(w)+','+str(h)+',permissions_'+str(m)+','+str(game.map_header(m)['song'])+'}')
patches=[]
for event in range(11):
 x,y,tilemap=map_event_patch(game,event);tm=game.load_tilemap(tilemap)
 width,height=(tm['w']+1)//2,(tm['h']+1)//2
 if tm['perm']:
  perm=list(Decompressor(game.rom,tm['bank'],tm['perm']).read(width*height));out.append(arr('patch_permissions_'+str(event),perm));pointer='patch_permissions_'+str(event)
 else:pointer='0'
 patches.append('{'+','.join(map(str,([31,32]+[2]*8+[1])[event:event+1]+[x,y,tm['w'],tm['h']]))+','+pointer+'}')
out.append('const OwPermissionPatch ow_permission_patches[11]={'+','.join(patches)+'};\n')
b,p=rom.addr('OverworldMap_CursorTransitions')
out.append('const uint8_t ow_world_neighbors[13][4]={'+','.join('{'+','.join(map(str,rom.read(b,p+i*4,4)))+'}' for i in range(13))+'};\n')
b,p=rom.addr('OverworldMap_PlayerMovementPaths');path_rows=[];seen_paths={}
for start in range(12):
 table=rom.word(b,p+start*2);row=[]
 for target in range(12):
  path=rom.word(b,table+target*2)
  if path not in seen_paths:
   name='world_path_'+str(len(seen_paths));seen_paths[path]=name;data=[];at=path
   while True:
    xy=list(rom.read(b,at,2));data+=xy;at+=2
    if xy==[255,255]:break
   out.append(arr(name,data))
  row.append(seen_paths[path])
 path_rows.append('{'+','.join(row)+'}')
out.append('const uint8_t *const ow_world_paths[12][12]={'+','.join(path_rows)+'};\n')
out.append('const OwMap ow_maps[34]={'+','.join(maprows)+'};\n')
out.append('const OwNpcDef *const ow_map_npcs[]={'+','.join(npc_ptrs)+'};\n')
out.append('const OwWarp *const ow_map_warps[]={'+','.join(warp_ptrs)+'};\n')
out.append('const uint16_t ow_map_handlers[][8]={'+','.join('{'+','.join(map(str,r))+'}' for r in handlers)+'};\n')
objects=[]
for mapid in range(34):
 p=handlers[mapid][2]
 if p:
  while rom.byte(mb,p)!=255:
   row=list(rom.read(mb,p,9));objects.append([mapid,row[0],row[1],row[2],row[3]|row[4]<<8,row[5]|row[6]<<8]);p+=9
out.append('const uint16_t ow_objects[][6]={'+','.join('{'+','.join(map(str,r))+'}' for r in objects)+'};\n')
out.append('const unsigned ow_object_count=sizeof(ow_objects)/sizeof(*ow_objects);\n')
text_lines=(ROOT/'port/src/gen/text.c').read_text().splitlines()
text_strings=['']
for line in text_lines:
 if '/*' in line:
  literals=re.findall(r'"(?:\\.|[^"\\])*"',line.split('*/',1)[1])
  if literals:text_strings.append(''.join(ast.literal_eval(t.replace('\\?','?')) for t in literals))
npc_names=[]
out.append('const OwNpcHeader ow_npc_headers[]={\n')
for n in range(const['NUM_NPCS']):
 b,p=rom.addr('NPCHeaderPointers'); ptr=rom.word(b,p+n*2)
 nameid=rom.word(b,ptr+7);npc_names.append(text_strings[nameid] if nameid<len(text_strings) else '')
 out.append('{'+','.join(map(str,(rom.word(b,ptr+5),rom.word(b,ptr+7),rom.byte(b,ptr+1),rom.byte(b,ptr+10),rom.byte(b,ptr+4),rom.byte(b,ptr+11),rom.byte(b,ptr+3))))+'},\n')
out.append('};\nconst char *const ow_npc_names[]={'+','.join(lit(v) for v in npc_names)+'};\n')
for sym,count,width,name in [('OverworldMapWarps',13,4,'ow_location_warps'),('OverworldMap_MapPositions',13,2,'ow_location_positions')]:
 b,p=rom.addr(sym);out.append('const uint8_t '+name+'[]['+str(width)+']={'+','.join('{'+','.join(map(str,rom.read(b,p+i*width,width)))+'}' for i in range(count))+'};\n')
out.append('typedef struct {const char *name;uint16_t addr;} Name;\nstatic const Name script_names[]={\n')
for name,(bank,address) in sorted(rom.sym.items()):
 if bank==3 or name in ('FindLoadedNPC','SetNextScript','SetNextNPCAndScript','FindEndOfDuelScript','TryGiveMedalPCPacks','TryGivePCPack','GetEventValue','MaxOutEventValue','GetCardCountInCollection','AddCardToCollection','GetCardName','Random','UpdateRNGSources','PlaySFX','SetOWMapEvent','ApplyOWMapEventChangeIfEventSet','FindExtraInteractableObjects','PCMenu','wd698','JumpToFunctionInTable'):out.append('{'+lit(name)+','+str(address)+'},\n')
out.append('};\n#include <string.h>\nuint16_t ow_script_address(const char *name){for(unsigned i=0;i<sizeof(script_names)/sizeof(*script_names);i++)if(!strcmp(name,script_names[i].name))return script_names[i].addr;return 0;}\n')
(OUT/'data.c').write_text(''.join(out).replace('[]={,','[]={'))
(OUT/'extraction.json').write_text(json.dumps(dict(native_instructions=len(natives),source_address_mismatches=mismatches),indent=2)+'\n')
assert not mismatches, mismatches
assert not unresolved, unresolved
print('Generated',len(natives),'native source operations; address mismatches',len(mismatches))
