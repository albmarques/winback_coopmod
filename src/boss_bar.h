// Boss health bar for co-op: centred on the screen, on the split between the two views.
// Included from coop.c (uses U8/U16/U32/S16, func_8007B8F4, hud_anchor).
//
// The game's own boss bar (func_800CF76C, row 1 of the shown-health table) isn't drawn by the
// Versus HUD the co-op uses. The values it reads:
//   D_801D362C (u32)  bar on: set when the boss is made (func_8002C86C, func_800ADE7C), cleared by stage init
//   D_801BE480 (u8)   boss index (character type +0x16F5 - 0x2B), picks a 6-byte entry of D_8010F668
//   D_801BE482 (s16)  boss health; starts at the entry's first byte (func_800ADA40), so that byte is the maximum

#define BOSS_ON      0x801D362Cu
#define BOSS_INDEX   0x801BE480u
#define BOSS_HEALTH  0x801BE482u
#define BOSS_TABLE   0x8010F668u

#include "boss_tex_data.h"   // frame drawn by tools/make_boss_hud.py (same style as the LIFE bar)

#define BOSS_SPR 0x80512E00u
#define BOSS_TEX 0x80580000u   // 288 * 38 * 2 = 0x5580 bytes

static int boss_shown10 = -1;          // eased health x 10 (the trail)
static int boss_last_fill = -1, boss_last_trail = -1;

// Health rows painted over the base frame, like hud_update_health_bar: 4 shades, light on top.
static void bb_render(int fill, int trail) {
    static const unsigned short shade[4] = {
        RGBA5551(255, 120, 95), RGBA5551(238, 58, 44), RGBA5551(208, 36, 30), RGBA5551(150, 20, 22),
    };
    const unsigned short c_trail = RGBA5551(240, 196, 96);
    for (int row = 0; row < 8; row++) {
        int ty = BOSS_BAR_ROW0 + row;
        int x0 = boss_bar_x0[row], x1 = boss_bar_x1[row];
        unsigned short c = shade[(row == 0) ? 0 : (row <= 2) ? 1 : (row <= 5) ? 2 : 3];
        for (int tx = x0; tx <= x1; tx++) {
            int i = tx - x0;
            int idx = ty * BOSS_W + tx;
            unsigned short px = (i < fill) ? c : (i < trail) ? c_trail : boss_base[idx];
            U16(BOSS_TEX + (unsigned int)idx * 2u) = px;
        }
    }
}

static void bb_sprite_init(void) {
    for (int i = 0; i < BOSS_W * BOSS_H; i++) U16(BOSS_TEX + (unsigned int)i * 2u) = boss_base[i];
    U8(BOSS_SPR + 0x00) = 0;               // G_IM_FMT_RGBA
    U8(BOSS_SPR + 0x01) = 2;               // G_IM_SIZ_16b
    U16(BOSS_SPR + 0x02) = BOSS_W;
    U16(BOSS_SPR + 0x04) = BOSS_H;
    U16(BOSS_SPR + 0x06) = 0;
    U32(BOSS_SPR + 0x08) = BOSS_TEX;
    U32(BOSS_SPR + 0x0C) = 0;
    U16(BOSS_SPR + 0x10) = 0x0001;         // visible + auto RDP setup
    U16(BOSS_SPR + 0x12) = 0;
    U16(BOSS_SPR + 0x14) = 0;
    U16(BOSS_SPR + 0x16) = 0;
    U32(BOSS_SPR + 0x18) = 0x3F000000u;    // 0.5: 144 x 19 on the 320 x 240 screen
    U32(BOSS_SPR + 0x1C) = 0x3F000000u;
    U8(BOSS_SPR + 0x20) = 255;
    U8(BOSS_SPR + 0x21) = 255;
    U8(BOSS_SPR + 0x22) = 255;
    U8(BOSS_SPR + 0x23) = 255;
    U32(BOSS_SPR + 0x24) = 0;
}

// Once a frame, after the split divider (see coop_divider_end).
static void boss_bar_draw(void) {
    if (U32(BOSS_ON) == 0 || recomp_get_config_u32("boss_bar") != 0) {
        boss_shown10 = -1;
        return;
    }
    int idx = U8(BOSS_INDEX);
    int max = U8(BOSS_TABLE + (unsigned int)idx * 6u);
    int hp = S16(BOSS_HEALTH);
    if (max <= 0) return;
    if (hp < 0) hp = 0;
    if (hp > max) max = hp;
    int hp10 = hp * 10;
    if (boss_shown10 < 0 || boss_shown10 < hp10) {
        boss_shown10 = hp10;
    } else if (boss_shown10 > hp10) {
        int step = (boss_shown10 - hp10) >> 4;
        boss_shown10 -= (step < 3) ? 3 : step;
        if (boss_shown10 < hp10) boss_shown10 = hp10;
    }
    if (hp == 0 && boss_shown10 == 0) return;       // beaten: hide once the trail has run out

    int span = boss_bar_x1[0] - boss_bar_x0[0] + 1;
    float inv = 1.0f / ((float)max * 10.0f);         // float: no integer divide
    int fill = (int)((float)hp10 * inv * (float)span + 0.5f);
    int trail = (int)((float)boss_shown10 * inv * (float)span + 0.5f);
    if (U16(BOSS_SPR + 0x02) != BOSS_W || U32(BOSS_SPR + 0x08) != BOSS_TEX) {
        bb_sprite_init();
        boss_last_fill = boss_last_trail = -1;
    }
    if (fill != boss_last_fill || trail != boss_last_trail) {
        bb_render(fill, trail);
        boss_last_fill = fill;
        boss_last_trail = trail;
    }
    // Centred on the split (y 110..129). Drawn right after the game's black divider line, so the
    // line doesn't cover it; the scissor there is the versus HUD's (14,14)-(306,230) and RT64 keeps
    // rectangles drawn outside its widescreen HUD block in the 4:3 centre, so no anchor is needed.
    U16(BOSS_SPR + 0x10) = 0x0001;          // keep the RDP setup header (like the life bars)
    func_8007B8F4(0x801D2C80u, BOSS_SPR, 160 - 72, 110);   // 144 x 19
}

// func_800B5400 (versus HUD) draws, after both players' HUDs, the black line between the two views:
// func_8007CBEC(&D_801D2C80, 0, 0x77, 0x140, 0x77, 4). The boss bar is drawn right after it.
static int divider_call;
RECOMP_HOOK("func_8007CBEC") void coop_divider_begin(unsigned int dlpp, int x1, int y1, int x2) {
    divider_call = (dlpp == 0x801D2C80u && x1 == 0 && y1 == 0x77 && x2 == 0x140 && U8(PLAYERS) == 2);
}

RECOMP_HOOK_RETURN("func_8007CBEC") void coop_divider_end(void) {
    if (divider_call) {
        divider_call = 0;
        boss_bar_draw();
    }
}
