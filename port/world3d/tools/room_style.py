"""Small painted materials and baked light shared by cartridge and WebGL."""
import math

TILES={
    'mason_laboratory':(167,194,201),
    'deck_machine_room':(167,194,201),
    'club_lobby':(171,180,153),
    'club_foyer':(196,185,161),
}
FLOOR_TINT={
    'lightning_club':(.97,.97,.97),
    'mason_laboratory':(.86,.90,.95),
    'deck_machine_room':(.86,.90,.95),
    'club_lobby':(.87,.90,.83),
    'club_foyer':(.90,.90,.86),
    'psychic_club':(.88,.82,.98),
    'hall_of_honor':(.85,.79,.69),
    'water_club':(.81,.84,.81),
    'fire_club':(.88,.80,.75),
    'grass_club':(.95,1.0,.90),
}
WALL_PAINT={
    'mason_laboratory':(163,159,130),
    'deck_machine_room':(163,159,130),
    'club_lobby':(177,165,142),
    'club_foyer':(177,165,142),
    'water_club':(170,192,200),
    'psychic_club':(136,111,170),
    'fire_club':(123,151,185),
    'science_club':(133,170,151),
    'grass_club':(141,165,115),
    'rock_club':(162,137,111),
    'hall_of_honor':(160,133,91),
}

def style_name(slug):
    if slug=='deck_machine_room_dynamic':return 'deck_machine_room'
    if slug.startswith('emblem_'):return 'club_foyer'
    return slug

def material_pixel(slug,x,y,source,is_plain):
    slug=style_name(slug)
    if slug in TILES and is_plain:
        base=TILES[slug]
        # Two source tiles per flagstone, soft grout and restrained grain.
        grain=((x*17+y*29)%7-3)*.45
        value=1+(.025 if (x//16+y//16)%2 else -.025)
        if x%16==0 or y%16==0:value*=.80
        elif x%16==1 or y%16==1:value*=1.035
        return tuple(round(c*value+grain) for c in base)+(255,)
    r,g,b,a=source
    if slug=='psychic_club':
        if r>155 and g>110 and r<g*1.6:
            v=max(r,g,b)/255
            return tuple(round(c*v) for c in ((190,178,211) if b>150 else (151,134,181)))+(a,)
    if slug=='hall_of_honor' and r>150 and g>85 and r<g*1.8:
        v=max(r,g,b)/255
        return tuple(round(c*v) for c in ((201,182,140) if b>120 else (174,143,91)))+(a,)
    if slug=='grass_club':
        if g>r*1.2 and g>b*1.2:
            return tuple(round(c*(.70+.30*g/255)) for c in (123,176,77))+(a,)
        if min(r,g,b)>210:return (199,204,151,a)
    # Leave source motifs intact while calming saturated painted surfaces.
    luminance=.25*r+.60*g+.15*b
    return tuple(round(c*.88+luminance*.12) for c in (r,g,b))+(a,)

def wall_paint(slug):
    return WALL_PAINT.get(style_name(slug),(169,151,132))+(255,)

def smooth(a,b,x):
    t=max(0,min(1,(x-a)/(b-a)))
    return t*t*(3-2*t)

def bake_lighting(scene,width,height):
    """Only environment vertex pigments change; actor rigs are never graded."""
    tint=FLOOR_TINT.get(style_name(scene.name),(.85,.85,.85))
    props=scene.props
    def light(v):
        x,y,z=v[:3]
        if y<.085:
            distance=max(0,min(x-1.8,width-1.8-x,z-1.8,height-2-z))
            ambient=.60+.40*smooth(0,3,distance)
            contact=1.0
            glow=0.0
            for p in props:
                depth=min(p['depth'],p['h'])
                a,b=p['x'],p['y']+p['h']-depth
                dx=max(a-x,0,x-a-p['w']);dz=max(b-z,0,z-b-depth)
                strength=.10 if p['kind'] in ('table','chair','bench','rack') else .24
                contact=min(contact,1-strength*math.exp(-(math.hypot(dx,dz)/.7)**2))
                if p['kind'] in ('torch','brazier'):
                    glow=max(glow,.15*math.exp(-((x-a-p['w']/2)/1.6)**2-((z-b-depth/2)/1.6)**2))
            aisle=1+.10*(1-smooth(1.4,4,abs(x-width/2)))
            focal=1+.08*math.exp(-((x-width/2)/4)**2-((z-height*.25)/4)**2)
            if scene.name=='hall_of_honor':
                focal+=.22*math.exp(-((x-12)/5)**2-((z-12)/5)**2)
            return tuple(min(1.12,ambient*contact*aisle*focal*c+glow) for c in tint)
        if z>height-(3.9 if scene.name=='lightning_club' else 2.2):return (.43,)*3
        if x<1.91 or x>width-1.91 or z<1.91:
            value=.86-.43*smooth(1.3,5,y)
            return (value,)*3
        # A gentle base gradient grounds shaded primitive props and cabinets.
        value=.76+.24*smooth(0,1.6,y)
        if scene.name in ('fire_club','psychic_club') and y>1.5 and v[5]>170 and (v[6]>100 or v[7]>180):value=1
        return (value,)*3
    for key,(vertices,indices,cache) in list(scene.batches.items()):
        if key[0]:continue
        result=[]
        for v in vertices:
            factors=light(v)
            result.append((*v[:5],*(max(0,min(255,round(c*f))) for c,f in zip(v[5:8],factors)),v[8]))
        scene.batches[key]=(result,indices,{})
