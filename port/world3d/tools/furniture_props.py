"""Reusable source-room furniture with actual surfaces rather than map sprites.

All dimensions are relative to the reviewed collision footprints. The only
extension is the .30-unit join between the lobby counter's two blocked runs.
The felt is a direct, TMEM-sized material; its card zones are painted from above
and contain no projected legs, floor, or apron from the Game Boy room image.
"""
import math

NAVY=(31,35,48,255)
WOOD=(145,74,31,255)
AMBER=(192,111,40,255)
EDGE=(217,145,66,255)
CREAM=(234,219,175,255)
PALE=(236,233,211,255)
RED=(194,43,36,255)
MAROON=(83,29,28,255)


def shade(c,f):
    return tuple(round(v*f) for v in c[:3])+(255,)


def ring(scene,x,z,w,d,y,inset,outer,inner=None):
    """Four owning cap regions, no complete face beneath an inset."""
    for a,b,c,e in ((x,z,w,inset),(x,z+d-inset,w,inset),
                    (x,z+inset,inset,d-2*inset),
                    (x+w-inset,z+inset,inset,d-2*inset)):
        scene.polygon([(a,y,b),(a+c,y,b),(a+c,y,b+e),(a,y,b+e)],outer)
    if inner is not None:
        scene.polygon([(x+inset,y,z+inset),(x+w-inset,y,z+inset),
                       (x+w-inset,y,z+d-inset),(x+inset,y,z+d-inset)],inner)


def felt_material(texture):
    # 64x32 RGBA16 occupies all 4 KiB TMEM. Broad 2-texel bindings survive
    # native filtering, while restrained cloth variation avoids moire.
    rows=[]
    def inside(x,y,a,b,c,d):return a<=x<c and b<=y<d
    zones=[(5,y,14,y+4) for y in (3,10,18,25)]
    zones += [(50,y,59,y+4) for y in (3,10,18,25)]
    zones += [(23,7,31,13),(33,19,41,25)]
    for y in range(32):
        row=[]
        for x in range(64):
            value=2*math.sin(x*math.pi/32)*math.sin(y*math.pi/16)
            color=tuple(round(c+value) for c in (44,111,53))+(255,)
            for a,b,c,d in zones:
                if inside(x,y,a,b,c,d):
                    color=(160,162,103,255) if min(x-a,c-1-x,y-b,d-1-y)<1 else (39,89,43,255)
                    break
            row.append(color)
        rows.append(row)
    return texture(rows)


def table(scene,p,texture):
    x,z,w,d,h=p.x,p.y,p.w,p.depth,p.height
    # The bevel, rail, binding and cloth partition the cap into disjoint
    # regions. Unlike the old full source crop, every texel depicts cloth.
    rail=.18;binding=.065;top=h
    scene.box(x,z,w,d,.18,WOOD,y=top-.18,omit=(0,5))
    ring(scene,x,z,w,d,top,rail,AMBER)
    ring(scene,x+rail,z+rail,w-2*rail,d-2*rail,top,binding,CREAM)
    a=x+rail+binding;b=z+rail+binding
    c=x+w-rail-binding;e=z+d-rail-binding
    scene.polygon([(a,top,b),(c,top,b),(c,top,e),(a,top,e)],
                  (255,255,255,255),felt_material(texture),[(0,0),(1,0),(1,1),(0,1)])
    # A dark slatted apron and inset square legs give a readable silhouette.
    apron=.18;low=max(.18,h-.38)
    scene.box(x+.13,z+.13,w-.26,d-.26,apron,NAVY,y=low,omit=(0,5,3))
    count=max(4,round(w/.45));step=(w-.26)/count
    for i in range(count):
        left=x+.13+i*step
        scene.polygon([(left,low,z+d-.13),(left+step,low,z+d-.13),
                       (left+step,low+apron,z+d-.13),(left,low+apron,z+d-.13)],
                      WOOD if i%2 else shade(AMBER,.77))
    leg=.23
    for lx,lz in ((x+.25,z+.25),(x+w-.25-leg,z+.25),
                  (x+.25,z+d-.25-leg),(x+w-.25-leg,z+d-.25-leg)):
        scene.box(lx,lz,leg,leg,h-.18,WOOD,omit=(0,5))


