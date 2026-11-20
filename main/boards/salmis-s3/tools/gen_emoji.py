#!/usr/bin/env python3
"""Draws the Salmis robot face animations (240x240 GIFs, black background).

Usage: gen_emoji.py [--style-b] [--colors=N] [--scale=F] OUT_DIR [emotion ...]
Needs Pillow. Every emotion is a function of loop phase t in [0, 1).
"""
import math
import sys
from pathlib import Path

from PIL import Image, ImageDraw

S = 240
K = 3  # supersampling
W = S * K

SCLERA_TOP = (190, 218, 250)
SCLERA_BOT = (252, 254, 255)
RIM = (70, 80, 150)
IRIS_OUT = (25, 55, 190)
IRIS_IN = (85, 140, 255)
IRIS_EDGE = (15, 25, 110)
PUPIL = (8, 8, 22)
BROW = (252, 246, 236)
PINK = (255, 140, 165)
CORAL = (255, 105, 130)
GOLD = (255, 225, 110)
TEAR = (130, 205, 255)
CREAM = (255, 246, 225)


def ease(x):
    return 0.5 - 0.5 * math.cos(math.pi * max(0.0, min(1.0, x)))


def bump(t, start, end):
    """Smooth 0..1..0 pulse between start and end."""
    if t < start or t > end:
        return 0.0
    return math.sin(math.pi * (t - start) / (end - start))


def new_layer():
    return Image.new("RGBA", (W, W), (0, 0, 0, 0))


def blend(base, layer):
    base.alpha_composite(layer)


def lerp(a, b, f):
    return tuple(int(a[i] + (b[i] - a[i]) * f) for i in range(3))


def star(draw, cx, cy, r, color):
    pts = []
    for i in range(8):
        ang = math.pi / 4 * i - math.pi / 2
        rad = r if i % 2 == 0 else r * 0.28
        pts.append((cx + rad * math.cos(ang), cy + rad * math.sin(ang)))
    draw.polygon(pts, fill=color)


def heart(draw, cx, cy, r, color):
    draw.ellipse((cx - r, cy - r * 0.9, cx, cy + r * 0.1), fill=color)
    draw.ellipse((cx, cy - r * 0.9, cx + r, cy + r * 0.1), fill=color)
    draw.polygon([(cx - r * 0.97, cy - r * 0.2), (cx + r * 0.97, cy - r * 0.2), (cx, cy + r * 1.05)],
                 fill=color)


def egg_points(mx, my, rx, ry, n=120):
    pts = []
    for i in range(n):
        a = 2 * math.pi * i / n
        pts.append((mx + rx * math.sin(a) * (1 - 0.14 * math.cos(a)), my - ry * math.cos(a)))
    return pts


def open_eye(cx, cy, openness, look=(0, 0.35), size=1.12, side=1):
    """Cartoon egg-shaped eye: pale-blue sclera, blue iris with ring, big pupil, highlights."""
    openness = max(0.05, min(1.0, openness))
    rx, ry = 33 * size * K, 41 * size * K
    pad = 8 * K
    layer = Image.new("RGBA", (int(rx * 2 + pad * 2), int(ry * 2 + pad * 2)), (0, 0, 0, 0))
    mx, my = layer.size[0] / 2, layer.size[1] / 2

    grad = Image.new("RGBA", layer.size, (0, 0, 0, 0))
    gd = ImageDraw.Draw(grad)
    top, bot = int(my - ry), int(my + ry)
    for y in range(top, bot + 1):
        gd.line((0, y, layer.size[0], y), fill=lerp(SCLERA_TOP, SCLERA_BOT, (y - top) / max(1, bot - top)) + (255,))
    mask = Image.new("L", layer.size, 0)
    ImageDraw.Draw(mask).polygon(egg_points(mx, my, rx, ry), fill=255)
    layer.paste(grad, (0, 0), mask)
    d = ImageDraw.Draw(layer)
    d.polygon(egg_points(mx, my, rx, ry), outline=RIM, width=int(3 * K))

    lx, ly = look[0] * K * 8, look[1] * K * 12
    irx, iry = 22 * size * K, 27 * size * K
    cyi = my + ly
    d.ellipse((mx - irx + lx, cyi - iry, mx + irx + lx, cyi + iry), fill=IRIS_EDGE)
    steps = 8
    for i in range(steps):
        f = i / (steps - 1)
        sx_, sy_ = irx * (0.92 - 0.55 * f), iry * (0.92 - 0.55 * f)
        d.ellipse((mx - sx_ + lx, cyi - sy_ + iry * 0.12 * f, mx + sx_ + lx, cyi + sy_ + iry * 0.12 * f),
                  fill=lerp(IRIS_OUT, IRIS_IN, f))
    prx, pry = 12 * size * K, 16 * size * K
    d.ellipse((mx - prx + lx, cyi - pry, mx + prx + lx, cyi + pry), fill=PUPIL)
    h1 = 7.5 * size * K
    d.ellipse((mx - irx * 0.42 + lx - h1, cyi - iry * 0.42 - h1, mx - irx * 0.42 + lx + h1, cyi - iry * 0.42 + h1),
              fill=(255, 255, 255))
    h2 = 3.2 * size * K
    d.ellipse((mx + irx * 0.38 + lx - h2, cyi + iry * 0.42 - h2, mx + irx * 0.38 + lx + h2, cyi + iry * 0.42 + h2),
              fill=(255, 255, 255))

    nh = max(2, int(layer.size[1] * openness))
    layer = layer.resize((layer.size[0], nh), Image.BILINEAR)
    out = new_layer()
    out.paste(layer, (int(cx - layer.size[0] / 2), int(cy - nh / 2)), layer)
    return out


