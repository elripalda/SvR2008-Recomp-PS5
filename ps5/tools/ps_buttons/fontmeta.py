"""Font metrics leaves ('d\0\0\0' ... name=font000.yfb): the private-use button glyphs (U+E0xx)."""
import struct
def icon_glyphs(b):
    """Private-use (U+E0xx) glyphs of a SvR font metrics leaf: {code: (x, y, w, h)} on the icon page."""
    i = 0
    while True:
        i = b.find(b'\x00\xe0\x00\x00', i)
        if i < 0: return {}
        if i % 4 == 0:
            last = struct.unpack_from('<I', b, i + 4)[0]
            if 0xE000 <= last < 0xE100: break
        i += 1
    codes = []
    p = i + 16
    while True:
        c = struct.unpack_from('<H', b, p)[0]
        if c == 0: break
        codes.append(c); p += 2
    q = i + 16 + ((len(codes) * 2 + 15) // 16) * 16
    out = {}
    for n, c in enumerate(codes):
        if q + 16 * n + 16 > len(b): break
        x, y, w, h, page = struct.unpack_from('<5H', b, q + 16 * n)
        out[c] = (x, y, w, h, page)
    return out
