// Live per-player game menu, laid out like the game's own pause menu and its pages
// (main page with tab buttons and the map; WEAPON, ITEM and TEAM pages with the red frame),
// but drawn inside each player's view while the game keeps running.
// Draws a 320x104 RGBA5551 texture (scale 1.0).
// Uses weapon_icons.h, ammo_tex.h (digits), menu_tex.h (labels, portraits) and map_data.h.
// Pure drawing code: no game memory access, no integer divide by a variable.
#ifndef LIVE_MENU_H
#define LIVE_MENU_H

#include "weapon_icons.h"
#include "ammo_tex.h"
#include "menu_tex.h"
#include "map_data.h"
#if defined(__has_include)
#if __has_include("weapon_spin.h")
#include "weapon_spin.h"   // rotating weapons from your own recording (tools/gen_weapon_spin.py)
#endif
#endif

#define LM_W 320
#define LM_H 104
#define LM_TABS 4   // MAP, WEAPON, ITEM, EXIT
enum { LM_T_MAP, LM_T_WEAPON, LM_T_ITEM, LM_T_TEAM, LM_T_CONFIG, LM_T_EXIT };
enum { LM_S_MAIN, LM_S_WEAPON, LM_S_ITEM, LM_S_TEAM };

typedef struct {
    int player;              // 0 = P1 (blue), 1 = P2 (orange)
    int screen;              // LM_S_*
    int tab;                 // main page: selected tab
    int wsel;                // WEAPON page: selected weapon slot (0..4)
    int tsel;                // TEAM page: selected member (0 = Jean-Luc, 1 = Engineer)
    int spin;                // WEAPON page: rotation step of the big weapon (draw count with 3D models)
    int no_model;            // WEAPON page: leave the 3D weapon out (coop.c draws it on its own)
    int held;                // weapon slot in hand, -1 none
    int owned[5];
    int mag[5];
    int cap[5];
    int reserve[5];          // -1 infinite, -2 none
    int can_give;            // WEAPON page: ammo sharing on (separate inventories)
    int partner[5];          // partner's ammo for the slot, -1 = partner hasn't got the weapon
    int give_amt[5];         // what R would give now (0 = can't)
    int give_why[5];         // why not: 2 partner full, 3 partner has no gun, 4 nothing to give
    int give_flash;          // 0..15: fades out after a successful R
    int map_img;
    unsigned int room_mask;
    float px[2], pz[2], yaw[2];
    int hp[2];
} LiveMenuIn;

typedef struct { int r, g, b; } LmCol;

static void lm_put(volatile unsigned short *t, int x, int y, LmCol c) {
    if (x < 0 || x >= LM_W || y < 0 || y >= LM_H) return;
    if (c.r < 0) c.r = 0;
    if (c.g < 0) c.g = 0;
    if (c.b < 0) c.b = 0;
    if (c.r > 255) c.r = 255;
    if (c.g > 255) c.g = 255;
    if (c.b > 255) c.b = 255;
    t[y * LM_W + x] = (unsigned short)(((c.r >> 3) << 11) | ((c.g >> 3) << 6) | ((c.b >> 3) << 1) | 1);
}

static void lm_blend(volatile unsigned short *t, int x, int y, LmCol c, int a) {
    if (x < 0 || x >= LM_W || y < 0 || y >= LM_H || a <= 0) return;
    if (a > 255) a = 255;
    unsigned short d = t[y * LM_W + x];
    LmCol o;
    o.r = ((d >> 11) & 31) << 3;
    o.g = ((d >> 6) & 31) << 3;
    o.b = ((d >> 1) & 31) << 3;
    o.r += ((c.r - o.r) * a) >> 8;
    o.g += ((c.g - o.g) * a) >> 8;
    o.b += ((c.b - o.b) * a) >> 8;
    lm_put(t, x, y, o);
}

static void lm_rect(volatile unsigned short *t, int x0, int y0, int x1, int y1, LmCol c) {
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) lm_put(t, x, y, c);
}

