"""Four N64 character-style directions applied to Mark, Dr. Mason and Ronald.

Every figure is a few continuous shells (head, hair/cap, torso, two arms, two
legs) with costume detail painted into 32x32 textures. Five rigid parts and
pivots keep the existing walk-animation contract. Heights stay near 2.2 units.
"""
import math
from dataclasses import dataclass, field
from geom import (Material, Mesh, Lighting, lathe, ellipsoid, loft, box, wedge, spike, extrude,
                  disc, superellipse_ring, normalize, bake, write_l643, dump_json)
from colors import q16f
try:
    from paint import Canvas, eye, brow, glasses, mouth, INK, EYE_WHITE, GRAY, WARM
except ModuleNotFoundError:  # Blender's Python has no Pillow; geometry still builds
    Canvas = None

SKIN8 = (231, 173, 99)
SKIN = q16f(tuple(c/255 for c in SKIN8))


@dataclass
class Style:
    id: str
    label: str
    title: str
    smooth: bool
    head_segs: int
    limb_segs: int
    light: Lighting
    head_center: tuple          # (y, z)
    head_r: tuple               # (rx, ry, rz)
    head_shape: str             # sphere | egg | facet
    eye_t: float                # latitude (-1..1) of the eye line
    face_faces: int             # textured front faces across the head
    nose: str                   # none | bump | cone | wedge
    ear: tuple                  # radii or None
    torso: list                 # (z, rx, ry) bottom..top shirt torso
    torso_segs: int
    neck: tuple                 # (z0, z1, r) or None
    arm_pivot: tuple            # (x, z)
    arm: dict                   # len, r_top, r_bot, hand (rx,ry,rz), tilt
    leg_pivot: tuple            # (x, z)
    leg: dict                   # r_top, r_bot, shoe (len, w, h)
    angular: bool = False
    hair_scale: float = 1.0
    text_eye: dict = field(default_factory=dict)


LIGHT_DIR = (-.35, .55, .75)
STYLES = {
    'a_round': Style('a_round', 'A Round', 'A · Round chibi (Goemon / Snap)', True, 12, 8,
                     Lighting(.62, .38, LIGHT_DIR),
                     (0.0, 1.70), (.47, .45, .47), 'sphere', -.04, 3, 'none', (.05, .06, .07),
                     [(.62, .33, .23), (.84, .34, .235), (1.06, .36, .24), (1.24, .36, .23), (1.33, .27, .19), (1.36, .12, .10)], 12,
                     None, (.41, 1.24), dict(len=.56, r_top=.115, r_bot=.095, hand=(.125, .12, .13), tilt=7),
                     (.165, .68), dict(r_top=.155, r_bot=.135, shoe=(.42, .30, .17)), hair_scale=1.05,
                     text_eye=dict(w=10, h=12, pw=6, ph=9, shape='oval', brow=2)),
    'b_pokemon': Style('b_pokemon', 'B Pokemon', 'B · Nintendo Pokémon 3D (Snap / Stadium era)', True, 12, 8,
                       Lighting(.56, .44, LIGHT_DIR),
                       (0.0, 1.80), (.40, .37, .41), 'egg', .02, 3, 'bump', (.055, .065, .08),
                       [(.70, .30, .205), (.92, .285, .195), (1.14, .32, .21), (1.32, .365, .22), (1.42, .29, .19), (1.46, .13, .11)], 12,
                       (1.40, 1.50, .11), (.385, 1.33), dict(len=.62, r_top=.105, r_bot=.085, hand=(.11, .105, .12), tilt=6),
                       (.16, .74), dict(r_top=.145, r_bot=.12, shoe=(.40, .27, .15)),
                       text_eye=dict(w=9, h=10, pw=5, ph=8, shape='oval', brow=2)),
    'c_zelda': Style('c_zelda', 'C Zelda', 'C · Ocarina townsfolk', True, 10, 8,
                     Lighting(.48, .52, LIGHT_DIR),
                     (0.0, 1.83), (.36, .34, .39), 'egg', .06, 3, 'cone', (.05, .06, .085),
                     [(.78, .27, .185), (1.00, .255, .175), (1.22, .29, .19), (1.40, .335, .205), (1.48, .24, .16), (1.52, .10, .09)], 10,
                     (1.44, 1.56, .095), (.36, 1.40), dict(len=.68, r_top=.09, r_bot=.075, hand=(.095, .09, .11), tilt=5),
                     (.145, .82), dict(r_top=.125, r_bot=.10, shoe=(.35, .23, .13)),
                     text_eye=dict(w=7, h=7, pw=4, ph=6, shape='oval', brow=2)),
    'd_angular': Style('d_angular', 'D Angular', 'D · Angular hard-surface (Mega Man 64 / Mystical Ninja)', False, 8, 6,
                       Lighting(.60, .40, LIGHT_DIR),
                       (0.0, 1.76), (.44, .42, .44), 'facet', -.02, 3, 'wedge', (.06, .07, .08),
                       [(.66, .31, .215), (.90, .32, .22), (1.14, .35, .235), (1.30, .35, .23), (1.38, .24, .17), (1.41, .12, .10)], 8,
                       None, (.45, 1.28), dict(len=.58, r_top=.115, r_bot=.10, hand=(.12, .11, .12), tilt=8),
                       (.17, .70), dict(r_top=.16, r_bot=.14, shoe=(.44, .30, .17)), angular=True,
                       text_eye=dict(w=8, h=8, pw=6, ph=7, shape='sharp', brow=3)),
}


