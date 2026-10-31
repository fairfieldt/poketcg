"""Small painted materials and baked light shared by cartridge and WebGL."""
import math

TILES={
    'mason_laboratory':(167,194,201),
    'deck_machine_room':(167,194,201),
    'club_lobby':(171,180,153),
    'club_foyer':(196,185,161),
    'lightning_club':(43,59,161),
    'water_club':(203,173,116),
    'psychic_club':(183,168,200),
    'fire_club':(213,178,136),
    'rock_club':(151,179,202),
    'science_club':(183,164,205),
    'fighting_club':(100,161,106),
    'pokemon_dome':(181,57,48),
    'challenge_hall':(211,176,126),
    'hall_of_honor':(213,190,143),
    'ishiharas_house':(201,175,119),
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
    if slug=='pokemon_dome_entrance':return 'pokemon_dome'
    if slug.startswith('emblem_'):return 'club_foyer'
    return slug

def material_pixel(slug,x,y,source,is_plain):
    slug=style_name(slug)
    if slug in ('rock_club','psychic_club','fire_club') and 100<=x<124 and y>=(24 if slug=='rock_club' else 32):
        # Retain the source runner footprint and its four-pixel edge binding.
        # Its old white one-pixel diagonal weave aliased along the entire
        # room; use a broad, quiet chevron inside the existing border.
        phase=(y+abs(x-112))%32
        band=1-smooth(1.0,3.0,abs(phase-16))
        base=(161,35,34);thread=(199,104,66)
        return tuple(round(a+(b-a)*band*.62) for a,b in zip(base,thread))+(255,)
    if slug=='lightning_club' and 100<=x<124 and y>=112:
        # The electric-blue runner keeps its cyan/cream outer border and
        # silhouette, with a four-tile interior chevron rather than white
        # single-texel zigzags. Imported prototype UVs use these same pixels.
        phase=(y+abs(x-112))%32
        band=1-smooth(1.0,3.0,abs(phase-16))
        base=(35,49,128);thread=(102,158,206)
        pigment=tuple(a+(b-a)*band*.62 for a,b in zip(base,thread))
        # The original central light pool crosses the runner. Apply its
        # wash after repainting the cloth, rather than erasing that half
        # of the pool while the adjacent source floor stays illuminated.
        pool=1-smooth(15,17,math.hypot(x-119.5,y-151.5))
        return tuple(round(c+(lit-c)*pool*.72) for c,lit in
                     zip(pigment,(241,248,255)))+(255,)
    if slug=='rock_club' and (72<=x<96 or 128<=x<152) and y>=24:
        # These are the two continuous sand-colored strips beside the runner,
        # including the floor restored below statue art. Replace white/gold
        # pixel stippling with low-contrast broad mineral variation.
        value=1+.025*math.sin(x*math.pi/16)+.025*math.cos(y*math.pi/24)
        return tuple(round(c*value) for c in (205,183,139))+(255,)
    if slug in TILES and is_plain:
        base=TILES[slug]
        # Two source tiles per flagstone. The original one-tile bright/dark
        # source patterns collapse into moire at the native far wall; broad,
        # low-contrast surfaces remain legible after the RDP and VI filters.
        grain=((x*17+y*29)%7-3)*.45
        value=1+(.025 if (x//16+y//16)%2 else -.025)
        def blend(pigment,weight):
            return tuple(round(a*(1-weight)+b*weight+grain) for a,b in zip(base,pigment))+(255,)
        if slug in ('fire_club','challenge_hall'):
            # Keep the source diamond livery, enlarged to a four-tile period.
            # Soft, two-texel bands survive minification better than its
            # repeating one-texel cream/orange diagonals.
            u=abs(x%32-16);v=abs(y%32-16);diamond=u+v
            ring=1-smooth(.8,2.4,abs(diamond-13))
            if slug=='fire_club':
                return blend((177,119,78),ring*.72)
            center=1-smooth(2.4,4.2,diamond)
            cream=(237,218,175)
            pigment=tuple(a*(1-center)+b*center for a,b in zip(cream,(177,137,89)))
            return blend(pigment,max(ring*.85,center*.55))
        elif slug=='pokemon_dome':
            # Broad gold octagons retain the original carpet's identity.
            # Their period is four source tiles, with a restrained dark
            # medallion instead of a high-contrast single-pixel rosette.
            u=abs(x%32-16);v=abs(y%32-16)
            octagon=max(u,v,(u+v)*math.sqrt(.5))
            ring=1-smooth(.8,2.3,abs(octagon-11))
            center=1-smooth(2.2,4.2,octagon)
            pigment=tuple(a*(1-center)+b*center for a,b in zip((204,128,79),(119,38,38)))
            return blend(pigment,max(ring*.82,center*.7))
        elif slug=='fighting_club':
            # Large staggered tatami mats, with broad binding and a faint
            # weave, retain the gym's distinctive green floor.
            u=(x+(16 if int(y//16)%2 else 0))%32;v=y%16
            binding=1-smooth(.7,1.8,min(u,32-u,v,16-v))
            grain=math.sin((x if int(y//16)%2 else y)*math.pi/4)*.8
            return blend((58,111,70),binding*.68)
        elif slug in ('water_club','ishiharas_house'):
            # Broad parquet blocks retain the warm timber identity without
            # a high-contrast one-pixel weave on every source tile.
            along=x if (x//32+y//32)%2 else y
            across=y if (x//32+y//32)%2 else x
            value=1+(.035 if (x//32+y//32)%2 else -.035)
            if across%16==0 or along%32==0:value*=.88
            grain=math.sin(along*math.pi/8)*1.4
        elif x%16==0 or y%16==0:value*=.88
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
        # Keep tiny flowers and flecks, but bring their value close enough to
        # the grass that they do not sparkle into white/yellow checkerboards.
        if min(r,g,b)>210:return (159,184,111,a)
        if r>210 and g>190 and b<170:return (169,187,112,a)
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
                a,b,right,south=p.get('solid_bounds',(p['x'],p['y']+p['h']-min(p['depth'],p['h']),p['x']+p['w'],p['y']+p['h']))
                dx=max(a-x,0,x-right);dz=max(b-z,0,z-south)
                strength=.10 if p['kind'] in ('table','chair','bench','rack') else .24
                contact=min(contact,1-strength*math.exp(-(math.hypot(dx,dz)/.7)**2))
                if p['kind'] in ('torch','brazier'):
                    glow=max(glow,.15*math.exp(-((x-(a+right)/2)/1.6)**2-((z-(b+south)/2)/1.6)**2))
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