def counter(scene,p,source):
    x,z,w,d,h=p.x,p.y,p.w,p.depth,p.height
    arm=source.y<4
    # Join the arm to the long run at z5.15; the neighbouring end faces are
    # omitted, so the L is one continuous exterior without intersecting caps.
    if arm:d+=.30
    end_omit=(3,) if arm else (1,)
    bands=[(.12,NAVY),(.48,WOOD),(.10,CREAM),(.13,NAVY)]
    scale=(h-.12)/sum(v for v,_ in bands);y=0
    for height,color in bands:
        height*=scale
        paneled=not arm and color==WOOD
        scene.box(x,z,w,d,height,color,y=y,omit=(0,5)+end_omit+((3,) if paneled else ()))
        if paneled:
            for i in range(6):
                left=x+w*i/6;right=x+w*(i+1)/6;post=.065
                for a,b,pigment in ((left,left+post,shade(WOOD,.64)),
                                    (left+post,right-post,WOOD),
                                    (right-post,right,shade(AMBER,.82))):
                    scene.polygon([(a,y,z+d),(b,y,z+d),
                                   (b,y+height,z+d),(a,y+height,z+d)],pigment)
        if not arm:
            # Only the free part of the long run has a north-facing side.
            right=min(x+w,11.15)
            if right>x:
                scene.polygon([(x,y,z),(right,y,z),(right,y+height,z),(x,y+height,z)],shade(color,.78))
        y+=height
    # The top is a thin orange slab with a broad, quiet edge highlight.
    scene.box(x,z,w,d,.12,AMBER,y=h-.12,omit=(0,5)+end_omit)
    if not arm:
        right=min(x+w,11.15)
        scene.polygon([(x,h-.12,z),(right,h-.12,z),(right,h,z),(x,h,z)],shade(AMBER,.78))
    ring(scene,x,z,w,d,h,.08,EDGE,AMBER)
    if arm:
        # The original yellow flower pot sits on this arm, above the worktop.
        cx=x+w/2;cz=z+.85
        scene.cylinder(cx,cz,h,.20,.24,WOOD,segments=6,top_r=.26)
        scene.cylinder(cx,cz,h+.24,.18,.17,(53,92,37,255),segments=6,top_r=.30)
        for dx,dz,color in ((-.17,-.08,CREAM),(.16,-.03,(229,193,55,255)),(0,.16,(242,213,76,255))):
            scene.cylinder(cx+dx,cz+dz,h+.41,.16,.12,color,segments=6,top_r=.13)
        return
    # Two card boxes/registers share the same simple workstation recipe.
    for cx in (6.0,10.0):
        if not x+.5<cx<x+w-.5:continue
        cz=z+d*.48
        scene.box(cx-.32,cz-.25,.64,.50,.17,PALE,y=h,omit=(0,5))
        scene.polygon([(cx-.32,h+.17,cz+.25),(cx+.32,h+.17,cz+.25),
                       (cx+.32,h+.29,cz-.25),(cx-.32,h+.29,cz-.25)],PALE)
        scene.box(cx+.08,cz-.10,.14,.12,.07,AMBER,y=h+.23,omit=(0,))
        # A stack of cream cards beside the register, with a dark blue sleeve.
        scene.box(cx-.8,cz+.05,.39,.53,.10,NAVY,y=h,omit=(0,5))
        scene.box(cx-.8,cz+.05,.39,.53,.055,CREAM,y=h+.10,omit=(0,))


def book(scene,x,z,width,height,y,color):
    # Front, top and two ends are enough: its back is against the backing
    # and its bottom rests on the shelf, leaving no coplanar hidden faces.
    scene.box(x,z,width,.22,height,color,y=y,omit=(0,1))


def jar(scene,x,z,w,h,y,color):
    """Five exterior faces and a cap-colored front band: ten triangles."""
    lid=(93,120,105,255);front=z+.22;split=y+h*.78
    scene.polygon([(x,y,front),(x+w,y,front),(x+w,split,front),(x,split,front)],color)
    scene.polygon([(x,split,front),(x+w,split,front),(x+w,y+h,front),(x,y+h,front)],lid)
    scene.polygon([(x,y,z),(x,y,front),(x,y+h,front),(x,y+h,z)],shade(color,.8))
    scene.polygon([(x+w,y,z),(x+w,y,front),(x+w,y+h,front),(x+w,y+h,z)],shade(color,.9))
    scene.polygon([(x,y+h,z),(x+w,y+h,z),(x+w,y+h,front),(x,y+h,front)],lid)


