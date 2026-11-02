"""Low-poly mesh authoring with smooth or flat baked lighting, and L643 export.

Authoring space: X right (viewer's right when the character faces the camera),
Y forward (toward the viewer, the character's front), Z up. Native runtime
space is X, Y up, Z south (forward); the exporter maps (x,y,z) -> (x,z,y).
"""
import json
import math
import struct
from pathlib import Path


class Material:
    def __init__(self, name, rgb, texture=None, lit=True):
        self.name = name
        self.rgb = tuple(rgb)
        self.texture = texture  # texture name (no extension) or None
        self.lit = lit

    def __repr__(self):
        return f'Material({self.name})'


class Mesh:
    """Polygons with per-face material, optional per-loop UVs and smoothing group."""
    _group_counter = [0]

    def __init__(self, name='mesh', part=0, pivot=(0, 0, 0)):
        self.name = name
        self.part = part
        self.pivot = tuple(pivot)
        self.verts = []
        self.faces = []  # dict(idx, mat, uv, group)

    @classmethod
    def new_group(cls):
        cls._group_counter[0] += 1
        return cls._group_counter[0]

    def add_vert(self, p):
        self.verts.append(tuple(float(c) for c in p))
        return len(self.verts) - 1

    def add_face(self, idx, mat, uv=None, group=None):
        idx = list(idx)
        if len(idx) < 3:
            return
        self.faces.append(dict(idx=idx, mat=mat, uv=list(uv) if uv else None, group=group))

    # ---- transforms -------------------------------------------------------
    def apply(self, fn):
        self.verts = [tuple(fn(v)) for v in self.verts]
        return self

    def translate(self, dx=0, dy=0, dz=0):
        return self.apply(lambda v: (v[0]+dx, v[1]+dy, v[2]+dz))

    def scale(self, sx=1, sy=None, sz=None, about=(0, 0, 0)):
        sy = sx if sy is None else sy
        sz = sx if sz is None else sz
        ax, ay, az = about
        self.apply(lambda v: (ax+(v[0]-ax)*sx, ay+(v[1]-ay)*sy, az+(v[2]-az)*sz))
        if sx*sy*sz < 0:
            for f in self.faces:
                f['idx'].reverse()
                if f['uv']:
                    f['uv'].reverse()
        return self

    def rotate_z(self, degrees, about=(0, 0)):
        a = math.radians(degrees)
        c, s = math.cos(a), math.sin(a)
        ax, ay = about
        return self.apply(lambda v: (ax+(v[0]-ax)*c-(v[1]-ay)*s, ay+(v[0]-ax)*s+(v[1]-ay)*c, v[2]))

    def rotate_x(self, degrees, about=(0, 0)):
        a = math.radians(degrees)
        c, s = math.cos(a), math.sin(a)
        ay, az = about
        return self.apply(lambda v: (v[0], ay+(v[1]-ay)*c-(v[2]-az)*s, az+(v[1]-ay)*s+(v[2]-az)*c))

    def rotate_y(self, degrees, about=(0, 0)):
        a = math.radians(degrees)
        c, s = math.cos(a), math.sin(a)
        ax, az = about
        return self.apply(lambda v: (ax+(v[0]-ax)*c+(v[2]-az)*s, v[1], az-(v[0]-ax)*s+(v[2]-az)*c))

    def mirror_x(self):
        return self.scale(-1, 1, 1)

    def merge(self, other):
        base = len(self.verts)
        self.verts.extend(other.verts)
        for f in other.faces:
            self.faces.append(dict(idx=[i+base for i in f['idx']], mat=f['mat'], uv=list(f['uv']) if f['uv'] else None, group=f['group']))
        return self

    def copy(self):
        m = Mesh(self.name, self.part, self.pivot)
        m.merge(self)
        return m

    def set_material(self, mat):
        for f in self.faces:
            f['mat'] = mat
        return self

    def remap_material(self, fn):
        """fn(face_center, mat) -> mat"""
        for f in self.faces:
            c = self.face_center(f)
            f['mat'] = fn(c, f['mat'])
        return self

    # ---- geometry helpers -----------------------------------------------
    def face_center(self, f):
        pts = [self.verts[i] for i in f['idx']]
        n = len(pts)
        return tuple(sum(p[c] for p in pts)/n for c in range(3))

    def face_normal(self, f):
        nx = ny = nz = 0.0
        pts = [self.verts[i] for i in f['idx']]
        for i, (x0, y0, z0) in enumerate(pts):
            x1, y1, z1 = pts[(i+1) % len(pts)]
            nx += (y0-y1)*(z0+z1)
            ny += (z0-z1)*(x0+x1)
            nz += (x0-x1)*(y0+y1)
        return normalize((nx, ny, nz))

    def face_area_normal(self, f):
        nx = ny = nz = 0.0
        pts = [self.verts[i] for i in f['idx']]
        for i, (x0, y0, z0) in enumerate(pts):
            x1, y1, z1 = pts[(i+1) % len(pts)]
            nx += (y0-y1)*(z0+z1)
            ny += (z0-z1)*(x0+x1)
            nz += (x0-x1)*(y0+y1)
        return (nx/2, ny/2, nz/2)

    def orient(self, outward_fn):
        """Flip faces whose normal points against outward_fn(center)."""
        for f in self.faces:
            n = self.face_normal(f)
            d = outward_fn(self.face_center(f))
            if n[0]*d[0]+n[1]*d[1]+n[2]*d[2] < 0:
                f['idx'].reverse()
                if f['uv']:
                    f['uv'].reverse()
        return self

    def vertex_normals(self):
        """(vertex index, group) -> smooth normal; flat faces use face normals."""
        acc = {}
        for f in self.faces:
            if f['group'] is None:
                continue
            an = self.face_area_normal(f)
            for i in f['idx']:
                key = (i, f['group'])
                a = acc.get(key, (0.0, 0.0, 0.0))
                acc[key] = (a[0]+an[0], a[1]+an[1], a[2]+an[2])
        return {k: normalize(v) for k, v in acc.items()}

    def triangle_count(self):
        return sum(len(f['idx'])-2 for f in self.faces)

    def bounds(self):
        xs, ys, zs = zip(*self.verts)
        return (min(xs), min(ys), min(zs)), (max(xs), max(ys), max(zs))


