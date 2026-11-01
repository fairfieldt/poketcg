#!/usr/bin/env python3
"""Build every map as reusable, actual N64 3D geometry (standard library only).

The original ROM is used only to extract authoritative map/NPC/warp/permission
metadata. Checked-in .l64 and textures build on hosts without ROM or Blender.
"""
from collections import Counter,defaultdict
from dataclasses import asdict,replace
import hashlib
import json
import math
from pathlib import Path
import struct
import sys

ROOT=Path(__file__).resolve().parents[1]
REPO=ROOT.parents[1]
sys.path.insert(0,str(REPO/'port/frontends/web/tools'))
sys.path.insert(0,str(REPO/'port/tools'))
from png import read_png,write_png
from render_maps import Game,Decompressor,parse_consts
from scene_specs import PROPS,FOYER,LOBBY,Prop,P,F,footprint
from room_style import material_pixel,wall_paint,bake_lighting
from solid_terrain import POOL_CELLS,swimming_pool as build_swimming_pool,align_lightning_room
from actor_grounding import strip_legacy_disks
from honor_props import build as build_honor_prop
from furniture_props import build as build_furniture_prop
from lab_props import build as build_lab_prop,build_deck_front
from decorative_props import build as build_decorative_prop
OUT=ROOT/'generated'
TEX=OUT/'textures'
MAPS=OUT/'maps'
TEX.mkdir(parents=True,exist_ok=True)
MAPS.mkdir(parents=True,exist_ok=True)

class Image:
    def __init__(self,path):
        self.width,self.height,rows,palette=read_png(path)
        self.rows=[[tuple(palette[p])+(255,) if palette else tuple(p) for p in row] for row in rows]
    def at(self,x,y):
        return self.rows[max(0,min(self.height-1,int(y)))][max(0,min(self.width-1,int(x)))]
    def crop(self,x,y,w,h,tw=None,th=None):
        if tw is None: tw=min(32,2**math.ceil(math.log2(max(1,w))))
        if th is None: th=min(64,2**math.ceil(math.log2(max(1,h))))
        while tw*th*2>4096: th//=2
        return [[self.at(x+i*w/tw,y+j*h/th) for i in range(tw)] for j in range(th)]

textures={}
def texture(rows):
    raw=b''.join(bytes(c for p in row for c in p) for row in rows)
    key='t'+hashlib.sha256(struct.pack('>HH',len(rows[0]),len(rows))+raw).hexdigest()[:16]
    if key not in textures:
        textures[key]=(len(rows[0]),len(rows))
        if not (TEX/(key+'.png')).exists():
            write_png(TEX/(key+'.png'),len(rows[0]),len(rows),[bytes(c for p in row for c in p) for row in rows])
    return key

def rgb(hexcolor):
    return tuple(bytes.fromhex(hexcolor.lstrip('#')))+(255,)

WHITE=rgb('efebda'); BLACK=rgb('181b2b'); BROWN=rgb('955f35'); GREEN=rgb('469d35')
DARKGREEN=rgb('1b5523'); GOLD=rgb('d4a836'); RED=rgb('d62824'); BLUE=rgb('374fb7')

def swimming_pool(scene):
    build_swimming_pool(scene,texture,rgb,WHITE)

# A source actor at anchor (x,y) is drawn over tiles x..x+2, y..y+2, but the
# world places actors, prop bodies and doorways at the anchor itself. Source
# art is therefore sampled one tile east/south of its world position.
ART=8
# East and south walls stand as far outside the outermost walkable anchors
# as the west/north walls; the source art draws them in the same place.
EAST_WALL=2.9
SOUTH_WALL=4.9

def doors(warps,w,h,side):
    intervals=[]
    for warp in warps:
        x,z=warp['x'],warp['y']
        if ((side=='north' and z<=1) or (side=='south' and z>=h-4)):
            intervals.append((x-1.05,x+1.05))
        if ((side=='west' and x<=1) or (side=='east' and x>=w-3)):
            intervals.append((z-1.05,z+1.05))
    # Two adjacent source anchors often describe one two-tile doorway.
    # Merge them before creating either wall gaps or threshold surfaces.
    merged=[]
    for left,right in sorted(intervals):
        if merged and left<=merged[-1][1]:merged[-1]=(merged[-1][0],max(right,merged[-1][1]))
        else:merged.append((left,right))
    return merged


