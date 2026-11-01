"""Source-shaped computers and laboratory instruments for the shared room mesh.

These are solids with their own front surfaces, rather than projected Game Boy
furniture painted over a box.  All low geometry stays inside the original
permission-grid footprint.  Screens use broad vertex-colour regions, so they
remain legible at 320x240 without new textures or material changes.

build() receives a footprint-transformed Prop and its original source Prop.
Deck-machine medal variants share the shell and replace only the screen face:
call build_deck_front() for each independently selected original off/on image.
"""
from collections import Counter

IVORY=(223,231,223,255)
LIGHT=(246,245,219,255)
NAVY=(27,39,64,255)
BLACK=(20,25,32,255)
BLUE=(50,77,156,255)
CYAN=(91,206,212,255)
WOOD=(151,89,39,255)
GOLD=(202,160,65,255)
GREEN=(41,132,59,255)
VIOLET=(112,91,168,255)
RED=(201,55,49,255)
TEST_COLORS=((224,204,52,255),(54,185,91,255),(48,93,210,255),(207,62,63,255))


def shade(c,s):
    return tuple(round(v*s) for v in c[:3])+(255,)


def front(scene,x,y,z,w,h,color):
    scene.polygon([(x,y,z),(x+w,y,z),(x+w,y+h,z),(x,y+h,z)],color)


def panel(scene,x,y,z,w,h,background,regions=()):
    """Partition one facade into coloured rectangles; never overlay a face.

    Regions are normalised (left,bottom,right,top,color), disjoint by design.
    This lets a CRT have broad readout bars without z-fighting at native depth.
    """
    pieces=[(0.,0.,1.,1.,background)]
    for a,b,c,d,color in regions:
        out=[]
        for left,bottom,right,top,pigment in pieces:
            l=max(a,left);r=min(c,right);lo=max(b,bottom);hi=min(d,top)
            if l>=r or lo>=hi:
                out.append((left,bottom,right,top,pigment));continue
            if bottom<lo:out.append((left,bottom,right,lo,pigment))
            if hi<top:out.append((left,hi,right,top,pigment))
            if left<l:out.append((left,lo,l,hi,pigment))
            if r<right:out.append((r,lo,right,hi,pigment))
        out.append((a,b,c,d,color));pieces=out
    for a,b,c,d,color in pieces:
        front(scene,x+a*w,y+b*h,z,(c-a)*w,(d-b)*h,color)


def frame(scene,x,y,z,w,h,rim,color):
    """An open bezel with a real opening reserved for the screen."""
    front(scene,x,y,z,w,rim,color)
    front(scene,x,y+h-rim,z,w,rim,shade(color,1.04))
    front(scene,x,y+rim,z,rim,h-2*rim,shade(color,.78))
    front(scene,x+w-rim,y+rim,z,rim,h-2*rim,shade(color,.92))


def crt_shell(scene,x,z,w,d,y,h,color=IVORY,rim=.10):
    """A deep CRT hood: tapered rear, broad front bezel, no backing front."""
    rear=[(x+w*.10,y+h*.10,z),(x+w*.90,y+h*.10,z),
          (x+w*.90,y+h*.90,z),(x+w*.10,y+h*.90,z)]
    face=[(x,y,z+d),(x+w,y,z+d),(x+w,y+h,z+d),(x,y+h,z+d)]
    scene.polygon(list(reversed(rear)),shade(color,.62))
    for i in range(4):
        j=(i+1)%4
        scene.polygon([rear[i],rear[j],face[j],face[i]],shade(color,(.58,.80,1.0,.70)[i]))
    frame(scene,x,y,z+d,w,h,rim,color)
    return x+rim,y+rim,z+d,w-2*rim,h-2*rim


def screen(scene,rect,color,mode='readout'):
    x,y,z,w,h=rect
    if mode=='bars':
        # The source Science Club test card: four saturated, broad bands.
        panel(scene,x,y,z,w,h,color,
              [(i/4,.12,(i+1)/4,.92,c) for i,c in enumerate(TEST_COLORS)])
    elif mode=='active':
        panel(scene,x,y,z,w,h,color,
              [(0,.5,.5,1,CYAN),(.5,0,1,.5,CYAN)])
    else:
        panel(scene,x,y,z,w,h,color,
              [(.15,.62,.71,.75,CYAN),(.15,.34,.51,.44,shade(CYAN,.8))])


