#!/usr/bin/env python3
"""Draws the launcher icon, the store icons and the store banner.

Run from the project root: python3 store/make_assets.py
"""
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
FONT = ROOT / 'resources/fonts/ChakraPetch-SemiBold.ttf'
SHOTS = ROOT / 'screenshots'

# The cyberpunk theme's colours, all from the Pebble 64-colour palette.
BLACK = (0, 0, 0, 255)
YELLOW = (255, 255, 0, 255)
RED = (255, 0, 85, 255)
CYAN = (0, 255, 255, 255)
DIM = (85, 85, 85, 255)
CLEAR = (0, 0, 0, 0)

# One heartbeat, as fractions of the box it is drawn in.
PULSE = [(0, .55), (.16, .55), (.23, .42), (.30, .55), (.37, .55), (.43, .70), (.52, .0),
         (.61, 1.0), (.68, .55), (.75, .55), (.83, .36), (.91, .55), (1, .55)]


def cut_rect(box, cut):
    """A rectangle with its top-left and bottom-right corners cut off."""
    x0, y0, x1, y1 = box
    return [(x0 + cut, y0), (x1, y0), (x1, y1 - cut), (x1 - cut, y1), (x0, y1), (x0, y0 + cut)]


def pulse(draw, box, width, color, dx=0):
    x0, y0, x1, y1 = box
    pts = [(x0 + dx + fx * (x1 - x0), y0 + fy * (y1 - y0)) for fx, fy in PULSE]
    draw.line(pts, fill=color, width=width, joint='curve')
    r = width / 2 - .5
    for x, y in (pts[0], pts[-1]):
        draw.ellipse((x - r, y - r, x + r, y + r), fill=color)


def segments(draw, box, filled, total, gap, color):
    x0, y0, x1, y1 = box
    step = (x1 - x0 + gap) / total
    for i in range(total):
        sx = x0 + i * step
        draw.rectangle((sx, y0, sx + step - gap - 1, y1), fill=color if i < filled else DIM)


def store_icon(size, detailed):
    s = 8  # Supersample, then shrink, for clean edges.
    u = size * s / 144  # One unit is a pixel of the 144px icon
    img = Image.new('RGBA', (size * s, size * s), CLEAR)
    d = ImageDraw.Draw(img)
    full = size * s

    d.polygon(cut_rect((0, 0, full - 1, full - 1), 30 * u), fill=BLACK)
    border = 5 * u if detailed else 8 * u
    inset = 6 * u
    d.polygon(cut_rect((inset, inset, full - 1 - inset, full - 1 - inset), 27 * u), fill=YELLOW)
    inner = inset + border
    d.polygon(cut_rect((inner, inner, full - 1 - inner, full - 1 - inner), 25 * u), fill=BLACK)

    if detailed:
        trace = (22 * u, 38 * u, 122 * u, 92 * u)
        stroke, split = 7 * u, 4 * u
    else:
        trace = (24 * u, 40 * u, 120 * u, 104 * u)
        stroke, split = 11 * u, 5 * u
    pulse(d, trace, round(stroke), CYAN, -split)
    pulse(d, trace, round(stroke), RED, split)
    pulse(d, trace, round(stroke), YELLOW)

    if detailed:
        # The face's three meters: vitality, water, food.
        for row, (filled, color) in enumerate(((9, RED), (7, CYAN), (5, YELLOW))):
            y = (102 + row * 9) * u
            right = (122 if row < 2 else 112) * u  # Stay clear of the cut corner
            segments(d, (22 * u, y, right, y + 5 * u), filled, 12 if row < 2 else 11, 2 * u, color)

    return img.resize((size, size), Image.LANCZOS)


def menu_icon():
    """25x25, drawn pixel by pixel: no antialiasing survives the watch's palette."""
    n = 25
    img = Image.new('RGBA', (n, n), CLEAR)
    d = ImageDraw.Draw(img)
    # Black outside the yellow keeps the edge visible on the launcher's white rows.
    d.polygon(cut_rect((0, 0, n - 1, n - 1), 6), fill=BLACK)
    d.polygon(cut_rect((1, 1, n - 2, n - 2), 5), fill=YELLOW)
    d.polygon(cut_rect((2, 2, n - 3, n - 3), 5), fill=BLACK)
    pts = [(4, 13), (7, 13), (8, 11), (9, 13), (10, 13), (11, 16), (12, 6), (14, 19), (15, 13),
           (17, 13), (18, 11), (19, 13), (20, 13)]
    d.line([(x - 1, y) for x, y in pts], fill=CYAN)
    d.line([(x + 1, y) for x, y in pts], fill=RED)
    d.line(pts, fill=YELLOW)
    return img


def banner():
    w, h = 720, 320
    img = Image.new('RGBA', (w, h), BLACK)
    d = ImageDraw.Draw(img)

    # A faint grid behind everything.
    for x in range(0, w, 20):
        d.line((x, 0, x, h), fill=(18, 18, 18, 255))
    for y in range(0, h, 20):
        d.line((0, y, w, y), fill=(18, 18, 18, 255))

    title = ImageFont.truetype(str(FONT), 58)
    small = ImageFont.truetype(str(FONT), 17)
    x, y = 28, 62
    d.text((x - 3, y), 'BIOMON', font=title, fill=CYAN)
    d.text((x + 3, y), 'BIOMON', font=title, fill=RED)
    d.text((x, y), 'BIOMON', font=title, fill=YELLOW)

    d.polygon(cut_rect((x, 140, x + 214, 166), 8), fill=YELLOW)
    d.text((x + 12, 142), 'YOUR BODY, AS A HUD', font=small, fill=BLACK)
    for i, line in enumerate(('HEART RATE / HRV / STRESS', 'ENERGY / WATER / FOOD', '3 GAME THEMES')):
        d.text((x, 182 + i * 24), line, font=small, fill=CYAN if i < 2 else RED)
    pulse(d, (x, 262, x + 214, 300), 3, CYAN, -2)
    pulse(d, (x, 262, x + 214, 300), 3, RED, 2)
    pulse(d, (x, 262, x + 214, 300), 3, YELLOW)

    # The three themes, fanned out and running off the right edge.
    for i, name in enumerate(('emery_rust_d1', 'emery_sub_d1', 'emery_cp_d1')):
        shot = Image.open(SHOTS / f'{name}.png').convert('RGBA')
        sx, sy = 590 - i * 150, 76 - i * 15
        d.rectangle((sx - 3, sy - 3, sx + shot.width + 2, sy + shot.height + 2), fill=BLACK)
        d.rectangle((sx - 2, sy - 2, sx + shot.width + 1, sy + shot.height + 1), outline=YELLOW)
        img.paste(shot, (sx, sy))
    return img.convert('RGB')


if __name__ == '__main__':
    out = ROOT / 'store'
    store_icon(144, True).save(out / 'icon_large.png')
    store_icon(48, False).save(out / 'icon_small.png')
    menu_icon().save(ROOT / 'resources/images/menu_icon.png')
    if SHOTS.exists():
        banner().save(out / 'banner.png')