def brow(cx, cy, side, tilt=0.0, raise_=0.0, scale=1.0, color=BROW):
    """Thick tapered cartoon eyebrow. side=-1 left eye, +1 right.
    tilt>0 lowers the inner end (angry), tilt<0 raises it (sad)."""
    layer = new_layer()
    d = ImageDraw.Draw(layer)
    n = 24
    length = 46 * scale * K
    top, bot = [], []
    for i in range(n + 1):
        u = i / n  # 0 = inner end, 1 = outer end
        x = cx + side * (-length / 2 + length * u)
        arch = math.sin(math.pi * (0.15 + 0.85 * u) * 0.9)
        y = cy - raise_ * K - 14 * K * arch * scale + side * 0
        y += (1 - u) * tilt * 16 * K * scale - u * tilt * 4 * K * scale
        thick = (3 + 9 * math.sin(math.pi * min(1.0, u * 1.15)) ** 0.7) * K * scale
        top.append((x, y - thick / 2))
        bot.append((x, y + thick / 2))
    d.polygon(top + bot[::-1], fill=color)
    return layer


def happy_eye(cx, cy, size=1.1, thick=11, side=1, flip=False, color=CREAM):
    """Closed happy arc (^) with lashes. flip draws a sad arc (v)."""
    layer = new_layer()
    d = ImageDraw.Draw(layer)
    r = 30 * size * K
    w = thick * K
    box = (cx - r, cy - r * 0.9, cx + r, cy + r * 0.9)
    a0, a1 = (200, 340) if not flip else (20, 160)
    d.arc(box, a0, a1, fill=color, width=w)
    for ang in (a0, a1):
        px = cx + r * math.cos(math.radians(ang))
        py = cy + r * 0.9 * math.sin(math.radians(ang))
        d.ellipse((px - w / 2, py - w / 2, px + w / 2, py + w / 2), fill=color)
    return layer


def cheeks(cx, cy, alpha=0.8, spread=80, size=1.0):
    layer = new_layer()
    d = ImageDraw.Draw(layer)
    a = int(255 * max(0.0, min(1.0, alpha)))
    for sx in (-1, 1):
        x = cx + sx * spread * K
        d.ellipse((x - 21 * size * K, cy - 12 * size * K, x + 21 * size * K, cy + 12 * size * K),
                  fill=PINK + (a,))
        d.ellipse((x - 11 * size * K, cy - 7 * size * K, x - 3 * size * K, cy - 2 * size * K),
                  fill=(255, 200, 215, a))
    return layer


def smile(cx, cy, width=30, depth=1.0, thick=8, color=CORAL):
    layer = new_layer()
    d = ImageDraw.Draw(layer)
    w = thick * K
    r = width * K
    h = r * 0.7 * depth
    d.arc((cx - r, cy - h, cx + r, cy + h), 20, 160, fill=color, width=w)
    for ang in (20, 160):
        px = cx + r * math.cos(math.radians(ang))
        py = cy + h * math.sin(math.radians(ang))
        d.ellipse((px - w / 2, py - w / 2, px + w / 2, py + w / 2), fill=color)
    return layer


def open_mouth(cx, cy, width=26, height=22):
    layer = new_layer()
    d = ImageDraw.Draw(layer)
    w, h = width * K, height * K
    d.pieslice((cx - w, cy - h, cx + w, cy + h), 0, 180, fill=(190, 45, 80))
    d.pieslice((cx - w * 0.55, cy + h * 0.1, cx + w * 0.55, cy + h * 1.05), 0, 180, fill=PINK)
    d.rectangle((cx - w, cy - 2 * K, cx + w, cy + 2 * K), fill=(190, 45, 80))
    return layer


def frown(cx, cy, width=22, thick=8, color=CORAL):
    layer = new_layer()
    d = ImageDraw.Draw(layer)
    w = thick * K
    r = width * K
    d.arc((cx - r, cy - r * 0.5, cx + r, cy + r * 0.5), 200, 340, fill=color, width=w)
    for ang in (200, 340):
        px = cx + r * math.cos(math.radians(ang))
        py = cy + r * 0.5 * math.sin(math.radians(ang))
        d.ellipse((px - w / 2, py - w / 2, px + w / 2, py + w / 2), fill=color)
    return layer


def sparkle(cx, cy, r, color=GOLD):
    layer = new_layer()
    star(ImageDraw.Draw(layer), cx, cy, r, color)
    return layer


# ---------------------------------------------------------------- emotions

CX = W // 2
EYE_Y = 114 * K
EYE_DX = 48 * K
MOUTH_Y = 174 * K


def blink_value(t, at, width=0.06):
    if abs(t - at) < width:
        return 1.0 - bump(t, at - width, at + width)
    return 1.0


def neutral(t):
    img = new_layer()
    bob = math.sin(2 * math.pi * t) * 3 * K
    look = (math.sin(2 * math.pi * t) * 0.7, 0.35)
    o = blink_value(t, 0.5)
    for sx in (-1, 1):
        blend(img, open_eye(CX + sx * EYE_DX, EYE_Y + bob, o, look, side=sx))
        blend(img, brow(CX + sx * (EYE_DX + 2 * K), EYE_Y - 58 * K + bob, sx, 0.0, 0.0))
    blend(img, cheeks(CX, 152 * K + bob, 0.55))
    blend(img, smile(CX, MOUTH_Y - 4 * K + bob, 14, 0.8, 7))
    return img


