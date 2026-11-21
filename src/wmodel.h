// Software 3D renderer for the weapon models of the live menu's WEAPON page.
// The meshes come from your ROM (tools/extract_weapon_models.py -> src/weapon_models.h); this
// file only draws them: rotation, the pause menu's own two directional lights, Gouraud
// shading, CI4 textures, 2x2 supersampling for smooth edges.
// Pure drawing code: no game memory access, no libm, no integer divide by a variable.
// Needs from the including file: LmCol, lm_blend(), lm_sin().
#ifndef WMODEL_H
#define WMODEL_H

#include "weapon_models.h"

#define WM_W 112                  // area on the menu texture (pixels)
#define WM_H 56
#define WM_SS 2                   // supersampling per axis
#define WM_BW (WM_W * WM_SS)
#define WM_BH (WM_H * WM_SS)
#define WM_MAXV 1024
#define WM_TURN 324               // draws per full turn (the game updates 30 times a second)

static unsigned short wm_col[WM_BW * WM_BH];   // RGB555, bit 0 = covered
static float wm_zb[WM_BW * WM_BH];
static float wm_sx[WM_MAXV], wm_sy[WM_MAXV], wm_sz[WM_MAXV];
static float wm_lr[WM_MAXV], wm_lg[WM_MAXV], wm_lb[WM_MAXV];
static float wm_nx[WM_MAXV], wm_ny[WM_MAXV], wm_nz[WM_MAXV];
static float wm_sp[WM_MAXV];                      // specular glint per vertex

static float wm_cos(float a) { return lm_sin(a + 1.57079633f); }

static int wm_floor(float x) {
    int i = (int)x;
    return (x < (float)i) ? i - 1 : i;
}

static float wm_clamp1(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }

// The pause menu's lights: ambient 0x78, directional 0xAA from (-120, 80, 30) and 0xB4 from (60, -120, 80).
#define WM_GAIN 1.25f             // the game's reflection pass brightens the models; this does too
#define WM_AMB 0.4706f
#define WM_L1C 0.6667f
#define WM_L2C 0.7059f
#define WM_L1X -0.8147f
#define WM_L1Y 0.5431f
#define WM_L1Z 0.2036f
#define WM_L2X 0.3858f
#define WM_L2Y -0.7716f
#define WM_L2Z 0.5144f

static float wm_texel(const WmMat *m, float u, float v, int *cut, float *tr, float *tg, float *tb) {
    int ti = m->tex;
    int w = wm_tw[ti], h = wm_th[ti];
    float fu = u - (float)wm_floor(u);
    float fv = v - (float)wm_floor(v);
    int iu = (int)(fu * (float)w);
    int iv = (int)(fv * (float)h);
    if (iu >= w) iu = w - 1;
    if (iv >= h) iv = h - 1;
    int n = iv * w + iu;
    unsigned char b = wm_tdata[wm_toff[ti] + (n >> 1)];
    int idx = (n & 1) ? (b & 15) : (b >> 4);
    unsigned short p = wm_tpal[ti][idx];
    *cut = (m->flags & 2) && !(p & 1);
    *tr = (float)((p >> 11) & 31) * (1.0f / 31.0f);
    *tg = (float)((p >> 6) & 31) * (1.0f / 31.0f);
    *tb = (float)((p >> 1) & 31) * (1.0f / 31.0f);
    return 0.0f;
}

// Procedural gunmetal standing in for the game's reflection map.
static void wm_env(const WmMat *m, float nx, float ny, float nz, float *r, float *g, float *b) {
    float e = 0.30f + 0.32f * (ny * 0.5f + 0.5f) + 0.22f * (nx * -0.4f + 0.5f * nz);
    float hl = nx * -0.35f + ny * 0.55f + nz * 0.75f;
    hl = hl > 0.0f ? hl : 0.0f;
    hl = hl * hl;
    hl = hl * hl;
    e += 0.55f * hl;
    *r = (float)m->r * (1.0f / 255.0f) * e * 1.5f;
    *g = (float)m->g * (1.0f / 255.0f) * e * 1.5f;
    *b = (float)m->b * (1.0f / 255.0f) * e * 1.5f;
}

