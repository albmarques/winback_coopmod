#!/usr/bin/env python3
"""
Desenha a barra de vida do chefe (co-op) no mesmo estilo da HUD dos jogadores e gera
src/boss_tex_data.h.

Usa as mesmas cores, frisos de aco, poco e letras retas inclinadas de tools/make_hud_frame.py:
placa "BOSS" a esquerda (mais alta, como a placa LIFE) e a barra longa a direita, com a ponta
inclinada da barra de vida. O destaque e vermelho (P1 azul, P2 laranja).

Textura de runtime: 288x38 RGBA5551 (desenhada em 0.5: 144x19 na tela 320x240).
A vida e pintada pelo mod nas linhas BAR_ROW0..BAR_ROW0+7 entre BAR_X0[r] e BAR_X1[r]
(mesmos valores de src/boss_bar.h).

Ferramenta de desenvolvimento (numpy + scipy + Pillow), como make_hud_frame.py:
    python tools/make_boss_hud.py
Grava assets/hud/hud_boss.png (576x76, 2x) e src/boss_tex_data.h.
"""
import os
import sys

import numpy as np
from PIL import Image, ImageDraw
from scipy.ndimage import distance_transform_edt

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_hud_frame as hf   # noqa: E402
import gen_hud_tex as gh      # noqa: E402

BASE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_PNG = os.path.join(BASE_DIR, "assets", "hud", "hud_boss.png")
OUT_HEADER = os.path.join(BASE_DIR, "src", "boss_tex_data.h")

S = hf.S
W, H = 288, 38

# Linhas da vida (mesma inclinacao da barra dos jogadores: esquerda -0.5 px/linha, direita -1 px/linha)
BAR_ROW0 = 14
BAR_X0 = [72, 71, 70, 69, 69, 68, 68, 68]
BAR_X1 = [272, 271, 270, 269, 268, 267, 266, 265]
WELL_Y0, WELL_Y1 = 11.0, 25.0

# Silhueta: placa (y 1..37) e corpo da barra (y 6..31) com a ponta inclinada.
SILHOUETTE = [
    (4, 1), (56, 1), (62, 6), (284.5, 6), (286.6, 8.4), (264.2, 31), (62, 31), (56, 36.5),
    (4, 36.5), (1, 33.5), (1, 4),
]

ACCENT = (235, 52, 40)          # vermelho (P1 70,165,255 / P2 255,140,50)


def sp(pts):
    return [(x * S, y * S) for x, y in pts]


def well_poly(inset):
    def left(y):
        return BAR_X0[0] - 0.5 * (y - BAR_ROW0) - hf.WELL_MARGIN + inset

    def right(y):
        return (BAR_X1[0] + 1) - 1.0 * (y - BAR_ROW0) + hf.WELL_MARGIN - inset

    y0, y1 = WELL_Y0 + inset, WELL_Y1 - inset
    return [(left(y0), y0), (right(y0), y0), (right(y1), y1), (left(y1), y1)]


def draw_boss_text(layer, color, dx, dy):
    """BOSS com os mesmos tracos retos inclinados do LIFE."""
    d = ImageDraw.Draw(layer)
    top, bot = BAR_ROW0 + 0.4, BAR_ROW0 + 7.6
    t = 1.65
    shear = 0.22
    ymid = (top + bot) / 2

    def sx(x, y):
        return x + (ymid - y) * shear + dx

    def rect(x0, y0, x1, y1):
        pts = [(sx(x0, y0), y0 + dy), (sx(x1, y0), y0 + dy), (sx(x1, y1), y1 + dy), (sx(x0, y1), y1 + dy)]
        d.polygon(sp(pts), fill=color)

    w = 5.6
    gap = 2.0
    x = 31.0 - (4 * w + 3 * gap) / 2      # centrado na placa (x 1..56)
    # B: haste, tres barras, duas hastes a direita com cantos cortados
    rect(x, top, x + t, bot)
    rect(x, top, x + w - 0.9, top + t)
    rect(x, ymid - t / 2, x + w - 0.6, ymid + t / 2)
    rect(x, bot - t, x + w - 0.9, bot)
    rect(x + w - t, top + 0.8, x + w, ymid - 0.2)
    rect(x + w - t + 0.3, ymid + 0.2, x + w + 0.3, bot - 0.8)
    x += w + gap
    # O
    rect(x, top + 0.8, x + t, bot - 0.8)
    rect(x + w - t, top + 0.8, x + w, bot - 0.8)
    rect(x + 0.7, top, x + w - 0.7, top + t)
    rect(x + 0.7, bot - t, x + w - 0.7, bot)
    x += w + gap
    # S (duas vezes)
    for _ in range(2):
        rect(x + 0.7, top, x + w, top + t)
        rect(x, top + 0.8, x + t, ymid)
        rect(x + 0.7, ymid - t / 2, x + w - 0.7, ymid + t / 2)
        rect(x + w - t, ymid, x + w, bot - 0.8)
        rect(x, bot - t, x + w - 0.7, bot)
        x += w + gap


