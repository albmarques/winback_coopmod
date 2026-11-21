#!/usr/bin/env python3
"""
Desenha a moldura do HUD do co-op (assets/hud/hud_frame.png, 448x96).

Ferramenta de desenvolvimento (precisa de numpy + scipy + Pillow). Nao e chamada pelo
build.bat: o PNG gerado fica versionado em assets/hud/.

A moldura e desenhada em 8x de supersampling no espaco de coordenadas da textura de
runtime (224x48) e reduzida com filtro Lanczos, por isso as bordas saem limpas e sem
serrilhado ou halo. A geometria da barra de vida (linhas 20..27, hud_bar_x0/x1) segue
exatamente o que src/coop.c pinta por cima da textura.

Uso:
    python tools/make_hud_frame.py
    python tools/gen_hud_tex.py --compose
"""
import os

import numpy as np
from PIL import Image, ImageDraw
from scipy.ndimage import distance_transform_edt

BASE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(BASE_DIR, "assets", "hud", "hud_frame.png")

S = 8                      # supersampling
W, H = 224, 48             # espaco da textura de runtime
OUT_W, OUT_H = 448, 96     # tamanho do PNG (2x a textura de runtime)

# Mesmos valores de src/coop.c (hud_bar_x0 / hud_bar_x1, linhas 20..27).
BAR_ROW0 = 20
BAR_X0 = [91, 90, 89, 88, 88, 87, 87, 87]
BAR_X1 = [209, 208, 207, 206, 205, 204, 203, 202]

# Cores
OUTLINE = (5, 7, 11)
STEEL_TOP = (196, 204, 216)
STEEL_BOT = (98, 107, 120)
FILL_TOP = (30, 35, 46)
FILL_BOT = (14, 17, 23)
WELL_STROKE = (58, 66, 79)
WELL_FILL = (8, 10, 14)
TRACK = (26, 30, 40)
WINDOW_TOP = (20, 28, 46)
WINDOW_BOT = (9, 13, 22)
TEXT = (228, 234, 240)
TEXT_SHADOW = (4, 5, 8)

# Silhueta externa (sentido horario). Escada de 3 niveis: retrato > placa LIFE > barra.
# A ponta direita segue a mesma inclinacao (-1 px por linha) da barra de vida.
SILHOUETTE = [
    (4, 1), (40, 1), (46, 7), (80, 7), (85, 12), (220.5, 12), (222.6, 14.4),
    (201.2, 36), (85, 36), (80, 41), (46, 41), (40, 47), (4, 47), (1, 44), (1, 4),
]

OUTLINE_W = 0.75           # contorno escuro externo (px da textura)
STEEL_W = 1.25             # friso de aco
WELL_Y0, WELL_Y1 = 17.0, 31.0
WELL_MARGIN = 2.0

WINDOW = (7.0, 7.0, 39.5, 40.5)   # janela do retrato (px da textura) = (14,14)-(79,81) em 448


def sp(pts):
    return [(x * S, y * S) for x, y in pts]


def vgrad(top, bot, y0, y1, shape):
    """Gradiente vertical (em px de supersample) como array HxWx3."""
    h, w = shape
    ys = np.clip((np.arange(h) - y0) / max(y1 - y0, 1), 0, 1)[:, None, None]
    top = np.array(top, dtype=np.float32)[None, None, :]
    bot = np.array(bot, dtype=np.float32)[None, None, :]
    return np.broadcast_to(top + (bot - top) * ys, (h, w, 3))


def well_poly(inset):
    """Paralelogramo do 'poco' da barra de vida, acompanhando a inclinacao de x0/x1."""
    def left(y):   # borda esquerda: 91 -> 87 em 8 linhas (-0.5 px/linha)
        return BAR_X0[0] - 0.5 * (y - BAR_ROW0) - WELL_MARGIN + inset

    def right(y):  # borda direita: 210 -> 203 (-1 px/linha)
        return (BAR_X1[0] + 1) - 1.0 * (y - BAR_ROW0) + WELL_MARGIN - inset

    y0, y1 = WELL_Y0 + inset, WELL_Y1 - inset
    return [(left(y0), y0), (right(y0), y0), (right(y1), y1), (left(y1), y1)]


