#!/usr/bin/env python3
"""Validate all exported meshes and the disassembly-to-scene manifest."""
from pathlib import Path
import json,math,struct,subprocess,sys
ROOT=Path(__file__).resolve().parents[1]
REPO=ROOT.parents[1]
sys.path.insert(0,str(REPO/'port/frontends/web/tools'))
from png import read_png
out=ROOT/'generated';world=json.loads((out/'world.json').read_text())
assert len(world['maps'])==34
assert [m['id'] for m in world['maps']]==list(range(34))
assert len({m['scene'] for m in world['maps'] if m['slug'].endswith('_lobby')})==1
foyers=[m for m in world['maps'] if m['slug'].endswith('_club_entrance') or m['slug']=='challenge_hall_entrance']
assert len(foyers)==9 and len({m['scene'] for m in foyers})==1
assert len({m['overlay'] for m in foyers})==9
assert len(world['overworld_nodes'])==12
# Dynamic fronts replace eight ordinary faces; they must not overlay them.
assert world['meshes']['deck_machine_room_dynamic']['triangles']==world['meshes']['deck_machine_room']['triangles']-16
vertices=triangles=0;max_textures=max_batches=0
for name,record in world['meshes'].items():
    blob=(out/record['file']).read_bytes()
    magic,nm,nt=struct.unpack_from('>4sHH',blob)
    assert magic==b'L643' and 0<nm<=256 and nt<=128,(name,nm,nt)
    max_textures=max(max_textures,nt);max_batches=max(max_batches,nm)
    offset=8
    for i in range(nt):
        filename=blob[offset:offset+32].split(b'\0')[0].decode();offset+=32
        w,h,_,_=read_png(out/'textures'/filename.replace('.sprite','.png'))
        assert w*h*2<=4096 and w&(w-1)==0 and h&(h-1)==0,(name,filename,w,h)
    nv_total=nt_total=0
    for i in range(nm):
        assert offset%4==0
        model,part,tex,zero,nv,ni,*pivot=struct.unpack_from('>HHhHII3f',blob,offset);offset+=28
        assert 0<nv<=65535 and ni%3==0 and -1<=tex<nt and zero==0
        vs=[]
        for j in range(nv):
            v=struct.unpack_from('>5f4B',blob,offset);offset+=24
            assert all(math.isfinite(x) for x in v[:5]);vs.append(v)
        indices=struct.unpack_from('>'+str(ni)+'H',blob,offset);offset+=ni*2;offset=(offset+3)&~3
        assert max(indices)<nv
        for j in range(0,ni,3):
            a,b,c=(vs[k][:3] for k in indices[j:j+3])
            ab=[b[k]-a[k] for k in range(3)];ac=[c[k]-a[k] for k in range(3)]
            cross=[ab[1]*ac[2]-ab[2]*ac[1],ab[2]*ac[0]-ab[0]*ac[2],ab[0]*ac[1]-ab[1]*ac[0]]
            assert sum(x*x for x in cross)>1e-13,(name,'degenerate',i,j)
        nv_total+=nv;nt_total+=ni//3
    assert offset==len(blob),(name,offset,len(blob))
    assert nv_total==record['vertices'] and nt_total==record['triangles']
    vertices+=nv_total;triangles+=nt_total
for m in world['maps']:
    assert (out/m['scene'].removeprefix('rom:/world/')).exists()
    if m['overlay']:assert (out/m['overlay'].removeprefix('rom:/world/')).exists()
    assert all(0<=w['map']<34 for w in m['warps'])
    assert all(n['model']==world['npc_models'][n['npc']] for n in m['npcs'])
lightning=world['maps'][15]
assert lightning['camera']==dict(eye=[14,9,34],target=[14,0,21.5],fov=58)
assert {(n['name'],n['x'],n['y'],n['dir']) for n in lightning['npcs']}=={
('NPC_JENNIFER',14,18,2),('NPC_NICHOLAS',6,10,2),('NPC_BRANDON',22,12,0),('NPC_ISAAC',12,4,0)}
subprocess.run([sys.executable,str(ROOT/'tools/check_prop_clearance.py'),
                '--report',str(out/'prop-clearance.json')],check=True)
print(f'WORLD3D PASS: 34 maps; {len(world["meshes"])} meshes; {triangles} triangles; {vertices} vertices; {world["texture_count"]} shared textures')
print(f'Per-scene maximum: {max_batches} batches, {max_textures} textures; RGBA16 TMEM and map/NPC/warp links verified')
