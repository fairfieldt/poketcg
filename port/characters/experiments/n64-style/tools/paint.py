"""32x32 RGBA16-safe texture painting for faces and costume fronts (Pillow)."""
from PIL import Image, ImageDraw
from colors import q16, q16f


class Canvas:
    def __init__(self, size=(32, 32), background=(231, 173, 99)):
        self.size = size
        self.im = Image.new('RGBA', size, q16(background)+(255,))
        self.d = ImageDraw.Draw(self.im)

    def rect(self, x0, y0, x1, y1, c):
        self.d.rectangle([x0, y0, x1-1, y1-1], fill=q16(c)+(255,))

    def ellipse(self, x0, y0, x1, y1, c):
        self.d.ellipse([x0, y0, x1-1, y1-1], fill=q16(c)+(255,))

    def poly(self, pts, c):
        self.d.polygon(pts, fill=q16(c)+(255,))

    def line(self, pts, c, w=1):
        self.d.line(pts, fill=q16(c)+(255,), width=w)

    def px(self, x, y, c):
        if 0 <= x < self.size[0] and 0 <= y < self.size[1]:
            self.im.putpixel((x, y), q16(c)+(255,))

    def mirror_left_to_right(self):
        w = self.size[0]
        left = self.im.crop((0, 0, w//2, self.size[1])).transpose(Image.FLIP_LEFT_RIGHT)
        self.im.paste(left, (w//2, 0))

    def save(self, path):
        # Quantize every pixel to RGBA16 so Blender previews match the console.
        px = self.im.load()
        for y in range(self.size[1]):
            for x in range(self.size[0]):
                r, g, b, a = px[x, y]
                px[x, y] = q16((r, g, b))+(255,)
        self.im.save(path)
        return path


INK = (34, 26, 26)
EYE_WHITE = (247, 240, 220)
GRAY = (132, 132, 124)
WARM = (120, 66, 40)


def eye(c, cx, cy, w, h, pupil_w, pupil_h, iris=(33, 41, 57), highlight=True, lid=None, shape='oval', px_offset=0, py_offset=0):
    x0, y0 = cx-w/2, cy-h/2
    x1, y1 = cx+w/2, cy+h/2
    if shape == 'oval':
        c.ellipse(round(x0), round(y0), round(x1), round(y1), EYE_WHITE)
        c.ellipse(round(cx-pupil_w/2+px_offset), round(cy-pupil_h/2+py_offset), round(cx+pupil_w/2+px_offset), round(cy+pupil_h/2+py_offset), iris)
        if highlight and pupil_w >= 4:
            c.rect(round(cx-pupil_w/2+px_offset)+1, round(cy-pupil_h/2+py_offset)+1, round(cx-pupil_w/2+px_offset)+3, round(cy-pupil_h/2+py_offset)+3, EYE_WHITE)
    elif shape == 'rect':
        c.rect(round(x0), round(y0), round(x1), round(y1), EYE_WHITE)
        c.rect(round(cx-pupil_w/2+px_offset), round(cy-pupil_h/2+py_offset), round(cx+pupil_w/2+px_offset), round(cy+pupil_h/2+py_offset), iris)
        if highlight and pupil_w >= 4:
            c.rect(round(cx-pupil_w/2+px_offset)+1, round(cy-pupil_h/2+py_offset)+1, round(cx-pupil_w/2+px_offset)+2, round(cy-pupil_h/2+py_offset)+2, EYE_WHITE)
    elif shape == 'sharp':
        # dark angular eye with a small outer white sliver (hard-surface look)
        pts = [(x0+1, y0), (x1, y0+1), (x1-1, y1), (x0, y1-1)]
        c.poly(pts, iris)
        c.rect(round(x0)+1, round(y0)+2, round(x0)+3, round(y1)-2, EYE_WHITE)
    elif shape == 'slant':
        # parallelogram white with angular pupil (hard-surface style)
        pts = [(x0+1, y0), (x1, y0), (x1-1, y1), (x0, y1)]
        c.poly(pts, EYE_WHITE)
        c.rect(round(cx-pupil_w/2+px_offset), round(cy-pupil_h/2+py_offset), round(cx+pupil_w/2+px_offset), round(cy+pupil_h/2+py_offset), iris)
    if lid:
        # upper lid: skin-coloured band over the top of the eye
        c.rect(round(x0)-1, round(y0)-1, round(x1)+1, round(y0+lid), c.im.getpixel((0, 0))[:3])


def brow(c, x0, x1, y0, y1, color=INK, thick=2):
    c.line([(x0, y0), (x1, y1)], color, thick)


def glasses(c, cx, cy, w, h, gap=3, color=INK, shape='round'):
    for side in (-1, 1):
        x0 = cx+side*(gap/2)+(0 if side > 0 else -w)
        x1 = x0+w
        if shape == 'round':
            c.d.ellipse([x0, cy-h/2, x1, cy+h/2], outline=q16(color)+(255,), width=1)
        else:
            c.d.rectangle([x0, cy-h/2, x1, cy+h/2], outline=q16(color)+(255,), width=1)
    c.line([(cx-gap/2, cy), (cx+gap/2, cy)], color, 1)
    c.line([(cx-gap/2-w, cy-1), (0, cy-2)], color, 1)
    c.line([(cx+gap/2+w, cy-1), (31, cy-2)], color, 1)


def mouth(c, cx, cy, w, kind='smile', color=WARM):
    if kind == 'smile':
        c.line([(cx-w/2, cy-1), (cx-w/4, cy), (cx+w/4, cy), (cx+w/2, cy-1)], color, 1)
    elif kind == 'smirk':
        c.line([(cx-w/2, cy), (cx+w/4, cy), (cx+w/2, cy-2)], color, 1)
    elif kind == 'line':
        c.line([(cx-w/2, cy), (cx+w/2, cy)], color, 1)
    elif kind == 'open':
        c.ellipse(round(cx-w/2), cy-1, round(cx+w/2), cy+2, INK)
    elif kind == 'grin':
        c.line([(cx-w/2, cy-1), (cx, cy+1), (cx+w/2, cy-1)], color, 2)
