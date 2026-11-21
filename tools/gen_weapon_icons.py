#!/usr/bin/env python3
"""Draws the weapon-row icons and writes src/weapon_icons.h.

Original artwork (not taken from the game): 0 handgun, 1 submachine gun, 2 shotgun,
3 rocket launcher, 4 C4. Each icon sits on a slanted dark plate in the style of the
life-bar HUD. Three variants per weapon: idle (dim, grey border), held by P1 (blue
border), held by P2 (orange border).
Output: 48x24 RGBA5551 (1-bit alpha), shown at scale 0.5.
Usage: python tools/gen_weapon_icons.py [preview.png]
"""
import sys
from PIL import Image, ImageDraw, ImageFilter, ImageChops

W, H, S = 48, 24, 8  # final size, supersampling factor


def P(pts):
    return [(x * S, y * S) for x, y in pts]


def R(x0, y0, x1, y1):
    return P([(x0, y0), (x1, y0), (x1, y1), (x0, y1)])


# Weapon shapes in a 48x24 box (the plate interior is about x 5..43, y 4..20).
# Each entry: list of ("metal"|"dark"|"hole", polygon)
WEAPONS = [
    [  # handgun
        ("metal", P([(13, 7), (34, 7), (35, 8), (35, 11), (13, 11)])),            # slide
        ("dark", P([(15, 11), (22, 11), (20, 19), (14, 19), (13, 17)])),          # grip
        ("metal", P([(22, 11), (26, 11), (26, 14), (22, 14)])),                   # trigger guard
        ("hole", P([(23, 11.6), (25.2, 11.6), (25.2, 13.2), (23, 13.2)])),
        ("dark", P([(31, 6), (33, 6), (33, 7), (31, 7)])),                        # front sight
    ],
    [  # submachine gun
        ("metal", P([(10, 8), (36, 8), (36, 12), (10, 12)])),                     # body
        ("metal", P([(36, 9), (41, 9), (41, 11), (36, 11)])),                     # barrel
        ("dark", P([(17, 12), (21, 12), (20, 19), (16, 19)])),                    # grip
        ("dark", P([(25, 12), (28, 12), (29, 20), (26, 20)])),                    # magazine
        ("metal", P([(10, 8), (6, 9), (6, 13), (8, 13), (10, 12)])),              # stock
        ("dark", P([(14, 7), (16, 7), (16, 8), (14, 8)])),
    ],
    [  # shotgun
        ("metal", P([(14, 8), (43, 8), (43, 10), (14, 10)])),                     # barrel
        ("dark", P([(22, 10), (34, 10), (34, 12), (22, 12)])),                    # pump
        ("metal", P([(14, 8), (18, 8), (18, 12), (14, 12)])),                     # receiver
        ("dark", P([(14, 8), (5, 13), (5, 17), (9, 17), (16, 12)])),              # stock
        ("hole", P([(15.5, 12), (17.5, 12), (17.5, 13.5), (15.5, 13.5)])),
    ],
    [  # rocket launcher
        ("metal", P([(7, 8), (40, 8), (40, 13), (7, 13)])),                       # tube
        ("dark", P([(4, 7), (8, 7), (8, 14), (4, 14)])),                          # rear bell
        ("dark", P([(39, 7), (43, 7), (43, 14), (39, 14)])),                      # front cap
        ("dark", P([(20, 13), (23, 13), (22, 19), (19, 19)])),                    # grip
        ("dark", P([(27, 13), (30, 13), (30, 17), (27, 17)])),                    # front grip
        ("metal", P([(21, 5), (28, 5), (28, 8), (21, 8)])),                       # sight
    ],
    [  # C4
        ("dark", P([(11, 9), (37, 9), (37, 18), (11, 18)])),                      # charge block
        ("metal", P([(13, 11), (35, 11), (35, 16), (13, 16)])),                   # wrapping
        ("dark", P([(20, 10), (23, 10), (23, 17), (20, 17)])),                    # tape
        ("dark", P([(27, 10), (30, 10), (30, 17), (27, 17)])),
        ("metal", P([(15, 6), (19, 6), (19, 9), (15, 9)])),                       # detonator
    ],
]

PLATE = P([(4, 2), (47, 2), (43, 22), (0, 22)])            # slanted like the HUD bar
PLATE_IN = P([(5.4, 3.2), (45.4, 3.2), (42.0, 20.8), (1.6, 20.8)])

BORDER = {"idle": (78, 92, 112), "p1": (80, 170, 255), "p2": (255, 150, 50)}
PLATE_TOP = (30, 38, 52)
PLATE_BOT = (12, 16, 24)


def mask(polys):
    m = Image.new("L", (W * S, H * S), 0)
    d = ImageDraw.Draw(m)
    for p in polys:
        d.polygon(p, fill=255)
    return m


def down(m):
    return m.resize((W, H), Image.LANCZOS)