def normalize(v):
    l = math.sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2])
    if l < 1e-12:
        return (0.0, 0.0, 1.0)
    return (v[0]/l, v[1]/l, v[2]/l)


# ---- primitives ------------------------------------------------------------
def lathe(rings, segs, mat, phase=.5, smooth=True, cap_bottom=True, cap_top=True,
          arc=(0.0, 360.0), name='lathe', uv_fn=None, cap_smooth=False):
    """Revolve rings about Z. ring = (z, rx, ry, cx, cy); rx==0 makes a pole.

    Angle 0 is +Y (front), increasing toward +X. arc limits the sweep in degrees
    around the front: (-70, 70) keeps only the front wedge; (0,360) is closed.
    uv_fn(angle_deg, z, ring_index) -> (u,v) or None gives per-loop UVs.
    """
    m = Mesh(name)
    group = Mesh.new_group() if smooth else None
    a0, a1 = arc
    closed = abs((a1-a0)-360) < 1e-6
    count = segs if closed else segs+1
    step = (a1-a0)/segs
    rows = []
    angles = []
    for ri, (z, rx, ry, cx, cy) in enumerate(rings):
        if rx < 1e-9 and ry < 1e-9:
            rows.append(m.add_vert((cx, cy, z)))
            angles.append(None)
            continue
        row = []
        ang = []
        for i in range(count):
            a = a0 + (i+(phase if closed else 0))*step
            r = math.radians(a)
            row.append(m.add_vert((cx+rx*math.sin(r), cy+ry*math.cos(r), z)))
            ang.append(a)
        rows.append(row)
        angles.append(ang)

    def uv(a, z, ri):
        return uv_fn(a, z, ri) if uv_fn else None

    for j in range(len(rings)-1):
        lo, hi = rows[j], rows[j+1]
        za, zb = rings[j][0], rings[j+1][0]
        if isinstance(lo, int) and isinstance(hi, int):
            continue
        if isinstance(lo, int):
            for i in range(segs):
                i2 = (i+1) % count
                a_i, a_i2 = angles[j+1][i], angles[j+1][i2]
                uvs = [uv((a_i+a_i2)/2, za, j), uv(a_i2, zb, j+1), uv(a_i, zb, j+1)]
                m.add_face([lo, hi[i2], hi[i]], mat, uvs if all(uvs) else None, group)
        elif isinstance(hi, int):
            for i in range(segs):
                i2 = (i+1) % count
                a_i, a_i2 = angles[j][i], angles[j][i2]
                uvs = [uv(a_i, za, j), uv(a_i2, za, j), uv((a_i+a_i2)/2, zb, j+1)]
                m.add_face([lo[i], lo[i2], hi], mat, uvs if all(uvs) else None, group)
        else:
            for i in range(segs):
                i2 = (i+1) % count
                a_i, a_i2 = angles[j][i], angles[j][i2]
                uvs = [uv(a_i, za, j), uv(a_i2, za, j), uv(a_i2, zb, j+1), uv(a_i, zb, j+1)]
                m.add_face([lo[i], lo[i2], hi[i2], hi[i]], mat, uvs if all(uvs) else None, group)
    cap_group = group if cap_smooth else None
    if cap_bottom and not isinstance(rows[0], int) and closed:
        m.add_face(list(reversed(rows[0])), mat, None, cap_group)
    if cap_top and not isinstance(rows[-1], int) and closed:
        m.add_face(list(rows[-1]), mat, None, cap_group)
    centers = {}
    for j, (z, rx, ry, cx, cy) in enumerate(rings):
        centers[j] = (cx, cy, z)

    def outward(c):
        # nearest ring center by z
        zs = [r[0] for r in rings]
        best = min(range(len(zs)), key=lambda j: abs(zs[j]-c[2]))
        cx, cy, cz = centers[best]
        d = (c[0]-cx, c[1]-cy, 0.0)
        if abs(d[0]) < 1e-9 and abs(d[1]) < 1e-9:
            return (0, 0, 1 if c[2] > (min(zs)+max(zs))/2 else -1)
        return d
    # caps are oriented by z
    for f in m.faces:
        n = m.face_normal(f)
        c = m.face_center(f)
        if abs(n[2]) > .999:  # a cap
            want = 1 if c[2] > (min(r[0] for r in rings)+max(r[0] for r in rings))/2 else -1
            if n[2]*want < 0:
                f['idx'].reverse()
                if f['uv']:
                    f['uv'].reverse()
        else:
            d = outward(c)
            if n[0]*d[0]+n[1]*d[1] < 0:
                f['idx'].reverse()
                if f['uv']:
                    f['uv'].reverse()
    return m