// Text mask with drop shadow; colour can be a vertical gradient (top -> bottom).
static int lm_mask2(volatile unsigned short *t, const unsigned char *px, int w, int h, int x, int y,
                    LmCol top, LmCol bot, int shadow) {
    LmCol sh = { 2, 3, 6 };
    for (int pass = shadow ? 0 : 1; pass < 2; pass++)
        for (int yy = 0; yy < h; yy++) {
            int k = (int)((float)(yy * 256) / (float)(h > 1 ? h - 1 : 1));   // float: no integer divide
            LmCol c = { top.r + (((bot.r - top.r) * k) >> 8), top.g + (((bot.g - top.g) * k) >> 8),
                        top.b + (((bot.b - top.b) * k) >> 8) };
            for (int xx = 0; xx < w; xx++) {
                int a = px[yy * w + xx];
                if (pass == 0) lm_blend(t, x + xx + 1, y + yy + 1, sh, (a * 210) >> 8);
                else lm_blend(t, x + xx, y + yy, c, a);
            }
        }
    return w;
}

static int lm_label2(volatile unsigned short *t, int id, int x, int y, LmCol top, LmCol bot, int shadow) {
    return lm_mask2(t, &lm_label_px[lm_label_off[id]], lm_label_w[id], lm_label_h[id], x, y, top, bot, shadow);
}
static int lm_label(volatile unsigned short *t, int id, int x, int y, LmCol c, int shadow) {
    return lm_label2(t, id, x, y, c, c, shadow);
}

static int lm_digits(int v, int *d) {
    int n = 0;
    if (v < 0) v = 0;
    if (v > 999) v = 999;
    do {
        int q = 0;
        while (v >= 10) { v -= 10; q++; }
        d[n++] = v;
        v = q;
    } while (v > 0 && n < 3);
    return n;
}

static int lm_num(volatile unsigned short *t, int v, int x, int y, LmCol c) {
    int d[3], n = lm_digits(v, d);
    for (int i = n - 1; i >= 0; i--)
        x += lm_mask2(t, &ammo_small_px[ammo_small_off[d[i]]], ammo_small_w[d[i]], AMMO_SMALL_H, x, y, c, c, 1) + 1;
    return x;
}

static int lm_glyph(volatile unsigned short *t, int g, int x, int y, LmCol c) {
    return lm_mask2(t, &ammo_small_px[ammo_small_off[g]], ammo_small_w[g], AMMO_SMALL_H, x, y, c, c, 1);
}

// Ammo text like the game's weapon cells: "inf", "mag/reserve" or "count".
static void lm_ammo(volatile unsigned short *t, int x, int y, int mag, int reserve, LmCol c) {
    if (reserve == -1) { lm_glyph(t, 11, x, y, c); return; }
    x = lm_num(t, mag, x, y, c);
    if (reserve >= 0) {
        x += 1 + lm_glyph(t, 10, x + 1, y, c) + 2;
        lm_num(t, reserve, x, y, c);
    }
}

// Weapon art: 48x24 icons, drawn at any integer scale (s2 = 1 -> half size, 2 -> full, 4 -> double).
static void lm_icon(volatile unsigned short *t, int slot, int variant, int x, int y, int s2, int tint, LmCol tc) {
    int w = (WICON_W * s2) >> 1, h = (WICON_H * s2) >> 1;
    for (int yy = 0; yy < h; yy++)
        for (int xx = 0; xx < w; xx++) {
            int sx = (s2 == 1) ? xx << 1 : (s2 == 2) ? xx : xx >> 1;    // s2 is 1, 2 or 4
            int sy = (s2 == 1) ? yy << 1 : (s2 == 2) ? yy : yy >> 1;
            unsigned short p = weapon_icons[slot][variant][sy * WICON_W + sx];
            if (!(p & 1)) continue;
            LmCol c = { ((p >> 11) & 31) << 3, ((p >> 6) & 31) << 3, ((p >> 1) & 31) << 3 };
            int lum = (c.r + c.g + c.b) >> 2;
            if (tint) { c.r = (tc.r * lum) >> 7; c.g = (tc.g * lum) >> 7; c.b = (tc.b * lum) >> 7; }
            lm_put(t, x + xx, y + yy, c);
        }
}

// Radial cyan glow, like the backdrop of the game's weapon / team pages.
static void lm_glow(volatile unsigned short *t, int cx, int cy, int r) {
    LmCol c = { 40, 150, 150 };
    int r2 = r * r;
    int k = (int)((float)(200 << 12) / (float)r2);
    for (int y = cy - r; y <= cy + r; y++)
        for (int x = cx - r; x <= cx + r; x++) {
            int dx = x - cx, dy = y - cy, d2 = dx * dx + dy * dy;
            if (d2 >= r2) continue;
            int a = 200 - ((d2 * k) >> 12);
            lm_blend(t, x, y, c, a);
        }
}