def vgrad(top, bot):
    g = Image.new("RGB", (W, H))
    for y in range(H):
        t = y / (H - 1)
        c = tuple(int(top[i] * (1 - t) + bot[i] * t) for i in range(3))
        ImageDraw.Draw(g).line([(0, y), (W, y)], fill=c)
    return g


def make(weapon, variant):
    held = variant != "idle"
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    if variant == "bare":
        return draw_weapon(img, weapon, True)
    # plate: border colour, then gradient interior
    img.paste(Image.new("RGB", (W, H), BORDER[variant]), (0, 0), down(mask([PLATE])))
    inner = down(mask([PLATE_IN]))
    img.paste(vgrad(PLATE_TOP, PLATE_BOT), (0, 0), inner)
    if held:  # soft glow of the player colour at the bottom of the plate
        b = BORDER[variant]
        glow = vgrad(PLATE_TOP, tuple(int(c * 0.35) for c in b))
        img.paste(glow, (0, 0), inner)
    return draw_weapon(img, weapon, held)


def draw_weapon(img, weapon, held):
    metal = down(mask([p for k, p in weapon if k == "metal"]))
    dark = down(mask([p for k, p in weapon if k == "dark"]))
    hole = down(mask([p for k, p in weapon if k == "hole"]))
    solid = ImageChops.subtract(ImageChops.lighter(metal, dark), hole)
    # dark outline around the whole weapon
    outline = solid.point(lambda v: 255 if v > 90 else 0).filter(ImageFilter.MaxFilter(3))
    img.paste(Image.new("RGB", (W, H), (6, 8, 12)), (0, 0), outline)

    k = 1.0 if held else 0.62
    mt = vgrad(tuple(int(c * k) for c in (245, 246, 250)), tuple(int(c * k) for c in (120, 128, 140)))
    dk = vgrad(tuple(int(c * k) for c in (120, 124, 132)), tuple(int(c * k) for c in (52, 56, 64)))
    img.paste(dk, (0, 0), ImageChops.subtract(dark, hole))
    img.paste(mt, (0, 0), ImageChops.subtract(ImageChops.subtract(metal, dark), hole))
    # highlight line along the top edge of the metal parts
    m1 = metal.point(lambda v: 255 if v > 128 else 0)
    shifted = ImageChops.offset(m1, 0, 1)
    edge = ImageChops.subtract(m1, shifted)
    hl = tuple(int(c * k) for c in (255, 255, 255))
    img.paste(Image.new("RGB", (W, H), hl), (0, 0), edge)
    return img


def quantize(im):
    """1-bit alpha; transparent pixels take the nearest opaque colour (no dark fringe)."""
    px = im.load()
    out = Image.new("RGBA", (W, H))
    o = out.load()
    for y in range(H):
        for x in range(W):
            r, g, b, a = px[x, y]
            if a >= 128:
                o[x, y] = (r, g, b, 255)
            else:
                best = None
                for dy in (-1, 0, 1):
                    for dx in (-1, 0, 1):
                        xx, yy = x + dx, y + dy
                        if 0 <= xx < W and 0 <= yy < H and px[xx, yy][3] >= 128:
                            best = px[xx, yy][:3]
                o[x, y] = (best or (0, 0, 0)) + (0,)
    return out


def to5551(im):
    v = []
    for y in range(H):
        for x in range(W):
            r, g, b, a = im.getpixel((x, y))
            v.append(((r >> 3) << 11) | ((g >> 3) << 6) | ((b >> 3) << 1) | (1 if a else 0))
    return v


VARIANTS = ["idle", "p1", "p2", "bare"]
icons = [[quantize(make(w, var)) for var in VARIANTS] for w in WEAPONS]

with open("src/weapon_icons.h", "w") as f:
    f.write("// Generated by tools/gen_weapon_icons.py. Do not edit.\n#pragma once\n")
    f.write(f"#define WICON_W {W}\n#define WICON_H {H}\n")
    f.write("// [weapon][variant: 0 idle, 1 held by P1, 2 held by P2, 3 weapon only][pixel]\n")
    f.write(f"static const unsigned short weapon_icons[5][4][{W * H}] = {{\n")
    for row in icons:
        f.write("  {\n")
        for im in row:
            f.write("    {" + ",".join(f"0x{x:04X}" for x in to5551(im)) + "},\n")
        f.write("  },\n")
    f.write("};\n")

if len(sys.argv) > 1:
    pv = Image.new("RGBA", (5 * (W + 2) + 4, 3 * (H + 2) + 4), (70, 80, 90, 255))
    for i, row in enumerate(icons):
        for j, im in enumerate(row):
            a = im.copy()
            pv.alpha_composite(a, (2 + i * (W + 2), 2 + j * (H + 2)))
    pv.resize((pv.width * 5, pv.height * 5), Image.NEAREST).save(sys.argv[1])
