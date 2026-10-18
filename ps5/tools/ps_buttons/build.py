"""Builds ps_buttons.bin: PlayStation button pictures and where they go in SvR 2008's textures.

    python3 build.py <game folder> <out dir> [dualsense.png]

Reads the game's archives only to find where each Xbox picture sits (and to write previews
into <out dir>/preview for checking); ps_buttons.bin itself holds only the drawn PlayStation
pictures and the edits, none of the game's own art. The port finds each texture by the
content key of its top level when it's loaded and applies the edits (src/native/svr_pad_art.cpp).
"""
import io
import json
import os
import struct
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import glyphs as G  # noqa: E402
import ops as O  # noqa: E402
from catalog import textures, dds_info, content_key  # noqa: E402

GAME = sys.argv[1] if len(sys.argv) > 1 else None
OUT = sys.argv[2] if len(sys.argv) > 2 else "out"
DUALSENSE = sys.argv[3] if len(sys.argv) > 3 else os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "..", "art", "local", "ps_buttons", "dualsense.png")


# ---------------------------------------------------------------- helpers

def alpha_bbox(img, x, y, w, h, thresh=24):
    a = np.asarray(img)[y:y + h, x:x + w, 3]
    ys, xs = np.nonzero(a > thresh)
    if not len(xs):
        return None
    return x + int(xs.min()), y + int(ys.min()), int(xs.max() - xs.min() + 1), int(ys.max() - ys.min() + 1)


class Builder:
    def __init__(self):
        self.masks = set()        # glyphs stored as alpha only
        self.glyphs = []          # PIL RGBA images
        self.glyph_ids = {}       # (kind, args) -> index
        self.edits = {}           # key -> O.Edit
        self.previews = []        # (name, before, after)

    def glyph(self, fn, w, h, *args, **kw):
        k = (fn.__name__, w, h, args, tuple(sorted(kw.items())))
        if k not in self.glyph_ids:
            self.glyph_ids[k] = len(self.glyphs)
            self.glyphs.append(fn(w, h, *args, **kw))
        return self.glyph_ids[k]

    def image(self, gid):
        return self.glyphs[gid]


B = Builder()


def put(ops, box, fn, *args, clear_box=None, pad=1, **kw):
    """Clear the Xbox picture's box and draw `fn` in it."""
    x, y, w, h = box
    cx, cy, cw, ch = clear_box or (x - pad, y - pad, w + 2 * pad, h + 2 * pad)
    ops.append(("clear", cx, cy, cw, ch))
    ops.append(("paste", B.glyph(fn, w, h, *args, **kw), x, y))


# ---------------------------------------------------------------- the font button pages

# The standard page (256x512): cell -> what goes there. The WWE logo and the stars stay.
STD = {
    (0, 0, 64, 52): (G.face, "square"),
    (64, 0, 64, 52): (G.face, "triangle"),
    (128, 0, 64, 52): (G.face, "circle"),
    (192, 0, 64, 52): (G.face, "cross"),
    (0, 56, 64, 52): (G.stick, "L"),
    (64, 56, 64, 52): (G.stick, "R"),
    (128, 56, 64, 52): (G.stick_click, "R3"),
    (192, 56, 64, 52): ("dpad", (), ()),
    (0, 112, 64, 52): ("dpad", ("up",), ()),
    (64, 112, 64, 52): ("dpad", ("down",), ()),
    (128, 112, 64, 52): ("dpad", ("left",), ()),
    (192, 112, 64, 52): ("dpad", ("right",), ()),
    (0, 168, 64, 52): ("dpad", ("up", "down"), ()),
    (64, 168, 64, 52): ("dpad", ("left", "right"), ()),
    (128, 168, 64, 52): ("dpad", (), ("ul", "ur")),
    (192, 168, 64, 52): ("dpad", (), ("dl", "dr")),
    (0, 224, 76, 52): (G.bumper, "L1"),
    (76, 224, 76, 52): (G.bumper, "R1"),
    (152, 224, 96, 52): (G.touchpad,),
    (0, 280, 76, 52): (G.trigger, "L2"),
    (76, 280, 76, 52): (G.trigger, "R2"),
    (152, 280, 96, 52): (G.options,),
    (66, 336, 64, 52): (G.stick_click, "L3"),
}