def draw_frame():
    sw, sh = W * S, H * S
    mask_img = Image.new("L", (sw, sh), 0)
    ImageDraw.Draw(mask_img).polygon(sp(SILHOUETTE), fill=255)
    mask = np.array(mask_img) > 0
    dist = distance_transform_edt(mask)

    rgb = np.zeros((sh, sw, 3), dtype=np.float32)
    steel = hf.vgrad(hf.STEEL_TOP, hf.STEEL_BOT, 0, (H - 1) * S, (sh, sw))
    fill = hf.vgrad(hf.FILL_TOP, hf.FILL_BOT, 0, (H - 1) * S, (sh, sw))
    band_outline = mask & (dist <= hf.OUTLINE_W * S)
    band_steel = mask & (dist > hf.OUTLINE_W * S) & (dist <= (hf.OUTLINE_W + hf.STEEL_W) * S)
    band_fill = mask & (dist > (hf.OUTLINE_W + hf.STEEL_W) * S)
    rgb[band_outline] = np.array(hf.OUTLINE, dtype=np.float32)
    rgb[band_steel] = steel[band_steel]
    rgb[band_fill] = fill[band_fill]
    img = Image.fromarray(np.dstack([rgb.clip(0, 255).astype(np.uint8), (mask * 255).astype(np.uint8)]), "RGBA")
    d = ImageDraw.Draw(img)

    # poco + trilho
    d.polygon(sp(well_poly(0.0)), fill=hf.WELL_STROKE + (255,))
    d.polygon(sp(well_poly(0.8)), fill=hf.WELL_FILL + (255,))
    for r in range(8):
        y = BAR_ROW0 + r
        d.rectangle([BAR_X0[r] * S, y * S, (BAR_X1[r] + 1) * S - 1, (y + 1) * S - 1], fill=hf.TRACK + (255,))

    # frisos vermelhos: topo da placa e embaixo da barra (como os azuis/laranja dos jogadores)
    ar, ag, ab = ACCENT
    full = (ar, ag, ab, 255)
    dim = (int(ar * 0.55), int(ag * 0.55), int(ab * 0.55), 255)
    d.polygon(sp([(8, 4.5), (54, 4.5), (54, 5.4), (8, 5.4)]), fill=full)
    d.polygon(sp([(8, 33.0), (54, 33.0), (54, 33.9), (8, 33.9)]), fill=dim)
    d.polygon(sp([(67, 26.4), (266.5, 26.4), (265.5, 27.4), (67, 27.4)]), fill=full)
    # pequenos chanfros vermelhos na ponta, como marcas de perigo
    for k in range(3):
        x = 276.0 - k * 3.2
        d.polygon(sp([(x, 9.2), (x + 1.4, 9.2), (x - 1.6, 12.2), (x - 3.0, 12.2)]), fill=dim)

    # texto
    sh_layer = Image.new("RGBA", (sw, sh), (0, 0, 0, 0))
    draw_boss_text(sh_layer, hf.TEXT_SHADOW + (255,), 0.45, 0.45)
    img.alpha_composite(sh_layer)
    tx = Image.new("RGBA", (sw, sh), (0, 0, 0, 0))
    draw_boss_text(tx, hf.TEXT + (255,), 0.0, 0.0)
    img.alpha_composite(tx)
    return img.resize((W * 2, H * 2), Image.LANCZOS)


def runtime(img2x):
    img = img2x.resize((W, H), Image.LANCZOS)
    px = img.load()
    for r in range(8):
        y = BAR_ROW0 + r
        for x in range(BAR_X0[r], BAR_X1[r] + 1):
            px[x, y] = gh.TRACK_RGB + (255,)
    for y in range(H):
        for x in range(W):
            r, g, b, a = px[x, y]
            px[x, y] = (r, g, b, 255) if a >= 128 else (r, g, b, 0)
    return gh.bleed_transparent(img)


def header(img):
    px = img.load()
    vals = []
    for y in range(H):
        for x in range(W):
            r, g, b, a = px[x, y]
            vals.append(((r >> 3) << 11) | ((g >> 3) << 6) | ((b >> 3) << 1) | (1 if a >= 128 else 0))
    out = [
        "// Auto-generated by tools/make_boss_hud.py (assets/hud/hud_boss.png). Do not edit.",
        "#ifndef BOSS_TEX_DATA_H",
        "#define BOSS_TEX_DATA_H",
        f"#define BOSS_W {W}",
        f"#define BOSS_H {H}",
        f"#define BOSS_BAR_ROW0 {BAR_ROW0}",
        "static const unsigned char boss_bar_x0[8] = { " + ", ".join(map(str, BAR_X0)) + " };",
        "static const unsigned short boss_bar_x1[8] = { " + ", ".join(map(str, BAR_X1)) + " };",
        f"static const unsigned short boss_base[{W} * {H}] = {{",
    ]
    for i in range(0, len(vals), 16):
        out.append("    " + ", ".join(f"0x{v:04X}" for v in vals[i:i + 16]) + ",")
    out += ["};", "#endif", ""]
    return "\n".join(out)


def main():
    img2x = draw_frame()
    img2x.save(OUT_PNG)
    rt = runtime(img2x)
    with open(OUT_HEADER, "w", encoding="utf-8") as f:
        f.write(header(rt))
    print("  Wrote", os.path.relpath(OUT_PNG, BASE_DIR), img2x.size, "and", os.path.relpath(OUT_HEADER, BASE_DIR))


if __name__ == "__main__":
    main()
