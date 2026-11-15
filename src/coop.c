// Story co-op (v0.2.0 + Engineer 0x3A rendering fix):
// A second player in Story mode with split screen, Death Match HUD,
// Engineer (model 0x3A) for Player 2, enemy AI targeting/visibility for both
// players, and optional friendly fire.
#include "winback_mod.h"

#define U8(a)  (*(volatile unsigned char *)(a))
#define S8(a)  (*(volatile signed char *)(a))
#define U16(a) (*(volatile unsigned short *)(a))
#define S16(a) (*(volatile signed short *)(a))
#define U32(a) (*(volatile unsigned int *)(a))
#define F32(a) (*(volatile float *)(a))

#define MODE        0x8016A40E  // u8: bit 0 Story, bit 1 two-view split
#define STATE       0x8016A41C  // u16: bit 0 set during normal play
#define PLAYERS     0x8016A4EE  // u8
#define ROOM_STATE  0x8016A410  // u32 per player (room u16 + subroom u16)
#define START_POS   0x8016A424  // 12 bytes per player
#define START_ID    0x8016A39A  // u16 per player
#define START_HEAD  0x8016A454  // float per player
#define START_CAM   0x8016A464  // 12 bytes per player
#define PLAYER_PTRS 0x801390C0  // object pointer per player
#define CAM_PTRS    0x80179280  // camera pointer per player
#define SLOT0_DATA  0x80179350  // Player 1 character data
#define SLOT1_DATA  0x8017ACD0  // Player 2 character data
#define BTN_NEW_P1  0x80187FB0  // u16 newly pressed buttons P1
#define BTN_NEW_P2  0x80187FB2  // u16 newly pressed buttons P2
#define BTN_MASK_P1 0x80187FC0  // u16 button mask P1
#define BTN_MASK_P2 0x80187FC2  // u16 button mask P2
#define CUTSCENE_FLAG 0x8010EDE4 // u8: non-zero during mid-stage cutscene (mode 3)
#define CUTSCENE_CAM  0x8011F784 // u8: non-zero when cutscene camera/actor is active
#define SCRIPT_FLAGS  0x8010E0F4 // u16: bit 4 (0x0010) set while a stage script/cutscene runs
#define DEATH_FLAG    0x8010E0FA // u8: set to 1 by func_8009CBA0 when a player dies

// Base game functions resolved via relocations:
extern unsigned int func_80027470(int list, int group, int size);
extern void func_800A8B10(unsigned int player);
extern void func_8004D6E8(unsigned int player, int slot);
extern void func_8006B374(unsigned int player);
extern void func_8006B3B8(unsigned int player);
extern void func_8006B564(unsigned int player);

static int spawn_calls;
static unsigned char drawing_split;
static unsigned char saved_a495;
static unsigned char saved_a4c1;
static unsigned char saved_a4bd;
static unsigned char saved_f848;
static unsigned char hud_updating;
static unsigned char swapped_players;
static unsigned int cur_enemy;
static unsigned char bullet_split;
static unsigned char damage_story_mode;
static unsigned int lock_other_player;
static unsigned short lock_saved_in_use;
static unsigned int cam_other_player;
static unsigned int cam_saved_fc;
static unsigned int last_log;

// Cover, item pickup, and cutscene state:
static unsigned char cover_split;
static unsigned char room_split;
static unsigned char room_swapped;
static unsigned char box_room_swapped;
static unsigned int kick_other_player;
static unsigned short kick_saved_in_use;
static int item_swap_depth;
static unsigned char evt_mgr_saved;
static unsigned short evt_saved_new_p1;
static unsigned short evt_saved_mask_p1;
static int trigger_player;
static int trigger_ttl;
static int cutscene_protagonist;
static unsigned char in_cutscene;
static unsigned char c2e4_swapped;
static unsigned char be18_swapped;
static int e1e0_mode;
static int tele_idx;

static int story_play(void) {
    return (U8(MODE) & 1) && (U16(STATE) & 1) && !in_cutscene;
}

static int coop_active(void) {
    return story_play() && U8(PLAYERS) == 2;
}

static int coop_story_session(void) {
    return (U8(MODE) & 1) && U8(PLAYERS) == 2;
}

static void swap_player_ptrs_and_rooms(void) {
    unsigned int p0 = U32(PLAYER_PTRS);
    unsigned int p1 = U32(PLAYER_PTRS + 4);
    U32(PLAYER_PTRS) = p1;
    U32(PLAYER_PTRS + 4) = p0;
    unsigned int r0 = U32(ROOM_STATE);
    unsigned int r1 = U32(ROOM_STATE + 4);
    U32(ROOM_STATE) = r1;
    U32(ROOM_STATE + 4) = r0;
}

static void swap_f32_pair(unsigned int addr) {
    float a = F32(addr);
    float b = F32(addr + 4);
    F32(addr) = b;
    F32(addr + 4) = a;
}

static void swap_all_player_state(void) {
    swap_player_ptrs_and_rooms();
    unsigned int c0 = U32(CAM_PTRS);
    unsigned int c1 = U32(CAM_PTRS + 4);
    U32(CAM_PTRS) = c1;
    U32(CAM_PTRS + 4) = c0;
    swap_f32_pair(0x8016D6D8); // Camera Eye X
    swap_f32_pair(0x801B0CE8); // Camera Eye Y
    swap_f32_pair(0x80188FB0); // Camera Eye Z
    swap_f32_pair(0x80168748); // Camera LookAt X
    swap_f32_pair(0x8016D268); // Camera LookAt Y
    swap_f32_pair(0x801C3808); // Camera LookAt Z
}

static void ensure_normal_player_order(void) {
    if (U32(PLAYER_PTRS) == SLOT1_DATA && U32(PLAYER_PTRS + 4) == SLOT0_DATA) {
        swap_all_player_state();
    }
}

static void set_protagonist(int p) {
    U8(SLOT0_DATA + 0x18CC) = (p == 0) ? 0xFF : 0xFE;
    U8(SLOT1_DATA + 0x18CC) = (p == 1) ? 0xFF : 0xFE;
}

static void restore_engineer_model(unsigned int p2) {
    if (recomp_get_config_u32("p2_character") != 0 || p2 == 0) {
        return;
    }
    unsigned int dl = U32(0x80138EEC);
    if (dl != 0) {
        U32(p2 + 0x188C) = dl;
        U32(p2 + 0x1890) = U32(0x80138EF0);
        U32(p2 + 0x1894) = U32(0x80138EF4);
        U8(p2 + 0x16F5) = 0x3A;
    }
}