def ellipsoid(center, radii, segs=8, rings=4, mat=None, smooth=True, name='ellipsoid', squash=None, uv_fn=None):
    """Sphere-like lathe. squash(t)-> scale of radius at latitude t in [-1,1]."""
    cx, cy, cz = center
    rx, ry, rz = radii
    rr = []
    for k in range(rings+1):
        t = -1 + 2*k/rings
        s = math.sqrt(max(0.0, 1-t*t))
        if squash:
            s *= squash(t)
        rr.append((cz+rz*t, rx*s, ry*s, cx, cy))
    return lathe(rr, segs, mat, smooth=smooth, name=name, uv_fn=uv_fn)


def superellipse_ring(z, rx, ry, cx=0, cy=0, segs=8, power=2.5, phase=.5):
    pts = []
    for i in range(segs):
        a = 2*math.pi*(i+phase)/segs
        sa, ca = math.sin(a), math.cos(a)
        x = math.copysign(abs(sa)**(2/power), sa)*rx
        y = math.copysign(abs(ca)**(2/power), ca)*ry
        pts.append((cx+x, cy+y, z))
    return pts


def loft(rings, mat, smooth=True, cap_bottom=True, cap_top=True, name='loft', closed=True, uv_fn=None):
    """Join rings (lists of 3D points, equal length) with quads."""
    m = Mesh(name)
    group = Mesh.new_group() if smooth else None
    rows = [[m.add_vert(p) for p in ring] for ring in rings]
    n = len(rings[0])
    for j in range(len(rings)-1):
        lo, hi = rows[j], rows[j+1]
        for i in range(n if closed else n-1):
            i2 = (i+1) % n
            uvs = None
            if uv_fn:
                uvs = [uv_fn(i, j), uv_fn(i2 if i2 else n, j), uv_fn(i2 if i2 else n, j+1), uv_fn(i, j+1)]
            m.add_face([lo[i], lo[i2], hi[i2], hi[i]], mat, uvs, group)
    if cap_bottom and closed:
        m.add_face(list(reversed(rows[0])), mat, None, None)
    if cap_top and closed:
        m.add_face(list(rows[-1]), mat, None, None)
    # orient outward from each ring's centroid
    cents = [tuple(sum(p[c] for p in ring)/len(ring) for c in range(3)) for ring in rings]
    zs = [c[2] for c in cents]

    def outward(c):
        j = min(range(len(cents)), key=lambda k: abs(zs[k]-c[2]))
        d = (c[0]-cents[j][0], c[1]-cents[j][1], 0.0)
        return d
    for f in m.faces:
        nrm = m.face_normal(f)
        c = m.face_center(f)
        if abs(nrm[2]) > .999 and len(f['idx']) == n:
            want = 1 if c[2] > (min(zs)+max(zs))/2 else -1
            if nrm[2]*want < 0:
                f['idx'].reverse()
                if f['uv']:
                    f['uv'].reverse()
        else:
            d = outward(c)
            if abs(d[0]) < 1e-9 and abs(d[1]) < 1e-9:
                continue
            if nrm[0]*d[0]+nrm[1]*d[1] < 0:
                f['idx'].reverse()
                if f['uv']:
                    f['uv'].reverse()
    return m