static int lm_orange;   // 1 while drawing Player 2's menu: orange accents like the P2 HUD
static void lm_button(volatile unsigned short *t, int x, int y, int w, int h, int label, int sel) {
    for (int yy = 0; yy < h; yy++) {
        int k = (yy * 2 - (h - 1));
        if (k < 0) k = -k;
        int base = 70 + ((k * 120) >> 4);
        if (base > 215) base = 215;
        LmCol c = !sel ? (LmCol){ base, base + 4, base + 12 }
                : lm_orange ? (LmCol){ base + 40, base * 3 / 5, base / 5 } : (LmCol){ base * 2 / 5, base, base + 40 };
        for (int xx = 0; xx < w; xx++) {
            LmCol cc = c;
            if (xx < 2 || xx >= w - 2) { cc.r >>= 1; cc.g >>= 1; cc.b >>= 1; }
            lm_put(t, x + xx, y + yy, cc);
        }
    }
    LmCol tc = { 14, 18, 26 };
    lm_label(t, label, x + ((w - lm_label_w[label]) >> 1), y + ((h - lm_label_h[label]) >> 1), tc, 0);
}

static void lm_background(volatile unsigned short *t) {
    for (int y = 0; y < LM_H; y++)
        for (int x = 0; x < LM_W; x++) {
            int k = (x * 24) >> 8;
            LmCol c = { 2 + (k >> 3), 5 + (k >> 1), 12 + k };
            lm_put(t, x, y, c);
        }
}

static float lm_sin(float a) {
    while (a > 3.14159265f) a -= 6.28318531f;
    while (a < -3.14159265f) a += 6.28318531f;
    float y = 1.27323954f * a - 0.405284735f * a * (a < 0.0f ? -a : a);
    float ay = y < 0.0f ? -y : y;
    return 0.225f * (ay * y - y) + y;
}

// Real 3D weapon models (software renderer; meshes from your ROM, see tools/extract_weapon_models.py).
#if defined(__has_include)
#if __has_include("weapon_models.h")
#include "wmodel.h"
#endif
#endif

static void lm_line(volatile unsigned short *t, float x0, float y0, float x1, float y1, LmCol c,
                    int cx0, int cy0, int cx1, int cy1) {
    float dx = x1 - x0, dy = y1 - y0;
    float adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
    int n = (int)(adx > ady ? adx : ady) + 1;
    if (n > 500) n = 500;
    float sx = dx / (float)n, sy = dy / (float)n;
    for (int i = 0; i <= n; i++) {
        int x = (int)(x0 + 0.5f), y = (int)(y0 + 0.5f);
        if (x >= cx0 && x <= cx1 && y >= cy0 && y <= cy1) lm_put(t, x, y, c);
        x0 += sx;
        y0 += sy;
    }
}

