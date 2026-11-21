// Partner radar: draws a 64x64 RGBA5551 texture with the other player on it.
//
// Pure drawing code (no game memory access, no libm), so it also builds on a PC for testing.
// coop.c fills a RadarIn from the two players and calls radar_render() on the sprite texture.
#ifndef RADAR_H
#define RADAR_H

#include "map_data.h"

#define RADAR_N 64

typedef struct {
    float dx, dz;      // other player's position minus this player's (world X and Z)
    float heading;     // this player's heading (radians, as stored at player + 0x64)
    float range;       // world distance that reaches the edge of the radar
    int viewer;        // 0 = Player 1 (blue), 1 = Player 2 (orange)
    int other_room;    // 1 when the partner is in another room
    int other_dead;    // 1 when the partner is down
    int north_up;      // 0 = heading-up (the radar turns with you), 1 = world axes fixed
    int rot90;         // 0..3: extra quarter turns, to match the game's heading convention
    int mirror;        // 1 = flip the heading's turn direction
    int has_face;      // 1 = face_x/face_y is the character's facing (unit vector, map axes)
    float face_x, face_y;
    int has_fwd;       // 1 = use fwd_x/fwd_y (unit vector, map axes) instead of heading
    float fwd_x, fwd_y;
    int map_img;       // level map image (map_data.h), -1 = none
    unsigned int room_mask;   // rooms (bit = room index) whose walls are drawn
    int cur_room;      // this player's room: drawn brighter
    float wx, wz;      // this player's absolute position (map axes)
    float zsign;       // -1: map z = -world Z (default), +1: flipped
    int use_map;       // 1 = paint the explored-area map under the dots
    volatile const unsigned char *grid;   // explored cells (RADAR_GRID_N x RADAR_GRID_N bytes, 0 = unknown)
    float gx, gz;      // this player's position relative to the grid centre (map axes)
} RadarIn;

#define RADAR_GRID_N 256
#define RADAR_CELL   50.0f   // world units per map cell

// RGBA5551 with the alpha bit set (opaque).
#define RADAR_RGB(r, g, b) \
    ((unsigned short)(((((r) >> 3) & 0x1F) << 11) | ((((g) >> 3) & 0x1F) << 6) | ((((b) >> 3) & 0x1F) << 1) | 1))

static float radar_wrap(float a) {
    const float pi = 3.14159265f;
    const float two_pi = 6.28318531f;
    if (!(a == a) || a > 1000.0f || a < -1000.0f) {   // NaN or nonsense
        return 0.0f;
    }
    while (a > pi) a -= two_pi;
    while (a < -pi) a += two_pi;
    return a;
}

static float radar_sin(float a) {
    a = radar_wrap(a);
    float y = 1.27323954f * a - 0.405284735f * a * (a < 0.0f ? -a : a);
    float ay = y < 0.0f ? -y : y;
    return 0.225f * (ay * y - y) + y;
}

static float radar_cos(float a) {
    return radar_sin(a + 1.57079633f);
}

static float radar_sqrt(float v) {
    if (v <= 0.0f) {
        return 0.0f;
    }
    union { float f; unsigned int i; } u;
    u.f = v;
    u.i = 0x5F3759DFu - (u.i >> 1);
    float y = u.f;
    float h = 0.5f * v;
    y = y * (1.5f - h * y * y);
    y = y * (1.5f - h * y * y);
    y = y * (1.5f - h * y * y);
    return v * y;
}

static float radar_abs(float v) {
    return v < 0.0f ? -v : v;
}

static void radar_accent(int who, int *r, int *g, int *b) {
    if (who == 0) { *r = 70; *g = 165; *b = 255; }   // Player 1: blue (same as the HUD)
    else          { *r = 255; *g = 140; *b = 50; }   // Player 2: orange
}

// Round dot at (cx, cy) in texel units, solid or hollow, with a dark outline.
static void radar_dot(volatile unsigned short *tex, float cx, float cy, float r, int hollow,
                      unsigned short col, unsigned short dark) {
    int x0 = (int)(cx - r - 2.0f), x1 = (int)(cx + r + 2.0f);
    int y0 = (int)(cy - r - 2.0f), y1 = (int)(cy + r + 2.0f);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > RADAR_N - 1) x1 = RADAR_N - 1;
    if (y1 > RADAR_N - 1) y1 = RADAR_N - 1;
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            float fx = (float)x + 0.5f - cx;
            float fy = (float)y + 0.5f - cy;
            float d2 = fx * fx + fy * fy;
            float ro = r + 1.0f;
            if (d2 > ro * ro) {
                continue;
            }
            if (d2 > r * r) {
                tex[y * RADAR_N + x] = dark;                    // outline
            } else if (hollow && d2 < (r - 1.6f) * (r - 1.6f)) {
                tex[y * RADAR_N + x] = dark;                    // hole
            } else {
                tex[y * RADAR_N + x] = col;
            }
        }
    }
}