// 1. Story's own player count while the stage loads.
RECOMP_HOOK("func_8004C438") void coop_stage_load(void) {
    if (U8(MODE) & 1) {
        U8(PLAYERS) = 1;
        trigger_player = 0;
        trigger_ttl = 0;
        cutscene_protagonist = 0;
        in_cutscene = 0;
        U32(0x80138EEC) = 0;
    }
}

// 2. Player 2's start point, player count, and Versus HUD object creation.
RECOMP_HOOK("func_8006B310") void coop_stage_players(void) {
    if (!(U8(MODE) & 1) || !(U16(STATE) & 1)) {
        return;
    }
    float off = (float)recomp_get_config_double("spawn_offset");
    F32(START_POS + 12 + 0) = F32(START_POS + 0) + off;
    F32(START_POS + 12 + 4) = F32(START_POS + 4);
    F32(START_POS + 12 + 8) = F32(START_POS + 8);
    U16(START_ID + 2) = U16(START_ID);
    F32(START_HEAD + 4) = F32(START_HEAD);
    F32(START_CAM + 12 + 0) = F32(START_CAM + 0) + off;
    F32(START_CAM + 12 + 4) = F32(START_CAM + 4);
    F32(START_CAM + 12 + 8) = F32(START_CAM + 8);
    U8(PLAYERS) = 2;
    spawn_calls = 0;
    trigger_player = 0;
    trigger_ttl = 0;
    cutscene_protagonist = 0;
    in_cutscene = 0;

    // "hud" option: 0 = "On", 1 = "Off"
    if (recomp_get_config_u32("hud") == 0) {
        unsigned int obj = func_80027470(3, 4, 0x100);
        if (obj != 0) {
            unsigned int data = U32(obj + 0x2C);
            U8(obj + 0x34) = 0x80;
            U16(data + 0x0A) = 0x10;
            U32(obj + 0x24) = 0x8007BAC4; // No-op update callback
            U32(obj + 0x28) = 0x800B5400; // Versus HUD draw callback
            U8(obj + 0x35) = 1;
            U16(obj + 0x36) |= 0x20;
        }
    }
}

// 3. Keep player 2's slot after enemy spawner setup.
RECOMP_HOOK_RETURN("func_80034364") void coop_keep_slot(void) {
    if (coop_active()) {
        unsigned int p2 = U32(PLAYER_PTRS + 4);
        if (p2 != 0) {
            U16(p2 + 0x92) = 1;
        }
    }
}

// --- Status log ----------------------------------------------------------------

static void put_hex(char *s, unsigned int v, int digits) {
    int i;
    for (i = digits - 1; i >= 0; i--) {
        s[i] = "0123456789ABCDEF"[v & 15];
        v >>= 4;
    }
}

static void log_status(void) {
    if (recomp_get_config_u32("log") == 0) {
        return;
    }
    unsigned int now = winback_mod_time_us();
    if ((unsigned int)(now - last_log) < 1000000u) {
        return;
    }
    last_log = now;
    unsigned int p0 = U32(PLAYER_PTRS);
    unsigned int p1 = U32(PLAYER_PTRS + 4);
    static const char tmpl[] = "coop: p0 0x........ hp 00 slot1 0000 | p1 0x........ hp 00 in_use 0000 map 0000";
    static char line[sizeof(tmpl)];
    unsigned int i;
    for (i = 0; i < sizeof(tmpl); i++) {
        line[i] = tmpl[i];
    }
    put_hex(line + 11, p0, 8);
    put_hex(line + 23, p0 ? (unsigned int)(unsigned char)S8(p0 + 0x176B) : 0, 2);
    put_hex(line + 32, U16(0x8017ACD0 + 0x92), 4);
    put_hex(line + 44, p1, 8);
    put_hex(line + 56, p1 ? (unsigned int)(unsigned char)S8(p1 + 0x176B) : 0, 2);
    put_hex(line + 66, p1 ? U16(p1 + 0x92) : 0, 4);
    put_hex(line + 75, U16(0x8016A41A), 4);
    winback_mod_log(line);
}

static void clear_player_targets(void);

static int get_outdoor_room(int room, int subroom) {
    if (room < 0) {
        room = 0;
    }
    if ((U8(0x801D3765 + (unsigned int)room * 0x58) & 1) == 0) {
        return room;
    }
    if (subroom >= 0 && (U8(0x801D3765 + (unsigned int)subroom * 0x58) & 1) == 0) {
        return subroom;
    }
    return -1;
}

static int room_is_outdoor(int room, int subroom) {
    return get_outdoor_room(room, subroom) >= 0;
}

static unsigned char fb_clear_active;
static unsigned char fb_saved_vp;
static int fb_p0_out;
static int fb_p1_out;
static unsigned int sky_upd_obj;
static unsigned int sky_upd_saved_parent;
static unsigned int sky_upd_player;
static short sky_upd_saved_room;
static unsigned int sky_draw_obj;
static unsigned int sky_draw_saved_parent;
static unsigned int sky_draw_saved_p0;
static unsigned int sky_draw_player;
static short sky_draw_saved_room;

// 4. Two-view split, Engineer buffer protection, and Death Match HUD rules while drawing.
RECOMP_HOOK("func_80025F10") void coop_draw_begin(void) {
    drawing_split = 0;
    if (coop_story_session()) {
        restore_engineer_model(SLOT1_DATA);
        if (in_cutscene && (U16(STATE) & 1) && U8(CUTSCENE_FLAG) == 0 && U8(CUTSCENE_CAM) == 0 && (U16(SCRIPT_FLAGS) & 0x0010) == 0) {
            ensure_normal_player_order();
            set_protagonist(0);
            cutscene_protagonist = 0;
            in_cutscene = 0;
        }
    }
    if (coop_active()) {
        log_status();
        U8(MODE) = 2;
        int p0_out = room_is_outdoor(S16(ROOM_STATE + 0), S16(ROOM_STATE + 2));
        int p1_out = room_is_outdoor(S16(ROOM_STATE + 4), S16(ROOM_STATE + 6));
        U8(0x8016A40F) = (!p0_out && p1_out) ? 1 : 0;
        saved_a495 = U8(0x8016A495);
        saved_a4c1 = U8(0x8016A4C1);
        saved_a4bd = U8(0x8016A4BD);
        saved_f848 = U8(0x8010F848);
        U8(0x8016A495) = 1;
        U8(0x8016A4C1) = 0;
        U8(0x8016A4BD) = 0xFF;
        U8(0x8010F848) = 0x10;
        U8(0x8010F844) = 0;
        U8(0x8010F845) = 0;
        clear_player_targets();
        drawing_split = 1;
    }
}

