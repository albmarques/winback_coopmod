#!/usr/bin/env python3
"""Builds src/weapon_spin.h (the rotating weapons of the live menu's WEAPON page) from YOUR
own screen recording of the game's pause menu (WEAPON page). Game imagery: do not publish
the output (it is in .gitignore).

Recording: the game shown 4:3 in the middle of a 16:9 capture (any size, e.g. 1920x1080),
WEAPON page open, the cursor resting on each weapon for at least one full turn (~11 s).
Pass the time (in seconds) where each weapon starts: the moment its name appears.

Usage:
  python tools/gen_weapon_spin.py VIDEO --handgun 33.07 --smg 20.33 --shotgun 8.17 [--preview out.png]

Steps: crop the model area, rebuild the cyan glow behind the model (radial fit over the
pixels where the glow shows), cut the model out of it, scale every frame with one common
factor, quantize to a 255-colour palette per weapon and pack transparent runs.
"""
import argparse
import subprocess
import sys

import numpy as np
from PIL import Image, ImageFilter

ap = argparse.ArgumentParser()
ap.add_argument("video")
ap.add_argument("--handgun", type=float, required=True)
ap.add_argument("--smg", type=float, required=True)
ap.add_argument("--shotgun", type=float, required=True)
ap.add_argument("--period", type=float, default=10.8, help="seconds per full turn")
ap.add_argument("--frames", type=int, default=48)
ap.add_argument("--box", default="112x56", help="max size on the menu (pixels)")
ap.add_argument("--preview")
ap.add_argument("--out", default="src/weapon_spin.h")
a = ap.parse_args()

FPS = 30
BW, BH = (int(v) for v in a.box.split("x"))

# capture size and the model area (relative to the 4:3 game picture)
probe = subprocess.run(["ffprobe", "-v", "error", "-select_streams", "v:0", "-show_entries",
                        "stream=width,height", "-of", "csv=p=0", a.video],
                       capture_output=True, text=True, check=True).stdout.split(",")
VW, VH = int(probe[0]), int(probe[1])
gw = VH * 4 / 3
gx0 = (VW - gw) / 2
cx0 = int(gx0 + gw * 380 / 1440)
cy0 = int(VH * 190 / 1080)
cw = int(gw * 1020 / 1440) // 2 * 2
ch = int(VH * 580 / 1080) // 2 * 2
W, H = cw // 2, ch // 2

order = [("handgun", a.handgun), ("smg", a.smg), ("shotgun", a.shotgun)]
n_per = int(round(a.period * FPS))


def grab(t0):
    raw = subprocess.run(["ffmpeg", "-v", "error", "-ss", "%.3f" % (t0 + 0.12), "-t", "%.3f" % a.period,
                          "-i", a.video, "-vf", "fps=%d,crop=%d:%d:%d:%d,scale=%d:%d:flags=area"
                          % (FPS, cw, ch, cx0, cy0, W, H),
                          "-pix_fmt", "rgb24", "-f", "rawvideo", "-"], capture_output=True, check=True).stdout
    f = np.frombuffer(raw, np.uint8).reshape(-1, H, W, 3)
    return f[:n_per]


clips = {k: grab(t) for k, t in order}
print("frames per turn:", {k: len(v) for k, v in clips.items()})

