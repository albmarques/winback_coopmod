#!/usr/bin/env python3
"""Builds src/weapon_models.h: the 3D weapon models of the game's pause menu (WEAPON page),
read from YOUR ROM, for the live menu's software 3D renderer (src/wmodel.h).
Game data: do not publish the output (it is in .gitignore, like src/map_data.h).

Usage:
  python tools/extract_weapon_models.py path/to/winback.us.z64 [--out src/weapon_models.h]

What it reads (ROM addresses, all found in md/HOOKS.md section 6):
  * display lists of the pause menu: ROM 0x1141C0 (segment 1), model table 0x8010C610[id]
  * vertices:  archive at ROM 0x8534A0, entry = 0x8010C670[id]   (loaded at 0x8027CA00)
  * textures:  archive at ROM 0x8815D0, entry = 0x8010C688[id]   (loaded at 0x8028EA00)
Ids: handgun 1, submachine gun 2, shotgun 3, rocket launcher 22, C4 23.

The display lists are walked with a small F3DEX2 state machine (vertex loads, triangles,
texture loads, tile sizes, combiner, prim colour, geometry mode). Every run of triangles with
the same state becomes a material:
  kind 0  texture * lighting          (CI4 texture decoded to 4-bit indices + 16 colour palette)
  kind 1  primitive colour * lighting
  kind 2  primitive colour, unlit
  kind 3  environment-mapped metal    (the game's reflection texture is not in the ROM files, it
                                       lives in a buffer filled while playing; the renderer
                                       draws a procedural metal instead)
The translucent reflection overlay passes (render mode 0xC8104E50) are left out.
"""
import argparse
import hashlib
import struct
import sys

ap = argparse.ArgumentParser()
ap.add_argument("rom")
ap.add_argument("--out", default="src/weapon_models.h")
ap.add_argument("--ids", default="1,2,3,22,23", help="pause model ids (handgun, SMG, shotgun, rocket launcher, C4)")
a = ap.parse_args()

R = open(a.rom, "rb").read()
if R[0x20:0x34].strip() != b"WIN BACK":
    sys.exit("this is not the WinBack ROM (big-endian .z64 expected)")
if hashlib.sha1(R).hexdigest() != "fa04477b321a95e1f7a2edcac5c09bfa6ef72041":
    print("warning: ROM hash differs from the USA release this tool was written for", file=sys.stderr)


def u32(o):
    return struct.unpack(">I", R[o:o + 4])[0]


def v2r(v):  # code/data segment: vram 0x80025C00 = ROM 0x1000
    return v - 0x80025C00 + 0x1000


DLB, VB, TB = 0x1141C0, 0x8534A0, 0x8815D0
VTX_BASE, TEX_BASE = 0x8027CA00, 0x8028EA00
WEAPONS = [("id%d" % int(i), int(i)) for i in a.ids.split(",")]


def entry(base, i):
    o = u32(base + 4 + 4 * i)
    return base + o, u32(base + 8 + 4 * i) - o


tex_e, tex_len = entry(TB, 1)           # all five weapons use texture entry 1
tex_data = R[tex_e:tex_e + tex_len]

textures = []   # (w, h, packed nibbles, palette[16])
tex_ids = {}


