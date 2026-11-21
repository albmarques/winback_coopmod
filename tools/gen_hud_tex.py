#!/usr/bin/env python3
"""
Gera src/hud_tex_data.h a partir das imagens editaveis em assets/hud/:
  - assets/hud/hud_frame.png    (moldura base 448x96 sem retrato; desenhada por tools/make_hud_frame.py)
  - assets/hud/portrait_p1.png  (retrato do Player 1 - Jean-Luc)
  - assets/hud/portrait_p2.png  (retrato do Player 2 - Engenheiro)
  - assets/hud/hud_p1.png       (HUD completa composta do Player 1 - 448x96, barra de vida vazia)
  - assets/hud/hud_p2.png       (HUD completa composta do Player 2 - 448x96, barra de vida vazia)

Como alterar as imagens:
  1. Voce pode editar diretamente `assets/hud/hud_p1.png` e `assets/hud/hud_p2.png`, OU
  2. Editar `assets/hud/portrait_p1.png`, `assets/hud/portrait_p2.png` ou `assets/hud/hud_frame.png`
     (se algum desses for mais recente que `hud_p1.png`/`hud_p2.png`, ou se passar `--compose`,
     este script remonta `hud_p1.png` e `hud_p2.png` automaticamente).
  3. Rodar `build.bat` (que chama este script automaticamente antes de compilar).

A barra de vida NAO fica na imagem: o trilho vazio e desenhado na moldura e o mod pinta a
vida por cima, em tempo de execucao (linhas 20..27 da textura 224x48, ver src/coop.c).

Requer apenas Pillow.
"""
import os
import sys
from PIL import Image, ImageDraw

BASE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HUD_DIR = os.path.join(BASE_DIR, "assets", "hud")
OUT_HEADER = os.path.join(BASE_DIR, "src", "hud_tex_data.h")

# --- Geometria compartilhada com src/coop.c (hud_bar_x0 / hud_bar_x1, linhas 20..27) ---
BAR_ROW0 = 20
BAR_X0 = [91, 90, 89, 88, 88, 87, 87, 87]
BAR_X1 = [209, 208, 207, 206, 205, 204, 203, 202]
TRACK_RGB = (26, 30, 40)   # cor do trilho vazio (mesma de tools/make_hud_frame.py)

# Janela do retrato em coordenadas 448x96 (px inclusivos)
WIN_X0, WIN_X1 = 14, 78
WIN_Y0, WIN_Y1 = 14, 80

# Recorte do rosto nos retratos de 128x128 (esquerda, topo, direita, base), proporcao 65:67 = janela.
PORTRAIT_CROP = {
    "p1": (20, 4, 110, 97),
    "p2": (14, 4, 110, 103),
}

# Cor de destaque por jogador
ACCENTS = {
    "p1": (70, 165, 255),    # azul
    "p2": (255, 140, 50),    # laranja
}


def add_accents(hc, accent):
    """Aro ao redor do retrato e frisos colorido na placa e embaixo da barra."""
    ar, ag, ab = accent
    dim = (int(ar * 0.55), int(ag * 0.55), int(ab * 0.55), 255)
    full = (ar, ag, ab, 255)
    px = hc.load()

    # aro de 2 px ao redor da janela do retrato (alinhado a pixel, sem serrilhado)
    for t, col in ((0, full), (1, dim)):
        x0, x1 = WIN_X0 - 2 + t, WIN_X1 + 2 - t
        y0, y1 = WIN_Y0 - 2 + t, WIN_Y1 + 2 - t
        for x in range(x0, x1 + 1):
            px[x, y0] = col
            px[x, y1] = col
        for y in range(y0, y1 + 1):
            px[x0, y] = col
            px[x1, y] = col

    # frisos inclinados (supersample 4x para ficarem suaves)
    S = 4
    layer = Image.new("RGBA", (hc.width * S, hc.height * S), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)

    def poly(pts):
        return [(x * 2 * S, y * 2 * S) for x, y in pts]   # coords em px da textura 224x48

    d.polygon(poly([(48, 8.5), (79, 8.5), (79, 9.4), (48, 9.4)]), fill=full)                 # placa
    d.polygon(poly([(86, 32.4), (200.5, 32.4), (199.5, 33.4), (86, 33.4)]), fill=full)       # sob a barra
    layer = layer.resize(hc.size, Image.LANCZOS)
    hc.alpha_composite(layer)


def compose_player_hud(frame_path, portrait_path, out_path, accent, accent_key=None):
    hc = Image.open(frame_path).convert("RGBA")
    if hc.size != (448, 96):
        hc = hc.resize((448, 96), Image.LANCZOS)

    head = Image.open(portrait_path).convert("RGBA")
    # Recorta no rosto (caixa em px do PNG do retrato) e escala para preencher a janela exata.
    crop = PORTRAIT_CROP.get(accent_key, None)
    if crop is not None:
        head = head.crop(crop)
    win_w, win_h = WIN_X1 - WIN_X0 + 1, WIN_Y1 - WIN_Y0 + 1
    head_res = head.resize((win_w, win_h), Image.LANCZOS)

    portrait_layer = Image.new("RGBA", hc.size, (0, 0, 0, 0))
    portrait_layer.paste(head_res, (WIN_X0, WIN_Y0))
    hc.alpha_composite(portrait_layer)

    add_accents(hc, accent)
    hc.save(out_path)


