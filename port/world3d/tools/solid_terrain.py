"""Special room geometry aligned to the original two-tile movement grid.

The Lightning room predates the shared prop recipes; its imported triangles
need the same explicit floor footprints as those recipes. No gameplay data or
actor geometry is changed here.
"""
import math


def pool_rows():
    """Unit water cells, leaving the source's y=16 promenade dry."""
    # The two shrubs root at (14,10)/(16,10), on the inner dry ledge.
    return [(z, max(3, 13-z), 16 if z < 9 else 12)
            for z in range(3, 15)]


POOL_CELLS = {(x, z) for z, left, right in pool_rows()
              for x in range(left, right+1)}


def swimming_pool(scene, texture, rgb, white):
    rows = []
    for y in range(32):
        row = []
        for x in range(32):
            wave = math.sin((x+y*2)*math.tau/16)*.5+.5
            glint = 10 if (x+y*3) % 29 < 2 else 0
            row.append((28+glint, 90+round(wave*15)+glint,
                        170+round(wave*23)+glint, 255))
        rows.append(row)
    tex = texture(rows)
    for z, left, right in pool_rows():
        scene.polygon([(left, -.24, z), (right+1, -.24, z),
                       (right+1, -.24, z+1), (left, -.24, z+1)],
                      tex=tex, uv=[(left/4, z/4), ((right+1)/4, z/4),
                                   ((right+1)/4, (z+1)/4), (left/4, (z+1)/4)])
    coping=[]
    for x, z in sorted(POOL_CELLS):
        edges = [((x, z-1), (x, z), (x+1, z), (0, -.15)),
                 ((x+1, z), (x+1, z), (x+1, z+1), (.15, 0)),
                 ((x, z+1), (x+1, z+1), (x, z+1), (0, .15)),
                 ((x-1, z), (x, z+1), (x, z), (-.15, 0))]
        for neighbor, a, b, outward in edges:
            if neighbor in POOL_CELLS:
                continue
            ax, az = a
            bx, bz = b
            dx, dz = outward
            scene.polygon([(ax, -.44, az), (bx, -.44, bz),
                           (bx, .06, bz), (ax, .06, az)], rgb('688e9c'))
            coping.append((min(ax,bx,ax+dx,bx+dx),min(az,bz,az+dz,bz+dz),
                           max(ax,bx,ax+dx,bx+dx),max(az,bz,az+dz,bz+dz)))
    # At each inward stair corner, the old horizontal edge strips overlapped
    # another strip at exactly y=.07. Partition their union into disjoint
    # runs so each coping pixel has one owning surface.
    levels=sorted({round(value,6) for a,b,c,d in coping for value in (b,d)})
    strips=[]
    for north,south in zip(levels,levels[1:]):
        intervals=sorted((a,c) for a,b,c,d in coping if b<(north+south)/2<d)
        merged=[]
        for left,right in intervals:
            if merged and left<=merged[-1][1]+1e-6:
                merged[-1]=(merged[-1][0],max(right,merged[-1][1]))
            else:merged.append((left,right))
        for left,right in merged:
            if strips and strips[-1][0]==left and strips[-1][2]==right and abs(strips[-1][3]-north)<1e-6:
                strips[-1]=(left,strips[-1][1],right,south)
            else:strips.append((left,north,right,south))
    for left,north,right,south in strips:
        scene.polygon([(left,.07,north),(right,.07,north),
                       (right,.07,south),(left,.07,south)],white)
    for x, z, heading in ((16.35, 5.8, 1), (4.8, 12.7, -1)):
        scene.ball(x, z, -.17, .45, .075, .18, rgb('94d5df'), segments=6, rings=3)
        scene.polygon([(x-heading*.33, -.10, z),
                       (x-heading*.68, -.10, z-.22),
                       (x-heading*.68, -.10, z+.22)], rgb('477fbc'))
    for z, left, right in pool_rows():
        scene.props.append(dict(kind='pool', x=left, y=z, w=right-left+1,
                                h=1, height=.07, depth=1, solid=True,
                                floor=(left-.15, z-.15, right-left+1.3, 1.3),
                                solid_bounds=(left-.15, z-.15, right+1.15, z+1.15)))


LIGHTNING_BOUNDS = (
    ('pillar', (7.15, 13.15, 8.85, 22.85), 4.17),
    ('pillar', (17.15, 13.15, 18.85, 22.85), 4.17),
    ('pillar', (3.15, 3.15, 4.85, 4.85), 1.97),
    ('pillar', (21.15, 3.15, 22.85, 4.85), 1.97),
    ('altar', (7.15, 1.15, 18.85, 2.85), 1.7),
)