@dataclass
class Design:
    slug: str
    name: str
    hair_rgb: tuple
    top_rgb: tuple        # main garment colour (vest / coat / jacket)
    shirt_rgb: tuple      # under-shirt / sleeve colour
    trousers_rgb: tuple
    shoe_rgb: tuple
    sole_rgb: tuple
    sleeve: str           # short | long
    sleeve_rgb: tuple
    coat: bool = False
    backpack: bool = False
    cap_rgb: tuple = None


DESIGNS = {
    'mark': Design('mark', 'Mark', (.08, .07, .08), (.93, .17, .27), (.16, .15, .17), (.10, .25, .66),
                   (.95, .95, .92), (.13, .13, .15), 'short', (.16, .15, .17), backpack=True, cap_rgb=(.93, .17, .27)),
    'dr_mason': Design('dr_mason', 'Dr. Mason', (.86, .86, .83), (.91, .90, .84), (.46, .31, .15), (.26, .22, .18),
                       (.14, .12, .12), (.08, .07, .07), 'long', (.91, .90, .84), coat=True),
    'ronald': Design('ronald', 'Ronald', (.075, .06, .06), (.09, .22, .64), (.09, .085, .09), (.10, .22, .62),
                     (.95, .95, .93), (.10, .10, .12), 'short', (.09, .085, .09)),
}


def mat(name, rgb, texture=None, lit=True):
    return Material(name, q16f(rgb), texture, lit)


def wrap(a):
    return ((a+180) % 360)-180