// Level map fitted into a box, with both players (dot + aim line).
static void lm_map(volatile unsigned short *t, const LiveMenuIn *in, int bx0, int by0, int bx1, int by1) {
    if (in->map_img < 0 || in->map_img >= MAP_IMAGES) return;
    const unsigned short *mi = map_img[in->map_img];
    float minx = 1e9f, maxx = -1e9f, minz = 1e9f, maxz = -1e9f;
    for (int r = 0; r < (int)mi[5]; r++) {
        if (r < 32 && !((in->room_mask >> r) & 1u)) continue;
        int l0 = (int)map_f[2 * (mi[4] + r)], ln = (int)map_f[2 * (mi[4] + r) + 1];
        for (int k = l0; k < l0 + ln; k++)
            for (int e = 0; e < 2; e++) {
                int v = (int)map_l[2 * (mi[2] + k) + e] + (int)mi[0];
                float x = (float)map_v[2 * v], z = (float)map_v[2 * v + 1];
                if (x < minx) minx = x;
                if (x > maxx) maxx = x;
                if (z < minz) minz = z;
                if (z > maxz) maxz = z;
            }
    }
    for (int p = 0; p < 2; p++) {
        if (in->px[p] < minx) minx = in->px[p];
        if (in->px[p] > maxx) maxx = in->px[p];
        if (in->pz[p] < minz) minz = in->pz[p];
        if (in->pz[p] > maxz) maxz = in->pz[p];
    }
    if (maxx <= minx || maxz <= minz) return;
    float bw = (float)(bx1 - bx0 - 6), bh = (float)(by1 - by0 - 6);
    float sx = bw / (maxx - minx), sz = bh / (maxz - minz);
    float s = sx < sz ? sx : sz;
    float ox = (float)bx0 + 3.0f + (bw - (maxx - minx) * s) * 0.5f;
    float oy = (float)by0 + 3.0f + (bh - (maxz - minz) * s) * 0.5f;
    LmCol yel = { 232, 216, 40 };
    for (int r = 0; r < (int)mi[5]; r++) {
        if (r < 32 && !((in->room_mask >> r) & 1u)) continue;
        int l0 = (int)map_f[2 * (mi[4] + r)], ln = (int)map_f[2 * (mi[4] + r) + 1];
        for (int k = l0; k < l0 + ln; k++) {
            int a = (int)map_l[2 * (mi[2] + k)] + (int)mi[0];
            int b = (int)map_l[2 * (mi[2] + k) + 1] + (int)mi[0];
            lm_line(t, ox + ((float)map_v[2 * a] - minx) * s, oy + ((float)map_v[2 * a + 1] - minz) * s,
                       ox + ((float)map_v[2 * b] - minx) * s, oy + ((float)map_v[2 * b + 1] - minz) * s, yel,
                       bx0, by0, bx1, by1);
        }
    }
    for (int n = 0; n < 2; n++) {
        int p = n ? in->player : (in->player ^ 1);
        LmCol c = p ? (LmCol){ 255, 150, 50 } : (LmCol){ 90, 200, 255 };
        float x = ox + (in->px[p] - minx) * s, y = oy + (in->pz[p] - minz) * s;
        float fx = lm_sin(in->yaw[p]), fz = lm_sin(in->yaw[p] + 1.57079633f);
        lm_line(t, x, y, x + fx * 7.0f, y + fz * 7.0f, c, bx0, by0, bx1, by1);
        for (int yy = -1; yy <= 1; yy++)
            for (int xx = -1; xx <= 1; xx++) lm_put(t, (int)x + xx, (int)y + yy, c);
    }
}

// ---- main page: blue frame, tab buttons on the left, map on the right ----
static void lm_page_main(volatile unsigned short *t, const LiveMenuIn *in) {
    LmCol cyan = lm_orange ? (LmCol){ 255, 150, 50 } : (LmCol){ 80, 180, 255 };
    lm_background(t);
    lm_rect(t, 6, 1, LM_W - 6, 2, cyan);
    lm_rect(t, 9, 5, LM_W - 6, 5, cyan);
    lm_rect(t, 6, LM_H - 3, LM_W - 6, LM_H - 2, cyan);
    lm_rect(t, 9, LM_H - 6, LM_W - 6, LM_H - 6, cyan);
    lm_rect(t, 9, 5, 9, LM_H - 6, cyan);
    lm_rect(t, LM_W - 9, 5, LM_W - 9, LM_H - 6, cyan);
    lm_rect(t, LM_W - 6, 0, LM_W - 2, 3, cyan);
    lm_rect(t, LM_W - 6, LM_H - 4, LM_W - 2, LM_H - 1, cyan);
    lm_rect(t, 2, 0, 6, 3, cyan);
    lm_rect(t, 2, LM_H - 4, 6, LM_H - 1, cyan);
    static const int labels[LM_TABS] = { LM_TAB_MAP, LM_TAB_WEAPON, LM_TAB_ITEM, LM_TAB_EXIT };
    for (int i = 0; i < LM_TABS; i++) {
        int y = 12 + i * 22;
        int sel = i == in->tab;
        if (sel) {                               // soft cyan halo behind the chosen tab
            for (int yy = y - 3; yy <= y + 16; yy++)
                for (int x = 14; x <= 82; x++) {
                    int dy = yy - (y + 6);
                    if (dy < 0) dy = -dy;
                    lm_blend(t, x, yy, (lm_orange ? (LmCol){ 230, 120, 30 } : (LmCol){ 40, 160, 220 }), 110 - dy * 10);
                }
        }
        // thin metal bars above and below each button, like the original
        lm_rect(t, 18, y, 77, y, sel ? (lm_orange ? (LmCol){ 255, 210, 150 } : (LmCol){ 170, 230, 255 }) : (LmCol){ 150, 156, 170 });
        lm_rect(t, 18, y + 13, 77, y + 13, sel ? (lm_orange ? (LmCol){ 220, 130, 50 } : (LmCol){ 90, 170, 220 }) : (LmCol){ 90, 96, 110 });
        lm_button(t, 21, y + 2, 54, 10, labels[i], sel);
        if (sel) {                               // pointer on the left
            for (int k = 0; k < 4; k++) lm_rect(t, 12 + k, y + 3 + k, 12 + k, y + 10 - k, (LmCol){ 230, 240, 255 });
        }
    }
    // map panel: dark inset with a faint grid and corner brackets
    int mx0 = 90, my0 = 9, mx1 = LM_W - 14, my1 = LM_H - 9;
    for (int y = my0; y <= my1; y++)
        for (int x = mx0; x <= mx1; x++) {
            int grid = (((x - mx0) & 15) == 0) || (((y - my0) & 15) == 0);
            lm_blend(t, x, y, grid ? (lm_orange ? (LmCol){ 130, 70, 30 } : (LmCol){ 40, 90, 130 }) : (LmCol){ 0, 6, 14 }, grid ? 90 : 120);
        }
    LmCol br = cyan;
    lm_rect(t, mx0, my0, mx0 + 8, my0, br);   lm_rect(t, mx0, my0, mx0, my0 + 8, br);
    lm_rect(t, mx1 - 8, my0, mx1, my0, br);   lm_rect(t, mx1, my0, mx1, my0 + 8, br);
    lm_rect(t, mx0, my1, mx0 + 8, my1, br);   lm_rect(t, mx0, my1 - 8, mx0, my1, br);
    lm_rect(t, mx1 - 8, my1, mx1, my1, br);   lm_rect(t, mx1, my1 - 8, mx1, my1, br);
    lm_map(t, in, mx0 + 2, my0 + 2, mx1 - 2, my1 - 2);
}