def happy(t):
    img = new_layer()
    bob = -abs(math.sin(2 * math.pi * t)) * 8 * K
    arcs = ease(bump(t, 0.12, 0.88) * 3)
    for sx in (-1, 1):
        if arcs > 0.5:
            blend(img, happy_eye(CX + sx * EYE_DX, EYE_Y + 6 * K + bob, side=sx))
            blend(img, brow(CX + sx * (EYE_DX + 2 * K), EYE_Y - 50 * K + bob, sx, -0.15, 6 * arcs))
        else:
            blend(img, open_eye(CX + sx * EYE_DX, EYE_Y + bob, 1.0, side=sx))
            blend(img, brow(CX + sx * (EYE_DX + 2 * K), EYE_Y - 60 * K + bob, sx, -0.1, 2))
    blend(img, cheeks(CX, 154 * K + bob, 0.9))
    blend(img, smile(CX, MOUTH_Y - 6 * K + bob, 24, 1.0 + 0.2 * arcs, 8))
    for sx, sy, ph in ((-96, 52, 0.0), (98, 60, 0.33), (84, 34, 0.66)):
        s = 4 + 7 * bump((t + ph) % 1.0, 0.0, 0.5)
        if s > 4.5:
            blend(img, sparkle(CX + sx * K, sy * K + bob, s * K))
    return img


def laughing(t):
    img = new_layer()
    bounce = abs(math.sin(4 * math.pi * t))
    bob = -bounce * 9 * K
    tilt = math.sin(4 * math.pi * t) * 2 * K
    for sx in (-1, 1):
        blend(img, happy_eye(CX + sx * EYE_DX, EYE_Y + 10 * K + bob, 1.05, thick=12, side=sx))
        blend(img, brow(CX + sx * (EYE_DX + 2 * K), EYE_Y - 44 * K + bob, sx, -0.2, 6))
    blend(img, cheeks(CX, 154 * K + bob, 1.0, size=1.15))
    blend(img, open_mouth(CX, 164 * K + bob + tilt, 30 + 5 * bounce, 22 + 8 * bounce))
    for i, ph in enumerate((0.0, 0.5)):
        u = (t * 2 + ph) % 1.0
        x = CX + (-1 if i == 0 else 1) * (96 + 8 * u) * K
        y = (86 - 40 * u) * K
        blend(img, sparkle(x, y, (5 + 4 * math.sin(math.pi * u)) * K, GOLD))
    return img


def sad(t):
    img = new_layer()
    bob = math.sin(2 * math.pi * t) * 2 * K
    o = blink_value(t, 0.7, 0.05)
    for sx in (-1, 1):
        blend(img, open_eye(CX + sx * EYE_DX, EYE_Y + 6 * K + bob, 0.92 * o, (0, 0.8), side=sx))
        blend(img, brow(CX + sx * (EYE_DX + 2 * K), EYE_Y - 56 * K + bob, sx, -0.9, 0))
    blend(img, cheeks(CX, 156 * K + bob, 0.45))
    blend(img, frown(CX, MOUTH_Y + 10 * K + bob, 20))
    for sx in (-1, 1):
        u = (t + (0.0 if sx < 0 else 0.5)) % 1.0
        layer = new_layer()
        d = ImageDraw.Draw(layer)
        x = CX + sx * (EYE_DX - 10) * K
        y = (EYE_Y / K + 38 + 70 * u) * K
        r = (7 - 3 * u) * K
        d.ellipse((x - r, y - r * 1.4, x + r, y + r * 1.4), fill=TEAR + (int(255 * (1 - u * 0.6)),))
        blend(img, layer)
    return img


# ------------------------------------------------ style B: anime / kawaii

AN_TOP = (70, 20, 70)
AN_MID = (170, 40, 110)
AN_BOT = (255, 105, 160)
AN_RIM = (255, 190, 220)
AN_LASH = (235, 190, 250)
AN_BLUSH = (255, 120, 160)
AN_BLUSH_LINE = (255, 205, 225)