# ---- background: the cyan glow (radial) inside, plain median outside ----
sample = np.concatenate([v[::6] for v in clips.values()]).astype(np.int16)
r_, g_, b_ = sample[..., 0], sample[..., 1], sample[..., 2]
teal = ((g_ + b_) // 2 - r_ > 10) & (g_ > 20)
cnt = teal.sum(0)
fm = np.where(teal[..., None], sample, -1).astype(np.float32)
fm[fm < 0] = np.nan
with np.errstate(all="ignore"):
    import warnings
    warnings.simplefilter("ignore")
    med = np.nanmedian(fm, axis=0)
trust = cnt >= 15
ys, xs = np.mgrid[0:H, 0:W]


def fit(cx, cy, full=False):
    ri = (np.sqrt((xs - cx) ** 2 + (ys - cy) ** 2) * 2).astype(int)
    rt = ri[trust]
    n = np.bincount(rt, None, 2400)
    prof = np.zeros((2400, 3))
    for c in range(3):
        prof[:, c] = np.bincount(rt, med[..., c][trust], 2400) / np.maximum(n, 1)
    if full:
        ok = n > 5
        for c in range(3):
            prof[:, c] = np.interp(np.arange(2400), np.where(ok)[0], prof[ok, c])
        return prof[ri]
    return np.abs(prof[ri] - med)[trust].mean()


ccx, ccy = W * 0.475, H * 0.476
best = (1e9, ccx, ccy)
for dx in range(-12, 13, 3):
    for dy in range(-12, 13, 3):
        e = fit(ccx + dx, ccy + dy)
        if e < best[0]:
            best = (e, ccx + dx, ccy + dy)
_, gcx, gcy = best
glow = fit(gcx, gcy, True)
allmed = np.median(sample[::2], axis=0).astype(float)
rr = np.sqrt((xs - gcx) ** 2 + (ys - gcy) ** 2)
bg = np.where((rr < H * 0.345)[..., None], glow, allmed)
print("glow centre %.1f %.1f (fit error %.2f)" % (gcx, gcy, best[0]))

# ---- cut out every frame ----
N = a.frames
cut = {}
for k, clip in clips.items():
    fr = []
    for j in range(N):
        f = clip[min(len(clip) - 1, int(round(j * len(clip) / N)))].astype(float)
        d = np.abs(f - bg).sum(-1)
        m = Image.fromarray(((d > 30) * 255).astype(np.uint8)).filter(ImageFilter.MedianFilter(3))
        fr.append((f, np.array(m) > 0))
    cut[k] = fr

# common bounding box (ignore specks: columns/rows need a few pixels across the turn)
acc = sum(m.astype(int) for fr in cut.values() for _, m in fr)
cols = np.where((acc > 2).sum(0) > 2)[0]
rows = np.where((acc > 2).sum(1) > 2)[0]
bx0, bx1, by0, by1 = cols.min(), cols.max() + 1, rows.min(), rows.max() + 1
s = min(BW / (bx1 - bx0), BH / (by1 - by0))
OW, OH = int(round((bx1 - bx0) * s)), int(round((by1 - by0) * s))
print("box", bx0, bx1, by0, by1, "scale %.3f" % s, "out %dx%d" % (OW, OH))

frames = {}
for k, fr in cut.items():
    out = []
    for f, m in fr:
        rgba = np.dstack([f, m * 255.0])[by0:by1, bx0:bx1]
        pre = rgba.copy()
        pre[..., :3] *= rgba[..., 3:4] / 255.0          # premultiply, then scale down
        im = Image.fromarray(pre.astype(np.uint8), "RGBA").resize((OW, OH), Image.BOX)
        p = np.asarray(im).astype(float)
        al = p[..., 3:4]
        rgb = np.where(al > 0, p[..., :3] * 255.0 / np.maximum(al, 1), 0)
        out.append(np.dstack([np.clip(rgb, 0, 255), al]).astype(np.uint8))
    frames[k] = out

# ---- palette per weapon + transparent runs ----
def to5551(c):
    return ((int(c[0]) >> 3) << 11) | ((int(c[1]) >> 3) << 6) | ((int(c[2]) >> 3) << 1) | 1


pals, data, offs = [], bytearray(), []
for k, _ in order:
    fr = frames[k]
    solid = [f[..., 3] >= 128 for f in fr]
    stack = np.concatenate([f[..., :3][m] for f, m in zip(fr, solid)])
    q = Image.fromarray(stack.reshape(-1, 1, 3)).quantize(255, method=Image.MEDIANCUT, dither=Image.NONE)
    pal = np.array(q.getpalette()[:255 * 3]).reshape(-1, 3)
    pals.append([0] + [to5551(c) for c in pal])
    pimg = Image.new("P", (1, 1))
    pimg.putpalette(list(pal.ravel()) + list(pal[-1]))   # 256th entry repeats the last colour
    fo = []
    for f, m in zip(fr, solid):
        idx = np.asarray(Image.fromarray(f[..., :3]).quantize(palette=pimg, dither=Image.NONE)).astype(int)
        idx = np.minimum(idx, 254) + 1
        idx[~m] = 0
        flat = idx.ravel()
        fo.append(len(data))
        i = 0
        while i < len(flat):                 # [skip][count][count indices] ...
            sk = 0
            while i < len(flat) and flat[i] == 0 and sk < 255:
                sk += 1
                i += 1
            run = []
            while i < len(flat) and flat[i] != 0 and len(run) < 255:
                run.append(flat[i])
                i += 1
            data += bytes([sk, len(run)] + run)
    offs.append(fo)

with open(a.out, "w") as f:
    f.write("// Generated by tools/gen_weapon_spin.py from a screen recording. Do not edit or publish.\n")
    f.write("#pragma once\n#define WSPIN_OK 1\n")
    f.write("#define WSPIN_N %d\n#define WSPIN_W %d\n#define WSPIN_H %d\n" % (N, OW, OH))
    f.write("#define WSPIN_TICKS %d   // game frames per full turn at 30 per second\n" % n_per)
    f.write("// [weapon: 0 handgun, 1 submachine gun, 2 shotgun][colour], index 0 = transparent\n")
    f.write("static const unsigned short wspin_pal[3][256] = {\n")
    for p in pals:
        f.write("  {" + ",".join("0x%04X" % v for v in p) + "},\n")
    f.write("};\nstatic const unsigned int wspin_off[3][WSPIN_N] = {\n")
    for o in offs:
        f.write("  {" + ",".join(map(str, o)) + "},\n")
    f.write("};\n// per frame: runs of [skip transparent][n][n palette indices] until W*H pixels\n")
    f.write("static const unsigned char wspin_data[%d] = {" % len(data))
    for i in range(0, len(data), 64):
        f.write("\n  " + ",".join(map(str, data[i:i + 64])) + ",")
    f.write("\n};\n")
print("%s: %d frames x 3, %dx%d, %d bytes" % (a.out, N, OW, OH, len(data)))

if a.preview:
    cols_n = 12
    pv = Image.new("RGBA", (cols_n * (OW + 2), 3 * 4 * (OH + 2)), (6, 22, 40, 255))
    for wi, (k, _) in enumerate(order):
        for j, fr in enumerate(frames[k]):
            im = Image.fromarray(fr)
            a2 = np.asarray(im).copy()
            a2[..., 3] = np.where(a2[..., 3] >= 128, 255, 0)
            pv.alpha_composite(Image.fromarray(a2), ((j % cols_n) * (OW + 2), (wi * 4 + j // cols_n) * (OH + 2)))
    pv.resize((pv.width * 2, pv.height * 2), Image.NEAREST).save(a.preview)
