"""Hand-reviewed elevations from port/maps/*.txt; source rectangles are tile units.

Every repeated prop uses one mesh recipe; only placement, crop and color differ.
Projected source height is never treated as a solid floor footprint.
"""
from dataclasses import dataclass

@dataclass(frozen=True)
class Prop:
    kind: str
    x: float
    y: float
    w: float=2
    h: float=4
    height: float=2
    depth: float=2
    floor: tuple | None=None
    solid: bool=True


def P(kind,x,y,w=2,h=4,height=2,depth=2,*,floor=None,solid=True):
    return Prop(kind,x,y,w,h,height,depth,floor,solid)


def F(x,z,right=None,south=None):
    """Body around blocked source anchors, with room for a passing actor.

    Logical actors stand at the even grid coordinates themselves. Their image
    crops include fake elevation and are deliberately independent of this box.
    Return left/north/width/depth, not a replacement texture crop.
    """
    return (x-.85,z-.85,(x if right is None else right)-x+1.7,
            (z if south is None else south)-z+1.7)


def footprint(p):
    return p.floor or (p.x,p.y+p.h-min(p.depth,p.h),p.w,min(p.depth,p.h))

FOYER=[P('palm',16,10,2,4,2,floor=F(16,12))]
LOBBY=[P('counter',2,6,12,4,1.05,3,floor=F(2,6,12,6)),
       P('counter',12,2,2,7,1.05,2,floor=F(12,2,12,4)),
       P('machine',2,7,2,4,2,floor=F(2,8)),
       P('table',8,14,4,5,.85,4,floor=F(8,14,10,16)),
       # Source chair cells are walkable (and host standing NPCs). Tuck the
       # smaller 3D chairs under the table, inside its existing blocked cells.
       *[P('chair',x,y,2,2,1,floor=(cx-.45,cz-.45,.9,.9))
         for x,y,cx,cz in [(6,14,7.65,13.65),(12,14,10.35,13.65),
                           (6,17,7.65,16.35),(12,17,10.35,16.35)]],
       *[P('cabinet',x,1,2,4,2,floor=F(x,2)) for x in (20,22,24)],
       P('palm',2,18,2,4,2,floor=F(2,18,2,20)),
       P('palm',24,18,2,4,2,floor=F(24,18,24,20))]