# ---------------------------------------------------------------------------
class Figure:
    def __init__(self, style, design):
        self.s = style
        self.d = design
        self.meshes = []
        self.textures = {}  # name -> Canvas
        self.skin = mat('skin', SKIN)
        self.prefix = f'{style.id}_{design.slug}'

    def add(self, mesh, part=0, pivot=(0, 0, 0)):
        mesh.part = part
        mesh.pivot = tuple(pivot)
        self.meshes.append(mesh)
        return mesh

    # ---- head -------------------------------------------------------------
    def head_z(self, t):
        return self.s.head_center[1]+self.s.head_r[2]*t

    def head_radius_scale(self, t):
        s = math.sqrt(max(0.0, 1-t*t))
        if self.s.head_shape == 'egg':
            s *= 1-.14*max(0.0, -t)
        return s

    def head_ring(self, t, ratio=1.0, lift=0.0):
        s = self.head_radius_scale(t)
        rx, ry, rz = self.s.head_r
        cy, cz = self.s.head_center
        return (cz+rz*t*ratio+lift, rx*s*ratio, ry*s*ratio, 0.0, cy)

    def head(self):
        s = self.s
        eye_t = s.eye_t
        ft, fb = eye_t+.36, eye_t-.60
        self.face_top_z, self.face_bottom_z = self.head_z(ft), self.head_z(fb)
        step = 360/s.head_segs
        self.face_half = s.face_faces*step/2
        face_tex = mat('face', (1, 1, 1), f'{self.prefix}_face')
        if s.head_shape == 'facet':
            ts = [-.92, fb, (fb+eye_t)/2, eye_t, (eye_t+ft)/2, ft, (ft+1)/2, .95]
            rings = [self.head_ring(t) for t in ts]
            rings[0] = (rings[0][0], rings[0][1]*.55, rings[0][2]*.55, 0, rings[0][4])
            rings[-1] = (rings[-1][0], rings[-1][1]*.75, rings[-1][2]*.75, 0, rings[-1][4])
        else:
            ts = [-1, (fb-1)/2, fb, (fb+eye_t)/2, eye_t, (eye_t+ft)/2, ft, (ft+1)/2+.05, 1]
            rings = [self.head_ring(t) for t in ts]

        def uv_fn(a, z, ri):
            a = wrap(a)
            u = (a+self.face_half)/(2*self.face_half)
            v = (self.face_top_z-z)/(self.face_top_z-self.face_bottom_z)
            return (min(1, max(0, u)), min(1, max(0, v)))
        head = lathe(rings, s.head_segs, self.skin, smooth=s.smooth, uv_fn=uv_fn, name='head',
                     cap_bottom=True, cap_top=True)
        half = self.face_half
        ftz, fbz = self.face_top_z, self.face_bottom_z

        def pick(c, m):
            a = wrap(math.degrees(math.atan2(c[0], c[1]-s.head_center[0])))
            if abs(a) < half-1e-3 and fbz-1e-4 < c[2] < ftz+1e-4:
                return face_tex
            return m
        head.remap_material(pick)
        self.add(head)
        # nose
        cy, cz = s.head_center
        ry = s.head_r[1]
        nz = self.head_z(eye_t-.22)
        if s.nose == 'bump':
            self.add(ellipsoid((0, cy+ry*.93, nz), (.055, .07, .05), 6, 2, self.skin, True, 'nose'))
        elif s.nose == 'cone':
            self.add(spike((0, cy+ry*.90, nz+.02), .05, .06, (0, cy+ry+.075, nz-.03), self.skin, 5, True, 'nose', (0, 1, -.2)))
        elif s.nose == 'wedge':
            self.add(wedge([(-.05, cy+ry*.92, nz+.07), (.05, cy+ry*.92, nz+.07), (.05, cy+ry*.92, nz-.05), (-.05, cy+ry*.92, nz-.05)],
                           (0, cy+ry+.08, nz-.02), self.skin, False, 'nose'))
        if s.ear:
            ex = s.head_r[0]*.98
            for side in (-1, 1):
                e = ellipsoid((side*ex, cy-.01, self.head_z(eye_t-.08)), s.ear, 6, 2, self.skin, s.smooth, 'ear')
                self.add(e)

    def hair_shell(self, material, ratio, t_low, hairline_t, open_half, name='hair', lift=0.0,
                   t_top=1.0, pole=True, back_low=None, segs=None, smooth=None, ring_ts=None):
        """Shell around the head, open over the face below hairline_t."""
        s = self.s
        segs = segs or s.head_segs
        smooth = s.smooth if smooth is None else smooth
        if ring_ts is None:
            n = 5
            ring_ts = [t_low+(t_top-t_low)*k/n for k in range(n)]+[t_top]
        rings = []
        for t in ring_ts:
            if t >= .999 and pole:
                rings.append((self.head_z(1)*1+ (ratio-1)*s.head_r[2]+lift, 0, 0, 0, s.head_center[0]))
            else:
                rings.append(self.head_ring(t, ratio, lift))
        shell = lathe(rings, segs, material, smooth=smooth, cap_bottom=False, cap_top=not pole, name=name)
        cy, cz = s.head_center
        keep = []
        for f in shell.faces:
            c = shell.face_center(f)
            a = wrap(math.degrees(math.atan2(c[0], c[1]-cy)))
            t = (c[2]-lift-cz)/(s.head_r[2]*ratio)
            inside_open = abs(a) < open_half and t < hairline_t
            if back_low is not None and abs(a) > 180-back_low[0] and t < back_low[1]:
                inside_open = False
            if not inside_open:
                keep.append(f)
        shell.faces = keep
        # tuck the opening rim onto the head surface
        used = set(i for f in keep for i in f['idx'])
        for i in used:
            x, y, z = shell.verts[i]
            a = wrap(math.degrees(math.atan2(x, y-cy)))
            t = (z-lift-cz)/(s.head_r[2]*ratio)
            if abs(a) <= open_half+1e-3 and t <= hairline_t+1e-3:
                sc = self.head_radius_scale(t)
                rx, ry = s.head_r[0]*sc*1.01, s.head_r[1]*sc*1.01
                r = math.radians(a)
                shell.verts[i] = (rx*math.sin(r), cy+ry*math.cos(r), z)
        return self.add(shell)

    # ---- torso ------------------------------------------------------------
    def torso(self, rings, texture_name):
        s = self.s
        top_rgb = self.d.top_rgb
        solid = mat('top', top_rgb)
        front = mat('torso_front', (1, 1, 1), texture_name)
        z0, z1 = rings[0][0], rings[-1][0]
        step = 360/s.torso_segs
        faces = 5 if s.torso_segs >= 10 else 3
        half = faces*step/2
        self.torso_half = half
        full = [(z, rx, ry, 0.0, 0.0) for z, rx, ry in rings]

        def uv_fn(a, z, ri):
            a = wrap(a)
            return (min(1, max(0, (a+half)/(2*half))), min(1, max(0, (z1-z)/(z1-z0))))
        body = lathe(full, s.torso_segs, solid, smooth=s.smooth, uv_fn=uv_fn, name='torso', cap_bottom=True, cap_top=True)

        def pick(c, m):
            a = wrap(math.degrees(math.atan2(c[0], c[1])))
            if abs(a) < half-1e-3 and z0 < c[2] < z1:
                return front
            return m
        body.remap_material(pick)
        if s.angular:
            # chamfered box torso instead of a round loft
            body = self.angular_torso(rings, solid, front, z0, z1)
        self.add(body)
        if s.neck:
            n0, n1, r = s.neck
            self.add(lathe([(n0, r, r*.9, 0, 0), (n1, r, r*.9, 0, 0)], 8, self.skin, smooth=s.smooth, name='neck'))

    def angular_torso(self, rings, solid, front, z0, z1):
        pts = []
        for z, rx, ry in rings:
            ring = superellipse_ring(z, rx, ry, 0, 0, 8, 3.2)
            pts.append(ring)
        body = loft(pts, solid, smooth=False, name='torso')
        # texture the front three facets (y > 0 side)
        for f in body.faces:
            c = body.face_center(f)
            a = wrap(math.degrees(math.atan2(c[0], c[1])))
            if abs(a) < 70 and z0 < c[2] < z1 and len(f['idx']) == 4:
                f['mat'] = front
                uvs = []
                for i in f['idx']:
                    x, y, z = body.verts[i]
                    aa = wrap(math.degrees(math.atan2(x, y)))
                    uvs.append((min(1, max(0, (aa+67.5)/135)), min(1, max(0, (z1-z)/(z1-z0)))))
                f['uv'] = uvs
        self.torso_half = 67.5
        return body

    # ---- limbs ------------------------------------------------------------
    def arms(self):
        s, d = self.s, self.d
        px, pz = s.arm_pivot
        a = s.arm
        sleeve = mat('sleeve', d.sleeve_rgb)
        for side in (-1, 1):
            part = 3 if side < 0 else 4
            x = side*px
            top = pz+.08
            bottom = pz-a['len']
            hand_z = bottom-a['hand'][2]*.55
            if d.sleeve == 'long':
                cut = bottom+.06
            else:
                cut = pz-a['len']*.42
            segs = s.limb_segs
            if s.angular:
                arm = loft([superellipse_ring(bottom, a['r_bot'], a['r_bot'], x, 0, 6, 3.0),
                            superellipse_ring(cut, a['r_bot']*1.05, a['r_bot']*1.05, x, 0, 6, 3.0),
                            superellipse_ring(cut+.001, a['r_top']*1.1, a['r_top']*1.1, x, 0, 6, 3.0),
                            superellipse_ring(top, a['r_top'], a['r_top'], x, 0, 6, 3.0)], sleeve, smooth=False, name='arm')
            else:
                arm = lathe([(bottom, a['r_bot'], a['r_bot'], x, 0), (cut, a['r_bot']*1.02, a['r_bot']*1.02, x, 0),
                             (cut+.002, a['r_top']*1.12, a['r_top']*1.12, x, 0), (pz-.04, a['r_top']*1.10, a['r_top']*1.10, x, 0),
                             (pz+.015, a['r_top']*.82, a['r_top']*.82, x-side*.05, 0), (pz+.05, 0, 0, x-side*.10, 0)], segs, sleeve, smooth=s.smooth, name='arm')
            forearm = self.skin if d.sleeve == 'short' else sleeve
            arm.remap_material(lambda c, m: forearm if c[2] < cut else m)
            hand = ellipsoid((x+side*.01, .02, hand_z), a['hand'], 6 if not s.angular else 4, 3 if not s.angular else 2, self.skin, s.smooth, 'hand')
            if s.angular:
                hand = box((x+side*.01, .02, hand_z), (a['hand'][0]*2, a['hand'][1]*2, a['hand'][2]*2), self.skin, bevel=.035, name='hand')
            arm.merge(hand)
            arm.rotate_y(-side*a['tilt'], about=(x, pz))
            self.add(arm, part, (x, 0, pz))

    def legs(self):
        s, d = self.s, self.d
        px, pz = s.leg_pivot
        l = s.leg
        trousers = mat('trousers', d.trousers_rgb)
        shoe_m = mat('shoe', d.shoe_rgb)
        sole_m = mat('sole', d.sole_rgb)
        L, W, H = l['shoe']
        for side in (-1, 1):
            part = 1 if side < 0 else 2
            x = side*px
            segs = s.limb_segs
            if s.angular:
                leg = loft([superellipse_ring(H*.6, l['r_bot'], l['r_bot']*1.1, x, 0, 6, 3.0),
                            superellipse_ring(pz-.08, l['r_top'], l['r_top']*1.1, x, 0, 6, 3.0),
                            superellipse_ring(pz+.10, l['r_top']*.86, l['r_top']*.95, x-side*.01, 0, 6, 3.0)], trousers, smooth=False, name='leg')
                shoe = box((x, .07, H/2), (W, L, H), shoe_m, bevel=.04, name='shoe')
                shoe.remap_material(lambda c, m: sole_m if c[2] < .04 else m)
            else:
                leg = lathe([(H*.6, l['r_bot'], l['r_bot']*1.1, x, 0), (pz-.1, l['r_top']*.96, l['r_top']*1.06, x, 0), (pz+.10, l['r_top']*.86, l['r_top']*.95, x-side*.01, 0)],
                            segs, trousers, smooth=s.smooth, name='leg', cap_top=True)
                rings = [superellipse_ring(0, W*.46, L*.46, x, .05, 8, 2.6),
                         superellipse_ring(.035, W*.5, L*.5, x, .06, 8, 2.6),
                         superellipse_ring(H*.62, W*.44, L*.42, x, .02, 8, 2.4),
                         superellipse_ring(H, W*.30, L*.26, x, -.05, 8, 2.2)]
                shoe = loft(rings, shoe_m, smooth=s.smooth, name='shoe')
                shoe.remap_material(lambda c, m: sole_m if c[2] < .03 else m)
            leg.merge(shoe)
            self.add(leg, part, (x, 0, pz))

    def shadow(self):
        self.add(disc((0, 0, .018), .45, .32, mat('shadow', (.055, .025, .11), lit=False), 12, name='shadow'))

    # ---- output -----------------------------------------------------------
    def finish(self):
        batches, textures = bake(self.meshes, self.s.light, model=1)
        return batches, textures


