"""The edits applied to a game texture when it's loaded (the port does the same in
src/native/svr_pad_art.cpp): the texture is decoded, edited and uploaded in its place.

  clear  x y w h            alpha to 0 in the rectangle
  paste  glyph x y          the glyph over the picture ("over" blending)
  fill   mask x y           the mask's pixels filled in from around them (smooth, from the
                            surrounding picture), for pictures with no transparency
  copy   sx sy w h dx dy    copy a block of the original picture (masked by brightness when
                            `bright` is set: only the light lettering and its dark edge move)
"""
from dataclasses import dataclass, field

import numpy as np
from PIL import Image


@dataclass
class Edit:
    key: int                       # content key of the original (see catalog.py)
    w: int
    h: int
    fmt: str                       # 'DXT1' / 'DXT3' / 'DXT5'
    name: str                      # for logs
    ops: list = field(default_factory=list)


def clear(img, x, y, w, h):
    a = np.asarray(img).copy()
    a[max(0, y):y + h, max(0, x):x + w, 3] = 0
    return Image.fromarray(a, "RGBA")


def over(dst, src, x, y):
    d = np.asarray(dst).astype(np.float32) / 255.0
    s = np.asarray(src).astype(np.float32) / 255.0
    H, W = d.shape[:2]
    h, w = s.shape[:2]
    x0, y0 = max(0, x), max(0, y)
    x1, y1 = min(W, x + w), min(H, y + h)
    if x1 <= x0 or y1 <= y0:
        return dst
    sd = s[y0 - y:y1 - y, x0 - x:x1 - x]
    dd = d[y0:y1, x0:x1]
    sa = sd[..., 3:4]
    da = dd[..., 3:4]
    oa = sa + da * (1 - sa)
    rgb = np.where(oa > 0, (sd[..., :3] * sa + dd[..., :3] * da * (1 - sa)) / np.maximum(oa, 1e-6), 0)
    d[y0:y1, x0:x1] = np.concatenate([rgb, oa], axis=2)
    return Image.fromarray((d * 255 + 0.5).clip(0, 255).astype(np.uint8), "RGBA")


def fill(img, mask, x, y):
    """Push-pull fill: masked pixels take a smooth blend of the picture around them."""
    a = np.asarray(img).astype(np.float32)
    m = np.zeros(a.shape[:2], np.float32)
    mk = (np.asarray(mask)[..., 3] > 127).astype(np.float32)
    h, w = mk.shape
    m[y:y + h, x:x + w] = mk
    known = 1 - m
    levels = []
    c, k = a * known[..., None], known.copy()
    while min(c.shape[:2]) > 1:
        levels.append((c, k))
        H2, W2 = (c.shape[0] + 1) // 2, (c.shape[1] + 1) // 2
        cp = np.zeros((H2 * 2, W2 * 2, 4), np.float32); cp[:c.shape[0], :c.shape[1]] = c
        kp = np.zeros((H2 * 2, W2 * 2), np.float32); kp[:k.shape[0], :k.shape[1]] = k
        c = cp.reshape(H2, 2, W2, 2, 4).sum((1, 3))
        k = kp.reshape(H2, 2, W2, 2).sum((1, 3))
        c = c / np.maximum(k, 1e-6)[..., None] * np.minimum(k, 1)[..., None]
        k = np.minimum(k, 1)
    levels.append((c, k))
    up = levels[-1][0] / np.maximum(levels[-1][1], 1e-6)[..., None]
    for c, k in reversed(levels[:-1]):
        u = np.repeat(np.repeat(up, 2, 0), 2, 1)[:c.shape[0], :c.shape[1]]
        # smooth the upsample a little (3x3 box) so blocks don't show
        pad = np.pad(u, ((1, 1), (1, 1), (0, 0)), mode="edge")
        u = sum(pad[dy:dy + u.shape[0], dx:dx + u.shape[1]] for dy in range(3) for dx in range(3)) / 9
        cur = c / np.maximum(k, 1e-6)[..., None]
        up = np.where(k[..., None] >= 0.999, cur, cur * k[..., None] + u * (1 - k[..., None]))
    out = np.where(m[..., None] > 0, up, a)
    return Image.fromarray(out.clip(0, 255).astype(np.uint8), "RGBA")


def copy(dst, src, sx, sy, w, h, dx, dy, bright=False):
    block = src.crop((sx, sy, sx + w, sy + h))
    if bright:
        b = np.asarray(block).astype(np.float32)
        luma = b[..., 0] * 0.299 + b[..., 1] * 0.587 + b[..., 2] * 0.114
        core = np.clip((luma - 150) / 60, 0, 1)
        # the lettering's dark edge: core grown by 2 px
        grown = core.copy()
        for _ in range(2):
            p = np.pad(grown, 1)
            grown = np.maximum.reduce([p[1:-1, 1:-1], p[:-2, 1:-1], p[2:, 1:-1], p[1:-1, :-2], p[1:-1, 2:]])
        b[..., 3] = grown * 255
        block = Image.fromarray(b.astype(np.uint8), "RGBA")
        return over(dst, block, dx, dy)
    d = dst.copy()
    d.paste(block, (dx, dy))
    return d


def bleed(img, passes=2):
    """Transparent pixels next to visible ones take their colour, so filtering at the edges
    doesn't pull in what was there before (or black)."""
    a = np.asarray(img).astype(np.float32)
    for _ in range(passes):
        al = a[..., 3]
        w = (al > 0).astype(np.float32) * al
        p = np.pad(a[..., :3] * w[..., None], ((1, 1), (1, 1), (0, 0)))
        pw = np.pad(w, 1)
        H, W = al.shape
        s = sum(p[dy:dy + H, dx:dx + W] for dy in range(3) for dx in range(3))
        sw = sum(pw[dy:dy + H, dx:dx + W] for dy in range(3) for dx in range(3))
        hole = (al == 0) & (sw > 0)
        a[..., :3] = np.where(hole[..., None], s / np.maximum(sw, 1e-6)[..., None], a[..., :3])
        # (a filled hole counts as visible for the next pass, still with alpha 0)
        a[..., 3] = np.where(hole, 0.001, a[..., 3])
    a[..., 3] = np.where(a[..., 3] < 0.5, 0, a[..., 3])
    return Image.fromarray(np.clip(a + 0.5, 0, 255).astype(np.uint8), "RGBA")


def apply(img, ops, glyphs):
    original = img
    for op in ops:
        kind = op[0]
        if kind == "clear":
            img = clear(img, *op[1:5])
        elif kind == "paste":
            img = over(img, glyphs[op[1]], op[2], op[3])
        elif kind == "fill":
            img = fill(img, glyphs[op[1]], op[2], op[3])
        elif kind == "copy":
            img = copy(img, original, *op[1:7], bright=bool(op[7]) if len(op) > 7 else False)
    return bleed(img)