def control_deck(scene,x,z,w,d,y,color=IVORY):
    """Sloping keyboard with broad key rows cut into its owning surface."""
    def q(a,b,c,e,pigment):
        scene.polygon([(x+a*w,y+.13*(1-b),z+b*d),(x+c*w,y+.13*(1-b),z+b*d),
                       (x+c*w,y+.13*(1-e),z+e*d),(x+a*w,y+.13*(1-e),z+e*d)],pigment)
    # Partition all rows: two cyan/cream keys, a dark central divider.
    q(0,0,1,.18,color);q(0,.86,1,1,color)
    q(0,.18,.12,.86,color);q(.88,.18,1,.86,color)
    for j in range(2):
        b=.18+j*.34;e=b+.34
        q(.12,b,.88,b+.08,shade(color,.78))
        q(.12,b+.08,.46,e,LIGHT);q(.46,b+.08,.53,e,shade(color,.78));q(.53,b+.08,.88,e,LIGHT)
    # Own the front lip and wedge ends; no complete box beneath this slope.
    front(scene,x,y-.12,z+d,w,.12,shade(color,.70))
    front(scene,x,y-.12,z,w,.25,shade(color,.70))
    scene.polygon([(x,y-.12,z),(x,y-.12,z+d),(x,y,z+d),(x,y+.13,z)],shade(color,.60))
    scene.polygon([(x+w,y-.12,z+d),(x+w,y-.12,z),(x+w,y+.13,z),(x+w,y,z+d)],shade(color,.82))


def pedestal(scene,p,top=1.05,color=BLUE):
    x,z,w,d=p.x,p.y,p.w,p.depth
    scene.box(x,z,w,d,.15,NAVY,omit=(0,5))
    scene.box(x+.06,z+.04,w-.12,d-.08,top-.15,color,y=.15,omit=(0,5))
    # Two pieces leave the sloped keyboard free to own its surface.
    scene.box(x,z,w,d*.52,.12,IVORY,y=top,omit=(0,))
    control_deck(scene,x,z+d*.52,w,d*.48,top+.10,IVORY)


def computer(scene,p):
    x,z,w,d=p.x,p.y,p.w,p.depth
    if w<2.4:
        # The lobby and Dome terminal is the same small blue/white PC.
        # Its source NORTH interaction opens PCMenu; it is not a card slot.
        pedestal(scene,p,.90,BLUE)
        rect=crt_shell(scene,x+.10,z+.08,w-.20,d*.52,1.08,.95,IVORY,.11)
        screen(scene,rect,(31,69,172,255))
        return
    pedestal(scene,p,1.03,WOOD)
    # The original PC occupies the left side of the desk; the right-hand
    # equipment/storage remains a low ivory cabinet rather than a giant CRT.
    mw=min(1.75,w*.59);md=d*.52
    rect=crt_shell(scene,x+.14,z+.07,mw,md,1.18,1.12,IVORY,.13)
    screen(scene,rect,(31,69,172,255))
    sx=x+w*.70
    scene.box(sx,z+.04,w*.23,d*.44,.68,IVORY,y=1.15,omit=(0,3))
    panel(scene,sx,1.15,z+.04+d*.44,w*.23,.68,IVORY,
          [(.20,.55,.80,.70,NAVY),(.25,.22,.42,.39,GOLD)])


def instrument_desk(scene,p):
    """The black/brass articulated optical instrument in both source labs."""
    x,z,w,d=p.x,p.y,p.w,p.depth
    top=max(.95,p.height)
    scene.box(x,z,w,d,.16,WOOD,y=top-.16,omit=(0,))
    for px in (x+.16,x+w-.38):
        scene.box(px,z+.10,.22,d-.20,top-.16,shade(WOOD,.68),omit=(0,5))
    cx=x+w*.67;cz=z+d*.38
    scene.box(cx-.30,cz-.24,.60,.48,.10,NAVY,y=top,omit=(0,))
    # Fewer, broader segments keep the lamp/microscope silhouette readable.
    scene.box(cx-.09,cz-.08,.18,.16,.60,GOLD,y=top+.10,omit=(0,))
    scene.box(cx-.50,cz-.08,.59,.16,.15,BLACK,y=top+.70,omit=(0,))
    scene.box(cx-.59,cz-.15,.26,.30,.27,BLACK,y=top+.46,omit=(0,))
    scene.box(cx-.55,cz-.12,.18,.24,.045,CYAN,y=top+.42,omit=(0,))
    # The pale square in the reference is a separate low sample tray.
    scene.box(x+.24,z+d*.58,w*.22,d*.27,.06,LIGHT,y=top,omit=(0,))