def draw_life_text(layer, color, dx, dy):
    """Palavra LIFE desenhada com tracos retos e leve inclinacao (italico)."""
    d = ImageDraw.Draw(layer)
    # coordenadas no espaco da textura (224x48)
    top, bot = 20.4, 27.6
    t = 1.65                    # espessura do traco
    shear = 0.22                # inclinacao
    ymid = (top + bot) / 2

    def sx(x, y):
        return x + (ymid - y) * shear + dx

    def rect(x0, y0, x1, y1):
        pts = [(sx(x0, y0), y0 + dy), (sx(x1, y0), y0 + dy), (sx(x1, y1), y1 + dy), (sx(x0, y1), y1 + dy)]
        d.polygon(sp(pts), fill=color)

    x = 52.0
    wl = 5.4                    # largura de L, F, E
    # L
    rect(x, top, x + t, bot)
    rect(x, bot - t, x + wl, bot)
    x += wl + 2.0
    # I
    rect(x, top, x + t, bot)
    x += t + 2.0
    # F
    rect(x, top, x + t, bot)
    rect(x, top, x + wl, top + t)
    rect(x, ymid - t / 2, x + wl - 0.8, ymid + t / 2)
    x += wl + 2.0
    # E
    rect(x, top, x + t, bot)
    rect(x, top, x + wl, top + t)
    rect(x, ymid - t / 2, x + wl - 0.8, ymid + t / 2)
    rect(x, bot - t, x + wl, bot)


def main():
    sw, sh = W * S, H * S

    # --- mascara da silhueta e bandas (contorno / aco / miolo) por distancia a borda ---
    mask_img = Image.new("L", (sw, sh), 0)
    ImageDraw.Draw(mask_img).polygon(sp(SILHOUETTE), fill=255)
    mask = np.array(mask_img) > 0
    dist = distance_transform_edt(mask)     # distancia (px de supersample) ate o fundo

    rgb = np.zeros((sh, sw, 3), dtype=np.float32)
    steel = vgrad(STEEL_TOP, STEEL_BOT, 0, 47 * S, (sh, sw))
    fill = vgrad(FILL_TOP, FILL_BOT, 0, 47 * S, (sh, sw))
    band_outline = mask & (dist <= OUTLINE_W * S)
    band_steel = mask & (dist > OUTLINE_W * S) & (dist <= (OUTLINE_W + STEEL_W) * S)
    band_fill = mask & (dist > (OUTLINE_W + STEEL_W) * S)
    rgb[band_outline] = np.array(OUTLINE, dtype=np.float32)
    rgb[band_steel] = steel[band_steel]
    rgb[band_fill] = fill[band_fill]

    img = Image.fromarray(np.dstack([rgb.clip(0, 255).astype(np.uint8), (mask * 255).astype(np.uint8)]), "RGBA")
    d = ImageDraw.Draw(img)

    # --- janela do retrato (fundo; o retrato e colado depois por gen_hud_tex.py) ---
    wx0, wy0, wx1, wy1 = WINDOW
    win = vgrad(WINDOW_TOP, WINDOW_BOT, wy0 * S, wy1 * S, (sh, sw)).clip(0, 255).astype(np.uint8)
    win_mask = Image.new("L", (sw, sh), 0)
    ImageDraw.Draw(win_mask).rectangle([wx0 * S, wy0 * S, wx1 * S - 1, wy1 * S - 1], fill=255)
    img.paste(Image.fromarray(np.dstack([win, np.full((sh, sw), 255, np.uint8)]), "RGBA"), (0, 0), win_mask)

    # --- poco da barra de vida + trilho (area exata que o C sobrescreve) ---
    d.polygon(sp(well_poly(0.0)), fill=WELL_STROKE + (255,))
    d.polygon(sp(well_poly(0.8)), fill=WELL_FILL + (255,))
    for r in range(8):
        y = BAR_ROW0 + r
        d.rectangle([BAR_X0[r] * S, y * S, (BAR_X1[r] + 1) * S - 1, (y + 1) * S - 1], fill=TRACK + (255,))

    # --- texto LIFE (sombra + frente) ---
    text_shadow = Image.new("RGBA", (sw, sh), (0, 0, 0, 0))
    draw_life_text(text_shadow, TEXT_SHADOW + (255,), 0.45, 0.45)
    img.alpha_composite(text_shadow)
    text = Image.new("RGBA", (sw, sh), (0, 0, 0, 0))
    draw_life_text(text, TEXT + (255,), 0.0, 0.0)
    img.alpha_composite(text)

    # Lanczos em RGBA do Pillow reduz com alfa pre-multiplicado: sem halo escuro nas bordas.
    out = img.resize((OUT_W, OUT_H), Image.LANCZOS)
    out.save(OUT)
    print("  Wrote", os.path.relpath(OUT, BASE_DIR), out.size)


if __name__ == "__main__":
    main()