// ---- red pages (WEAPON / ITEM / TEAM): shared frame ----
static void lm_red_frame(volatile unsigned short *t, int label) {
    LmCol red = { 150, 0, 0 };
    LmCol grey = { 150, 170, 190 };
    lm_background(t);
    lm_rect(t, 0, 0, LM_W - 1, 0, grey);
    lm_rect(t, 0, 0, 0, LM_H - 1, grey);
    lm_rect(t, 0, LM_H - 1, LM_W - 1, LM_H - 1, (LmCol){ 60, 70, 84 });
    lm_rect(t, 2, 2, LM_W - 3, 2, red);            // outer red frame
    lm_rect(t, LM_W - 3, 2, LM_W - 3, LM_H - 3, red);
    lm_rect(t, 2, 2, 2, 22, red);
    lm_rect(t, 2, 22, 78, 22, red);                // under the title button
    lm_rect(t, 78, 22, 78, LM_H - 3, red);
    lm_rect(t, 78, LM_H - 3, LM_W - 3, LM_H - 3, red);
    // inner frame with a rounded top-left corner
    int cx = 96, cy = 22, r = 12;
    lm_rect(t, cx, 10, LM_W - 3, 11, red);
    lm_rect(t, 84, cy, 85, LM_H - 3, red);
    for (int a = 0; a <= r; a++) {                  // quarter circle from (84,22) to (96,10)
        for (int b = 0; b <= r; b++) {
            int d = (a - r) * (a - r) + (b - r) * (b - r);
            if (d >= (r - 1) * (r - 1) && d <= (r + 1) * (r + 1)) lm_put(t, 84 + a, 10 + b, red);
        }
    }
    // title button (top left)
    lm_rect(t, 8, 4, 70, 4, (LmCol){ 200, 206, 220 });
    lm_rect(t, 8, 19, 70, 19, (LmCol){ 110, 116, 130 });
    lm_button(t, 12, 7, 54, 10, label, 0);
}

// Teal cell of the left column.
static void lm_cell(volatile unsigned short *t, int x, int y, int w, int h, int sel) {
    for (int yy = 0; yy < h; yy++) {
        int k = (int)((float)(yy * 40) / (float)h);
        LmCol c = sel ? (LmCol){ 60 - k, 130 - k, 132 - k } : (LmCol){ 14 + (k >> 2), 46 - (k >> 1), 52 - (k >> 1) };
        for (int xx = 0; xx < w; xx++) lm_put(t, x + xx, y + yy, c);
    }
}