# ---------------------------------------------------------------------------
# Face and costume painting
def paint_face(style, design):
    s = style
    c = Canvas((32, 32), SKIN8)
    e = s.text_eye
    eye_t = s.eye_t
    ft, fb = eye_t+.36, eye_t-.60

    def row(t):
        return (ft-t)/(ft-fb)*32
    ey = row(eye_t)
    my = row(eye_t-.40)
    by = row(eye_t+.17)
    # horizontal: texture spans +-face_half degrees; eyes at +-26 deg on a 90 deg face
    span = s.face_faces*(360/s.head_segs)
    def col(deg):
        return (deg+span/2)/span*32
    ex = 24 if span <= 95 else 22
    if design.slug == 'mark':
        iris = (31, 44, 70)
        for side in (-1, 1):
            cx = 16+side*(col(ex)-16)
            shape = 'rect' if s.id == 'd_angular' else e['shape']
            eye(c, cx, ey, e['w'], e['h'], e['pw'] if shape != 'rect' else 4, e['ph'] if shape != 'rect' else 6, iris, True, shape=shape)
            if s.id == 'd_angular':
                brow(c, cx-e['w']/2, cx+e['w']/2-1, by-2, by-2, INK, e['brow'])
            else:
                brow(c, cx-e['w']/2, cx+e['w']/2-1, by-1 if side < 0 else by, by if side < 0 else by-1, INK, e['brow'])
        mouth(c, 16, my, 7 if span <= 95 else 6, 'smile', WARM)
    elif design.slug == 'ronald':
        iris = (40, 38, 44)
        # narrow, sharp eyes: little white, heavy pupils, V brows clear of the lids
        hh = {'a_round': 9, 'b_pokemon': 6, 'c_zelda': 5, 'd_angular': 7}[s.id]
        pw, ph = {'a_round': (6, 7), 'b_pokemon': (5, 5), 'c_zelda': (4, 4), 'd_angular': (6, 6)}[s.id]
        for side in (-1, 1):
            cx = 16+side*(col(ex)-16)
            eye(c, cx, ey+1, e['w'], hh, pw, ph, iris, s.id == 'a_round', shape=e['shape'], px_offset=-1,
                lid=1 if s.id in ('a_round', 'b_pokemon') else None)
            inner = cx-side*e['w']/2
            outer = cx+side*e['w']/2
            top = ey+1-hh/2
            brow(c, inner, outer, top-1, top-4, INK, e['brow'])
        mouth(c, 15, my, 7, 'smirk', WARM)
    else:  # mason
        for side in (-1, 1):
            cx = 16+side*(col(ex)-16)
            eye(c, cx, ey, max(5, e['w']-2), max(4, e['h']-4), max(3, e['pw']-1), max(3, e['ph']-3), (36, 34, 36), False, shape='oval')
            brow(c, cx-e['w']/2, cx+e['w']/2-1, by-1, by-1, GRAY, e['brow']+1)
        shape = 'round' if s.id in ('a_round', 'b_pokemon') else 'rect'
        glasses(c, 16, ey, e['w']+1, e['h'], 2, INK, shape)
        mouth(c, 16, my, 6, 'smile', WARM)
        if s.id == 'c_zelda':
            # a couple of age lines beside the mouth
            c.line([(10, my-2), (9, my+2)], (170, 120, 70), 1)
            c.line([(21, my-2), (22, my+2)], (170, 120, 70), 1)
    return c