def challenge_machine(scene,p):
    x,z,w,d=p.x,p.y,p.w,p.depth
    pedestal(scene,p,1.0,BLUE)
    for i in range(2):
        mw=(w-.42)/2;px=x+.14+i*(mw+.14)
        rect=crt_shell(scene,px,z+.14,mw,d*.46,1.16,.83,IVORY,.10)
        screen(scene,rect,(181,49,170,255),'active')
    # Exposed gold and ivory pipework is its source-specific top silhouette.
    py=2.05
    for i in range(3):
        px=x+.35+i*(w-.70)/2
        scene.box(px-.07,z+.22,.14,d*.34,.52,GOLD,y=py,omit=(0,))
    scene.box(x+.28,z+.22,w-.56,.14,.14,IVORY,y=py+.52,omit=(0,))


def science_bank(scene,p):
    x,z,w,d=p.x,p.y,p.w,p.depth
    # Two independent test-card instruments; the warm microscope bench is
    # handled by instrument_desk(), not reproduced from the projected crop.
    cell=w/2
    for i in range(2):
        px=x+i*cell
        scene.box(px+.06,z+.03,cell-.12,d-.06,.17,NAVY,omit=(0,5))
        scene.box(px+.12,z+.10,cell-.24,d-.20,.95,IVORY,y=.17,omit=(0,5))
        scene.box(px+.08,z+.08,cell-.16,d*.64,.12,CYAN,y=1.12,omit=(0,))
        mw=cell*.48
        rect=crt_shell(scene,px+.18,z+.15,mw,d*.46,1.24,1.48,IVORY,.12)
        screen(scene,rect,NAVY,'bars')
        # The cyan neck and return pipe in the original art are true 3D
        # vertical and horizontal square tubes, safely inside the footprint.
        pipe_x=px+cell*.80
        scene.box(pipe_x,z+.20,.16,d*.31,1.48,CYAN,y=1.24,omit=(0,))
        scene.box(px+cell*.62,z+.20,cell*.21,.16,.16,IVORY,y=2.72,omit=(0,))
        scene.box(px+cell*.70,z+d*.65,cell*.20,d*.24,.33,IVORY,y=1.13,omit=(0,3))
        front(scene,px+cell*.74,1.23,z+d*.89,cell*.12,.14,NAVY)


def test_machine(scene,p):
    x,z,w,d=p.x,p.y,p.w,p.depth
    scene.box(x,z,w,d,.13,NAVY,omit=(0,5))
    scene.box(x+.05,z+.04,w-.10,d-.08,.48,VIOLET,y=.13,omit=(0,3))
    # The piping partitions the front rather than hovering above a copy of
    # the original flat sprite. A large blue screen carries the silhouette.
    front_z=z+d-.04
    panel(scene,x+.05,.13,front_z,w-.10,.48,VIOLET,
          [(.06,.04,.15,.92,IVORY),(.15,.04,.45,.14,IVORY),
           (.42,.14,.51,.44,IVORY),(.51,.36,.85,.46,IVORY),
           (.76,.46,.85,.92,IVORY)])
    rect=crt_shell(scene,x+w*.17,z+d*.34,w*.46,d*.42,.61,.70,IVORY,.09)
    screen(scene,rect,(36,55,177,255),'plain')
    # Red caps and a second pale return elbow are shared across all four.
    for px in (x+w*.29,x+w*.75):
        scene.box(px-.07,z+.20,.14,.20,.70,IVORY,y=.61,omit=(0,5))
        scene.box(px-.11,z+.17,.22,.30,.16,RED,y=1.31,omit=(0,))


def scoreboard(scene,p):
    x,z,w,d=p.x,p.y,p.w,p.depth
    # A score board, not a deep green-screen computer. The broad screen and
    # warm plinth copy the source's silhouette without its 6x7-tile crop.
    scene.box(x,z,w,d,.20,shade(WOOD,.65),omit=(0,5))
    scene.box(x+.10,z+.08,w-.20,d-.16,.28,WOOD,y=.20,omit=(0,))
    bw=w*.76;left=x+(w-bw)/2;back=z+d*.25;depth=d*.38
    face_z=back+depth
    scene.box(left,back,bw,depth,2.17,GREEN,y=.48,omit=(0,3))
    rim=.17
    frame(scene,left,.48,face_z,bw,2.17,rim,shade(GREEN,.48))
    iw=bw-2*rim;ih=2.17-2*rim
    regions=[]
    for row in range(4):
        y=.13+row*.21
        regions += [(.10,y,.20,y+.07,GOLD),(.31,y,.72,y+.07,GOLD)]
    panel(scene,left+rim,.48+rim,face_z,iw,ih,shade(GREEN,.82),regions)


