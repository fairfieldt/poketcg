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


def P(kind,x,y,w=2,h=4,height=2,depth=2):
    return Prop(kind,x,y,w,h,height,depth)

FOYER=[P('palm',16,10,2,4,2)]
LOBBY=[P('counter',2,6,12,4,1.05,3),P('counter',12,2,2,7,1.05,2),
       P('machine',2,7,2,4,2),P('table',8,14,4,5,.85,4),
       *[P('chair',x,y,2,2,1) for x,y in [(6,14),(12,14),(6,17),(12,17)]],
       *[P('cabinet',x,1,2,4,2) for x in (20,22,24)],
       P('palm',2,18,2,4,2),P('palm',24,18,2,4,2)]
PROPS={
'mason_laboratory':[
    P('machine',2,1,4,4,2),P('table',4,2,6,3,1,2),
    *[P('cabinet',x,1,3,4,2) for x in (17,20,23)],
    *[P('cabinet',x,14,2,3,1.7) for x in (20,22,24)],
    P('counter',18,26,8,2,.8),P('machine',8,14,5,6,2.4,3),P('palm',2,22,3,5,2.4),
    *[P('balloon',x,y,1,1,1.8,.3) for x,y in [(5,14),(11,14),(5,17),(11,17)]]],
'deck_machine_room':[
    *[P('machine',x,1,4,4,1.8) for x in (2,6,10,14,18)],
    *[P('machine',x,y,4,5,1.8) for x,y in [(14,8),(18,8),(2,16),(14,16),(18,16)]]],
'ishiharas_house':[
    *[P('cabinet',x,1,2,4,2) for x in (5,7,9,11,13,15)],
    *[P('cabinet',x,12,2,3,1.7) for x in (2,4,6,12,14,16)],
    P('table',2,1,3,4,1),P('palm',16,8,2,4,2),P('shrub',2,18,2,2,.8),P('shrub',4,18,2,2,.8)],
'fighting_club':[
    *[P('weight',x,y,2,2,.6) for x,y in [(20,3),(2,3),(20,5),(2,7),(20,7),(20,9)]],
    P('balloon',2,5,2,2,1.2),
    *[P('weight',x,12,1,2,.25,1) for x in range(3,22,2)]],
'rock_club':[
    *[P('statue',x,y,2,4,2.3) for x in (10,16) for y in (3,10,18)],
    *[P('boulder',x,y,4,4,1.6,3) for x in (2,22) for y in (3,22)],
    *[P('boulder',x,10,4,7,2.5,4) for x in (2,22)],
    *[P('boulder',x,y,1,1,.35,1) for x,y in [(4,5),(23,5),(6,13),(21,13),(4,23),(23,23)]]],
'water_club':[
    *[P('palm',x,1,2,3,4.4) for x in (4,6)],
    P('bench',20,4,5,3,1.1),P('bench',5,20,5,3,1.1),P('bench',22,22,4,5,1.1),
    *[P('horse',x,y,w,h,1.5) for x,y,w,h in [(20,2,4,2),(4,18,4,3),(24,14,2,3)]],
    *[P('shrub',x,y,2,1,.7,1) for x in (14,16) for y in (11,12)],P('palm',2,24,2,4,2)],
'grass_club':[
    *[P('bed',x,y,w,h,.18,h) for x,y,w,h in [(4,3,6,2),(14,3,10,2),(24,6,2,5),(2,8,2,5),(24,14,2,5),(2,14,2,5)]],
    *[P('topiary',x,y,2,3,1.7) for y,xs in [(6,(14,16,18)),(10,(14,16,18)),(16,(6,8,10,12,18,20)),(20,(6,8,10,12,18,20))] for x in xs],
    *[P('flowers',x,y,2,2,.7) for x,y in [(8,8),(10,8),(8,10),(10,10),(2,24),(24,24),(2,26),(24,26)]]],
'psychic_club':[
    *[P('torch',x,y,2,5,2.7) for x,y in [(10,4),(16,4),(6,6),(20,6),(6,14),(20,14),(10,16),(16,16)]]],
'science_club':[
    P('table',2,1,4,4,1),P('machine',7,1,9,4,1.8),P('machine',18,1,8,4,1.8),
    P('cabinet',2,13,4,4,1.6),P('counter',7,12,5,2,.8),P('counter',12,12,5,2,.8),
    P('machine',12,9,4,2,.9),*[P('machine',x,20,3,3,1.3) for x in (14,17,20,23)]],
'fire_club':[*[P('brazier',x,y,2,5,2.3) for x in (6,20) for y in (4,12,20)]],
'challenge_hall':[
    P('machine',13,6,6,7,3,2),P('balloon',12,8,2,2,1),P('balloon',18,8,2,2,1),
    *[P('palm',x,y,2,3,3) for x in (4,26) for y in (3,6,9)],
    P('steps',8,14,4,5,.5,4),P('steps',19,14,4,5,.5,4)],
'pokemon_dome_entrance':[
    *[P('cabinet',x,y,2,4,1.6) for x in (2,4,6) for y in (1,8)],
    P('machine',29,1,2,4,1.6),P('counter',22,1,2,2,.8),P('counter',24,1,2,2,.8),P('palm',28,11,2,4,2)],
'pokemon_dome':[
    P('table',14,12,4,7,.8,6),P('serpent',12,14,2,2,1),P('serpent',18,14,2,2,1),
    *[P('shrub',x,y,2,2,1) for x in (1,28) for y in range(9,21,2)]],
'hall_of_honor':[
    P('altar',7,9,10,6,1.8,4),P('pillar',7,10,2,5,3),P('pillar',15,10,2,5,3),
    P('torch',8,8,1,1,2.5,1),P('torch',14,8,1,1,2.5,1),P('ball',11,10,4,3,1.5,2)]}

# Original sprites encode their height north of their feet. These are the same
# two-tile feet used by the collision/script grid, not giant solid billboards.
# Terrain/water/flower beds keep their complete footprint.