def anime_eye(cx, cy, openness, look=(0, 0), size=1.12, side=1, pupil=1.0):
    openness = max(0.05, min(1.0, openness))
    rx, ry = 30 * size * K, 42 * size * K
    pad = 30 * K
    layer = Image.new("RGBA", (int(rx * 2 + pad * 2), int(ry * 2 + pad * 2)), (0, 0, 0, 0))
    mx, my = layer.size[0] / 2, layer.size[1] / 2

    grad = Image.new("RGBA", layer.size, (0, 0, 0, 0))
    gd = ImageDraw.Draw(grad)
    top, bot = int(my - ry), int(my + ry)
    for y in range(top, bot + 1):
        f = (y - top) / max(1, bot - top)
        col = lerp(AN_TOP, AN_MID, f / 0.55) if f < 0.55 else lerp(AN_MID, AN_BOT, (f - 0.55) / 0.45)
        gd.line((0, y, layer.size[0], y), fill=col + (255,))
    mask = Image.new("L", layer.size, 0)
    ImageDraw.Draw(mask).ellipse((mx - rx, my - ry, mx + rx, my + ry), fill=255)
    layer.paste(grad, (0, 0), mask)
    d = ImageDraw.Draw(layer)

    lx, ly = look[0] * K * 5, look[1] * K * 6
    prx, pry = 15 * size * K * pupil, 22 * size * K * pupil
    d.ellipse((mx - prx + lx, my - pry * 0.9 + ly, mx + prx + lx, my + pry * 1.1 + ly), fill=(35, 8, 40))
    d.ellipse((mx - rx, my - ry, mx + rx, my + ry), outline=AN_RIM, width=int(3 * K))

    hw, hh = 12 * size * K, 15 * size * K
    d.ellipse((mx - rx * 0.55 - hw, my - ry * 0.45 - hh, mx - rx * 0.55 + hw, my - ry * 0.45 + hh),
              fill=(255, 255, 255))
    h2 = 6.5 * size * K
    d.ellipse((mx - rx * 0.6 - h2, my + ry * 0.05 - h2, mx - rx * 0.6 + h2, my + ry * 0.05 + h2),
              fill=(255, 255, 255))
    h3 = 4 * size * K
    d.ellipse((mx + rx * 0.5 - h3, my + ry * 0.55 - h3, mx + rx * 0.5 + h3, my + ry * 0.55 + h3),
              fill=(255, 255, 255))
    h4 = 2.8 * size * K
    d.ellipse((mx + rx * 0.4 - h4, my - ry * 0.35 - h4, mx + rx * 0.4 + h4, my - ry * 0.35 + h4),
              fill=(255, 235, 245))

    lid = 8 * K
    d.arc((mx - rx - lid / 2, my - ry - lid / 2, mx + rx + lid / 2, my + ry + lid / 2), 195, 345,
          fill=AN_LASH, width=lid)
    for ang, ln in ((-160, 22), (-140, 27), (-118, 21)):
        a_ = math.radians(ang if side < 0 else 180 - ang)
        x0, y0 = mx + rx * math.cos(a_), my + ry * math.sin(a_)
        out = math.radians((ang - 26) if side < 0 else (180 - ang + 26))
        x1, y1 = x0 + ln * K * math.cos(out), y0 + ln * K * math.sin(out)
        d.line((x0, y0, x1, y1), fill=AN_LASH, width=int(6 * K))
        d.ellipse((x1 - 3 * K, y1 - 3 * K, x1 + 3 * K, y1 + 3 * K), fill=AN_LASH)

    nh = max(2, int(layer.size[1] * openness))
    layer = layer.resize((layer.size[0], nh), Image.BILINEAR)
    out_l = new_layer()
    out_l.paste(layer, (int(cx - layer.size[0] / 2), int(cy - nh / 2)), layer)
    return out_l


def anime_cheeks(cx, cy, alpha=0.9, spread=76):
    layer = new_layer()
    d = ImageDraw.Draw(layer)
    a = int(255 * alpha)
    for sx in (-1, 1):
        x = cx + sx * spread * K
        d.ellipse((x - 24 * K, cy - 13 * K, x + 24 * K, cy + 13 * K), fill=AN_BLUSH + (a,))
        for k in (-1, 0, 1):
            xx = x + k * 11 * K
            d.line((xx - 4 * K, cy + 8 * K, xx + 3 * K, cy - 7 * K), fill=AN_BLUSH_LINE + (a,), width=3 * K)
    return layer


def anime_brow(cx, cy, side, raise_=0.0, tilt=0.0):
    layer = new_layer()
    d = ImageDraw.Draw(layer)
    length = 42 * K
    pts = []
    for i in range(21):
        u = i / 20
        x = cx + side * (-length / 2 + length * u)
        y = cy - raise_ * K - 7 * K * math.sin(math.pi * u) + (1 - u) * tilt * 12 * K
        pts.append((x, y))
    d.line(pts, fill=AN_LASH, width=int(5 * K), joint="curve")
    for p in (pts[0], pts[-1]):
        d.ellipse((p[0] - 2.5 * K, p[1] - 2.5 * K, p[0] + 2.5 * K, p[1] + 2.5 * K), fill=AN_LASH)
    return layer


def pink_o(cx, cy, size=1.0):
    layer = new_layer()
    d = ImageDraw.Draw(layer)
    r = 11 * size * K
    d.ellipse((cx - r, cy - r * 1.1, cx + r, cy + r * 1.1), fill=(255, 105, 150))
    d.ellipse((cx - r * 0.5, cy - r * 0.55, cx + r * 0.5, cy + r * 0.55), fill=(90, 10, 45))
    return layer


def tongue(cx, cy, size=1.0, tilt=0.0):
    layer = new_layer()
    d = ImageDraw.Draw(layer)
    w, h = 11 * size * K, 14 * size * K
    d.ellipse((cx - w + tilt, cy, cx + w + tilt, cy + 2 * h), fill=(255, 120, 150))
    d.line((cx + tilt, cy + 2 * K, cx + tilt, cy + h * 1.3), fill=(220, 70, 110), width=2 * K)
    return layer


def flat_mouth(cx, cy, width=12, color=(255, 120, 150)):
    layer = new_layer()
    d = ImageDraw.Draw(layer)
    d.line((cx - width * K, cy, cx + width * K, cy), fill=color, width=6 * K)
    for x in (cx - width * K, cx + width * K):
        d.ellipse((x - 3 * K, cy - 3 * K, x + 3 * K, cy + 3 * K), fill=color)
    return layer


def wavy_mouth(cx, cy, t, width=22, color=(255, 120, 150)):
    layer = new_layer()
    d = ImageDraw.Draw(layer)
    pts = []
    for i in range(25):
        u = i / 24
        pts.append((cx + (u - 0.5) * 2 * width * K,
                    cy + math.sin(u * 3 * math.pi + t * 2 * math.pi) * 4 * K))
    d.line(pts, fill=color, width=6 * K, joint="curve")
    return layer