// Straight line in texel coordinates, only inside the round radar.
static void radar_line(volatile unsigned short *tex, float x0, float y0, float x1, float y1, unsigned short col) {
    if ((x0 < -8.0f && x1 < -8.0f) || (x0 > 72.0f && x1 > 72.0f) ||
        (y0 < -8.0f && y1 < -8.0f) || (y0 > 72.0f && y1 > 72.0f)) {
        return;
    }
    float ddx = x1 - x0, ddy = y1 - y0;
    float adx = radar_abs(ddx), ady = radar_abs(ddy);
    float lenf = adx > ady ? adx : ady;
    int n = (int)(lenf * 1.5f) + 1;
    if (n > 400) {
        n = 400;
    }
    float sx = ddx / (float)n, sy = ddy / (float)n;
    float x = x0, y = y0;
    for (int i = 0; i <= n; i++) {
        if (x >= 0.0f && y >= 0.0f) {
            int ix = (int)x, iy = (int)y;
            if (ix < RADAR_N && iy < RADAR_N) {
                int ax = 2 * ix - 63, ay = 2 * iy - 63;
                if (ax * ax + ay * ay <= 3025) {
                    tex[iy * RADAR_N + ix] = col;
                }
            }
        }
        x += sx;
        y += sy;
    }
}

