"""Source-shaped room ornaments, authored inside the existing solid footprints.

The map PNGs are projected elevations: Water's alleged 'horses' are parasols
and its benches are chaise lounges; Psychic's alleged 'torches' have square
heads. Keep source kind names so saved map/event/permission metadata is stable.
All materials are vertex colours shared in the room's existing untextured batch.
"""
import math

NAVY=(23,35,73,255)
CREAM=(239,232,185,255)
WHITE=(235,241,229,255)
ORANGE=(198,106,31,255)
RED=(188,43,42,255)
GOLD=(202,158,51,255)
GOLD_LIGHT=(235,190,65,255)
BLUE=(54,63,145,255)
LAVENDER=(158,123,225,255)
PURPLE=(51,35,109,255)


def tint(color,value):
    return tuple(max(0,min(255,round(c*value))) for c in color[:3])+(255,)


def ring(cx,cz,radius,y,segments=8,rz=None):
    if rz is None:rz=radius
    return [(cx+radius*math.cos(i*math.tau/segments),y,
             cz+rz*math.sin(i*math.tau/segments)) for i in range(segments)]


def profile(scene,cx,cz,levels,color,segments=8,fluted=False,cap=True):
    """An owning lathe skin, including step shoulders, with no buried caps."""
    rings=[ring(cx,cz,r,y,segments) for y,r in levels]
    for lower,upper in zip(rings,rings[1:]):
        for i in range(segments):
            j=(i+1)%segments
            shade=.72+.28*(.5+.5*math.cos((i+.5)*math.tau/segments))
            if fluted and i%2:shade*=.68
            scene.polygon([lower[i],lower[j],upper[j],upper[i]],tint(color,shade))
    if cap:scene.polygon(rings[-1],color,uv=[(0,0)]*segments)


def pole(scene,cx,cz,r,bottom,top,color):
    profile(scene,cx,cz,[(bottom,r),(top,r)],color,6)


def parasol(scene,p):
    """Red/cream eight-panel canopy, round table and a central orange pole."""
    cx=p.x+p.w*.5;cz=p.y+p.depth*.5
    r=min(p.w,p.depth)*.475
    # A low, broad canopy with a folded radial profile reads as fabric rather
    # than a pointed traffic cone. The skirt owns its rim and underside.
    edge_y=1.91;mid_y=2.18;tip_y=2.27
    outer=ring(cx,cz,r,edge_y);inner=ring(cx,cz,r*.53,mid_y)
    lower=ring(cx,cz,r,edge_y-.055)
    for i in range(8):
        j=(i+1)%8;color=RED if i%2==0 else CREAM
        scene.polygon([(cx,tip_y,cz),inner[i],inner[j]],color)
        scene.polygon([inner[i],outer[i],outer[j],inner[j]],tint(color,.94))
        scene.polygon([outer[i],lower[i],lower[j],outer[j]],tint(color,.70))
        scene.polygon([(cx,edge_y-.055,cz),lower[j],lower[i]],tint(RED,.49))
    # Pole terminates against the canopy's underside and pierces the table
    # through a real annular opening, rather than an overlapping top disk.
    pole_r=.055
    profile(scene,cx,cz,[(.10,.11),(.18,pole_r),(edge_y-.055,pole_r)],ORANGE,8,cap=False)
    table_r=r*.72;top=.85;bottom=.75
    top_ring=ring(cx,cz,table_r,top);bottom_ring=ring(cx,cz,table_r,bottom)
    hole=ring(cx,cz,pole_r,top)
    for i in range(8):
        j=(i+1)%8
        scene.polygon([hole[i],top_ring[i],top_ring[j],hole[j]],CREAM)
        scene.polygon([bottom_ring[i],bottom_ring[j],top_ring[j],top_ring[i]],NAVY)
    # Three splayed navy feet give the round table a solid, readable base.
    for i in range(3):
        a=(i+.25)*math.tau/3
        bx=cx+math.cos(a)*table_r*.60;bz=cz+math.sin(a)*table_r*.60
        pole(scene,bx,bz,.055,0,bottom,NAVY)