RECOMP_HOOK_RETURN("func_80025F10") void coop_draw_end(void) {
    if (drawing_split) {
        U8(MODE) = 1;
        U8(0x8016A495) = saved_a495;
        U8(0x8016A4C1) = saved_a4c1;
        U8(0x8016A4BD) = saved_a4bd;
        U8(0x8010F848) = saved_f848;
        drawing_split = 0;
    }
}

// 4a. Per-viewport sky/black background clear and Sky Dome / Cloud rendering when one player is indoors and the other is outdoors.
RECOMP_HOOK("func_80026EB8") void coop_fb_clear_begin(void) {
    fb_clear_active = 0;
    if (!coop_active()) {
        return;
    }
    fb_p0_out = room_is_outdoor(S16(ROOM_STATE + 0), S16(ROOM_STATE + 2));
    fb_p1_out = room_is_outdoor(S16(ROOM_STATE + 4), S16(ROOM_STATE + 6));
    fb_saved_vp = U8(0x8016A40F);
    U8(0x8016A40F) = (!fb_p0_out && fb_p1_out) ? 1 : 0;
    fb_clear_active = 1;
}

RECOMP_HOOK_RETURN("func_80026EB8") void coop_fb_clear_end(void) {
    if (!fb_clear_active) {
        return;
    }
    U8(0x8016A40F) = fb_saved_vp;
    fb_clear_active = 0;
    if (fb_p0_out != fb_p1_out) {
        unsigned int dl = U32(0x801D2C80);
        if (dl != 0) {
            unsigned int uly = fb_p0_out ? 120u : 0u;
            unsigned int lry = fb_p0_out ? 239u : 119u;
            U32(dl + 0)  = 0xF7000000u; // gDPSetFillColor
            U32(dl + 4)  = 0x00010001u; // RGBA5551 black (0,0,0,1)
            U32(dl + 8)  = 0xF6000000u | (319u << 14) | (lry << 2);
            U32(dl + 12) = (uly << 2);
            U32(dl + 16) = 0xE7000000u; // gDPPipeSync
            U32(dl + 20) = 0u;
            U32(0x801D2C80) = dl + 24u;
        }
    }
}

RECOMP_HOOK("func_800C4EE0") void coop_sky_update_begin(unsigned int obj) {
    sky_upd_obj = 0;
    if (!coop_active() || obj == 0) {
        return;
    }
    int out0 = get_outdoor_room(S16(ROOM_STATE + 0), S16(ROOM_STATE + 2));
    int out1 = get_outdoor_room(S16(ROOM_STATE + 4), S16(ROOM_STATE + 6));
    int use_p1 = (out0 < 0 && out1 >= 0) ? 1 : 0;
    int out_room = use_p1 ? out1 : out0;
    unsigned int target_p = U32(PLAYER_PTRS + use_p1 * 4);
    if (target_p != 0 && out_room >= 0) {
        sky_upd_obj = obj;
        sky_upd_saved_parent = U32(obj + 0x20);
        sky_upd_player = target_p;
        sky_upd_saved_room = S16(target_p + 0x8E);
        U32(obj + 0x20) = U32(target_p + 0x1740);
        S16(target_p + 0x8E) = (short)out_room;
    }
}

RECOMP_HOOK_RETURN("func_800C4EE0") void coop_sky_update_end(void) {
    if (sky_upd_obj != 0) {
        U32(sky_upd_obj + 0x20) = sky_upd_saved_parent;
        S16(sky_upd_player + 0x8E) = sky_upd_saved_room;
        sky_upd_obj = 0;
    }
}

RECOMP_HOOK("func_800C4FB0") void coop_sky_draw_begin(unsigned int obj) {
    sky_draw_obj = 0;
    if (!coop_active() || obj == 0) {
        return;
    }
    int vp = U8(0x8016A40F) ? 1 : 0;
    short room = S16(ROOM_STATE + vp * 4);
    short subroom = S16(ROOM_STATE + vp * 4 + 2);
    int out_room = get_outdoor_room(room, subroom);
    unsigned int p_vp = U32(PLAYER_PTRS + vp * 4);
    if (p_vp != 0) {
        sky_draw_obj = obj;
        sky_draw_saved_parent = U32(obj + 0x20);
        sky_draw_saved_p0 = U32(PLAYER_PTRS);
        sky_draw_player = p_vp;
        sky_draw_saved_room = S16(p_vp + 0x8E);
        U32(obj + 0x20) = U32(p_vp + 0x1740);
        U32(PLAYER_PTRS) = p_vp;
        S16(p_vp + 0x8E) = (out_room >= 0) ? (short)out_room : (room >= 0 ? room : 0);
    }
}

RECOMP_HOOK_RETURN("func_800C4FB0") void coop_sky_draw_end(void) {
    if (sky_draw_obj != 0) {
        U32(sky_draw_obj + 0x20) = sky_draw_saved_parent;
        U32(PLAYER_PTRS) = sky_draw_saved_p0;
        S16(sky_draw_player + 0x8E) = sky_draw_saved_room;
        sky_draw_obj = 0;
    }
}

// 4b. Update HUD data (weapon icon, ammo, reload, laser, life bar) for both players.
RECOMP_HOOK("func_800D1E24") void coop_hud_update_begin(void) {
    hud_updating = 0;
    if (coop_active()) {
        U8(MODE) = 2;
        hud_updating = 1;
    }
}

RECOMP_HOOK_RETURN("func_800D1E24") void coop_hud_update_end(void) {
    if (hud_updating) {
        U8(MODE) = 1;
        hud_updating = 0;
    }
}

// 5. Engineer model (0x3A = 58) for Player 2 after second player creation.
RECOMP_HOOK_RETURN("func_800593EC") void coop_p2_model(void) {
    spawn_calls++;
    if (spawn_calls != 2 || !(U8(MODE) & 1) || U8(PLAYERS) != 2) {
        return;
    }
    set_protagonist(0);
    if (recomp_get_config_u32("p2_character") != 0) {
        return;
    }
    unsigned int p2 = U32(PLAYER_PTRS + 4);
    if (p2 == 0) {
        return;
    }
    U8(p2 + 0x16F5) = 0x3A;              // Engineer model ID
    U8(p2 + 0x18CD) &= (unsigned char)~2;// Prevent frame-1 func_800A8B10 buffer reset
    func_800A8B10(p2);                   // Set Engineer DL offset table (0x1744)
    func_8004D6E8(p2, 0x37);             // Decompress Engineer DLs/textures/vertices to 0x806EE400
    func_8006B374(p2);                   // Free Jean-Luc's dynamic joint slot in D_801DC710
    func_8006B3B8(p2);                   // DMA Engineer's dynamic joint vertices into D_801DC710
    func_8006B564(p2);                   // Skin Engineer's initial dynamic joint vertices
    U32(0x80138EEC) = U32(p2 + 0x188C);  // Save Engineer DL buffer pointer
    U32(0x80138EF0) = U32(p2 + 0x1890);  // Save Engineer texture buffer pointer
    U32(0x80138EF4) = U32(p2 + 0x1894);  // Save Engineer vertex buffer pointer
}