def paint_torso(style, design, height_units):
    """Front 150° of the torso; v=0 at the top ring, v=1 at the bottom ring."""
    s = style
    top = design.top_rgb
    c = Canvas((32, 32), tuple(round(v*255) for v in top))
    T = tuple(round(v*255) for v in top)
    shirt = tuple(round(v*255) for v in design.shirt_rgb)
    white = (240, 238, 228)
    dark = tuple(max(0, v-50) for v in T)
    if design.slug == 'mark':
        # dark shirt panel, red vest edges, white collar notch, straps, belt
        c.poly([(12, 0), (20, 0), (21, 26), (11, 26)], shirt)
        c.line([(11, 2), (11, 26)], dark, 1); c.line([(20, 2), (20, 26)], dark, 1)
        c.poly([(9, 0), (14, 0), (16, 5)], white); c.poly([(18, 0), (23, 0), (16, 5)], white)
        c.line([(6, 0), (9, 14)], (60, 40, 20), 2); c.line([(26, 0), (23, 14)], (60, 40, 20), 2)
        c.rect(0, 26, 32, 29, (28, 26, 30)); c.rect(14, 26, 18, 29, (170, 170, 165))
        c.rect(0, 29, 32, 32, tuple(round(v*255) for v in design.trousers_rgb))
    elif design.slug == 'ronald':
        c.poly([(12, 0), (20, 0), (20, 25), (12, 25)], shirt)
        c.line([(11, 0), (11, 25)], dark, 1); c.line([(20, 0), (20, 25)], dark, 1)
        c.poly([(8, 0), (14, 0), (16, 6)], white); c.poly([(18, 0), (24, 0), (16, 6)], white)
        c.rect(5, 10, 9, 13, white)   # chest badge
        c.rect(0, 25, 32, 28, (22, 22, 26)); c.rect(14, 25, 18, 28, (170, 170, 165))
        c.rect(0, 28, 32, 32, tuple(round(v*255) for v in design.trousers_rgb))
    else:  # mason lab coat: lapels, ochre shirt V, buttons, pockets
        lap = (252, 250, 242)
        c.poly([(13, 0), (19, 0), (16, 12)], shirt)
        c.line([(16, 3), (16, 12)], (90, 50, 20), 1)
        c.poly([(9, 0), (14, 0), (16, 13), (12, 10)], lap); c.poly([(18, 0), (23, 0), (20, 10), (16, 13)], lap)
        c.line([(12, 10), (16, 13), (20, 10)], (200, 198, 190), 1)
        c.line([(16, 13), (16, 32)], (205, 203, 195), 1)
        for y in (16, 21, 26):
            c.rect(15, y, 18, y+2, (150, 150, 146))
        c.rect(4, 19, 10, 20, (214, 212, 204)); c.rect(22, 19, 28, 20, (214, 212, 204))
    return c