def box(center, size, mat, bevel=0.0, smooth=False, name='box', taper=1.0):
    """Axis-aligned box; bevel>0 chamfers all edges with one loop (octagonal rings)."""
    cx, cy, cz = center
    sx, sy, sz = (s/2 for s in size)
    if bevel <= 0:
        rings = [[(cx-sx, cy-sy, cz-sz), (cx+sx, cy-sy, cz-sz), (cx+sx, cy+sy, cz-sz), (cx-sx, cy+sy, cz-sz)],
                 [(cx-sx*taper, cy-sy*taper, cz+sz), (cx+sx*taper, cy-sy*taper, cz+sz), (cx+sx*taper, cy+sy*taper, cz+sz), (cx-sx*taper, cy+sy*taper, cz+sz)]]
        return loft(rings, mat, smooth=False, name=name)
    b = min(bevel, sx*.9, sy*.9, sz*.9)

    def ring(z, inset):
        hx, hy = sx-inset, sy-inset
        return [(cx-hx+b, cy-hy, z), (cx+hx-b, cy-hy, z), (cx+hx, cy-hy+b, z), (cx+hx, cy+hy-b, z),
                (cx+hx-b, cy+hy, z), (cx-hx+b, cy+hy, z), (cx-hx, cy+hy-b, z), (cx-hx, cy-hy+b, z)]
    rings = [ring(cz-sz, b), ring(cz-sz+b, 0), ring(cz+sz-b, 0), ring(cz+sz, b)]
    return loft(rings, mat, smooth=smooth, name=name)


def wedge(base, tip, mat, smooth=False, name='wedge'):
    """Pyramid from a quad base (4 points, any orientation) to a tip."""
    m = Mesh(name)
    group = Mesh.new_group() if smooth else None
    ids = [m.add_vert(p) for p in base]
    t = m.add_vert(tip)
    m.add_face(ids, mat, None, None)
    for i in range(4):
        m.add_face([ids[i], ids[(i+1) % 4], t], mat, None, group)
    c = tuple(sum(p[k] for p in base+[tip])/5 for k in range(3))
    m.orient(lambda fc: (fc[0]-c[0], fc[1]-c[1], fc[2]-c[2]))
    return m


def spike(base_center, base_rx, base_ry, tip, mat, sides=4, smooth=True, name='spike', base_dir=(0, 0, 1)):
    """Cone-like spike: polygonal base perpendicular to base_dir, apex at tip."""
    bx, by, bz = base_center
    d = normalize(base_dir)
    # basis perpendicular to d
    up = (0, 0, 1) if abs(d[2]) < .9 else (0, 1, 0)
    u = normalize(cross(d, up))
    v = normalize(cross(u, d))
    m = Mesh(name)
    group = Mesh.new_group() if smooth else None
    ids = []
    for i in range(sides):
        a = 2*math.pi*(i+.5)/sides
        p = (bx+u[0]*base_rx*math.cos(a)+v[0]*base_ry*math.sin(a),
             by+u[1]*base_rx*math.cos(a)+v[1]*base_ry*math.sin(a),
             bz+u[2]*base_rx*math.cos(a)+v[2]*base_ry*math.sin(a))
        ids.append(m.add_vert(p))
    t = m.add_vert(tip)
    m.add_face(ids, mat, None, None)
    for i in range(sides):
        m.add_face([ids[i], ids[(i+1) % sides], t], mat, None, group)
    c = tuple((base_center[k]*2+tip[k])/3 for k in range(3))
    m.orient(lambda fc: (fc[0]-c[0], fc[1]-c[1], fc[2]-c[2]))
    return m


def cross(a, b):
    return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])


def extrude(points, z0, z1, mat, smooth=False, name='extrude', taper=1.0):
    """Prism from a 2D polygon (x,y) between z0 and z1; sides flat."""
    cx = sum(p[0] for p in points)/len(points)
    cy = sum(p[1] for p in points)/len(points)
    rings = [[(x, y, z0) for x, y in points], [(cx+(x-cx)*taper, cy+(y-cy)*taper, z1) for x, y in points]]
    return loft(rings, mat, smooth=smooth, name=name)