// 6. Enemies target nearer living player and stay visible in either player's room.
RECOMP_HOOK("func_800337A8") void coop_enemy_begin(unsigned int obj) {
    swapped_players = 0;
    cur_enemy = 0;
    if (!coop_active() || recomp_get_config_u32("enemies") != 0) {
        return;
    }
    unsigned int e = U32(obj + 0x2C);
    cur_enemy = e;
    unsigned int p0 = U32(PLAYER_PTRS);
    unsigned int p1 = U32(PLAYER_PTRS + 4);
    if (e == 0 || p0 == 0 || p1 == 0) {
        return;
    }
    int hp0 = S8(p0 + 0x176B);
    int hp1 = S8(p1 + 0x176B);
    if (hp0 <= 0) {
        if (hp1 <= 0) {
            return;
        }
    } else {
        if (hp1 <= 0) {
            return;
        }
        float dx1 = F32(e + 0) - F32(p1 + 0);
        float dy1 = F32(e + 4) - F32(p1 + 4);
        float dz1 = F32(e + 8) - F32(p1 + 8);
        float d1 = dx1 * dx1 + dy1 * dy1 + dz1 * dz1;

        float dx0 = F32(e + 0) - F32(p0 + 0);
        float dy0 = F32(e + 4) - F32(p0 + 4);
        float dz0 = F32(e + 8) - F32(p0 + 8);
        float d0 = dx0 * dx0 + dy0 * dy0 + dz0 * dz0;

        if (!(d1 < d0 * 0.64f)) {
            return;
        }
    }
    U32(PLAYER_PTRS) = p1;
    U32(PLAYER_PTRS + 4) = p0;
    unsigned int c0 = U32(0x80179280);
    unsigned int c1 = U32(0x80179284);
    U32(0x80179280) = c1;
    U32(0x80179284) = c0;
    unsigned int r0 = U32(0x8016A410);
    unsigned int r1 = U32(0x8016A414);
    U32(0x8016A410) = r1;
    U32(0x8016A414) = r0;
    swapped_players = 1;
}

RECOMP_HOOK_RETURN("func_800337A8") void coop_enemy_end(void) {
    if (swapped_players) {
        unsigned int p0 = U32(PLAYER_PTRS);
        unsigned int p1 = U32(PLAYER_PTRS + 4);
        U32(PLAYER_PTRS) = p1;
        U32(PLAYER_PTRS + 4) = p0;
        unsigned int c0 = U32(0x80179280);
        unsigned int c1 = U32(0x80179284);
        U32(0x80179280) = c1;
        U32(0x80179284) = c0;
        unsigned int r0 = U32(0x8016A410);
        unsigned int r1 = U32(0x8016A414);
        U32(0x8016A410) = r1;
        U32(0x8016A414) = r0;
        swapped_players = 0;
    }
    unsigned int e = cur_enemy;
    if (e != 0) {
        unsigned short eroom = U16(e + 0x8E);
        unsigned int p0 = U32(PLAYER_PTRS);
        unsigned int p1 = U32(PLAYER_PTRS + 4);
        int vis = 0;
        if (p0 != 0 && (U16(p0 + 0x8E) == eroom || U16(0x8016A410) == eroom || U16(0x8016A412) == eroom)) {
            vis = 1;
        } else if (p1 != 0 && (U16(p1 + 0x8E) == eroom || U16(0x8016A414) == eroom || U16(0x8016A416) == eroom)) {
            vis = 1;
        }
        if (vis) {
            U32(e + 0xF4) &= ~0x100u;
        }
        cur_enemy = 0;
    }
}

// 7. Friendly fire: player-fired bullets use Versus collision rules.
RECOMP_HOOK("func_800B876C") void coop_bullet_begin(unsigned int obj) {
    bullet_split = 0;
    if (!coop_active() || recomp_get_config_u32("friendly_fire") != 0) {
        return;
    }
    unsigned int owner_obj = U32(obj + 0x20);
    unsigned int owner = U32(owner_obj + 0x2C);
    if (owner != 0 && (owner == U32(PLAYER_PTRS) || owner == U32(PLAYER_PTRS + 4))) {
        U8(MODE) = 2;
        bullet_split = 1;
    }
}

RECOMP_HOOK_RETURN("func_800B876C") void coop_bullet_end(void) {
    if (bullet_split) {
        U8(MODE) = 1;
        bullet_split = 0;
    }
}

// 7b. Apply Story Mode direct damage during friendly-fire bullet hits (skip Versus HUD score gate).
RECOMP_HOOK("func_80057BB4") void coop_damage_begin(int dmg, unsigned int target, int attacker_idx) {
    (void)dmg;
    (void)target;
    (void)attacker_idx;
    damage_story_mode = 0;
    if (bullet_split && U8(MODE) == 2) {
        U8(MODE) = 1;
        damage_story_mode = 1;
    }
}

RECOMP_HOOK_RETURN("func_80057BB4") void coop_damage_end(void) {
    if (damage_story_mode) {
        U8(MODE) = 2;
        damage_story_mode = 0;
    }
}