# ---------------------------------------------------------------------------
def build(style, design, paint=True):
    s, d = style, design
    fig = Figure(s, d)
    fig.head()
    # torso rings per design
    rings = list(s.torso)
    if d.coat:
        hip_z = rings[0][0]
        coat_bottom = hip_z-.22 if s.id != 'c_zelda' else hip_z-.30
        rings = [(coat_bottom, rings[0][1]*1.22, rings[0][2]*1.20), (hip_z-.02, rings[0][1]*1.15, rings[0][2]*1.14), (rings[0][0]+.001, rings[0][1]*1.10, rings[0][2]*1.08)]+rings[1:]
    fig.torso(rings, f'{fig.prefix}_torso')
    if paint and Canvas:
        fig.textures[f'{fig.prefix}_torso'] = paint_torso(s, d, rings[-1][0]-rings[0][0])
        fig.textures[f'{fig.prefix}_face'] = paint_face(s, d)
    fig.arms()
    fig.legs()
    hair_for(fig)
    if d.backpack:
        backpack(fig)
    fig.shadow()
    return fig


def backpack(fig):
    s = fig.s
    z = (s.torso[0][0]+s.torso[-1][0])/2+.04
    depth = max(r[2] for r in s.torso)
    w = .40 if s.id != 'c_zelda' else .34
    h = .42 if s.id != 'c_zelda' else .40
    m = mat('pack', (.52, .33, .13))
    if s.angular:
        pack = box((0, -depth-.10, z), (w, .22, h), m, bevel=.04, name='backpack')
    else:
        pack = loft([superellipse_ring(z-h/2, w/2*.85, .10*.85, 0, -depth-.10, 8, 3.0),
                     superellipse_ring(z-h/2+.04, w/2, .11, 0, -depth-.10, 8, 3.0),
                     superellipse_ring(z+h/2-.05, w/2, .11, 0, -depth-.10, 8, 3.0),
                     superellipse_ring(z+h/2, w/2*.8, .10*.8, 0, -depth-.10, 8, 3.0)], m, smooth=s.smooth, name='backpack')
    fig.add(pack)