def disc(center, rx, ry, mat, segs=12, z=None, name='disc'):
    m = Mesh(name)
    cx, cy, cz = center
    ids = [m.add_vert((cx+rx*math.cos(2*math.pi*i/segs), cy+ry*math.sin(2*math.pi*i/segs), cz)) for i in range(segs)]
    m.add_face(ids, mat, None, None)
    m.orient(lambda c: (0, 0, 1))
    return m


# ---- export ------------------------------------------------------------------
class Lighting:
    def __init__(self, ambient=.62, diffuse=.38, direction=(-.35, .55, .75), gamma=1.0):
        self.ambient = ambient
        self.diffuse = diffuse
        self.direction = normalize(direction)
        self.gamma = gamma

    def shade(self, n):
        d = max(0.0, n[0]*self.direction[0]+n[1]*self.direction[1]+n[2]*self.direction[2])
        return min(1.0, self.ambient+self.diffuse*d)


def bake(meshes, lighting, model=1):
    """Return (batches, textures). batch key: (model, part, texture index, pivot)."""
    textures = []
    batches = {}
    for mesh in meshes:
        normals = mesh.vertex_normals()
        for f in mesh.faces:
            mat = f['mat']
            tex = -1
            if mat.texture:
                if mat.texture not in textures:
                    textures.append(mat.texture)
                tex = textures.index(mat.texture)
            key = (model, mesh.part, tex, mesh.pivot)
            vs, indices, cache = batches.setdefault(key, ([], [], {}))
            fn = mesh.face_normal(f)
            loops = []
            for li, vi in enumerate(f['idx']):
                n = normals.get((vi, f['group']), fn) if f['group'] is not None else fn
                shade = lighting.shade(n) if mat.lit else 1.0
                if mat.texture:
                    rgb = (shade, shade, shade)
                else:
                    rgb = tuple(c*shade for c in mat.rgb)
                color = tuple(max(0, min(255, round(c*255))) for c in rgb)+(255,)
                uv = f['uv'][li] if f['uv'] else (0.0, 0.0)
                p = mesh.verts[vi]
                px, py, pz = p[0]-mesh.pivot[0], p[2]-mesh.pivot[2], p[1]-mesh.pivot[1]
                vertex = (round(px, 5), round(py, 5), round(pz, 5), round(uv[0], 5), round(uv[1], 5))+color
                if vertex not in cache:
                    cache[vertex] = len(vs)
                    vs.append(vertex)
                loops.append(cache[vertex])
            for k in range(1, len(loops)-1):
                tri = (loops[0], loops[k], loops[k+1])
                if len(set(tri)) < 3:
                    continue
                indices.extend(tri)
    return batches, textures


def write_l643(path, batches, textures):
    path = Path(path)
    with path.open('wb') as f:
        f.write(struct.pack('>4sHH', b'L643', len(batches), len(textures)))
        for name in textures:
            encoded = (name+'.sprite').encode()
            assert len(encoded) < 32, name
            f.write(encoded.ljust(32, b'\0'))
        for (model, part, tex, pivot), (vs, indices, _) in sorted(batches.items()):
            assert 0 < len(vs) < 65536 and indices
            # pivot stored as (x, up, south)
            f.write(struct.pack('>HHhHII3f', model, part, tex, 0, len(vs), len(indices), pivot[0], pivot[2], pivot[1]))
            for v in vs:
                f.write(struct.pack('>5f4B', *v))
            f.write(struct.pack('>'+str(len(indices))+'H', *indices))
            f.write(b'\0'*((-f.tell()) % 4))
    return dict(vertices=sum(len(b[0]) for b in batches.values()),
                triangles=sum(len(b[1])//3 for b in batches.values()), batches=len(batches))


def dump_json(path, meshes, textures_meta):
    """Intermediate description for Blender import (positions, faces, uvs, materials)."""
    out = []
    for mesh in meshes:
        mats = {}
        faces = []
        for f in mesh.faces:
            mat = f['mat']
            if mat.name not in mats:
                mats[mat.name] = dict(name=mat.name, rgb=mat.rgb, texture=mat.texture, lit=mat.lit)
            faces.append(dict(idx=f['idx'], mat=mat.name, uv=f['uv'], smooth=f['group'] is not None))
        out.append(dict(name=mesh.name, part=mesh.part, pivot=mesh.pivot, verts=mesh.verts, faces=faces, materials=list(mats.values())))
    Path(path).write_text(json.dumps(dict(meshes=out, textures=textures_meta)))