// 7c. Prevent auto-aim and manual lock-on from targeting the other player.
static void clear_player_targets(void) {
    if (!coop_active()) {
        return;
    }
    unsigned int p0 = U32(PLAYER_PTRS);
    unsigned int p1 = U32(PLAYER_PTRS + 4);
    if (p0 == 0 || p1 == 0) {
        return;
    }
    if (U32(0x80138EF8) == p1) {
        U32(0x80138EF8) = 0;
        if (recomp_get_config_u32("log") != 0) winback_mod_log("coop: cleared 8EF8[0]=p1");
    }
    if (U32(0x80138EF8 + 4) == p0) {
        U32(0x80138EF8 + 4) = 0;
        if (recomp_get_config_u32("log") != 0) winback_mod_log("coop: cleared 8EF8[1]=p0");
    }
    if (U32(0x80188C38) == p1) {
        U32(0x80188C38) = 0;
        if (recomp_get_config_u32("log") != 0) winback_mod_log("coop: cleared 8C38[0]=p1");
    }
    if (U32(0x80188C38 + 4) == p0) {
        U32(0x80188C38 + 4) = 0;
        if (recomp_get_config_u32("log") != 0) winback_mod_log("coop: cleared 8C38[1]=p0");
    }
    if (U32(0x801C3690) == p1) {
        U32(0x801C3690) = 0;
    }
    if (U32(0x801C3690 + 4) == p0) {
        U32(0x801C3690 + 4) = 0;
    }
    unsigned int cam0 = U32(CAM_PTRS);
    unsigned int cam1 = U32(CAM_PTRS + 4);
    if (cam0 != 0 && U32(cam0 + 0x164) == p1) {
        U32(cam0 + 0x164) = 0;
    }
    if (cam1 != 0 && U32(cam1 + 0x164) == p0) {
        U32(cam1 + 0x164) = 0;
    }
}

static void cam_target_hide_other(unsigned int cam_obj) {
    cam_other_player = 0;
    if (!coop_active() || cam_obj == 0) {
        return;
    }
    unsigned int owner = U32(cam_obj + 0x20);
    unsigned int p0 = U32(PLAYER_PTRS);
    unsigned int p1 = U32(PLAYER_PTRS + 4);
    unsigned int other = 0;
    if (owner != 0 && p0 != 0 && p1 != 0) {
        if (owner == U32(p0 + 0x1740)) {
            other = p1;
        } else if (owner == U32(p1 + 0x1740)) {
            other = p0;
        }
    }
    if (other != 0) {
        cam_other_player = other;
        cam_saved_fc = U32(other + 0xFC);
        U32(other + 0xFC) = 0;
    }
}

static void cam_target_restore_other(void) {
    if (cam_other_player != 0) {
        U32(cam_other_player + 0xFC) = cam_saved_fc;
        cam_other_player = 0;
    }
    clear_player_targets();
}

RECOMP_HOOK("func_8003A054") void coop_cam_lock_begin(unsigned int cam_obj) {
    cam_target_hide_other(cam_obj);
}

RECOMP_HOOK_RETURN("func_8003A054") void coop_cam_lock_end(void) {
    cam_target_restore_other();
}

RECOMP_HOOK("func_8003A6EC") void coop_cam_cycle_begin(unsigned int cam_obj) {
    cam_target_hide_other(cam_obj);
}

RECOMP_HOOK_RETURN("func_8003A6EC") void coop_cam_cycle_end(void) {
    cam_target_restore_other();
}

RECOMP_HOOK("func_80058438") void coop_lockon_begin(unsigned int player) {
    lock_other_player = 0;
    if (!coop_active()) {
        return;
    }
    unsigned int p0 = U32(PLAYER_PTRS);
    unsigned int p1 = U32(PLAYER_PTRS + 4);
    unsigned int other = (player == p0) ? p1 : (player == p1 ? p0 : 0);
    if (other != 0) {
        lock_other_player = other;
        lock_saved_in_use = U16(other + 0x92);
        U16(other + 0x92) = 0;
    }
}

RECOMP_HOOK_RETURN("func_80058438") void coop_lockon_end(void) {
    if (lock_other_player != 0) {
        U16(lock_other_player + 0x92) = lock_saved_in_use;
        lock_other_player = 0;
    }
    clear_player_targets();
}

// 8. Wall cover, box/crate/door collision for P2, and non-lethal friendly kick.
RECOMP_HOOK("func_800ABC04") void coop_wall_cover_begin(unsigned int player) {
    (void)player;
    cover_split = 0;
    if (coop_active()) {
        U8(MODE) = 2;
        cover_split = 1;
    }
}

RECOMP_HOOK_RETURN("func_800ABC04") void coop_wall_cover_end(void) {
    if (cover_split) {
        U8(MODE) = 1;
        cover_split = 0;
    }
}

RECOMP_HOOK("func_8003C58C") void coop_room_begin(void) {
    room_split = 0;
    room_swapped = 0;
    if (coop_story_session() && U8(MODE) == 1) {
        U8(MODE) = 2;
        room_split = 1;
    }
    if (coop_story_session() && U32(PLAYER_PTRS) == SLOT1_DATA && U32(PLAYER_PTRS + 4) == SLOT0_DATA) {
        unsigned int r0 = U32(ROOM_STATE);
        unsigned int r1 = U32(ROOM_STATE + 4);
        U32(ROOM_STATE) = r1;
        U32(ROOM_STATE + 4) = r0;
        room_swapped = 1;
    }
}

RECOMP_HOOK_RETURN("func_8003C58C") void coop_room_end(void) {
    if (room_swapped) {
        unsigned int r0 = U32(ROOM_STATE);
        unsigned int r1 = U32(ROOM_STATE + 4);
        U32(ROOM_STATE) = r1;
        U32(ROOM_STATE + 4) = r0;
        room_swapped = 0;
    }
    if (room_split) {
        U8(MODE) = 1;
        room_split = 0;
    }
}

RECOMP_HOOK("func_8003FCE4") void coop_box_col_begin(unsigned int obj) {
    box_room_swapped = 0;
    if (!coop_active() || obj == 0) {
        return;
    }
    unsigned int p = U32(obj + 0x2C);
    if (p == U32(PLAYER_PTRS + 4) && p != U32(PLAYER_PTRS)) {
        unsigned int r0 = U32(ROOM_STATE);
        unsigned int r1 = U32(ROOM_STATE + 4);
        U32(ROOM_STATE) = r1;
        U32(ROOM_STATE + 4) = r0;
        box_room_swapped = 1;
    }
}

RECOMP_HOOK_RETURN("func_8003FCE4") void coop_box_col_end(void) {
    if (box_room_swapped) {
        unsigned int r0 = U32(ROOM_STATE);
        unsigned int r1 = U32(ROOM_STATE + 4);
        U32(ROOM_STATE) = r1;
        U32(ROOM_STATE + 4) = r0;
        box_room_swapped = 0;
    }
}

// Prevent Y-button / melee lock-on from targeting the other player.
RECOMP_HOOK("func_800AB594") void coop_kick_begin(unsigned int player) {
    kick_other_player = 0;
    if (!coop_active()) {
        return;
    }
    unsigned int p0 = U32(PLAYER_PTRS);
    unsigned int p1 = U32(PLAYER_PTRS + 4);
    unsigned int other = (player == p0) ? p1 : (player == p1 ? p0 : 0);
    if (other != 0) {
        kick_other_player = other;
        kick_saved_in_use = U16(other + 0x92);
        U16(other + 0x92) = 0;
    }
}

