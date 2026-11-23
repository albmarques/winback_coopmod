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
extern void func_800C4EE0(unsigned int obj);
extern void func_80028740(unsigned int sky_data);  // build the sky dome matrix

static int spawn_calls;
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
static void lad_event(const char *tag, int a, int b);   // ladder diagnosis (log)
static void lad_log(const char *tag, const unsigned int *v, int n);
static int hit_room_depth;
static int hit_diag_lines;
static int jf_valid;                       // Passo 56 jump follow
static float jf_last[3];
static int settle_frames;
static unsigned int spawn_prev_map = 0xFFFFu;   // map of the last load (Passo 54)
static unsigned int spawn_load_flag;             // 0x8016A418 when the load started
static int spawn_restored;                 // a continue / checkpoint restore came before this load
static int itemdiag_ready;              // Passo 52 diagnosis
static unsigned int mg_obj[8];         // Passo 51b machine guns
// Passo 47 diagnosis: after a P2 shot, the enemies near P2 and the rooms of everyone.
static int shot_diag_lines = 99;
static unsigned int shot_diag_tick;
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
static int radar_map_reset = 1;   // set when a stage loads: the explored-area map starts over

// Co-op difficulty: 0 Normal, 1 Hard, 2 Brutal.
// enemy_taken: % of the damage enemies take (lower = tougher); enemy_dealt: % of the damage
// players take; pickup: % of the ammo a pickup gives.
static const int diff_enemy_taken[3] = { 100, 70, 50 };
static const int diff_enemy_dealt[3] = { 100, 130, 160 };
static const int diff_pickup[3]      = { 100, 70, 50 };
static int coop_difficulty(void) {
    unsigned int d = recomp_get_config_u32("difficulty");
    return d > 2 ? 0 : (int)d;
}
static int ff_applying;
static float aim_yaw[2];
static int aim_ok[2];
static int bullet_inv_entered;               // our own friendly-fire hit: no difficulty scaling


// Separate inventories. The Story inventory is one global block (0x80158760 story state):
// reserves, weapons owned, rockets and C4. The globals always hold the inventory of the
// player being processed; P2's own copy lives in inv_p2 and is swapped in while the game runs
// code for P2 (its player update, its weapon, its pickups), then swapped back.
static const unsigned int inv_addr[] = {
    0x801587C0u, // submachine gun reserve
    0x801587C1u, // shotgun reserve
    0x801587C4u, // C4 count
    0x801587C6u, // submachine gun owned
    0x801587C7u, // shotgun owned
    0x801587CBu, // rockets
    0x801587CCu, // handgun magazine upgrade (14 rounds)
};
#define INV_N ((int)(sizeof(inv_addr) / sizeof(inv_addr[0])))
static unsigned char inv_p2[INV_N];
static int inv_p2_valid;
static int inv_active;          // whose inventory the globals hold: 0 = P1, 1 = P2
static int inv_depth;

static unsigned char drawing_split;
static int inv_separate(void) {
    // MODE is 2 while the split screen is drawn (HUD), so accept drawing_split as Story too.
    return U8(PLAYERS) == 2 && ((U8(MODE) & 1) || drawing_split) && recomp_get_config_u32("inventory") == 0;
}

static void inv_exchange(void) {
    for (int i = 0; i < INV_N; i++) {
        unsigned char g = U8(inv_addr[i]);
        U8(inv_addr[i]) = inv_p2[i];
        inv_p2[i] = g;
    }
    inv_active ^= 1;
}

// Make the globals hold player k's inventory for a nested section; pair with inv_leave().
static int inv_enter(int k) {
    if (!inv_separate() || !inv_p2_valid) return 0;
    if (inv_depth == 0 && inv_active != k) {
        inv_exchange();
        inv_depth = 1;
        return 1;
    }
    if (inv_depth > 0) inv_depth++;
    return inv_depth > 0;
}

static void inv_leave(int entered) {
    if (!entered || inv_depth <= 0) return;
    inv_depth--;
    if (inv_depth == 0 && inv_active != 0) inv_exchange();
}

// Read / write one inventory byte of player k wherever it currently lives.
static int inv_index(unsigned int addr) {
    for (int i = 0; i < INV_N; i++) if (inv_addr[i] == addr) return i;
    return -1;
}
static int inv_get(int k, unsigned int addr) {
    int i = inv_index(addr);
    if (i < 0 || !inv_separate() || !inv_p2_valid || k == inv_active) return U8(addr);
    return inv_p2[i];
}
static void inv_set(int k, unsigned int addr, int v) {
    int i = inv_index(addr);
    if (i < 0 || !inv_separate() || !inv_p2_valid || k == inv_active) { U8(addr) = (unsigned char)v; return; }
    inv_p2[i] = (unsigned char)v;
}

// P2 starts each Story session with a copy of P1's inventory.
static void inv_init_p2(void) {
    if (inv_active != 0) inv_exchange();
    for (int i = 0; i < INV_N; i++) inv_p2[i] = U8(inv_addr[i]);
    inv_p2_valid = 1;
    inv_depth = 0;
}

static int inv_slot_of(unsigned int player) {
    return (player == SLOT1_DATA) ? 1 : 0;
}

// Live per-player menu (Start in co-op): each player opens a weapon menu in their own view
// while the game keeps running. Input is read here and hidden from that player's update.
static int lm_open[2];
static int lm_cursor[2];          // weapon list cursor
static int lm_tab[2];             // main page tab: MAP, WEAPON, ITEM, EXIT
static unsigned int lm_tick;      // +1 per drawn frame
static int lm_screen[2];          // 0 main page, 1 WEAPON, 2 ITEM, 3 TEAM
static int lm_tsel[2];            // TEAM page selection
static int lm_stick_prev[2];
static int lm_inject;            // 1 = press Start for the original pause menu next frame
static unsigned short lm_saved_btn;
static unsigned int lm_saved_raw0, lm_saved_raw1;
static int lm_suppressed = -1;
#define RAW_PADS 0x80134718u     // 8 bytes per controller: buttons, stick x, stick y

static int live_menu_on(void) {
    return recomp_get_config_u32("live_menu") == 0;
}

static int lm_slot_owned(int k, int slot) {
    switch (slot) {
        case 0: return 1;
        case 1: return inv_get(k, 0x801587C6) != 0;
        case 2: return inv_get(k, 0x801587C7) != 0;
        case 3: return inv_get(k, 0x801587CB) != 0;
        case 4: return inv_get(k, 0x801587C4) != 0;
    }
    return 0;
}

static int lm_slot_held(unsigned int p) {
    int id = U8(p + 0x1758);
    if (id >= 0 && id < 4) return id;
    if (id == 0x14) return 4;
    return -1;
}

// Equip a weapon the way the game's weapon switch (func_80062AE8) does it.
static void lm_equip(unsigned int p, int slot) {
    if (U16(p + 0x16EC) != 0) return;            // busy (reloading, acting): ignore
    int old = U8(p + 0x1758);
    int id = (slot == 4) ? 0x14 : slot;
    if (old == id) return;
    if (old == 0x14) U32(p + 0xF8) &= ~0x400u;
    U8(p + 0x1758) = (unsigned char)id;
    if (id == 0x14) U32(p + 0xF8) |= 0x400u;
    if (id == 3 && (U32(p + 0xF4) & 0x100000u)) {
        U32(p + 0x130) = 0;
        U16(p + 0x16EC) = 0;
        U8(p + 0x16F0) = 1;
        U32(p + 0xF4) &= ~0x100000u;
    }
    unsigned int f4 = U32(p + 0xF4);
    unsigned int tbl;
    if (f4 & 0x100000u) tbl = (f4 & 0x800u) ? 0x801054D8u : 0x801054C0u;
    else tbl = (f4 & 0x800u) ? 0x80105450u : 0x80105430u;
    unsigned int cls = U8(0x80105C70u + (unsigned int)id);
    unsigned short anim = U16(tbl + cls * 2);
    U16(p + 0x16E0) = anim;
    U16(p + 0x16E2) = anim;
}

// Ammo sharing from the WEAPON page (R): one magazine's worth of the selected weapon's ammo goes
// to the partner. SMG / shotgun: reserve only (the partner must have the gun). Rockets / C4: the
// count is the weapon, so the partner gets it with the first one; the giver keeps 1 while holding it.
static const unsigned int give_addr[5] = { 0, 0x801587C0u, 0x801587C1u, 0x801587CBu, 0x801587C4u };
static const int give_chunk[5] = { 0, 30, 8, 1, 1 };
static const int give_cap[5] = { 0, 250, 250, 4, 7 };   // rockets / C4: what the infinite-ammo cheat fills
static const int give_pick[5] = { -1, 0, 1, 2, 3 };     // index in pick_addr (coop_pickup_scale)
static int pick_last[2][4];                             // defined with its initialiser further down
static unsigned int lm_give_t[2] = { 0x80000000u, 0x80000000u };   // lm_tick of the last successful R

static int lm_slot_owned(int k, int slot);
static int lm_slot_held(unsigned int p);

// How much R would give now (> 0), or why not: -2 partner full, -3 partner has no gun, -4 nothing.
static int lm_give_amount(int k, unsigned int p, int s) {
    if (s < 1 || s > 4) return -4;
    int o = k ^ 1;
    unsigned int a = give_addr[s];
    if (s <= 2 && !lm_slot_owned(o, s)) return -3;
    int keep = (s >= 3 && lm_slot_held(p) == s) ? 1 : 0;
    int n = inv_get(k, a) - keep;
    if (n <= 0) return -4;
    int room = give_cap[s] - inv_get(o, a);
    if (room <= 0) return -2;
    if (n > give_chunk[s]) n = give_chunk[s];
    if (n > room) n = room;
    return n;
}

static int lm_give(int k, unsigned int p, int s) {
    int n = lm_give_amount(k, p, s);
    if (n <= 0) return 0;
    int o = k ^ 1;
    unsigned int a = give_addr[s];
    int mine = inv_get(k, a), theirs = inv_get(o, a);
    inv_set(k, a, mine - n);
    inv_set(o, a, theirs + n);
    int pi = give_pick[s];
    pick_last[k][pi] = mine - n;          // not a pickup: keep the difficulty scaling out of it
    pick_last[o][pi] = theirs + n;
    if (recomp_get_config_u32("log") != 0) winback_mod_log(k ? "coop: P2 gave ammo to P1" : "coop: P1 gave ammo to P2");
    return 1;
}

static void lm_close_all(void) {
    lm_open[0] = lm_open[1] = 0;
}

// Menu input for player k (called at the start of its update); then hides the pad from it.
static void lm_player_begin(unsigned int p, int k) {
    lm_suppressed = -1;
    unsigned int bn = BTN_NEW_P1 + (unsigned int)k * 2;
    unsigned int raw = RAW_PADS + (unsigned int)k * 8;
    if (!lm_open[k]) return;
    if (!coop_active() || !live_menu_on()) { lm_open[k] = 0; return; }
    unsigned short b = U16(bn);
    int sy = S8(raw + 3);
    int dir = 0;
    if (b & 0x0800) dir = -1;
    else if (b & 0x0400) dir = 1;
    else if (sy > 50 && lm_stick_prev[k] <= 50) dir = -1;
    else if (sy < -50 && lm_stick_prev[k] >= -50) dir = 1;
    lm_stick_prev[k] = sy;
    int scr = lm_screen[k];
    if (dir != 0) {
        if (scr == 1) {                          // WEAPON page: owned weapons only
            int c = lm_cursor[k];
            for (int n = 0; n < 5; n++) {
                c += dir;
                if (c < 0) c = 4;
                if (c > 4) c = 0;
                if (lm_slot_owned(k, c)) break;
            }
            lm_cursor[k] = c;
        } else if (scr == 3) {
            lm_tsel[k] ^= 1;
        } else if (scr == 0) {
            int t = lm_tab[k] + dir;
            if (t < 0) t = 3;
            if (t > 3) t = 0;
            lm_tab[k] = t;
        }
    }
    if ((b & 0x0010) && scr == 1 && inv_separate() && inv_p2_valid) {   // R: give ammo to the partner
        if (lm_give(k, p, lm_cursor[k])) lm_give_t[k] = lm_tick;
        if (!lm_slot_owned(k, lm_cursor[k])) {   // gave the last rocket / C4 away
            int h = lm_slot_held(p);
            lm_cursor[k] = (h >= 0) ? h : 0;
        }
    }
    if (b & 0x8000) {                            // A
        if (scr == 1) {
            if (lm_slot_owned(k, lm_cursor[k])) {
                lm_equip(p, lm_cursor[k]);
                lm_screen[k] = 0;
                lm_open[k] = 0;
            }
        } else if (scr == 0) {
            int t = lm_tab[k];
            if (t == 1) {
                int h = lm_slot_held(p);
                lm_cursor[k] = (h >= 0) ? h : 0;
                lm_screen[k] = 1;
            } else if (t == 2) {
                lm_screen[k] = 2;
            } else if (t == 3) {                 // EXIT: close
                lm_open[k] = 0;
            }
        }
    } else if (b & 0x4000) {                     // B: back / close
        if (scr != 0) lm_screen[k] = 0;
        else lm_open[k] = 0;
    }
    // Hide this controller from the player's update while the menu is (or was just) open.
    lm_saved_btn = U16(bn);
    lm_saved_raw0 = U32(raw);
    lm_saved_raw1 = U32(raw + 4);
    U16(bn) = 0;
    U32(raw) = 0;
    U32(raw + 4) = 0;
    lm_suppressed = k;
}

static void lm_player_end(void) {
    if (lm_suppressed < 0) return;
    int k = lm_suppressed;
    U16(BTN_NEW_P1 + (unsigned int)k * 2) = lm_saved_btn;
    U32(RAW_PADS + (unsigned int)k * 8) = lm_saved_raw0;
    U32(RAW_PADS + (unsigned int)k * 8 + 4) = lm_saved_raw1;
    lm_suppressed = -1;
}


// Live menu button (co-op): D-pad down opens the live menu of the player who pressed it.
static void lm_start_filter(void) {
    if (lm_inject) {
        lm_inject = 0;
        U16(BTN_NEW_P1) |= 0x1000;           // the original pause menu
        return;
    }
    if (!coop_active() || !live_menu_on()) { lm_close_all(); return; }
    // D-pad down opens this player's live menu (B or EXIT closes it). Start is left alone:
    // it opens the game's own pause menu as usual.
    for (int k = 0; k < 2; k++) {
        unsigned int bn = BTN_NEW_P1 + (unsigned int)k * 2;
        if (!lm_open[k] && (U16(bn) & 0x0400)) {
            U16(bn) &= (unsigned short)~0x0400;
            unsigned int p = k ? SLOT1_DATA : SLOT0_DATA;
            int h = lm_slot_held(p);
            lm_open[k] = 1;
            lm_cursor[k] = (h >= 0) ? h : 0;
            lm_tab[k] = 0;
            lm_screen[k] = 0;
        }
    }
}

// Player update (func_80059E5C(obj), player = obj + 0x2C): firing, reloading, weapon change
// and item use all happen inside it.
static int inv_pu_entered;
// Player being updated right now (0 / 1), -1 outside func_80059E5C. Events raised from a player's
// own action code (e.g. the ladder check, event 30) belong to that player.
static int updating_player = -1;
// Passo 60: who raised the ladder / vault event (0x8010E0F8 = 1) during their own update.
static int flag15_owner = -1;
static short flag15_before;
static int lad_stuck[2];

RECOMP_HOOK("func_80059E5C") void coop_inv_player_begin(unsigned int obj) {
    inv_pu_entered = 0;
    lm_suppressed = -1;
    updating_player = -1;
    if (obj >= 0x80000000u && obj < 0x80800000u) {
        unsigned int pl = U32(obj + 0x2C);
        if (pl == SLOT1_DATA || pl == SLOT0_DATA) {
            updating_player = (pl == SLOT1_DATA) ? 1 : 0;
            flag15_before = S16(0x8010E0F8);
            inv_pu_entered = inv_enter(inv_slot_of(pl));
            lm_player_begin(pl, inv_slot_of(pl));
        }
    }
}
RECOMP_HOOK_RETURN("func_80059E5C") void coop_inv_player_end(void) {
    if (updating_player >= 0 && flag15_before != 1 && S16(0x8010E0F8) == 1) {
        flag15_owner = updating_player;
        if (recomp_get_config_u32("log") != 0) {
            unsigned int v[1] = { (unsigned int)updating_player };
            lad_log("flag15_owner", v, 1);
        }
    }
    updating_player = -1;
    lm_player_end();
    inv_leave(inv_pu_entered);
    inv_pu_entered = 0;
}

// Checkpoints. func_8009C8E0 copies the mission flags and the progress block (P1's inventory
// included) to D_8011DAB0 / D_8011E350, at stage start (func_8004BD7C) and at mid-stage
// checkpoints (func_8009AA9C); the continue screen (func_800904E4) puts them back with
// func_8009C998. P2's inventory lives in the mod, so it is copied alongside: after a continue both
// players are back to what they had at the checkpoint.
static unsigned char inv_cp[INV_N];
static int inv_cp_valid;
static int inv_cp_swapped;

RECOMP_HOOK("func_8009C8E0") void coop_checkpoint_save_begin(void) {
    lad_event("checkpoint", 0, 0);
    // If P2's inventory is in the globals right now, put P1's back first so the game's own
    // checkpoint keeps P1's (and P2's copy below is the right one).
    inv_cp_swapped = 0;
    if (inv_active != 0) {
        inv_exchange();
        inv_cp_swapped = 1;
    }
    inv_cp_valid = inv_p2_valid;
    if (inv_p2_valid) for (int i = 0; i < INV_N; i++) inv_cp[i] = inv_p2[i];
}

RECOMP_HOOK_RETURN("func_8009C8E0") void coop_checkpoint_save_end(void) {
    if (inv_cp_swapped) {
        inv_exchange();
        inv_cp_swapped = 0;
    }
    if (recomp_get_config_u32("log") != 0)
        winback_mod_log(inv_cp_valid ? "coop: checkpoint saved (P2 inventory kept)" : "coop: checkpoint saved (no P2 inventory yet)");
}

RECOMP_HOOK_RETURN("func_8009C998") void coop_checkpoint_restore_end(void) {
    spawn_restored = 1;                       // Passo 54: the next load is a continue, not a door
    // The game just wrote P1's checkpoint inventory into the globals: they are P1's again.
    inv_active = 0;
    inv_depth = 0;
    if (inv_cp_valid) {
        for (int i = 0; i < INV_N; i++) inv_p2[i] = inv_cp[i];
        inv_p2_valid = 1;
    } else {
        inv_p2_valid = 0;   // no copy yet: P2 gets P1's again at the stage start, as on a new session
    }
    for (int k = 0; k < 2; k++)
        for (int i = 0; i < 4; i++) pick_last[k][i] = -1;   // not a pickup: the difficulty scaling restarts
    if (recomp_get_config_u32("log") != 0)
        winback_mod_log(inv_cp_valid ? "coop: continue, P2 inventory restored" : "coop: continue, P2 inventory recopied from P1");
}