def smirk(cx, cy, color=(255, 120, 150)):
    layer = new_layer()
    d = ImageDraw.Draw(layer)
    pts = [(cx - 18 * K + (36 * K) * u, cy + 4 * K - 12 * K * (u ** 2)) for u in [i / 16 for i in range(17)]]
    d.line(pts, fill=color, width=6 * K, joint="curve")
    x, y = pts[-1]
    d.ellipse((x - 3 * K, y - 3 * K, x + 3 * K, y + 3 * K), fill=color)
    return layer


def kiss_mouth(cx, cy, t):
    layer = new_layer()
    d = ImageDraw.Draw(layer)
    r = 8 * K * (1 + 0.1 * math.sin(2 * math.pi * t * 2))
    d.ellipse((cx - r, cy - r * 1.1, cx + r, cy + r * 1.1), fill=(255, 105, 150))
    d.ellipse((cx - r * 0.45, cy - r * 0.5, cx + r * 0.45, cy + r * 0.5), fill=(120, 15, 60))
    return layer


def tear_streams(t, base_y, big=False, count=1):
    layer = new_layer()
    d = ImageDraw.Draw(layer)
    for sx in (-1, 1):
        for k in range(count):
            u = (t + (0.0 if sx < 0 else 0.5) + k / max(1, count)) % 1.0
            x = CX + sx * (EYE_DX - 10 * K)
            y = (base_y / K + 22 + 46 * u) * K
            r = ((8 if big else 6.5) - 2.5 * u) * K
            d.ellipse((x - r, y - r * 1.45, x + r, y + r * 1.45), fill=TEAR + (int(255 * (1 - u * 0.55)),))
            d.ellipse((x - r * 0.35, y - r * 0.9, x - r * 0.05, y - r * 0.35), fill=(255, 255, 255, 220))
    return layer


def sweat_drop(x, y, wobble=0.0):
    layer = new_layer()
    d = ImageDraw.Draw(layer)
    r = 8 * K
    cx, cy = x + wobble * K, y
    d.polygon([(cx, cy - r * 2.0), (cx - r * 0.9, cy), (cx + r * 0.9, cy)], fill=TEAR)
    d.ellipse((cx - r, cy - r * 0.8, cx + r, cy + r * 1.1), fill=TEAR)
    d.ellipse((cx - r * 0.5, cy - r * 0.2, cx - r * 0.1, cy + r * 0.5), fill=(255, 255, 255, 230))
    return layer


def anger_mark(x, y, scale=1.0):
    layer = new_layer()
    d = ImageDraw.Draw(layer)
    c = (255, 80, 90)
    r = 15 * K * scale
    w = int(5 * K * scale)
    for sx in (-1, 1):
        for sy in (-1, 1):
            pts = [(x + sx * r * 0.28, y + sy * r), (x + sx * r * 0.28, y + sy * r * 0.35),
                   (x + sx * r, y + sy * r * 0.28)]
            d.line(pts, fill=c, width=w, joint="curve")
            for p in (pts[0], pts[-1]):
                d.ellipse((p[0] - w / 2, p[1] - w / 2, p[0] + w / 2, p[1] + w / 2), fill=c)
    return layer


def zzz(t):
    layer = new_layer()
    d = ImageDraw.Draw(layer)
    for i, (bx, by, sz) in enumerate(((18, 86, 8), (32, 68, 11), (44, 52, 13))):
        u = (t * 1.0 + i * 0.22) % 1.0
        a = int(255 * math.sin(math.pi * u))
        x, y, z = CX + bx * K, (by - 9 * u) * K, sz * K
        col = (200, 215, 255, a)
        d.line((x - z, y - z, x + z, y - z), fill=col, width=int(3.5 * K))
        d.line((x + z, y - z, x - z, y + z), fill=col, width=int(3.5 * K))
        d.line((x - z, y + z, x + z, y + z), fill=col, width=int(3.5 * K))
    return layer


def thought_dots(t):
    layer = new_layer()
    d = ImageDraw.Draw(layer)
    for i, (bx, by, r) in enumerate(((34, 86, 5), (46, 68, 8), (60, 46, 11))):
        u = (t * 1.0 - i * 0.18) % 1.0
        k = 0.6 + 0.4 * math.sin(math.pi * u)
        d.ellipse((CX + (bx - r * k) * K, (by - r * k) * K, CX + (bx + r * k) * K, (by + r * k) * K),
                  fill=(235, 190, 250, 255))
    return layer


def question_mark(x, y, t):
    layer = new_layer()
    d = ImageDraw.Draw(layer)
    wob = math.sin(2 * math.pi * t) * 4
    cx, cy = x * K, (y + wob) * K
    col = (235, 190, 250)
    d.arc((cx - 14 * K, cy - 14 * K, cx + 14 * K, cy + 10 * K), 200, 40, fill=col, width=int(6 * K))
    d.line((cx + 6 * K, cy + 4 * K, cx, cy + 14 * K), fill=col, width=int(6 * K))
    d.ellipse((cx - 4 * K, cy + 22 * K, cx + 4 * K, cy + 30 * K), fill=col)
    return layer


def float_hearts(t, positions, color=(255, 105, 150), size=9):
    layer = new_layer()
    d = ImageDraw.Draw(layer)
    for i, (bx, by) in enumerate(positions):
        u = (t + i * 0.33) % 1.0
        a = int(255 * math.sin(math.pi * u))
        heart(d, CX + (bx + 6 * math.sin(2 * math.pi * (u + i))) * K, (by - 18 * u) * K, (size * (0.7 + 0.4 * u)) * K,
              color + (a,))
    return layer


