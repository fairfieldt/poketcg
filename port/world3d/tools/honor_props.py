"""Hall of Honor: a card shrine and the separate Legendary Deck Machine.

The source's blue/white projected shrine is architecture, not a computer
front. These shapes replace that painted elevation with owning 3D surfaces.
Coordinates remain inside the hand-reviewed source collision footprints.
"""
import math

IVORY=(222,231,232,255)
LIGHT=(245,246,228,255)
BLUE=(59,83,143,255)
NAVY=(29,44,91,255)
GOLD=(211,174,82,255)
BLACK=(22,30,48,255)


def shade(color,value):
    return tuple(round(c*value) for c in color[:3])+(255,)


def banded_box(scene,x,z,w,d,bands):
    """Stack disjoint facade bands; only the final band owns the top cap."""
    y=0
    for i,(height,color) in enumerate(bands):
        scene.box(x,z,w,d,height,color,y=y,omit=(0,) if i==len(bands)-1 else (0,5))
        y+=height


def stairs(scene,left,right,north,south,top,steps=4):
    """One extruded stair profile, without complete overlapping step boxes."""
    depth=(south-north)/steps
    for i in range(steps):
        front=south-i*depth;back=front-depth
        y=top*(i+1)/steps;below=top*i/steps
        scene.polygon([(left,y,back),(right,y,back),(right,y,front),(left,y,front)],
                      LIGHT if i%2 else IVORY)
        scene.polygon([(left,below,front),(right,below,front),(right,y,front),(left,y,front)],
                      shade(BLUE,.87))
        # Separate side profiles keep the diagonal stair silhouette solid.
        side=[(left,0,back),(left,0,front),(left,y,front),(left,y,back)]
        scene.polygon(list(reversed(side)),shade(IVORY,.65))
        scene.polygon([(right,p[1],p[2]) for p in side],shade(IVORY,.81))


def column(scene,p):
    # A wide low plinth retains the exact existing solid footprint. The
    # narrower shaft is displaced outward so the original sparks stay clear.
    x,z,w,d=p.x,p.y,p.w,p.depth
    cx=x+w/2+(-.45 if x<10 else .45);cz=z+d/2
    banded_box(scene,x,z,w,d,[(.15,NAVY),(.19,IVORY),(.09,GOLD)])
    scene.cylinder(cx,cz,.43,1.04,.24,IVORY,segments=8,top_r=.86)
    scene.cylinder(cx,cz,.67,.86,1.75,IVORY,segments=8,top_r=.76)
    scene.cylinder(cx,cz,2.42,.76,.20,GOLD,segments=8,top_r=1.02)
    scene.cylinder(cx,cz,2.62,1.02,.22,IVORY,segments=8)
    scene.box(cx-1.10,cz-1.10,2.20,2.20,.16,LIGHT,y=2.84,omit=(0,))


def shrine(scene,p):
    x,z,w,d=p.x,p.y,p.w,p.depth
    front=z+2.0
    banded_box(scene,x,z,w,2.0,[(.18,NAVY),(.16,GOLD),(1.28,BLUE),(.13,IVORY)])
    # The stairs terminate against the altar's front plane, between the two
    # source pillars. Their side profiles never cross either plinth.
    stairs(scene,8.85,13.15,front,z+d,1.40)
    # A raised back panel makes the blue Poké Ball a relief, rather than an
    # unrelated floating sphere. Original animated card components stay clear.
    panel_x=9.30;panel_z=z+.07;panel_w=3.40;panel_d=.42
    # Keep the top below 3.3: the source north-machine interaction is behind
    # this dais and must leave Mark visible from the tracking camera.
    scene.box(panel_x,panel_z,panel_w,panel_d,1.55,IVORY,y=1.75,omit=(0,3))
    # Frame and inset partition the front; no full face hides behind them.
    face=panel_z+panel_d
    for a,b,c,e,color in [(panel_x,1.75,panel_w,.15,GOLD),
                         (panel_x,3.15,panel_w,.15,GOLD),
                         (panel_x,1.90,.16,1.25,BLUE),
                         (panel_x+panel_w-.16,1.90,.16,1.25,BLUE),
                         (panel_x+.16,1.90,panel_w-.32,1.25,LIGHT)]:
        scene.polygon([(a,b,face),(a+c,b,face),(a+c,b+e,face),(a,b+e,face)],color)