// New game (func_8004DA40 resets the story state): P2's copy is rebuilt at the first stage.
RECOMP_HOOK_RETURN("func_8004DA40") void coop_inv_new_game(void) {
    spawn_prev_map = 0xFFFFu;                 // Passo 54: a new game starts with a stage start
    inv_p2_valid = 0;
    inv_active = 0;
    inv_depth = 0;
    inv_cp_valid = 0;
}

static int spawn_guard;           // frames after a stage load in which P2 is pushed off P1
static float spawn_side_x = 1.0f, spawn_side_z = 0.0f; // unit vector from P1 to P2 at the start
static float spawn_guard_dist = -1.0f; // distance the overlap guard restores (-1: "spawn_distance")

static float spawn_sqrt(float v) {
    if (v <= 0.0f) return 0.0f;
    float r = v > 1.0f ? v : 1.0f;
    for (int i = 0; i < 20; i++) r = 0.5f * (r + v / r);
    return r;
}

// sin without libm: range-reduce to [-pi, pi], then a 9th-order Taylor series.
static float spawn_sin(float x) {
    const float pi = 3.14159265f, tau = 6.28318531f;
    // v0.3.47: headings are not kept in [-pi, pi] (stage 9 log: -494.7); reduce any size.
    if (!(x == x) || x > 1.0e6f || x < -1.0e6f) return 0.0f;
    int k = (int)(x / tau);
    x -= (float)k * tau;
    if (x > pi) x -= tau;
    else if (x < -pi) x += tau;
    if (x > pi * 0.5f) x = pi - x;
    else if (x < -pi * 0.5f) x = -pi - x;
    float x2 = x * x;
    return x * (1.0f - x2 / 6.0f * (1.0f - x2 / 20.0f * (1.0f - x2 / 42.0f * (1.0f - x2 / 72.0f))));
}

// Passo 48: a door that loads another map (stage 2 double door) gave both players the same HP
// (in the log P1 went from 0x26 to P2's 0x14). The HP of both are kept here and, on the first
// frame of play, put back if the game wrote one value for both that is one of the two saved
// ones. A refill (continue, new stage: a value that is neither) is left alone.
static int hp_keep_pending;
static signed char hp_keep[2];

RECOMP_HOOK("func_8004C438") void coop_stage_load(void) {
    spawn_load_flag = U16(0x8016A418);
    hp_keep_pending = 0;
    if (coop_story_session() && U8(PLAYERS) == 2) {
        hp_keep[0] = S8(SLOT0_DATA + 0x176B);
        hp_keep[1] = S8(SLOT1_DATA + 0x176B);
        hp_keep_pending = (hp_keep[0] > 0 && hp_keep[1] > 0 && hp_keep[0] != hp_keep[1]);
    }
    if (recomp_get_config_u32("log") != 0) {
        unsigned int v[2] = { U16(0x8016A41A), U16(0x8016A418) };
        lad_log("stage_load", v, 2);
    }
    radar_map_reset = 1;
    hit_diag_lines = 0;
    jf_valid = 0;                                 // a load moves both: not a jump
    settle_frames = 0;                            // Passo 56c: check P2's place once play settles
    itemdiag_ready = 0;
    for (int i = 0; i < 8; i++) mg_obj[i] = 0;   // objects are rebuilt on each load
    spawn_guard = 90;
    spawn_guard_dist = -1.0f;
    lm_close_all();
    if (U8(MODE) & 1) {
        U8(PLAYERS) = 1;
        trigger_player = 0;
        trigger_ttl = 0;
        cutscene_protagonist = 0;
        in_cutscene = 0;
        U32(0x80138EEC) = 0;
    }
}

// Title menu: "1 PLAYER / 2 PLAYER" screen after STORY.
// The Versus player-count screen (title state 22) is reused: its three menu sprites
// (table 0x8010DD00) are swapped for "1 PLAYER", "2 PLAYER" and an invisible third,
// and while it is open the original title logic is paused and input is handled here.
#define TITLE_STATE   0x8010DBE8u // u8: title menu state (12 main menu, 13 start Story, 22 Versus players)
#define TITLE_LIMIT   0x8010DBF0u // s32: title logic only runs once its timer reaches this
#define TITLE_CURSOR  0x8011D754u // s32: cursor of the current title menu
#define TITLE_VS_TBL  0x8010DD00u // 3 sprite pointers drawn in state 22
#define SPR_2P        0x8010D6E8u // original "2 PLAYER" sprite (92x12 CI4)
#define MENU_SPR_1P   0x80512000u
#define MENU_SPR_NONE 0x80512040u
#define MENU_TEX_1P   0x80512100u
#define MENU_TEX_NONE 0x80512400u
#define MENU_TEX_SIZE (92 * 12 / 2)

extern unsigned int func_8008E6C0(unsigned int mask);         // menu input from all pads
extern void func_80090024(unsigned int cursor_ptr, int max); // cursor down (+sound)
extern void func_80090088(unsigned int cursor_ptr, int max); // cursor up (+sound)
extern void func_8008E9BC(void);                             // confirm sound
extern void func_8008E9EC(void);                             // cancel sound
extern void func_8008EFFC(int state);                        // go to title state (12 = main menu)

static int story_choice = 1;  // 0 = 1 Player, 1 = 2 Players (last choice made in the menu)
static int story_menu;        // the 1P/2P screen is open
static int title_prev;
static int title_frozen;
static int title_saved_limit;
static unsigned int title_saved_tbl[3];

// "1" digit drawn in the palette of the "2 PLAYER" texture (indices: 2 edge, 3/4 body, 13 shadow).
static const unsigned char glyph_one[12][20] = {
    {0},{0},
    {0,0,0,0,0,0,0,2,3,3,3,13},
    {0,0,0,0,2,3,3,3,3,3,3,13},
    {0,0,0,0,0,0,0,2,3,3,3,13},
    {0,0,0,0,0,0,2,4,4,4,13},
    {0,0,0,0,0,0,2,4,4,4,13},
    {0,0,0,0,0,2,4,4,4,13},
    {0,0,0,0,0,2,3,3,3,13},
    {0,0,2,3,3,3,3,3,3,3,3,13},
    {0},{0},
};

// Title menu list (func_8008FF34(dl, sprites, count, cursor, y, step)): the chosen item opaque, the
// others at alpha 0x55, each centred horizontally, one under the other. Same code, except for the
// mod's 1P/2P screen: it reuses the Versus 2/3/4 PLAYER list (3 items from y 0x9E), so only the two
// real items are drawn, from y 0xA5 like LOAD GAME / NEW GAME, which centres them in the same space.
extern void func_8007CFE4(unsigned int sprite, int selected);
extern void func_8007BAAC(unsigned int sprite, int r, int g, int b, int a);
extern void func_8007B8F4(unsigned int dl_ptr_ptr, unsigned int sprite_ptr, int x, int y);

RECOMP_PATCH void func_8008FF34(unsigned int dlpp, unsigned int table, int count, int cursor, int y, int step) {
    if (story_menu && table == TITLE_VS_TBL && U32(TITLE_VS_TBL + 8) == MENU_SPR_NONE) {
        count = 2;
        y = 0xA5;
    }
    for (int i = 0; i < count; i++) {
        unsigned int spr = U32(table + (unsigned int)i * 4u);
        func_8007CFE4(spr, i == cursor);
        func_8007BAAC(spr, 0xFF, 0xFF, 0xFF, (i == cursor) ? 0xFF : 0x55);
        int d = 0x140 - (int)U16(spr + 2);
        func_8007B8F4(dlpp, spr, d / 2, y);
        y += step;
    }
}

static void story_menu_open(void) {
    unsigned int i;
    unsigned int src = U32(SPR_2P + 8);
    // "1 PLAYER": the game's own "2 PLAYER" texture with the digit replaced.
    for (i = 0; i < MENU_TEX_SIZE; i++) {
        U8(MENU_TEX_1P + i) = U8(src + i);
        U8(MENU_TEX_NONE + i) = 0;
    }
    for (int y = 0; y < 12; y++) {
        for (int x = 0; x < 20; x++) {
            unsigned int a = MENU_TEX_1P + (unsigned int)(y * 92 + x) / 2;
            unsigned char v = glyph_one[y][x];
            U8(a) = (x & 1) ? (unsigned char)((U8(a) & 0xF0) | v) : (unsigned char)((U8(a) & 0x0F) | (v << 4));
        }
    }
    for (i = 0; i < 0x28; i++) {
        U8(MENU_SPR_1P + i) = U8(SPR_2P + i);
        U8(MENU_SPR_NONE + i) = U8(SPR_2P + i);
    }
    U32(MENU_SPR_1P + 8) = MENU_TEX_1P;
    U32(MENU_SPR_NONE + 8) = MENU_TEX_NONE;
    U16(MENU_SPR_NONE + 0x10) = 0;
    for (i = 0; i < 3; i++) {
        title_saved_tbl[i] = U32(TITLE_VS_TBL + i * 4);
    }
    U32(TITLE_VS_TBL + 0) = MENU_SPR_1P;
    U32(TITLE_VS_TBL + 4) = SPR_2P;
    U32(TITLE_VS_TBL + 8) = MENU_SPR_NONE;
    U32(TITLE_CURSOR) = 0; // cursor always starts on 1 PLAYER
    U8(TITLE_STATE) = 22;
    story_menu = 1;
}

static void story_menu_close(void) {
    for (int i = 0; i < 3; i++) {
        U32(TITLE_VS_TBL + i * 4) = title_saved_tbl[i];
    }
    story_menu = 0;
}

RECOMP_HOOK("func_800904E4") void coop_title_begin(void) {
    title_prev = U8(TITLE_STATE);
    title_frozen = 0;
    if (!story_menu) {
        return;
    }
    if (title_prev != 22) {
        story_menu_close();
        return;
    }
    unsigned int b = func_8008E6C0(0xC00) & 0xFFFF;
    if (b == 0x0800) {
        func_80090088(TITLE_CURSOR, 1);
    } else if (b == 0x0400) {
        func_80090024(TITLE_CURSOR, 1);
    } else if (b == 0x1000 || b == 0x8000) {
        func_8008E9BC();
        story_choice = (U32(TITLE_CURSOR) == 0) ? 0 : 1;
        inv_p2_valid = 0;   // new Story session: P2's inventory is copied from P1 at the first stage
        story_menu_close();
        U32(TITLE_CURSOR) = 0;
        U8(TITLE_STATE) = 13; // start Story; the original code runs it this frame
        return;
    } else if (b == 0x4000) {
        func_8008E9EC();
        story_menu_close();
        func_8008EFFC(12); // back to the main menu, cursor on STORY
        // Fall through to the freeze below: otherwise the main menu would read the same B
        // press this frame and go back one more screen (to PRESS START).
    }
    // Keep the original state-22 (Versus) logic from running this frame.
    title_saved_limit = (int)U32(TITLE_LIMIT);
    U32(TITLE_LIMIT) = 0x7FFFFFFF;
    title_frozen = 1;
}

RECOMP_HOOK_RETURN("func_800904E4") void coop_title_end(void) {
    if (title_frozen) {
        U32(TITLE_LIMIT) = (unsigned int)title_saved_limit;
        title_frozen = 0;
    }
    // STORY was just confirmed on the main menu: show the 1P/2P screen instead of starting.
    if (!story_menu && title_prev == 12 && U8(TITLE_STATE) == 13 &&
        recomp_get_config_u32("players") == 0) {
        story_menu_open();
    }
}

// Story player count: config "players" 0 = ask in the menu, 1 = always 2, 2 = always 1.
static int story_two_players(void) {
    unsigned int c = recomp_get_config_u32("players");
    if (c == 1) {
        return 1;
    }
    if (c == 2) {
        return 0;
    }
    return story_choice;
}