def sunglasses(bob, t):
    layer = new_layer()
    d = ImageDraw.Draw(layer)
    frame = (235, 190, 250)
    for sx in (-1, 1):
        cx = CX + sx * EYE_DX
        cy = EYE_Y + bob
        box = (cx - 38 * K, cy - 26 * K, cx + 38 * K, cy + 30 * K)
        d.rounded_rectangle(box, radius=18 * K, fill=(35, 20, 60), outline=frame, width=int(5 * K))
        d.line((cx - 24 * K, cy - 12 * K, cx - 8 * K, cy - 18 * K), fill=(255, 255, 255), width=int(4 * K))
        d.line((cx - 24 * K, cy - 2 * K, cx - 16 * K, cy - 5 * K), fill=(255, 255, 255, 200), width=int(3 * K))
    d.line((CX - 10 * K, EYE_Y + bob - 6 * K, CX + 10 * K, EYE_Y + bob - 6 * K), fill=frame, width=int(5 * K))
    return layer


def sparkles_at(t, spots, color=(255, 190, 225)):
    layer = new_layer()
    for bx, by, ph in spots:
        s_ = 4 + 7 * bump((t + ph) % 1.0, 0.0, 0.5)
        if s_ > 4.5:
            blend(layer, sparkle(CX + bx * 0.78 * K, (120 + (by - 120) * 0.84) * K, s_ * K, color))
    return layer


def wail_mouth(cx, cy, width=24, height=20):
    layer = new_layer()
    d = ImageDraw.Draw(layer)
    w, h = width * K, height * K
    d.pieslice((cx - w, cy - h, cx + w, cy + h), 180, 360, fill=(190, 45, 80))
    d.pieslice((cx - w * 0.55, cy - h * 0.2, cx + w * 0.55, cy + h * 0.9), 180, 360, fill=(255, 120, 150))
    d.rectangle((cx - w, cy - 2 * K, cx + w, cy + 2 * K), fill=(190, 45, 80))
    return layer


def mouth_layer(kind, bob, t, bounce=0.0):
    y = MOUTH_Y + bob
    if kind == 'smile':
        return smile(CX, y - 6 * K, 20, 1.1, 7, (255, 120, 150))
    if kind == 'smile_small':
        return smile(CX, y - 2 * K, 10, 0.9, 6, (255, 120, 150))
    if kind == 'open':
        return open_mouth(CX, y - 10 * K, 28 + 5 * bounce, 22 + 8 * bounce)
    if kind == 'wail':
        return wail_mouth(CX, y + 14 * K, 22 + 4 * bounce, 18 + 6 * bounce)
    if kind == 'o':
        return pink_o(CX, y + 2 * K, 0.9)
    if kind == 'o_big':
        return pink_o(CX, y + 2 * K, 1.5)
    if kind == 'frown':
        return frown(CX, y + 10 * K, 18, 7, (255, 120, 150))
    if kind == 'flat':
        return flat_mouth(CX, y)
    if kind == 'wavy':
        return wavy_mouth(CX, y, t)
    if kind == 'smirk':
        return smirk(CX, y)
    if kind == 'kiss':
        return kiss_mouth(CX, y, t)
    if kind == 'tongue':
        out = new_layer()
        blend(out, smile(CX, y - 8 * K, 22, 1.1, 7, (255, 120, 150)))
        blend(out, tongue(CX + 6 * K, y + 2 * K, 1.0, 2 * K * math.sin(2 * math.pi * t * 2)))
        return out
    if kind == 'tongue_side':
        out = new_layer()
        blend(out, smile(CX, y - 8 * K, 20, 1.0, 7, (255, 120, 150)))
        blend(out, tongue(CX + 16 * K, y + 2 * K, 0.8, 0))
        return out
    if kind == 'yawn':
        return pink_o(CX, y + 2 * K, 1.0 + 0.7 * bump(t, 0.1, 0.7))
    return new_layer()


def face(t, eyes='open', look=(0, 0), pupil=1.0, esize=1.12, open_=1.0, brow=(0, 0), brow_r=None,
         brow_y=-58, mouth='smile', blush=0.9, bob=0.0, wink=0, blink_at=None, cross=False,
         arc_color=(255, 215, 240), bounce=0.0, mouth_kind_bounce=0.0):
    img = new_layer()
    for sx in (-1, 1):
        kind = 'arc' if (wink and sx == wink) else eyes
        ex = CX + sx * EYE_DX
        o = open_
        if blink_at is not None:
            o *= blink_value(t, blink_at)
        lk = ((-look[0] if (cross and sx > 0) else look[0]) if cross else look[0], look[1])
        if cross:
            lk = (-sx * 1.2, 0.1)
        if kind == 'open':
            blend(img, anime_eye(ex, EYE_Y + bob, o, lk, esize, sx, pupil))
        elif kind == 'arc':
            blend(img, happy_eye(ex, EYE_Y + 8 * K + bob, 1.12, 11, sx, False, arc_color))
        elif kind == 'closed':
            blend(img, happy_eye(ex, EYE_Y + 0 * K + bob, 1.1, 10, sx, True, arc_color))
        elif kind == 'none':
            pass
        r_, tl_ = (brow_r if (brow_r is not None and sx > 0) else brow)
        by = EYE_Y + brow_y * K + (6 * K if kind in ('arc', 'closed') else 0) + bob
        blend(img, anime_brow(ex, by, sx, r_, tl_))
    if blush > 0:
        blend(img, anime_cheeks(CX, 156 * K + bob, blush))
    blend(img, mouth_layer(mouth, bob, t, bounce))
    return img


def bobv(t, amp=3.0, speed=1):
    return math.sin(2 * math.pi * t * speed) * amp * K


