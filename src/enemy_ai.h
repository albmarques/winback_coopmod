// Smarter enemies for Story co-op (option "enemy_ai": Original / Smart / Elite).
// Included from coop.c just before the enemy update hooks (section 6).
//
// How the game's enemy AI works (read from the code, see md/HOOKS.md "IA dos inimigos"):
//  - func_800337A8 is an enemy's update. After the state handler (D_80103BF4[state], flags at
//    D_80103BF8) it runs the perception trio only when the state's flag 0x2 is set and only on
//    one frame in 7 per enemy ((+0x16F4 % 7) == (D_8010DD60 % 7)), unless +0xFC & 0x10000:
//      func_800CE518  horizontal distance (+0x1718); lost when farther than +0x1954 (2000)
//      func_800CE718  angle (+0x171C) inside +0x1950 (1.2217 rad = 70 deg each side), then line
//                     of sight (func_80044D98); sets +0xFC & 0x80 while it sees the player
//      func_800CE854  reacts: first sighting -> mode 5 (state 18), lost sight -> mode 2 (search)
//  - State 18 waits +0x174C (1 or 3) frames and turns, state 3 aims for +0x174C (6) frames,
//    state 4 fires a burst of func_80031100() shots (1, 3 or 5) and moves the gun aim (+0x158)
//    by 4-6 degrees per shot (doubles D_80115F00..D_80115F20, used only there).
// Everything here only runs in two-player Story (coop_active()), with the "enemies" option on.

extern int func_800CE518(unsigned int e);
extern void func_800CE718(unsigned int e);
extern void func_800CE854(unsigned int e);

#define CHAR_SLOTS   0x80179350u
#define CHAR_SLOT_SZ 0x1980u
#define GAME_CLOCK   0x8010DD60u   // u32, counts frames (the enemy perception stagger uses % 7)
#define ENEMY_STATE_FLAGS 0x80103BF8u  // u16 per state, 8 bytes apart: 0x2 = perception runs

//                                       Original  Smart     Elite
static const unsigned char ai_every[3]   = { 7,        2,        1 };     // perception, frames
static const unsigned int ai_fov_bits[3] = { 0x3F9C61AAu, 0x3FBDE44Eu, 0x3FDF66F3u }; // 70/85/100 deg
static const float ai_range[3]           = { 2000.0f,  2600.0f,  3200.0f };
static const float ai_alert_radius[3]    = { 0.0f,     1500.0f,  2500.0f };
static const unsigned char ai_aim_cap[3] = { 255,      3,        1 };     // state 3 aim wait, frames
static const unsigned char ai_react_cap[3] = { 255,    1,        1 };     // state 18 wait, frames
static const unsigned char ai_burst_add[3] = { 0,      1,        2 };     // extra shots per burst
static const float ai_spread[3]          = { 1.0f,     0.6f,     0.35f }; // per-shot aim drift
static const unsigned short ai_revenge[3] = { 0,       120,      180 };   // frames it chases its attacker

// Per-shot aim drift constants and their original values (rodata, only func_80031148 reads them).
static const unsigned int ai_drift_addr[5] = { 0x80115F00u, 0x80115F08u, 0x80115F10u, 0x80115F18u, 0x80115F20u };
static const double ai_drift_orig[5] = { 0.10472, 0.10472, 0.069813, 0.087266, 0.087266 };

static int ai_level(void) {
    if (!coop_story_session() || recomp_get_config_u32("enemies") != 0) return 0;
    unsigned int v = recomp_get_config_u32("enemy_ai");
    return v > 2 ? 0 : (int)v;
}

// Per character slot (0-7) state.
static unsigned int ai_obj[8];          // the slot's object when this state was made: resets on reuse
static unsigned char ai_target[8];      // 0 / 1 = player it is fighting, 0xFF = none yet
static unsigned char ai_prev_seen[8];
static unsigned char ai_alert[8];       // frames left of a squad alert (wide view on its next looks)
static unsigned char ai_revenge_k[8];   // player that last shot it, 0xFF = none
static unsigned int ai_revenge_t[8];    // lm_tick of that hit
static unsigned char ai_force_look[8];  // look this frame (target changed, just alerted)
static int ai_bullet_owner = -1;        // player whose weapon task is running (coop_bullet_begin)

static int ai_cur_idx = -1;             // slot of the enemy being updated
static int ai_natural;                  // the game's own perception turn this frame
static unsigned int ai_log_last;

static int ai_slot_index(unsigned int e) {
    if (e < CHAR_SLOTS || e >= CHAR_SLOTS + 8u * CHAR_SLOT_SZ) return -1;
    unsigned int off = e - CHAR_SLOTS;
    unsigned int i = off / CHAR_SLOT_SZ;            // constant divide: no trap instruction
    if (off != i * CHAR_SLOT_SZ) return -1;
    return (int)i;
}

