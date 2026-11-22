// Tougher bosses for co-op (option boss_toughness). Included from coop.c after enemy_ai.h.
//
// func_800AF368(data) is the boss's hit handler (called from func_800AE4BC in the boss brain):
//   - clears 0x1750 (brain tick counter), 0x154, 0x158; combo 0x1798 (+1 inside the 0x174D window)
//   - bullet  (0x16F0 & 0x08): D_801BE482 -= (s16)(lb 0x176B * (4 or 5 - D_8010F6AC[diff])) / combo^2
//   - bit 0x40: -5, otherwise -20 (explosions); cheat D_8016A40E & 0x20 sets 0
//   - every hit puts the boss in the hit state 0x16EC = 0xCE with a hit animation in 0x16EE
//   - if D_801BE482 <= 0 it runs the death block inline (state 0xCD, sounds, return 1)
// With two players shooting in turn the boss is kept in 0xCE and its tick counter is reset all the
// time, so it hardly ever attacks. The mod:
//   1. damage: health is lifted by BOSS_LIFT around the call so the game can't kill it inside;
//      the loss is scaled (percent, remainder carried) and written back; if that reaches 0 the
//      death block is replayed here (same writes and calls as 0x800AF6B0..0x800AF7C0).
//   2. super armour: after a stagger the boss ignores new staggers for a while (the hit still
//      does damage): the state, animation and timers it had before the hit are put back.

#define BOSS_LIFT 10000

static const int boss_dmg_pct[3] = { 100, 55, 40 };
static const unsigned int boss_armor_ticks[3] = { 0, 45, 90 };

extern void func_8009C2E4(int a0, int a1);
extern void func_8004DE48(int a0, int a1, int a2, int a3);

static unsigned int bh_obj;            // boss data in the current call, 0 = not handled
static int bh_hp_before;
static int bh_rem;
static unsigned int bh_last_stagger;   // lm_tick of the last stagger let through
static int bh_lv;
static short bh_state, bh_anim;
static unsigned short bh_88;
static unsigned int bh_1750, bh_154, bh_158;

// Log: "boss: hit game 00 -> 00 hp 00 -> 00 armour 0" (hex), when the log option is on.
static void boss_hit_log(int loss, int out, int hp, int armour) {
    if (recomp_get_config_u32("log") == 0) return;
    static const char tmpl[] = "boss: hit game 0000 -> 0000 hp 0000 -> 0000 armour 0";
    static char line[sizeof(tmpl)];
    for (unsigned int i = 0; i < sizeof(tmpl); i++) line[i] = tmpl[i];
    put_hex(line + 15, (unsigned int)loss & 0xFFFF, 4);
    put_hex(line + 23, (unsigned int)out & 0xFFFF, 4);
    put_hex(line + 31, (unsigned int)bh_hp_before & 0xFFFF, 4);
    put_hex(line + 39, (unsigned int)(hp < 0 ? 0 : hp) & 0xFFFF, 4);
    line[51] = armour ? '1' : '0';
    winback_mod_log(line);
}

static int boss_tough_level(void) {
    if (!coop_story_session()) return 0;
    unsigned int v = recomp_get_config_u32("boss_toughness");
    return v > 2 ? 0 : (int)v;
}

RECOMP_HOOK("func_800AF368") void coop_boss_hit_begin(unsigned int d) {
    bh_obj = 0;
    {   // Passo 53: who hit the boss (the weapon task running now), for its target choice
        int k = ai_bullet_owner;
        if ((k == 0 || k == 1) && boss_hits_by[k] < 100) boss_hits_by[k]++;
    }
    int lv = boss_tough_level();
    if (lv == 0 || d < 0x80000000u || d >= 0x80800000u) return;
    int hp = S16(BOSS_HEALTH);
    if (hp <= 0) return;
    bh_obj = d;
    bh_lv = lv;
    bh_hp_before = hp;
    bh_state = S16(d + 0x16EC);
    bh_anim = S16(d + 0x16EE);
    bh_88 = U16(d + 0x88);
    bh_1750 = U32(d + 0x1750);
    bh_154 = U32(d + 0x154);
    bh_158 = U32(d + 0x158);
    S16(BOSS_HEALTH) = (short)(hp + BOSS_LIFT);
}

// The game's death block for a boss whose health reached 0 (0x800AF6B0..0x800AF7C0).
static void boss_hit_kill(unsigned int d) {
    unsigned int f4 = U32(d + 0xF4);
    S16(0x8011F7F4u) = 0;
    unsigned int slot = U8(d + 0x176A);
    U16(d + 0x88) = 0x4000;
    U8(d + 0x174D) = 0;
    S16(d + 0x16EC) = 0xCD;
    U32(d + 0xF4) = f4 & 0xDFFFFFFFu;
    U8(0x8015890Fu + slot * 22u) = 0xFF;
    U8(d + 0x176B) = 0;
    U16(0x80159A36u) = (unsigned short)(U16(0x80159A36u) + 1);
    func_8009C2E4(7, U8(d + 0x1766));
    if ((unsigned int)(U8(d + 0x16F5) - 0x2B) < 2u) {
        func_8004DE48(0x80002, 0x101, 9, 0);
        func_8004DE48(0x80002, 0x102, 9, 0);
        func_8004DE48(0x80002, 0x103, 9, 0);
        for (int s = 7; s < 12; s++) func_8004DE48(0x80002, s | 0x100, 9, 0);
    }
}

RECOMP_HOOK_RETURN("func_800AF368") void coop_boss_hit_end(void) {
    unsigned int d = bh_obj;
    if (d == 0) return;
    bh_obj = 0;
    int after = S16(BOSS_HEALTH);
    int loss = bh_hp_before + BOSS_LIFT - after;      // what the game took off
    if (loss <= 0) {                                  // no damage (early return): just undo the lift
        S16(BOSS_HEALTH) = (short)(after - BOSS_LIFT);
        return;
    }
    int hp, out = loss;
    if (after <= 0) {
        hp = 0;                                       // cheat / overflow: let it die
    } else {
        int v = loss * boss_dmg_pct[bh_lv] + bh_rem;
        out = v / 100;                                // constant divide: no trap
        bh_rem = v - out * 100;
        hp = bh_hp_before - out;
    }
    if (hp <= 0) {
        boss_hit_log(loss, out, 0, 0);
        S16(BOSS_HEALTH) = 0;
        bh_rem = 0;
        boss_hit_kill(d);
        return;
    }
    S16(BOSS_HEALTH) = (short)hp;
    int armour = (unsigned int)(lm_tick - bh_last_stagger) < boss_armor_ticks[bh_lv] && bh_state != 0xCD;
    boss_hit_log(loss, out, hp, armour);
    if (armour) {
        // super armour: keep doing what it was doing
        S16(d + 0x16EC) = bh_state;
        S16(d + 0x16EE) = bh_anim;
        U16(d + 0x88) = bh_88;
        U32(d + 0x1750) = bh_1750;
        U32(d + 0x154) = bh_154;
        U32(d + 0x158) = bh_158;
    } else {
        bh_last_stagger = lm_tick;
    }
}