def deck_screen_color(img,source_p):
    """Select each printer state from its authoritative source screen pixels."""
    if img is None:return (39,57,157,255),'readout'
    p=source_p
    colors=Counter(img.at(p.x*8+i,p.y*8+j) for j in range(8,min(24,int(p.h*8))) for i in range(int(p.w*8)))
    magenta=sum(n for (r,g,b,a),n in colors.items() if r>120 and b>120 and g<80)
    green=sum(n for (r,g,b,a),n in colors.items() if g>90 and r<80 and b<100)
    if magenta:return (178,48,168,255),'active'
    if green:return (39,140,72,255),'readout'
    return (39,57,157,255),'readout'


def deck_screen_rect(p):
    x,z,w,d=p.x,p.y,p.w,p.depth
    return x+w*.24+.11,1.13+.11,z+d*.57,w*.52-.22,1.10-.22


def build_deck_front(scene,p,source_p=None,img=None,texture=None):
    """Emit only the owning screen; use for every off/on medal-state overlay.

    p must already contain footprint coordinates, just as build() receives.
    The dynamic shell has every bezel/hood/key/rack but no such screen, so
    exactly one independently selected state owns this plane at runtime.
    """
    color,mode=deck_screen_color(img,source_p or p)
    screen(scene,deck_screen_rect(p),color,mode)
    return True


def deck_machine(scene,p,source_p,img):
    x,z,w,d=p.x,p.y,p.w,p.depth
    pedestal(scene,p,.94,BLUE)
    rect=crt_shell(scene,x+w*.24,z+.08,w*.52,d*.57-.08,1.13,1.10,IVORY,.11)
    assert all(abs(a-b)<1e-7 for a,b in zip(rect,deck_screen_rect(p)))
    # Open black cartridge rack with four golden blanks above the tube.
    scene.box(x+w*.14,z+.10,w*.72,d*.36,.13,NAVY,y=2.27,omit=(0,))
    for i in range(4):
        cx=x+w*(.24+i*.17)
        scene.box(cx-w*.043,z+.15,w*.086,d*.25,.40,GOLD,y=2.40,omit=(0,))
    scene.box(x+w*.14,z+.10,w*.72,.12,.12,NAVY,y=2.80,omit=(0,))
    switched=getattr(scene,'switchable',False) and (
        (source_p.y==1 and source_p.x in (6,10,14,18)) or
        (source_p.y in (8,16) and source_p.x in (14,18)))
    if not switched:build_deck_front(scene,p,source_p,img)


def wall_display(scene,p):
    """Mason's permanently installed small green north-wall readout."""
    x,z,w,d=p.x,p.y,p.w,p.depth
    y=1.83;h=.91
    scene.box(x,z,w,d,h,NAVY,y=y,omit=(0,3))
    edge=.11
    frame(scene,x,y,z+d,w,h,edge,IVORY)
    screen(scene,(x+edge,y+edge,z+d,w-2*edge,h-2*edge),(31,113,49,255))


def build(scene,p,source_p=None,img=None,texture=None):
    source_p=source_p or p
    name=scene.name
    if name in ('club_lobby','pokemon_dome_entrance') and p.kind=='machine':
        computer(scene,p)
    elif name in ('deck_machine_room','deck_machine_room_dynamic') and p.kind=='machine':
        deck_machine(scene,p,source_p,img)
    elif name=='mason_laboratory':
        if p.kind=='wall_display':wall_display(scene,p)
        elif p.kind=='machine' and source_p.x==2:computer(scene,p)
        elif p.kind=='machine' and source_p.x==10:challenge_machine(scene,p)
        elif p.kind=='table' and source_p.y==2:instrument_desk(scene,p)
        else:return False
    elif name=='science_club':
        if p.kind=='table' and source_p.y==1:instrument_desk(scene,p)
        elif p.kind=='machine' and source_p.y==1:science_bank(scene,p)
        elif p.kind=='machine' and source_p.y==20:test_machine(scene,p)
        elif p.kind=='machine':
            # The small source colour-bar instrument uses the same CRT hood.
            pedestal(scene,p,.62,VIOLET)
            rect=crt_shell(scene,p.x+.15,p.y+.08,p.w-.30,p.depth*.42,.84,.65,IVORY,.09)
            screen(scene,rect,NAVY,'bars')
        else:return False
    elif name=='challenge_hall' and p.kind=='machine':scoreboard(scene,p)
    else:return False
    return True