RECOMP_HOOK_RETURN("func_800AB594") void coop_kick_end(void) {
    if (kick_other_player != 0) {
        U16(kick_other_player + 0x92) = kick_saved_in_use;
        kick_other_player = 0;
    }
    clear_player_targets();
}

// 9. Item pickup for Player 2 and item popup dismissal.
static int should_p2_pick_item(unsigned int item_data) {
    unsigned int p1 = U32(PLAYER_PTRS);
    unsigned int p2 = U32(PLAYER_PTRS + 4);
    if (item_data == 0 || p1 == 0 || p2 == 0) {
        return 0;
    }
    if (S8(p2 + 0x176B) <= 0) {
        return 0;
    }
    if (S16(item_data + 0x132) != 0) {
        return U16(p2 + 0x16EC) == 17;
    }
    unsigned short iroom = U16(item_data + 0x8E);
    int p2_in_room = (U16(p2 + 0x8E) == iroom || U16(ROOM_STATE + 4) == iroom || U16(ROOM_STATE + 6) == iroom);
    if (!p2_in_room) {
        return 0;
    }
    if (S8(p1 + 0x176B) <= 0) {
        return 1;
    }
    int p1_in_room = (U16(p1 + 0x8E) == iroom || U16(ROOM_STATE + 0) == iroom || U16(ROOM_STATE + 2) == iroom);
    if (!p1_in_room) {
        return 1;
    }
    float dx1 = F32(item_data + 0) - F32(p1 + 0);
    float dy1 = F32(item_data + 4) - F32(p1 + 4);
    float dz1 = F32(item_data + 8) - F32(p1 + 8);
    float d1 = dx1 * dx1 + dy1 * dy1 + dz1 * dz1;

    float dx2 = F32(item_data + 0) - F32(p2 + 0);
    float dy2 = F32(item_data + 4) - F32(p2 + 4);
    float dz2 = F32(item_data + 8) - F32(p2 + 8);
    float d2 = dx2 * dx2 + dy2 * dy2 + dz2 * dz2;
    return d2 < d1;
}

static void item_pick_begin(unsigned int obj) {
    if (coop_active() && obj != 0 && should_p2_pick_item(U32(obj + 0x2C))) {
        if (item_swap_depth == 0) {
            swap_player_ptrs_and_rooms();
        }
        item_swap_depth++;
    }
}

static void item_pick_end(void) {
    if (item_swap_depth > 0) {
        item_swap_depth--;
        if (item_swap_depth == 0) {
            swap_player_ptrs_and_rooms();
        }
    }
}

RECOMP_HOOK("func_800873A4") void coop_stage_item_begin(unsigned int obj, int item_id, int subtype) {
    (void)item_id;
    (void)subtype;
    item_pick_begin(obj);
}

RECOMP_HOOK_RETURN("func_800873A4") void coop_stage_item_end(void) {
    item_pick_end();
}

RECOMP_HOOK("func_800C958C") void coop_drop_item_begin(unsigned int obj) {
    item_pick_begin(obj);
}

RECOMP_HOOK_RETURN("func_800C958C") void coop_drop_item_end(void) {
    item_pick_end();
}

RECOMP_HOOK("func_800A8620") void coop_enemy_item_begin(unsigned int obj) {
    item_pick_begin(obj);
}

RECOMP_HOOK_RETURN("func_800A8620") void coop_enemy_item_end(void) {
    item_pick_end();
}

// 10. Cutscene trigger tracking, protagonist selection, and Engineer preservation.
static unsigned int a8b10_player;

RECOMP_HOOK("func_800A8B10") void coop_a8b10_begin(unsigned int player) {
    a8b10_player = player;
}

RECOMP_HOOK_RETURN("func_800A8B10") void coop_a8b10_end(void) {
    if (coop_story_session() && a8b10_player == SLOT1_DATA) {
        restore_engineer_model(SLOT1_DATA);
    }
}

RECOMP_HOOK("func_8004DE48") void coop_event_push(int type, int sub, int val, unsigned int obj) {
    (void)sub;
    (void)val;
    if (!coop_active() || type != 1 || obj == 0) {
        return;
    }
    unsigned int p = U32(obj + 0x2C);
    if (p == SLOT1_DATA) {
        trigger_player = 1;
        trigger_ttl = 180;
    } else if (p == SLOT0_DATA) {
        trigger_player = 0;
        trigger_ttl = 0;
    }
}

RECOMP_HOOK("func_800944E0") void coop_event_mgr_begin(void) {
    evt_mgr_saved = 0;
    if (!coop_story_session()) {
        return;
    }
    if (U8(DEATH_FLAG) != 0) {
        int dead_p = (S8(SLOT1_DATA + 0x176B) <= 0 && S8(SLOT0_DATA + 0x176B) > 0) ? 1 : 0;
        cutscene_protagonist = dead_p;
        trigger_player = dead_p;
        trigger_ttl = 180;
        set_protagonist(dead_p);
        if (dead_p == 1 && U32(PLAYER_PTRS) == SLOT0_DATA) {
            swap_all_player_state();
        } else if (dead_p == 0 && U32(PLAYER_PTRS) == SLOT1_DATA) {
            ensure_normal_player_order();
        }
        in_cutscene = 1;
    }
    unsigned int p1 = U32(PLAYER_PTRS);
    unsigned int p2 = U32(PLAYER_PTRS + 4);
    if (p1 != 0 && p2 != 0 && U16(p1 + 0x16EC) < 2 && U16(p2 + 0x16EC) < 2) {
        evt_saved_new_p1 = U16(BTN_NEW_P1);
        evt_saved_mask_p1 = U16(BTN_MASK_P1);
        U16(BTN_NEW_P1) |= U16(BTN_NEW_P2);
        U16(BTN_MASK_P1) |= U16(BTN_MASK_P2);
        evt_mgr_saved = 1;
    }
}

RECOMP_HOOK_RETURN("func_800944E0") void coop_event_mgr_end(void) {
    if (evt_mgr_saved) {
        U16(BTN_NEW_P1) = evt_saved_new_p1;
        U16(BTN_MASK_P1) = evt_saved_mask_p1;
        evt_mgr_saved = 0;
    }
    if (coop_story_session() && !in_cutscene && trigger_ttl > 0) {
        trigger_ttl--;
        if (trigger_ttl == 0) {
            trigger_player = 0;
        }
    }
}