def lounge(scene,left,center_z,width,depth):
    """White slatted chaise: long low seat and a raised east-end backrest."""
    right=left+width
    north=center_z-depth*.5;south=center_z+depth*.5
    # Orange feet meet a navy structural rail; flat seat remains exactly .71
    # so Amy's established .66/.71 presentation support requires no change.
    for x in (left+width*.14,right-width*.16):
        for z in (north+.12,south-.24):
            scene.box(x,z,.14,.14,.49,ORANGE,omit=(0,5))
    for z in (north,south-.10):
        scene.box(left,z,width,.10,.10,NAVY,y=.49,omit=(0,))
    for x in (left,right-.10):
        scene.box(x,north,.10,depth,.12,NAVY,y=.59,omit=(0,))
    # Individual broad slats leave narrow navy gaps and have visible thickness.
    seat_end=left+width*.75
    length=seat_end-left-.11;count=6
    for i in range(count):
        x=left+.11+i*length/count
        scene.box(x,north+.10,length/count*.82,depth-.20,.09,WHITE,y=.62,omit=(0,))
    # A single inclined back plane with disjoint cream slat strips, navy rails
    # and a filled underside. The raised end matches the original sprite.
    low_y=.71;high_y=1.20
    x0=seat_end;x1=right-.10
    def back_patch(a,b,near,far,color):
        xa=x0+(x1-x0)*a;xb=x0+(x1-x0)*b
        ya=low_y+(high_y-low_y)*a;yb=low_y+(high_y-low_y)*b
        scene.polygon([(xa,ya,near),(xb,yb,near),(xb,yb,far),(xa,ya,far)],color)
    back_patch(0,1,north,north+.10,NAVY)
    back_patch(0,1,south-.10,south,NAVY)
    for i in range(3):
        a=i/3;b=(i+.78)/3
        back_patch(a,b,north+.10,south-.10,WHITE)
        back_patch(b,(i+1)/3,north+.10,south-.10,NAVY)
    scene.polygon([(x0,low_y-.10,south),(x1,high_y-.10,south),
                   (x1,high_y-.10,north),(x0,low_y-.10,north)],tint(NAVY,.75))
    scene.polygon([(x0,low_y,north),(x1,high_y,north),
                   (x1,high_y-.10,north),(x0,low_y-.10,north)],NAVY)
    scene.polygon([(x1,high_y,south),(x0,low_y,south),
                   (x0,low_y-.10,south),(x1,high_y-.10,south)],NAVY)
    scene.polygon([(x1,high_y,north),(x1,high_y-.10,north),
                   (x1,high_y-.10,south),(x1,high_y,south)],NAVY)


def benches(scene,p,source_p):
    count=2 if source_p and source_p.h>=5 else 1
    d=min(1.35,p.depth/count-.12)
    for i in range(count):
        z=p.y+p.depth*(i+.5)/count
        lounge(scene,p.x,z,p.w,d)


def ellipsoid(scene,x,z,y,rx,ry,rz,color,segments=8):
    """A coarse faceted volume with directional gold shading."""
    bottom=(x,y-ry,z);top=(x,y+ry,z)
    lo=ring(x,z,rx*.81,y-ry*.38,segments,rz*.81)
    hi=ring(x,z,rx*.81,y+ry*.38,segments,rz*.81)
    for i in range(segments):
        j=(i+1)%segments
        shade=.70+.30*(.5+.5*math.cos((i+.5)*math.tau/segments-.8))
        c=tint(color,shade)
        scene.polygon([bottom,lo[j],lo[i]],tint(c,.85))
        scene.polygon([lo[i],lo[j],hi[j],hi[i]],c)
        scene.polygon([hi[i],hi[j],top],tint(c,1.07))


def spike(scene,x,z,y,rx,rz,height,color):
    base=ring(x,z,rx,y,6,rz)
    for i in range(6):
        scene.polygon([base[i],base[(i+1)%6],(x,y+height,z)],tint(color,.78+i*.035))


