"""Minimal PNG reader/writer (standard library only) for the web frontend's build.

read_png(path) -> (width, height, pixels, palette)
    pixels: list of rows, each a list of palette indexes (indexed/grayscale images)
            or (r, g, b, a) tuples (truecolor images)
    palette: list of (r, g, b) for indexed images, grayscale ramp for gray images, else None
write_png(path, width, height, rgba_rows)  rgba_rows: list of bytes/bytearray (4 bytes per pixel)
"""
import struct
import zlib


def _paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    return b if pb <= pc else c


def read_png(path):
    data = open(path, 'rb').read()
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        raise ValueError(f'{path}: not a PNG')
    pos, idat, plte, trns = 8, b'', None, None
    while pos < len(data):
        (length,) = struct.unpack('>I', data[pos:pos + 4])
        ctype = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if ctype == b'IHDR':
            width, height, depth, color, _, _, interlace = struct.unpack('>IIBBBBB', body)
            if interlace:
                raise ValueError(f'{path}: interlaced PNGs are not supported')
        elif ctype == b'PLTE':
            plte = [tuple(body[i:i + 3]) for i in range(0, len(body), 3)]
        elif ctype == b'tRNS':
            trns = body
        elif ctype == b'IDAT':
            idat += body
        elif ctype == b'IEND':
            break
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[color]
    bpp = max(1, channels * depth // 8)          # bytes per pixel for filtering
    stride = (width * channels * depth + 7) // 8
    raw = zlib.decompress(idat)
    rows, prev = [], bytearray(stride)
    for y in range(height):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if f == 1:
                line[i] = (line[i] + a) & 0xff
            elif f == 2:
                line[i] = (line[i] + b) & 0xff
            elif f == 3:
                line[i] = (line[i] + ((a + b) >> 1)) & 0xff
            elif f == 4:
                line[i] = (line[i] + _paeth(a, b, c)) & 0xff
        rows.append(bytes(line))
        prev = line
    pixels = []
    for line in rows:
        if color in (0, 3) and depth < 8:
            per = 8 // depth
            mask = (1 << depth) - 1
            px = []
            for x in range(width):
                byte = line[x // per]
                shift = 8 - depth * (x % per + 1)
                px.append((byte >> shift) & mask)
            pixels.append(px)
        elif color in (0, 3):
            pixels.append(list(line[:width]))
        elif color == 2:
            pixels.append([tuple(line[i * 3:i * 3 + 3]) + (255,) for i in range(width)])
        elif color == 6:
            pixels.append([tuple(line[i * 4:i * 4 + 4]) for i in range(width)])
        else:
            raise ValueError(f'{path}: unsupported color type {color}')
    palette = None
    if color == 3:
        palette = plte
    elif color == 0:
        n = 1 << depth
        palette = [(v * 255 // (n - 1),) * 3 for v in range(n)]
    return width, height, pixels, palette


def write_png(path, width, height, rgba_rows):
    raw = b''.join(b'\x00' + bytes(r) for r in rgba_rows)

    def chunk(t, body):
        return struct.pack('>I', len(body)) + t + body + struct.pack('>I', zlib.crc32(t + body) & 0xffffffff)

    out = b'\x89PNG\r\n\x1a\n'
    out += chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0))
    out += chunk(b'IDAT', zlib.compress(raw, 9))
    out += chunk(b'IEND', b'')
    with open(path, 'wb') as f:
        f.write(out)