def cabinet(scene,p):
    x,z,w,d,h=p.x,p.y,p.w,p.depth,p.height
    lab=scene.name=='science_club'
    mason=scene.name=='mason_laboratory'
    private=scene.name=='ishiharas_house'
    timber=(165,87,35,255) if lab or mason else AMBER
    interior=(60,36,23,255) if private else NAVY
    front=z+d-.05;post=.11;back=.12;bottom=h*.27
    # Real deep case: back board, tall stiles, cornice and recessed shelves.
    scene.box(x,z,w,back,h,interior,omit=(0,3))
    for lx in (x,x+w-post):
        scene.box(lx,z+back,post,d-back,h,timber,omit=(0,1,5))
    scene.box(x+post,z+back,w-2*post,d-back,.13,EDGE,y=h-.13,omit=(0,1,2,4))
    # Cupboard doors partition the complete lower face; their broad central
    # seam survives the native filters without tiny floating handle decals.
    mid=x+w/2
    for left,right in ((x+post,mid-.025),(mid+.025,x+w-post)):
        scene.polygon([(left,0,front),(right,0,front),
                       (right,bottom,front),(left,bottom,front)],timber)
    scene.polygon([(mid-.025,0,front),(mid+.025,0,front),
                   (mid+.025,bottom,front),(mid-.025,bottom,front)],interior)
    rows=3 if private or lab or mason else 2
    available=h-.13-bottom;stride=available/rows
    colors=[CREAM,(190,128,60,255),(219,189,134,255),(137,69,37,255)]
    for row in range(rows):
        sy=bottom+row*stride
        scene.box(x+post,z+back,w-2*post,d-back,.075,timber,y=sy,omit=(0,1,2,4))
        # A recessed dark panel owns the gap behind each row. This is actual
        # shelf depth, not a photographed front laid over a complete box.
        scene.polygon([(x+post,sy+.075,front-.28),(x+w-post,sy+.075,front-.28),
                       (x+w-post,sy+stride,front-.28),(x+post,sy+stride,front-.28)],interior)
        count=(2 if w<2.2 else 4) if lab else (3 if w<2.2 else 5)
        space=(w-2*post-.12)/count
        for i in range(count):
            lx=x+post+.06+i*space
            height=(stride-.095)*( .72+.12*((i+row)%3))
            if lab:
                # Broad specimen jars: pale body and a distinctly darker lid.
                body=space*.68;px=lx+(space-body)/2
                jar(scene,px,front-.25,body,height,sy+.075,PALE if i%2 else CREAM)
            else:
                book(scene,lx,front-.25,space*.84,height,sy+.075,colors[(i+row)%len(colors)])


def chair(scene,p,source):
    x,z,w,d=p.x,p.y,p.w,p.depth
    cx=x+w/2;cz=z+d/2
    # Lobby chairs face inward across the card table, rather than all facing
    # the north wall. The collision audit requires them tucked into its corners.
    direction=1 if source.x<8 else -1
    half=min(w,d)*.38;seat=.49;thick=.12
    scene.box(cx-half,cz-half,2*half,2*half,thick,RED,y=seat,omit=(0,))
    for dx in (-half*.72,half*.72):
        for dz in (-half*.72,half*.72):
            scene.box(cx+dx-.045,cz+dz-.045,.09,.09,seat,WOOD,omit=(0,5))
    # Chamfered upper corners soften the red padded back in only eight faces.
    bx=cx-direction*half
    profile=[(cz-half,seat+thick),(cz+half,seat+thick),
             (cz+half,seat+.48),(cz+half-.12,seat+.61),
             (cz-half+.12,seat+.61),(cz-half,seat+.48)]
    rear=bx-direction*.10;inner=bx+direction*.035
    scene.polygon([(rear,y,a) for a,y in reversed(profile)],MAROON,uv=[(0,0)]*6)
    scene.polygon([(inner,y,a) for a,y in profile],RED,uv=[(0,0)]*6)
    for (a,y),(b,v) in zip(profile,profile[1:]+profile[:1]):
        if y==v==seat+thick:continue  # The seat owns the shared lower cap.
        scene.polygon([(rear,y,a),(rear,v,b),(inner,v,b),(inner,y,a)],shade(RED,.75))


def stool(scene,p):
    # The four magenta circles in Mason's original are round stools, not
    # balloons. Their source y positions bracket the same green duel table.
    x=p.x+p.w/2;z=p.y+p.depth/2;radius=min(p.w,p.depth)/2
    scene.cylinder(x,z,0,radius*.70,.09,NAVY,segments=6)
    scene.cylinder(x,z,.09,.075,.38,WOOD,segments=4)
    scene.cylinder(x,z,.47,radius,.145,(220,91,195,255),segments=8,top_r=radius*.92)


def build(scene,p,source_p=None,img=None,texture=None):
    """Return True only for furniture recipes owned by this module."""
    source=source_p or p
    if p.kind=='table' and (scene.name in ('club_lobby','pokemon_dome') or
                            (scene.name=='mason_laboratory' and source.y>=14)):
        if texture is None:raise ValueError('Card table needs a texture writer')
        table(scene,p,texture);return True
    if p.kind=='counter' and scene.name=='club_lobby':
        counter(scene,p,source);return True
    if p.kind=='cabinet':cabinet(scene,p);return True
    if p.kind=='chair' and scene.name=='club_lobby':
        chair(scene,p,source);return True
    if p.kind=='balloon' and scene.name=='mason_laboratory' and source.y in (14,17):
        stool(scene,p);return True
    return False