#ifdef WSPIN_OK
// One frame of the rotating weapon (palette indices with transparent runs), centred on (cx, cy).
__attribute__((unused)) static void lm_spin(volatile unsigned short *t, int w, int f, int cx, int cy) {
    const unsigned char *d = &wspin_data[wspin_off[w][f]];
    const unsigned short *pal = wspin_pal[w];
    int x0 = cx - (WSPIN_W >> 1), y0 = cy - (WSPIN_H >> 1);
    int x = 0, y = 0, n = WSPIN_W * WSPIN_H;
    while (n > 0) {
        int sk = *d++, cnt = *d++;
        n -= sk + cnt;
        x += sk;
        while (x >= WSPIN_W) { x -= WSPIN_W; y++; }
        for (int i = 0; i < cnt; i++) {
            unsigned short p = pal[*d++];
            lm_put(t, x0 + x, y0 + y, (LmCol){ ((p >> 11) & 31) << 3, ((p >> 6) & 31) << 3, ((p >> 1) & 31) << 3 });
            if (++x >= WSPIN_W) { x = 0; y++; }
        }
    }
}
#endif

static const int lm_cap_default[5] = { 8, 30, 8, 4, 7 };

// Ammo sharing strip (WEAPON page, bottom): a teal cell like the weapon list, with
//   [R] GIVE 30  >>>  [partner portrait] 12
// The chevrons and the portrait frame take the partner's colour (P1 blue, P2 orange). When R can't
// give, the reason replaces "GIVE n" and the chevrons go dark. After a successful R the cell lights
// up and the partner's count turns green, fading out (give_flash).
static void lm_share_strip(volatile unsigned short *t, const LiveMenuIn *in, int s) {
    const int x0 = 92, y0 = 87, w = 212, h = 13;
    int fl = in->give_flash;
    lm_cell(t, x0, y0, w, h, fl > 0);
    if (fl > 0) {                                   // fading light over the cell
        for (int yy = 0; yy < h; yy++)
            for (int xx = 0; xx < w; xx++) lm_blend(t, x0 + xx, y0 + yy, (LmCol){ 120, 255, 200 }, fl * 6);
    }
    lm_rect(t, x0, y0 + h, x0 + w - 1, y0 + h, (LmCol){ 6, 20, 24 });           // shadow line under it
    int pm = (in->player ^ 1) & 1;                  // partner: 0 Jean-Luc (blue), 1 Engineer (orange)
    LmCol acc = pm ? (LmCol){ 255, 140, 50 } : (LmCol){ 70, 165, 255 };
    // [R] button, like the menu's grey buttons
    lm_button(t, x0 + 4, y0 + 2, 13, 9, LM_R, 0);
    int amt = in->give_amt[s];
    int x = x0 + 22;
    if (amt > 0) {
        LmCol yl = { 236, 214, 110 };
        x += lm_label(t, LM_GIVE, x, y0 + 3, yl, 1) + 4;
        lm_num(t, amt, x, y0 + 2, yl);
    } else {
        static const int why_label[5] = { 0, 0, LM_MSG_FULL, LM_MSG_NOGUN, LM_MSG_EMPTY };
        int wl = (in->give_why[s] >= 2 && in->give_why[s] <= 4) ? why_label[in->give_why[s]] : LM_MSG_EMPTY;
        lm_label(t, wl, x, y0 + 3, (LmCol){ 240, 120, 100 }, 1);
    }
    // chevrons towards the partner
    int px = x0 + 160;                              // portrait
    for (int c = 0; c < 3; c++) {
        int cx = px - 26 + c * 7;
        int a = (amt > 0) ? 90 + c * 60 : 40;
        LmCol cc = { (acc.r * a) >> 8, (acc.g * a) >> 8, (acc.b * a) >> 8 };
        if (fl > 0 && amt >= 0) cc = (LmCol){ acc.r, acc.g, acc.b };
        for (int k = 0; k < 4; k++) {
            lm_put(t, cx + k, y0 + 2 + k, cc);
            lm_put(t, cx + k + 1, y0 + 2 + k, cc);
            lm_put(t, cx + k, y0 + 9 - k, cc);
            lm_put(t, cx + k + 1, y0 + 9 - k, cc);
        }
    }
    // partner portrait 12x12 (every 14/3 px of the 56x56 picture) in a frame of its colour
    lm_rect(t, px - 1, y0, px + 12, y0 + h - 1, acc);
    for (int yy = 0; yy < 11; yy++)
        for (int xx = 0; xx < 12; xx++) {
            unsigned short p = lm_portrait[pm][((yy * 14) / 3 + 2) * LM_PORT + (xx * 14) / 3 + 2];
            LmCol pc = (p & 1) ? (LmCol){ ((p >> 11) & 31) << 3, ((p >> 6) & 31) << 3, ((p >> 1) & 31) << 3 }
                               : (LmCol){ 10, 16, 26 };
            lm_put(t, px + xx, y0 + 1 + yy, pc);
        }
    // partner's count
    if (in->partner[s] >= 0) {
        LmCol nc = (fl > 0) ? (LmCol){ 150, 240, 150 } : (LmCol){ 220, 228, 236 };
        lm_num(t, in->partner[s], px + 17, y0 + 2, nc);
    } else {
        lm_rect(t, px + 17, y0 + 6, px + 23, y0 + 7, (LmCol){ 120, 130, 140 });     // "-": hasn't got the gun
    }
}

