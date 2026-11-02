"""RGBA16-safe colour quantisation shared by geometry, painting and Blender import."""


def q16(rgb):
    """Nearest RGB5 value expanded exactly as the RDP expands RGBA16 texels."""
    levels = [(v << 3) | (v >> 2) for v in range(32)]
    return tuple(min(levels, key=lambda l: abs(l-round(c))) for c in rgb[:3])


def q16f(rgb):
    return tuple(c/255 for c in q16(tuple(c*255 for c in rgb)))