# The menu font with its own layout (fontHD FNTG/50).
PCMENU = {
    (1, 0, 55, 56): (G.face, "square"),
    (57, 0, 55, 56): (G.face, "triangle"),
    (113, 0, 55, 56): (G.face, "circle"),
    (169, 0, 55, 56): (G.face, "cross"),
    (0, 56, 58, 56): (G.touchpad,),
    (59, 56, 58, 56): (G.options,),
    (118, 56, 59, 56): ("dpad", (), ()),
    (178, 56, 57, 56): (G.ps_home,),
    (0, 112, 39, 56): (G.trigger, "L2"),
    (40, 112, 41, 56): (G.trigger, "R2"),
    (82, 112, 82, 56): (G.bumper, "L1"),
    (165, 112, 82, 56): (G.bumper, "R1"),
    (0, 168, 61, 56): (G.stick, "L"),
    (62, 168, 60, 56): (G.stick, "R"),
    (176, 168, 59, 56): (G.stick_click, "L3"),
    (198, 224, 56, 56): (G.stick_click, "R3"),
    (0, 280, 57, 56): ("dpad", ("left", "right"), ()),
    (58, 280, 57, 56): ("dpad", ("up", "down"), ()),
}


def dpad_base_box(img, cells):
    """The d-pad disc's box inside a cell, from the plain d-pad cell (the lit ones have red
    triangles sticking out)."""
    plain = [c for c, v in cells.items() if v[0] == "dpad" and not v[1] and not v[2]][0]
    bx, by, bw, bh = alpha_bbox(img, *plain)
    return bx - plain[0], by - plain[1], bw, bh


def page_edits(img, cells, ops):
    db = dpad_base_box(img, cells)
    for cell, spec in cells.items():
        x, y, w, h = cell
        if spec[0] == "dpad":
            box = (x + db[0], y + db[1], db[2], db[3])
            got = alpha_bbox(img, *cell)
            ops.append(("clear", x, y, w, h))
            ops.append(("paste", B.glyph(G.dpad, box[2], box[3], lit=spec[1], corners=spec[2]), box[0], box[1]))
            continue
        box = alpha_bbox(img, *cell)
        if box is None:
            continue
        ops.append(("clear", x, y, w, h))
        ops.append(("paste", B.glyph(spec[0], box[2], box[3], *spec[1:]), box[0], box[1]))


def stick_in_box(box, letter, ops):
    """A stick picture with direction arrows around it: only the stick (box) changes."""
    x, y, w, h = box
    ops.append(("clear", x, y, w, h))
    ops.append(("paste", B.glyph(G.stick, w, h, letter), x, y))


# FNTM's second page half: the left and right sticks with direction arrows, 84x72 cells,
# the stick at (30, 14) 36x44 in each.
FNTM_STICKS = [((256 + 84 * c + 30, 76 * r + 14, 36, 44), "L" if r < 3 else "R")
               for r in range(6) for c in range(3)]
PCMENU_STICKS = [((12, 225, 46, 54), "L"), ((122, 225, 46, 54), "L")]


# ---------------------------------------------------------------- in-match prompts (game2dHD)

# The English textures (GA2D) with the box of each Xbox picture; the other languages (GG, GI,
# GS) have the same pictures elsewhere in their own texture, found by matching.
PROMPTS = {
    "0/10/0": [((2, 2, 48, 48), G.stick, "R"), ((54, 2, 48, 48), G.stick, "R")],
    "0/11/0": [((17, 32, 35, 32), G.trigger, "L2"), ((78, 32, 38, 32), G.trigger, "R2")],
    "0/23/0": [((39, 0, 36, 36), G.face, "triangle")],
    "0/2/0": [((71, 6, 51, 52), G.stick, "R")],
    "0/3/0": [((73, 6, 52, 52), G.stick, "R")],
    "0/6/0": [((106, 17, 52, 32), G.bumper, "L1"), ((183, 6, 36, 51), G.trigger, "L2")],
    "0/7/0": [((54, 0, 46, 32), G.face, "triangle")],
    "0/8/0": [((84, 6, 44, 52), G.stick, "R")],
    "7/0/0": [((2, 2, 48, 48), G.stick, "L"), ((54, 2, 48, 48), G.stick, "R")],
    "7/1/0": [((0, 0, 48, 34), G.stick, "L"), ((50, 0, 48, 34), G.stick, "R")],
    "8/0/0": [((104, 0, 29, 40), G.trigger, "L2"), ((164, 0, 31, 40), G.trigger, "R2")],
    # the same in every language
    "0/9/1": [((0, 0, 32, 32), G.face, "triangle")],
    "12/90/13": [((6, 6, 52, 52), G.stick, "L")],
    "1/0/2": [((17, 0, 42, 52), G.face, "cross")],
    "1/0/3": [((17, 0, 42, 52), G.face, "circle")],
    "1/0/4": [((17, 0, 42, 52), G.face, "triangle")],
    "6/0/0": [((0, 0, 32, 32), G.face, "cross")],
}