static void radar_render(volatile unsigned short *tex, const RadarIn *in) {
    int vr, vg, vb;
    radar_accent(in->viewer, &vr, &vg, &vb);
    int pr, pg, pb;
    radar_accent(in->viewer ^ 1, &pr, &pg, &pb);

    const unsigned short c_out = (unsigned short)(RADAR_RGB(vr, vg, vb) & 0xFFFEu);   // alpha 0, edge colour
    const unsigned short c_ring = RADAR_RGB(vr, vg, vb);
    const unsigned short c_edge = RADAR_RGB(vr * 5 / 10, vg * 5 / 10, vb * 5 / 10);
    const unsigned short c_fill = RADAR_RGB(9, 14, 24);
    const unsigned short c_grid = RADAR_RGB(30, 42, 62);
    const unsigned short c_dark = RADAR_RGB(4, 6, 10);

    // Heading with the game's convention corrected: mirror, then quarter turns.
    float sh, ch;
    if (in->has_fwd) {
        sh = in->fwd_x;
        ch = in->fwd_y;
        for (int k = 0; k < (in->rot90 & 3); k++) {
            float t = sh;
            sh = ch;
            ch = -t;
        }
    } else {
        float h = in->mirror ? -in->heading : in->heading;
        h += (float)(in->rot90 & 3) * 1.57079633f;
        sh = radar_sin(h);
        ch = radar_cos(h);
    }
    const float edge = 25.5f;   // texels from the centre where a dot sits at maximum range
    const float unit = in->range / edge;   // world units per texel
    const unsigned short c_mapc = RADAR_RGB(46, 88, 138);
    const unsigned short c_mapg = RADAR_RGB(70, 112, 164);

    // Background: disc, outer ring, range ring and cross hair.
    for (int y = 0; y < RADAR_N; y++) {
        for (int x = 0; x < RADAR_N; x++) {
            int ax = 2 * x - 63;
            int ay = 2 * y - 63;
            int r2 = ax * ax + ay * ay;
            unsigned short c;
            if (r2 > 3600) {
                c = c_out;
            } else if (r2 > 3249) {
                c = c_ring;
            } else if (r2 > 3025) {
                c = c_edge;
            } else {
                int vis = 0;
                if (in->use_map && in->grid) {
                    float sx = (float)ax * 0.5f * unit;
                    float sy = (float)(-ay) * 0.5f * unit;
                    float wx, wz;
                    if (in->north_up) {
                        wx = sx;
                        wz = sy;
                    } else {
                        wx = sx * ch + sy * sh;
                        wz = -sx * sh + sy * ch;
                    }
                    float fx = (in->gx + wx) * (1.0f / RADAR_CELL) + (RADAR_GRID_N / 2) + 0.5f;
                    float fz = (in->gz + wz) * (1.0f / RADAR_CELL) + (RADAR_GRID_N / 2) + 0.5f;
                    if (fx >= 0.0f && fz >= 0.0f) {
                        int ix = (int)fx, iz = (int)fz;
                        if (ix < RADAR_GRID_N && iz < RADAR_GRID_N) {
                            vis = in->grid[iz * RADAR_GRID_N + ix] != 0;
                        }
                    }
                }
                int line = (r2 >= 841 && r2 <= 961) || ax == 1 || ax == -1 || ay == 1 || ay == -1;
                c = line ? (vis ? c_mapg : c_grid) : (vis ? c_mapc : c_fill);
            }
            tex[y * RADAR_N + x] = c;
        }
    }

    // Level layout: the game's own map-screen lines (same world X/Z space as the players), one group of
    // lines per room. Only rooms in room_mask are drawn; the player's own room is brighter.
    if (in->map_img >= 0 && in->map_img < MAP_IMAGES) {
        const unsigned short *mi = map_img[in->map_img];
        const unsigned short c_line = RADAR_RGB(120, 172, 228);
        const unsigned short c_cur = RADAR_RGB(190, 228, 255);
        const float inv_unit = 1.0f / unit;
        for (int r = 0; r < (int)mi[5]; r++) {
            if (r < 32 && !((in->room_mask >> r) & 1u)) {
                continue;
            }
            int l0 = (int)map_f[2 * (mi[4] + r)];
            int ln = (int)map_f[2 * (mi[4] + r) + 1];
            unsigned short col = (r == in->cur_room) ? c_cur : c_line;
            for (int k = l0; k < l0 + ln; k++) {
                int a = (int)map_l[2 * (mi[2] + k)] + (int)mi[0];
                int b = (int)map_l[2 * (mi[2] + k) + 1] + (int)mi[0];
                float ax0 = (float)map_v[2 * a] - in->wx;
                float az0 = in->zsign * (float)map_v[2 * a + 1] - in->wz;
                float ax1 = (float)map_v[2 * b] - in->wx;
                float az1 = in->zsign * (float)map_v[2 * b + 1] - in->wz;
                float u0, v0, u1, v1;
                if (in->north_up) {
                    u0 = ax0; v0 = az0; u1 = ax1; v1 = az1;
                } else {
                    u0 = ax0 * ch - az0 * sh;
                    v0 = ax0 * sh + az0 * ch;
                    u1 = ax1 * ch - az1 * sh;
                    v1 = ax1 * sh + az1 * ch;
                }
                radar_line(tex, 32.0f + u0 * inv_unit, 32.0f - v0 * inv_unit,
                                32.0f + u1 * inv_unit, 32.0f - v1 * inv_unit, col);
            }
        }
    }

    // Partner position on the radar (x to the right, y up), in world units.
    float sx, sy;
    float fx, fy;   // direction this player faces on the radar
    if (in->north_up) {
        sx = in->dx;
        sy = in->dz;
        fx = sh;
        fy = ch;
        if (in->has_face) {
            fx = in->face_x;
            fy = in->face_y;
        }
    } else {
        sx = in->dx * ch - in->dz * sh;
        sy = in->dx * sh + in->dz * ch;
        fx = 0.0f;
        fy = 1.0f;
        if (in->has_face) {
            // the character's facing, seen from the camera: up = camera forward
            fx = in->face_x * ch - in->face_y * sh;
            fy = in->face_x * sh + in->face_y * ch;
        }
    }

    float dist = radar_sqrt(sx * sx + sy * sy);
    int clamped = dist > in->range;
    float scale = (clamped ? edge / dist : edge / in->range);
    float px = sx * scale;
    float py = sy * scale;

    // This player: a small arrow in the centre, pointing where they face.
    unsigned short c_me = RADAR_RGB(vr, vg, vb);
    for (int y = 20; y < 44; y++) {
        for (int x = 20; x < 44; x++) {
            float qx = (float)x + 0.5f - 32.0f;
            float qy = 32.0f - ((float)y + 0.5f);
            float a = qx * fx + qy * fy;        // along the facing direction
            float b = qx * fy - qy * fx;        // across it
            float half = (6.5f - a) * 0.55f;
            if (a >= -3.0f && a <= 6.5f && radar_abs(b) <= half) {
                tex[y * RADAR_N + x] = (radar_abs(b) > half - 1.0f || a < -2.0f || a > 5.5f) ? c_dark : c_me;
            }
        }
    }

    // Partner: solid dot, hollow when in another room, grey when down, small when beyond range.
    unsigned short c_other = in->other_dead ? RADAR_RGB(130, 134, 144) : RADAR_RGB(pr, pg, pb);
    float dot_r = clamped ? 2.4f : 3.2f;
    radar_dot(tex, 32.0f + px, 32.0f - py, dot_r, in->other_room, c_other, c_dark);
}

#endif