static void lm_page_weapon(volatile unsigned short *t, const LiveMenuIn *in) {
    lm_red_frame(t, LM_TAB_WEAPON);
    LmCol yellow = { 236, 214, 110 };
    // left column: owned weapons in order, then empty cells
    int row = 0;
    for (int s = 0; s < 5; s++) {
        if (!in->owned[s]) continue;
        int y = 25 + row * 15;
        lm_cell(t, 5, y, 70, 14, s == in->wsel);
        lm_icon(t, s, 3, 46, y + 1, 1, 1, (LmCol){ 150, 14, 14 });
        lm_ammo(t, 8, y + 3, in->mag[s], in->reserve[s], yellow);
        row++;
    }
    for (; row < 5; row++) lm_cell(t, 5, 25 + row * 15, 70, 14, 0);
    // right side: big weapon over a cyan glow, name, capacity and reserve
    int s = in->wsel;
    if (s < 0 || s > 4 || !in->owned[s]) return;
#ifdef WM_OK
    {
        lm_glow(t, 200, 37, 34);
        if (!in->no_model) wm_draw(t, s, in->spin, 200, 37);   // in->spin counts draws, WM_TURN per full turn
    }
#else
#ifdef WSPIN_OK
    if (s < 3) {
        lm_glow(t, 200, 37, 34);
        lm_spin(t, s, in->spin, 200, 37);
    } else
#endif
    {
        lm_glow(t, 200, 40, 30);
        lm_icon(t, s, 3, 200 - WICON_W, 40 - WICON_H, 4, 0, (LmCol){ 0, 0, 0 });
    }
#endif
    int nl = LM_BIG_HANDGUN + s;
    int share = in->can_give && s > 0;                // the share strip takes the bottom row
    int ty = share ? -5 : 0;
    lm_label2(t, nl, 200 - (lm_label_w[nl] >> 1), 66 + ty, (LmCol){ 255, 230, 150 }, (LmCol){ 200, 110, 20 }, 1);
    LmCol teal = { 70, 160, 160 };
    int cap = in->cap[s] > 0 ? in->cap[s] : lm_cap_default[s];
    int x = 150;
    x += lm_label(t, LM_CAPACITY, x, 82 + ty, teal, 1) + 4;
    lm_num(t, cap, x, 80 + ty, teal);
    if (in->reserve[s] >= 0) {
        x = 226;
        x += lm_label(t, LM_RESERVE, x, 82 + ty, teal, 1) + 4;
        lm_num(t, in->reserve[s], x, 80 + ty, teal);
    }
    if (share) lm_share_strip(t, in, s);

}

static void lm_page_item(volatile unsigned short *t) {
    lm_red_frame(t, LM_TAB_ITEM);
    for (int r = 0; r < 3; r++) {
        int y = 25 + r * 15;
        lm_cell(t, 5, y, 62, 14, 0);
        int cx = 36, cy = y + 7;                    // small diamond marker
        for (int d = -3; d <= 3; d++) {
            int w = 3 - (d < 0 ? -d : d);
            for (int xx = -w; xx <= w; xx++) lm_put(t, cx + xx, cy + d, (LmCol){ 30, 70, 72 });
        }
    }
    lm_glow(t, 200, 48, 30);
}

