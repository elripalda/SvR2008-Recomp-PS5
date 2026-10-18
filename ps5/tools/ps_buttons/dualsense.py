"""The DualSense pictures: the controller icons on the superstar select screens (contro_01 /
contro_02) and the loading screen's controls card (demoTips), from a front picture of the
DualSense (art/local/ps_buttons/dualsense.png: transparent background)."""
import math

import cairo
import numpy as np
from PIL import Image, ImageFilter
from scipy import ndimage

import glyphs as G

# Where things are on the DualSense picture, cropped to the controller (2288 x 1601).
CROP = (777, 320, 3065, 1921)
PARTS = {
    "triangle": (1856, 270), "square": (1686, 436), "circle": (2022, 436), "cross": (1856, 604),
    "dpad": (430, 436), "dpad_up": (430, 330), "dpad_left": (318, 436),
    "lstick": (780, 750), "rstick": (1510, 750),
}
BUTTON_R = 74
STICK_R = 140


def load(path):
    """The controller, without the soft shadow the picture has under it."""
    im = Image.open(path).convert("RGBA").crop(CROP)
    a = np.asarray(im).astype(np.float32)
    al = a[..., 3] / 255.0
    a[..., 3] = np.clip((al - 0.40) / 0.45, 0, 1) * 255
    return Image.fromarray(a.astype(np.uint8), "RGBA")


def _premul_resize(im, size):
    """Resize without dark fringes: work on premultiplied colour."""
    a = np.asarray(im).astype(np.float32) / 255.0
    pm = np.concatenate([a[..., :3] * a[..., 3:4], a[..., 3:4]], axis=2)
    pim = Image.fromarray((pm * 255 + 0.5).astype(np.uint8), "RGBA")
    r = np.asarray(pim.resize(size, Image.LANCZOS)).astype(np.float32) / 255.0
    al = r[..., 3:4]
    rgb = np.where(al > 1e-3, r[..., :3] / np.maximum(al, 1e-3), 0)
    return Image.fromarray((np.concatenate([rgb, al], axis=2) * 255 + 0.5).clip(0, 255).astype(np.uint8), "RGBA")


def _outline(alpha, px):
    """Alpha grown by px (round)."""
    r = int(math.ceil(px))
    yy, xx = np.mgrid[-r:r + 1, -r:r + 1]
    k = ((xx * xx + yy * yy) <= px * px + 0.5).astype(np.float32)
    return ndimage.grey_dilation(alpha, footprint=k > 0)