def statue(scene,p):
    """A broad gold creature, with ears/horn, snout, arms and planted feet."""
    cx=p.x+p.w*.5;cz=p.y+p.depth*.5
    width=p.w*.92;depth=min(p.depth,1.48)
    scene.box(cx-width*.5,cz-depth*.5,width,depth,.18,tint(GOLD,.68),omit=(0,5))
    scene.box(cx-width*.5,cz-depth*.5,width,depth,.16,GOLD_LIGHT,y=.18,omit=(0,))
    # Broad feet/haunches distinguish the silhouette from the former pawn.
    for side in (-1,1):
        ellipsoid(scene,cx+side*width*.20,cz+.12,.51,width*.20,.20,.37,GOLD)
    ellipsoid(scene,cx,cz,.99,width*.35,.58,.37,GOLD)
    for side in (-1,1):
        ellipsoid(scene,cx+side*width*.34,cz+.02,1.14,width*.14,.33,.21,GOLD_LIGHT,6)
    ellipsoid(scene,cx,cz+.02,1.72,width*.35,.38,.32,GOLD_LIGHT)
    # Squat muzzle and cheek plane, not a floating ball head.
    ellipsoid(scene,cx,cz+.29,1.60,width*.27,.18,.24,GOLD,6)
    for side in (-1,1):
        spike(scene,cx+side*width*.24,cz-.03,1.91,.15,.15,.31,GOLD_LIGHT)
    spike(scene,cx,cz+.43,1.66,.105,.11,.25,GOLD_LIGHT)


def square_pedestal(scene,p):
    cx=p.x+p.w*.5;cz=p.y+p.depth*.5
    r=min(p.w*.43,.70)
    profile(scene,cx,cz,[(0,r),(.14,r),(.22,r*.73),(.31,r*.73),
                        (.36,r*.44),(1.78,r*.44),(1.84,r*.72),
                        (1.96,r*.72)],WHITE,8,fluted=True)
    # Square purple head; front is partitioned into a cream frame and dark
    # inset. There is no complete front plane behind the replacement regions.
    w=min(p.w*.90,1.40);d=.54;bottom=1.96;height=.90
    left=cx-w*.5;north=cz-d*.5;front=north+d
    scene.box(left,north,w,d,height,BLUE,y=bottom,omit=(0,3))
    edge=.12
    for x,y,ww,hh,color in [(left,bottom,w,edge,BLUE),
                            (left,bottom+height-edge,w,edge,WHITE),
                            (left,bottom+edge,edge,height-2*edge,WHITE),
                            (left+w-edge,bottom+edge,edge,height-2*edge,LAVENDER),
                            (left+edge,bottom+edge,w-2*edge,height-2*edge,LAVENDER)]:
        # The inset lavender panel below is replaced by its inner dark square.
        if color==LAVENDER and ww>edge:
            inset=.105
            for a,b,c,e,pigment in [(x,y,ww,inset,LAVENDER),
                                   (x,y+hh-inset,ww,inset,LAVENDER),
                                   (x,y+inset,inset,hh-2*inset,LAVENDER),
                                   (x+ww-inset,y+inset,inset,hh-2*inset,LAVENDER),
                                   (x+inset,y+inset,ww-2*inset,hh-2*inset,PURPLE)]:
                scene.polygon([(a,b,front),(a+c,b,front),(a+c,b+e,front),(a,b+e,front)],pigment)
        else:
            scene.polygon([(x,y,front),(x+ww,y,front),(x+ww,y+hh,front),(x,y+hh,front)],color)


def build(scene,p,source_p=None,img=None,texture=None):
    if scene.name=='water_club' and p.kind=='horse':parasol(scene,p)
    elif scene.name=='water_club' and p.kind=='bench':benches(scene,p,source_p)
    elif scene.name=='rock_club' and p.kind=='statue':statue(scene,p)
    elif scene.name=='psychic_club' and p.kind=='torch':square_pedestal(scene,p)
    else:return False
    return True