LIGHTNING_WALL_BOUNDS = (
    ('wall', (0, 0, 28, 1.31), 4.5),
    ('wall', (0, 2, 1.31, 30), 4.5),
    ('wall', (24.79, 2, 28, 30), 4.5),
    ('wall', (-.05, 26.96, 11.15, 31.04), 1.3),
    ('wall', (14.84, 26.96, 28.06, 31.04), 1.3),
)


def _inside(vertices, left, north, right, south):
    return all(left-.001 <= v[0] <= right+.001 and
               north-.001 <= v[2] <= south+.001 for v in vertices)


def _lightning_triangle(vertices):
    """Classify before moving, so shared batch materials stay unchanged."""
    if (all(abs(v[1]) < .0001 for v in vertices) and
        max(v[0] for v in vertices)-min(v[0] for v in vertices) > 3.999 and
        max(v[2] for v in vertices)-min(v[2] for v in vertices) > 3.999 and
        min(v[0] for v in vertices) >= 1.999 and
        max(v[0] for v in vertices) <= 26.001 and
        min(v[2] for v in vertices) >= 1.999):
        # Extend the edge floor beneath a person standing at x=2 or z=2.
        return lambda x, z: (1 if abs(x-2) < .001 else x,
                             1 if abs(z-2) < .001 else 31 if abs(z-30) < .001 else z)
    for old_x, old_z, new_x in ((9, 20, 8), (18, 20, 18),
                                 (5, 4, 4), (23, 4, 22)):
        if _inside(vertices, old_x-1.081, old_z-1.081,
                   old_x+1.081, old_z+1.081):
            return lambda x, z, ox=old_x, oz=old_z, nx=new_x: (
                nx+(x-ox)*1.7/2.16, oz+(z-oz)*1.7/2.16)
    if _inside(vertices, 8, 2, 20, 3.701):
        # The badge quads used to float .2 units ahead of the open box front.
        if all(abs(v[2]-3.7) < .001 for v in vertices):
            return lambda x, z: (7.15+(x-8)*11.7/12, 2.85)
        if min(v[2] for v in vertices) >= 3.49:
            return lambda x, z: (7.15+(x-8)*11.7/12, 2.77+(z-3.5))
        return lambda x, z: (7.15+(x-8)*11.7/12,
                             1.15+(z-2)*1.7/1.5)
    if max(v[2] for v in vertices) <= 2.201:
        return lambda x, z: (x, z-.9 if z >= 1.9 else z*.55)
    if max(v[0] for v in vertices) <= 2.301:
        return lambda x, z: (x-.9 if x >= 1.9 else x*.55, z)
    if min(v[0] for v in vertices) >= 25.699:
        return lambda x, z: (x-1 if x <= 26.1 else 25+(x-26)*1.5, z)
    if min(v[2] for v in vertices) >= 29.4:
        west = max(v[0] for v in vertices) < 13
        # Two legal door anchors at x12/14 need an opening x11.1..14.9.
        return lambda x, z: (x*11.1/12 if west else 14.9+(x-16)*13.1/12,
                             27.15+(z-29.5)*3.7)
    return lambda x, z: (x, z)


def align_lightning_room(room):
    """Normalize imported room triangles; call once after copying the prototype.

    The material keys, UVs, height, camera and all actor rigs are preserved.
    Metadata covers the physical bodies, including the low long plinths that
    make the original blocked pillar lanes visible.
    """
    for key, (vertices, indices, _) in list(room.batches.items()):
        if key[0]:
            continue
        result, elements, cache = [], [], {}
        for offset in range(0, len(indices), 3):
            triangle = [vertices[i] for i in indices[offset:offset+3]]
            transform = _lightning_triangle(triangle)
            for v in triangle:
                x, z = transform(v[0], v[2])
                moved = (x, v[1], z, *v[3:])
                if moved not in cache:
                    cache[moved] = len(result)
                    result.append(moved)
                elements.append(cache[moved])
        room.batches[key] = (result, elements, {})
    gold, maroon = (212, 168, 54, 255), (115, 0, 66, 255)
    for x in (8, 18):
        room.box(x-.85, 13.15, 1.7, 9.7, .15, gold)
        room.box(x-.76, 13.24, 1.52, 9.52, .09, maroon, y=.15)
    room.props[:] = [dict(kind=kind, x=a, y=b, w=c-a, h=d-b,
                         height=height, depth=d-b, floor=(a, b, c-a, d-b),
                         solid=True, solid_bounds=(a, b, c, d))
                     for kind, (a, b, c, d), height in
                     (*LIGHTNING_BOUNDS, *LIGHTNING_WALL_BOUNDS)]
    return room