def refine(img, box):
    """The exact box of the picture found near `box`: the pieces (alpha blobs) centred in it."""
    from scipy import ndimage
    x, y, w, h = box
    a = np.asarray(img)[..., 3] > 24
    lab, n = ndimage.label(a)
    if not n:
        return box
    cents = ndimage.center_of_mass(a, lab, range(1, n + 1))
    objs = ndimage.find_objects(lab)
    got = None
    for (cy, cx), sl in zip(cents, objs):
        if x - 2 <= cx <= x + w + 2 and y - 2 <= cy <= y + h + 2:
            bx0, by0, bx1, by1 = sl[1].start, sl[0].start, sl[1].stop, sl[0].stop
            # a piece much bigger than the picture is lettering it touches: not ours
            if (bx1 - bx0) > w * 1.4 or (by1 - by0) > h * 1.4:
                continue
            got = (bx0, by0, bx1, by1) if got is None else (min(got[0], bx0), min(got[1], by0),
                                                             max(got[2], bx1), max(got[3], by1))
    if got is None:
        return box
    return got[0], got[1], got[2] - got[0], got[3] - got[1]


def find(img, tmpl, taken=()):
    """Best position of tmpl in img: sum of squared differences over premultiplied RGBA (the
    colour under transparent pixels is noise). Positions overlapping `taken` boxes are skipped."""
    from scipy.signal import fftconvolve

    def premul(im):
        a = np.asarray(im).astype(np.float64) / 255.0
        return np.concatenate([a[..., :3] * a[..., 3:4], a[..., 3:4]], axis=2)
    I, T = premul(img), premul(tmpl)
    th, tw = T.shape[:2]
    ssd = 0
    for c in range(4):
        i, t = I[..., c], T[..., c]
        ssd = ssd + fftconvolve(i * i, np.ones_like(t), mode="valid") \
            - 2 * fftconvolve(i, t[::-1, ::-1], mode="valid") + (t * t).sum()
    for (x, y, w, h) in taken:
        ssd[max(0, y - th + 4):y + h - 3, max(0, x - tw + 4):x + w - 3] = np.inf
    y, x = np.unravel_index(np.argmin(ssd), ssd.shape)
    return int(x), int(y), float(ssd[y, x]) / (th * tw)


# ---------------------------------------------------------------- main

PACS = ["menu/fontHD.pac", "menu/MatchHD.pac", "menu/LoadHD.pac", "game2dHD.pac", "menu/menuHD.pac",
        "menu/ScrLayHD.pac"]


def decode(dds):
    return Image.open(io.BytesIO(dds)).convert("RGBA")


def add_edit(key, dds, name, ops, img=None):
    if key in B.edits or not ops:
        return
    w, h, fmt, _ = dds_info(dds)
    B.edits[key] = O.Edit(key, w, h, fmt, name, ops)
    before = img if img is not None else decode(dds)
    B.previews.append((name, before, O.apply(before, ops, B.glyphs)))


def font_pages(pac_path, tag, items):
    """The button page of every font in this pac."""
    import re
    import fontmeta
    from catalog import leaves
    meta = {k: b for k, b in leaves(pac_path) if b[:4] == b"d\0\0\0"}
    fonts = {}
    for name, dds in items:
        m = re.match(r"^[^:]+:(.+)/(\d+)/(\d+)$", name)
        if m:
            fonts.setdefault((m.group(1), int(m.group(2))), []).append((int(m.group(3)), name, dds))
    for (prefix, fid), pages in fonts.items():
        table = fontmeta.icon_glyphs(meta.get(f"{prefix}/{fid + 1}", b""))
        if not table:
            continue
        _, name, dds = max(pages)
        key = content_key(dds)
        if key in B.edits:
            continue
        img = decode(dds)
        ops = []
        if 0xE017 in table:
            page_edits(img, PCMENU, ops)
            for box, letter in PCMENU_STICKS:
                stick_in_box(box, letter, ops)
        else:
            page_edits(img, STD, ops)
            if 0xE01B in table and img.width >= 512:
                for box, letter in FNTM_STICKS:
                    stick_in_box(box, letter, ops)
        add_edit(key, dds, name, ops, img)