def crest(scene):
    # A shallow twelve-sided blue/ivory medallion. The band and button replace
    # their owning disk faces; there are no intersecting complete spheres.
    cx,cy,back,front,radius=11.0,2.50,7.64,7.77,.70
    points=[(cx+radius*math.cos(i*math.tau/12),
             cy+radius*math.sin(i*math.tau/12)) for i in range(12)]
    for i,(a,b) in enumerate(zip(points,points[1:]+points[:1])):
        scene.polygon([(a[0],a[1],back),(b[0],b[1],back),
                       (b[0],b[1],front),(a[0],a[1],front)],shade(GOLD,.8))
    # Clipping the disk into three material regions gives a real continuous
    # exterior even at native depth precision.
    def clip(poly,height,above):
        out=[]
        for a,b in zip(poly,poly[1:]+poly[:1]):
            ain=(a[1]>=height) if above else (a[1]<=height)
            bin=(b[1]>=height) if above else (b[1]<=height)
            if ain:out.append(a)
            if ain!=bin:
                t=(height-a[1])/(b[1]-a[1]);out.append((a[0]+t*(b[0]-a[0]),height))
        return out
    for lo,hi,color in [(-10,cy-.10,BLUE),(cy-.10,cy+.10,IVORY),(cy+.10,10,NAVY)]:
        poly=clip(clip(points,lo,True),hi,False)
        scene.polygon([(a,b,front) for a,b in poly],color,uv=[(0,0)]*len(poly))
    # Button is a shallow solid, not a coplanar white texture overlay.
    button=[(cx+.22*math.cos(i*math.tau/8),cy+.22*math.sin(i*math.tau/8)) for i in range(8)]
    scene.polygon([(a,b,front+.06) for a,b in button],LIGHT,uv=[(0,0)]*8)
    for a,b in zip(button,button[1:]+button[:1]):
        scene.polygon([(a[0],a[1],front),(b[0],b[1],front),
                       (b[0],b[1],front+.06),(a[0],a[1],front+.06)],BLUE)


def machine(scene,p):
    """North-wall original Auto Deck Machine: console, card slot, red key."""
    x,z,w,d=p.x,p.y,p.w,p.depth
    banded_box(scene,x,z,w,d,[(.16,NAVY),(.17,IVORY),(1.18,BLUE),(.19,IVORY)])
    # The tall back narrows above the inclined control deck.
    scene.box(x+.18,z+.08,w-.36,d-.62,1.65,IVORY,y=1.70,omit=(0,3))
    front=z+d-.54;left=x+.18;right=x+w-.18
    # Owning screen-face regions, with a broad blue display and cream frame.
    for a,b,c,h,color in [(left,1.70,right-left,.23,BLUE),(left,3.12,right-left,.23,LIGHT),
                         (left,1.93,.27,1.19,IVORY),(right-.27,1.93,.27,1.19,IVORY),
                         (left+.27,1.93,right-left-.54,1.19,NAVY)]:
        scene.polygon([(a,b,front),(a+c,b,front),(a+c,b+h,front),(a,b+h,front)],color)
    # A large card silhouette in the display survives the native far view.
    scene.box(x+w*.5-.39,front+.015,.78,.045,.90,BLUE,y=2.05,omit=(0,1))
    scene.box(x+w*.5-.30,front+.065,.60,.025,.63,LIGHT,y=2.18,omit=(0,1))
    # Sloped control deck with one red activation key, no tiny pseudo-text.
    scene.polygon([(x,1.70,z+d),(x+w,1.70,z+d),
                   (x+w,1.94,front),(x,1.94,front)],LIGHT)
    scene.box(x+.48,z+d+.015,w-1.45,.045,.20,BLACK,y=1.18,omit=(0,1))
    scene.box(x+w-.72,z+d+.015,.35,.08,.20,(191,58,54,255),y=1.18,omit=(0,1))


def build(scene,p):
    if p.kind=='altar':shrine(scene,p)
    elif p.kind=='pillar':column(scene,p)
    elif p.kind=='ball':crest(scene)
    elif p.kind=='legendary_machine':machine(scene,p)
    else:return False
    return True