PROPS={
'mason_laboratory':[
    P('machine',2,1,4,4,2,floor=F(2,2,4,2)),
    P('table',4,2,6,3,1,2,floor=F(6,2,8,2)),
    *[P('cabinet',x,1,2,4,2,floor=F(x,2)) for x in (18,20,22,24)],
    *[P('cabinet',x,14,2,3,1.7,floor=F(x,14)) for x in (20,22,24)],
    P('counter',18,26,8,2,.8,floor=F(18,26,24,26)),
    P('table',6,14,4,5,.85,4,floor=F(6,14,8,16)),
    P('palm',2,22,3,5,2.4,floor=F(2,22,2,24)),
    *[P('balloon',x,y,2,2,1.8,.3,floor=(cx-.35,cz-.35,.7,.7))
      for x,y,cx,cz in [(4,14,5.65,13.65),(10,14,8.35,13.65),
                        (4,17,5.65,16.35),(10,17,8.35,16.35)]]],
'deck_machine_room':[
    *[P('machine',x,1,4,4,1.8,floor=F(x,2,x+2,2)) for x in (2,6,10,14,18)],
    *[P('machine',x,y,4,5,1.8,floor=F(x,y,x+2,y+2)) for x,y in [(14,8),(18,8),(2,16),(14,16),(18,16)]]],
'ishiharas_house':[
    *[P('cabinet',x,1,2,4,2,floor=F(x-1,2)) for x in (5,7,9,11,13,15)],
    *[P('cabinet',x,12,2,3,1.7,floor=F(x,12)) for x in (2,4,6,12,14,16)],
    P('table',2,1,3,4,1,floor=F(2,2)),
    P('palm',16,8,2,4,2,floor=F(16,8,16,10)),
    P('shrub',2,18,2,2,.8,floor=F(2,18)),P('shrub',4,18,2,2,.8,floor=F(4,18))],
'fighting_club':[
    *[P('weight',x,y,2,2,.6,floor=F(x,y-1)) for x,y in [(20,3),(2,3),(20,5),(2,7),(20,7),(20,9)]],
    P('balloon',2,5,2,2,1.2,floor=F(2,4)),
    P('rack',2,12,8,2,1.05,1.4,floor=F(2,14,8,14)),
    P('rack',14,12,8,2,1.05,1.4,floor=F(14,14,20,14))],
'rock_club':[
    *[P('statue',x,y,2,4,2.3,floor=F(x,2 if y==3 else y,x,4 if y==3 else y+2)) for x in (10,16) for y in (3,10,18)],
    *[P('boulder',x,y,4,4,1.6,3,floor=F(2 if x==2 else 22,2 if y==3 else 24,4 if x==2 else 24,2 if y==3 else 24)) for x in (2,22) for y in (3,22)],
    *[P('boulder',x,10,4,7,2.5,4,floor=F(2 if x==2 else 24,10,2 if x==2 else 24,14)) for x in (2,22)],
    *[P('boulder',x,12,2,2,1.1,floor=F(x,12)) for x in (4,22)]],
'water_club':[
    *[P('palm',x,1,2,3,4.4,floor=F(x,2)) for x in (4,6)],
    P('bench',20,4,5,3,1.1,floor=F(22,4,24,4)),
    P('bench',5,20,5,3,1.1,floor=F(6,20,8,20)),
    P('bench',22,22,4,5,1.1,floor=F(22,22,24,24)),
    *[P('horse',x,y,w,h,1.5,floor=F(x,y)) for x,y,w,h in [(20,2,4,2),(4,18,4,3),(24,14,2,3)]],
    *[P('shrub',x,11,2,2,.7,floor=F(x,10)) for x in (14,16)],
    P('palm',2,24,2,4,2,floor=F(2,24,2,26))],
'grass_club':[
    *[P('bed',x,y,w,h,.18,h,floor=f) for x,y,w,h,f in [(4,3,6,2,F(4,2,8,2)),(14,3,10,2,F(14,2,22,2)),(24,6,2,5,F(24,6,24,8)),(2,8,2,5,F(2,8)),(24,14,2,5,F(24,14,24,16)),(2,14,2,5,F(2,14,2,16))]],
    *[P('topiary',x,y,2,3,1.7,floor=F(x,y)) for y,xs in [(6,(14,16,18)),(10,(14,16,18)),(16,(6,8,10,12,18,20)),(20,(6,8,10,12,18,20))] for x in xs],
    *[P('flowers',x,y,2,2,.7,floor=F(x,y)) for x,y in [(8,8),(10,8),(8,10),(10,10),(2,24),(24,24),(2,26),(24,26)]]],
'psychic_club':[
    *[P('torch',x,y,2,5,2.7,floor=F(x,y,x,y+2)) for x,y in [(10,4),(16,4),(6,6),(20,6),(6,14),(20,14),(10,16),(16,16)]]],
'science_club':[
    P('table',2,1,4,4,1,floor=F(2,2,4,2)),
    P('machine',7,1,9,4,1.8,floor=F(8,2,16,2)),
    P('machine',18,1,8,4,1.8,floor=F(18,2,24,2)),
    P('cabinet',2,13,4,4,1.6,floor=F(2,12,4,14)),
    P('counter',7,12,5,2,.8,floor=F(6,12)),
    P('counter',12,12,5,2,.8,floor=F(10,12,16,12)),
    P('counter',16,4,2,6,1.1,floor=F(16,4,16,8)),
    P('machine',12,9,4,2,.9,floor=F(16,10)),
    *[P('machine',x,20,3,3,1.3,floor=(13.15+3*i,19.15,2.7,1.7)) for i,x in enumerate((14,17,20,23))]],
'fire_club':[*[P('brazier',x,y,2,5,2.3,floor=F(x,y,x,y+2)) for x in (6,20) for y in (4,12,20)]],
'challenge_hall':[
    P('machine',13,6,6,7,3,2,floor=F(14,6,16,10)),
    P('balloon',12,8,2,2,1,floor=(13.2,7.65,.7,.7)),
    P('balloon',18,8,2,2,1,floor=(16.1,7.65,.7,.7)),
    *[P('palm',x,y,2,3,3,floor=F(x,2 if y==3 else 10 if y==9 else y)) for x in (4,26) for y in (3,6,9)],
    P('steps',8,14,4,5,.5,4,floor=F(8,14,8,16)),
    P('steps',19,14,4,5,.5,4,floor=F(22,14,22,16))],
'pokemon_dome_entrance':[
    *[P('cabinet',x,y,2,4,1.6,floor=F(x,2 if y==1 else 8)) for x in (2,4,6) for y in (1,8)],
    P('machine',29,1,2,4,1.6,floor=F(28,2)),
    P('counter',22,1,2,2,.8,floor=(21.15,.15,1.7,.9)),
    P('counter',24,1,2,2,.8,floor=(23.15,.15,1.7,.9)),
    P('palm',28,11,2,4,2,floor=F(28,10,28,12))],
'pokemon_dome':[
    P('table',14,12,4,7,.8,6,floor=F(14,12,16,16)),
    P('serpent',12,14,2,2,1,floor=(13.15,13.15,1.7,1.7)),
    P('serpent',18,14,2,2,1,floor=(15.15,13.15,1.7,1.7)),
    *[P('shrub',x,y,2,2,1,floor=F(2 if x==1 else 28,y-1)) for x in (1,28) for y in range(9,19,2)]],
'hall_of_honor':[
    # This is the Legendary Auto Deck Machine (Script_fbe1), not a banner.
    P('legendary_machine',10,1,4,4,3.35,1.7,floor=F(10,2,12,2)),
    # Include the projected side feet when erasing the old sprite from the
    # floor. The independent canonical solid footprint remains unchanged.
    P('altar',6,9,12,6,1.8,4,floor=F(8,8,14,10)),
    P('pillar',7,10,2,5,3,floor=F(6,10,8,12)),
    P('pillar',15,10,2,5,3,floor=F(14,10,16,12)),
    P('torch',8,8,1,1,2.5,1,floor=F(8,8)),
    P('torch',14,8,1,1,2.5,1,floor=F(14,8)),
    P('ball',11,10,4,3,1.5,2,floor=F(10,8,12,10),solid=False)]}

# Original sprites encode their height north of their feet. These are the same
# two-tile feet used by the collision/script grid, not giant solid billboards.
# Terrain/water/flower beds keep their complete footprint.
