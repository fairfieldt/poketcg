"""Original 32x32 facial paint for the four story leads.

Broad marks are authored for the actual small face plane, not extra geometry.
The ordinary cast keeps its shared face recipes. All faces remain opaque; the
skin pass subsequently gives the background the same RGB5 pigment as the body.
"""
LEADS = ('mark', 'ronald', 'dr_mason', 'imakuni')


def paint(slug, background=(227, 172, 100, 255)):
    if slug not in LEADS:
        return None
    pixels = [[background for _ in range(32)] for _ in range(32)]
    ink = (33, 24, 24, 255)
    eye = (33, 41, 57, 255)
    white = (247, 239, 214, 255)
    gray = (132, 132, 123, 255)
    warm = (99, 57, 33, 255)

    def rect(x0, y0, x1, y1, color):
        for y in range(max(0, y0), min(32, y1)):
            for x in range(max(0, x0), min(32, x1)):
                pixels[y][x] = color

    def ellipse(x0, y0, x1, y1, color):
        rx, ry = (x1-x0)/2, (y1-y0)/2
        cx, cy = (x1+x0)/2, (y1+y0)/2
        for y in range(max(0, y0), min(32, y1)):
            for x in range(max(0, x0), min(32, x1)):
                if ((x+.5-cx)/rx)**2 + ((y+.5-cy)/ry)**2 <= 1:
                    pixels[y][x] = color

    def line(points, color, width=2):
        for (x0, y0), (x1, y1) in zip(points, points[1:]):
            dx, dy = abs(x1-x0), -abs(y1-y0)
            sx, sy = 1 if x0 < x1 else -1, 1 if y0 < y1 else -1
            error = dx+dy
            while True:
                rect(x0, y0, x0+width, y0+width, color)
                if x0 == x1 and y0 == y1:
                    break
                twice = 2*error
                if twice >= dy:
                    error += dy
                    x0 += sx
                if twice <= dx:
                    error += dx
                    y0 += sy

    if slug == 'mark':
        # Open eyes and substantial dark pupils feel young; the calm brow and
        # restrained, slightly raised smile keep confidence rather than surprise.
        for left in (3, 19):
            ellipse(left, 10, left+11, 23, white)
            ellipse(left+3, 11, left+9, 23, eye)
            rect(left+4, 12, left+6, 15, white)
            line([(left+1, 10), (left+7, 9), (left+9, 10)], ink, 1)
        line([(4, 7), (9, 7), (12, 8)], ink)
        line([(20, 8), (23, 7), (27, 7)], ink)
        line([(11, 27), (14, 28), (18, 28), (21, 26)], warm)
    elif slug == 'ronald':
        # Lower upper lids, a sideways glance and unequal brow heights give a
        # knowing rival expression. The mouth turns up on only one side.
        for left in (3, 19):
            ellipse(left, 12, left+11, 22, white)
            rect(left+2, 13, left+7, 21, eye)
            rect(left+3, 14, left+5, 16, white)
        line([(3, 12), (7, 13), (12, 14)], ink)
        line([(19, 13), (24, 12), (28, 12)], ink)
        line([(3, 7), (8, 8), (12, 10)], ink)
        line([(19, 6), (23, 4), (28, 5)], ink)
        line([(11, 27), (17, 27), (21, 24)], warm)
        rect(21, 23, 23, 25, warm)
    elif slug == 'dr_mason':
        # Broad gray brows and big square spectacles are the professor's
        # signature. Clear lens interiors preserve his kind, visible eyes.
        line([(3, 7), (7, 6), (12, 7)], gray, 3)
        line([(19, 7), (23, 6), (28, 7)], gray, 3)
        for left in (2, 18):
            rect(left, 11, left+12, 24, ink)
            rect(left+2, 13, left+10, 22, white)
            ellipse(left+4, 14, left+8, 22, eye)
            rect(left+4, 14, left+6, 16, white)
        rect(0, 14, 3, 17, ink)
        rect(29, 14, 32, 17, ink)
        rect(14, 14, 18, 17, ink)
        line([(11, 27), (14, 29), (18, 29), (21, 27)], warm)
    else:
        # Imakuni's hood already supplies a strong silhouette. Unequal raised
        # brows, round staring eyes and an open toothy grin carry the oddness.
        line([(3, 7), (7, 5), (12, 7)], ink)
        line([(20, 6), (25, 4), (29, 5)], ink)
        for left in (3, 19):
            ellipse(left, 10, left+11, 23, white)
            ellipse(left+3, 11, left+8, 23, ink)
        ellipse(8, 24, 25, 32, ink)
        rect(11, 25, 22, 28, white)
    return pixels