def prepare_for_runtime(hud_img):
    """Reduz para 224x48, garante o trilho vazio exato e limpa a transparencia."""
    img = hud_img.convert("RGBA").copy()
    if img.size != (448, 96):
        img = img.resize((448, 96), Image.LANCZOS)
    img = img.resize((224, 48), Image.LANCZOS)    # Pillow reduz RGBA com alfa pre-multiplicado
    px = img.load()

    # Area que o mod sobrescreve com a vida: base = trilho vazio, exatamente nos mesmos pixels.
    for r in range(8):
        y = BAR_ROW0 + r
        for x in range(BAR_X0[r], BAR_X1[r] + 1):
            px[x, y] = TRACK_RGB + (255,)

    # Alfa de 1 bit (RGBA5551): corta em 50% e deixa pixels opacos com alfa 255.
    w, h = img.size
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            px[x, y] = (r, g, b, 255) if a >= 128 else (r, g, b, 0)
    return bleed_transparent(img)


def bleed_transparent(img, passes=3):
    """Pinta os pixels transparentes com a cor media dos vizinhos opacos.

    O RT64 filtra a textura de forma bilinear: se os texels transparentes forem preto puro
    (0x0000), a borda da silhueta ganha uma franja escura. Com a cor da borda 'vazando' para
    os texels transparentes, a franja some. O alfa continua 0 (bit 0 do RGBA5551).
    """
    px = img.load()
    w, h = img.size
    filled = [[px[x, y][3] != 0 for x in range(w)] for y in range(h)]
    for _ in range(passes):
        updates = []
        for y in range(h):
            for x in range(w):
                if filled[y][x]:
                    continue
                acc = [0, 0, 0]
                n = 0
                for dy in (-1, 0, 1):
                    for dx in (-1, 0, 1):
                        nx, ny = x + dx, y + dy
                        if (dx or dy) and 0 <= nx < w and 0 <= ny < h and filled[ny][nx]:
                            c = px[nx, ny]
                            acc[0] += c[0]
                            acc[1] += c[1]
                            acc[2] += c[2]
                            n += 1
                if n:
                    updates.append((x, y, (acc[0] // n, acc[1] // n, acc[2] // n, 0)))
        for x, y, c in updates:
            px[x, y] = c
            filled[y][x] = True
    return img


def to_rgba5551_array(img, name):
    px = img.load()
    w, h = img.size
    vals = []
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            r5 = (r >> 3) & 0x1F
            g5 = (g >> 3) & 0x1F
            b5 = (b >> 3) & 0x1F
            vals.append((r5 << 11) | (g5 << 6) | (b5 << 1) | (1 if a >= 128 else 0))
    lines = [f"static const unsigned short {name}[224 * 48] = {{"]
    for i in range(0, len(vals), 16):
        chunk = ", ".join(f"0x{v:04X}" for v in vals[i : i + 16])
        lines.append(f"    {chunk},")
    lines.append("};")
    return "\n".join(lines)


def needs_compose(frame_path, port_path, hud_path, force):
    if force or not os.path.exists(hud_path):
        return True
    if not os.path.exists(frame_path) or not os.path.exists(port_path):
        return False
    hud_mtime = os.path.getmtime(hud_path)
    return os.path.getmtime(frame_path) > hud_mtime or os.path.getmtime(port_path) > hud_mtime


def main():
    force_compose = "--compose" in sys.argv
    frame_path = os.path.join(HUD_DIR, "hud_frame.png")
    p1_port = os.path.join(HUD_DIR, "portrait_p1.png")
    p2_port = os.path.join(HUD_DIR, "portrait_p2.png")
    p1_hud = os.path.join(HUD_DIR, "hud_p1.png")
    p2_hud = os.path.join(HUD_DIR, "hud_p2.png")

    if needs_compose(frame_path, p1_port, p1_hud, force_compose):
        compose_player_hud(frame_path, p1_port, p1_hud, ACCENTS["p1"], "p1")
        print("  Composed assets/hud/hud_p1.png")

    if needs_compose(frame_path, p2_port, p2_hud, force_compose):
        compose_player_hud(frame_path, p2_port, p2_hud, ACCENTS["p2"], "p2")
        print("  Composed assets/hud/hud_p2.png")

    p1_224 = prepare_for_runtime(Image.open(p1_hud))
    p2_224 = prepare_for_runtime(Image.open(p2_hud))

    header = (
        "// Auto-generated from assets/hud/hud_p1.png and assets/hud/hud_p2.png\n"
        "// Edit the PNGs in assets/hud/ and run build.bat to update!\n"
        "#ifndef HUD_TEX_DATA_H\n"
        "#define HUD_TEX_DATA_H\n\n"
        + to_rgba5551_array(p1_224, "hud_p1_base")
        + "\n\n"
        + to_rgba5551_array(p2_224, "hud_p2_base")
        + "\n\n#endif\n"
    )
    with open(OUT_HEADER, "w", encoding="utf-8") as f:
        f.write(header)
    print("  Generated src/hud_tex_data.h from assets/hud/")


if __name__ == "__main__":
    main()