static void ai_slot_check(int i, unsigned int e) {
    unsigned int obj = U32(e + 0x1740);
    if (ai_obj[i] != obj) {
        ai_obj[i] = obj;
        ai_target[i] = 0xFF;
        ai_prev_seen[i] = 0;
        ai_alert[i] = 0;
        ai_revenge_k[i] = 0xFF;
        ai_force_look[i] = 0;
    }
}

static void ai_log(const char *what, int i, int k) {
    if (recomp_get_config_u32("log") == 0) return;
    unsigned int now = winback_mod_time_us();
    if ((unsigned int)(now - ai_log_last) < 250000u) return;   // at most 4 lines a second
    ai_log_last = now;
    static char line[64];
    int n = 0;
    const char *pre = "coop: ai ";
    while (*pre && n < 40) line[n++] = *pre++;
    while (*what && n < 52) line[n++] = *what++;
    line[n++] = ' ';
    line[n++] = 's';
    line[n++] = (char)('0' + (i & 7));
    line[n++] = ' ';
    line[n++] = 'P';
    line[n++] = (char)('1' + (k & 1));
    line[n] = 0;
    winback_mod_log(line);
}

// Once a frame (coop_draw_begin): per-shot drift constants for this level.
static void ai_frame(void) {
    int lv = ai_level();
    for (int n = 0; n < 5; n++) {
        union { double d; unsigned int u[2]; } v;
        v.d = ai_drift_orig[n] * (double)ai_spread[lv];
        set_f64(ai_drift_addr[n], v.u[0], v.u[1]);
    }
}

// New enemies: wider view and longer sight. Only the spawn (func_8002C86C) writes these, so a
// value still at the original (or at another level's) is replaced.
static void ai_tune_senses(unsigned int e, int lv) {
    unsigned int fov = U32(e + 0x1950);
    for (int n = 0; n < 3; n++) {
        if (fov == ai_fov_bits[n] && n != lv) { U32(e + 0x1950) = ai_fov_bits[lv]; break; }
    }
    float r = F32(e + 0x1954);
    for (int n = 0; n < 3; n++) {
        if (r == ai_range[n] && n != lv) { F32(e + 0x1954) = ai_range[lv]; break; }
    }
}

// Which player this enemy fights: 0 or 1. Called only with both players alive.
static int ai_pick_target(int i, unsigned int e, unsigned int p0, unsigned int p1, float d0, float d1, int lv) {
    unsigned short er = U16(e + 0x8E);
    int same0 = U16(p0 + 0x8E) == er, same1 = U16(p1 + 0x8E) == er;
    int want;
    int cur = ai_target[i];
    // 1. Revenge: whoever just shot it, while they are in its room.
    if (ai_revenge_k[i] < 2 && (unsigned int)(lm_tick - ai_revenge_t[i]) < ai_revenge[lv] &&
        (ai_revenge_k[i] ? same1 : same0)) {
        want = ai_revenge_k[i];
    } else if (cur < 2) {
        // 2. Keep the current target; change only if it left the room and the other is here,
        //    or the other is much closer (under 60% of the distance).
        int same_cur = cur ? same1 : same0, same_oth = cur ? same0 : same1;
        float dc = cur ? d1 : d0, dn = cur ? d0 : d1;
        want = cur;
        if ((!same_cur && same_oth) || dn < dc * 0.36f) want = cur ^ 1;
    } else {
        // 3. First choice: the nearer one; Hard / Brutal keep the old split between players.
        want = (d1 < d0) ? 1 : 0;
        if (coop_difficulty() != 0) {
            int pref = (int)(((e >> 4) ^ (e >> 9)) & 1);
            if (U16((pref ? p1 : p0) + 0x8E) == er) want = pref;
        }
    }
    if (cur < 2 && want != cur) {
        ai_force_look[i] = 1;
        ai_log("switch", i, want);
    }
    ai_target[i] = (unsigned char)want;
    return want;
}