RECOMP_HOOK("func_8009C2E4") void coop_event_dispatch_begin(int type, int val) {
    (void)type;
    (void)val;
    c2e4_swapped = 0;
    if (!coop_story_session() || in_cutscene) {
        return;
    }
    set_protagonist(trigger_player);
    if (trigger_player == 1 && U32(PLAYER_PTRS) == SLOT0_DATA) {
        swap_all_player_state();
        c2e4_swapped = 1;
    }
}

RECOMP_HOOK_RETURN("func_8009C2E4") void coop_event_dispatch_end(void) {
    if (c2e4_swapped) {
        if (!in_cutscene) {
            ensure_normal_player_order();
        }
        c2e4_swapped = 0;
    }
}

RECOMP_HOOK("func_8009BE18") void coop_script_run_begin(void) {
    be18_swapped = 0;
    if (!coop_story_session()) {
        return;
    }
    int active_protag = in_cutscene ? cutscene_protagonist : trigger_player;
    if (active_protag == 1 && U32(PLAYER_PTRS) == SLOT0_DATA) {
        swap_all_player_state();
        be18_swapped = 1;
    }
}

RECOMP_HOOK_RETURN("func_8009BE18") void coop_script_run_end(void) {
    if (be18_swapped && !in_cutscene) {
        ensure_normal_player_order();
    }
    be18_swapped = 0;
}

RECOMP_HOOK("func_8009E1E0") void coop_cutscene_mode_begin(int mode) {
    e1e0_mode = mode;
    if (!coop_story_session()) {
        return;
    }
    if (mode == 1 || mode == 3) {
        unsigned short act0 = U16(SLOT0_DATA + 0x16EC);
        unsigned short act1 = U16(SLOT1_DATA + 0x16EC);
        if (act1 == 23 || act1 == 25 || act1 == 12 || act1 == 13) {
            trigger_player = 1;
        } else if (act0 == 23 || act0 == 25 || act0 == 12 || act0 == 13) {
            trigger_player = 0;
        }
        if (S8(SLOT1_DATA + 0x176B) <= 0 && S8(SLOT0_DATA + 0x176B) > 0) {
            trigger_player = 1;
        }
        cutscene_protagonist = trigger_player;
        set_protagonist(cutscene_protagonist);
        unsigned int other = (cutscene_protagonist == 1) ? SLOT0_DATA : SLOT1_DATA;
        U32(other + 0x0154) = 0;
        U32(other + 0x0158) = 0;
        U16(other + 0x16EC) = 0;
        U8(other + 0x16F0) = 1;
        if (cutscene_protagonist == 1 && U32(PLAYER_PTRS) == SLOT0_DATA) {
            swap_all_player_state();
        }
        in_cutscene = 1;
    }
}

RECOMP_HOOK_RETURN("func_8009E1E0") void coop_cutscene_mode_end(void) {
    if (!coop_story_session()) {
        return;
    }
    if (e1e0_mode == 0 || e1e0_mode == 2) {
        ensure_normal_player_order();
        U16(SLOT1_DATA + 0x16EC) = 0;
        U8(SLOT1_DATA + 0x16F0) = 1;
        U16(SLOT1_DATA + 0x0088) = 0x1000;
        U32(SLOT1_DATA + 0x00F4) = (U32(SLOT1_DATA + 0x00F4) & ~0x20000200u) | 0x00000002u;
        set_protagonist(0);
        cutscene_protagonist = 0;
        trigger_player = 0;
        trigger_ttl = 0;
        in_cutscene = 0;
    }
}

static unsigned char tele_saved_p0;
static float tele_saved_pos[3];
static unsigned short tele_saved_id;
static float tele_saved_head;
static float tele_saved_cam[3];

RECOMP_HOOK("func_80098F20") void coop_cutscene_teleport_begin(int pos_id, int player_idx) {
    (void)pos_id;
    tele_idx = player_idx;
    tele_saved_p0 = 0;
    if (coop_story_session() && player_idx == 0 && U32(PLAYER_PTRS) == SLOT1_DATA) {
        tele_saved_pos[0] = F32(START_POS + 0);
        tele_saved_pos[1] = F32(START_POS + 4);
        tele_saved_pos[2] = F32(START_POS + 8);
        tele_saved_id = U16(START_ID);
        tele_saved_head = F32(START_HEAD);
        tele_saved_cam[0] = F32(START_CAM + 0);
        tele_saved_cam[1] = F32(START_CAM + 4);
        tele_saved_cam[2] = F32(START_CAM + 8);
        tele_saved_p0 = 1;
    }
}

RECOMP_HOOK_RETURN("func_80098F20") void coop_cutscene_teleport_end(void) {
    if (!coop_story_session()) {
        return;
    }
    if (tele_saved_p0) {
        // Move P2's newly written start/camera data into slot 1 and restore P1's slot 0 data.
        F32(START_POS + 12 + 0) = F32(START_POS + 0);
        F32(START_POS + 12 + 4) = F32(START_POS + 4);
        F32(START_POS + 12 + 8) = F32(START_POS + 8);
        U16(START_ID + 2) = U16(START_ID);
        F32(START_HEAD + 4) = F32(START_HEAD);
        F32(START_CAM + 12 + 0) = F32(START_CAM + 0);
        F32(START_CAM + 12 + 4) = F32(START_CAM + 4);
        F32(START_CAM + 12 + 8) = F32(START_CAM + 8);

        F32(START_POS + 0) = tele_saved_pos[0];
        F32(START_POS + 4) = tele_saved_pos[1];
        F32(START_POS + 8) = tele_saved_pos[2];
        U16(START_ID) = tele_saved_id;
        F32(START_HEAD) = tele_saved_head;
        F32(START_CAM + 0) = tele_saved_cam[0];
        F32(START_CAM + 4) = tele_saved_cam[1];
        F32(START_CAM + 8) = tele_saved_cam[2];
        tele_saved_p0 = 0;
    }
}

// 11. Custom HUD with character portraits (Jean-Luc for P1, Engineer for P2) and slanted LIFE bar.
#include "hud_tex_data.h"

#define HUD_SPRITE_P1 0x80500000u
#define HUD_SPRITE_P2 0x80500040u
#define HUD_TEX_P1    0x80500100u
#define HUD_TEX_P2    0x80506000u
#define HUD_TEX_W     224
#define HUD_TEX_H     48