def get_texture(img, pal, w, h, stride):
    key = (img, pal, w, h, stride)
    if key in tex_ids:
        return tex_ids[key]
    io, po = img - TEX_BASE, pal - TEX_BASE
    if not (0 <= io and io + (stride * h) // 2 <= len(tex_data) and 0 <= po and po + 32 <= len(tex_data)):
        tex_ids[key] = -1
        return -1
    idx = []
    for y in range(h):
        row = tex_data[io + (y * stride) // 2: io + (y * stride) // 2 + (w + 1) // 2]
        for x in range(w):
            b = row[x >> 1]
            idx.append((b >> 4) if (x & 1) == 0 else (b & 15))
    packed = bytearray()
    for i in range(0, len(idx), 2):
        packed.append((idx[i] << 4) | (idx[i + 1] if i + 1 < len(idx) else 0))
    palette = list(struct.unpack(">16H", tex_data[po:po + 32]))
    textures.append((w, h, bytes(packed), palette))
    tex_ids[key] = len(textures) - 1
    return tex_ids[key]


materials = []  # (kind, tex, r, g, b, flags, scale_s, scale_t)
mat_ids = {}


def get_material(m):
    if m not in mat_ids:
        mat_ids[m] = len(materials)
        materials.append(m)
    return mat_ids[m]


def build(name, wid):
    dl = u32(v2r(0x8010C610) + 4 * wid) & 0xFFFFFF
    ve, vl = entry(VB, R[v2r(0x8010C670) + wid])
    nv_all = vl // 16
    verts = [struct.unpack(">hhhHhhbbbB", R[ve + 16 * i:ve + 16 * i + 16]) for i in range(nv_all)]
    pos = DLB + dl
    vbuf = [None] * 32
    st = dict(timg=None, img=None, pal=None, tile=None, tsize=None, scale=(0x8000, 0x8000),
              geom=0x230405, oml=0, prim=0xFFFFFFFF, comb=None, ci=None)
    tris = []   # (v0, v1, v2, material)
    skipped = 0
    cur_mat = None
    while True:
        w0, w1 = struct.unpack(">II", R[pos:pos + 8])
        pos += 8
        op = w0 >> 24
        if op == 0xDF:
            break
        if op == 0x01:
            n = (w0 >> 12) & 0xFF
            v0 = ((w0 >> 1) & 0x7F) - n
            for k in range(n):
                vbuf[v0 + k] = (w1 - VTX_BASE) // 16 + k
            continue
        if op in (0x05, 0x06):
            if cur_mat is None:
                cur_mat = material_now(st)
            ts = [[((w0 >> 16) & 0xFF) // 2, ((w0 >> 8) & 0xFF) // 2, (w0 & 0xFF) // 2]]
            if op == 0x06:
                ts.append([((w1 >> 16) & 0xFF) // 2, ((w1 >> 8) & 0xFF) // 2, (w1 & 0xFF) // 2])
            for t in ts:
                if cur_mat == "skip":
                    skipped += 1
                else:
                    tris.append((vbuf[t[0]], vbuf[t[1]], vbuf[t[2]], cur_mat))
            continue
        if op == 0xFD:
            st["timg"] = (w0, w1)
        elif op == 0xF3:                         # LOADBLOCK: the image being loaded
            st["img"] = st["timg"][1]
        elif op == 0xF4:                         # LOADTILE (8-bit images, sub-rectangles): not decoded
            st["img"] = None
        elif op == 0xF0:                         # LOADTLUT: the palette
            st["pal"] = st["timg"][1]
        elif op == 0xF5:
            if ((w1 >> 24) & 7) == 0:
                st["tile"] = (w0, w1)
        elif op == 0xF2:
            if ((w1 >> 24) & 7) == 0:
                st["tsize"] = (w0, w1)
        elif op == 0xD7:
            st["scale"] = (w1 >> 16, w1 & 0xFFFF)
        elif op == 0xD9:
            st["geom"] = (st["geom"] & ~((~w0) & 0xFFFFFF)) | w1
        elif op == 0xE2:
            st["oml"] = w1
        elif op == 0xFA:
            st["prim"] = w1
        elif op == 0xFC:
            st["comb"] = (w0, w1)
        else:
            continue
        cur_mat = None       # state changed: next triangles get a new material
    return verts, tris, skipped


def material_now(st):
    geom, oml = st["geom"], st["oml"]
    if oml == 0xC8104E50:
        return "skip"                                    # translucent reflection overlay
    cull = 1 if geom & 0x400 else 0
    cut = 1 if oml == 0xC8113078 else 0                  # alpha-compare: transparent texels
    pr, pg, pb = (st["prim"] >> 24) & 255, (st["prim"] >> 16) & 255, (st["prim"] >> 8) & 255
    comb = st["comb"][0] if st["comb"] else 0xFC127FFF
    flags = cull | (cut << 1)
    ss, ts = st["scale"]
    if comb == 0xFCFFFFFF:
        return get_material((2, -1, pr, pg, pb, flags, ss, ts))
    if comb == 0xFC3239FF:
        return get_material((1, -1, pr, pg, pb, flags, ss, ts))
    if geom & 0x40000:                                    # texture generation: reflection map
        return get_material((3, -1, 150, 156, 170, flags, ss, ts))
    tile, tsize = st["tile"], st["tsize"]
    if tile is None or tsize is None or st["img"] is None or st["pal"] is None:
        return get_material((1, -1, 120, 120, 120, flags, ss, ts))
    fmt, siz = (tile[0] >> 21) & 7, (tile[0] >> 19) & 3
    line = (tile[0] >> 9) & 0x1FF
    w = ((tsize[1] >> 12) & 0xFFF) // 4 + 1
    h = (tsize[1] & 0xFFF) // 4 + 1
    tid = -1
    if (tsize[0] & 0xFFFFFF) != 0:                        # tile origin not 0: not decoded
        tid = -1
    elif fmt == 2 and siz == 0:                           # CI4, the common case
        tid = get_texture(st["img"], st["pal"], w, h, line * 16)
    if tid < 0:                                           # other formats: plain grey
        return get_material((1, -1, 110, 110, 116, flags, ss, ts))
    return get_material((0, tid, 255, 255, 255, flags, ss, ts))


models = []
for name, wid in WEAPONS:
    verts, tris, skipped = build(name, wid)
    used = sorted({v for t in tris for v in t[:3]})
    remap = {v: i for i, v in enumerate(used)}
    vs = [verts[v] for v in used]
    xs, ys, zs = [v[0] for v in vs], [v[1] for v in vs], [v[2] for v in vs]
    cx, cy, cz = (min(xs) + max(xs)) // 2, (min(ys) + max(ys)) // 2, (min(zs) + max(zs)) // 2
    rad = max(((v[0] - cx) ** 2 + (v[1] - cy) ** 2 + (v[2] - cz) ** 2) ** 0.5 for v in vs)
    rh = max(((v[0] - cx) ** 2 + (v[2] - cz) ** 2) ** 0.5 for v in vs)      # reach while it spins
    hy = max(abs(v[1] - cy) for v in vs)                                   # half height
    models.append((name, vs, [(remap[t[0]], remap[t[1]], remap[t[2]], t[3]) for t in tris], (cx, cy, cz), rad, skipped > 0, rh, hy))
    print("%-8s %5d vertices %5d triangles (%d overlay triangles left out)" % (name, len(vs), len(tris), skipped))
print("%d materials, %d textures" % (len(materials), len(textures)))

o = []
o.append("// Generated by tools/extract_weapon_models.py from your ROM. Game data: do not publish.")
o.append("#ifndef WEAPON_MODELS_H\n#define WEAPON_MODELS_H\n#define WM_OK 1")
o.append("typedef struct { short x, y, z; signed char nx, ny, nz, pad; short s, t; } WmVtx;")
o.append("typedef struct { unsigned short a, b, c, mat; } WmTri;")
o.append("// kind: 0 texture*light, 1 prim*light, 2 prim flat, 3 env metal; flags: 1 cull back, 2 alpha cut")
o.append("typedef struct { unsigned char kind, tex, r, g, b, flags; unsigned short ss, ts; } WmMat;")
o.append("#define WM_COUNT %d" % len(models))
o.append("static const int wm_nv[WM_COUNT] = { %s };" % ", ".join(str(len(m[1])) for m in models))
o.append("static const int wm_nt[WM_COUNT] = { %s };" % ", ".join(str(len(m[2])) for m in models))
o.append("// 1 when the game draws a translucent reflection pass over the model (the renderer adds a sheen)")
o.append("static const unsigned char wm_refl[WM_COUNT] = { %s };" % ", ".join("1" if m[5] else "0" for m in models))
o.append("// centre x, y, z, reach while spinning, half height")
o.append("static const short wm_ctr[WM_COUNT][5] = { %s };" %
         ", ".join("{ %d, %d, %d, %d, %d }" % (m[3][0], m[3][1], m[3][2], int(m[6]) + 1, int(m[7]) + 1) for m in models))
o.append("static const WmMat wm_mat[%d] = {" % len(materials))
for k, m in enumerate(materials):
    o.append("  { %d, %d, %d, %d, %d, %d, %d, %d }," % (m[0], m[1] if m[1] >= 0 else 255, m[2], m[3], m[4], m[5], m[6], m[7]))
o.append("};")
off = 0
offs = []
blob = bytearray()
for w, h, packed, pal in textures:
    offs.append(len(blob))
    blob += packed
o.append("#define WM_TEXN %d" % len(textures))
o.append("static const unsigned char wm_tw[WM_TEXN] = { %s };" % ", ".join(str(t[0]) for t in textures))
o.append("static const unsigned char wm_th[WM_TEXN] = { %s };" % ", ".join(str(t[1]) for t in textures))
o.append("static const unsigned short wm_toff[WM_TEXN] = { %s };" % ", ".join(str(v) for v in offs))
o.append("static const unsigned short wm_tpal[WM_TEXN][16] = {")
for w, h, packed, pal in textures:
    o.append("  { %s }," % ", ".join("0x%04X" % p for p in pal))
o.append("};")
o.append("static const unsigned char wm_tdata[%d] = {" % max(1, len(blob)))
for i in range(0, len(blob), 32):
    o.append("  " + ", ".join(str(b) for b in blob[i:i + 32]) + ",")
o.append("};")
for k, (name, vs, ts, c, r, refl, rh, hy) in enumerate(models):
    o.append("static const WmVtx wm_v%d[%d] = {" % (k, len(vs)))
    for i in range(0, len(vs), 4):
        o.append("  " + " ".join("{%d,%d,%d,%d,%d,%d,0,%d,%d}," % (v[0], v[1], v[2], v[6], v[7], v[8], v[4], v[5]) for v in vs[i:i + 4]))
    o.append("};")
    o.append("static const WmTri wm_t%d[%d] = {" % (k, len(ts)))
    for i in range(0, len(ts), 6):
        o.append("  " + " ".join("{%d,%d,%d,%d}," % t for t in ts[i:i + 6]))
    o.append("};")
o.append("static void wm_get(int k, const WmVtx **v, const WmTri **t) {")
o.append("    switch (k) {")
for k in range(len(models)):
    o.append("    case %d: *v = wm_v%d; *t = wm_t%d; break;" % (k, k, k))
o.append("    }\n}\n#endif")
open(a.out, "w").write("\n".join(o) + "\n")
print("wrote", a.out)