class Scene:
    def __init__(self,name):
        self.name=name
        self.batches=defaultdict(lambda:([],[],{}))
        self.props=[]
        self.switchable=False
        self.bounds=None
        self.lighting_baked=False
    def polygon(self,points,color=WHITE,tex=None,uv=None,model=0,part=0,pivot=(0,0,0),vertex_colors=None):
        key=(model,part,tex,pivot)
        vs,inds,cache=self.batches[key]
        if uv is None: uv=[(0,0),(1,0),(1,1),(0,1)]
        for i in range(1,len(points)-1):
            tri=(0,i,i+1)
            a,b,c=(points[j] for j in tri)
            ab=[b[k]-a[k] for k in range(3)];ac=[c[k]-a[k] for k in range(3)]
            cross=[ab[1]*ac[2]-ab[2]*ac[1],ab[2]*ac[0]-ab[0]*ac[2],ab[0]*ac[1]-ab[1]*ac[0]]
            if sum(v*v for v in cross)<1e-13:continue
            for j in tri:
                v=(*points[j],*uv[j%len(uv)],*(vertex_colors[j] if vertex_colors else color))
                if v not in cache:
                    cache[v]=len(vs);vs.append(v)
                inds.append(cache[v])
    def box(self,x,z,w,d,h,color=WHITE,tex=None,y=0,front=None,omit=(),top=None,top_hole=None):
        # Coordinates are X east / Y up / Z south. Front is +Z.
        points=[(x,y,z),(x+w,y,z),(x+w,y,z+d),(x,y,z+d),
                (x,y+h,z),(x+w,y+h,z),(x+w,y+h,z+d),(x,y+h,z+d)]
        for fi,face in enumerate([(0,3,2,1),(0,1,5,4),(1,2,6,5),(2,3,7,6),(3,0,4,7),(4,5,6,7)]):
            if fi in omit:continue
            # Front textures replace, rather than overlay, the side plane.
            t=front if fi==3 and front else top if fi==5 and top else tex
            shade=(.6,.78,.86,1,.72,1)[fi]
            pigment=WHITE if t else color
            c=tuple(int(v*shade) for v in pigment[:3])+(255,)
            uv=[(1,1),(0,1),(0,0),(1,0)] if fi==3 else [(0,1),(1,1),(1,0),(0,0)] if fi!=5 else None
            if fi==5 and top_hole:
                # A raised body owns the opening instead of covering a full
                # backing cap with a second, coplanar underside.
                a,b,right,south=top_hole
                assert x<=a<=right<=x+w and z<=b<=south<=z+d
                for left,north,r,s in ((x,z,x+w,b),(x,south,x+w,z+d),
                                        (x,b,a,south),(right,b,x+w,south)):
                    if r>left and s>north:
                        self.polygon([(left,y+h,north),(r,y+h,north),
                                      (r,y+h,s),(left,y+h,s)],c,t,
                                     [((left-x)/w,(north-z)/d),((r-x)/w,(north-z)/d),
                                      ((r-x)/w,(s-z)/d),((left-x)/w,(s-z)/d)])
                continue
            self.polygon([points[i] for i in face],c,t,uv)
    def cylinder(self,x,z,y,r,h,color=WHITE,segments=8,top_r=None):
        if top_r is None:top_r=r
        bottom=[(x+r*math.cos(i*math.tau/segments),y,z+r*math.sin(i*math.tau/segments)) for i in range(segments)]
        top=[(x+top_r*math.cos(i*math.tau/segments),y+h,z+top_r*math.sin(i*math.tau/segments)) for i in range(segments)]
        self.polygon(top,color)
        for i in range(segments):
            j=(i+1)%segments
            shade=.7+.3*(.5+.5*math.cos(i*math.tau/segments))
            c=tuple(int(v*shade) for v in color[:3])+(255,)
            self.polygon([bottom[i],bottom[j],top[j],top[i]],c)
    def ball(self,x,z,y,sx,sy,sz,color=WHITE,segments=8,rings=4):
        ps=[(x,y-sy,z)]
        for j in range(1,rings):
            a=-math.pi/2+math.pi*j/rings
            ps.extend((x+sx*math.cos(a)*math.cos(i*math.tau/segments),y+sy*math.sin(a),z+sz*math.cos(a)*math.sin(i*math.tau/segments)) for i in range(segments))
        top=len(ps);ps.append((x,y+sy,z))
        for i in range(segments):
            n=(i+1)%segments
            self.polygon([ps[0],ps[1+i],ps[1+n]],color)
            for j in range(rings-2):
                a=1+j*segments;b=a+segments
                shade=.7+.3*j/(rings-2)
                c=tuple(int(v*shade) for v in color[:3])+(255,)
                self.polygon([ps[a+i],ps[b+i],ps[b+n],ps[a+n]],c)
            a=1+(rings-2)*segments
            self.polygon([ps[a+i],ps[top],ps[a+n]],color)
    def floor(self,img,erase,warps=(),base_rect=(16,64,16,16),exclude_emblem=False):
        w,h=img.width//8,img.height//8
        if self.name=='psychic_club':h=28
        self.bounds=(w,h)
        # The source draws side-wall doorways a further tile south than
        # their anchors, below the projected end of the upper wall segment.
        # The band reaches half a tile past the opening, beyond the mat.
        side_doors=[(-1,4,left,right+.5) for left,right in doors(warps,w,h,'west')]
        side_doors+=[(w-6,w,left,right+.5) for left,right in doors(warps,w,h,'east')]
        # Most frequent original 16x16 patch is the repeating room floor.
        patches=[]
        for y in range(4,h-4,2):
            for x in range(2,w-2,2):
                if any(p.x-1<=x<p.x+p.w and p.y-1<=y<p.y+p.h for p in erase): continue
                patch=tuple(img.at(x*8+i,y*8+j) for j in range(16) for i in range(16))
                patches.append((patch,(x*8,y*8)))
        if patches:
            best=Counter(p for p,_ in patches).most_common(1)[0][0]
            bx,by=next(pos for p,pos in patches if p==best)
        else: bx,by=base_rect[:2];best=tuple(img.at(bx+i,by+j) for j in range(16) for i in range(16))
        palette=set(best)
        plain={}
        for ty in range(h):
            for tx in range(w):
                plain[tx,ty]=all(img.at(tx*8+i,ty*8+j) in palette for j in range(8) for i in range(8))
        def pixel(wx,wy):
            # wx,wy are world positions in source pixels. Plain floor keeps
            # one continuous pattern phase; only source art is redirected.
            x,y=wx+ART,wy+ART
            sy=y+(ART if any(a*8<=wx<c*8 and left*8<=wy<right*8 for a,c,left,right in side_doors) else 0)
            tx,ty=x/8,sy/8
            if self.name=='water_club' and 2<=ty<18:
                # Repaint the old projected pool/coping where the real pool
                # was pulled back to leave the source's dry walking lane.
                left=max(3,13-int(ty))
                right=18 if ty<11 else 14
                if left<=tx<right:
                    return material_pixel(self.name,x,y,img.at(bx+x%16,by+y%16),True)
            if any(p.x<=tx<p.x+p.w and p.y<=ty<p.y+p.h for p in erase) or (exclude_emblem and 8<=tx<12 and 6<=ty<10):
                return material_pixel(self.name,x,y,img.at(bx+x%16,by+y%16),True)
            return material_pixel(self.name,x,y,img.at(x,sy),plain.get((int(tx),int(ty)),False))
        self.floor_pixel=pixel
        # Floor runs from the west/north wall faces to beneath the east wall,
        # and south to the room's edge for the south doorways.
        for z in range(1,h-1,4):
            for x in range(1,w-2,4):
                cw=min(4,w-2-x);cd=min(4,h-1-z)
                rows=[[pixel(x*8+i*cw/4,z*8+j*cd/4) for i in range(32)] for j in range(32)]
                tex=texture(rows)
                # Four quads share the same tiny texture, adding room-scale
                # light gradients without another material or runtime light.
                divisions=4 if self.name=='water_club' else 2
                for iz in range(divisions):
                    for ix in range(divisions):
                        a,b=x+cw*ix/divisions,z+cd*iz/divisions;c,d=a+cw/divisions,b+cd/divisions
                        if self.name=='water_club' and (int((a+c)/2),int((b+d)/2)) in POOL_CELLS:continue
                        points=[(a,0,b),(c,0,b),(c,0,d),(a,0,d)]
                        self.polygon(points,tex=tex,
                            uv=[(ix/divisions,iz/divisions),((ix+1)/divisions,iz/divisions),((ix+1)/divisions,(iz+1)/divisions),(ix/divisions,(iz+1)/divisions)])
        if self.name=='water_club':swimming_pool(self)
    def save(self,path):
        if self.bounds and not self.lighting_baked:
            bake_lighting(self,*self.bounds);self.lighting_baked=True
        tex=sorted({key[2] for key in self.batches if key[2] is not None})
        with Path(path).open('wb') as f:
            f.write(struct.pack('>4sHH',b'L643',len(self.batches),len(tex)))
            for name in tex:f.write((name+'.sprite').encode().ljust(32,b'\0'))
            for (model,part,t,pivot),(vs,inds,_) in sorted(self.batches.items(),key=lambda e:(e[0][0],e[0][1],e[0][2] or '',e[0][3])):
                assert len(vs)<65536
                f.write(struct.pack('>HHhHII3f',model,part,tex.index(t) if t else -1,0,len(vs),len(inds),*pivot))
                for v in vs:f.write(struct.pack('>5f4B',*v))
                f.write(struct.pack('>'+str(len(inds))+'H',*inds));f.write(b'\0'*((-f.tell())%4))
        return dict(file=str(Path(path).relative_to(OUT)),vertices=sum(len(v[0]) for v in self.batches.values()),triangles=sum(len(v[1])//3 for v in self.batches.values()),batches=len(self.batches),textures=tex,props=self.props)


def prop(destination,img,p):
    scene=Scene(destination.name)
    scene.switchable=destination.switchable
    source_p=p
    switch=destination.switchable and p.kind=='machine' and (
        (p.y==1 and p.x in (6,10,14,18)) or
        (p.y in (8,16) and p.x in (14,18)))
    record=asdict(p)
    bx,bz,bw,bd=footprint(p)
    record['solid_bounds']=[bx,bz,bx+bw,bz+bd]
    if scene.name=='water_club' and p.kind=='bench' and p.x==20:
        record['supports_npcs']=[34]  # Amy lies/stands on this bench.
    honor=scene.name=='hall_of_honor' and p.kind in ('altar','pillar','ball','legendary_machine')
    face=None if honor else texture(img.crop(p.x*8,p.y*8,p.w*8,p.h*8))
    # Crop/erase rectangles describe projected GBC art. The floor body is
    # authored independently against the original even-coordinate step grid.
    p=replace(p,x=bx,y=bz,w=bw,h=bd,depth=bd)
    x=p.x+p.w/2; z=p.y+p.h/2
    kind=p.kind;h=p.height
    if honor:
        build_honor_prop(scene,p)
    elif build_furniture_prop(scene,p,source_p=source_p,img=img,texture=texture):pass
    elif build_lab_prop(scene,p,source_p=source_p,img=img,texture=texture):pass
    elif build_decorative_prop(scene,p,source_p=source_p,img=img,texture=texture):pass
    elif kind in ('machine','cabinet','counter','pillar','altar'):
        omit=(0,)+(3,) if switch else (0,)
        if kind=='machine':omit+=(5,)
        hole=(p.x+.6,z-p.depth/2+.6,p.x+p.w-.6,z+p.depth/2-.6) if kind=='altar' else None
        scene.box(p.x,z-p.depth/2,p.w,p.depth,h,BROWN,front=None if switch else face,omit=omit,top_hole=hole)
        if kind=='machine':
            scene.box(p.x-.05,z-p.depth/2-.05,p.w+.1,p.depth+.1,.12,WHITE,y=h,omit=(0,))
        if kind=='altar':
            scene.box(p.x+.6,z-p.depth/2+.6,p.w-1.2,p.depth-1.2,.5,WHITE,y=h,omit=(0,))
    elif kind in ('table','bed','steps'):
        if kind=='bed':scene.box(p.x,p.y,p.w,p.h,h,DARKGREEN,tex=face)
        else:
            depth=min(p.depth,p.h)
            if kind=='table':
                scene.box(p.x,z-depth/2,p.w,depth,.15,BROWN,y=h-.15,top=face)
                for a,b in ((p.x+.2,z-depth/2+.2),(p.x+p.w-.4,z-depth/2+.2),(p.x+.2,z+depth/2-.4),(p.x+p.w-.4,z+depth/2-.4)):
                    scene.box(a,b,.22,.22,h-.15,BROWN,omit=(0,5))
            else:
                # Steps are a single body. The old full box plus shallow
                # tabletop had coincident side strips and competing caps.
                scene.box(p.x,z-depth/2,p.w,depth,h,BROWN,top=face,omit=(0,))
    elif kind in ('palm','topiary','shrub','flowers'):
        scene.cylinder(x,z,0,.52,.5,BROWN,top_r=.65)
        if kind=='palm':
            # Fronds can overhang the footpath only above a person's head.
            h=max(h,3.25)
            scene.cylinder(x,z,.45,.16,h-.65,BROWN,segments=6)
            for i in range(6):
                a=i*math.tau/6
                dx,dz=math.cos(a),math.sin(a)
                center=(x,h-.2,z)
                points=[center,(x+dx*.85-dz*.3,h+.2,z+dz*.85+dx*.3),
                        (x+dx*1.6,h-.6,z+dz*1.6),(x+dx*.85+dz*.3,h+.2,z+dz*.85-dx*.3)]
                scene.polygon(points,GREEN if i%2 else DARKGREEN)
        else:
            if kind=='topiary':scene.cylinder(x,z,.45,.12,.6,BROWN,segments=6)
            scene.ball(x,z,h*.7,.8,h*.45,.8,GREEN if kind!='flowers' else rgb('ffb5ff'))
    elif kind in ('boulder','statue','serpent','horse'):
        if kind=='boulder':
            scene.ball(x,z,h*.5,p.w*.48,h*.5,p.depth*.48,rgb('968c80') if p.w<=1 else rgb('ad8460'),segments=6 if p.w<=1 else 8,rings=3 if p.w<=1 else 4)
            if p.w>2:scene.ball(x-.7,z+.3,h*.3,p.w*.3,h*.3,p.depth*.35,rgb('efa518'))
        elif kind=='statue':
            scene.box(p.x,z-.7,p.w,1.4,.4,GOLD,front=face)
            scene.cylinder(x,z,.4,.45,h*.45,GOLD,top_r=.3)
            scene.ball(x,z,h*.8,.65,h*.25,.55,GOLD)
        elif kind=='serpent':
            scene.ball(x,z,.35,.7,.35,.55,RED)
            scene.cylinder(x,z,.4,.25,.6,BLUE,segments=6,top_r=.17)
            scene.ball(x,z-.2,.95,.4,.3,.35,RED)
        else:
            scene.box(x-.65,z-.3,1.3,.6,.5,WHITE,y=.5)
            for dx in (-.5,.5):
                for dz in (-.2,.2):scene.box(x+dx-.08,z+dz-.08,.16,.16,.55,GOLD)
            scene.box(x-.7,z-.25,.3,.5,.8,WHITE,y=.7)
            scene.box(x-.75,z-.28,.4,.56,.1,RED,y=1.45)
    elif kind in ('brazier','torch'):
        scene.cylinder(x,z,0,.32,h*.63,rgb('65533c') if kind=='brazier' else BLACK,top_r=.20)
        scene.cylinder(x,z,h*.60,.26,h*.15,rgb('a18a5d'),top_r=.52)
        flame=rgb('ac70dc') if scene.name=='psychic_club' else RED
        core=rgb('e6c8ff') if scene.name=='psychic_club' else rgb('ffe59c')
        scene.cylinder(x,z,h*.73,.36,h*.35,flame,segments=6,top_r=0)
        scene.cylinder(x-.06,z-.03,h*.74,.19,h*.24,core,segments=5,top_r=0)
        scene.cylinder(x+.19,z+.04,h*.76,.14,h*.25,flame,segments=4,top_r=0)
    elif kind=='balloon':
        scene.cylinder(x,z,0,.04,h*.7,BLACK,segments=4)
        scene.ball(x,z,h*.85,.34,h*.25,.34,rgb('e94ac9'))
    elif kind=='ball':scene.ball(x,z,3.05,.75,.75,.75,rgb('000052'))
    elif kind=='chair':
        sx=min(1,p.w/1.4);sz=min(1,p.depth/1.4)
        scene.box(x-.6*sx,z-.6*sz,1.2*sx,1.2*sz,.15,RED,y=.5)
        scene.box(x-.6*sx,z-.7*sz,1.2*sx,.2*sz,.6,RED,y=.55)
        for dx,dz in ((-.45,-.45),(.45,-.45),(-.45,.45),(.45,.45)):
            scene.box(x+dx*sx,z+dz*sz,.14*sx,.14*sz,.5,BROWN)
    elif kind=='bench':
        scene.box(p.x,z-.7,p.w,1.4,.16,WHITE,y=.55)
        for i in range(max(2,int(p.w)*2)):
            scene.box(p.x+i*.45,z-.75,.3,.18,.7,WHITE,y=.6)
        scene.box(p.x+.3,z-.5,.25,1,.6,BROWN)
        scene.box(p.x+p.w-.55,z-.5,.25,1,.6,BROWN)
    elif kind=='weight':
        scene.box(x-.75,z-.1,1.5,.2,.18,rgb('9494c6'),y=.12)
        for dx in (-.7,.55):scene.box(x+dx,z-.35,.22,.7,.65,BLACK)
    elif kind=='rack':
        metal=rgb('66747d')
        for px in (p.x+.6,p.x+p.w-.6):
            scene.box(px-.12,z-.45,.24,.9,h,metal)
            scene.box(px-.45,z-.65,.9,1.3,.12,BLACK)
        scene.box(p.x,z-.07,p.w,.14,.14,metal,y=h*.70)
        for i in range(6):
            px=p.x+1+i*(p.w-2)/5
            for dx in (-.18,.18):scene.ball(px+dx,z,h*.70,.13,.40,.40,BLACK,segments=6,rings=3)

    # Keep the validator tied to the exported body rather than an approximate
    # authoring rectangle. High palm fronds and the floating honor-room orb
    # do not occupy a walking character's height range.
    low=[v for vs,_,_ in scene.batches.values() for v in vs if -.01<=v[1]<=2.3]
    if low:
        record['solid_bounds']=[min(v[0] for v in low),min(v[2] for v in low),
                                max(v[0] for v in low),max(v[2] for v in low)]
    destination.props.append(record)
    for key,(vertices,indices,_) in scene.batches.items():
        vs,inds,cache=destination.batches[key]
        remap=[]
        for v in vertices:
            if v not in cache:cache[v]=len(vs);vs.append(v)
            remap.append(cache[v])
        inds.extend(remap[i] for i in indices)


def walls(scene,img,warps,height=5):
    w,h=img.width/8,img.height/8
    if scene.name=='psychic_club':h=28
    if scene.name in ('grass_club','challenge_hall'):height=2
    if scene.name=='pokemon_dome_entrance':height=3.5
    # Sample south livery so north cabinetry doesn't get duplicated on walls.
    wall_tex=texture(img.crop(16,(h-3)*8,32,16,32,16))
    cornice_tex=texture(img.crop(16,(h-3)*8,32,8,32,8))
    paint=wall_paint(scene.name)
    brown=img.at(4,4)
    if max(brown[:3])<20:brown=rgb('521800')
    east=w-EAST_WALL;south=h-SOUTH_WALL
    # Fighting's barbell racks stand on the blocked row against its south
    # wall; the wall stays behind them.
    south=max([south]+[z+d+.1 for _,z,_,d in map(footprint,PROPS.get(scene.name,[]))])
    # Tall walls close above each doorway, clear of every actor's head
    # (the tallest actor model is 2.55 units).
    lintel=2.8 if height>3.4 else height
    def runs(side,stop):
        start=.9;end=stop-.9
        for left,right in doors(warps,w,h,side):
            if left>start:yield start,min(left,end)
            start=max(start,right)
            if start>=end:return
        if start<end:yield start,end
    def bands(side,start,end,low=0):
        # Source livery is wainscot and cornice, never a wall-sized stripe.
        # U is absolute so the livery continues across each lintel.
        def face(lo,hi,color=WHITE,tex=None):
            if hi<=low:return
            lo=max(lo,low)
            if side=='north':points=[(start,lo,.93),(end,lo,.93),(end,hi,.93),(start,hi,.93)]
            else:
                ix=.93 if side=='west' else east-.03
                points=[(ix,lo,start),(ix,lo,end),(ix,hi,end),(ix,hi,start)]
            scene.polygon(points,color,tex,[(start/2,1),(end/2,1),(end/2,0),(start/2,0)])
        waist=min(1.45,height-.4)
        face(0,waist,tex=wall_tex)
        face(waist,waist+.065,GOLD)
        if height>waist+.4:
            # Split at the lintel so every run shares whole edges with the
            # paint above a doorway; T-junctions open pixel cracks.
            for lo,hi in ((waist+.065,lintel),(lintel,height-.3)) if waist<lintel<height-.3 else ((waist+.065,height-.3),):
                face(lo,hi,paint)
        face(height-.3,height,tex=cornice_tex)
    def art(left,north,right,south_edge):
        # The floor's own source art, for surfaces that continue it.
        tw=min(64,2**math.ceil(math.log2(max(1,(right-left)*8))))
        th=min(32,2**math.ceil(math.log2(max(1,(south_edge-north)*8))))
        return texture([[scene.floor_pixel((left+(i+.5)*(right-left)/tw)*8,(north+(j+.5)*(south_edge-north)/th)*8)
                         for i in range(tw)] for j in range(th)])
    def flat(left,north,right,south_edge,y=0):
        scene.polygon([(left,y,north),(right,y,north),(right,y,south_edge),(left,y,south_edge)],
                      tex=art(left,north,right,south_edge))
    # One continuous face per uninterrupted wall. Adjacent antialiased quads
    # exposed their backing along every two-tile seam; repeat UVs preserve the
    # original livery without geometric cracks or coincident internal caps.
    def solid_wall(x,z,width,depth,high):
        scene.props.append(dict(kind='wall',x=x,y=z,w=width,h=depth,height=high,
                                depth=depth,solid=True,solid_bounds=[x,z,x+width,z+depth]))
    for x,end in runs('north',w-2):
        scene.box(x,-.1,end-x,1,height,brown,omit=(0,3))
        solid_wall(x,-.1,end-x,1.03,height)
        bands('north',x,end)
    # The low south wall's top carries the source's south livery, which was
    # previously painted on the floor in front of a thinner wall.
    for x,end in runs('south',w-2):
        scene.box(x,south,end-x,h-1.9-south,.9,brown,omit=(0,5))
        # At most eight tiles per texture keeps one texel per source pixel.
        pieces=math.ceil((end-x)/8)
        for i in range(pieces):
            a,c=x+(end-x)*i/pieces,x+(end-x)*(i+1)/pieces
            flat(a,south,c,h-1.9,y=.9)
        solid_wall(x,south,end-x,h-1.9-south,.9)
    for side,x in [('west',-.1),('east',east)]:
        for z,end in runs(side,h):
            span=end-z
            scene.box(x,z,1,span,height,brown,omit=(0,2) if side=='west' else (0,4))
            solid_wall(x if side=='west' else x-.03,z,1.03,span,height)
            bands(side,z,end)
    # Each opening in a tall wall is a short passage: the floor continues
    # through the wall to a dark doorway, between jambs and under a lintel.
    # The doorway darkens upward, as a passage lit from the room.
    shade=[rgb('5a4434'),rgb('5a4434'),rgb('22180f'),rgb('22180f')]
    for left,right in doors(warps,w,h,'north'):
        if lintel<height:
            scene.box(left,-.1,right-left,1,height-lintel,brown,y=lintel,omit=(0,2,3,4))
            bands('north',left,right,lintel)
        flat(left,-.9,right,1)
        for x in (left,right):
            scene.polygon([(x,0,-.9),(x,0,-.1),(x,lintel,-.1),(x,lintel,-.9)],brown)
        scene.polygon([(left,0,-.9),(right,0,-.9),(right,lintel,-.9),(left,lintel,-.9)],vertex_colors=shade)
    for side,inner,outer,back,floor_edge in (('west',.9,-.1,-.9,1),('east',east,east+1,east+1.8,w-2)):
        for left,right in doors(warps,w,h,side):
            if lintel<height:
                omit=(0,1,2,3) if side=='west' else (0,1,3,4)
                scene.box(min(inner,outer),left,1,right-left,height-lintel,brown,y=lintel,omit=omit)
                bands(side,left,right,lintel)
            flat(min(back,floor_edge),left,max(back,floor_edge),right)
            for z in (left,right):
                scene.polygon([(outer,0,z),(back,0,z),(back,lintel,z),(outer,lintel,z)],brown)
            scene.polygon([(back,0,left),(back,0,right),(back,lintel,right),(back,lintel,left)],vertex_colors=shade)


def terrain(scene,img):
    """Continuous island ground and small, recognizable source landmarks.

    The original tile-by-tile columns left coincident black internal walls and
    partial-coverage cracks across every tile. Only coastline edges have walls
    now. Projected roofs, tree silhouettes, and the old name panel are not
    reused as ground textures underneath their real geometry.
    """
    sea=rgb('4a18ff');grass=rgb('42d600');cliff=rgb('183621')
    cream=rgb('fff7de');navy=rgb('181842');ochre=rgb('ce9431')
    forest=rgb('087b00');leaf=rgb('299c18');red=rgb('ff1029')
    ground=.36
    scene.box(0,0,20,18,.16,sea,y=-.16,omit=(0,))

    # Two source pixels per cell retain the original coast while allowing
    # merged planar ground instead of hundreds of disconnected terrain boxes.
    size=.25;cols,rows=80,72
    land=set()
    for z in range(rows):
        for x in range(cols):
            pixels=[img.at(x*2+i,z*2+j) for j in range(2) for i in range(2)]
            count=sum((g>r*1.3 and g>b*1.3) or (r>100 and 40<g<190 and b<100)
                      for r,g,b,a in pixels)
            if count>=1:land.add((x,z))
    # The source buildings are opaque white/black icons. Their occupied ground
    # must remain present after those painted icons have been removed.
    footprints=[(1,3.7,1.6,1.1),(7.2,3.8,2.1,2),(13.3,1,4,3.4),
                (15.4,4.6,2.1,1.2),(12,5.8,.85,2),(15.4,8.6,2.4,1.1),
                (4.8,10.5,1.7,1.1),(1.2,13.6,2.3,1.3),(6.8,13.5,1.5,1.1),
                (13.4,12.8,2.2,2.2),(8.5,8.4,2.1,2)]
    for x,z,w,d in footprints:
        for iz in range(max(0,int(z/size)),min(rows,math.ceil((z+d)/size))):
            for ix in range(max(0,int(x/size)),min(cols,math.ceil((x+w)/size))):land.add((ix,iz))
    # Fill enclosed gaps from icon outlines, then remove the little colored
    # boat pixels: boats are their own geometry, rather than square islands.
    outside=set();todo=[(0,0)]
    while todo:
        x,z=todo.pop()
        if not (0<=x<cols and 0<=z<rows) or (x,z) in land or (x,z) in outside:continue
        outside.add((x,z));todo.extend(((x-1,z),(x+1,z),(x,z-1),(x,z+1)))
    land.update((x,z) for z in range(rows) for x in range(cols) if (x,z) not in outside)
    unseen=set(land)
    while unseen:
        first=unseen.pop();component={first};todo=[first]
        while todo:
            x,z=todo.pop()
            for p in ((x-1,z),(x+1,z),(x,z-1),(x,z+1)):
                if p in unseen:unseen.remove(p);component.add(p);todo.append(p)
        if len(component)<10:land.difference_update(component)

    # Greedily merge the top into rectangles. There are no black faces between
    # neighbors, and shared terrain edges have exactly the same elevation.
    remaining=set(land)
    while remaining:
        x,z=min(remaining,key=lambda p:(p[1],p[0]));w=1
        while (x+w,z) in remaining:w+=1
        d=1
        while all((ix,z+d) in remaining for ix in range(x,x+w)):d+=1
        for iz in range(z,z+d):
            for ix in range(x,x+w):remaining.remove((ix,iz))
        a,b,c,e=x*size,z*size,(x+w)*size,(z+d)*size
        scene.polygon([(a,ground,b),(c,ground,b),(c,ground,e),(a,ground,e)],grass)
    for x,z in sorted(land):
        a,b=x*size,z*size;c,e=a+size,b+size
        for neighbor,edge in [((x,z-1),[(a,0,b),(c,0,b),(c,ground,b),(a,ground,b)]),
                              ((x+1,z),[(c,0,b),(c,0,e),(c,ground,e),(c,ground,b)]),
                              ((x,z+1),[(c,0,e),(a,0,e),(a,ground,e),(c,ground,e)]),
                              ((x-1,z),[(a,0,e),(a,0,b),(a,ground,b),(a,ground,e)])]:
            if neighbor not in land:scene.polygon(edge,cliff)

    # Tiny waves and subtle grass flecks supply the source's broad texture
    # without projecting its grid, roofs, or trees onto the ground a second time.
    for z in range(2,142):
        for x in range(2,158):
            if (x//2,z//2) in land:continue
            if img.at(x,z)==(255,255,255,255) and img.at(x-1,z)!=img.at(x,z) and img.at(x,z-1)!=img.at(x,z) and sum(img.at(x+i,z+j)==sea for i,j in ((-2,0),(2,0),(0,-2),(0,2)))>=3:
                scene.polygon([(x/8-.08,.015,z/8),(x/8+.13,.015,z/8),(x/8,.015,z/8-.09)],cream)
    for x,z in sorted(land):
        if (x*37+z*71)%29:continue
        px,pz=(x+.4)*size,(z+.4)*size
        if any(a-.12<px<a+w+.12 and b-.12<pz<b+d+.12 for a,b,w,d in footprints):continue
        if (px<8 and 6.7<pz<11.1) or (px>13.6 and 4.8<pz<12):continue
        color=rgb('75de31') if (x+z)%3 else rgb('31b510')
        scene.polygon([(px,ground+.012,pz),(px+.065,ground+.012,pz),(px+.03,ground+.012,pz+.08)],color)

    def box(x,z,w,d,h,color,y=ground):
        scene.box(x,z,w,d,h,color,y=y,omit=(0,))
    def roof(x,z,w,d,y,rise,color):
        ridge=z+d*.5
        scene.polygon([(x,y,z),(x+w,y,z),(x+w,y+rise,ridge),(x,y+rise,ridge)],color)
        scene.polygon([(x,y+rise,ridge),(x+w,y+rise,ridge),(x+w,y,z+d),(x,y,z+d)],tuple(int(v*.8) for v in color[:3])+(255,))
        scene.polygon([(x,y,z),(x,y+rise,ridge),(x,y,z+d)],color)
        scene.polygon([(x+w,y,z+d),(x+w,y+rise,ridge),(x+w,y,z)],color)
    def door(x,z,w,d,y=ground):
        # The dark inset is visible both on the front and from above.
        box(x,z,w,d,.06,navy,y=y)
    def ladder(x,z,w=.35):
        for dx in (0,w):box(x+dx,z,.055,.8,.04,cream,y=ground+.025)
        for i in range(4):box(x,z+i*.22,w,.055,.045,cream,y=ground+.04)

    # Ishihara: the little tan hut and its white roof edge.
    box(1.05,3.8,1.3,.8,.55,ochre)
    roof(.96,3.69,1.5,1.02,ground+.53,.28,cream)
    roof(1.12,3.72,1.18,.93,ground+.56,.25,ochre)
    door(1.45,4.5,.37,.12)
    # Mason's laboratory: low white wings, dark roof, and blue windows.
    box(1.35,13.65,2.15,1.05,.58,cream)
    box(1.2,13.72,.55,.75,.36,cream);box(3.1,13.8,.65,.65,.32,cream)
    box(1.45,13.7,1.95,.9,.08,navy,y=ground+.58)
    for x in (1.52,2.78):box(x,14.66,.42,.03,.21,BLUE,y=ground+.23)
    door(2.22,14.6,.42,.16)
    # Fighting Club's unmistakable red, two-tier pagoda roof.
    box(7.0,13.8,1.25,.85,.48,cream)
    roof(6.8,13.65,1.65,1.1,ground+.46,.38,cream)
    roof(6.92,13.67,1.41,1.06,ground+.49,.35,red)
    box(7.35,13.96,.55,.43,.37,cream,y=ground+.72)
    roof(7.17,13.85,.9,.65,ground+1.07,.28,red)
    door(7.45,14.57,.35,.15)
    # The rocky west hill is one low, faceted mass with a cave entrance.
    edge=[(1.15,8),(1.5,7.3),(3.0,7),(5.8,7.1),(7.25,8.1),(7.65,9.4),
          (6.5,10.55),(4.3,11.05),(2.2,10.7),(1.25,9.4)]
    center=(4.1,.87,8.95)
    for i,(x,z) in enumerate(edge):
        nx,nz=edge[(i+1)%len(edge)]
        color=ochre if i%3 else rgb('b58431')
        scene.polygon([(x,ground+.02,z),(nx,ground+.02,nz),center],color)
    box(2.8,7.75,1.35,.95,.25,ochre,y=.62)
    box(2.93,7.9,1.08,.68,.035,navy,y=.89)
    box(2.83,7.8,1.28,.15,.06,cream,y=.88)
    # Lightning: a yellow framed hall with a dark roof.
    box(4.95,10.55,1.55,1.0,.55,cream)
    box(5.08,10.62,1.29,.82,.07,navy,y=ground+.55)
    box(5.08,11.46,1.29,.07,.3,GOLD,y=ground+.15)
    door(5.52,11.47,.35,.12)
    # Psychic: the slim purple tower, separated by white bands.
    box(12.05,5.9,.62,1.45,.88,cream)
    box(12.16,6.02,.4,1.2,.05,rgb('6331ad'),y=ground+.88)
    box(12.11,6.55,.5,.14,.08,cream,y=ground+.9)
    roof(12.01,5.85,.7,.5,ground+.88,.3,rgb('6331ad'))
    # Science and Grass have green roofs instead of generic white domes.
    for x,z,w,d in ((15.45,4.62,1.9,1.0),(15.5,8.6,2.35,.85)):
        box(x,z,w,d,.53,cream)
        roof(x-.07,z-.05,w+.14,d+.1,ground+.52,.22,forest)
        door(x+w*.4,z+d-.06,w*.22,.15)
    # Challenge Hall: round brown tower, dark flat roof, white rim and ladder.
    scene.cylinder(8.35,4.65,ground,.8,.75,BROWN,segments=12)
    scene.cylinder(8.35,4.65,ground+.75,.91,.1,cream,segments=12)
    scene.cylinder(8.35,4.65,ground+.85,.74,.015,navy,segments=12)
    ladder(8.18,5.4)
    # Water Club: open, oval stadium with a green interior and white seating.
    cx,cz=14.45,13.85
    segments=16
    for i in range(segments):
        a,b=i*math.tau/segments,(i+1)*math.tau/segments
        p=(cx+1.05*math.cos(a),ground+.62,cz+.78*math.sin(a))
        q=(cx+1.05*math.cos(b),ground+.62,cz+.78*math.sin(b))
        ip=(cx+.7*math.cos(a),ground+.56,cz+.5*math.sin(a))
        iq=(cx+.7*math.cos(b),ground+.56,cz+.5*math.sin(b))
        scene.polygon([p,q,iq,ip],cream)
        scene.polygon([(p[0],ground,p[2]),(q[0],ground,q[2]),q,p],rgb('b5c6de'))
    box(cx-.7,cz-.5,1.4,1,.06,forest,y=ground+.48)
    ladder(cx-.18,cz+.8)
    # Fire Club: the source's tan mountain and red banner at its entrance.
    mountain=[(14.7,1.3),(15.35,.9),(16.4,1.55),(17.25,2.3),(17.2,3.75),(15.4,4.0),(14.5,3.1)]
    peak=(15.8,2.0,1.7)
    for i,(x,z) in enumerate(mountain):
        nx,nz=mountain[(i+1)%len(mountain)]
        scene.polygon([(x,ground,z),(nx,ground,nz),peak],ochre if i%2 else rgb('a56321'))
    scene.polygon([(15.8,2.02,1.7),(16.75,ground+.02,3.65),(16.25,ground+.02,3.5)],navy)
    box(13.65,2.4,1.1,.95,.47,ochre)
    box(13.64,2.41,.06,.08,1.3,cream)
    scene.polygon([(13.72,ground+1.3,2.46),(14.65,ground+1.3,2.46),
                   (14.65,ground+.83,2.46),(13.72,ground+.83,2.46)],red)
    door(13.9,3.18,.5,.13)
    # Pokemon Dome: a compact Pokeball roof on a real stadium body.
    cx,cz=9.5,9.45
    scene.cylinder(cx,cz,ground,1.0,.55,cream,segments=16)
    scene.cylinder(cx,cz,ground+.55,1.06,.07,navy,segments=16)
    for half,color in ((0,red),(1,cream)):
        points=[(cx,ground+.66,cz)]
        points.extend((cx+1.0*math.cos((i+half*8)*math.pi/8),ground+.66,
                       cz+.84*math.sin((i+half*8)*math.pi/8)) for i in range(9))
        scene.polygon(points,color)
    box(cx-.99,cz-.07,1.98,.14,.025,navy,y=ground+.68)
    scene.cylinder(cx,cz,ground+.71,.18,.025,cream,segments=8)
    door(cx-.2,cz+.96,.4,.15)

    def tree(x,z,height=.7):
        scene.cylinder(x,z,ground,.065,.2,BROWN,segments=4)
        scene.cylinder(x,z,ground+.16,.29,height*.65,forest,segments=6,top_r=0)
        scene.cylinder(x,z,ground+height*.4,.23,height*.65,leaf,segments=6,top_r=0)
    # Keep the eastern forest as real conifers, without its old black tile art.
    for iz in range(9):
        z=5.1+iz*.77
        for ix in range(6):
            x=14.1+ix*.78+(iz%2)*.2
            if (int(x/size),int(z/size)) not in land:continue
            if any(px-.15<x<px+w+.15 and pz-.15<z<pz+d+.15 for px,pz,w,d in footprints):continue
            tree(x,z,.6+((ix*3+iz)%4)*.06)
    for x,z in ((5.4,6.3),(6.6,7.25),(5.5,9.5),(9.55,11.35),(11.5,10.5),
                (13.35,6.5),(4.5,14.5),(3.55,15.7),(17.75,13.5),(18.3,12.0)):
        tree(x,z,.7)
    # The two source boats remain tiny props; neither becomes terrain columns.
    for x,z in ((1.15,12.0),(18.6,16.3)):
        scene.cylinder(x,z,.03,.36,.12,BROWN,segments=4,top_r=.45)
        scene.cylinder(x,z,.16,.13,.25,ochre,segments=4,top_r=.05)


def read_l643(path):
    blob=Path(path).read_bytes();magic,count,nt=struct.unpack_from('>4sHH',blob)
    assert magic==b'L643';offset=8
    tex=[]
    for _ in range(nt):
        tex.append(blob[offset:offset+32].split(b'\0')[0].decode());offset+=32
    meshes=[]
    for _ in range(count):
        model,part,t,res,nv,ni,*pivot=struct.unpack_from('>HHhHII3f',blob,offset);offset+=28
        vs=[struct.unpack_from('>5f4B',blob,offset+i*24) for i in range(nv)];offset+=nv*24
        inds=struct.unpack_from('>'+str(ni)+'H',blob,offset);offset+=ni*2;offset=(offset+3)&~3
        meshes.append((model,part,t,pivot,vs,inds))
    assert offset==len(blob)
    return tex,meshes


def copy_lightning():
    assets=REPO/'port/frontends/n64/lightning/assets'
    tex,meshes=read_l643(assets/'scene.l64')
    remap={}
    for i,name in enumerate(tex):
        # Floor quads are repainted below, from the source map.
        if name.startswith('floor_'):continue
        remap[i]=texture(Image(assets/'textures'/name.replace('.sprite','.png')).rows)
    room,actors=Scene('lightning_club'),Scene('actors')
    meshes,_=strip_legacy_disks(meshes,{1,2,3,4})
    # The prototype painted each 4x4 floor quad with the source tiles at its
    # own position. Repaint each quad from source art one tile east/south,
    # as every other room does, so the runner meets the anchored doorway.
    source=Image(REPO/'port/maps/lightning_club.png')
    def floor_pixel(x,y):
        # The prototype's repaint: carpet beneath the pillars, cream apron.
        # Its one-tile blue/black grid aliases at native distance, so it
        # takes the shared broad material; the runner keeps its border.
        x,y=min(207,x),min(223,y);tx,ty=x//8,y//8
        if (8<=tx<10 or 17<=tx<20) and 14<=ty<26:p=source.at(16+x%16,128+y%16)
        elif ((4<=tx<6 or 22<=tx<24) and 3<=ty<7) or (8<=tx<20 and 2<=ty<5):p=(255,255,173,255)
        else:p=source.at(x,y)
        return material_pixel('lightning_club',x,y,p,p[:3] in ((24,0,165),(0,0,33)))
    floor_tex={}
    for model,part,t,pivot,vs,inds in meshes:
        scene=actors if model else room
        if not model and t>=0 and tex[t].startswith('floor_'):
            for offset in range(0,len(inds),3):
                triangle=[vs[i] for i in inds[offset:offset+3]]
                origin=(round(min(v[0] for v in triangle)),round(min(v[2] for v in triangle)))
                if origin not in floor_tex:
                    ox,oz=origin
                    floor_tex[origin]=texture([[floor_pixel((ox*8+ART)+i,(oz*8+ART)+j) for i in range(32)] for j in range(32)])
                key=(model,part,floor_tex[origin],tuple(pivot))
                batch,elements,cache=scene.batches.setdefault(key,([],[],{}))
                for v in triangle:
                    if v not in cache:cache[v]=len(batch);batch.append(v)
                    elements.append(cache[v])
            continue
        key=(model,part,remap[t] if t>=0 else None,tuple(pivot))
        scene.batches[key]=(list(vs),list(inds),{})
    room.bounds=(28,32)
    for x,z,right,bottom in json.loads((assets/'scene.json').read_text())['colliders']:
        room.props.append(asdict(Prop('pillar',x,z,right-x,bottom-z,2.5,bottom-z)))
    # Non-human legendary-card/spark actors are actual small meshes.
    card=Scene('cards')
    # Gold border and blue center partition one card exterior. The old pair
    # of complete boxes left near-coplanar front/back layers only .01 apart.
    xs=(-.32,-.25,.25,.32);ys=(.12,.22,.92,1.02)
    for side in (-1,1):
        for row in range(3):
            for col in range(3):
                points=[(xs[col],ys[row],side*.08),(xs[col+1],ys[row],side*.08),
                        (xs[col+1],ys[row+1],side*.08),(xs[col],ys[row+1],side*.08)]
                if side<0:points.reverse()
                card.polygon(points,BLUE if row==col==1 else GOLD)
    for row in range(3):
        for x in (xs[0],xs[-1]):
            points=[(x,ys[row],-.08),(x,ys[row],.08),(x,ys[row+1],.08),(x,ys[row+1],-.08)]
            if x>0:points.reverse()
            card.polygon(points,GOLD)
    for col in range(3):
        for y in (ys[0],ys[-1]):
            points=[(xs[col],y,-.08),(xs[col+1],y,-.08),(xs[col+1],y,.08),(xs[col],y,.08)]
            if y>ys[0]:points.reverse()
            card.polygon(points,GOLD)
    for (m,p,t,pivot),data in card.batches.items():actors.batches[(5,p,t,pivot)]=data
    # Overworld cursor ball; root places/bobs it above the selected location.
    ball=Scene('cursor')
    # A single spherical exterior owns the red/black/white regions. A smaller
    # red ellipsoid over a complete white ball used to cross the white shell.
    segments=12;radius=.7;center=.8
    latitudes=(-math.pi/2,-math.pi/4,-math.pi/30,math.pi/30,math.pi/4,math.pi/2)
    rings=[[(radius*math.cos(a)*math.cos(i*math.tau/segments),
             center+radius*math.sin(a),radius*math.cos(a)*math.sin(i*math.tau/segments))
            for i in range(segments)] for a in latitudes]
    for band,(lower,upper) in enumerate(zip(rings,rings[1:])):
        pigment=WHITE if band<2 else BLACK if band==2 else RED
        for i in range(segments):
            j=(i+1)%segments
            points=[lower[i],upper[i],upper[j],lower[j]]
            if band==0:points=[lower[i],upper[i],upper[j]]
            elif band==len(rings)-2:points=[lower[i],upper[i],lower[j]]
            a,b,c=points[:3];u=[b[k]-a[k] for k in range(3)];v=[c[k]-a[k] for k in range(3)]
            normal=(u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0])
            length=math.sqrt(sum(value*value for value in normal))
            light=.76+.24*max(0,sum(n*s for n,s in zip(normal,(-.4,.7,.55)))/length)
            ball.polygon(points,tuple(round(value*light) for value in pigment[:3])+(255,))
    for (m,p,t,pivot),data in ball.batches.items():actors.batches[(6,p,t,pivot)]=data
    align_lightning_room(room)
    return room,actors


def load_metadata():
    game=Game(str(REPO));consts=game.consts['map_constants.asm']
    names=[None]*34
    for name,value in consts.items():
        if value<34 and (name=='OVERWORLD_MAP' or name.endswith(('_CLUB','_ENTRANCE','_LOBBY')) or name in ('MASON_LABORATORY','DECK_MACHINE_ROOM','ISHIHARAS_HOUSE','CHALLENGE_HALL','POKEMON_DOME','HALL_OF_HONOR')):
            # OWMAP_* values overlap map IDs, so never take them.
            if not name.startswith('OWMAP_'):names[value]=name
    assert all(names),names
    maps=[]
    for mid,name in enumerate(names):
        slug='overworld' if mid==0 else name.lower()
        header=game.map_header(mid);tm=game.load_tilemap(header['tilemap_cgb'])
        npcs=[]
        for npc in game.map_npc_list(mid):
            npc=dict(npc)
            npc['x']=npc['x'] if npc['x']<128 else npc['x']-256
            npc['y']=npc['y'] if npc['y']<128 else npc['y']-256
            npc['name']=game.npc_names.get(npc['npc'],'DYNAMIC_OPPONENT')
            npc['conditional']=bool(npc['preload'])
            npc['heading']=(180,90,0,-90)[npc['dir']%4]
            npcs.append(npc)
        bank,base=game.rom.addr('WarpDataPointers');pointer=game.rom.word(bank,base+mid*2)
        warps=[]
        if pointer:
            for _ in range(64):
                x,y=game.rom.read(bank,pointer,2)
                if x==y==0:break
                x,y,dest,dx,dy=game.rom.read(bank,pointer,5);pointer+=5
                warps.append(dict(x=x,y=y,map=dest,dest_x=dx,dest_y=dy))
        permissions=[]
        if tm['perm']:
            d=Decompressor(game.rom,tm['bank'],tm['perm'])
            permissions=[list(d.read((tm['w']+1)//2)) for _ in range((tm['h']+1)//2)]
        maps.append(dict(id=mid,name=name,slug=slug,width=tm['w'],height=tm['h'],npcs=npcs,warps=warps,permissions=permissions))
    bank,p=game.rom.addr('OverworldMap_MapPositions');wb,wp=game.rom.addr('OverworldMapWarps')
    nodes=[]
    for i in range(1,13):
        x,y=game.rom.read(bank,p+i*2,2);mid,dx,dy,_=game.rom.read(wb,wp+i*4,4)
        nodes.append(dict(selection=i,map=mid,x=dx,y=dy,marker_x=x/8+1,marker_z=y/8+1))
    return game,maps,nodes


def actor_models(game,maps):
    females={'SARA','AMANDA','AMY','JENNIFER','NIKKI','HEATHER','KRISTIN','JESSICA','STEPHANIE','COURTNEY','BRITTANY','WOMAN','LASS','GAL','GRANNY'}
    coats={'DRMASON','ISAAC','RICK','TECH','AARON'}
    models=[2]*256;models[0]=0
    for number,name in game.npc_names.items():
        short=name.removeprefix('NPC_')
        models[number]=5 if 'LEGENDARY_CARD' in short else 4 if any(short.startswith(n) for n in females) else 3 if any(short.startswith(n) for n in coats) else 2
    for data in maps:
        for npc in data['npcs']:npc['model']=models[npc['npc']]
    return models


def scene_props(slug,img):
    props=list(PROPS.get(slug,[]))
    if slug=='grass_club':
        # Grey stones are extracted from their original tiles, not scattered
        # randomly. Exclude the designed beds/topiary/basket silhouettes.
        for y in range(2,28):
            for x in range(2,26):
                if any(p.x<=x<p.x+p.w and p.y<=y<p.y+p.h for p in props):continue
                gray=sum(1 for j in range(8) for i in range(8)
                         if max(img.at(x*8+i,y*8+j)[:3])-min(img.at(x*8+i,y*8+j)[:3])<10
                         and img.at(x*8+i,y*8+j)[0]>120)
                # Grey stepping stones are flush floor details. They are
                # intentionally traversable, unlike the flower/topiary beds.
                # The body sits where the shifted floor art draws the stone.
                if gray>=4:props.append(P('boulder',x,y,1,1,.035,1,solid=False,floor=(x-1,y-1,1,1)))
    return props


def camera(data):
    w,h=data['width'],data['height']
    if data['id']==0:
        authored=OUT/'overworld_authoring.json'
        if authored.exists():
            record=json.loads(authored.read_text())
            visibility=record['record'].get('visibility')
            if visibility:
                from island_visibility import camera_hash
                assert camera_hash(record['camera'])==visibility['camera_sha256'],'Island camera changed; rebuild the authored overworld before publishing.'
            return record['camera']
        return dict(eye=[w/2,27,h/2+3.5],target=[w/2,0,h/2],fov=40)
    if data['slug']=='lightning_club':return dict(eye=[14,9,34],target=[14,0,21.5],fov=58)
    if data['slug']=='psychic_club':h=28
    # Taller halls use the approved camera proportions; short rooms keep walls
    # and player visible with a higher tilt rather than a distant camera.
    return dict(eye=[w/2,max(7,h*.28125),h+2],target=[w/2,0,h*.67],fov=58 if h>=24 else 64)


def main():
    game,maps,nodes=load_metadata();models=actor_models(game,maps)
    lightning,actors=copy_lightning()
    records={};artifacts=[]
    foyer_done=lobby_done=False
    for data in maps:
        slug=data['slug'];img=Image(REPO/'port/maps'/(slug+'.png'))
        data['camera']=camera(data)
        if data['id']==0:
            authored=OUT/'overworld_authoring.json'
            if authored.exists() and (MAPS/'overworld.l64').exists():
                # The Blender-authored island is a checked-in mesh, not a
                # terrain placeholder to overwrite during interior generation.
                record=json.loads(authored.read_text())['record']
                records['overworld']=record
                for name in record['textures']:
                    image=Image(TEX/(name+'.png'));textures[name]=(image.width,image.height)
                data['scene']='rom:/world/maps/overworld.l64';data['overlay']=None
                continue
            scene=Scene('overworld');terrain(scene,img)
        elif slug.endswith('_lobby'):
            data['scene']='rom:/world/maps/club_lobby.l64';data['overlay']=None
            if lobby_done:continue
            lobby_done=True;scene=Scene('club_lobby')
            scene.floor(img,LOBBY,data['warps']);walls(scene,img,data['warps'])
            for p in LOBBY:prop(scene,img,p)
        elif slug.endswith('_club_entrance') or slug=='challenge_hall_entrance':
            data['scene']='rom:/world/maps/club_foyer.l64'
            data['overlay']='rom:/world/maps/emblem_'+slug+'.l64'
            emblem=Scene('emblem_'+slug)
            emblem.bounds=(data['width'],data['height'])
            emblem.polygon([(7,.06,5),(11,.06,5),(11,.06,9),(7,.06,9)],tex=texture(img.crop(64,48,32,32,32,32)))
            rec=emblem.save(MAPS/(emblem.name+'.l64'));records[emblem.name]=rec
            if foyer_done:continue
            foyer_done=True;scene=Scene('club_foyer')
            scene.floor(img,FOYER,data['warps'],exclude_emblem=True);walls(scene,img,data['warps'])
            for p in FOYER:prop(scene,img,p)
        elif slug=='lightning_club':
            scene=lightning
        else:
            scene=Scene(slug);props=scene_props(slug,img)
            scene.floor(img,props,data['warps']);walls(scene,img,data['warps'])
            for p in props:prop(scene,img,p)
        data.setdefault('scene','rom:/world/maps/'+scene.name+'.l64')
        data.setdefault('overlay',None)
        records[scene.name]=scene.save(MAPS/(scene.name+'.l64'))
    records['actors']=actors.save(OUT/'actors.l64')
    # Original event images provide the exact altered panels. Variants are full
    # scenes because tile changes can also affect nearby texture chunk packing.
    variants=[('mason_laboratory_challenge_machine',1,10),('deck_machine_room_all_deck_machines',2,2),
              ('pokemon_dome_entrance_door_open',31,0),('pokemon_dome_hall_of_honor_door_open',32,1)]
    event_records=[]
    for name,mid,event in variants:
        data=maps[mid];img=Image(REPO/'port/maps/events'/(name+'.png'))
        scene=Scene(data['slug']);props=scene_props(data['slug'],img)
        if mid==1:props.append(P('machine',10,0,4,5,2.2,2,floor=F(10,2,12,2)))
        # These are the closed doorway's panels, not permanent counters.
        if mid==31:props=[p for p in props if p.kind!='counter']
        scene.floor(img,props,data['warps']);walls(scene,img,data['warps'])
        for p in props:prop(scene,img,p)
        records[name]=scene.save(MAPS/(name+'.l64'))
        event_records.append(dict(map=mid,event=event,scene='rom:/world/maps/'+name+'.l64'))
    # Eight independent medal-controlled deck printer fronts. The alternate
    # shell omits only these faces, so the frontend draws exactly one off/on
    # front for each machine with no coplanar geometry or all-lit shortcut.
    deck=maps[2];base_img=Image(REPO/'port/maps/deck_machine_room.png')
    lit_img=Image(REPO/'port/maps/events/deck_machine_room_all_deck_machines.png')
    shell=Scene('deck_machine_room_dynamic');shell.switchable=True
    shell.floor(base_img,PROPS['deck_machine_room'],deck['warps']);walls(shell,base_img,deck['warps'])
    for p in PROPS['deck_machine_room']:prop(shell,base_img,p)
    records[shell.name]=shell.save(MAPS/(shell.name+'.l64'))
    switches=[]
    for event,(x,y) in enumerate([(6,1),(10,1),(14,1),(18,1),(14,8),(18,8),(14,16),(18,16)],2):
        h=4 if y==1 else 5
        record=dict(map=2,event=event)
        for state,image in [('off',base_img),('on',lit_img)]:
            face=Scene(f'deck_machine_{event}_{state}')
            machine=next(p for p in PROPS['deck_machine_room'] if p.x==x and p.y==y)
            left,north,width,depth=footprint(machine);front=north+depth
            transformed=replace(machine,x=left,y=north,w=width,h=depth,depth=depth)
            build_deck_front(face,transformed,source_p=machine,img=image,texture=texture)
            records[face.name]=face.save(MAPS/(face.name+'.l64'))
            record[state]='rom:/world/maps/'+face.name+'.l64'
        switches.append(record)
    doc=dict(format='L643',coordinate_system='X east, Y up, Z south; one source tile = one world unit',
             maps=maps,overworld_nodes=nodes,events=event_records,npc_models=models,meshes=records,
             switches=switches,switch_base='rom:/world/maps/deck_machine_room_dynamic.l64',
             shared=dict(lobbies=9,foyers=9,actors='actors.l64',textures='content-addressed PNG files, global deduplication'),
             texture_count=len(textures))
    from sync_characters import apply_characters
    from world_manifest import write_world_metadata
    if (OUT/'world.json').exists():
        previous=json.loads((OUT/'world.json').read_text())
        doc['individual_characters']=previous.get('individual_characters',[])
        if 'overworld_authoring' in previous:doc['overworld_authoring']=previous['overworld_authoring']
    doc=apply_characters(doc)
    from honor_cards import build as build_honor_cards
    build_honor_cards(sys.modules[__name__],doc)
    referenced={t for record in doc['meshes'].values() for t in record['textures']}
    for old in TEX.glob('t????????????????.png'):
        if old.stem not in referenced:old.unlink()
    write_world_metadata(OUT,doc)
    print(f"Built {len(maps)} map descriptions, {len(doc['meshes'])} reusable mesh files, {doc['texture_count']} deduplicated textures")
    print('Triangles:',sum(r['triangles'] for r in doc['meshes'].values()),'mesh bytes:',sum((OUT/r['file']).stat().st_size for r in doc['meshes'].values()))

def update_overworld():
    """Rebuild only the island, preserving all room and character artifacts."""
    from world_manifest import write_world_metadata
    if (OUT/'overworld_authoring.json').exists():
        import os,shutil,subprocess
        blender=os.environ.get('POKETCG_BLENDER') or shutil.which('blender') or '/Applications/Blender.app/Contents/MacOS/Blender'
        subprocess.run([blender,'--background','--python-exit-code','1','--python',str(ROOT/'tools/author_overworld.py')],check=True)
        return
    doc=json.loads((OUT/'world.json').read_text())
    scene=Scene('overworld');terrain(scene,Image(REPO/'port/maps/overworld.png'))
    doc['meshes']['overworld']=scene.save(MAPS/'overworld.l64')
    doc['maps'][0]['camera']=camera(doc['maps'][0])
    doc['texture_count']=len({t for record in doc['meshes'].values() for t in record['textures']})
    write_world_metadata(OUT,doc)
    record=doc['meshes']['overworld']
    print(f"Overworld rebuilt: {record['triangles']} triangles, {record['vertices']} vertices, {record['batches']} batches")


def update_hall():
    """Rebuild the Hall alone; leave every other authored export untouched."""
    from world_manifest import write_world_metadata
    existing_textures=set(TEX.glob('*.png'))
    doc=json.loads((OUT/'world.json').read_text())
    data=next(m for m in doc['maps'] if m['slug']=='hall_of_honor')
    image=Image(REPO/'port/maps/hall_of_honor.png')
    scene=Scene('hall_of_honor');props=scene_props(scene.name,image)
    scene.floor(image,props,data['warps']);walls(scene,image,data['warps'])
    for p in props:prop(scene,image,p)
    doc['meshes'][scene.name]=scene.save(MAPS/'hall_of_honor.l64')
    from honor_cards import build as build_honor_cards
    build_honor_cards(sys.modules[__name__],doc)
    referenced={t for record in doc['meshes'].values() for t in record['textures']}
    # Generic primitive extraction can produce a face crop that a geometric
    # torch never uses. Only remove new, unreferenced files from this run.
    for path in set(TEX.glob('*.png'))-existing_textures:
        if path.stem not in referenced:path.unlink()
    doc['texture_count']=len(referenced)
    write_world_metadata(OUT,doc)
    record=doc['meshes'][scene.name]
    print(f"Hall rebuilt: {record['triangles']} triangles, {record['vertices']} vertices, {record['batches']} batches")

if __name__=='__main__':
    if '--overworld-only' in sys.argv:update_overworld()
    elif '--hall-only' in sys.argv:update_hall()
    else:main()