def prompts(items):
    import re
    by = {}
    for name, dds in items:
        m = re.match(r"^game2dHD:G(.)2D/BASE/(.+)$", name)
        if m and m.group(2) in PROMPTS:
            by.setdefault(m.group(2), {})[m.group(1)] = (name, dds)
    for t, spec in PROMPTS.items():
        langs = by.get(t, {})
        if "A" not in langs:
            print(f"  game2d {t}: not found")
            continue
        en = decode(langs["A"][1])
        for lang, (name, dds) in sorted(langs.items()):
            img = decode(dds)
            ops = []
            taken = []
            for box, fn, *args in spec:
                x, y, w, h = box
                if lang != "A" and content_key(dds) != content_key(langs["A"][1]):
                    tx, ty, err = find(img, en.crop((x, y, x + w, y + h)), taken)
                    if err > 0.004:
                        print(f"  {name}: {fn.__name__} {args} matched at {tx},{ty} err {err:.4f}")
                    x, y = tx, ty
                if lang != "A":
                    x, y, w, h = refine(img, (x, y, w, h))
                taken.append((x, y, w, h))
                put(ops, (x, y, w, h), fn, *args)
            add_edit(content_key(dds), dds, name, ops, img)


def by_name(items, suffix):
    return [(n, d) for n, d in items if n.endswith(suffix)]


def box_of(img, thresh=12):
    a = np.asarray(img)[..., 3]
    ys, xs = np.nonzero(a > thresh)
    return int(xs.min()), int(ys.min()), int(xs.max() - xs.min() + 1), int(ys.max() - ys.min() + 1)


def create_a_superstar(items):
    for name, dds in by_name(items, "/CAS_button01"):
        img = decode(dds)
        put(ops := [], box_of(img), G.key, "R1")
        add_edit(content_key(dds), dds, name, ops, img)
    for name, dds in by_name(items, "/CAS_button02"):
        img = decode(dds)
        put(ops := [], box_of(img), G.face, "square", ring=True)
        add_edit(content_key(dds), dds, name, ops, img)
    for name, dds in by_name(items, "/CAS_cont01"):
        img = decode(dds)
        put(ops := [], box_of(img), G.dpad, ring=True)
        add_edit(content_key(dds), dds, name, ops, img)


def cropped(img):
    """The image cut to what's visible, and where the cut starts."""
    a = np.asarray(img)[..., 3]
    ys, xs = np.nonzero(a)
    x0, y0, x1, y1 = int(xs.min()), int(ys.min()), int(xs.max()) + 1, int(ys.max()) + 1
    return img.crop((x0, y0, x1, y1)), x0, y0


def raw_glyph(img, mask=False):
    gid = len(B.glyphs)
    B.glyphs.append(img)
    if mask:
        B.masks.add(gid)
    return gid


def controllers(items):
    import dualsense as DS
    path = DUALSENSE
    if not os.path.isfile(path):
        print(f"  no {path}: the controller pictures stay the game's")
        return
    ds = DS.load(path)
    # the select screens' controller icons: the game draws them 84x64 from 128x128, so the
    # DualSense is drawn 109x100 here to come out its own shape
    box = (10, 0, 109, 100)
    for suffix, sil in (("/contro_01", False), ("/contro_02", True)):
        for name, dds in by_name(items, suffix):
            key = content_key(dds)
            if key in B.edits:
                continue
            img = decode(dds)
            gid = raw_glyph(DS.icon(ds, img.width, img.height, box, silhouette=sil))
            add_edit(key, dds, name, [("clear", 0, 0, img.width, img.height), ("paste", gid, 0, 0)], img)
    for name, dds in by_name(items, "/demoTips"):
        key = content_key(dds)
        if key in B.edits:
            continue
        img = decode(dds)
        mask, mx, my = cropped(DS.card_mask(img))
        over, ox, oy = cropped(DS.card_overlay(ds))
        ops = DS.card_ops(img, raw_glyph(mask, True), raw_glyph(over))
        ops[0] = ("fill", ops[0][1], mx, my)
        ops[1] = ("paste", ops[1][1], ox, oy)
        add_edit(key, dds, name, ops, img)