// After the enemy's own update, still with its target player in D_801390C0[0].
static void ai_after_update(unsigned int e, int lv) {
    int i = ai_cur_idx;
    if (i < 0 || lv == 0) return;
    if (S8(e + 0x176B) <= 0) return;
    unsigned int tp = U32(PLAYER_PTRS);
    if (tp == 0 || S8(tp + 0x176B) <= 0) return;
    if (U8(CUTSCENE_FLAG) != 0 || U32(0x80134710) != 0) return;
    if (U16(e + 0x1958) & 0x20) return;               // scripted: the game skips its senses too
    int state = S16(e + 0x16EC);
    if (state < 0 || state > 46) return;

    // Quicker reactions: shorten the spotted pause (state 18) and the aim pause (state 3).
    unsigned char sub = U8(e + 0x16F0);
    if (state == 18 && sub == 6 && U8(e + 0x174C) > ai_react_cap[lv]) U8(e + 0x174C) = ai_react_cap[lv];
    if (state == 3 && sub == 1 && U8(e + 0x174C) > ai_aim_cap[lv]) U8(e + 0x174C) = ai_aim_cap[lv];

    // More frequent looks than the game's 1 in 7 frames.
    if ((U16(ENEMY_STATE_FLAGS + (unsigned int)state * 8u) & 2) && !ai_natural) {
        unsigned int clk = U32(GAME_CLOCK) + U8(e + 0x16F4);
        int turn = (ai_every[lv] <= 1) || ((clk & 1) == 0);
        if (turn || ai_force_look[i] || ai_alert[i]) {
            unsigned short t1930 = U16(e + 0x1930);
            if (func_800CE518(e)) {
                func_800CE718(e);
                func_800CE854(e);
            }
            // The lost-sight countdown counts looks: keep it at the game's pace.
            if (t1930 != 0 && U16(e + 0x1930) == (unsigned short)(t1930 - 1)) U16(e + 0x1930) = t1930;
        }
    }
    ai_force_look[i] = 0;
    if (ai_alert[i]) ai_alert[i]--;

    // Squad alert: an enemy that has just spotted a player warns those near it in its room,
    // who then look all around them (their next looks use a full circle, line of sight still needed).
    int seen = (U32(e + 0xFC) & 0x80) != 0;
    if (seen && !ai_prev_seen[i]) {
        float rad = ai_alert_radius[lv];
        unsigned short er = U16(e + 0x8E);
        int sent = 0;
        for (int j = 0; j < 8; j++) {
            unsigned int s = CHAR_SLOTS + (unsigned int)j * CHAR_SLOT_SZ;
            if (s == e || s == U32(PLAYER_PTRS) || s == U32(PLAYER_PTRS + 4)) continue;
            if (U16(s + 0x92) == 0 || S8(s + 0x176B) <= 0) continue;
            unsigned int obj = U32(s + 0x1740);
            if (obj < 0x80000000u || obj >= 0x80800000u || U8(obj + 0x34) != 2) continue;
            if (U16(s + 0x8E) != er || (U32(s + 0xFC) & 0x80) || (U16(s + 0x1958) & 0x20)) continue;
            float dx = F32(s + 0) - F32(e + 0), dz = F32(s + 8) - F32(e + 8);
            if (dx * dx + dz * dz > rad * rad) continue;
            ai_slot_check(j, s);
            ai_alert[j] = 45;
            ai_force_look[j] = 1;
            sent++;
        }
        if (sent) ai_log("alert", i, tp == SLOT1_DATA ? 1 : 0);
    }
    ai_prev_seen[i] = (unsigned char)seen;
}

// func_800CE718 (angle + line of sight): an alerted enemy looks all around it.
static unsigned int ai_fov_e;
static unsigned int ai_fov_saved;

RECOMP_HOOK("func_800CE718") void coop_ai_look_begin(unsigned int e) {
    ai_fov_e = 0;
    if (!coop_active()) return;
    int i = ai_slot_index(e);
    if (i < 0 || ai_alert[i] == 0 || ai_level() == 0) return;
    ai_fov_e = e;
    ai_fov_saved = U32(e + 0x1950);
    F32(e + 0x1950) = 3.1416f;
}

RECOMP_HOOK_RETURN("func_800CE718") void coop_ai_look_end(void) {
    unsigned int e = ai_fov_e;
    if (e == 0) return;
    ai_fov_e = 0;
    U32(e + 0x1950) = ai_fov_saved;
    if (U32(e + 0xFC) & 0x80) {
        int i = ai_slot_index(e);
        if (i >= 0) ai_alert[i] = 0;
    }
}

// Longer bursts: the game's shot count per burst plus ai_burst_add (co-op only).
// Original: pistol 1 (3 with +0x1958 & 0x2000), class 1 (+0x1762 == 1) 3 (5), others 1.
RECOMP_PATCH int func_80031100(unsigned int e) {
    unsigned short f = U16(e + 0x1958);
    unsigned char w = U8(e + 0x1762);
    int lim;
    if (w == 0) lim = (f & 0x2000) ? 3 : 1;
    else if (w == 1) lim = (f & 0x2000) ? 5 : 3;
    else lim = 1;
    if (coop_active()) lim += ai_burst_add[ai_level()];
    return U8(e + 0x1932) < lim;
}

// A player's shot hit an enemy (from coop_damage_begin): remember who, and make it turn round.
static void ai_on_enemy_hit(unsigned int target, int attacker_idx) {
    if (!coop_active() || ai_level() == 0) return;
    if (target == SLOT0_DATA || target == SLOT1_DATA) return;
    int i = ai_slot_index(target);
    if (i < 0) return;
    int k = ai_bullet_owner;
    if (k < 0 && (attacker_idx == 0 || attacker_idx == 1)) k = attacker_idx;
    if (k < 0) return;
    ai_slot_check(i, target);
    ai_revenge_k[i] = (unsigned char)k;
    ai_revenge_t[i] = lm_tick;
    ai_alert[i] = 30;
    ai_force_look[i] = 1;
}
