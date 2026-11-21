// Ammo panel: draws a 128x32 RGBA5551 texture with the magazine count, the reserve
// ammo and one round per bullet in the magazine. Shown at scale 0.5 (64x16 on screen).
// Art (plate, glyph masks, bullet) comes from src/ammo_tex.h (tools/gen_ammo_tex.py).
//
// Pure drawing code (no game memory access, no integer divide), so it also builds on a
// PC for a preview.
#ifndef AMMO_HUD_H
#define AMMO_HUD_H

#include "ammo_tex.h"

#define AMMO_W 128
#define AMMO_H 32

typedef struct {
    int player;    // 0 = Player 1 (blue), 1 = Player 2 (orange)
    int mag;       // rounds in the magazine
    int cap;       // magazine size
    int reserve;   // reserve rounds; -1 = infinite, -2 = none (rockets, C4)
} AmmoIn;

typedef struct { int r, g, b; } AmmoCol;

static void ammo_blend(volatile unsigned short *t, int x, int y, AmmoCol c, int a) {
    if (x < 0 || x >= AMMO_W || y < 0 || y >= AMMO_H || a <= 0) return;
    unsigned short d = t[y * AMMO_W + x];
    if (!(d & 1)) return;  // outside the plate
    int dr = ((d >> 11) & 31) << 3, dg = ((d >> 6) & 31) << 3, db = ((d >> 1) & 31) << 3;
    int r = dr + (((c.r - dr) * a) >> 8);
    int g = dg + (((c.g - dg) * a) >> 8);
    int b = db + (((c.b - db) * a) >> 8);
    if (r < 0) r = 0;
    if (g < 0) g = 0;
    if (b < 0) b = 0;
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    t[y * AMMO_W + x] = (unsigned short)(((r >> 3) << 11) | ((g >> 3) << 6) | ((b >> 3) << 1) | 1);
}

// Draws a glyph mask with a dark drop shadow and a vertical colour gradient.
static int ammo_draw_mask(volatile unsigned short *t, const unsigned char *px, int w, int h,
                          int x, int y, AmmoCol top, AmmoCol bot, int dim) {
    AmmoCol sh = { 2, 3, 6 };
    for (int pass = 0; pass < 2; pass++) {
        for (int yy = 0; yy < h; yy++) {
            int k = (yy * 256) >> 4;   // ~ yy / 16
            if (k > 256) k = 256;
            AmmoCol c;
            c.r = top.r + (((bot.r - top.r) * k) >> 8);
            c.g = top.g + (((bot.g - top.g) * k) >> 8);
            c.b = top.b + (((bot.b - top.b) * k) >> 8);
            if (dim) { c.r = 52; c.g = 58; c.b = 74; }
            for (int xx = 0; xx < w; xx++) {
                int a = px[yy * w + xx];
                if (pass == 0) ammo_blend(t, x + xx + 1, y + yy + 1, sh, (a * 220) >> 8);
                else ammo_blend(t, x + xx, y + yy, c, a);
            }
        }
    }
    return w;
}

static int ammo_digits(int v, int *d) {
    int n = 0;
    if (v < 0) v = 0;
    if (v > 999) v = 999;
    do {
        int q = 0;
        while (v >= 10) { v -= 10; q++; }   // v / 10 without a divide
        d[n++] = v;
        v = q;
    } while (v > 0 && n < 3);
    return n;
}

static void ammo_render(volatile unsigned short *t, const AmmoIn *in) {
    const unsigned short *pl = ammo_plate[in->player ? 1 : 0];
    for (int i = 0; i < AMMO_W * AMMO_H; i++) t[i] = pl[i];

    int low = in->cap > 0 && in->mag * 4 <= in->cap;
    AmmoCol gtop = { 255, 240, 170 }, gbot = { 238, 150, 36 };
    AmmoCol rtop = { 255, 160, 130 }, rbot = { 225, 40, 30 };
    AmmoCol wtop = { 240, 244, 252 }, wbot = { 140, 152, 176 };

    // Magazine count: two big digits, a leading zero drawn dim.
    int d[3];
    int n = ammo_digits(in->mag, d);
    int lead_zero = 0;
    if (n < 2) { d[1] = 0; n = 2; lead_zero = 1; }
    int x = 13, y = 3;
    for (int i = n - 1; i >= 0; i--) {
        int dim = lead_zero && i == 1;
        x += ammo_draw_mask(t, &ammo_big_px[ammo_big_off[d[i]]], ammo_big_w[d[i]], AMMO_BIG_H,
                            x, y, low ? rtop : gtop, low ? rbot : gbot, dim) + 1;
    }

    // Reserve: "/ 85" or "/ infinity", smaller, bottom-aligned with the big digits.
    if (in->reserve != -2) {
        int sy = y + AMMO_BIG_H - AMMO_SMALL_H;
        x += 2;
        x += ammo_draw_mask(t, &ammo_small_px[ammo_small_off[10]], ammo_small_w[10], AMMO_SMALL_H,
                            x, sy, wtop, wbot, 0) + 2;
        if (in->reserve == -1) {
            ammo_draw_mask(t, &ammo_small_px[ammo_small_off[11]], ammo_small_w[11], AMMO_SMALL_H,
                           x, sy, wtop, wbot, 0);
        } else {
            int rn = ammo_digits(in->reserve, d);
            for (int i = rn - 1; i >= 0; i--) {
                x += ammo_draw_mask(t, &ammo_small_px[ammo_small_off[d[i]]], ammo_small_w[d[i]],
                                    AMMO_SMALL_H, x, sy, wtop, wbot, 0) + 1;
            }
        }
    }

    // Magazine row: one lying round per bullet; thin brass bars when the magazine is large.
    if (in->cap > 0) {
        const int area = 104, x0 = 10, y0 = 24;
        int pitch = 2;
        while ((pitch + 1) * in->cap <= area && pitch < 15) pitch++;
        for (int i = 0; i < in->cap; i++) {
            int full = i < in->mag;
            int bx = x0 + i * pitch;
            if (pitch >= 10) {
                for (int yy = 0; yy < AMMO_BUL_H; yy++) {
                    for (int xx = 0; xx < AMMO_BUL_W && xx < pitch - 1; xx++) {
                        const unsigned char *p = ammo_bullet[yy][xx];
                        AmmoCol c = { p[0], p[1], p[2] };
                        if (!full) { c.r = 40 + (c.r >> 4); c.g = 46 + (c.g >> 4); c.b = 60 + (c.b >> 4); }
                        else if (low) { c.g = c.g >> 1; c.b = c.b >> 1; }
                        ammo_blend(t, bx + xx, y0 + yy, c, p[3]);
                    }
                }
            } else {
                for (int yy = 0; yy < AMMO_BUL_H; yy++) {
                    int k = (yy == 1) ? 50 : (yy >= 4 ? -50 : 0);
                    AmmoCol c;
                    if (full && !low) { c.r = 225 + (k >> 1); c.g = 165 + k; c.b = 60 + (k >> 1); }
                    else if (full) { c.r = 230 + (k >> 1); c.g = 70 + (k >> 1); c.b = 50; }
                    else { c.r = 44; c.g = 50; c.b = 64; }
                    for (int xx = 0; xx < pitch - 1; xx++) ammo_blend(t, bx + xx, y0 + yy, c, 255);
                }
            }
        }
    }
}

#endif