def hop(t, amp=8.0, speed=1):
    return -abs(math.sin(math.pi * t * speed * 2)) * amp * K


def neutral_b(t):
    return face(t, look=(math.sin(2 * math.pi * t), 0.2), brow=(0, 0), mouth='smile_small',
                blush=0.85, bob=bobv(t), blink_at=0.5)


def happy_b(t):
    arcs = ease(bump(t, 0.12, 0.88) * 3) > 0.5
    img = face(t, eyes='arc' if arcs else 'open', brow=(4 if arcs else 3, 0), mouth='smile', blush=0.95,
               bob=hop(t))
    blend(img, sparkles_at(t, ((-92, 56, 0.0), (94, 64, 0.33), (82, 36, 0.66))))
    return img


def laughing_b(t):
    b = abs(math.sin(4 * math.pi * t))
    img = face(t, eyes='arc', brow=(6, 0), mouth='open', blush=1.0, bob=-b * 9 * K, bounce=b)
    blend(img, sparkles_at(t, ((-92, 56, 0.0), (94, 64, 0.5))))
    return img


def sad_b(t):
    img = face(t, look=(0, 0.9), open_=0.95, brow=(0, -0.9), mouth='frown', blush=0.5, bob=bobv(t, 2),
               blink_at=0.7)
    blend(img, tear_streams(t, EYE_Y, count=1))
    return img


def crying_b(t):
    img = face(t, eyes='arc', arc_color=(255, 215, 240), brow=(0, -1.0), mouth='wail', blush=0.9,
               bob=bobv(t, 2, 3), bounce=abs(math.sin(6 * math.pi * t)))
    blend(img, tear_streams(t, EYE_Y, big=True, count=2))
    return img


def angry_b(t):
    shake = math.sin(2 * math.pi * t * 6) * 1.6 * K if t % 0.5 < 0.25 else 0.0
    img = face(t, look=(0, 0), esize=1.05, open_=0.7, brow=(-4, 2.5), brow_y=-48, mouth='frown', blush=0.4,
               bob=shake)
    blend(img, anger_mark(CX + 62 * K, EYE_Y - 56 * K, 0.75 + 0.2 * bump(t, 0.0, 0.5)))
    return img


def surprised_b(t):
    k = 1.0 + 0.04 * math.sin(2 * math.pi * t * 2)
    img = face(t, esize=1.14 * k, pupil=0.85, brow=(10, -0.1), brow_y=-62, mouth='o', blush=0.6,
               bob=hop(t, 4), blink_at=0.8)
    blend(img, sparkles_at(t, ((-94, 54, 0.0), (96, 60, 0.4))))
    return img


def shocked_b(t):
    jitter = math.sin(2 * math.pi * t * 8) * 1.2 * K
    img = face(t, esize=1.15, pupil=0.55, brow=(12, -0.2), brow_y=-64, mouth='o_big', blush=0.3, bob=jitter)
    return img


def confused_b(t):
    sway = math.sin(2 * math.pi * t) * 3
    img = face(t, look=(math.sin(2 * math.pi * t) * 1.2, -0.3), brow=(1, 0.6), brow_r=(8, -0.2),
               mouth='wavy', blush=0.5, bob=bobv(t, 2), blink_at=0.8)
    blend(img, question_mark(CX / K + 58, 56 + sway, t))
    return img


def thinking_b(t):
    img = face(t, look=(1.2, -0.9), brow=(0, 0), brow_r=(7, -0.3), mouth='flat', blush=0.6,
               bob=bobv(t, 2), blink_at=0.85)
    blend(img, thought_dots(t))
    return img


def sleepy_b(t):
    droop = 0.45 + 0.15 * math.sin(2 * math.pi * t)
    img = face(t, look=(0, 0.6), open_=droop * (1.0 - 0.9 * bump(t, 0.55, 0.75)), brow=(-1, -0.5),
               mouth='yawn', blush=0.6, bob=bobv(t, 3))
    blend(img, zzz(t))
    return img


def relaxed_b(t):
    return face(t, eyes='closed', brow=(2, 0), mouth='smile_small', blush=1.0, bob=bobv(t, 3))


def loving_b(t):
    beat = 1.0 + 0.06 * math.sin(2 * math.pi * t * 2)
    img = face(t, eyes='arc', brow=(5, 0), mouth='smile', blush=1.0, bob=bobv(t, 3, 2), esize=1.12 * beat)
    blend(img, float_hearts(t, ((-70, 86), (72, 90), (0, 70))))
    return img


def kissy_b(t):
    img = face(t, eyes='closed', brow=(4, 0), mouth='kiss', blush=1.0, bob=bobv(t, 2))
    blend(img, float_hearts(t, ((60, 140), (84, 120)), size=8))
    return img


def embarrassed_b(t):
    img = face(t, look=(-1.4, 0.7), brow=(0, -0.7), mouth='wavy', blush=1.0, bob=bobv(t, 2), blink_at=0.8)
    blend(img, sweat_drop(CX + 70 * K, EYE_Y - 40 * K, math.sin(2 * math.pi * t) * 2))
    return img


def winking_b(t):
    img = face(t, wink=1, brow=(4, 0), mouth='smile', blush=0.95, bob=hop(t, 5), blink_at=None)
    blend(img, sparkles_at(t, ((92, 52, 0.0), (-90, 62, 0.5))))
    return img