def hair_for(fig):
    s, d = fig.s, fig.d
    hair = mat('hair', d.hair_rgb)
    cy, cz = s.head_center
    rx, ry, rz = s.head_r
    eye_t = s.eye_t
    if d.slug == 'mark':
        cap = mat('cap', d.cap_rgb)
        base_t = eye_t+.30
        # hair band under the cap (sides and back) and two fringe tufts
        fig.hair_shell(hair, 1.05, eye_t+.02, base_t+.08, 88, 'hair band', ring_ts=[eye_t+.02, base_t+.09], pole=False)
        for side in (-1, 1):
            a = math.radians(side*30)
            bx, by = rx*math.sin(a)*1.0, cy+ry*math.cos(a)*1.0
            bz = fig.head_z(base_t+.02)
            tipz = fig.head_z(base_t-.22)
            fig.add(wedge([(bx-side*.09, by-.02, bz), (bx+side*.08, by+.0, bz), (bx+side*.08, by+.05, bz-.02), (bx-side*.09, by+.03, bz-.02)],
                          (bx+side*.11, by+.04, tipz), hair, s.smooth, 'fringe'))
        # cap crown
        ratio = 1.15 if s.id != 'c_zelda' else 1.12
        puff = .16 if s.id != 'c_zelda' else .12
        if s.angular:
            ts = [base_t, base_t+.30, base_t+.55, .92]
            rings = [fig.head_ring(t, ratio, .01) for t in ts]
            rings[2] = (rings[2][0]+puff*.5, rings[2][1], rings[2][2], 0, cy)
            rings[-1] = (fig.head_z(1)+puff, rx*.55, ry*.55, 0, cy)
            crown = lathe(rings, 8, cap, smooth=False, cap_bottom=False, cap_top=True, name='cap')
        else:
            ts = [base_t, base_t+.22, base_t+.48, .84, 1.0]
            rings = [fig.head_ring(t, ratio, .01) for t in ts]
            rings[1] = (rings[1][0]+puff*.15, rings[1][1]*1.02, rings[1][2]*1.02, 0, cy)
            rings[2] = (rings[2][0]+puff*.45, rings[2][1]*1.03, rings[2][2]*1.03, 0, cy)
            rings[3] = (rings[3][0]+puff*.8, rings[3][1]*.98, rings[3][2]*.98, 0, cy-.01)
            rings[-1] = (fig.head_z(1)+puff, 0, 0, 0, cy-.02)
            crown = lathe(rings, s.head_segs, cap, smooth=True, cap_bottom=False, name='cap')
        fig.add(crown)
        # brim: half-disc extruded, pointing to the viewer's left and back
        bz = fig.head_z(base_t)+.005
        r0 = max(rx, ry)*ratio*.98
        pts = [(0, -r0*.55)]+[(r0*.32*math.cos(math.radians(a)), r0*.36*math.sin(math.radians(a))+r0*.9) for a in range(-80, 81, 20)]
        pts = [(x, y) for x, y in pts]
        length = .30 if s.id != 'c_zelda' else .25
        outline = [(-r0*.45, r0*.55)]+[(length*1.15*math.cos(math.radians(a))*.8 if False else x, y) for x, y in []]
        # explicit brim outline: a tongue from the cap edge outward
        w = .21 if s.id != 'c_zelda' else .18
        outline = [(-w, r0*.55), (-w*.9, r0+length*.55), (-w*.5, r0+length*.95), (0, r0+length), (w*.5, r0+length*.95), (w*.9, r0+length*.55), (w, r0*.55)]
        brim = extrude(outline, bz-.02, bz+.025, cap, smooth=False, name='brim')
        brim.rotate_x(-24, about=(r0*.7, bz))
        brim.rotate_z(100)  # toward -X (viewer's left), slightly back
        fig.add(brim)
    elif d.slug == 'ronald':
        hairline = eye_t+.44
        fig.hair_shell(hair, 1.07, eye_t-.22, hairline, 66, 'hair', back_low=(50, eye_t-.1))
        # swept spikes: (angle deg from front, latitude, length factor, up bias)
        if s.id == 'a_round':
            spikes_def = [(-25, .92, .50, 1.0), (30, .90, .50, 1.0), (-85, .72, .46, .8), (85, .72, .46, .8), (-140, .52, .42, .6), (140, .52, .42, .6)]
            base_r = .14
        elif s.id == 'b_pokemon':
            spikes_def = [(-20, .92, .40, 1.0), (25, .90, .42, 1.0), (-75, .70, .40, .8), (75, .70, .40, .8), (-125, .55, .36, .7), (125, .55, .36, .7), (180, .40, .34, .5), (-160, .75, .30, .6)]
            base_r = .11
        elif s.id == 'c_zelda':
            spikes_def = [(-25, .90, .30, 1.0), (30, .88, .32, 1.0), (-85, .70, .30, .8), (85, .70, .30, .8), (150, .55, .28, .6), (-150, .55, .28, .6)]
            base_r = .09
        else:
            spikes_def = [(0, .92, .55, 1.0), (-65, .75, .52, .85), (65, .75, .52, .85), (-135, .55, .48, .7), (135, .55, .48, .7), (180, .35, .42, .5)]
            base_r = .15
        for ang, t, length, up in spikes_def:
            a = math.radians(ang)
            sc = fig.head_radius_scale(min(.98, t))
            bx, by = rx*sc*1.05*math.sin(a), cy+ry*sc*1.05*math.cos(a)
            bz = fig.head_z(t*1.05)
            n = normalize((bx, by-cy, (bz-cz)*1.0))
            out = normalize((n[0]*.5-math.sin(a)*.2, n[1]*.5-math.cos(a)*.35, up*1.0))
            tip = (bx+out[0]*length*rz*1.6, by+out[1]*length*rz*1.6, bz+out[2]*length*rz*1.6)
            sp = spike((bx, by, bz), base_r*s.hair_scale, base_r*.8*s.hair_scale, tip, hair, 4 if s.angular else 5, not s.angular, 'spike', n)
            fig.add(sp)
        # fringe: two forelocks sweeping toward the viewer's left
        for i, (ang, length) in enumerate([(-28, .36), (10, .30), (40, .26)]):
            a = math.radians(ang)
            sc = fig.head_radius_scale(hairline)
            bx, by = rx*sc*1.04*math.sin(a), cy+ry*sc*1.04*math.cos(a)
            bz = fig.head_z(hairline)+.02
            tip = (bx-.10, by+.06, bz-length*rz)
            fig.add(spike((bx, by, bz), .10*s.hair_scale, .055, tip, hair, 4, not s.angular, 'fringe', (0, .4, -1)))
    else:  # mason
        hairline = eye_t+.50 if s.id != 'c_zelda' else eye_t+.62
        fig.hair_shell(hair, 1.07, eye_t-.18, hairline, 70, 'hair', back_low=(55, eye_t-.05))
        white_hi = mat('hair_hi', tuple(min(1, v+.05) for v in d.hair_rgb))
        if s.id == 'a_round':
            # round cloud lobes over the crown and temples
            for (ang, t, r) in [(0, .78, .30), (-55, .62, .26), (55, .62, .26), (180, .55, .24)]:
                a = math.radians(ang)
                sc = fig.head_radius_scale(t)
                fig.add(ellipsoid((rx*sc*.95*math.sin(a), cy+ry*sc*.95*math.cos(a), fig.head_z(t)), (r, r*.9, r*.8), 8, 3, white_hi, True, 'lobe'))
        elif s.id == 'b_pokemon':
            # swept crest from the hairline up and back
            sc = fig.head_radius_scale(hairline)
            bz = fig.head_z(hairline)
            lobe = ellipsoid((-.06, cy+ry*sc*.62, fig.head_z(.84)), (.27, .25, .095), 8, 3, white_hi, True, 'crest')
            lobe.rotate_x(-30, about=(cy+ry*sc*.62, fig.head_z(.84)))
            fig.add(lobe)
            back_lobe = ellipsoid((.05, cy-ry*.25, fig.head_z(.90)), (.26, .22, .08), 8, 3, white_hi, True, 'crown lobe')
            fig.add(back_lobe)
            for side in (-1, 1):
                a = math.radians(side*68)
                sc2 = fig.head_radius_scale(.3)
                fig.add(ellipsoid((rx*sc2*1.0*math.sin(a), cy+ry*sc2*math.cos(a)-.03, fig.head_z(.25)), (.12, .16, .17), 6, 2, white_hi, True, 'temple'))
        elif s.id == 'c_zelda':
            # receding: side tufts and a thin comb-over
            for side in (-1, 1):
                a = math.radians(side*75)
                sc2 = fig.head_radius_scale(.2)
                fig.add(ellipsoid((rx*sc2*1.02*math.sin(a), cy+ry*sc2*math.cos(a)-.02, fig.head_z(.18)), (.09, .14, .16), 6, 2, white_hi, True, 'temple'))
            sc = fig.head_radius_scale(hairline)
            bz = fig.head_z(hairline)
            fig.add(wedge([(-.14, cy+ry*sc*1.0, bz-.01), (.14, cy+ry*sc*1.0, bz-.01), (.14, cy+ry*sc*.7, bz+.07), (-.14, cy+ry*sc*.7, bz+.07)],
                          (-.05, cy-.05, fig.head_z(1)+.08), white_hi, True, 'comb'))
        else:
            sc = fig.head_radius_scale(hairline)
            bz = fig.head_z(hairline)
            fig.add(wedge([(-.26, cy+ry*sc*1.03, bz-.02), (.20, cy+ry*sc*1.03, bz-.02), (.22, cy+ry*sc*.7, bz+.16), (-.28, cy+ry*sc*.7, bz+.16)],
                          (-.16, cy-.02, fig.head_z(1)+.16), white_hi, False, 'crest'))
            for side in (-1, 1):
                fig.add(box((side*rx*1.0, cy-.02, fig.head_z(.22)), (.14, .30, .32), white_hi, bevel=.03, name='temple'))