static void lm_page_team(volatile unsigned short *t, const LiveMenuIn *in) {
    lm_red_frame(t, LM_TAB_TEAM);
    for (int m = 0; m < 2; m++) {
        int y = 25 + m * 13;
        int sel = (m == in->tsel);
        lm_cell(t, 4, y, 72, 12, sel);
        int id = m ? LM_LIST_ENGINEER : LM_LIST_JEAN;
        lm_label(t, id, 10, y + 2, sel ? (LmCol){ 230, 236, 240 } : (LmCol){ 190, 196, 204 }, 1);
        LmCol dot = sel ? (LmCol){ 60, 200, 255 } : (LmCol){ 250, 220, 40 };
        if (in->hp[m] <= 0) dot = (LmCol){ 200, 40, 30 };
        lm_rect(t, 66, y + 4, 69, y + 7, dot);
    }
    int m = in->tsel & 1;
    lm_glow(t, 196, 38, 30);
    // portrait (56x56)
    for (int yy = 0; yy < LM_PORT; yy++)
        for (int xx = 0; xx < LM_PORT; xx++) {
            unsigned short p = lm_portrait[m][yy * LM_PORT + xx];
            if (!(p & 1)) continue;
            lm_put(t, 168 + xx, 10 + yy, (LmCol){ ((p >> 11) & 31) << 3, ((p >> 6) & 31) << 3, ((p >> 1) & 31) << 3 });
        }
    int nl = m ? LM_BIG_ENGINEER : LM_BIG_JEANLUC;
    lm_label2(t, nl, 196 - (lm_label_w[nl] >> 1), 68, (LmCol){ 255, 240, 120 }, (LmCol){ 210, 120, 20 }, 1);
    // status badge
    int alive = in->hp[m] > 0;
    lm_rect(t, 262, 14, 304, 22, alive ? (LmCol){ 30, 150, 170 } : (LmCol){ 150, 30, 30 });
    lm_rect(t, 256, 18, 310, 18, alive ? (LmCol){ 60, 200, 230 } : (LmCol){ 200, 60, 50 });
    int bl = alive ? LM_ALIVE : LM_DOWN;
    lm_label(t, bl, 283 - (lm_label_w[bl] >> 1), 15, (LmCol){ 8, 14, 20 }, 0);
    // life
    LmCol teal = { 70, 160, 160 };
    lm_label(t, LM_LIFE, 150, 85, teal, 1);
    int bx = 172, bw = 100;
    lm_rect(t, bx - 1, 84, bx + bw, 92, (LmCol){ 8, 10, 16 });
    int hp = in->hp[m];
    if (hp < 0) hp = 0;
    if (hp > 100) hp = 100;
    LmCol g = hp > 50 ? (LmCol){ 60, 210, 80 } : hp > 25 ? (LmCol){ 230, 200, 40 } : (LmCol){ 230, 60, 40 };
    for (int yy = 85; yy <= 91; yy++)
        for (int x = bx; x < bx + hp; x++) lm_put(t, x, yy, yy == 85 ? (LmCol){ g.r + 40, g.g + 40, g.b + 40 } : g);
}

#ifdef WM_OK
// The 3D weapon's area (centre 200, 37) with the page under it, kept per player, so a new turn
// step repaints only this block instead of the whole page.
#define LM_WX0 (200 - (WM_W >> 1))
#define LM_WY0 (37 - (WM_H >> 1))
static unsigned short lm_wbg[2][WM_W * WM_H];
static void lm_wbg_save(volatile unsigned short *t, int k) {
    for (int y = 0; y < WM_H; y++)
        for (int x = 0; x < WM_W; x++) lm_wbg[k][y * WM_W + x] = t[(LM_WY0 + y) * LM_W + LM_WX0 + x];
}
static void lm_wbg_restore(volatile unsigned short *t, int k) {
    for (int y = 0; y < WM_H; y++)
        for (int x = 0; x < WM_W; x++) t[(LM_WY0 + y) * LM_W + LM_WX0 + x] = lm_wbg[k][y * WM_W + x];
}
#endif

static void live_menu_render(volatile unsigned short *t, const LiveMenuIn *in) {
    lm_orange = in->player != 0;
    switch (in->screen) {
        case LM_S_WEAPON: lm_page_weapon(t, in); break;
        case LM_S_ITEM: lm_page_item(t); break;
        case LM_S_TEAM: lm_page_team(t, in); break;
        default: lm_page_main(t, in); break;
    }
    // player colour mark on the right edge
    LmCol pc = in->player ? (LmCol){ 255, 150, 50 } : (LmCol){ 80, 170, 255 };
    lm_rect(t, LM_W - 2, 6, LM_W - 1, LM_H - 7, pc);
}

#endif