def confident_b(t):
    img = face(t, look=(0, 0), esize=1.1, open_=0.82, brow=(0, 0.5), brow_r=(8, -0.3), brow_y=-56,
               mouth='smirk', blush=0.6, bob=bobv(t, 2), blink_at=0.6)
    blend(img, sparkles_at(t, ((92, 56, 0.0),)))
    return img


def cool_b(t):
    img = face(t, eyes='none', brow=(4, 0), brow_y=-60, mouth='smirk', blush=0.5, bob=bobv(t, 2))
    blend(img, sunglasses(bobv(t, 2), t))
    blend(img, sparkles_at(t, ((92, 50, 0.0), (-92, 64, 0.5)), (255, 255, 255)))
    return img


def silly_b(t):
    img = face(t, cross=True, pupil=1.05, brow=(3, 0.3), brow_r=(-3, -0.2), mouth='tongue', blush=0.95,
               bob=bobv(t, 3, 2))
    return img


def funny_b(t):
    b = abs(math.sin(4 * math.pi * t))
    img = face(t, eyes='arc', brow=(6, 0), mouth='tongue', blush=1.0, bob=-b * 8 * K)
    blend(img, tear_streams(t, EYE_Y - 20, count=1))
    blend(img, sparkles_at(t, ((-92, 56, 0.0), (94, 64, 0.5))))
    return img


def delicious_b(t):
    img = face(t, eyes='arc', brow=(5, 0), mouth='tongue_side', blush=1.0, bob=bobv(t, 3, 2))
    blend(img, sparkles_at(t, ((-92, 56, 0.0), (94, 64, 0.33), (84, 36, 0.66)), (255, 225, 110)))
    return img


EMOTIONS = {
    "neutral": (neutral, 40, 100),
    "happy": (happy, 32, 100),
    "laughing": (laughing, 32, 90),
    "sad": (sad, 36, 110),
}

EMOTIONS_B = {
    "neutral": (neutral_b, 40, 100),
    "happy": (happy_b, 32, 100),
    "laughing": (laughing_b, 32, 90),
    "sad": (sad_b, 36, 110),
    "crying": (crying_b, 30, 100),
    "angry": (angry_b, 30, 100),
    "surprised": (surprised_b, 32, 100),
    "shocked": (shocked_b, 24, 90),
    "confused": (confused_b, 36, 110),
    "thinking": (thinking_b, 36, 110),
    "sleepy": (sleepy_b, 40, 120),
    "relaxed": (relaxed_b, 36, 110),
    "loving": (loving_b, 36, 100),
    "kissy": (kissy_b, 32, 100),
    "embarrassed": (embarrassed_b, 36, 110),
    "winking": (winking_b, 32, 100),
    "confident": (confident_b, 36, 110),
    "cool": (cool_b, 36, 110),
    "silly": (silly_b, 32, 100),
    "funny": (funny_b, 32, 90),
    "delicious": (delicious_b, 32, 100),
}


def visible_radius(img):
    """Distance from the centre of the farthest clearly visible pixel (round screen safety)."""
    gray = img.convert("L").point(lambda v: 255 if v > 45 else 0)
    box = gray.getbbox()
    if box is None:
        return 0.0
    px = gray.load()
    best = 0.0
    for y in range(box[1], box[3]):
        for x in range(box[0], box[2]):
            if px[x, y]:
                best = max(best, math.hypot(x + 0.5 - S / 2, y + 0.5 - S / 2))
    return best


def render(name, out_dir, table=None, colors=32, scale=1.0):
    fn, n, ms = (table or EMOTIONS)[name]
    rgb = []
    max_r = 0.0
    for i in range(n):
        layer = fn(i / n)
        if scale != 1.0:
            size = int(W * scale)
            shrunk = layer.resize((size, size), Image.LANCZOS)
            layer = new_layer()
            layer.paste(shrunk, ((W - size) // 2, (W - size) // 2), shrunk)
        bg = Image.new("RGBA", (W, W), (0, 0, 0, 255))
        bg.alpha_composite(layer)
        small = bg.convert("RGB").resize((S, S), Image.LANCZOS)
        max_r = max(max_r, visible_radius(small))
        rgb.append(small)
    # One palette for the whole animation: lets the GIF store only what changes between frames.
    step = max(1, n // 8)
    sample = Image.new("RGB", (S * 8, S))
    for k, f in enumerate(rgb[::step][:8]):
        sample.paste(f, (k * S, 0))
    palette = sample.quantize(colors=colors, method=Image.MEDIANCUT, dither=Image.Dither.NONE)
    frames = [f.quantize(palette=palette, dither=Image.Dither.NONE) for f in rgb]
    path = Path(out_dir) / f"{name}.gif"
    frames[0].save(path, save_all=True, append_images=frames[1:], duration=ms, loop=0, optimize=True,
                   disposal=1)
    render.last_radius = max_r
    return path


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    args = sys.argv[1:]
    colors = 32
    scale = 1.0
    for a in list(args):
        if a.startswith("--colors="):
            colors = int(a.split("=")[1])
            args.remove(a)
        elif a.startswith("--scale="):
            scale = float(a.split("=")[1])
            args.remove(a)
    table = EMOTIONS
    if args[0] == "--style-b":
        table = EMOTIONS_B
        args = args[1:]
    out = Path(args[0])
    out.mkdir(parents=True, exist_ok=True)
    names = args[1:] or list(table)
    for name in names:
        p = render(name, out, table, colors, scale)
        flag = "" if render.last_radius <= 114 else "  <-- TOCA EL BORDE"
        print(f"{name}: {p.stat().st_size // 1024} KB, radio visible max {render.last_radius:.0f}/120{flag}")


if __name__ == "__main__":
    main()