# ---------------------------------------------------------------- output

KINDS = {"clear": 0, "paste": 1, "fill": 2, "copy": 3}
FMTS = {"DXT1": 1, "DXT3": 3, "DXT5": 5}


def write(path):
    """ps_buttons.bin, little-endian:
         "SVRPAD01", u32 glyphs, u32 edits, u32 ops, u32 names bytes
         glyphs: u16 w, u16 h, u8 format (0 RGBA8, 1 A8), u8[3], u32 data offset
         edits:  u64 key, u16 w, u16 h, u8 format (1/3/5 = DXT1/3/5), u8, u16 op count,
                 u32 first op, u32 name offset
         ops:    u8 kind (0 clear, 1 paste, 2 fill, 3 copy), u8 flags (copy: 1 = lettering only),
                 u16 glyph, i16 x, y, w, h, sx, sy  (copy: x, y, w, h = source; sx, sy = target)
         names, then the glyph pixels."""
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    pixels = bytearray()
    gtab = bytearray()
    for i, g in enumerate(B.glyphs):
        a = np.asarray(g.convert("RGBA"))
        if i in B.masks:
            data, fmt = a[..., 3].tobytes(), 1
        else:
            data, fmt = a.tobytes(), 0
        gtab += struct.pack("<HHB3xI", g.width, g.height, fmt, len(pixels))
        pixels += data
    etab, otab, names = bytearray(), bytearray(), bytearray()
    nops = 0
    for e in B.edits.values():
        etab += struct.pack("<QHHBxHII", e.key, e.w, e.h, FMTS[e.fmt], len(e.ops), nops, len(names))
        names += e.name.encode() + b"\0"
        for op in e.ops:
            k = op[0]
            if k == "clear":
                rec = (0, 0, 0, *op[1:5], 0, 0)
            elif k == "paste":
                rec = (1, 0, op[1], op[2], op[3], 0, 0, 0, 0)
            elif k == "fill":
                rec = (2, 0, op[1], op[2], op[3], 0, 0, 0, 0)
            else:
                rec = (3, int(op[7]) if len(op) > 7 else 0, 0, *op[1:7])
            otab += struct.pack("<BBH6h", *rec)
            nops += 1
    head = b"SVRPAD01" + struct.pack("<IIII", len(B.glyphs), len(B.edits), nops, len(names))
    with open(path, "wb") as f:
        f.write(head + gtab + etab + otab + names + pixels)
    print(f"{path}: {len(B.edits)} textures, {len(B.glyphs)} pictures, {nops} edits, "
          f"{os.path.getsize(path) // 1024} KiB")


def previews(folder):
    """Before / after of every edited texture, for checking by eye."""
    os.makedirs(folder, exist_ok=True)
    for name, before, after in B.previews:
        S = 3 if before.width <= 256 else 1
        W, H = before.width * S, before.height * S
        c = Image.new("RGBA", (W * 2 + 12, H), (58, 58, 76, 255))
        c.alpha_composite(before.resize((W, H), Image.LANCZOS), (0, 0))
        c.alpha_composite(after.resize((W, H), Image.LANCZOS), (W + 12, 0))
        safe = name.replace(":", "_").replace("/", "_")
        c.convert("RGB").save(os.path.join(folder, safe + ".png"))


def main():
    if not GAME:
        sys.exit(__doc__)
    items = []
    for rel in PACS:
        path = os.path.join(GAME, "pac", rel)
        got = list(textures(path))
        print(f"{rel}: {len(got)} textures")
        if rel.startswith("menu/fontHD") or rel.startswith("menu/MatchHD") or rel.startswith("menu/LoadHD"):
            font_pages(path, rel, got)
        items += got
    prompts(items)
    create_a_superstar(items)
    controllers(items)
    write(os.path.join(OUT, "ps_buttons.bin"))
    previews(os.path.join(OUT, "preview"))


if __name__ == "__main__":
    main()
