#!/usr/bin/env python3
"""
Gera src/hud_tex_data.h a partir das imagens editaveis em assets/hud/:
  - assets/hud/hud_frame.png    (moldura base 448x96 sem retrato)
  - assets/hud/portrait_p1.png  (retrato do Player 1 - Jean-Luc)
  - assets/hud/portrait_p2.png  (retrato do Player 2 - Engenheiro)
  - assets/hud/hud_p1.png       (HUD completa composta do Player 1 - 448x96)
  - assets/hud/hud_p2.png       (HUD completa composta do Player 2 - 448x96)

Como alterar as imagens:
  1. Voce pode editar diretamente `assets/hud/hud_p1.png` e `assets/hud/hud_p2.png`, OU
  2. Editar `assets/hud/portrait_p1.png`, `assets/hud/portrait_p2.png` ou `assets/hud/hud_frame.png`
     (se algum desses for mais recente que `hud_p1.png`/`hud_p2.png`, ou se passar `--compose`,
     este script remonta `hud_p1.png` e `hud_p2.png` automaticamente).
  3. Rodar `build.bat` (que chama este script automaticamente antes de compilar).
"""
import os
import sys
from PIL import Image

BASE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HUD_DIR = os.path.join(BASE_DIR, "assets", "hud")
OUT_HEADER = os.path.join(BASE_DIR, "src", "hud_tex_data.h")


def compose_player_hud(frame_path, portrait_path, out_path):
    hc = Image.open(frame_path).convert("RGBA")
    if hc.size != (448, 96):
        hc = hc.resize((448, 96), Image.LANCZOS)
    px = hc.load()

    head = Image.open(portrait_path).convert("RGBA")
    hw, hh = head.size
    scale = 76.0 / max(hh, 1)
    nw, nh = int(round(hw * scale)), int(round(hh * scale))
    head_res = head.resize((nw, nh), Image.LANCZOS)

    ox = 14 + (65 - nw) // 2
    oy = 88 - nh

    portrait_layer = Image.new("RGBA", hc.size, (0, 0, 0, 0))
    portrait_layer.paste(head_res, (ox, oy))
    ppx = portrait_layer.load()
    for y in range(hc.size[1]):
        for x in range(hc.size[0]):
            if not (14 <= x <= 78 and 14 <= y <= 81):
                ppx[x, y] = (0, 0, 0, 0)

    hc.alpha_composite(portrait_layer)
    for y in range(14, 82):
        for x in range(14, 79):
            if x in (14, 78) or y in (14, 81):
                r, g, b, a = px[x, y]
                px[x, y] = (int(r * 0.65), int(g * 0.65), int(b * 0.65), 255)

    hc.save(out_path)


def prepare_for_runtime(hud_img):
    """Remove a barra verde estatica da imagem para que o C desenhe o HP dinamico."""
    img = hud_img.convert("RGBA").copy()
    if img.size != (448, 96):
        img = img.resize((448, 96), Image.LANCZOS)
    px = img.load()
    for y in range(38, 58):
        for x in range(170, 425):
            r, g, b, a = px[x, y]
            if g > r + 12 and g > b + 12:
                px[x, y] = (15, 17, 16, 255)
    return img.resize((224, 48), Image.LANCZOS)


def to_rgba5551_array(img, name):
    px = img.load()
    w, h = img.size
    vals = []
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            if a < 128:
                vals.append(0x0000)
            else:
                r5 = (r >> 3) & 0x1F
                g5 = (g >> 3) & 0x1F
                b5 = (b >> 3) & 0x1F
                vals.append((r5 << 11) | (g5 << 6) | (b5 << 1) | 1)
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
        compose_player_hud(frame_path, p1_port, p1_hud)
        print("  Composed assets/hud/hud_p1.png")

    if needs_compose(frame_path, p2_port, p2_hud, force_compose):
        compose_player_hud(frame_path, p2_port, p2_hud)
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