// Draws weapon k (0 handgun, 1 SMG, 2 shotgun, 3 rocket launcher, 4 C4) turned by `turn` / WM_TURN of a
// full circle, centred on (cx, cy) of the menu texture.
static void wm_draw(volatile unsigned short *t, int k, int turn, int cx, int cy) {
    if (k < 0 || k >= WM_COUNT) return;
    const WmVtx *V;
    const WmTri *T;
    wm_get(k, &V, &T);
    int nv = wm_nv[k], nt = wm_nt[k], refl = wm_refl[k];
    if (nv > WM_MAXV) return;

    float ang = (float)turn * (6.28318531f / (float)WM_TURN) + 1.2f;
    float ca = wm_cos(ang), sa = lm_sin(ang);
    const float tilt = 0.20f;      // the game turns these weapons flat; a little tilt shows their depth
    float ct = wm_cos(tilt), st = lm_sin(tilt);

    float ox = (float)wm_ctr[k][0], oy = (float)wm_ctr[k][1], oz = (float)wm_ctr[k][2];
    float rh = (float)wm_ctr[k][3];                   // reach while it spins
    float hy = (float)wm_ctr[k][4];                   // half height
    float dist = rh * 4.0f;                           // camera distance (mild perspective)
    float mag = dist / (dist - rh);                   // the nearest point is drawn this much bigger
    // scale so the model fits its area whatever its turn
    float scale = (float)(WM_BW / 2 - 2) / (rh * mag);
    float sv = (float)(WM_BH / 2 - 2) / ((hy * ct + rh * st) * mag + 0.0001f);
    if (sv < scale) scale = sv;

    for (int i = 0; i < nv; i++) {
        float x = (float)V[i].x - ox, y = (float)V[i].y - oy, z = (float)V[i].z - oz;
        // spin about Y, then the fixed tilt about X
        float x1 = x * ca + z * sa;
        float z1 = -x * sa + z * ca;
        float y2 = y * ct - z1 * st;
        float z2 = y * st + z1 * ct;
        float nx = (float)V[i].nx * (1.0f / 127.0f), ny = (float)V[i].ny * (1.0f / 127.0f), nz = (float)V[i].nz * (1.0f / 127.0f);
        float mx = nx * ca + nz * sa;
        float mz1 = -nx * sa + nz * ca;
        float my = ny * ct - mz1 * st;
        float mz = ny * st + mz1 * ct;
        float depth = dist - z2;
        if (depth < 1.0f) depth = 1.0f;
        float f = scale * dist / depth;
        wm_sx[i] = (float)(WM_BW / 2) + x1 * f;
        wm_sy[i] = (float)(WM_BH / 2) - y2 * f;
        wm_sz[i] = depth;
        float d1 = mx * WM_L1X + my * WM_L1Y + mz * WM_L1Z;
        float d2 = mx * WM_L2X + my * WM_L2Y + mz * WM_L2Z;
        float sh = WM_AMB + (d1 > 0.0f ? d1 * WM_L1C : 0.0f) + (d2 > 0.0f ? d2 * WM_L2C : 0.0f);
        sh = wm_clamp1(sh);
        // glint: stands in for the game's translucent reflection pass (a light from the upper left front)
        float hx = WM_L1X, hy = WM_L1Y, hz = WM_L1Z + 1.0f;
        float hn = 1.0f / (hx * hx + hy * hy + hz * hz);
        float nh = (mx * hx + my * hy + mz * hz) * hn;
        float gl = nh > 0.0f ? nh * 1.4f : 0.0f;
        if (gl > 1.0f) gl = 1.0f;
        gl = gl * gl; gl = gl * gl; gl = gl * gl;
        wm_sp[i] = gl;
        wm_lr[i] = wm_lg[i] = wm_lb[i] = sh;
        wm_nx[i] = mx; wm_ny[i] = my; wm_nz[i] = mz;
    }

    for (int i = 0; i < WM_BW * WM_BH; i++) { wm_col[i] = 0; wm_zb[i] = 1.0e9f; }

    for (int ti = 0; ti < nt; ti++) {
        int a = T[ti].a, b = T[ti].b, c = T[ti].c;
        const WmMat *m = &wm_mat[T[ti].mat];
        float ax = wm_sx[a], ay = wm_sy[a], bx = wm_sx[b], by = wm_sy[b], cx2 = wm_sx[c], cy2 = wm_sy[c];
        float area = (bx - ax) * (cy2 - ay) - (by - ay) * (cx2 - ax);
        if (area > -0.0001f && area < 0.0001f) continue;
        // y points down on screen: front faces run clockwise there (counter-clockwise in the game's y-up space)
        if ((m->flags & 1) && area < 0.0f) continue;
        float minx = ax < bx ? ax : bx; if (cx2 < minx) minx = cx2;
        float maxx = ax > bx ? ax : bx; if (cx2 > maxx) maxx = cx2;
        float miny = ay < by ? ay : by; if (cy2 < miny) miny = cy2;
        float maxy = ay > by ? ay : by; if (cy2 > maxy) maxy = cy2;
        int x0 = wm_floor(minx), x1 = wm_floor(maxx) + 1, y0 = wm_floor(miny), y1 = wm_floor(maxy) + 1;
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 > WM_BW) x1 = WM_BW;
        if (y1 > WM_BH) y1 = WM_BH;
        if (x0 >= x1 || y0 >= y1) continue;
        float inv = 1.0f / area;
        float ua = 0, va = 0, ub = 0, vb = 0, uc = 0, vc = 0;
        if (m->kind == 0) {
            float ku = (float)m->ss * (1.0f / 2097152.0f), kv = (float)m->ts * (1.0f / 2097152.0f);
            ua = (float)V[a].s * ku; va = (float)V[a].t * kv;
            ub = (float)V[b].s * ku; vb = (float)V[b].t * kv;
            uc = (float)V[c].s * ku; vc = (float)V[c].t * kv;
        }
        for (int y = y0; y < y1; y++) {
            float py = (float)y + 0.5f;
            for (int x = x0; x < x1; x++) {
                float px = (float)x + 0.5f;
                float w0 = ((bx - px) * (cy2 - py) - (by - py) * (cx2 - px)) * inv;
                float w1 = ((cx2 - px) * (ay - py) - (cy2 - py) * (ax - px)) * inv;
                float w2 = 1.0f - w0 - w1;
                if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) continue;
                float z = w0 * wm_sz[a] + w1 * wm_sz[b] + w2 * wm_sz[c];
                int o = y * WM_BW + x;
                if (z >= wm_zb[o]) continue;
                float r, g, bl;
                float sr = w0 * wm_lr[a] + w1 * wm_lr[b] + w2 * wm_lr[c];
                float sg = w0 * wm_lg[a] + w1 * wm_lg[b] + w2 * wm_lg[c];
                float sb = w0 * wm_lb[a] + w1 * wm_lb[b] + w2 * wm_lb[c];
                if (m->kind == 0) {
                    int cut;
                    float tr, tg, tb;
                    wm_texel(m, w0 * ua + w1 * ub + w2 * uc, w0 * va + w1 * vb + w2 * vc, &cut, &tr, &tg, &tb);
                    if (cut) continue;
                    r = tr * sr; g = tg * sg; bl = tb * sb;
                } else if (m->kind == 1) {
                    r = (float)m->r * (1.0f / 255.0f) * sr; g = (float)m->g * (1.0f / 255.0f) * sg; bl = (float)m->b * (1.0f / 255.0f) * sb;
                } else if (m->kind == 2) {
                    r = (float)m->r * (1.0f / 255.0f); g = (float)m->g * (1.0f / 255.0f); bl = (float)m->b * (1.0f / 255.0f);
                } else {
                    float nx = w0 * wm_nx[a] + w1 * wm_nx[b] + w2 * wm_nx[c];
                    float ny = w0 * wm_ny[a] + w1 * wm_ny[b] + w2 * wm_ny[c];
                    float nz = w0 * wm_nz[a] + w1 * wm_nz[b] + w2 * wm_nz[c];
                    float er, eg, eb;
                    wm_env(m, nx, ny, nz, &er, &eg, &eb);
                    r = er * (0.6f + 0.4f * sr); g = eg * (0.6f + 0.4f * sg); bl = eb * (0.6f + 0.4f * sb);
                }
                if (m->kind != 2) {
                    float gl = (w0 * wm_sp[a] + w1 * wm_sp[b] + w2 * wm_sp[c]) * 0.55f;
                    float sheen = 0.0f;
                    if (refl && m->kind != 3) {
                        // stand-in for the game's translucent reflection pass: steel-blue studio sheen
                        float nx = w0 * wm_nx[a] + w1 * wm_nx[b] + w2 * wm_nx[c];
                        float ny = w0 * wm_ny[a] + w1 * wm_ny[b] + w2 * wm_ny[c];
                        float nz = w0 * wm_nz[a] + w1 * wm_nz[b] + w2 * wm_nz[c];
                        sheen = 0.16f + 0.22f * (ny * 0.5f + 0.5f) + 0.10f * (nz > 0.0f ? nz : 0.0f) + 0.06f * (nx * -0.5f + 0.5f);
                    }
                    r = r * WM_GAIN + gl + sheen * 0.85f; g = g * WM_GAIN + gl + sheen * 0.92f; bl = bl * WM_GAIN + gl + sheen;
                }
                int ir = (int)(wm_clamp1(r) * 31.0f + 0.5f), ig = (int)(wm_clamp1(g) * 31.0f + 0.5f), ib = (int)(wm_clamp1(bl) * 31.0f + 0.5f);
                wm_zb[o] = z;
                wm_col[o] = (unsigned short)((ir << 11) | (ig << 6) | (ib << 1) | 1);
            }
        }
    }

    // down to the menu: average the covered samples, coverage is the blend amount
    int x0 = cx - (WM_W >> 1), y0 = cy - (WM_H >> 1);
    for (int y = 0; y < WM_H; y++)
        for (int x = 0; x < WM_W; x++) {
            int sr = 0, sg = 0, sb = 0, n = 0;
            for (int j = 0; j < WM_SS; j++)
                for (int i = 0; i < WM_SS; i++) {
                    unsigned short p = wm_col[(y * WM_SS + j) * WM_BW + x * WM_SS + i];
                    if (!(p & 1)) continue;
                    sr += (p >> 11) & 31; sg += (p >> 6) & 31; sb += (p >> 1) & 31; n++;
                }
            if (!n) continue;
            float rn = 8.0f / (float)n;   // no integer divide by a variable
            LmCol c = { (int)((float)sr * rn), (int)((float)sg * rn), (int)((float)sb * rn) };
            lm_blend(t, x0 + x, y0 + y, c, (n * 255) >> 2);
        }
}

#endif