def icon(ds, w, h, box, silhouette=False):
    """The 128x128 select-screen icon: the DualSense in box (x, y, w, h) with a white edge like the
    game's. The game draws this texture 84x64, so it's squeezed across to come out right."""
    x, y, bw, bh = box
    inner = (bw - 4, bh - 4)
    pad = _premul_resize(ds, inner)
    a = np.asarray(pad).astype(np.float32)
    if silhouette:
        a[..., :3] = 8
    canvas = np.zeros((h, w, 4), np.float32)
    canvas[y + 2:y + 2 + inner[1], x + 2:x + 2 + inner[0]] = a
    al = canvas[..., 3] / 255.0
    edge = _outline(al, 2.0)
    edge = np.asarray(Image.fromarray((edge * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(0.4))) / 255.0
    out = np.zeros((h, w, 4), np.float32)
    out[..., :3] = 255
    out[..., 3] = edge * 255
    # the controller over its white edge
    fa = al[..., None]
    out[..., :3] = canvas[..., :3] * fa + out[..., :3] * (1 - fa)
    out[..., 3] = np.maximum(out[..., 3], canvas[..., 3])
    return Image.fromarray(out.clip(0, 255).astype(np.uint8), "RGBA")


# ---------------------------------------------------------------- the controls card

# The card (1024x512) as the game has it: where the labels are (text only), and the pictures
# that go. Coordinates in the card.
LABELS = {  # text box: x0, y0, x1, y1
    "strike": (655, 105, 719, 124), "finisher": (686, 150, 762, 170), "whip": (745, 201, 845, 223),
    "ring": (705, 241, 872, 263), "grapple": (695, 318, 829, 340),
    "move": (271, 122, 319, 142), "taunt": (247, 325, 301, 344),
}
KEEP_TEXT = [(310, 172, 372, 201), (340, 410, 690, 440)]  # "- Run", "Hold ... - Strong Grapple"
OLD_ICONS = {  # x, y, w, h (with a margin)
    "lt": (456, 57, 33, 77), "rt": (551, 57, 33, 77),
    "lb": (188, 170, 62, 31), "ls": (262, 159, 48, 52), "rb": (390, 408, 62, 31), "rs": (468, 397, 48, 52),
}
CONTROLLER_AT = (376, 176, 300)  # left, top, width of the DualSense on the card


def card_mask(card):
    """What comes off the card: the Xbox controller, its lines and underlines, the Xbox button
    pictures, and the two labels that swap places. From the card itself (bright, grey, connected
    to the controller), so only the picture's shape is kept."""
    a = np.asarray(card.convert("RGB")).astype(int)
    H, W = a.shape[:2]
    luma = (a[..., 0] * 299 + a[..., 1] * 587 + a[..., 2] * 114) / 1000
    sat = a.max(2) - a.min(2)
    bright = (luma > 150) & (sat < 70)
    lab, _ = ndimage.label(bright)
    comp = lab == lab[280, 520]
    filled = ndimage.binary_fill_holes(comp)
    # the controller without its thin lines, and its outline hull (takes in the dark rim)
    yy, xx = np.mgrid[-4:5, -4:5]
    body = ndimage.binary_opening(filled, structure=(xx * xx + yy * yy) <= 16)
    blab, n = ndimage.label(body)
    sizes = ndimage.sum(body, blab, range(1, n + 1))
    body = blab == (int(np.argmax(sizes)) + 1)
    # the controller's dark rim runs along the bottom of its grips: take the body grown by a
    # few pixels, more downwards
    hull_m = ndimage.binary_dilation(body, iterations=4)
    hull_m |= np.roll(ndimage.binary_dilation(body, iterations=4), 8, axis=0)
    lines = ndimage.binary_dilation(filled, iterations=4)
    lines |= np.roll(np.roll(lines, 2, axis=0), 2, axis=1)  # their drop shadow
    m = hull_m | lines
    # every other bit of lettering stays (lines and the controller's edge run close to it)
    inside = ndimage.binary_dilation(filled, iterations=2)
    text = ndimage.binary_dilation(bright & ~inside, iterations=1)
    for x0, y0, x1, y1 in KEEP_TEXT:
        box = np.zeros_like(m)
        box[y0:y1, x0:x1] = True
        text |= ndimage.binary_dilation(bright & box, iterations=1)
    m &= ~text
    for k, (x0, y0, x1, y1) in LABELS.items():
        if k in ("move", "taunt"):
            m[y0 - 2:y1 + 3, x0 - 2:x1 + 3] = True
    for x, y, w, h in OLD_ICONS.values():
        m[y:y + h, x:x + w] = True
    out = np.zeros((H, W, 4), np.uint8)
    out[..., 3] = m * 255
    return Image.fromarray(out, "RGBA")


def _at(px, py):
    x0, y0, w = CONTROLLER_AT
    s = w / (CROP[2] - CROP[0])
    return x0 + px * s, y0 + py * s, s


def card_overlay(ds):
    """Everything drawn on the card: the DualSense (with a soft shadow), the lines from each
    label to its button, and the PlayStation button pictures."""
    W, H = 1024, 512
    x0, y0, w = CONTROLLER_AT
    s = w / ds.width
    h = int(round(ds.height * s))
    pad = _premul_resize(ds, (w, h))
    layer = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    # shadow
    sh = np.zeros((H, W), np.float32)
    al = np.asarray(pad)[..., 3].astype(np.float32)
    sh[y0 + 5:y0 + 5 + h, x0:x0 + w] = al
    sh = np.asarray(Image.fromarray(sh.astype(np.uint8)).filter(ImageFilter.GaussianBlur(7))).astype(np.float32)
    shadow = np.zeros((H, W, 4), np.uint8)
    shadow[..., 3] = (sh * 0.55).astype(np.uint8)
    layer = Image.alpha_composite(layer, Image.fromarray(shadow, "RGBA"))
    layer.alpha_composite(pad, (x0, y0))

    surf = cairo.ImageSurface(cairo.FORMAT_ARGB32, W, H)
    cr = cairo.Context(surf)

    def stroke(path):
        for width, col in ((4.2, (0, 0, 0, 0.55)), (2.0, (1, 1, 1, 1))):
            cr.new_path()
            cr.move_to(*path[0])
            for q in path[1:]:
                cr.line_to(*q)
            cr.set_line_cap(cairo.LINE_CAP_ROUND)
            cr.set_line_join(cairo.LINE_JOIN_ROUND)
            cr.set_source_rgba(*col)
            cr.set_line_width(width)
            cr.stroke()
        ex, ey = path[-1]
        cr.new_path(); cr.arc(ex, ey, 3.2, 0, 2 * math.pi); cr.set_source_rgba(0, 0, 0, 0.6); cr.fill()
        cr.new_path(); cr.arc(ex, ey, 2.2, 0, 2 * math.pi); cr.set_source_rgb(1, 1, 1); cr.fill()

    def toward(part, radius, fx, fy):
        """The edge of a button, on the side facing (fx, fy)."""
        cx, cy, sc = _at(*PARTS[part])
        r = radius * sc + 1.5
        d = math.hypot(fx - cx, fy - cy)
        return cx + (fx - cx) / d * r, cy + (fy - cy) / d * r

    def label_line(box, via, part, radius, right_side):
        """Underline the label, then run from its end nearest the controller to the button."""
        x0, y0, x1, y1 = box
        uy = y1 + 4.5
        near, far = ((x0 - 3, uy), (x1 + 3, uy)) if right_side else ((x1 + 3, uy), (x0 - 3, uy))
        aim = via[-1] if via else near
        stroke([far, near] + via + [toward(part, radius, *aim)])

    label_line(LABELS["strike"], [(_at(*PARTS["square"])[0], 180)], "square", BUTTON_R, True)
    label_line(LABELS["finisher"], [], "triangle", BUTTON_R, True)
    label_line(LABELS["whip"], [], "circle", BUTTON_R, True)
    label_line(LABELS["ring"], [], "cross", BUTTON_R, True)
    label_line(LABELS["grapple"], [], "rstick", STICK_R * 0.9, True)
    # left side: Taunt moves up (where Move was) for the d-pad, Move down for the stick
    taunt_box, move_box = swapped_labels()
    # along past "- Run", then down to the d-pad
    label_line(taunt_box, [(392, taunt_box[3] + 4.5)], "dpad_left", 70, False)
    label_line(move_box, [], "lstick", STICK_R * 0.9, False)
    lines_img = G.to_image(surf)
    layer = Image.alpha_composite(layer, lines_img)

    # button pictures where the Xbox ones were
    def put(img, x, y):
        layer.alpha_composite(img, (x, y))
    put(G.card_trigger(26, 70, "L2"), 459, 60)
    put(G.card_trigger(26, 70, "R2"), 555, 60)
    put(G.card_bumper(56, 25, "L1"), 191, 173)
    put(G.stick(40, 40, "L"), 266, 163)
    put(G.card_bumper(56, 25, "R1"), 393, 411)
    put(G.stick(40, 40, "R"), 472, 401)
    return layer


def swapped_labels():
    """Taunt's text placed where Move's was and Move's where Taunt's was (left-aligned)."""
    mv, ta = LABELS["move"], LABELS["taunt"]
    taunt = (mv[0], mv[1], mv[0] + ta[2] - ta[0], mv[1] + ta[3] - ta[1])
    move = (ta[0], ta[1], ta[0] + mv[2] - mv[0], ta[1] + mv[3] - mv[1])
    return taunt, move


def card_ops(card, mask_gid, overlay_gid):
    """fill the old pictures, draw the new, and move the two labels."""
    mv, ta = LABELS["move"], LABELS["taunt"]
    return [
        ("fill", mask_gid, 0, 0),
        ("paste", overlay_gid, 0, 0),
        # Taunt's text goes up where Move was, Move's down where Taunt was (left-aligned)
        ("copy", ta[0] - 2, ta[1] - 2, ta[2] - ta[0] + 5, ta[3] - ta[1] + 5, mv[0] - 2, mv[1] - 2, 1),
        ("copy", mv[0] - 2, mv[1] - 2, mv[2] - mv[0] + 5, mv[3] - mv[1] + 5, ta[0] - 2, ta[1] - 2, 1),
    ]