// 2. Player 2's start point, player count, and Versus HUD object creation.
RECOMP_HOOK("func_8006B310") void coop_stage_players(void) {
    if (!(U8(MODE) & 1) || !(U16(STATE) & 1)) {
        return;
    }
    // 1 Player chosen: leave the stage exactly as the original single-player Story (PLAYERS stays 1).
    if (!story_two_players()) {
        return;
    }
    float off = (float)recomp_get_config_double("spawn_distance");
    // Put P2 to the LEFT of P1, as seen from P1's start camera (camera -> player = forward).
    // Map axes are (X, -Z) with up = -Z, so left of forward (fx, fz) is (fz, -fx) in world X/Z.
    float fx = F32(START_POS + 0) - F32(START_CAM + 0);
    float fz = F32(START_POS + 8) - F32(START_CAM + 8);
    float fl = spawn_sqrt(fx * fx + fz * fz);
    if (fl > 1.0f) {
        spawn_side_x = fz / fl;
        spawn_side_z = -fx / fl;
    } else {
        spawn_side_x = 1.0f;
        spawn_side_z = 0.0f;
    }
    if (recomp_get_config_u32("spawn_side") == 1) { // Right
        spawn_side_x = -spawn_side_x;
        spawn_side_z = -spawn_side_z;
    }
    F32(START_POS + 12 + 0) = F32(START_POS + 0) + spawn_side_x * off;
    F32(START_POS + 12 + 4) = F32(START_POS + 4);
    F32(START_POS + 12 + 8) = F32(START_POS + 8) + spawn_side_z * off;
    U16(START_ID + 2) = U16(START_ID);
    F32(START_HEAD + 4) = F32(START_HEAD);
    F32(START_CAM + 12 + 0) = F32(START_CAM + 0) + spawn_side_x * off;
    F32(START_CAM + 12 + 4) = F32(START_CAM + 4);
    F32(START_CAM + 12 + 8) = F32(START_CAM + 8) + spawn_side_z * off;
    // Door map changes (stage 2 double door, Passo 46d): with spawn_distance 0 both players start
    // on the same spot in the doorway and the body collision pushes them apart, often P2 back
    // through the door into the scrap of the old side that the new map keeps. Start P2 a bit
    // ahead instead, along the start camera's forward (camera -> player), i.e. into the room.
    if (off <= 0.0f && recomp_get_config_u32("door_fix") == 0) {
        // v0.3.23: the start camera does not always look into the room (on the way back out of
        // the stage 2 building it looked inwards, and P2 started inside). The start heading is
        // the way the player faces after the door: 0 = +Z, pi = -Z (seen in the log on both
        // sides of the double door), so "ahead" = (sin h, cos h).
        // v0.3.34: ahead only for a door map change (same stage, another map, not a continue).
        // At a stage start, a continue or a checkpoint the start often faces the enemies (the
        // machine-gun boss): P2 started right in front of it. There P2 starts 55 units BEHIND.
        // A door load is requested by the script with 0x8016A418 = 1 (Passo 4.3 of HOOKS); a
        // continue/checkpoint comes after func_8009C998 and a new game has no previous map.
        unsigned int cur_map = U16(0x8016A41A);
        int door = (spawn_load_flag == 1 && spawn_prev_map != 0xFFFFu && spawn_prev_map != cur_map && !spawn_restored);
        spawn_prev_map = cur_map;
        spawn_restored = 0;
        float h = F32(START_HEAD);
        float ax, az;
        if (door) {
            ax = spawn_sin(h) * 55.0f;
            az = spawn_sin(h + 1.5707963f) * 55.0f;
        } else {
            // v0.3.36: "behind" put P2 back through the door when a continue restarts at a
            // doorway (stage 2 checkpoint, v0.3.35 log). Sideways (to P1's left, as seen along
            // the start heading) keeps both on the same side of a door and out of the line of
            // what P1 faces; the bodies then separate sideways too.
            ax = spawn_sin(h + 1.5707963f) * 45.0f;
            az = -spawn_sin(h) * 45.0f;
        }
        F32(START_POS + 12 + 0) += ax;
        F32(START_POS + 12 + 8) += az;
        F32(START_CAM + 12 + 0) += ax;
        F32(START_CAM + 12 + 8) += az;
        if (recomp_get_config_u32("log") != 0) {
            union { float f; unsigned int u; } hb, cx, cz;
            hb.f = h; cx.f = fx; cz.f = fz;
            unsigned int v[7] = { U16(0x8016A41A), U32(START_POS + 12 + 0), U32(START_POS + 12 + 8), hb.u, cx.u, cz.u, (unsigned int)door };
            lad_log(door ? "p2_start_ahead" : "p2_start_side", v, 7);
        }
    }
    // Passo 56b: the intro of a boss can move P1 before the first frame of play (shotgun boss:
    // P1 put behind cover during the intro, P2 left at the start). Start the jump watch from
    // P1's start point, so that move counts as a jump on the first frame of play.
    jf_last[0] = F32(START_POS + 0);
    jf_last[1] = F32(START_POS + 4);
    jf_last[2] = F32(START_POS + 8);
    jf_valid = 1;
    U8(PLAYERS) = 2;
    if (!inv_p2_valid) {
        inv_init_p2();
    }
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
    // Passo 49 diagnosis: P2's state once a second (action, sub-state, flags +0xF4, +0x88,
    // position, room state of both, PLAYERS, the shot-room nesting depth).
    unsigned int v[10] = { U16(SLOT1_DATA + 0x16EC), U8(SLOT1_DATA + 0x16F0), U32(SLOT1_DATA + 0xF4),
                           U16(SLOT1_DATA + 0x88), U32(SLOT1_DATA + 0), U32(SLOT1_DATA + 8),
                           U32(ROOM_STATE + 0), U32(ROOM_STATE + 4), U8(PLAYERS),
                           (unsigned int)hit_room_depth | ((U32(PLAYER_PTRS) == SLOT1_DATA) ? 0x100u : 0u) };
    lad_log("p2_state", v, 10);
    // Passo 51 diagnosis (stage 3 mounted machine gun): the characters near P2 (list at
    // D_801788F0, next +0x1C, data +0x2C): object, update (+0x24), type (+0x92), room (+0x8E),
    // action (+0x16EC), HP, distance to P2 and to P1.
    int n = 0;
    for (unsigned int o = U32(0x801788F0); o >= 0x80000000u && o < 0x80800000u && n < 8; o = U32(o + 0x1C)) {
        unsigned int d = U32(o + 0x2C);
        if (d < 0x80000000u || d >= 0x80800000u || d == SLOT0_DATA || d == SLOT1_DATA) continue;
        float x1 = F32(d + 0) - F32(SLOT1_DATA + 0), z1 = F32(d + 8) - F32(SLOT1_DATA + 8);
        float x0 = F32(d + 0) - F32(SLOT0_DATA + 0), z0 = F32(d + 8) - F32(SLOT0_DATA + 8);
        float q1 = x1 * x1 + z1 * z1, q0 = x0 * x0 + z0 * z0;
        if (q1 > 1500.0f * 1500.0f) continue;
        n++;
        unsigned int w[9] = { o, U32(o + 0x24), U16(d + 0x92), U16(d + 0x8E), U16(d + 0x16EC),
                              (unsigned int)(unsigned char)S8(d + 0x176B), (unsigned int)(int)spawn_sqrt(q1),
                              (unsigned int)(int)spawn_sqrt(q0), U32(o + 0x28) };
        lad_log("near_p2", w, 9);
    }
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

#define SKY_MTX_P1  0x80500080u // 64-byte N64 Mtx for Viewport 0 Sky Dome
#define SKY_MTX_P2  0x805000C0u // 64-byte N64 Mtx for Viewport 1 Sky Dome

static unsigned char fb_clear_active;
static unsigned char fb_saved_vp;
static int fb_p0_out;
static int fb_p1_out;
static unsigned int sky_upd_obj;
static unsigned int sky_upd_saved_parent;
static unsigned int sky_upd_player;
static short sky_upd_saved_room;
static unsigned int sky_draw_obj;
static int sky_draw_vp;
static unsigned int sky_draw_saved_parent;
static unsigned int sky_draw_saved_p0;
static unsigned int sky_draw_player;
static short sky_draw_saved_room;

// 4. Two-view split, Engineer buffer protection, and Death Match HUD rules while drawing.
// Auto-aim tuning: func_80058438 reads its range/cone limits from rodata doubles.
static void set_f64(unsigned int addr, unsigned int hi, unsigned int lo) {
    if (U32(addr) != hi || U32(addr + 4) != lo) {
        U32(addr) = hi;
        U32(addr + 4) = lo;
    }
}

static void apply_aim_tuning(void) {
    set_f64(0x80117050u, 0x40C77000u, 0x00000000u); // base range: 1600 -> 12000
    set_f64(0x80117058u, 0x40CB5800u, 0x00000000u); // enemy range: 2000 -> 14000
    set_f64(0x80117060u, 0x3FF921FBu, 0x54442D18u); // aim cone: 72 deg -> 90 deg
    set_f64(0x80117068u, 0x40740000u, 0x00000000u); // close-range bypass: 200 -> 320
}

// Friendly fire: the game's bullets never test the partner in Story, so the mod checks it.
// A shot is seen as the magazine count dropping; the shooter aims along its view camera.
static int ff_last_id[2] = { -1, -1 };
static int ff_last_mag[2] = { -1, -1 };

static float ff_sin(float a) {
    const float PI = 3.14159265f, TWO_PI = 6.28318531f;
    int guard = 0;
    while (a > PI && guard++ < 64) a -= TWO_PI;
    while (a < -PI && guard++ < 128) a += TWO_PI;
    if (a > PI * 0.5f) a = PI - a;          // fold to [-pi/2, pi/2]
    else if (a < -PI * 0.5f) a = -PI - a;
    float a2 = a * a;
    return a * (1.0f - a2 / 6.0f * (1.0f - a2 / 20.0f * (1.0f - a2 / 42.0f * (1.0f - a2 / 72.0f))));
}

static unsigned int ff_mag_addr(unsigned int p, int id) {
    if (id == 0) return p + 0x1767;
    if (id == 1) return p + 0x174F;
    if (id == 2) return p + 0x1763;
    return 0;
}

// Friendly fire on the game's own shot ray (see ff_scan_ray_begin): the partner is hit only if
// the ray passes through its body (radius FF_RADIUS around the centre line, FF_HALF_H up and down
// from the chest) before the wall. A shot (magazine drop, coop_friendly_fire) and a ray that hits
// the partner are paired when they are at most 3 frames apart.
static int ai_bullet_owner;   // defined with its initialiser in enemy_ai.h
#define FF_RADIUS   26.0f
#define FF_CHEST    80.0f      // centre of the body above the position (feet)
#define FF_HALF_H   95.0f
static int ff_pending[2], ff_pending_id[2];
static unsigned int ff_pending_tick[2];
static unsigned int ff_scan_hit_tick[2] = { 0x80000000u, 0x80000000u };
static float ff_scan_hit_t[2];
static int ff_scan_wall;
static float ff_o[3], ff_e[3], ff_w[3];
static unsigned int ff_last_dist_a;      // first pointer of the last func_8004387C call in the shot
static unsigned int ff_scan_seen_tick[2] = { 0x80000000u, 0x80000000u };   // last ray of each player (diagnosis)

static void ff_apply(int k, int id, float dist, int shots) {
    unsigned int q = k ? SLOT0_DATA : SLOT1_DATA;
    if (S8(q + 0x176B) <= 0) return;
    int dmg = (id == 2) ? (dist < 600.0f ? 20 : 8) : (id == 1 ? 5 : 8);
    dmg *= shots;
    // Apply the hit directly (the game's damage routine can refuse player targets):
    // lower the life and trigger the same hit reaction it sets (+0x88, +0x174D).
    int hp = S8(q + 0x176B) - dmg;
    if (hp < 0) hp = 0;
    S8(q + 0x176B) = (signed char)hp;
    U16(q + 0x88) = 0x4000;
    U8(q + 0x174D) = 0x1E;
    if (recomp_get_config_u32("log") != 0) winback_mod_log(k ? "coop: ff P2 hit P1" : "coop: ff P1 hit P2");
}

static void ff_try_resolve(int k) {
    if (ff_pending[k] <= 0) return;
    if ((unsigned int)(lm_tick - ff_scan_hit_tick[k]) > 3u) return;
    ff_apply(k, ff_pending_id[k], ff_scan_hit_t[k], ff_pending[k]);
    ff_pending[k] = 0;
    ff_scan_hit_tick[k] = lm_tick - 0x40000000u;     // used up
}

// Every ray of the game goes through func_8004221C(origin, end, ...): bullets, line of sight,
// the march of the shot code. The players' shot rays are recognised by where they start: at the
// shooter's body (within FF_ORIGIN_R horizontally, FF_ORIGIN_H vertically) and long
// (> FF_MIN_LEN). The wall, when there is one, is the func_8004387C call measured from the same
// origin variable just before (see ff_scan_wall_begin).
#define FF_ORIGIN_R 100.0f
#define FF_ORIGIN_H 260.0f
#define FF_MIN_LEN  400.0f
static float ff_near_d[2] = { 99999.0f, 99999.0f };   // diagnosis: closest ray origin since the last shot
static int ff_rays_seen;

static void ff_check_ray(int k) {
    unsigned int p = k ? SLOT1_DATA : SLOT0_DATA;
    unsigned int q = k ? SLOT0_DATA : SLOT1_DATA;
    int lg = recomp_get_config_u32("log") != 0;
    if (recomp_get_config_u32("friendly_fire") != 0) return;
    if (S8(p + 0x176B) <= 0 || S8(q + 0x176B) <= 0) return;
    if (U16(p + 0x8E) != U16(q + 0x8E)) return;
    float dx = ff_e[0] - ff_o[0], dy = ff_e[1] - ff_o[1], dz = ff_e[2] - ff_o[2];
    float len = spawn_sqrt(dx * dx + dy * dy + dz * dz);
    if (len < 1.0f) return;
    dx /= len; dy /= len; dz /= len;
    float stop = len;                                    // where the bullet ends: range or wall
    if (ff_scan_wall) {
        float wx = ff_w[0] - ff_o[0], wy = ff_w[1] - ff_o[1], wz = ff_w[2] - ff_o[2];
        stop = spawn_sqrt(wx * wx + wy * wy + wz * wz);
    }
    float vx = F32(q + 0) - ff_o[0], vy = F32(q + 4) + FF_CHEST - ff_o[1], vz = F32(q + 8) - ff_o[2];
    float t = vx * dx + vy * dy + vz * dz;              // distance along the ray to the partner
    float sx = vx - dx * t, sz = vz - dz * t;            // horizontal miss distance
    float side = spawn_sqrt(sx * sx + sz * sz);
    float up = vy - dy * t;
    if (up < 0.0f) up = -up;
    int hit = (t > 10.0f && t < stop - 8.0f && side < FF_RADIUS && up < FF_HALF_H);
    if (hit || (lg && ff_pending[k] > 0)) {
        if (lg) {
            union { float f; unsigned int u; } a, b, c, d;
            a.f = t; b.f = stop; c.f = side; d.f = up;
            static const char tmpl[] = "coop: ff ray ... t 0x........ wall 0x........ side 0x........ up 0x........";
            static char line[sizeof(tmpl)];
            for (unsigned int n = 0; n < sizeof(tmpl); n++) line[n] = tmpl[n];
            line[13] = hit ? 'H' : 'm'; line[14] = hit ? 'I' : 'i'; line[15] = hit ? 'T' : 's';
            put_hex(line + 21, a.u, 8);
            put_hex(line + 37, b.u, 8);
            put_hex(line + 53, c.u, 8);
            put_hex(line + 67, d.u, 8);
            if (!ff_scan_wall) {
                static const char none[10] = { 'n', 'o', 'n', 'e', ' ', ' ', ' ', ' ', ' ', ' ' };
                for (int n = 0; n < 10; n++) line[35 + n] = none[n];
            }
            winback_mod_log(line);
        }
    }
    if (!hit) return;
    ff_scan_hit_tick[k] = lm_tick;
    ff_scan_hit_t[k] = t;
    ff_try_resolve(k);
}

RECOMP_HOOK("func_8004221C") void ff_scan_ray_begin(unsigned int o, unsigned int e) {
    if (U8(PLAYERS) != 2) return;
    if (o < 0x80000000u || o >= 0x80800000u || e < 0x80000000u || e >= 0x80800000u) return;
    float ox = F32(o), oy = F32(o + 4), oz = F32(o + 8);
    float ex = F32(e), ey = F32(e + 4), ez = F32(e + 8);
    ff_rays_seen++;
    int best = -1;
    float bestd = FF_ORIGIN_R;
    for (int k = 0; k < 2; k++) {
        unsigned int p = k ? SLOT1_DATA : SLOT0_DATA;
        float hx = ox - F32(p + 0), hz = oz - F32(p + 8), hy = oy - F32(p + 4);
        float d = spawn_sqrt(hx * hx + hz * hz);
        if (hy < 0.0f) hy = -hy;
        if (hy > FF_ORIGIN_H) continue;
        if (d < ff_near_d[k]) ff_near_d[k] = d;
        if (d < bestd) { bestd = d; best = k; }
    }
    int was_wall = (ff_last_dist_a == o);
    ff_last_dist_a = 0;
    if (best < 0) return;
    float lx = ex - ox, ly = ey - oy, lz = ez - oz;
    if (lx * lx + ly * ly + lz * lz < FF_MIN_LEN * FF_MIN_LEN) return;
    ff_o[0] = ox; ff_o[1] = oy; ff_o[2] = oz;
    ff_e[0] = ex; ff_e[1] = ey; ff_e[2] = ez;
    ff_scan_wall = was_wall;
    ff_scan_seen_tick[best] = lm_tick;
    ff_check_ray(best);
}

RECOMP_HOOK("func_8004387C") void ff_scan_wall_begin(unsigned int o, unsigned int w) {
    if (U8(PLAYERS) != 2) return;
    if (w < 0x80000000u || w >= 0x80800000u) return;
    ff_last_dist_a = o;                                 // kept if the next ray starts from it
    for (int i = 0; i < 3; i++) ff_w[i] = F32(w + (unsigned int)i * 4u);
}

static void coop_friendly_fire(void) {
    for (int i = 0; i < 2; i++) {
        unsigned int p = U32(PLAYER_PTRS + i * 4);
        unsigned int q = U32(PLAYER_PTRS + (i ^ 1) * 4);
        if (p == 0 || q == 0) continue;
        int k = (p == SLOT1_DATA) ? 1 : 0;
        int id = U8(p + 0x1758);
        unsigned int ma = ff_mag_addr(p, id);
        int mag = ma ? U8(ma) : -1;
        int shots = (id == ff_last_id[k] && mag >= 0 && ff_last_mag[k] > mag) ? ff_last_mag[k] - mag : 0;
        ff_last_id[k] = id;
        ff_last_mag[k] = mag;
        if (shots <= 0 || shots > 3 || recomp_get_config_u32("friendly_fire") != 0) continue;
        if (recomp_get_config_u32("log") != 0) {
            winback_mod_log(k ? "coop: shot by P2" : "coop: shot by P1");
            if (k) { shot_diag_lines = 0; shot_diag_tick = lm_tick; }   // Passo 47 diagnosis
            if ((unsigned int)(lm_tick - ff_scan_seen_tick[k]) > 3u) {
                // diagnosis: how far from the shooter the closest ray started, and how many rays
                union { float f; unsigned int u; } nd;
                nd.f = ff_near_d[k];
                static const char tmpl[] = "coop: ff no ray at the shooter; closest origin 0x........ rays 0x........";
                static char line[sizeof(tmpl)];
                for (unsigned int n = 0; n < sizeof(tmpl); n++) line[n] = tmpl[n];
                put_hex(line + 49, nd.u, 8);
                put_hex(line + 65, (unsigned int)ff_rays_seen, 8);
                winback_mod_log(line);
            }
            ff_near_d[k] = 99999.0f;
            ff_rays_seen = 0;
        }
        if (S8(p + 0x176B) <= 0 || S8(q + 0x176B) <= 0) continue;
        if (U16(p + 0x8E) != U16(q + 0x8E)) continue;
        // The hit itself is decided on the game's own shot ray (ff_scan_ray_begin): a shot is
        // pending for a few frames until that ray is seen.
        ff_pending[k] = shots;
        ff_pending_id[k] = id;
        ff_pending_tick[k] = lm_tick;
        ff_try_resolve(k);
    }
    for (int k = 0; k < 2; k++)
        if (ff_pending[k] > 0 && (unsigned int)(lm_tick - ff_pending_tick[k]) > 3u) ff_pending[k] = 0;
}

// Hard / Brutal: pickups give less ammo. Reserve counters are compared frame to frame and
// any increase (a pickup) is scaled down.
static const unsigned int pick_addr[4] = { 0x801587C0u, 0x801587C1u, 0x801587CBu, 0x801587C4u };
static int pick_last[2][4] = { { -1, -1, -1, -1 }, { -1, -1, -1, -1 } };   // declared above (lm_give)
static int pick_rem[2][4];

static void coop_pickup_scale(int settle) {
    int d = coop_difficulty();
    for (int k = 0; k < 2; k++) {
        for (int i = 0; i < 4; i++) {
            int v = inv_get(k, pick_addr[i]);
            int *last = &pick_last[k][i];
            if (!settle && d != 0 && *last >= 0 && v > *last) {
                int gain = v - *last;
                int s2 = gain * diff_pickup[d] + pick_rem[k][i];
                int g = s2 / 100;
                pick_rem[k][i] = s2 - g * 100;
                if (g < 1 && *last == 0) { g = 1; pick_rem[k][i] = 0; }  // never leave a new weapon empty
                v = *last + g;
                inv_set(k, pick_addr[i], v);
            }
            *last = v;
        }
    }
}

// Ladder diagnosis (option log): action 23 (ladder) / 9 (vault) of each player, the event flag
// 0x8010E0F8, cutscene state and player order, plus the event calls made while someone is on a
// ladder. One line when something changes, and every 8 frames while on the ladder.
// "coop: lad <tag> a b c ..." with hex fields.
static int lad_watch;                 // frames left to keep logging after a ladder/vault
static int lad_cond_lines;            // condition lines logged for the current ladder
static unsigned short lad_last_act[2] = { 0xFFFF, 0xFFFF };
static unsigned char lad_last_cut = 0xFF;

static void lad_log(const char *tag, const unsigned int *v, int n) {
    static char line[160];
    int k = 0;
    const char *pre = "coop: lad ";
    while (*pre) line[k++] = *pre++;
    while (*tag && k < 40) line[k++] = *tag++;
    for (int i = 0; i < n && k < 150; i++) {
        line[k++] = ' ';
        put_hex(line + k, v[i], 8);
        k += 8;
    }
    line[k] = 0;
    winback_mod_log(line);
}

static int lad_someone_on(void) {
    unsigned short a0 = U16(SLOT0_DATA + 0x16EC), a1 = U16(SLOT1_DATA + 0x16EC);
    return a0 == 23 || a0 == 9 || a1 == 23 || a1 == 9 ||
           a0 == 12 || a0 == 13 || a1 == 12 || a1 == 13;   // doors too (stage 2 double door diagnosis)
}

// Door / room diagnosis (option log): one line whenever a player's room, sub-room or zone
// marker changes ("coop: lad rooms"), and the flag bytes +0x14..+0x17 of every room of the
// room table (0x801D3750, 0x58 per room, count 0x8016A396) whenever any of them changes
// ("coop: lad rflags"), to see whether a door switches rooms on or off for both players.
static unsigned int door_last_room[2] = { 0xFFFFFFFFu, 0xFFFFFFFFu };
static unsigned short door_last_mark[2] = { 0xFFFF, 0xFFFF };
static unsigned int door_last_hash = 0xFFFFFFFFu;

static void door_rooms_log(void) {
    if (recomp_get_config_u32("log") == 0 || !coop_story_session()) return;
    int changed = 0;
    for (int k = 0; k < 2; k++) {
        unsigned int d = k ? SLOT1_DATA : SLOT0_DATA;
        if (U32(ROOM_STATE + k * 4) != door_last_room[k] || U16(d + 0x8A) != door_last_mark[k]) changed = 1;
    }
    if (changed) {
        unsigned int v[11];
        for (int k = 0; k < 2; k++) {
            unsigned int d = k ? SLOT1_DATA : SLOT0_DATA;
            door_last_room[k] = U32(ROOM_STATE + k * 4);
            door_last_mark[k] = U16(d + 0x8A);
            v[k * 5 + 0] = door_last_room[k];             // room << 16 | sub-room
            v[k * 5 + 1] = door_last_mark[k];             // zone marker
            v[k * 5 + 2] = U16(d + 0x8E);                 // room in the player struct
            v[k * 5 + 3] = U32(d + 0);                    // x (float bits)
            v[k * 5 + 4] = U32(d + 8);                    // z (float bits)
        }
        v[10] = U16(0x8016A41A) | ((unsigned int)U16(0x8016A396) << 16);   // map id, room count
        lad_log("rooms", v, 11);
    }
    int n = S16(0x8016A396);
    if (n <= 0 || n > 64) return;
    unsigned int h = 2166136261u;
    for (int r = 0; r < n; r++) {
        h ^= U32(0x801D3764u + (unsigned int)r * 0x58u);
        h *= 16777619u;
    }
    if (h == door_last_hash) return;
    door_last_hash = h;
    for (int r0 = 0; r0 < n; r0 += 12) {
        unsigned int v[13];
        int c = 0;
        v[c++] = (unsigned int)r0;
        for (int r = r0; r < n && r < r0 + 12; r++) v[c++] = U32(0x801D3764u + (unsigned int)r * 0x58u);
        lad_log("rflags", v, c);
    }
}

static void lad_state(int k, const char *why) {
    unsigned int d = k ? SLOT1_DATA : SLOT0_DATA;
    unsigned int v[10];
    v[0] = (unsigned int)k;
    v[1] = U16(d + 0x16EC);                          // action
    v[2] = U8(d + 0x16F0);                           // sub-state (0x0B down, 0x0C up)
    v[3] = (unsigned int)(unsigned short)S16(d + 0x16E4);   // animation
    v[4] = U32(d + 0x16C8);                          // animation frame (float bits)
    v[5] = U32(d + 0xF4) & 0xCu;                     // on the ground (0x4 / 0x8)
    v[6] = U32(d + 4);                               // y (float bits)
    v[7] = (unsigned int)in_cutscene | ((unsigned int)trigger_player << 4) | ((unsigned int)cutscene_protagonist << 8);
    v[8] = (U32(PLAYER_PTRS) == SLOT1_DATA) ? 1u : 0u;   // 1 = P2 swapped in as player 1
    v[9] = U16(0x8010E0F8);
    lad_log(why, v, 10);
}

static void lad_frame(void) {
    if (recomp_get_config_u32("log") == 0 || !coop_story_session()) return;
    door_rooms_log();
    int on = lad_someone_on();
    if (on) lad_watch = 240;
    if (!on && lad_watch <= 0) lad_cond_lines = 0;
    if (lad_watch <= 0) return;
    lad_watch--;
    for (int k = 0; k < 2; k++) {
        unsigned int d = k ? SLOT1_DATA : SLOT0_DATA;
        unsigned short a = U16(d + 0x16EC);
        if (a != lad_last_act[k]) {
            lad_last_act[k] = a;
            lad_state(k, "act");
        } else if ((a == 23 || a == 9) && (lm_tick & 7u) == 0) {
            lad_state(k, "on");
        }
    }
    if (in_cutscene != lad_last_cut) {
        unsigned int v[3] = { in_cutscene, (unsigned int)cutscene_protagonist, (U32(PLAYER_PTRS) == SLOT1_DATA) ? 1u : 0u };
        lad_last_cut = in_cutscene;
        lad_log("cutscene", v, 3);
    }
}

static void lad_event(const char *tag, int a, int b) {
    if (recomp_get_config_u32("log") == 0 || !coop_story_session()) return;
    if (!lad_someone_on() && lad_watch <= 0) return;
    unsigned int v[6] = { (unsigned int)a, (unsigned int)b, (unsigned int)trigger_player, (unsigned int)in_cutscene,
                          (U32(PLAYER_PTRS) == SLOT1_DATA) ? 1u : 0u, U16(0x8010E0F8) };
    lad_log(tag, v, 6);
}

// Ladder diagnosis, part 2: the script conditions the game tests for event 15 (ladder), and the
// zone markers of both players. func_8009C050 tries every enabled script with
// func_8009BBB8(type, value, conditions); the first one whose conditions pass runs. Conditions
// read PLAYER_PTRS[0] + 0x8A (zone marker) -> D_801D41D0 + marker * 60 (+0x2 flags, +0xC zone id).
static void lad_markers(const char *tag) {
    unsigned int v[10];
    for (int k = 0; k < 2; k++) {
        unsigned int d = k ? SLOT1_DATA : SLOT0_DATA;
        unsigned int m = U16(d + 0x8A);
        v[k * 4 + 0] = m;
        v[k * 4 + 1] = U16(d + 0x90);
        v[k * 4 + 2] = (m < 512u) ? U16(0x801D41D2u + m * 60u) : 0xFFFFu;   // zone flags
        v[k * 4 + 3] = (m < 512u) ? U8(0x801D41DCu + m * 60u) : 0xFFu;      // zone id
    }
    v[8] = U16(0x8016A39Au);
    v[9] = U16(0x8016A39Cu);
    lad_log(tag, v, 10);
}

RECOMP_HOOK("func_8009BBB8") void lad_cond_begin(int type, int val, unsigned int cond) {
    if ((type & 0xFF) != 15 || recomp_get_config_u32("log") == 0) return;
    if (!lad_someone_on() && lad_watch <= 0) return;
    if (lad_cond_lines >= 40) return;              // one event is enough
    lad_cond_lines++;
    if (cond < 0x80000000u || cond >= 0x80800000u) return;
    unsigned int v[9];
    v[0] = ((unsigned int)(type & 0xFF) << 16) | ((unsigned int)val & 0xFFFFu);
    v[1] = cond;
    for (int i = 0; i < 6; i++) v[2 + i] = U32((cond & ~3u) + (unsigned int)i * 4u);
    v[8] = cond & 3u;                              // the bytes start this far into v[2]
    lad_log("cond", v, 9);
    if (lad_cond_lines == 1) lad_markers("markers");
}

// Ladder fade only on the climber's view (co-op). The ladder script fades the screen with
// opcode 0x16 (func_8007A0D8 colour + func_8007A1C0 start); func_8007A280 (from func_800944E0)
// moves the level D_8010AF14 to 0xFF and back, and the script waits for it, so the level itself
// is left alone. Only the drawing changes:
//   - func_80025F10 skips the 3D of both views while D_8010AF14 > 0x80: it is held at 0x80
//     during the draw, so both views keep rendering;
//   - func_8007A238 draws the full-screen fade sprite D_8010AF2C when D_8010AF14 != 0: it sees
//     0 instead, and the mod draws the same colour and level over the climber's half only
//     (func_8007CBC8 colour + func_8007CD50 rectangle, top view y 0..120, bottom 120..240).
// Only within 400 frames of a player being in action 23 (ladder) or 9 (vault).
#define FADE_LEVEL 0x8010AF14u
#define FADE_SPRITE 0x8010AF2Cu
extern void func_8007CBC8(int r, int g, int b, int a);
extern void func_8007CD50(unsigned int dlpp, int x, int y, int w, int h);
static unsigned int lad_act_tick = 0x80000000u;
static int lad_act_player;
static int fade_draw_saved = -1;     // D_8010AF14 held during func_80025F10
static int fade_spr_saved = -1;      // D_8010AF14 hidden from func_8007A238

static int fade_split_active(void) {
    if (!coop_story_session() || U8(PLAYERS) != 2) return 0;
    if (recomp_get_config_u32("ladder_fade") != 0) return 0;
    return (unsigned int)(lm_tick - lad_act_tick) < 400u;
}

static void fade_track_ladder(void) {
    for (int k = 0; k < 2; k++) {
        unsigned short a = U16((k ? SLOT1_DATA : SLOT0_DATA) + 0x16EC);
        if (a == 23 || a == 9) {
            lad_act_tick = lm_tick;
            lad_act_player = k;
        }
    }
}

RECOMP_HOOK("func_8007A238") void coop_fade_draw_begin(void) {
    fade_spr_saved = -1;
    if (!fade_split_active()) return;
    int level = (fade_draw_saved >= 0) ? fade_draw_saved : U8(FADE_LEVEL);
    if (level == 0) return;
    fade_spr_saved = U8(FADE_LEVEL);
    U8(FADE_LEVEL) = 0;                          // the game's full-screen sprite is skipped
    int r = U8(FADE_SPRITE + 0x20), g = U8(FADE_SPRITE + 0x21), b = U8(FADE_SPRITE + 0x22);
    func_8007CBC8(r, g, b, level);
    int y0 = lad_act_player ? 120 : 0;
    func_8007CD50(0x801D2C80u, -200, y0, 720, 120);   // wider than 320: covers widescreen sides too
}

RECOMP_HOOK_RETURN("func_8007A238") void coop_fade_draw_end(void) {
    if (fade_spr_saved >= 0) {
        U8(FADE_LEVEL) = (unsigned char)fade_spr_saved;
        fade_spr_saved = -1;
    }
}

// Passo 47: func_8004394C is the character test of a shot ray. In Story it only tests
// characters whose zone/room (+0x8A/+0x8C/+0x8E) equals P1's room or sub-room (D_8016A410/12),
// so with P1 and P2 in different rooms, P2's shots went through the enemies of P2's room and
// those enemies' shots went through P2 (kicks, which don't use rays, still hit). When the
// shooter (a0, or the object that fired it, a0 + 0x20) is P2, or a character standing in P2's
// room and not in P1's, ROOM_STATE is swapped for the call, as for func_8003FCE4.
static unsigned char hit_room_swap[8];
// The character data of the shooter: the object itself or, for a bullet, the object that fired
// it (+0x20). 0 if none looks like a character.
static unsigned int hit_shooter_data(unsigned int obj) {
    for (int i = 0; i < 2; i++) {
        if (obj < 0x80000000u || obj >= 0x80800000u) return 0;
        unsigned int d = U32(obj + 0x2C);
        if (d == SLOT0_DATA || d == SLOT1_DATA) return d;
        if (d >= 0x80000000u && d < 0x80800000u && U16(d + 0x16EC) < 0x100u && S8(d + 0x176B) > 0) return d;
        obj = U32(obj + 0x20);
    }
    return 0;
}
RECOMP_HOOK("func_8004394C") void coop_hit_room_begin(unsigned int shooter) {
    int lvl = hit_room_depth++;
    if (lvl < 8) hit_room_swap[lvl] = 0;
    if (lvl >= 8 || !coop_story_session() || U8(PLAYERS) != 2 || recomp_get_config_u32("shot_rooms") != 0) return;
    unsigned int d = hit_shooter_data(shooter);
    if (d == 0 || d == SLOT0_DATA) return;            // P1 (or unknown): the game's own rule
    short a0 = S16(ROOM_STATE + 0), a1 = S16(ROOM_STATE + 2);
    short b0 = S16(ROOM_STATE + 4), b1 = S16(ROOM_STATE + 6);
    int swap;
    if (d == SLOT1_DATA) {
        swap = 1;
    } else {
        // An enemy: v0.3.25 swapped only when its room (+0x8E) was P2's and not P1's, which
        // missed the enemies shooting at P2 (v0.3.27 test). Now, with the players in different
        // rooms, an enemy whose target is P2 or that is nearer to P2 tests P2's room.
        if (a0 == b0) return;
        short r = S16(d + 0x8E);
        float x0 = F32(d + 0) - F32(SLOT0_DATA + 0), z0 = F32(d + 8) - F32(SLOT0_DATA + 8);
        float x1 = F32(d + 0) - F32(SLOT1_DATA + 0), z1 = F32(d + 8) - F32(SLOT1_DATA + 8);
        int near_p2 = (x1 * x1 + z1 * z1) < (x0 * x0 + z0 * z0);
        int room_p2 = (r != a0 && r != a1) && (r == b0 || r == b1);
        int tgt_p2 = (d == cur_enemy && swapped_players);
        swap = room_p2 || near_p2 || tgt_p2;
        if (hit_diag_lines < 24 && recomp_get_config_u32("log") != 0) {
            hit_diag_lines++;
            unsigned int v[7] = { shooter, d, (unsigned int)(unsigned short)r, U32(ROOM_STATE + 0), U32(ROOM_STATE + 4),
                                  (unsigned int)((room_p2 << 8) | (near_p2 << 4) | tgt_p2), (unsigned int)swap };
            lad_log("enemy_shot", v, 7);
        }
    }
    if (!swap) return;
    unsigned int t = U32(ROOM_STATE);
    U32(ROOM_STATE) = U32(ROOM_STATE + 4);
    U32(ROOM_STATE + 4) = t;
    hit_room_swap[lvl] = 1;
}
RECOMP_HOOK_RETURN("func_8004394C") void coop_hit_room_end(void) {
    if (hit_room_depth <= 0) { hit_room_depth = 0; return; }
    int lvl = --hit_room_depth;
    if (lvl >= 8 || !hit_room_swap[lvl]) return;
    hit_room_swap[lvl] = 0;
    unsigned int t = U32(ROOM_STATE);
    U32(ROOM_STATE) = U32(ROOM_STATE + 4);
    U32(ROOM_STATE + 4) = t;
}

// Passo 50: the stage 3 laser trap (object family set up by func_800D49A0) only works in
// P1's room. Its update (func_800D5094 for the emitter) and its draws (func_800D500C, and
// func_800D5884 for the beam, which also tests the beam against D_801390C0 = P1) all compare
// the object's room (+0x8E) with P1's room/sub-room (D_8016A410/12). So it only switched on
// when P1 came near, and P2 could cross the beam. Now:
//  - update: if the object is in P2's room and not in P1's (or in both and P2 is nearer),
//    ROOM_STATE and PLAYER_PTRS[0]/[1] are swapped for the call (P2 switches it on);
//  - draws: while drawing P2's half of the split screen, the same swap, so P2 sees the beam
//    and the beam is tested against P2 (crossing it sets off the trap like for P1).
static unsigned char trap_swap[4];
static int trap_depth;
static int trap_room_is_p2_only(unsigned int obj) {
    if (obj < 0x80000000u || obj >= 0x80800000u) return 0;
    unsigned int d = U32(obj + 0x2C);
    if (d < 0x80000000u || d >= 0x80800000u) return 0;
    short r = S16(d + 0x8E);
    short a0 = S16(ROOM_STATE + 0), a1 = S16(ROOM_STATE + 2);
    short b0 = S16(ROOM_STATE + 4), b1 = S16(ROOM_STATE + 6);
    if (r != b0 && r != b1) return 0;                 // not in P2's room
    if (r != a0 && r != a1) return 1;                 // only in P2's room
    // In both players' rooms: the trap measures its distance to D_801390C0, so use P2 when
    // P2 is the nearer one.
    float x0 = F32(d + 0) - F32(SLOT0_DATA + 0), z0 = F32(d + 8) - F32(SLOT0_DATA + 8);
    float x1 = F32(d + 0) - F32(SLOT1_DATA + 0), z1 = F32(d + 8) - F32(SLOT1_DATA + 8);
    return (x1 * x1 + z1 * z1) < (x0 * x0 + z0 * z0);
}
static void trap_swap_players(void) {
    unsigned int t = U32(ROOM_STATE);
    U32(ROOM_STATE) = U32(ROOM_STATE + 4);
    U32(ROOM_STATE + 4) = t;
    t = U32(PLAYER_PTRS);
    U32(PLAYER_PTRS) = U32(PLAYER_PTRS + 4);
    U32(PLAYER_PTRS + 4) = t;
}
static void trap_begin(unsigned int obj, int draw) {
    int lvl = trap_depth++;
    if (lvl >= 4) return;
    trap_swap[lvl] = 0;
    if (U8(PLAYERS) != 2 || !((U8(MODE) & 1) || drawing_split) || recomp_get_config_u32("traps") != 0) return;
    if (U32(PLAYER_PTRS) != SLOT0_DATA) return;      // already swapped by something else
    int swap = draw ? (drawing_split && U8(0x8016A40F) == 1) : trap_room_is_p2_only(obj);
    if (!swap) return;
    trap_swap_players();
    trap_swap[lvl] = 1;
}
static void trap_end(void) {
    if (trap_depth <= 0) { trap_depth = 0; return; }
    int lvl = --trap_depth;
    if (lvl >= 4 || !trap_swap[lvl]) return;
    trap_swap[lvl] = 0;
    trap_swap_players();
}
RECOMP_HOOK("func_800D5094") void coop_trap_update_begin(unsigned int obj) { trap_begin(obj, 0); }
RECOMP_HOOK_RETURN("func_800D5094") void coop_trap_update_end(void) { trap_end(); }
RECOMP_HOOK("func_800D500C") void coop_trap_draw_begin(unsigned int obj) { trap_begin(obj, 1); }
RECOMP_HOOK_RETURN("func_800D500C") void coop_trap_draw_end(void) { trap_end(); }
RECOMP_HOOK("func_800D5884") void coop_beam_draw_begin(unsigned int obj) { trap_begin(obj, 1); }
RECOMP_HOOK_RETURN("func_800D5884") void coop_beam_draw_end(void) { trap_end(); }

// Passo 51b: the stage 3 mounted machine gun is an object of its own (update func_800D6044,
// draw func_800D6028, type 2 in the log), not an enemy: it measures, aims and fires only at
// D_801390C0 (P1) through func_800D6364 / func_800D65A4. Its update now runs with P2 as
// player 1 (PLAYER_PTRS and ROOM_STATE swapped, as for the traps) when P2 is the living
// player it should aim at: P2 clearly nearer (distance below 0.8 x P1's, squared 0.64), or
// P1 dead. The choice is kept per gun until the other player is clearly nearer.
static unsigned char mg_tgt[8];
static int mg_swapped;
RECOMP_HOOK("func_800D6044") void coop_mg_begin(unsigned int obj) {
    mg_swapped = 0;
    if (U8(PLAYERS) != 2 || !(U8(MODE) & 1) || recomp_get_config_u32("traps") != 0) return;
    if (U32(PLAYER_PTRS) != SLOT0_DATA || obj < 0x80000000u || obj >= 0x80800000u) return;
    unsigned int d = U32(obj + 0x2C);
    if (d < 0x80000000u || d >= 0x80800000u) return;
    int i, slot = -1;
    for (i = 0; i < 8; i++) if (mg_obj[i] == obj) { slot = i; break; }
    if (slot < 0) {
        for (i = 0; i < 8; i++) if (mg_obj[i] == 0) { slot = i; break; }
        if (slot < 0) slot = (int)((obj >> 4) & 7u);
        mg_obj[slot] = obj;
        mg_tgt[slot] = 0;
    }
    int hp0 = S8(SLOT0_DATA + 0x176B), hp1 = S8(SLOT1_DATA + 0x176B);
    float x0 = F32(d + 0) - F32(SLOT0_DATA + 0), z0 = F32(d + 8) - F32(SLOT0_DATA + 8);
    float x1 = F32(d + 0) - F32(SLOT1_DATA + 0), z1 = F32(d + 8) - F32(SLOT1_DATA + 8);
    float q0 = x0 * x0 + z0 * z0, q1 = x1 * x1 + z1 * z1;
    int t = mg_tgt[slot];
    if (hp1 <= 0) t = 0;
    else if (hp0 <= 0) t = 1;
    else if (t == 0 && q1 < q0 * 0.64f) t = 1;
    else if (t == 1 && q0 < q1 * 0.64f) t = 0;
    if (t != mg_tgt[slot] && recomp_get_config_u32("log") != 0) {
        unsigned int v[4] = { obj, (unsigned int)t, (unsigned int)(int)spawn_sqrt(q0), (unsigned int)(int)spawn_sqrt(q1) };
        lad_log("mg_target", v, 4);
    }
    mg_tgt[slot] = (unsigned char)t;
    if (!t) return;
    trap_swap_players();
    mg_swapped = 1;
}
RECOMP_HOOK_RETURN("func_800D6044") void coop_mg_end(void) {
    if (!mg_swapped) return;
    mg_swapped = 0;
    trap_swap_players();
}

// Passo 52 diagnosis: where the game keeps key items. Logs every byte that changes in the
// story state block 0x80158700..0x801587FF and the item array 0x80172500..0x801725EF
// ("coop: lad itembyte <addr> <old> <new>"), at most 6 changes per byte (counters go quiet).
#define ITEMDIAG_N 496
static unsigned char itemdiag_last[ITEMDIAG_N];
static unsigned char itemdiag_cnt[ITEMDIAG_N];
static unsigned int itemdiag_addr(int i) { return i < 256 ? 0x80158700u + (unsigned int)i : 0x80172500u + (unsigned int)(i - 256); }
static void itemdiag_frame(void) {
    if (recomp_get_config_u32("log") == 0 || !coop_story_session() || !(U16(STATE) & 1)) return;
    if (!itemdiag_ready) {
        for (int i = 0; i < ITEMDIAG_N; i++) { itemdiag_last[i] = U8(itemdiag_addr(i)); itemdiag_cnt[i] = 0; }
        itemdiag_ready = 1;
        return;
    }
    int lines = 0;
    for (int i = 0; i < ITEMDIAG_N; i++) {
        unsigned char v = U8(itemdiag_addr(i));
        if (v == itemdiag_last[i]) continue;
        if (itemdiag_cnt[i] < 6 && lines < 12) {
            itemdiag_cnt[i]++;
            lines++;
            unsigned int w[3] = { itemdiag_addr(i), itemdiag_last[i], v };
            lad_log("itembyte", w, 3);
        }
        itemdiag_last[i] = v;
    }
}

// Passo 56: P1 moved by a cutscene. The intro of the shotgun boss (script 9 after the load)
// puts P1 behind cover without func_80098F20, and P2 stayed at the start, out in the open. When
// P1 jumps more than 250 units in one frame (not a ladder/vault of P1, P2 alive and not already
// near), P2 is put 45 units behind P1 the way func_80098F20 does it: position, heading, zone
// marker (+0x8A), room from the marker (func_8003CB80) and the room state (func_8003C58C).
extern short func_8003CB80(int marker);
extern void func_8003C58C(unsigned int player);
static void p2_put_behind_p1(float back) {
    unsigned int a = SLOT0_DATA, b = SLOT1_DATA;
    float h = F32(a + 0x64);
    if (!(h == h) || h > 1.0e6f || h < -1.0e6f) { h = 0.0f; back = 0.0f; }
    F32(b + 0) = F32(a + 0) - spawn_sin(h) * back;
    F32(b + 4) = F32(a + 4);
    F32(b + 8) = F32(a + 8) - spawn_sin(h + 1.5707963f) * back;
    F32(b + 0x64) = h;
    U16(b + 0x8A) = U16(a + 0x8A);
    S16(b + 0x8E) = func_8003CB80(U16(b + 0x8A) & 0xFFF);
    func_8003C58C(b);
}
// Passo 56c: after a load the intro can move P1 in many small scripted steps (stage 5 log),
// which the jump watch does not see. For the first 900 frames of play after a load (scenes
// excluded), while P2 has not moved by itself since it was placed (within 40 units of where the
// load, a teleport or this check left it) and is more than 500 units from P1, P2 is put behind
// P1. A player who walked away on purpose is left alone.
static float settle_anchor[2];
static void settle_set_anchor(void) {
    settle_anchor[0] = F32(SLOT1_DATA + 0);
    settle_anchor[1] = F32(SLOT1_DATA + 8);
}
static void load_settle_frame(void) {
    unsigned int a = SLOT0_DATA, b = SLOT1_DATA;
    if (settle_frames < 0 || !coop_story_session() || U32(PLAYER_PTRS) != a) return;
    // v0.3.40: only "in play" counts. The scene flags (in_cutscene, CUTSCENE_FLAG/CAM, script
    // bit 4) stayed set after the stage 5 intro, so the check never ran (no log line at all).
    if (!(U16(STATE) & 1)) return;
    // v0.3.39: anchor on the first frame of play, not right after the load: the players are
    // only placed later in the load, so the early anchor saw P2 "move" and the check stopped.
    if (settle_frames == 0) { settle_set_anchor(); settle_frames = 1; return; }
    if (++settle_frames > 900) { settle_frames = -1; return; }
    if (settle_frames < 30) return;                  // let the intro finish placing P1
    if (recomp_get_config_u32("door_fix") != 0 || S8(a + 0x176B) <= 0 || S8(b + 0x176B) <= 0) return;
    float mx = F32(b + 0) - settle_anchor[0], mz = F32(b + 8) - settle_anchor[1];
    if (mx * mx + mz * mz > 40.0f * 40.0f) {                                // P2 is playing: stop
        settle_frames = -1;
        if (recomp_get_config_u32("log") != 0) {
            unsigned int v[2] = { U32(b + 0), U32(b + 8) };
            lad_log("settle_stop", v, 2);
        }
        return;
    }
    float px = F32(b + 0) - F32(a + 0), pz = F32(b + 8) - F32(a + 8);
    if (px * px + pz * pz <= 500.0f * 500.0f) return;
    p2_put_behind_p1(45.0f);
    settle_set_anchor();
    if (recomp_get_config_u32("log") != 0) {
        unsigned int v[4] = { U32(a + 0), U32(a + 8), U16(b + 0x8A), U16(b + 0x8E) };
        lad_log("settle_follow", v, 4);
    }
}

// Passo 61: a door into a new room can start a scene (stage 9 log: P2 went through the door from
// room 8 into room 9, script 0x5F / 0x60 ran a scene, and the door stayed shut after it: P1 kept
// walking into it, z stuck at 5765). The room change of each player is timed; when a scene
// (func_8009E1E0 mode 1/3) starts within 10 s of one player entering a new room, and after the
// scene the players are in different rooms with that player still in the new room, the other one
// is put behind them (marker and room taken from them, as func_80098F20 does).
static unsigned int join_room[2] = { 0xFFFFu, 0xFFFFu };
static unsigned int join_tick[2];
static int join_who = -1, join_wait;
static unsigned int join_moved_tick[2];
static void put_behind(unsigned int b, unsigned int a, float back) {
    float h = F32(a + 0x64);
    if (!(h == h) || h > 1.0e6f || h < -1.0e6f) { h = 0.0f; back = 0.0f; }
    F32(b + 0) = F32(a + 0) - spawn_sin(h) * back;
    F32(b + 4) = F32(a + 4);
    F32(b + 8) = F32(a + 8) - spawn_sin(h + 1.5707963f) * back;
    F32(b + 0x64) = h;
    U16(b + 0x8A) = U16(a + 0x8A);
    S16(b + 0x8E) = func_8003CB80(U16(b + 0x8A) & 0xFFF);
    func_8003C58C(b);
}
static void scene_join_start(void) {
    join_who = -1;
    int best = -1;
    for (int k = 0; k < 2; k++) {
        if (join_tick[k] == 0 || (unsigned int)(lm_tick - join_tick[k]) > 600u) continue;
        if (best < 0 || join_tick[k] > join_tick[best]) best = k;
    }
    if (best >= 0) { join_who = best; join_wait = 0; }
}
static void scene_join_frame(void) {
    for (int k = 0; k < 2; k++) {
        unsigned int d = k ? SLOT1_DATA : SLOT0_DATA;
        unsigned int r = U16(d + 0x8E);
        if (r != join_room[k]) {
            // v0.3.46: a room change right after this player was moved by the join (pushed back
            // through the door) does not count as entering a room.
            if (join_room[k] != 0xFFFFu &&
                (join_moved_tick[k] == 0 || (unsigned int)(lm_tick - join_moved_tick[k]) > 300u))
                join_tick[k] = lm_tick;
            join_room[k] = r;
        }
    }
    if (join_who < 0 || in_cutscene || !coop_story_session() || !(U16(STATE) & 1) ||
        U32(PLAYER_PTRS) != SLOT0_DATA) return;
    if (++join_wait < 10) return;
    int k = join_who;
    join_who = -1;
    if (recomp_get_config_u32("door_fix") != 0) return;
    unsigned int a = k ? SLOT1_DATA : SLOT0_DATA, b = k ? SLOT0_DATA : SLOT1_DATA;
    if (S16(a + 0x8E) == S16(b + 0x8E) || S8(a + 0x176B) <= 0 || S8(b + 0x176B) <= 0) return;
    unsigned int old = U16(b + 0x8E);
    // v0.3.48: 20 ahead (60 left P2 floating past a ledge in the stage 9 room).
    // v0.3.46: put ahead of the one who entered (further into the new room). Behind them is
    // towards the door: in the log P1 was put there, pushed back out through the door and a
    // second scene then pulled P2 out too.
    put_behind(b, a, -20.0f);
    join_room[k ^ 1] = U16(b + 0x8E);
    join_tick[k ^ 1] = 0;
    join_moved_tick[k ^ 1] = lm_tick ? lm_tick : 1u;
    if (recomp_get_config_u32("log") != 0) {
        unsigned int v[4] = { (unsigned int)k, old, U16(a + 0x8E), U16(b + 0x8E) };
        lad_log("scene_join", v, 4);
    }
}

// Passo 57: room desync. Stage 7 has a dark hole that P1 drops into (a vault/drop action, so
// only P1 is moved by the script) to reach another area (room 0). P2 walked in after P1 and
// stood right next to it, but kept room 5 (marker 5): its view drew only room 5, a black void
// with floating crates. When P2 stays within 60 units of P1 for 30 frames of play while their
// rooms differ, P2 gets P1's zone marker and the room the game derives from it
// (func_8003CB80 / func_8003C58C, as func_80098F20 does).
static int desync_frames;
static void room_desync_frame(void) {
    unsigned int a = SLOT0_DATA, b = SLOT1_DATA;
    if (!coop_story_session() || !(U16(STATE) & 1) || U32(PLAYER_PTRS) != a ||
        recomp_get_config_u32("door_fix") != 0 || S8(a + 0x176B) <= 0 || S8(b + 0x176B) <= 0) {
        desync_frames = 0;
        return;
    }
    short ra = S16(a + 0x8E), rb = S16(b + 0x8E);
    float px = F32(b + 0) - F32(a + 0), py = F32(b + 4) - F32(a + 4), pz = F32(b + 8) - F32(a + 8);
    if (ra == rb || ra < 0 || px * px + pz * pz > 60.0f * 60.0f || py > 80.0f || py < -80.0f) {
        desync_frames = 0;
        return;
    }
    if (++desync_frames < 30) return;
    desync_frames = 0;
    U16(b + 0x8A) = U16(a + 0x8A);
    S16(b + 0x8E) = func_8003CB80(U16(b + 0x8A) & 0xFFF);
    func_8003C58C(b);
    if (recomp_get_config_u32("log") != 0) {
        unsigned int v[4] = { (unsigned int)(unsigned short)rb, (unsigned int)(unsigned short)ra,
                              U16(b + 0x8A), (unsigned int)(unsigned short)S16(b + 0x8E) };
        lad_log("room_desync", v, 4);
    }
}

// Passo 58 diagnosis: stairs / invisible floor. Each player's index (+0x90), zone
// marker, height (Y), floor flags (+0xF4) and action, whenever the zone marker changes or the
// height jumps more than 40 in one frame. (+0x90 is the player index, not a floor triangle.)
static unsigned int floor_last_tri[2] = { 0xFFFFu, 0xFFFFu };
static float floor_last_y[2];
static void floor_diag_frame(void) {
    if (recomp_get_config_u32("log") == 0 || !coop_story_session() || !(U16(STATE) & 1)) return;
    for (int k = 0; k < 2; k++) {
        unsigned int d = k ? SLOT1_DATA : SLOT0_DATA, o = k ? SLOT0_DATA : SLOT1_DATA;
        unsigned int tri = U16(d + 0x8A);
        float y = F32(d + 4), dy = y - floor_last_y[k];
        int jump = (dy > 40.0f || dy < -40.0f);
        if (tri != floor_last_tri[k] || jump) {
            unsigned int v[9] = { (unsigned int)k | (jump ? 0x10u : 0u), U16(d + 0x90), tri, U32(d + 4), U32(d + 0xF4),
                                  U16(d + 0x16EC), U32(d + 0), U32(d + 8), U32(o + 4) };
            lad_log("floor", v, 9);
        }
        floor_last_tri[k] = tri;
        floor_last_y[k] = y;
    }
}

// Passo 62 diagnosis: after a bomb crate blows up in the stage 9 room, P1 slowly loses HP and
// P2 does not. On every HP drop of a player: player, old and new HP, action, room, zone marker
// and its flags (D_801D41D2 + marker * 60), and the characters within 800 of that player
// (object, update, draw, type, HP, distance), to find what deals the damage.
static int hpd_last[2] = { -1, -1 };
static void hp_drop_frame(void) {
    if (recomp_get_config_u32("log") == 0 || !coop_story_session() || !(U16(STATE) & 1)) {
        hpd_last[0] = hpd_last[1] = -1;
        return;
    }
    for (int k = 0; k < 2; k++) {
        unsigned int d = k ? SLOT1_DATA : SLOT0_DATA;
        int hp = S8(d + 0x176B);
        if (hpd_last[k] >= 0 && hp < hpd_last[k] && hp > 0) {
            unsigned int m = U16(d + 0x8A);
            unsigned int v[8] = { (unsigned int)k, (unsigned int)hpd_last[k], (unsigned int)hp, U16(d + 0x16EC),
                                  U16(d + 0x8E), m, (m < 512u) ? U16(0x801D41D2u + m * 60u) : 0xFFFFu,
                                  U32(d + 0xF4) };
            lad_log("hp_drop", v, 8);
            int n = 0;
            for (unsigned int o = U32(0x801788F0); o >= 0x80000000u && o < 0x80800000u && n < 6; o = U32(o + 0x1C)) {
                unsigned int e = U32(o + 0x2C);
                if (e < 0x80000000u || e >= 0x80800000u || e == SLOT0_DATA || e == SLOT1_DATA) continue;
                float x = F32(e + 0) - F32(d + 0), z = F32(e + 8) - F32(d + 8), q = x * x + z * z;
                if (q > 800.0f * 800.0f) continue;
                n++;
                unsigned int w[6] = { o, U32(o + 0x24), U32(o + 0x28), U16(e + 0x92),
                                      (unsigned int)(unsigned char)S8(e + 0x176B), (unsigned int)(int)spawn_sqrt(q) };
                lad_log("hp_near", w, 6);
            }
        }
        hpd_last[k] = hp;
    }
}

static void jump_follow_frame(void) {
    unsigned int a = SLOT0_DATA, b = SLOT1_DATA;
    // Outside play (cutscenes, menus) or with the players swapped, keep the last position:
    // the jump is seen on the first frame of play after the scene.
    if (!coop_story_session() || !(U16(STATE) & 1) || U32(PLAYER_PTRS) != a) return;
    float x = F32(a + 0), y = F32(a + 4), z = F32(a + 8);
    if (jf_valid && recomp_get_config_u32("door_fix") == 0 && S8(b + 0x176B) > 0 && S8(a + 0x176B) > 0 &&
        !((unsigned int)(lm_tick - lad_act_tick) < 400u && lad_act_player == 0)) {
        float dx = x - jf_last[0], dy = y - jf_last[1], dz = z - jf_last[2];
        float px = F32(b + 0) - x, pz = F32(b + 8) - z;
        if (dx * dx + dy * dy + dz * dz > 250.0f * 250.0f && px * px + pz * pz > 300.0f * 300.0f) {
            p2_put_behind_p1(45.0f);
            if (settle_frames > 0) settle_frames = 0;   // re-anchor on the next frame of play
            if (recomp_get_config_u32("log") != 0) {
                unsigned int v[4] = { U32(a + 0), U32(a + 8), U16(b + 0x8A), U16(b + 0x8E) };
                lad_log("jump_follow", v, 4);
            }
        }
    }
    jf_last[0] = x; jf_last[1] = y; jf_last[2] = z;
    jf_valid = 1;
}

static void ai_frame(void);
RECOMP_HOOK("func_80025F10") void coop_draw_begin(void) {
    drawing_split = 0;
    lm_tick++;
    ai_frame();
    lad_frame();
    itemdiag_frame();
    jump_follow_frame();
    load_settle_frame();
    room_desync_frame();
    scene_join_frame();
    hp_drop_frame();
    floor_diag_frame();
    fade_track_ladder();
    fade_draw_saved = -1;
    if (fade_split_active() && U8(FADE_LEVEL) > 0x80) {
        fade_draw_saved = U8(FADE_LEVEL);
        U8(FADE_LEVEL) = 0x80;                   // keep the 3D of both views
    }
    if (coop_story_session() && hp_keep_pending && (U16(STATE) & 1) && U8(PLAYERS) == 2) {
        hp_keep_pending = 0;
        signed char n0 = S8(SLOT0_DATA + 0x176B), n1 = S8(SLOT1_DATA + 0x176B);
        if (n0 == n1 && (n0 == hp_keep[0] || n0 == hp_keep[1])) {
            S8(SLOT0_DATA + 0x176B) = hp_keep[0];
            S8(SLOT1_DATA + 0x176B) = hp_keep[1];
            if (recomp_get_config_u32("log") != 0) {
                unsigned int v[3] = { (unsigned int)(unsigned char)n0, (unsigned int)(unsigned char)hp_keep[0],
                                      (unsigned int)(unsigned char)hp_keep[1] };
                lad_log("hp_keep", v, 3);
            }
        }
    }
    if (coop_story_session()) {
        // Spawn guard: if P2 appears on top of P1 right after a stage loads (something reset
        // its start point), move it to P1's position plus the start offset.
        if (spawn_guard > 0 && (U16(STATE) & 1)) {
            spawn_guard--;
            unsigned int a = SLOT0_DATA, b = SLOT1_DATA;
            float dx = F32(b + 0) - F32(a + 0);
            float dz = F32(b + 8) - F32(a + 8);
            float off = (spawn_guard_dist >= 0.0f) ? spawn_guard_dist : (float)recomp_get_config_double("spawn_distance");
            if (off > 0.0f && dx * dx + dz * dz < (off * 0.5f) * (off * 0.5f) &&
                U16(a + 0x8E) == U16(b + 0x8E)) {
                F32(b + 0) = F32(a + 0) + spawn_side_x * off;
                F32(b + 4) = F32(a + 4);
                F32(b + 8) = F32(a + 8) + spawn_side_z * off;
            }
        }
        if (coop_active()) {
            coop_friendly_fire();
            coop_pickup_scale(spawn_guard > 0);
        } else {
            coop_pickup_scale(1);
        }
        apply_aim_tuning();
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
    if (fade_draw_saved >= 0) {
        U8(FADE_LEVEL) = (unsigned char)fade_draw_saved;
        fade_draw_saved = -1;
    }
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
    if (!drawing_split) {   // runs inside the split draw, where MODE = 2 and coop_active() is false
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
    if (!coop_active() || obj == 0 || sky_draw_obj != 0) {
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
    // During the split draw MODE is 2 (not Story), so coop_active() is false here: that kept
    // this whole per-viewport sky fix from ever running. drawing_split is the right test.
    if (!drawing_split || obj == 0) {
        return;
    }
    int vp = U8(0x8016A40F) ? 1 : 0;
    short room = S16(ROOM_STATE + vp * 4);
    short subroom = S16(ROOM_STATE + vp * 4 + 2);
    int out_room = get_outdoor_room(room, subroom);
    unsigned int p_vp = U32(PLAYER_PTRS + vp * 4);
    if (p_vp != 0) {
        sky_draw_obj = obj;
        sky_draw_vp = vp;
        sky_draw_saved_parent = U32(obj + 0x20);
        sky_draw_saved_p0 = U32(PLAYER_PTRS);
        sky_draw_player = p_vp;
        sky_draw_saved_room = S16(p_vp + 0x8E);
        U32(obj + 0x20) = U32(p_vp + 0x1740);
        U32(PLAYER_PTRS) = p_vp;
        S16(p_vp + 0x8E) = (out_room >= 0) ? (short)out_room : (room >= 0 ? room : 0);
        if (out_room >= 0) {
            unsigned int sky_data = U32(obj + 0x2C);
            if (sky_data != 0) {
                float saved_u = F32(sky_data + 0x24);
                float saved_v = F32(sky_data + 0x28);
                func_800C4EE0(obj);
                F32(sky_data + 0x24) = saved_u;
                F32(sky_data + 0x28) = saved_v;
                // Centre the dome on THIS viewport's player directly (the update above goes
                // through obj + 0x20 -> player + 0x1740, which can still lead to P1), with the
                // height rule of func_800C4EE0: room height + 100, at least 500.
                F32(sky_data + 0) = F32(p_vp + 0);
                F32(sky_data + 8) = F32(p_vp + 8);
                int h = S16(0x801D3760 + (unsigned int)out_room * 0x58);
                F32(sky_data + 4) = (h > 500) ? (float)h + 100.0f : 500.0f;
                func_80028740(sky_data);
                if (vp == 1 && recomp_get_config_u32("log") != 0) {
                    static unsigned int sky_last_log;
                    unsigned int now = winback_mod_time_us();
                    if ((unsigned int)(now - sky_last_log) > 2000000u) {
                        sky_last_log = now;
                        unsigned int po = U32(p_vp + 0x1740);
                        int ok = (po >= 0x80000000u && po < 0x80800000u && U32(po + 0x2C) == p_vp);
                        winback_mod_log(ok ? "coop: sky P2 parent ok" : "coop: sky P2 parent points elsewhere (fixed)");
                    }
                }
            }
        }
    }
}

RECOMP_HOOK_RETURN("func_800C4FB0") void coop_sky_draw_end(void) {
    if (sky_draw_obj != 0) {
        unsigned int dl = U32(0x801D2C80);
        unsigned int sky_data = U32(sky_draw_obj + 0x2C);
        if (dl != 0 && sky_data != 0 && U32(dl - 16) == 0xDA380003u && U32(dl - 12) == sky_data + 0x60) {
            unsigned int dst_mtx = sky_draw_vp ? SKY_MTX_P2 : SKY_MTX_P1;
            for (int i = 0; i < 16; i++) {
                U32(dst_mtx + i * 4) = U32(sky_data + 0x60 + i * 4);
            }
            U32(dl - 12) = dst_mtx;
        }
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
#include "enemy_ai.h"

// Bosses use their own update, func_800AD9F0 (set by func_8002C86C for characters with +0x92 == 2):
// it runs the boss logic (func_800ADCF8) only while D_801390C0[0] (player 1) is alive. The same
// target swap and visibility rule as regular enemies are applied to it (boss = 1), without the
// smarter-enemy AI, whose state numbers don't apply to bosses.
static void enemy_begin_common(unsigned int obj, int boss);

// Passo 53: bosses split their attention. Before, a boss went for P2 only when P2 was much
// nearer (distance < 0.8 x P1's), so with both at a similar range it stayed on P1. Now each boss
// keeps a target and switches to the other player when:
//  - the current target is dead;
//  - the other player is much nearer (< 0.8 x the distance), after 1.5 s on the target;
//  - the other player hit it 3 times since the last switch (revenge), after 2 s;
//  - 8 s passed on the same target and the other one is not much farther (< 1.5 x).
// Hits are counted in coop_boss_hit_begin (boss_ai.h) from the shooter's weapon task.
static unsigned int boss_t_obj;
static int boss_t_tgt;
static unsigned int boss_t_since;
static int boss_hits_by[2];
static int boss_pick_target(unsigned int e, float d0, float d1) {
    if (boss_t_obj != e) {                       // a new boss: start on the nearer one
        boss_t_obj = e;
        boss_t_tgt = (d1 < d0) ? 1 : 0;
        boss_t_since = lm_tick;
        boss_hits_by[0] = boss_hits_by[1] = 0;
    }
    int cur = boss_t_tgt, oth = 1 - cur;
    float dc = cur ? d1 : d0, dn = cur ? d0 : d1;    // squared distances
    unsigned int held = lm_tick - boss_t_since;      // drawn frames on the target
    int sw = 0;
    if (S8((cur ? SLOT1_DATA : SLOT0_DATA) + 0x176B) <= 0) sw = 1;
    else if (held > 45u && dn < dc * 0.64f) sw = 1;
    else if (held > 60u && boss_hits_by[oth] >= 3) sw = 1;
    else if (held > 240u && dn < dc * 2.25f) sw = 1;
    if (sw && S8((oth ? SLOT1_DATA : SLOT0_DATA) + 0x176B) > 0) {
        boss_t_tgt = oth;
        boss_t_since = lm_tick;
        boss_hits_by[0] = boss_hits_by[1] = 0;
        if (recomp_get_config_u32("log") != 0) {
            unsigned int v[3] = { e, (unsigned int)oth, held };
            lad_log("boss_target", v, 3);
        }
    }
    return boss_t_tgt;
}

RECOMP_HOOK("func_800337A8") void coop_enemy_begin(unsigned int obj) {
    enemy_begin_common(obj, 0);
}

RECOMP_HOOK("func_800AD9F0") void coop_boss_begin(unsigned int obj) {
    enemy_begin_common(obj, 1);
}

static void enemy_begin_common(unsigned int obj, int boss) {
    swapped_players = 0;
    cur_enemy = 0;
    ai_cur_idx = -1;
    if (!coop_active() || recomp_get_config_u32("enemies") != 0) {
        return;
    }
    unsigned int e = U32(obj + 0x2C);
    cur_enemy = e;
    if (e != 0 && shot_diag_lines < 6 && (unsigned int)(lm_tick - shot_diag_tick) < 3u &&
        recomp_get_config_u32("log") != 0) {
        float qx = F32(e + 0) - F32(SLOT1_DATA + 0), qz = F32(e + 8) - F32(SLOT1_DATA + 8);
        if (qx * qx + qz * qz < 2500.0f * 2500.0f) {
            shot_diag_lines++;
            unsigned int v[10] = { e, U16(e + 0x8E), U16(e + 0x8A), (unsigned int)(unsigned char)S8(e + 0x176B),
                                   U16(SLOT1_DATA + 0x8E), U16(SLOT1_DATA + 0x8A), U32(ROOM_STATE + 4),
                                   U16(SLOT0_DATA + 0x8E), U32(ROOM_STATE + 0), U32(PLAYER_PTRS + 4) };
            lad_log("p2shot_enemy", v, 10);
        }
    }
    unsigned int p0 = U32(PLAYER_PTRS);
    unsigned int p1 = U32(PLAYER_PTRS + 4);
    if (e == 0 || p0 == 0 || p1 == 0) {
        return;
    }
    // Smarter enemies (enemy_ai.h): slot state, senses, and whether the game looks this frame.
    int ai_lv = boss ? 0 : ai_level();
    if (!boss) ai_tune_senses(e, ai_lv);   // also puts Original values back after switching the option
    if (ai_lv != 0) {
        ai_cur_idx = ai_slot_index(e);
        if (ai_cur_idx >= 0) {
            ai_slot_check(ai_cur_idx, e);
            unsigned int id7 = U8(e + 0x16F4) % 7u, clk7 = U32(GAME_CLOCK) % 7u;   // constant: no trap
            ai_natural = (U32(e + 0xFC) & 0x10000) || id7 == clk7;
        }
    }
    int hp0 = S8(p0 + 0x176B);
    int hp1 = S8(p1 + 0x176B);
    if (hp0 <= 0) {
        if (hp1 <= 0) {
            return;
        }
        if (ai_cur_idx >= 0) ai_target[ai_cur_idx] = 1;
    } else {
        if (hp1 <= 0) {
            if (ai_cur_idx >= 0) ai_target[ai_cur_idx] = 0;
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

        int want_p1 = boss ? boss_pick_target(e, d0, d1) : (d1 < d0 * 0.64f);
        if (ai_lv != 0 && ai_cur_idx >= 0) {
            // Smart / Elite: keep a target, take revenge on whoever shoots them.
            want_p1 = ai_pick_target(ai_cur_idx, e, p0, p1, d0, d1, ai_lv);
        } else if (!boss && coop_difficulty() != 0) {
            // Hard / Brutal: enemies split their attention between the two players. Each enemy has
            // a preferred player; it goes for that one when they are in its room, otherwise the nearer.
            int pref = (int)(((e >> 4) ^ (e >> 9)) & 1);
            unsigned int pp = pref ? p1 : p0;
            if (U16(pp + 0x8E) == U16(e + 0x8E)) {
                want_p1 = pref;
            }
        }
        if (!want_p1) {
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

static void enemy_end_common(void);

RECOMP_HOOK_RETURN("func_800337A8") void coop_enemy_end(void) {
    enemy_end_common();
}

RECOMP_HOOK_RETURN("func_800AD9F0") void coop_boss_end(void) {
    enemy_end_common();
}

static void enemy_end_common(void) {
    // Smarter enemies: extra looks, quicker reactions, squad alert. Runs before the swap back,
    // while D_801390C0[0] is still the player this enemy fights.
    if (cur_enemy != 0 && ai_cur_idx >= 0) {
        ai_after_update(cur_enemy, ai_level());
    }
    ai_cur_idx = -1;
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
    // Remember each player's aim: the shot code (func_800B8FB0) builds the bullet ray from the
    // weapon object's data (obj + 0x2C): pitch at +0x60, yaw at +0x64,
    // direction = (sin(yaw) * cos(pitch), -sin(pitch), cos(yaw) * cos(pitch)).
    bullet_inv_entered = 0;
    ai_bullet_owner = -1;
    if (obj >= 0x80000000u && obj < 0x80800000u) {
        unsigned int owner_obj0 = U32(obj + 0x20);
        unsigned int bd = U32(obj + 0x2C);
        if (owner_obj0 >= 0x80000000u && owner_obj0 < 0x80800000u && bd >= 0x80000000u && bd < 0x80800000u) {
            unsigned int own = U32(owner_obj0 + 0x2C);
            if (own == SLOT0_DATA || own == SLOT1_DATA) {
                int k = (own == SLOT1_DATA) ? 1 : 0;
                aim_yaw[k] = F32(bd + 0x64);
                aim_ok[k] = 1;
                bullet_inv_entered = inv_enter(k);
                ai_bullet_owner = k;
            }
        }
    }
    // The old friendly-fire trick (Versus MODE during the shot) did not hit the partner:
    // friendly fire is now done by coop_friendly_fire() instead.
    if (1) {
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
    inv_leave(bullet_inv_entered);
    ai_bullet_owner = -1;
    bullet_inv_entered = 0;
    if (bullet_split) {
        U8(MODE) = 1;
        bullet_split = 0;
    }
}

// 7b. Apply Story Mode direct damage during friendly-fire bullet hits (skip Versus HUD score gate).
static unsigned int dmg_target;
static int dmg_hp_before;
#define DMG_REM_N 16
static unsigned int dmg_rem_ptr[DMG_REM_N];
static int dmg_rem_val[DMG_REM_N];

// Integer percent scaling that carries the remainder per target, so small hits add up.
static int dmg_scale(unsigned int target, int loss, int pct) {
    int k = 0;
    for (; k < DMG_REM_N; k++) {
        if (dmg_rem_ptr[k] == target) break;
    }
    if (k == DMG_REM_N) {
        for (k = 0; k < DMG_REM_N && dmg_rem_ptr[k] != 0; k++) {}
        if (k == DMG_REM_N) k = (int)((target >> 4) & (DMG_REM_N - 1));
        dmg_rem_ptr[k] = target;
        dmg_rem_val[k] = 0;
    }
    int v = loss * pct + dmg_rem_val[k];
    int out = v / 100;               // constant divide: no trap instruction
    dmg_rem_val[k] = v - out * 100;
    return out;
}

RECOMP_HOOK("func_80057BB4") void coop_damage_begin(int dmg, unsigned int target, int attacker_idx) {
    (void)dmg;
    if (!ff_applying && target >= 0x80000000u && target < 0x80800000u) {
        ai_on_enemy_hit(target, attacker_idx);
    }
    dmg_target = 0;
    if (coop_story_session() && !ff_applying && target >= 0x80000000u && target < 0x80800000u &&
        coop_difficulty() != 0) {
        dmg_target = target;
        dmg_hp_before = S8(target + 0x176B);
    }
    damage_story_mode = 0;
    if (bullet_split && U8(MODE) == 2) {
        U8(MODE) = 1;
        damage_story_mode = 1;
    }
}

RECOMP_HOOK_RETURN("func_80057BB4") void coop_damage_end(void) {
    if (dmg_target != 0) {
        unsigned int t = dmg_target;
        dmg_target = 0;
        int after = S8(t + 0x176B);
        int loss = dmg_hp_before - after;
        if (loss > 0) {
            int is_player = (t == SLOT0_DATA || t == SLOT1_DATA);
            int d = coop_difficulty();
            int pct = is_player ? diff_enemy_dealt[d] : diff_enemy_taken[d];
            int nl = dmg_scale(t, loss, pct);
            int hp = dmg_hp_before - nl;
            if (hp < 0) hp = 0;
            if (hp > 127) hp = 127;
            S8(t + 0x176B) = (signed char)hp;
        }
    }
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

// Passo 59: player-mounted machine gun (stage 8). The player uses it with action 25; the gun's
// update, func_80036830, reads the buttons of its user by the player index (+0x90), but the
// stick only from player 1's block (D_801659C0, 0x18 bytes per player). While P2 is the user,
// P2's stick block is copied over player 1's for the call and restored after.
static unsigned char mgun_saved[0x18];
static int mgun_swapped;
RECOMP_HOOK("func_80036830") void coop_mgun_begin(unsigned int obj) {
    mgun_swapped = 0;
    if (!coop_story_session() || obj == 0) return;
    unsigned int parent = U32(obj + 0x20);
    if (parent < 0x80000000u || parent >= 0x80800000u) return;
    unsigned int p = U32(parent + 0x2C);
    if (p != SLOT1_DATA || S16(p + 0x90) != 1 || U16(p + 0x16EC) != 0x19) return;
    for (int i = 0; i < 0x18; i++) {
        mgun_saved[i] = U8(0x801659C0u + (unsigned int)i);
        U8(0x801659C0u + (unsigned int)i) = U8(0x801659D8u + (unsigned int)i);
    }
    mgun_swapped = 1;
}

RECOMP_HOOK_RETURN("func_80036830") void coop_mgun_end(void) {
    if (!mgun_swapped) return;
    for (int i = 0; i < 0x18; i++) U8(0x801659C0u + (unsigned int)i) = mgun_saved[i];
    mgun_swapped = 0;
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

static int item_inv_entered[8];
static void item_pick_begin(unsigned int obj) {
    if (coop_active() && obj != 0 && should_p2_pick_item(U32(obj + 0x2C))) {
        if (item_swap_depth == 0) {
            swap_player_ptrs_and_rooms();
        }
        if (item_swap_depth < 8) item_inv_entered[item_swap_depth] = inv_enter(1);  // P2 gets the item
        item_swap_depth++;
    }
}

static void item_pick_end(void) {
    if (item_swap_depth > 0) {
        item_swap_depth--;
        if (item_swap_depth < 8) inv_leave(item_inv_entered[item_swap_depth]);
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

// Passo 60: there is one ladder event flag. If both players reach the ladder trigger frame at
// once, the second request is lost and that player hangs in action 23 (on a ladder, past the
// trigger frame 18, off the ground) waiting for a script that never comes. After 20 such frames
// with no event pending and no cutscene, the request is raised again for that player.
static void ladder_requeue_frame(void) {
    if (in_cutscene || S16(0x8010E0F8) != 0 || !(U16(STATE) & 1)) {
        lad_stuck[0] = lad_stuck[1] = 0;
        return;
    }
    for (int k = 0; k < 2; k++) {
        unsigned int d = k ? SLOT1_DATA : SLOT0_DATA;
        float fr = F32(d + 0x16C8);
        if (U16(d + 0x16EC) == 23 && fr >= 18.5f && (U32(d + 0xF4) & 0xCu) == 0 && S8(d + 0x176B) > 0) {
            if (++lad_stuck[k] >= 20) {
                lad_stuck[0] = lad_stuck[1] = 0;
                U16(0x8010E0F8) = 1;
                flag15_owner = k;
                if (recomp_get_config_u32("log") != 0) {
                    unsigned int v[2] = { (unsigned int)k, U32(d + 0x16C8) };
                    lad_log("ladder_requeue", v, 2);
                }
                return;
            }
        } else {
            lad_stuck[k] = 0;
        }
    }
}

RECOMP_HOOK("func_800944E0") void coop_event_mgr_begin(void) {
    evt_mgr_saved = 0;
    if (!coop_story_session()) {
        return;
    }
    lm_start_filter();
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
    } else if (!in_cutscene && S16(0x8010E0F8) == 1) {
        lad_event("flag15", 1, 0);
        lad_cond_lines = 0;          // log the conditions of this attempt
        if (recomp_get_config_u32("log") != 0) lad_markers("markers");
        // Event 15 (0x8010E0F8 == 1) is queued by Action 23 (ladder) and Action 9 (vault)
        // when the entry animation reaches frame 18.0f. Neither action pushes to func_8004DE48,
        // so detect which player is currently on the ladder/vault before func_8009C2E4(15, 0) runs.
        unsigned short act0 = U16(SLOT0_DATA + 0x16EC);
        unsigned short act1 = U16(SLOT1_DATA + 0x16EC);
        if (flag15_owner >= 0) {
            // Passo 60: both can be on ladders at once; the one who raised the event goes.
            trigger_player = flag15_owner;
            trigger_ttl = flag15_owner ? 180 : 0;
        } else if (act1 == 23 || act1 == 9) {
            trigger_player = 1;
            trigger_ttl = 180;
        } else if (act0 == 23 || act0 == 9) {
            trigger_player = 0;
            trigger_ttl = 0;
        }
    }
    if (!in_cutscene && S16(0x8010E0F8) != 1) flag15_owner = -1;
    ladder_requeue_frame();
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
    c2e4_swapped = 0;
    lad_event("c2e4", type, val);
    if (!coop_story_session() || in_cutscene) {
        return;
    }
    unsigned short act0 = U16(SLOT0_DATA + 0x16EC);
    unsigned short act1 = U16(SLOT1_DATA + 0x16EC);
    if (act1 == 23 || act1 == 25 || act1 == 12 || act1 == 13 || act1 == 9) {
        trigger_player = 1;
        trigger_ttl = 180;
    } else if (act0 == 23 || act0 == 25 || act0 == 12 || act0 == 13 || act0 == 9) {
        trigger_player = 0;
        trigger_ttl = 0;
    }
    // Event 30 (0x1E) is the ladder check func_8009C860 makes when Action is pressed at a ladder
    // (0x800A9C18 in func_800A8ED0), before the player is put in action 23: it is raised from that
    // player's own update, so the player being updated is the one at the ladder. Without this the
    // check ran for P1 on P2's first try, the ladder script (0x35) didn't start and the climb was
    // cancelled; the second try only worked because trigger_player was still 1 from the first.
    if ((type & 0xFF) == 15 && flag15_owner >= 0 && S16(0x8010E0F8) == 1) {
        trigger_player = flag15_owner;
        trigger_ttl = flag15_owner ? 180 : 0;
    }
    if ((type & 0xFF) == 0x1E && updating_player >= 0) {
        trigger_player = updating_player;
        trigger_ttl = updating_player ? 180 : 0;
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

RECOMP_HOOK("func_8009BE18") void coop_script_run_begin(int script, int mode) {
    be18_swapped = 0;
    lad_event("script", script, mode);
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
    lad_event("e1e0", mode, 0);
    if (!coop_story_session()) {
        return;
    }
    if (mode == 1 || mode == 3) {
        if (!in_cutscene) scene_join_start();
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

static int tele_pos_id, tele_follow_nested;
extern void func_80098F20(int pos_id, int player_idx);
RECOMP_HOOK("func_80098F20") void coop_cutscene_teleport_begin(int pos_id, int player_idx) {
    if (!tele_follow_nested) tele_pos_id = pos_id;
    if (recomp_get_config_u32("log") != 0 && coop_story_session()) {
        unsigned int v[4] = { (unsigned int)pos_id, (unsigned int)player_idx, (unsigned int)trigger_player,
                              (U32(PLAYER_PTRS) == SLOT1_DATA) ? 1u : 0u };
        lad_log("teleport", v, 4);
    }
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
        return;
    }
    // Passo 55: a script teleport of P1 that is not a ladder/vault (boss intros, scripted
    // scenes, doors inside a map) left P2 where it was: at the machine-gun boss P2 stayed out in
    // the open near the boss while P1 was put behind cover. The same teleport is now run for
    // player index 1 (the game's own code: position, heading, zone marker, room), and P2 is put
    // 45 units behind P1. Ladders keep moving only the climber.
    if (tele_follow_nested || tele_idx != 0 || U32(PLAYER_PTRS) != SLOT0_DATA || U8(PLAYERS) != 2) return;
    if (recomp_get_config_u32("door_fix") != 0) return;
    if ((unsigned int)(lm_tick - lad_act_tick) < 400u && lad_act_player == 0) return;   // P1's ladder/vault
    if (S8(SLOT1_DATA + 0x176B) <= 0) return;
    // v0.3.40: off. In the stage 5 intro P1 is teleported to point 4 and then walked back next
    // to its start; following the teleport left P2 alone at point 4. The settle check
    // (load_settle_frame) brings P2 to wherever P1 ends up instead.
    if (1) return;
    tele_follow_nested = 1;
    func_80098F20(tele_pos_id, 1);
    tele_follow_nested = 0;
    float h = F32(SLOT0_DATA + 0x64);
    F32(SLOT1_DATA + 0) = F32(SLOT0_DATA + 0) - spawn_sin(h) * 45.0f;
    F32(SLOT1_DATA + 8) = F32(SLOT0_DATA + 8) - spawn_sin(h + 1.5707963f) * 45.0f;
    if (recomp_get_config_u32("log") != 0) {
        unsigned int v[3] = { (unsigned int)tele_pos_id, U32(SLOT1_DATA + 0), U32(SLOT1_DATA + 8) };
        lad_log("tele_follow", v, 3);
    }
    settle_frames = 0;                            // re-anchor P2 where the teleport put it
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

// 12. Partner radar: a small round radar in the lower right of each view that shows where the
// other player is. Drawn as one more 2D sprite right after the life bar (see radar.h).
#include "radar.h"

#define RADAR_SPRITE_P1 0x8050B800u
#define RADAR_SPRITE_P2 0x8050B840u
#define RADAR_TEX_P1    0x8050C000u   // 64 * 64 * 2 = 0x2000 bytes each
#define RADAR_TEX_P2    0x8050E000u
// Radar offset from the (x, y) the game gives the life bar: lower right corner of each view.
#define RADAR_DX        52
#define RADAR_DY        (-26)

static int radar_init_done[2] = { 0, 0 };
static unsigned int last_radar_log;

// Explored-area map: one byte per cell, shared by both players, in the Expansion Pak range.
#define RADAR_GRID      0x80530000u   // 256 * 256 bytes
static float map_ox, map_oz;          // world position (map axes) at the centre of the grid
static int map_ready;

// Camera forward direction per viewport, smoothed so the radar never snaps.
static float cam_fx[2], cam_fy[2];
static int cam_ok[2];

static void radar_map_clear(float cx, float cz) {
    for (unsigned int i = 0; i < (unsigned int)(RADAR_GRID_N * RADAR_GRID_N); i += 4) {
        U32(RADAR_GRID + i) = 0;
    }
    map_ox = cx;
    map_oz = cz;
    map_ready = 1;
}

static void radar_map_stamp(float mx, float mz) {
    float fx = (mx - map_ox) * (1.0f / RADAR_CELL) + (RADAR_GRID_N / 2) + 0.5f;
    float fz = (mz - map_oz) * (1.0f / RADAR_CELL) + (RADAR_GRID_N / 2) + 0.5f;
    if (fx < 2.0f || fz < 2.0f || fx > RADAR_GRID_N - 3 || fz > RADAR_GRID_N - 3) {
        return;
    }
    int ix = (int)fx, iz = (int)fz;
    for (int dz = -1; dz <= 1; dz++) {
        for (int dx = -1; dx <= 1; dx++) {
            U8(RADAR_GRID + (iz + dz) * RADAR_GRID_N + ix + dx) = 1;
        }
    }
}

// The character's facing is learned from how it walks: the yaw at +0x64 is compared with the
// walking direction under 8 possible conventions (4 rotations x 2 turn directions).
static float cal_score[8];
static int cal_n;
static int cal_best;
static float cal_last_x[2], cal_last_z[2];
static int cal_last_ok[2];

static void radar_calibrate(int slot, float mx, float mz, float heading) {
    if (cal_last_ok[slot]) {
        float vx = mx - cal_last_x[slot];
        float vz = mz - cal_last_z[slot];
        float d2 = vx * vx + vz * vz;
        if (d2 > 1.0f && d2 < 40000.0f) {
            float inv = 1.0f / radar_sqrt(d2);
            vx *= inv;
            vz *= inv;
            for (int c = 0; c < 8; c++) {
                float hh = ((c & 1) ? -heading : heading) + (float)(c >> 1) * 1.57079633f;
                cal_score[c] += vx * radar_sin(hh) + vz * radar_cos(hh);
            }
            cal_n++;
            if (cal_n >= 30) {
                int best = 0;
                for (int c = 1; c < 8; c++) {
                    if (cal_score[c] > cal_score[best]) {
                        best = c;
                    }
                }
                cal_best = best;
            }
            if (cal_n > 1500) {
                for (int c = 0; c < 8; c++) {
                    cal_score[c] *= 0.5f;
                }
                cal_n = 750;
            }
        }
    }
    cal_last_x[slot] = mx;
    cal_last_z[slot] = mz;
    cal_last_ok[slot] = 1;
}

static void radar_log(const RadarIn *in) {
    if (recomp_get_config_u32("log") == 0) {
        return;
    }
    unsigned int now = winback_mod_time_us();
    if ((unsigned int)(now - last_radar_log) < 1000000u) {
        return;
    }
    last_radar_log = now;
    union { float f; unsigned int u; } h, dx, dz;
    h.f = in->heading;
    dx.f = in->dx;
    dz.f = in->dz;
    static const char tmpl[] = "coop: radar heading 0x........ dx 0x........ dz 0x........ (float bits)";
    static char line[sizeof(tmpl)];
    unsigned int i;
    for (i = 0; i < sizeof(tmpl); i++) {
        line[i] = tmpl[i];
    }
    put_hex(line + 22, h.u, 8);
    put_hex(line + 36, dx.u, 8);
    put_hex(line + 50, dz.u, 8);
    winback_mod_log(line);
}

static void radar_draw(int player_idx, unsigned int player, int char_idx, int x, int y) {
    if (player_idx > 1 || U8(PLAYERS) != 2 || recomp_get_config_u32("radar") != 0) {
        return;
    }
    unsigned int other = U32(PLAYER_PTRS + (player_idx ^ 1) * 4);
    if (other == 0 || other == player) {
        return;
    }

    static const float ranges[3] = { 600.0f, 1200.0f, 2400.0f };
    unsigned int range_opt = recomp_get_config_u32("radar_range");
    if (range_opt > 2) {
        range_opt = 1;
    }

    // Map axes: x = world X, z = -world Z (the game is right handed, Y is up). The "left/right fix"
    // option flips the Z sign if this guess is wrong.
    float zs = (recomp_get_config_u32("radar_mirror") != 0) ? 1.0f : -1.0f;
    float mx = F32(player + 0);
    float mz = zs * F32(player + 8);
    float ox = F32(other + 0);
    float oz = zs * F32(other + 8);
    float heading = F32(player + 0x64);

    // Explored-area map (reset on each stage load or after a big jump).
    unsigned int map_mode = recomp_get_config_u32("radar_map");   // 0 level map, 1 full level map, 2 trail, 3 off
    int map_img = -1;
    unsigned int room_mask = 0xFFFFFFFFu;
    int cur_room = (int)U16(player + 0x8E);
    unsigned int stage = U8(0x8016A4EB);
    if (map_mode <= 1 && stage < 0x52 && map_stage[stage] < MAP_IMAGES) {
        map_img = map_stage[stage];
        if (map_mode == 0) {
            // Same rule as the game's map screen: one bit per room in a per-image word at 0x80158764.
            // The game only marks rooms P1 visits, so rooms where either player stands count too.
            unsigned int mi = (unsigned int)map_img;
            if (mi == 22) mi = 1;
            else if (mi == 23) mi = 6;
            room_mask = U32(0x80158764u + mi * 4u);
            unsigned int r0 = U16(player + 0x8E), r1 = U16(other + 0x8E);
            if (r0 < 32) room_mask |= 1u << r0;
            if (r1 < 32) room_mask |= 1u << r1;
        }
    }
    int use_map = (map_mode == 2) || (map_mode <= 1 && map_img < 0);
    if (use_map) {
        float jx = mx - map_ox, jz = mz - map_oz;
        if (radar_map_reset || !map_ready || jx > 6000.0f || jx < -6000.0f || jz > 6000.0f || jz < -6000.0f) {
            radar_map_clear(mx, mz);
            radar_map_reset = 0;
        }
        radar_map_stamp(mx, mz);
        radar_map_stamp(ox, oz);
    }

    // The radar turns with the CAMERA of this viewport (what you see as "up" on screen), not with the
    // character's own yaw, which snaps to the stick direction. Eye / look-at tables are per player.
    int slot = player_idx & 1;
    float cex = F32(0x8015D6D8 + slot * 4), cez = F32(0x80178FB0 + slot * 4);
    float clx = F32(0x80158748 + slot * 4), clz = F32(0x801C3808 + slot * 4);
    float vfx = clx - cex;
    float vfy = zs * (clz - cez);
    float vl2 = vfx * vfx + vfy * vfy;
    int has_fwd = 0;
    if (vl2 > 1.0f && vl2 < 1.0e10f) {
        float inv = 1.0f / radar_sqrt(vl2);
        vfx *= inv;
        vfy *= inv;
        if (!cam_ok[slot]) {
            cam_fx[slot] = vfx;
            cam_fy[slot] = vfy;
            cam_ok[slot] = 1;
        } else {
            cam_fx[slot] += (vfx - cam_fx[slot]) * 0.3f;
            cam_fy[slot] += (vfy - cam_fy[slot]) * 0.3f;
            float l2 = cam_fx[slot] * cam_fx[slot] + cam_fy[slot] * cam_fy[slot];
            if (l2 > 0.0001f) {
                float k = 1.0f / radar_sqrt(l2);
                cam_fx[slot] *= k;
                cam_fy[slot] *= k;
            }
        }
        has_fwd = 1;
    }

    radar_calibrate(slot, mx, mz, heading);
    int has_face = 0;
    float face_x = 0.0f, face_y = 1.0f;
    if (cal_n >= 30) {
        float hh = ((cal_best & 1) ? -heading : heading) + (float)(cal_best >> 1) * 1.57079633f;
        face_x = radar_sin(hh);
        face_y = radar_cos(hh);
        has_face = 1;
    }

    RadarIn in;
    in.dx = ox - mx;
    in.dz = oz - mz;
    in.heading = heading;
    in.range = ranges[range_opt];
    in.viewer = char_idx;
    in.other_room = U16(player + 0x8E) != U16(other + 0x8E);
    in.other_dead = S8(other + 0x176B) <= 0;
    in.north_up = recomp_get_config_u32("radar_mode") != 0;
    in.rot90 = (int)(recomp_get_config_u32("radar_rot") & 3);
    in.mirror = 0;
    in.has_face = has_face;
    in.face_x = face_x;
    in.face_y = face_y;
    in.has_fwd = has_fwd;
    in.fwd_x = cam_fx[slot];
    in.fwd_y = cam_fy[slot];
    in.use_map = use_map;
    in.map_img = map_img;
    in.room_mask = room_mask;
    in.cur_room = cur_room;
    in.wx = mx;
    in.wz = mz;
    in.zsign = zs;
    in.grid = (volatile const unsigned char *)RADAR_GRID;
    in.gx = mx - map_ox;
    in.gz = mz - map_oz;

    if (player_idx == 0) {
        radar_log(&in);
    }

    unsigned int spr = (char_idx == 1) ? RADAR_SPRITE_P2 : RADAR_SPRITE_P1;
    unsigned int tex = (char_idx == 1) ? RADAR_TEX_P2 : RADAR_TEX_P1;
    if (!radar_init_done[char_idx] || U16(spr + 0x02) != RADAR_N) {
        U8(spr + 0x00) = 0;               // G_IM_FMT_RGBA
        U8(spr + 0x01) = 2;               // G_IM_SIZ_16b
        U16(spr + 0x02) = RADAR_N;
        U16(spr + 0x04) = RADAR_N;
        U16(spr + 0x06) = 0;
        U32(spr + 0x08) = tex;
        U32(spr + 0x0C) = 0;              // no TLUT
        U16(spr + 0x12) = 0;
        U16(spr + 0x14) = 0;
        U16(spr + 0x16) = 0;
        U32(spr + 0x18) = 0x3F000000u;    // 0.5f: 64 texels -> 32 pixels on the 320x240 screen
        U32(spr + 0x1C) = 0x3F000000u;
        U8(spr + 0x20) = 255;
        U8(spr + 0x21) = 255;
        U8(spr + 0x22) = 255;
        U8(spr + 0x23) = 255;
        U32(spr + 0x24) = 0;
        radar_init_done[char_idx] = 1;
    }
    U16(spr + 0x10) = 0x0001;

    radar_render((volatile unsigned short *)tex, &in);
    func_8007B8F4(0x801D2C80u, spr, x + RADAR_DX, y + RADAR_DY);
}

// Weapon row: icons of the weapons in the inventory, top right of each view.
// The pause menu shows the same 5 slots: handgun (always), SMG, shotgun, rocket launcher, C4.
// The inventory is the game's single Story inventory (shared); the highlighted icon is the
// weapon each player is holding (player + 0x1758).
#include "weapon_icons.h"
#define WROW_SPR   0x80512800u   // 10 sprites (5 per player), 0x40 apart
#define WROW_TEX   0x80513000u   // 5 weapons * 3 variants * 48 * 24 * 2 bytes (to 0x8051B700)
#define WROW_STEP  25            // screen units between icons
#define WROW_RIGHT 84            // right edge of the row, from x (same as the radar's right edge)
#define WROW_DY    (-91)         // top of the row, from y (same as the life bar)
static int wrow_ready;

static int wrow_owned(int k, int slot) {
    switch (slot) {
        case 0: return 1;
        case 1: return inv_get(k, 0x801587C6) != 0;
        case 2: return inv_get(k, 0x801587C7) != 0;
        case 3: return inv_get(k, 0x801587CB) != 0;
        case 4: return inv_get(k, 0x801587C4) != 0;
    }
    return 0;
}

static int wrow_slot_of(int weapon_id) {
    if (weapon_id >= 0 && weapon_id < 4) return weapon_id;
    if (weapon_id == 0x14) return 4;
    return -1;
}

static void weapon_row_draw(unsigned int player, int char_idx, int x, int y) {
    if (recomp_get_config_u32("weapon_row") != 0) {
        return;
    }
    const unsigned int tex_size = WICON_W * WICON_H * 2;
    if (!wrow_ready) {
        for (int i = 0; i < 5; i++) {
            for (int v = 0; v < 3; v++) {
                unsigned int t = WROW_TEX + (unsigned int)(i * 3 + v) * tex_size;
                for (int k = 0; k < WICON_W * WICON_H; k++) {
                    U16(t + (unsigned int)k * 2) = weapon_icons[i][v][k];
                }
            }
        }
        wrow_ready = 1;
    }
    int held = wrow_slot_of(U8(player + 0x1758));
    int n = 0;
    for (int i = 0; i < 5; i++) n += wrow_owned(char_idx, i);
    int px = x + WROW_RIGHT - n * WROW_STEP;
    for (int i = 0; i < 5; i++) {
        if (!wrow_owned(char_idx, i)) continue;
        int v = (i == held) ? 1 + char_idx : 0;
        unsigned int spr = WROW_SPR + (unsigned int)(char_idx * 5 + i) * 0x40;
        U8(spr + 0x00) = 0;               // RGBA
        U8(spr + 0x01) = 2;               // 16-bit
        U16(spr + 0x02) = WICON_W;
        U16(spr + 0x04) = WICON_H;
        U16(spr + 0x06) = 0;
        U32(spr + 0x08) = WROW_TEX + (unsigned int)(i * 3 + v) * tex_size;
        U32(spr + 0x0C) = 0;
        U16(spr + 0x10) = 0x0001;
        U16(spr + 0x12) = 0;
        U32(spr + 0x14) = 0;
        U32(spr + 0x18) = 0x3F000000u;    // 0.5f: 48x24 texels -> 24x12 on screen
        U32(spr + 0x1C) = 0x3F000000u;
        U8(spr + 0x20) = 255;
        U8(spr + 0x21) = 255;
        U8(spr + 0x22) = 255;
        U8(spr + 0x23) = 255;
        U32(spr + 0x24) = 0;
        func_8007B8F4(0x801D2C80u, spr, px, y + WROW_DY);
        px += WROW_STEP;
    }
}

// Ammo panel (replaces the game's bullet column): magazine count, reserve and pips.
// The game keeps each player's ammo HUD state at 0x8011FB80 + idx * 0x2C:
// +0 weapon id, +0x18 pointer to the magazine count (u8), +0x22 magazine size (u8).
#include "ammo_hud.h"
#include "live_menu.h"
#define AMMO_STATE   0x8011FB80u
#define AMMO_SPR_P1  0x80512C00u
#define AMMO_SPR_P2  0x80512C40u
#define AMMO_TEX_P1  0x8051C000u   // 128 * 32 * 2 = 0x2000 bytes each
#define AMMO_TEX_P2  0x8051E000u
static int ammo_last[2][4] = { { -9, -9, -9, -9 }, { -9, -9, -9, -9 } };
static unsigned int ammo_saved_id[4];
static int ammo_hidden[4];
static int ammo_hide_idx;

// Hide the original bullet column: the function returns early when the weapon id is 0xFF.
RECOMP_HOOK("func_800CEC1C") void coop_ammo_col_begin(int idx) {
    ammo_hide_idx = idx;
    if (idx < 0 || idx > 3 || recomp_get_config_u32("ammo_hud") != 0) {
        ammo_hide_idx = -1;
        return;
    }
    unsigned int st = AMMO_STATE + (unsigned int)idx * 0x2C;
    ammo_saved_id[idx] = U32(st);
    U32(st) = 0xFF;
    ammo_hidden[idx] = 1;
}

RECOMP_HOOK_RETURN("func_800CEC1C") void coop_ammo_col_end(void) {
    int idx = ammo_hide_idx;
    if (idx >= 0 && idx <= 3 && ammo_hidden[idx]) {
        U32(AMMO_STATE + (unsigned int)idx * 0x2C) = ammo_saved_id[idx];
        ammo_hidden[idx] = 0;
    }
}

static void ammo_panel_draw(int player_idx, int char_idx, int x, int y) {
    if (recomp_get_config_u32("ammo_hud") != 0 || player_idx < 0 || player_idx > 3) {
        return;
    }
    unsigned int st = AMMO_STATE + (unsigned int)player_idx * 0x2C;
    unsigned int id = U32(st);
    if (id == 0xFF || id > 0x14) {
        return;
    }
    unsigned int mp = U32(st + 0x18);
    if (mp < 0x80000000u || mp >= 0x80800000u) {
        return;
    }
    AmmoIn in;
    in.player = char_idx;
    // Rockets and C4 point at the (per-player) inventory, the rest at the player's own magazine.
    in.mag = (mp == 0x801587CBu || mp == 0x801587C4u) ? inv_get(char_idx, mp) : U8(mp);
    in.cap = U8(st + 0x22);
    if (id == 0) in.cap = inv_get(char_idx, 0x801587CC) ? 14 : 8;   // handgun, with the upgrade
    if (in.cap > 40) in.cap = 40;
    if (id == 0) in.reserve = -1;                                   // handgun: unlimited
    else if (id == 1) in.reserve = inv_get(char_idx, 0x801587C0);   // submachine gun
    else if (id == 2) in.reserve = inv_get(char_idx, 0x801587C1);   // shotgun
    else in.reserve = -2;                                           // rocket launcher, C4: count only

    unsigned int spr = char_idx ? AMMO_SPR_P2 : AMMO_SPR_P1;
    unsigned int tex = char_idx ? AMMO_TEX_P2 : AMMO_TEX_P1;
    int *last = ammo_last[char_idx];
    if (last[0] != in.mag || last[1] != in.cap || last[2] != in.reserve || last[3] != (int)id ||
        U32(spr + 0x08) != tex) {
        last[0] = in.mag; last[1] = in.cap; last[2] = in.reserve; last[3] = (int)id;
        ammo_render((volatile unsigned short *)tex, &in);
        U8(spr + 0x00) = 0;
        U8(spr + 0x01) = 2;
        U16(spr + 0x02) = AMMO_W;
        U16(spr + 0x04) = AMMO_H;
        U16(spr + 0x06) = 0;
        U32(spr + 0x08) = tex;
        U32(spr + 0x0C) = 0;
        U16(spr + 0x12) = 0;
        U32(spr + 0x14) = 0;
        U32(spr + 0x18) = 0x3F000000u;   // 0.5f: 128x32 -> 64x16 on screen
        U32(spr + 0x1C) = 0x3F000000u;
        U32(spr + 0x20) = 0xFFFFFFFFu;
        U32(spr + 0x24) = 0;
    }
    U16(spr + 0x10) = 0x0001;
    func_8007B8F4(0x801D2C80u, spr, x, y);
}


// Live menu drawing (in the player's own view), laid out like the game's pause menu.
#define LM_SPR_P1 0x80512D00u
#define LM_SPR_P2 0x80512D40u
#define LM_TEX_P1 0x80540000u   // 320 * 104 * 2 = 0x10400 bytes each
#define LM_TEX_P2 0x80560000u
static unsigned int lm_last_key[2] = { 0xFFFFFFFFu, 0xFFFFFFFFu };
static unsigned int lm_frame;
static int lm_spin_f[2], lm_spin_acc[2], lm_spin_w[2] = { -1, -1 };

static void live_menu_draw(int char_idx, int x, int y) {
    if (!lm_open[char_idx]) return;
    int k = char_idx;
    unsigned int p = k ? SLOT1_DATA : SLOT0_DATA;
    unsigned int other = k ? SLOT0_DATA : SLOT1_DATA;
    LiveMenuIn in;
    in.player = k;
    in.screen = lm_screen[k];
    in.tab = lm_tab[k];
    in.wsel = lm_cursor[k];
    in.tsel = lm_tsel[k];
    in.held = lm_slot_held(p);
    in.spin = 0;
    in.no_model = 0;
#ifdef WM_OK
    // 3D weapon: one step per draw, WM_TURN steps per turn, restarts when the selection changes
    if (in.screen == LM_S_WEAPON) {
        if (lm_spin_w[k] != in.wsel) { lm_spin_w[k] = in.wsel; lm_spin_f[k] = 0; }
        lm_spin_acc[k] = 0;
        if (++lm_spin_f[k] >= WM_TURN) lm_spin_f[k] = 0;
        in.spin = lm_spin_f[k];
    } else {
        lm_spin_w[k] = -1;
    }
#elif defined(WSPIN_OK)
    // rotating weapon: one turn every WSPIN_TICKS frames, restarts when the selection changes
    if (in.screen == LM_S_WEAPON) {
        if (lm_spin_w[k] != in.wsel) { lm_spin_w[k] = in.wsel; lm_spin_f[k] = 0; lm_spin_acc[k] = 0; }
        lm_spin_acc[k] += WSPIN_N;
        while (lm_spin_acc[k] >= WSPIN_TICKS) {
            lm_spin_acc[k] -= WSPIN_TICKS;
            if (++lm_spin_f[k] >= WSPIN_N) lm_spin_f[k] = 0;
        }
        in.spin = lm_spin_f[k];
    } else {
        lm_spin_w[k] = -1;
    }
#endif
    static const unsigned int mag_off[3] = { 0x1767, 0x174F, 0x1763 };
    // The key leaves the turn step out: on the WEAPON page the page is drawn once and only the
    // weapon's block is repainted as it turns (see below).
    unsigned int key = (unsigned int)(in.screen * 1031 + in.tab * 131 + in.tsel * 17 + in.wsel * 7 + in.held + 3) * 2654435761u;
    for (int i = 0; i < 5; i++) {
        in.owned[i] = lm_slot_owned(k, i);
        if (i < 3) in.mag[i] = U8(p + mag_off[i]);
        else in.mag[i] = inv_get(k, i == 3 ? 0x801587CB : 0x801587C4);
        in.reserve[i] = (i == 0) ? -1 : (i == 1) ? inv_get(k, 0x801587C0) : (i == 2) ? inv_get(k, 0x801587C1) : -2;
        in.cap[i] = (i == 0) ? (inv_get(k, 0x801587CC) ? 14 : 8) : 0;
        key = key * 31u + (unsigned int)(in.owned[i] * 977 + in.mag[i] * 13 + in.reserve[i] + 5);
    }
    // ammo sharing: the partner's ammo and the message of the last R (2 s)
    in.can_give = inv_separate() && inv_p2_valid;
    {
        unsigned int dt = lm_tick - lm_give_t[k];   // light fades over 30 frames (one step every 2)
        in.give_flash = (dt < 30u) ? 15 - (int)(dt >> 1) : 0;
    }
    for (int i = 0; i < 5; i++) {
        in.partner[i] = -1;                                          // handgun / partner hasn't got the gun
        if (i >= 3 || (i > 0 && lm_slot_owned(k ^ 1, i))) in.partner[i] = inv_get(k ^ 1, give_addr[i]);
        int g = lm_give_amount(k, p, i);
        in.give_amt[i] = g > 0 ? g : 0;
        in.give_why[i] = g > 0 ? 0 : -g;
        key = key * 31u + (unsigned int)(in.partner[i] * 7 + in.give_amt[i] * 3 + in.give_why[i] + 7);
    }
    key = key * 31u + (unsigned int)(in.can_give * 101 + in.give_flash);
    // map: the level image and the rooms discovered (as the game's map screen), plus both players
    in.map_img = -1;
    in.room_mask = 0;
    unsigned int stage = U8(0x8016A4EB);
    if (stage < 0x52 && map_stage[stage] < MAP_IMAGES) {
        in.map_img = map_stage[stage];
        unsigned int mi = (unsigned int)in.map_img;
        if (mi == 22) mi = 1;
        else if (mi == 23) mi = 6;
        in.room_mask = U32(0x80158764u + mi * 4u);
        unsigned int r0 = U16(SLOT0_DATA + 0x8E), r1 = U16(SLOT1_DATA + 0x8E);
        if (r0 < 32) in.room_mask |= 1u << r0;
        if (r1 < 32) in.room_mask |= 1u << r1;
    }
    for (int i = 0; i < 2; i++) {
        unsigned int q = i ? SLOT1_DATA : SLOT0_DATA;
        in.px[i] = F32(q + 0);
        in.pz[i] = F32(q + 8);
        in.yaw[i] = F32(q + 0x64) + F32(q + 0x158);
        in.hp[i] = S8(q + 0x176B);
        key = key * 31u + (unsigned int)(in.hp[i] * 3 + lm_slot_held(q));
    }
    (void)other;
    key = key * 31u + in.room_mask;
    unsigned int spr = k ? LM_SPR_P2 : LM_SPR_P1;
    unsigned int tex = k ? LM_TEX_P2 : LM_TEX_P1;
    int live_map = (in.screen == 0) && (((lm_tick + (unsigned int)k) & 1u) == 0);   // map moves: redraw every 2nd frame
    int model = 0;
#ifdef WM_OK
    // 3D weapon on screen: P1 repaints it on even frames, P2 on odd ones (half the cost, and two
    // open menus never draw it in the same frame).
    model = (in.screen == LM_S_WEAPON && in.wsel >= 0 && in.wsel < 5 && in.owned[in.wsel]);
    int model_step = model && (((lm_tick + (unsigned int)k) & 1u) == 0);
#endif
    if (live_map || key != lm_last_key[k] || U32(spr + 0x08) != tex) {
        lm_last_key[k] = key;
        in.no_model = model;
        live_menu_render((volatile unsigned short *)tex, &in);
#ifdef WM_OK
        if (model) {
            lm_wbg_save((volatile unsigned short *)tex, k);
            wm_draw((volatile unsigned short *)tex, in.wsel, in.spin, 200, 37);
        }
#endif
        U8(spr + 0x00) = 0;
        U8(spr + 0x01) = 2;
        U16(spr + 0x02) = LM_W;
        U16(spr + 0x04) = LM_H;
        U16(spr + 0x06) = 0;
        U32(spr + 0x08) = tex;
        U32(spr + 0x0C) = 0;
        U16(spr + 0x12) = 0;
        U32(spr + 0x14) = 0;
        U32(spr + 0x18) = 0x3F666666u;   // 0.9f: the split view stretches 2D ~10%, so it fits
        U32(spr + 0x1C) = 0x3F666666u;
        U32(spr + 0x20) = 0xFFFFFFFFu;
        U32(spr + 0x24) = 0;
    }
#ifdef WM_OK
    else if (model_step) {
        lm_wbg_restore((volatile unsigned short *)tex, k);
        wm_draw((volatile unsigned short *)tex, in.wsel, in.spin, 200, 37);
    }
#endif
    if (k == 1) lm_frame++;
    U16(spr + 0x10) = 0x0001;
    func_8007B8F4(0x801D2C80u, spr, x, y);
}

// Widescreen anchoring. In the two-view split the recomp widens the picture and anchors each HUD part
// to the left or right edge with RT64 extended GBI (src/game/hud.cpp): everything from the life bar on
// (0x800B50FC, the call to this function) is anchored right. This function also draws the mod's ammo
// panel, radar, weapon row and live menu, so the life bar and ammo panel ended up pushed to the right.
// When the recomp has just set the right anchor (its rect-align command is the last one in the display
// list), each piece gets its own anchor here, and the right anchor is put back at the end.
#define EX_SETRECTALIGN 0x64000006u
#define HUD_ANCHOR_LEFT   0
#define HUD_ANCHOR_RIGHT  1
#define HUD_ANCHOR_CENTRE 2
static int hud_ws_active;

static void hud_emit(unsigned int w0, unsigned int w1) {
    unsigned int dl = U32(0x801D2C80);
    U32(dl) = w0;
    U32(dl + 4) = w1;
    U32(0x801D2C80) = dl + 8;
}

static int hud_ws_detect(void) {
    unsigned int dl = U32(0x801D2C80);
    return U32(dl - 16) == EX_SETRECTALIGN && U32(dl - 12) == 0x00400400u;   // right anchor (origin 0x400)
}

static void hud_anchor(int a) {
    if (!hud_ws_active) return;
    // Same values as hud.cpp: offsets in quarter pixels; the 3D picture keeps a 14/320 border that grows by 4/3.
    unsigned int origin = (a == HUD_ANCHOR_LEFT) ? 0x000u : (a == HUD_ANCHOR_RIGHT) ? 0x400u : 0x800u;
    int offset = (a == HUD_ANCHOR_LEFT) ? 19 : (a == HUD_ANCHOR_RIGHT) ? (-320 * 4 - 19) : 0;
    unsigned int packed = ((unsigned int)offset & 0xFFFFu) << 16;
    hud_emit(EX_SETRECTALIGN, origin | (origin << 12));
    hud_emit(packed, packed);
}

#include "boss_bar.h"
#include "boss_ai.h"

RECOMP_PATCH void func_800CF6A8(int player_idx, int x, int y) {
    hud_ws_active = (U8(PLAYERS) == 2) && hud_ws_detect();
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

    int hp10 = hp * 10;
    int del10;
    if (char_idx == 0) {
        del10 = S16(0x801B1168 + player_idx * 2);
    } else {
        // Row 1 of the shown-health table is the boss bar's in Story (func_800D24D4 writes the boss's
        // health x 10 there), so P2's trail is eased here instead: drops an eighth of the gap per frame
        // (at least 4), follows healing at once.
        static int p2_shown10 = -1;
        if (p2_shown10 < 0 || p2_shown10 < hp10) {
            p2_shown10 = hp10;
        } else if (p2_shown10 > hp10) {
            int step = (p2_shown10 - hp10) >> 3;
            p2_shown10 -= (step < 4) ? 4 : step;
            if (p2_shown10 < hp10) p2_shown10 = hp10;
        }
        del10 = p2_shown10;
    }
    if (del10 < 0) del10 = 0;
    if (del10 > 1000) del10 = 1000;

    int bar_hp10 = (del10 < hp10) ? del10 : hp10;
    int trail_hp10 = (del10 > hp10) ? del10 : hp10;

    hud_ensure_loaded(char_idx, spr_addr, tex_addr, base_tex);
    hud_update_health_bar(char_idx, tex_addr, base_tex, hp, bar_hp10, trail_hp10);

    // Clear bit 0x2000 so func_8007A754 always emits the RDP setup header for our RGBA16 sprite
    U16(spr_addr + 0x10) = 0x0001;

    // Posicao da HUD: topo de cada viewport, com a borda esquerda alinhada a coluna de munição (pente).
    // (x, y) vem do chamador original (canto inferior direito do viewport). Antes: x - 28, y - 15.
    int hud_x = x - 187;
    int hud_y = y - 91;
    if (U8(PLAYERS) != 2) {
        // Single player (1 PLAYER): one tall view, so the co-op offsets land mid-screen.
        // Put the bar in the lower right corner instead, mirroring the ammo column on the left.
        hud_x = x - 81;
        hud_y = y - 27;
    }
    hud_anchor(HUD_ANCHOR_LEFT);
    func_8007B8F4(0x801D2C80u, spr_addr, hud_x, hud_y);
    // Ammo panel: lower left of each view (co-op), above the life bar in single player.
    if (U8(PLAYERS) == 2) {
        ammo_panel_draw(player_idx, char_idx, hud_x + 2, hud_y + 80); // lower left, where the bullet column was
    } else {
        ammo_panel_draw(player_idx, char_idx, hud_x + 40, hud_y - 17);
    }

    hud_anchor(HUD_ANCHOR_RIGHT);
    radar_draw(player_idx, player, char_idx, x, y);
    if (U8(PLAYERS) == 2) {
        weapon_row_draw(player, char_idx, x, y);
        hud_anchor(HUD_ANCHOR_CENTRE);
        live_menu_draw(char_idx, hud_x - 10, hud_y + 3);
    }
    hud_anchor(HUD_ANCHOR_RIGHT);   // what hud.cpp set for the rest of this player's HUD
    hud_ws_active = 0;
}