#define RGBA5551(r, g, b) \
    ((unsigned short)(((((r) >> 3) & 0x1F) << 11) | ((((g) >> 3) & 0x1F) << 6) | ((((b) >> 3) & 0x1F) << 1) | 1))

extern void func_8007B8F4(unsigned int dl_ptr_ptr, unsigned int sprite_ptr, int x, int y);

static int hud_init_done[2] = { 0, 0 };
static int hud_last_bar[2] = { -1, -1 };
static int hud_last_trail[2] = { -1, -1 };
static int hud_last_hp[2] = { -1, -1 };

static const int hud_bar_x0[8] = { 91, 90, 89, 88, 88, 87, 87, 87 };
static const int hud_bar_x1[8] = { 209, 208, 207, 206, 205, 204, 203, 202 };

static void hud_ensure_loaded(int char_idx, unsigned int spr_addr, unsigned int tex_addr, const unsigned short *base_tex) {
    if (!hud_init_done[char_idx] || U16(spr_addr + 0x02) != HUD_TEX_W) {
        for (int i = 0; i < HUD_TEX_W * HUD_TEX_H; i++) {
            U16(tex_addr + i * 2) = base_tex[i];
        }
        U8(spr_addr + 0x00) = 0;          // G_IM_FMT_RGBA
        U8(spr_addr + 0x01) = 2;          // G_IM_SIZ_16b
        U16(spr_addr + 0x02) = HUD_TEX_W; // width = 224
        U16(spr_addr + 0x04) = HUD_TEX_H; // height = 48
        U16(spr_addr + 0x06) = 0;
        U32(spr_addr + 0x08) = tex_addr;
        U32(spr_addr + 0x0C) = 0;         // no TLUT
        U16(spr_addr + 0x10) = 0x0001;    // visible + auto RDP setup
        U16(spr_addr + 0x12) = 0;
        U16(spr_addr + 0x14) = 0;
        U16(spr_addr + 0x16) = 0;
        U32(spr_addr + 0x18) = 0x3F000000u; // scale_x = 0.5f (112px on 320x240 screen)
        U32(spr_addr + 0x1C) = 0x3F000000u; // scale_y = 0.5f (24px on 320x240 screen)
        U8(spr_addr + 0x20) = 255;
        U8(spr_addr + 0x21) = 255;
        U8(spr_addr + 0x22) = 255;
        U8(spr_addr + 0x23) = 255;
        U32(spr_addr + 0x24) = 0;
        hud_init_done[char_idx] = 1;
        hud_last_bar[char_idx] = -1;
        hud_last_trail[char_idx] = -1;
        hud_last_hp[char_idx] = -1;
    }
}

static void hud_update_health_bar(int char_idx, unsigned int tex_addr, const unsigned short *base_tex, int hp, int bar_hp10, int trail_hp10) {
    if (hud_last_bar[char_idx] == bar_hp10 &&
        hud_last_trail[char_idx] == trail_hp10 &&
        hud_last_hp[char_idx] == hp) {
        return;
    }
    hud_last_bar[char_idx] = bar_hp10;
    hud_last_trail[char_idx] = trail_hp10;
    hud_last_hp[char_idx] = hp;

    unsigned short c_top, c_upper, c_mid, c_bot;
    if (hp > 50) {
        c_top   = RGBA5551(80, 235, 95);
        c_upper = RGBA5551(45, 205, 65);
        c_mid   = RGBA5551(34, 182, 52);
        c_bot   = RGBA5551(22, 135, 36);
    } else if (hp > 25) {
        c_top   = RGBA5551(250, 230, 80);
        c_upper = RGBA5551(235, 195, 40);
        c_mid   = RGBA5551(215, 170, 25);
        c_bot   = RGBA5551(160, 120, 15);
    } else {
        c_top   = RGBA5551(250, 95, 85);
        c_upper = RGBA5551(230, 50, 45);
        c_mid   = RGBA5551(205, 35, 30);
        c_bot   = RGBA5551(150, 20, 20);
    }
    unsigned short c_trail = RGBA5551(150, 28, 28);

    for (int row = 0; row < 8; row++) {
        int ty = 20 + row;
        int x0 = hud_bar_x0[row];
        int x1 = hud_bar_x1[row];
        int span = x1 - x0 + 1;
        int x_fill = x0 + (span * bar_hp10) / 1000;
        int x_trail = x0 + (span * trail_hp10) / 1000;
        unsigned short c_row = (row == 0) ? c_top : (row <= 2) ? c_upper : (row <= 5) ? c_mid : c_bot;

        for (int tx = x0; tx <= x1; tx++) {
            int idx = ty * HUD_TEX_W + tx;
            unsigned short px;
            if (tx < x_fill) {
                px = c_row;
            } else if (tx < x_trail) {
                px = c_trail;
            } else {
                px = base_tex[idx];
            }
            U16(tex_addr + idx * 2) = px;
        }
    }
}

RECOMP_PATCH void func_800CF6A8(int player_idx, int x, int y) {
    if (player_idx < 0 || player_idx > 3) {
        return;
    }
    unsigned int player = U32(PLAYER_PTRS + player_idx * 4);
    if (player == 0) {
        return;
    }

    int char_idx = (player == SLOT1_DATA || player_idx == 1) ? 1 : 0;
    unsigned int spr_addr = (char_idx == 1) ? HUD_SPRITE_P2 : HUD_SPRITE_P1;
    unsigned int tex_addr = (char_idx == 1) ? HUD_TEX_P2 : HUD_TEX_P1;
    const unsigned short *base_tex = (char_idx == 1) ? hud_p2_base : hud_p1_base;

    int hp = S8(player + 0x176B);
    if (hp < 0) hp = 0;
    if (hp > 100) hp = 100;

    int del10 = S16(0x801B1168 + player_idx * 2);
    if (del10 < 0) del10 = 0;
    if (del10 > 1000) del10 = 1000;

    int hp10 = hp * 10;
    int bar_hp10 = (del10 < hp10) ? del10 : hp10;
    int trail_hp10 = (del10 > hp10) ? del10 : hp10;

    hud_ensure_loaded(char_idx, spr_addr, tex_addr, base_tex);
    hud_update_health_bar(char_idx, tex_addr, base_tex, hp, bar_hp10, trail_hp10);

    // Clear bit 0x2000 so func_8007A754 always emits the RDP setup header for our RGBA16 sprite
    U16(spr_addr + 0x10) = 0x0001;

    int hud_x = x - 28;
    int hud_y = y - 15;
    func_8007B8F4(0x801D2C80u, spr_addr, hud_x, hud_y);
}

