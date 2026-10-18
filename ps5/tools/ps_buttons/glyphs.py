"""PlayStation button pictures for SvR 2008, drawn to sit where the game's Xbox ones were.

Every glyph is drawn into a w x h box (the box the Xbox picture filled). The game's button
pictures are stored squeezed (a round button is narrower than tall in the texture and is
stretched back when drawn), so each glyph is drawn round in a unit space of height h and
scaled across to w.
"""
import math

import cairo
import numpy as np
from PIL import Image

FONT = "Anton"

# Symbol colours, bright enough for a dark button.
TRIANGLE = (0.25, 0.86, 0.66)
CIRCLE = (1.00, 0.36, 0.40)
CROSS = (0.45, 0.66, 1.00)
SQUARE = (0.96, 0.55, 0.86)
RED = (0.90, 0.16, 0.20)


def _surface(w, h):
    s = cairo.ImageSurface(cairo.FORMAT_ARGB32, w, h)
    return s, cairo.Context(s)


def to_image(s):
    """cairo ARGB32 (premultiplied BGRA) -> straight RGBA PIL image."""
    s.flush()
    w, h = s.get_width(), s.get_height()
    a = np.ndarray((h, s.get_stride() // 4, 4), np.uint8, s.get_data())[:, :w].astype(np.float32)
    al = a[:, :, 3:4]
    rgb = np.where(al > 0, a[:, :, 2::-1] * 255.0 / np.maximum(al, 1), 0)
    out = np.concatenate([np.clip(rgb + 0.5, 0, 255), al], axis=2).astype(np.uint8)
    return Image.fromarray(out, "RGBA")


def _squeeze(cr, w, h):
    """Unit space: a square of side h, scaled across to w."""
    cr.scale(w / h, 1.0)
    return float(h)


def _circle(cr, cx, cy, r):
    cr.new_path()
    cr.arc(cx, cy, r, 0, 2 * math.pi)
    cr.close_path()


def _vgrad(y0, y1, stops):
    g = cairo.LinearGradient(0, y0, 0, y1)
    for t, c in stops:
        g.add_color_stop_rgba(t, *c) if len(c) == 4 else g.add_color_stop_rgb(t, *c)
    return g


def _text_path(cr, text, cx, cy, height, font=FONT, squeeze=1.0, weight=cairo.FONT_WEIGHT_NORMAL):
    """Path of `text` centred on (cx, cy), cap height ~ `height`."""
    cr.save()
    cr.select_font_face(font, cairo.FONT_SLANT_NORMAL, weight)
    cr.set_font_size(100)
    ext = cr.text_extents("H")
    size = 100 * height / ext.height
    cr.set_font_size(size)
    e = cr.text_extents(text)
    cr.translate(cx, cy)
    cr.scale(squeeze, 1.0)
    cr.move_to(-e.x_bearing - e.width / 2, -e.y_bearing - e.height / 2)
    cr.text_path(text)
    cr.restore()


def _outlined_text(cr, text, cx, cy, height, fill=(1, 1, 1), outline=(0, 0, 0), stroke=0.16,
                   squeeze=1.0, shade=True):
    """White lettering with a black outline (the game's button lettering)."""
    lw = height * stroke
    _text_path(cr, text, cx, cy, height, squeeze=squeeze)
    cr.set_line_join(cairo.LINE_JOIN_ROUND)
    cr.set_source_rgb(*outline)
    cr.set_line_width(lw * 2)
    cr.stroke_preserve()
    if shade:
        cr.set_source(_vgrad(cy - height / 2, cy + height / 2,
                             [(0, fill), (0.55, fill), (1, tuple(c * 0.78 for c in fill))]))
    else:
        cr.set_source_rgb(*fill)
    cr.fill()


# ---------------------------------------------------------------- face buttons

def _symbol(cr, kind, cx, cy, r, color):
    """The PlayStation symbol, outlined in near black so it reads on any background."""
    lw = r * 0.25
    cr.new_path()
    if kind == "triangle":
        rr = r * 0.66
        pts = [(cx + rr * math.cos(a), cy + 0.10 * r + rr * math.sin(a))
               for a in (-math.pi / 2, math.pi / 6, 5 * math.pi / 6)]
        cr.move_to(*pts[0])
        for p in pts[1:]:
            cr.line_to(*p)
        cr.close_path()
    elif kind == "circle":
        cr.arc(cx, cy, r * 0.52, 0, 2 * math.pi)
        cr.close_path()
    elif kind == "cross":
        d = r * 0.44
        cr.move_to(cx - d, cy - d); cr.line_to(cx + d, cy + d)
        cr.move_to(cx + d, cy - d); cr.line_to(cx - d, cy + d)
    elif kind == "square":
        d = r * 0.43
        cr.rectangle(cx - d, cy - d, 2 * d, 2 * d)
    cr.set_line_join(cairo.LINE_JOIN_MITER if kind != "cross" else cairo.LINE_JOIN_ROUND)
    cr.set_line_cap(cairo.LINE_CAP_ROUND if kind == "cross" else cairo.LINE_CAP_BUTT)
    path = cr.copy_path()
    cr.set_source_rgba(0, 0, 0, 0.9)
    cr.set_line_width(lw + r * 0.14)
    cr.stroke()
    cr.append_path(path)
    cr.set_source(_vgrad(cy - r * 0.6, cy + r * 0.6,
                         [(0, tuple(min(1, c * 1.10 + 0.08) for c in color)), (1, tuple(c * 0.82 for c in color))]))
    cr.set_line_width(lw)
    cr.stroke()


def face(w, h, kind, ring=False):
    """A dark round button with the coloured symbol (Cross, Circle, Square, Triangle)."""
    color = {"triangle": TRIANGLE, "circle": CIRCLE, "cross": CROSS, "square": SQUARE}[kind]
    s, cr = _surface(w, h)
    H = _squeeze(cr, w, h)
    cx = cy = H / 2
    r = H / 2 - 0.5
    if ring:  # Create-a-Superstar's buttons sit in a white ring
        _circle(cr, cx, cy, r)
        cr.set_source_rgb(1, 1, 1)
        cr.fill()
        r -= H * 0.06
    _circle(cr, cx, cy, r)
    cr.set_source_rgb(0.02, 0.02, 0.03)
    cr.fill()
    body = r - max(1.2, H * 0.045)
    _circle(cr, cx, cy, body)
    g = cairo.RadialGradient(cx, cy - body * 0.45, body * 0.1, cx, cy, body * 1.05)
    g.add_color_stop_rgb(0, 0.36, 0.36, 0.40)
    g.add_color_stop_rgb(0.65, 0.16, 0.16, 0.19)
    g.add_color_stop_rgb(1, 0.07, 0.07, 0.09)
    cr.set_source(g)
    cr.fill()
    # a faint coloured rim, like the lit edge of the Xbox buttons
    _circle(cr, cx, cy, body - H * 0.02)
    cr.set_source_rgba(*color, 0.55)
    cr.set_line_width(max(0.9, H * 0.035))
    cr.stroke()
    _symbol(cr, kind, cx, cy + body * 0.02, body * 0.98, color)
    # gloss
    cr.save()
    cr.new_path()
    cr.scale(1, 0.62)
    cr.arc(cx, (cy - body * 0.42) / 0.62, body * 0.70, 0, 2 * math.pi)
    cr.restore()
    cr.set_source(_vgrad(cy - body, cy - body * 0.05, [(0, (1, 1, 1, 0.42)), (1, (1, 1, 1, 0.0))]))
    cr.fill()
    return to_image(s)


# ---------------------------------------------------------------- sticks

def stick(w, h, letter):
    """Analog stick top: dark centre in a light ring, with L or R."""
    s, cr = _surface(w, h)
    H = _squeeze(cr, w, h)
    cx = cy = H / 2
    r = H / 2 - 0.5
    _circle(cr, cx, cy, r)
    cr.set_source(_vgrad(0, H, [(0, (0.86, 0.86, 0.88)), (1, (0.50, 0.50, 0.53))]))
    cr.fill()
    inner = r - max(1.4, H * 0.07)
    _circle(cr, cx, cy, inner)
    cr.set_source_rgb(0.05, 0.05, 0.06)
    cr.fill()
    body = inner - max(0.8, H * 0.025)
    _circle(cr, cx, cy, body)
    g = cairo.RadialGradient(cx, cy + body * 0.35, body * 0.15, cx, cy, body)
    g.add_color_stop_rgb(0, 0.30, 0.30, 0.33)
    g.add_color_stop_rgb(1, 0.12, 0.12, 0.14)
    cr.set_source(g)
    cr.fill()
    # the concave top's rim highlight
    cr.new_path()
    cr.arc(cx, cy, body * 0.86, math.pi * 1.1, math.pi * 1.9)
    cr.set_source_rgba(1, 1, 1, 0.22)
    cr.set_line_width(max(0.8, H * 0.03))
    cr.stroke()
    _outlined_text(cr, letter, cx, cy + H * 0.01, H * 0.50, stroke=0.15)
    return to_image(s)


def stick_click(w, h, label, box=None):
    """Pressing a stick: the stick from the side with an arrow pushing down, and L3 / R3."""
    s, cr = _surface(w, h)
    sx = w / h
    H = float(h)
    cr.scale(sx, 1)
    cx = H / 2 * (w / sx / (H)) if False else (w / sx) / 2
    # stick cap (an ellipse seen from the side) and its skirt
    top, rx, ry = H * 0.58, H * 0.46, H * 0.16
    for k, (y, col) in enumerate([(top + H * 0.14, (0.18, 0.18, 0.20)), (top, (0.40, 0.40, 0.43))]):
        cr.save(); cr.translate(cx, y); cr.scale(rx, ry); cr.arc(0, 0, 1, 0, 2 * math.pi); cr.restore()
        if k == 0:
            cr.rectangle(cx - rx, top, 2 * rx, H * 0.14)
        cr.set_source_rgb(0.02, 0.02, 0.03)
        cr.set_line_width(max(1.0, H * 0.04))
        cr.stroke_preserve()
        cr.set_source_rgb(*col)
        cr.fill()
    cr.save(); cr.translate(cx, top); cr.scale(rx * 0.72, ry * 0.62); cr.arc(0, 0, 1, 0, 2 * math.pi); cr.restore()
    cr.set_source_rgb(0.24, 0.24, 0.27)
    cr.fill()
    # arrow
    aw, ah = H * 0.30, H * 0.40
    ay = H * 0.02
    cr.new_path()
    cr.move_to(cx - aw * 0.28, ay); cr.line_to(cx + aw * 0.28, ay)
    cr.line_to(cx + aw * 0.28, ay + ah * 0.48); cr.line_to(cx + aw * 0.62, ay + ah * 0.48)
    cr.line_to(cx, ay + ah); cr.line_to(cx - aw * 0.62, ay + ah * 0.48)
    cr.line_to(cx - aw * 0.28, ay + ah * 0.48); cr.close_path()
    cr.set_line_join(cairo.LINE_JOIN_ROUND)
    cr.set_source_rgb(0, 0, 0); cr.set_line_width(max(1.2, H * 0.06)); cr.stroke_preserve()
    cr.set_source_rgb(1, 1, 1); cr.fill()
    _outlined_text(cr, label, cx, top + H * 0.10, H * 0.30, stroke=0.18)
    return to_image(s)


# ---------------------------------------------------------------- d-pad

def dpad(w, h, lit=(), corners=(), base=True, scale=1.0, ring=False):
    """The PlayStation d-pad: four separate keys, lit ones in the game's red. `corners` marks
    diagonals ('ul', 'ur', 'dl', 'dr') with red triangles outside the keys, as the game does."""
    s, cr = _surface(w, h)
    H = _squeeze(cr, w, h)
    cx = cy = H / 2
    r = H / 2 - 0.5
    if ring:
        _circle(cr, cx, cy, r)
        cr.set_source_rgb(1, 1, 1)
        cr.fill()
        r -= H * 0.035
    if base:
        _circle(cr, cx, cy, r)
        cr.set_source_rgb(0.02, 0.02, 0.03)
        cr.fill()
        _circle(cr, cx, cy, r - max(1.0, H * 0.04))
        g = cairo.RadialGradient(cx, cy - r * 0.4, r * 0.1, cx, cy, r)
        g.add_color_stop_rgb(0, 0.34, 0.34, 0.37)
        g.add_color_stop_rgb(1, 0.14, 0.14, 0.16)
        cr.set_source(g)
        cr.fill()
    k = r * 0.80 * scale        # key reach from the centre
    kw = r * 0.27 * scale       # half key width
    gap = r * 0.10 * scale      # centre gap
    for d, ang in (("up", -90), ("right", 0), ("down", 90), ("left", 180)):
        cr.save()
        cr.translate(cx, cy)
        cr.rotate(math.radians(ang + 90))  # draw "up" and turn it
        cr.new_path()
        cr.move_to(0, -gap)
        cr.line_to(kw, -gap - kw * 0.9)
        cr.line_to(kw, -k + kw * 0.35)
        cr.curve_to(kw, -k, kw * 0.6, -k, 0, -k)
        cr.curve_to(-kw * 0.6, -k, -kw, -k, -kw, -k + kw * 0.35)
        cr.line_to(-kw, -gap - kw * 0.9)
        cr.close_path()
        on = d in lit
        cr.set_line_join(cairo.LINE_JOIN_ROUND)
        cr.set_source_rgb(0.02, 0.02, 0.03)
        cr.set_line_width(max(1.0, H * 0.035))
        cr.stroke_preserve()
        top = (1.0, 0.30, 0.32) if on else (0.86, 0.86, 0.89)
        bot = (0.70, 0.08, 0.12) if on else (0.55, 0.55, 0.59)
        cr.set_source(_vgrad(-k, -gap, [(0, top), (1, bot)]))
        cr.fill()
        # the little arrow printed on each key
        t = kw * 0.42
        ty = -k + kw * 0.95
        cr.new_path()
        cr.move_to(0, ty - t * 0.8); cr.line_to(t, ty + t * 0.4); cr.line_to(-t, ty + t * 0.4); cr.close_path()
        cr.set_source_rgba(1, 1, 1, 0.95) if on else cr.set_source_rgba(0.25, 0.25, 0.28, 0.9)
        cr.fill()
        cr.restore()
    for c in corners:
        ax = {"ul": -1, "ur": 1, "dl": -1, "dr": 1}[c]
        ay = {"ul": -1, "ur": -1, "dl": 1, "dr": 1}[c]
        px, py = cx + ax * r * 0.70, cy + ay * r * 0.70
        a = math.atan2(ay, ax)
        t = r * 0.26
        cr.new_path()
        cr.move_to(px + math.cos(a) * t, py + math.sin(a) * t)
        cr.line_to(px + math.cos(a + 2.2) * t * 0.8, py + math.sin(a + 2.2) * t * 0.8)
        cr.line_to(px + math.cos(a - 2.2) * t * 0.8, py + math.sin(a - 2.2) * t * 0.8)
        cr.close_path()
        cr.set_source_rgb(0, 0, 0); cr.set_line_width(max(1.0, H * 0.035)); cr.stroke_preserve()
        cr.set_source_rgb(*RED); cr.fill()
    return to_image(s)


# ---------------------------------------------------------------- shoulders, triggers

def _rounded_rect(cr, x, y, w, h, r):
    r = min(r, w / 2, h / 2)
    cr.new_path()
    cr.arc(x + w - r, y + r, r, -math.pi / 2, 0)
    cr.arc(x + w - r, y + h - r, r, 0, math.pi / 2)
    cr.arc(x + r, y + h - r, r, math.pi / 2, math.pi)
    cr.arc(x + r, y + r, r, math.pi, 1.5 * math.pi)
    cr.close_path()


def _silver(cr, y0, y1):
    return _vgrad(y0, y1, [(0, (0.97, 0.97, 0.98)), (0.5, (0.82, 0.82, 0.85)), (1, (0.55, 0.55, 0.59))])


def bumper(w, h, label, box=None):
    """L1 / R1: a bumper bar behind big outlined lettering (the game's LB / RB style)."""
    s, cr = _surface(w, h)
    W, H = float(w), float(h)
    bh = H * 0.42
    _rounded_rect(cr, 1, (H - bh) / 2, W - 2, bh, bh * 0.35)
    cr.set_source_rgb(0.02, 0.02, 0.03)
    cr.set_line_width(max(1.0, H * 0.05))
    cr.stroke_preserve()
    cr.set_source(_silver(cr, (H - bh) / 2, (H + bh) / 2))
    cr.fill()
    _outlined_text(cr, label, W / 2, H / 2, H * 0.80, stroke=0.12, squeeze=1.0)
    return to_image(s)


def trigger(w, h, label):
    """L2 / R2: big outlined lettering in front of a trigger (the game's LT / RT style)."""
    s, cr = _surface(w, h)
    W, H = float(w), float(h)
    tw = min(W * 0.62, H * 0.62)
    x0 = (W - tw) / 2
    th = H * 0.80
    cr.new_path()
    cr.move_to(x0, th)
    cr.line_to(x0, H * 0.20)
    cr.curve_to(x0, 1, x0 + tw, 1, x0 + tw, H * 0.20)
    cr.line_to(x0 + tw, th)
    cr.close_path()
    cr.set_line_join(cairo.LINE_JOIN_ROUND)
    cr.set_source_rgb(0.02, 0.02, 0.03)
    cr.set_line_width(max(1.0, H * 0.045))
    cr.stroke_preserve()
    cr.set_source(_silver(cr, 0, th))
    cr.fill()
    lh = H * 0.56
    _outlined_text(cr, label, W / 2, H - lh / 2 - H * 0.08, lh, stroke=0.13,
                   squeeze=min(1.0, (W * 0.86) / (lh * 1.05)))
    return to_image(s)


def key(w, h, label, ring=True):
    """Create-a-Superstar's big bumper key (R1): a silver key with dark lettering."""
    s, cr = _surface(w, h)
    W, H = float(w), float(h)
    m = H * 0.06
    if ring:
        _rounded_rect(cr, 0.5, 0.5, W - 1, H - 1, H * 0.16)
        cr.set_source_rgb(1, 1, 1)
        cr.fill()
    _rounded_rect(cr, m, m, W - 2 * m, H - 2 * m, H * 0.12)
    cr.set_source_rgb(0.15, 0.15, 0.17)
    cr.fill()
    _rounded_rect(cr, m + H * 0.05, m + H * 0.05, W - 2 * m - H * 0.10, H - 2 * m - H * 0.10, H * 0.09)
    cr.set_source(_silver(cr, m, H - m))
    cr.fill()
    _text_path(cr, label, W / 2, H / 2, H * 0.30, font="Oswald", weight=cairo.FONT_WEIGHT_BOLD)
    cr.set_source_rgb(0.08, 0.08, 0.09)
    cr.fill()
    return to_image(s)


# ---------------------------------------------------------------- touch pad, options, PS

def touchpad(w, h, box=None):
    """BACK is the touch pad press: the pad, silver."""
    s, cr = _surface(w, h)
    W, H = float(w), float(h)
    pw, ph = W * 0.92, H * 0.66
    x0, y0 = (W - pw) / 2, (H - ph) / 2
    cr.new_path()
    cr.move_to(x0, y0 + ph * 0.18)
    cr.curve_to(x0, y0, x0, y0, x0 + pw * 0.12, y0)
    cr.line_to(x0 + pw * 0.88, y0)
    cr.curve_to(x0 + pw, y0, x0 + pw, y0, x0 + pw, y0 + ph * 0.18)
    cr.line_to(x0 + pw * 0.93, y0 + ph * 0.82)
    cr.curve_to(x0 + pw * 0.92, y0 + ph, x0 + pw * 0.90, y0 + ph, x0 + pw * 0.80, y0 + ph)
    cr.line_to(x0 + pw * 0.20, y0 + ph)
    cr.curve_to(x0 + pw * 0.10, y0 + ph, x0 + pw * 0.08, y0 + ph, x0 + pw * 0.07, y0 + ph * 0.82)
    cr.close_path()
    cr.set_line_join(cairo.LINE_JOIN_ROUND)
    cr.set_source_rgb(0.02, 0.02, 0.03)
    cr.set_line_width(max(1.0, H * 0.06))
    cr.stroke_preserve()
    cr.set_source(_silver(cr, y0, y0 + ph))
    cr.fill()
    # the light bar strips either side
    for sx in (x0 + pw * 0.035, x0 + pw * 0.965):
        cr.new_path()
        cr.move_to(sx, y0 + ph * 0.25); cr.line_to(sx + (pw * 0.02 if sx > W / 2 else -pw * 0.02) * 0, y0 + ph * 0.70)
        cr.set_source_rgba(0.35, 0.50, 1.0, 0.9)
        cr.set_line_width(max(0.8, H * 0.035))
        cr.stroke()
    # a finger press in the middle
    tx, ty, tr = W / 2, y0 + ph * 0.48, ph * 0.20
    cr.new_path(); cr.arc(tx, ty, tr * 1.7, 0, 2 * math.pi)
    cr.set_source_rgba(0.25, 0.25, 0.30, 0.55); cr.set_line_width(max(0.8, H * 0.03)); cr.stroke()
    cr.new_path(); cr.arc(tx, ty, tr, 0, 2 * math.pi)
    cr.set_source_rgb(0.22, 0.22, 0.26); cr.fill()
    return to_image(s)


def options(w, h, box=None):
    """START is OPTIONS: the silver round button with the three lines."""
    s, cr = _surface(w, h)
    H = _squeeze(cr, w, h)
    cx = cy = H / 2
    r = H / 2 - 0.5
    _circle(cr, cx, cy, r)
    cr.set_source_rgb(0.30, 0.30, 0.33)
    cr.fill()
    _circle(cr, cx, cy, r - max(1.0, H * 0.06))
    cr.set_source(_silver(cr, cy - r, cy + r))
    cr.fill()
    lw = H * 0.075
    for i in (-1, 0, 1):
        y = cy + i * H * 0.15
        cr.new_path()
        cr.move_to(cx - H * 0.20, y); cr.line_to(cx + H * 0.20, y)
        cr.set_line_cap(cairo.LINE_CAP_ROUND)
        cr.set_source_rgb(0.22, 0.22, 0.25)
        cr.set_line_width(lw)
        cr.stroke()
    return to_image(s)


def ps_home(w, h):
    """The Xbox Guide button's slot: a dark round button marked PS."""
    s, cr = _surface(w, h)
    H = _squeeze(cr, w, h)
    cx = cy = H / 2
    r = H / 2 - 0.5
    _circle(cr, cx, cy, r)
    cr.set_source_rgb(0.02, 0.02, 0.03)
    cr.fill()
    _circle(cr, cx, cy, r - max(1.0, H * 0.05))
    cr.set_source(_vgrad(0, H, [(0, (0.38, 0.38, 0.42)), (1, (0.10, 0.10, 0.12))]))
    cr.fill()
    _outlined_text(cr, "PS", cx, cy, H * 0.38, stroke=0.12)
    return to_image(s)


def ring_stick(w, h, letter):
    """Only the stick in a direction picture (the arrows around it stay the game's own)."""
    return stick(w, h, letter)


# ---------------------------------------------------------------- the loading screen's controls card

def card_trigger(w, h, label):
    """L2 / R2 on the controls card: a tall silver trigger, dark lettering at the bottom."""
    s, cr = _surface(w, h)
    W, H = float(w), float(h)
    _rounded_rect(cr, 1, 1, W - 2, H - 2, W * 0.28)
    cr.set_source_rgb(0.10, 0.10, 0.12)
    cr.fill()
    _rounded_rect(cr, 2.5, 2.5, W - 5, H - 5, W * 0.24)
    cr.set_source(_vgrad(0, H, [(0, (0.98, 0.98, 0.99)), (0.55, (0.80, 0.80, 0.83)), (1, (0.62, 0.62, 0.66))]))
    cr.fill()
    _text_path(cr, label, W / 2, H - W * 0.62, W * 0.48, font="Oswald", weight=cairo.FONT_WEIGHT_BOLD)
    cr.set_source_rgb(1, 1, 1)
    cr.set_line_width(W * 0.12)
    cr.set_line_join(cairo.LINE_JOIN_ROUND)
    cr.stroke_preserve()
    cr.set_source_rgb(0.05, 0.05, 0.06)
    cr.fill()
    return to_image(s)


def card_bumper(w, h, label):
    """L1 / R1 on the controls card: a silver bumper key with dark lettering."""
    s, cr = _surface(w, h)
    W, H = float(w), float(h)
    _rounded_rect(cr, 1, 1, W - 2, H - 2, H * 0.30)
    cr.set_source_rgb(0.10, 0.10, 0.12)
    cr.fill()
    _rounded_rect(cr, 2.5, 2.5, W - 5, H - 5, H * 0.26)
    cr.set_source(_vgrad(0, H, [(0, (0.99, 0.99, 1.0)), (0.6, (0.84, 0.84, 0.87)), (1, (0.62, 0.62, 0.66))]))
    cr.fill()
    _text_path(cr, label, W / 2, H / 2 + 0.5, H * 0.46, font="Oswald", weight=cairo.FONT_WEIGHT_BOLD)
    cr.set_source_rgb(0.05, 0.05, 0.06)
    cr.fill()
    return to_image(s)
