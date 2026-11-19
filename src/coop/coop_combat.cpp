#include "coop/coop_zonechara.hpp" // coop-pt1111k
#include "coop/game_ns.hpp"
#include "coop/coop.hpp"
#include "coop/coop_audio.hpp" // coop-menuse
#include "coop/coop_perf.hpp"

#include <libvu0.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <tuple>

#include "character.hpp"
#include "clothread.hpp"
#include "collisiondata.hpp"
#include "dun/gameloop.hpp"
#include "btactstatus.hpp"
#include "frame.hpp"
#include "btmisc.hpp"
#include "hitmark.hpp"
#include "healeffect.hpp"
#include "gameutil.hpp"
#include "shot_freefuncs.hpp"
#include "runscript_opcodes.hpp"
#include "runscript.hpp"
#include "savedata.hpp"
#include "dngmessageman.hpp"
#include "dngstatusdata.hpp"
#include "mathutil.hpp"
#include "dungeonparts.hpp"
#include "dungeonmap.hpp"
#include "framevu1.hpp"
#include "hitvalue.hpp"
#include "itembombeffect.hpp"
#include "itemdata.hpp"
#include "mainitemmodel.hpp" // coop-actorswap: the throw parity rows
#include "motionmodel.hpp"
#include "menu_draw.hpp"
#include "menu_inventory.hpp"
#include "menu_misc.hpp" // gaps2: WeaponDataChangeByRGate
#include "menu_save.hpp" // coop-actorswap: BattleSubWeaponDmg
#include "clsmes.hpp"    // gaps2: MES_POS_BOTTOM
#include "sysmes.hpp"    // gaps2: SetSystemMes
#include "mglib.hpp"
#include "monstorunit.hpp"
#include "rect.hpp"
#include "shot_utils.hpp"
#include "snd.hpp"
#include "sound_effect.h"
#include "texture.hpp"
#include "dataread.hpp"
#include "shot_effect.hpp"
#include "userstatus.hpp"

#include "coop/coop_core.hpp"
#include "coop/coop_actor.hpp" // coop-actorswap
#include "coop/coop_menu.hpp" // coop-menus
#include "coop/coop_camera.hpp" // coop-p2circle: PartnerCameraBehind
#include "coop/coop_camtest.hpp"
#include "coop/coop_combat.hpp"
#include "coop/coop_inventory.hpp" // coop-inventory
#include "coop/coop_interact.hpp"
#include "coop/coop_lockhud.hpp" // coop-lockhud
#include "coop/coop_party.hpp"
#include "coop/coop_prompt.hpp" // coop-revive
#include "coop/coop_rumble.hpp"
#include "coop/coop_members.hpp" // coop-p2rumble: PartnerPlayer
#include "coop/coop_wheel.hpp"
#include "coop/coop_worlds.hpp" // coop-kojoin: CoopWorldsApart
#include "coop/netplay.hpp"     // coop-kojoin: NetplayPartnerLinked
#include "coop/coop_message.hpp" // coop-itemprompt

namespace dc::game {
extern CFrame *bombCursorFrame; // gameloop.cpp: retail's landing-point cursor of a thrown bomb (coop-p2throw)
extern float   targetCursorCnt; // gameloop.cpp: his lock-on cursor's count (coop-lockon1118's test hook)
extern s32     autoDemo; // gameloop.cpp: the title's demo play (coop-lockon1118: setTargetCursor's gate)
} // namespace dc::game

namespace coop {

Combat g_combat;

// Where the partner's heal sparkle was last drawn (for the tests; draw-side only).
float g_heal_drawn_at[3] = {0.0f, 0.0f, 0.0f};

// The motions a tapped attack plays, as each character's key or battle action plays them, -1 past the
// last: Toan's swing, Xiao's wind-up and throw, Goro's swing, Ruby's one cast (BattleActionPlay_Ruby's
// motion 11: 14 is her charge, held, and 16 the charged cast), Ungaga's swing, Osmond's burst.
constexpr int kTapMotions[6][2] = {{0x24, -1}, {11, 13}, {0x24, -1}, {11, -1}, {0x25, -1}, {11, -1}};

// Ruby's charge, held with the ring raised, and the cast it lets go with.
constexpr int kRubyChargeMotion = 14;
constexpr int kRubyChargedCast = 16;

namespace {

// The floor (dungeon and floor number) CheckFloor last saw.
int g_floor_key = -1;

// Ruby's ring magic for the partner: the effect BattleActionShotRuby launches from NowMainEffect, which
// holds only the controlled character's, loaded here into an effect of the partner's own (its own
// texture block, model arena and pack buffer) for the element of her weapon, as the element picker
// loads the player's (menu_dungeon.cpp).
// Osmond's main effect (his muzzle flash) uses the same partner effect.
constexpr int kRingTextureBlock = 0x42;
constexpr int kRingQuads = 0x20000;
constexpr int kRingPackBytes = 2 * 1024 * 1024;

struct Ring {
    CSHOT_EFFECT   effect;
    CDataAlloc2<1> arena;
    u_char        *model = nullptr;
    u_int         *pack = nullptr;
    BT_SHOT_EFFECT *entry = nullptr;
    int            element = -1;
    bool           loaded = false;
    int            shots = 0;
    int            flashes = 0;
    std::vector<u_long> textures; // its block's textures as it loaded them
};

Ring g_ring;

bool RingBlockEmpty() {
    for (const CTexture &texture : TexManager.textures) {
        if (texture.block == kRingTextureBlock) {
            return false;
        }
    }

    return true;
}

void UnloadRing() {
    g_ring.effect.Initialize();
    g_ring.loaded = false;
    g_ring.entry = nullptr;
    g_ring.element = -1;
}

// Loads an effect of the controlled characters' set into the partner's own (once per effect, and again
// when a new floor has emptied the block), as MainChara_Effect loads the player's. element is what
// the ring test reads back (-1 for Osmond's). False when the file cannot be read.
bool EffectReady(BT_SHOT_EFFECT *entry, int element) {
    if (entry == nullptr) {
        return false;
    }

    if (g_ring.loaded && g_ring.entry == entry && !RingBlockEmpty()) {
        return true;
    }

    if (g_ring.model == nullptr) {
        g_ring.model = new (std::nothrow) u_char[(std::size_t) kRingQuads * 16];
        g_ring.pack = new (std::nothrow) u_int[kRingPackBytes / 4];
    }

    if (g_ring.model == nullptr || g_ring.pack == nullptr) {
        return false;
    }

    char path[64];
    int  size = 0;

    std::snprintf(path, sizeof path, "dun/mainchara/wep_eff/%s.chr", entry->model_name);
    UnloadRing();
    TexManager.DeleteTextureBlock(kRingTextureBlock);

    if (LoadFile(path, g_ring.pack, &size) == 0 || size <= 0 || size > kRingPackBytes) {
        std::fprintf(stderr, "coop: cannot load %s\n", path);
        return false;
    }

    g_ring.arena.base = g_ring.model;
    g_ring.arena.buffer = g_ring.model;
    g_ring.arena.used = 0;
    g_ring.arena.limit = kRingQuads;

    if (g_ring.effect.Entry2(entry, g_ring.pack, kRingTextureBlock, &g_ring.arena, 4) == 0) {
        std::fprintf(stderr, "coop: cannot set up %s\n", path);
        return false;
    }

    g_ring.loaded = true;
    g_ring.entry = entry;
    g_ring.element = element;
    g_ring.textures.clear(); // coop-osmondflash: what it loaded, for the effectblock test

    for (const CTexture &texture : TexManager.textures) {
        if (texture.name[0] != 0 && texture.block == kRingTextureBlock) {
            g_ring.textures.push_back(texture.tex0);
        }
    }

    std::printf("coop: P2 effect %s (element %d), %d quadwords\n", path, element, g_ring.arena.used);
    return true;
}

// The ring Ruby's weapon element draws.
bool RingReady(int element) {
    if (element < 0 || element > 4) {
        element = 0;
    }

    return EffectReady(Get_Main_EffectPtr(CHARA_RUBY, element), element);
}

// Osmond's gun's effect, as Get_Main_EffectPtr picks it from his equipped gun. It also sets the
// controlled character's gun_type, which is put back: the partner's gun is not the player's.
BT_SHOT_EFFECT *OsmondEffect() {
    s32             gun_type = BtActStatus.gun_type;
    BT_SHOT_EFFECT *entry = Get_Main_EffectPtr(CHARA_OSMOND, 0);

    BtActStatus.gun_type = gun_type;
    return entry;
}

// Osmond's muzzle flash, as BattleActionPlay_Ozumond sets it off with each burst: his main effect on
// the gun's "dcol0" frame (the partner's weapon model), or at the muzzle point without one.
bool ShootFlash(const float *from, const float *direction) {
    if (Mutated("flash") || !EffectReady(OsmondEffect(), -1)) {
        return false;
    }

    CFrame       *muzzle = g_partner.weapon_loaded && g_partner.weapon != nullptr && g_partner.weapon->frame != nullptr
                               ? g_partner.weapon->frame->SearchFrame((char *) "dcol0")
                               : nullptr;
    sceVu0FVECTOR position;
    sceVu0FVECTOR target;

    for (int axis = 0; axis < 3; axis++) {
        position[axis] = from[axis];
        target[axis] = from[axis] + 10.0f * direction[axis];
    }

    position[3] = target[3] = 1.0f;

    if (g_ring.effect.Set(position, target, -1, -1, 0, muzzle, -1) < 0) {
        return false;
    }

    g_ring.flashes++;
    return true;
}

// Launches Ruby's ring as BattleActionShotRuby does: from the partner's hand toward where the shot
// goes, its damage and weapon bonuses the partner's own. Owner 3 is Ruby's, as the game's.
bool ShootRing(const float *from, const float *direction) {
    const WEAPON_HAVE *weapon = EquippedWeapon(CHARA_RUBY);

    if (Mutated("ring") || !RingReady(weapon != nullptr ? weapon->best_elem : 0)) {
        return false;
    }

    sceVu0FVECTOR position;
    sceVu0FVECTOR target;

    for (int axis = 0; axis < 3; axis++) {
        position[axis] = from[axis];
        target[axis] = from[axis] + 10.0f * direction[axis];
    }

    position[3] = target[3] = 1.0f;

    if (g_ring.effect.Set(position, target, 3, weapon != nullptr ? weapon->best_elem : 0, 0, nullptr, 1) < 0) {
        return false;
    }

    if (weapon != nullptr) {
        g_ring.effect.SetWepStatus(weapon->flags);
        g_ring.effect.SetVsMonster((char *) weapon->vs_monster);
    }

    g_ring.effect.SetDmg(Damage());
    g_ring.shots++;
    return true;
}

// Charging, as ToanKey_Play charges the player's sword: the ticks to each level (charge_time from 1.0
// to 1.5 and 2.5 at a sixtieth a tick), the most counted, the damage a charged release scales by (its
// blow is damage * 1.5 at either level), and the second level's rising wave, NowMainEffect's
// SetDmg(1.5 * damage) from where the player stands, here a hit that size round the partner.
constexpr int   kChargeLevel1 = 30;
constexpr int   kChargeLevel2 = 90;
constexpr int   kChargeMost = 180;
constexpr float kChargeScale = 1.5f;
constexpr float kWaveRadius = 12.0f;

// Circle held this long lets go of the lock.
constexpr int kLockReleaseTicks = 24;

// The motions a held swing waits in: Xiao's drawn slingshot, as BattleActionPlay_Jinn holds it, and
// the sword charge ToanKey_Play holds.
constexpr int kXiaoHoldMotion = 12;
constexpr int kChargeMotion = 14;

// Throws: ticks in the air, the pull down each tick, how far ahead one lands with nothing locked and
// the farthest a locked monster may be to be thrown at.
constexpr int   kThrowTicks = 30;
constexpr float kThrowGravity = 0.15f;
constexpr float kThrowAhead = 40.0f;
constexpr float kThrowReach = 110.0f;

// The way the partner faces, as a yaw.
float Yaw() {
    sceVu0FVECTOR rotation;

    g_partner.chara->frame->GetRotation(rotation);
    return rotation[1];
}

bool Lockable(int i, float range_scale) {
    const MONSTOR &monster = NowMonstorUnit->monster[i];

    if (monster.state != 2 || monster.hp <= 0 || monster.revealed == 0 || monster.lockon_enabled == 0 ||
        monster.kind == MONSTER_KIND_NO_LOCK_ON) {
        return false;
    }

    float position[4];

    NowMonstorUnit->chara[i][0].GetPosition(position);
    return DistVector(position, g_partner.pos) < monster.lock_range * range_scale && std::fabs(position[1] - g_partner.pos[1]) < 80.0f;
}

// Where the partner's shots and throws go at the locked monster: its lock-on point, as
// setTargetCursor takes it.
bool LockPoint(float *out) {
    if (g_combat.lock < 0) {
        return false;
    }

    const MONSTOR &monster = NowMonstorUnit->monster[g_combat.lock];

    if (monster.lockon_frame != nullptr) {
        sceVu0CopyVector(out, (float *) monster.lockon_position);
    } else {
        NowMonstorUnit->chara[g_combat.lock][0].GetPosition(out);
        out[1] += 8.0f;
    }

    out[3] = 1.0f;
    return true;
}

void ReleaseLock(const char *why) {
    if (g_combat.lock >= 0) {
        std::printf("coop: P2 lock on monster %d %s\n", g_combat.lock, why);
    }

    g_combat.lock = -1;
}

// coop-p2controls3 (Jep 11-17: "copy P1's controls for lock-on: Circle toggles, L1 cycles targets"): retail's lock-on choice
// (gameloop.cpp's SetNearLockOnTarget, split out: NearLockOnOrder / NearLockOnPick) run for her body: the lockable monsters in her
// member's reach and in front of her heading, nearest first. Her lock was a tap that locked or moved on and a hold that let go.
int HerLockOrder(int *order) {
    float rotation[4] = {};
    float at[4] = {g_partner.pos[0], g_partner.pos[1], g_partner.pos[2], 1.0f};

    g_partner.chara->frame->GetRotation(rotation);
    return NearLockOnOrder(at, rotation[1], LockRangeRateOf(g_partner.loaded_chara), order, nullptr);
}

// Test hook (coop-p2controls3, DC_COOP_TEST_LOCK_FRONT=N:D:T): at press clock T the floor's first N sleeping monsters are put in front
// of her heading, the k-th D + 12k ahead and k * 6 to her right, and woken, on every copy alike, for her lock-on's order.
void TestLockFront() {
    static const char *spec = TestEnv("DC_COOP_TEST_LOCK_FRONT");
    int                n = 0;
    int                distance = 0;
    long               when = 0;

    if (spec == nullptr || NowMonstorUnit == nullptr || std::sscanf(spec, "%d:%d:%ld", &n, &distance, &when) != 3 || NetplayTestPressClock() != when) {
        return;
    }
    float rotation[4] = {};

    g_partner.chara->frame->GetRotation(rotation);
    const float yaw = rotation[1];
    int         placed = 0;

    for (int i = 0; i < 16 && placed < n; i++) {
        if (NowMonstorUnit->monster[i].state != 1 || NowMonstorUnit->monster[i].hp <= 0) {
            continue;
        }
        const float ahead = (float) (distance + 12 * placed);
        const float side = 6.0f * (float) placed;
        float       at[4] = {g_partner.pos[0] + ahead * sinf(yaw) + side * cosf(yaw), g_partner.pos[1], g_partner.pos[2] + ahead * cosf(yaw) - side * sinf(yaw),
                             1.0f};

        NowMonstorUnit->chara[i][0].SetPosition(at);
        NowMonstorUnit->monster[i].state = 2;
        std::fprintf(stderr, "coop: test: monster %d woken %.0f ahead of her at (%.0f %.0f)\n", i, ahead, at[0], at[2]);
        placed++;
    }
}

// DunMoveChara's Circle (PadInput_NO's Down): with no monster to lock the camera comes round behind; else locked lets go (the
// refuse sound), unlocked locks the one setTargetCursor holds ready (SetNearLockOnTarget(0, 1): the nearest).
void HerLockCircle() {
    int       order[16];
    const int count = HerLockOrder(order);
    const int ready = count > 0 ? NearLockOnPick(order, count, 0, 1, -1) : -1;

    if (g_combat.lock >= 0) {
        ReleaseLock("let go (her Circle)");
        SndSePlay(MENU_SOUND_REFUSE, -1, 0);
    } else if (ready < 0) {
        PartnerCameraBehind(); // (coop-p2circle: retail's camera behind the player)
    } else {
        g_combat.lock = ready;
        g_combat.lock_shift_rot = count;
        g_combat.lock_cursor = 8.0f; // coop-lockon1118: as SetNearLockOnTarget sets targetCursorCnt for a new target
        SndSePlay(SE_LOCK_ON, -1, 0);
        std::printf("coop: P2 locked on monster %d\n", ready);
    }
    std::fprintf(stderr, "coop: player 2's Circle: lock %d (%d to lock in front of her) at link tick %lld\n", g_combat.lock, count,
                 (long long) NetplayLinkTick());
}

// DunMoveChara's L1 while locked: the next in the order (SetNearLockOnTarget(shift, 0)), back to the first past the last.
int HerLockShift(int from) {
    int       order[16];
    const int count = HerLockOrder(order);

    if (count == 0) {
        ReleaseLock("none in front (her L1)");
        return 0;
    }
    const int was = g_combat.lock;

    g_combat.lock = NearLockOnPick(order, count, from, 0, g_combat.lock);
    if (g_combat.lock != was) {
        g_combat.lock_cursor = 8.0f; // coop-lockon1118: a new target's cursor, as SetNearLockOnTarget's
    }
    std::fprintf(stderr, "coop: player 2's lock-on order at her L1: %d %d %d %d (from %d, held %d) at link tick %lld\n", order[0], order[1], order[2], order[3],
                 from, was, (long long) NetplayLinkTick());
    return count;
}

void HerLockL1() {
    if (g_combat.lock_shift_no >= g_combat.lock_shift_rot - 1) {
        g_combat.lock_shift_no = 0;
        g_combat.lock_shift_rot = HerLockShift(0);
    } else {
        g_combat.lock_shift_no++;
        g_combat.lock_shift_rot = HerLockShift(g_combat.lock_shift_no);
    }
    SndSePlay(SE_LOCK_ON, -1, 0);
    std::fprintf(stderr, "coop: player 2's L1: lock %d (place %d of %d) at link tick %lld\n", g_combat.lock, g_combat.lock_shift_no, g_combat.lock_shift_rot,
                 (long long) NetplayLinkTick());
}

} // namespace

// coop-p2controls: her ring's swap and her lock-on, by retail's rule (DunMoveChara's quick change: the change drops the lock, and
// with Swap Keeps Lock-On it comes back while its monster can still be locked on, which Lockable checks every tick). Her lock
// stayed through every swap whatever her setting.
void PartnerSwapLock(bool keep) {
    std::fprintf(stderr, "coop: player 2's swap: her lock-on %s (monster %d; her Swap Keeps Lock-On %s) at link tick %lld\n",
                 g_combat.lock < 0 ? "none" : keep ? "kept" : "dropped", g_combat.lock, keep ? "on" : "off", (long long) NetplayLinkTick());
    if (!keep) {
        ReleaseLock("dropped by the swap");
    }
}

// Test hook (coop-lockon1118, DC_COOP_TEST_LOCK_SLIP=F[/F...]:T, with two monsters woken in front of her: DC_COOP_TEST_LOCK_FRONT): a
// phase per F, 250 ticks each from press clock T. At its start player 1 is locked on the first awake lockable monster and she on the
// second (retail's lock-on fields, her g_combat.lock; both cursors at 8, as a new lock's), each held at half its player's reach; 40 ticks on, each monster is held every tick
// F hundredths of its lock range times its player's reach away from that player, on the line from him or her to it, until its lock lets
// go. Each copy logs how many ticks each player's lock held after the move, seen here before her step (his lets go in the draw before),
// the same way for both. Every copy alike (the press clock).
namespace {

struct LockSlip {
    int  factor[4] = {};
    int  phases = 0;
    long start = -1;
    int  monster[2] = {-1, -1};
    bool held[2] = {};
    int  phase = -1;
};

LockSlip g_slip;

void Hold(int i, const float *from, float reach) {
    float at[4];

    NowMonstorUnit->chara[i][0].GetPosition(at);
    float dx = at[0] - from[0];
    float dz = at[2] - from[2];
    float flat = std::sqrt(dx * dx + dz * dz);

    if (flat < 0.01f) {
        dx = 1.0f;
        dz = 0.0f;
        flat = 1.0f;
    }
    float put[4] = {from[0] + reach * dx / flat, from[1], from[2] + reach * dz / flat, 1.0f};

    NowMonstorUnit->chara[i][0].SetPosition(put);
}

void TestLockSlip() {
    static const char *spec = TestEnv("DC_COOP_TEST_LOCK_SLIP");
    static bool        parsed = false;

    if (spec == nullptr || NowMonstorUnit == nullptr) {
        return;
    }
    if (!parsed) {
        parsed = true;
        const char *colon = std::strrchr(spec, ':');

        if (colon == nullptr) {
            return;
        }
        g_slip.start = std::atol(colon + 1);
        for (const char *p = spec; p < colon && g_slip.phases < 4;) {
            g_slip.factor[g_slip.phases++] = std::atoi(p);
            const char *slash = std::strchr(p, '/');

            if (slash == nullptr || slash > colon) {
                break;
            }
            p = slash + 1;
        }
    }
    const long clock = NetplayTestPressClock();

    if (g_slip.start < 0 || clock < g_slip.start || clock >= g_slip.start + 250L * g_slip.phases) {
        return;
    }
    const int  phase = (int) ((clock - g_slip.start) / 250);
    const long in = (clock - g_slip.start) % 250;
    const int  factor = g_slip.factor[phase];

    if (in == 0) {
        int found = 0;

        for (int i = 0; i < 16 && found < 2; i++) {
            const MONSTOR &m = NowMonstorUnit->monster[i];

            if (m.state == 2 && m.hp > 0 && m.lockon_enabled != 0 && m.kind != MONSTER_KIND_NO_LOCK_ON) {
                g_slip.monster[found++] = i;
            }
        }
        g_slip.phase = found == 2 ? phase : -1;
        if (found < 2) {
            std::fprintf(stderr, "coop: test: lock slip: phase %d: only %d monsters to lock\n", phase, found);
            return;
        }
        lockOnTargetNo = g_slip.monster[0];
        lockOnTargetFlag = 1;
        targetCursorCnt = 8.0f;
        g_combat.lock = g_slip.monster[1];
        g_combat.lock_cursor = 8.0f;
        g_slip.held[0] = g_slip.held[1] = true;
        Hold(g_slip.monster[0], CharaMain.pos, 0.5f * NowMonstorUnit->monster[g_slip.monster[0]].lock_range * LockRangeRateOf(UserStatus->cur_chara));
        Hold(g_slip.monster[1], g_partner.pos, 0.5f * NowMonstorUnit->monster[g_slip.monster[1]].lock_range * LockRangeRateOf(g_partner.loaded_chara));
        std::fprintf(stderr, "coop: test: lock slip: phase %d: player 1 locked on monster %d, player 2 on monster %d\n", phase, g_slip.monster[0],
                     g_slip.monster[1]);
        return;
    }
    if (g_slip.phase != phase) {
        return;
    }
    if (in < 40) { // (each monster held at half its player's reach until the move, so each lock starts held)
        Hold(g_slip.monster[0], CharaMain.pos, 0.5f * NowMonstorUnit->monster[g_slip.monster[0]].lock_range * LockRangeRateOf(UserStatus->cur_chara));
        Hold(g_slip.monster[1], g_partner.pos, 0.5f * NowMonstorUnit->monster[g_slip.monster[1]].lock_range * LockRangeRateOf(g_partner.loaded_chara));
        return;
    }
    const long after = in - 40;

    for (int p = 0; p < 2; p++) {
        if (!g_slip.held[p]) {
            continue;
        }
        const int  i = g_slip.monster[p];
        const bool kept = p == 0 ? lockOnTargetFlag != 0 && lockOnTargetNo == i : g_combat.lock == i;

        if (!kept) {
            g_slip.held[p] = false;
            std::fprintf(stderr, "coop: test: lock slip: phase %d factor %d: player %d's lock let go %ld ticks after the move\n", phase, factor, p + 1,
                         after);
            continue;
        }
        if (in == 249) {
            std::fprintf(stderr, "coop: test: lock slip: phase %d factor %d: player %d's lock still held %ld ticks after the move\n", phase, factor,
                         p + 1, after);
            continue;
        }
        const float rate = p == 0 ? LockRangeRateOf(UserStatus->cur_chara) : LockRangeRateOf(g_partner.loaded_chara);

        Hold(i, p == 0 ? CharaMain.pos : g_partner.pos, (float) factor / 100.0f * NowMonstorUnit->monster[i].lock_range * rate);
    }
    if (after == 0) {
        std::fprintf(stderr, "coop: test: lock slip: phase %d factor %d: both monsters moved at link tick %lld\n", phase, factor,
                     (long long) NetplayLinkTick());
    }
}

} // namespace

// coop-lockon1118 (Jep: her lock-on is player 1's; p2-like-p1): each tick her lock is kept or let go by setTargetCursor's own rule, run
// for her body: LockOnKeep (awake, alive, lockable, within the monster's lock range times her member's reach, not stood on it, not 80
// above or below) and LockOnCountdown (slipping, it lets go 32 ticks on, with the refuse sound; back in reach, it holds again). Run where
// DunMainDraw runs his (no battle cleared, no monsters hidden, no demo), but in her step, on every copy alike: her lock and count are
// state the step reads. Her rule was her own: let go at once past 1.25 times the monster's lock range (any member) or not revealed.
// DC_COOP_MUTATE=lockon1118 (twin): that rule again.
void StepHerLock() {
    TestLockSlip();

    if (g_combat.lock < 0 || NowMonstorUnit == nullptr) {
        return;
    }

    // coop-downmenu (the developer's rc5b playtest, 11-19: "dead players can still lock onto enemies"): lying down she holds no lock, as retail's
    // death lets go of his (LockOffTargte, and its task never runs his lock-on again). Her keep rule had no down in it (retail's never
    // needed one), so a lock she held as she fell stayed, its corners and bar on her screen. DC_COOP_MUTATE=down_lock (twin).
    if (g_partner.down > 0 && !Mutated("down_lock")) { // (her step: she is on the floor; visible is set after it)
        g_combat.lock_shift_no = 0;
        g_combat.lock_shift_rot = 0;
        ReleaseLock("let go: she is down");
        return;
    }

    if (Mutated("lockon1118")) {
        if (!Lockable(g_combat.lock, 1.25f)) {
            ReleaseLock("lost");
        }
        return;
    }

    if (BtAllClear != 0 || DebugStatus[10] != 0 || autoDemo != 0 || CMonUnitHyde != 0) {
        return;
    }

    float     target[4];
    const int keep = LockOnKeep(g_partner.pos, g_combat.lock, LockRangeRateOf(g_partner.loaded_chara), target);

    if (LockOnCountdown(keep, &g_combat.lock_cursor) != 0) {
        SoundOwner sound(2, true); // (her refuse sound is hers, as her Circle's: coop-menuse)

        SndSePlay(MENU_SOUND_REFUSE, -1, 0);
        g_combat.lock_shift_no = 0;
        g_combat.lock_shift_rot = 0;
        std::fprintf(stderr, "coop: player 2's lock on monster %d slipped away at link tick %lld\n", g_combat.lock, (long long) NetplayLinkTick());
        ReleaseLock("lost");
    }
}

namespace {

// What Square does with an item: heals the partner, is thrown, or is not the partner's to use.
enum ItemUse { ITEM_USE_NONE, ITEM_USE_HEAL, ITEM_USE_THROW, ITEM_USE_DRINK, ITEM_USE_REPAIR, ITEM_USE_CURE, ITEM_USE_BOOST };

ItemUse UseOf(int item) {
    // coop-p2feather: retail's running item (ACTIVE_ITEM_FEATHER), held with Square, not used by one press (SpeedBoosted); her own
    // bag's slot spends it. DC_COOP_MUTATE=p2feather (twin): not hers, as before.
    if (item == ITEM_DRAN_S_FEATHER) {
        return OwnBags() && !Mutated("p2feather") ? ITEM_USE_BOOST : ITEM_USE_NONE;
    }

    switch (item) {
        case ITEM_REPAIR_POWDER:
            return ITEM_USE_REPAIR;
        case ITEM_STAMINA_DRINK:
        case ITEM_ANTIDOTE_DRINK:
        case ITEM_SOAP:
        case ITEM_MIGHTY_HEALING:
            return ITEM_USE_CURE;
        case ITEM_BOMB:
        case ITEM_STONE:
        case ITEM_THROBBING_CHERRY:
        case ITEM_GOOEY_PEACH:
        case ITEM_POISONOUS_APPLE:
            return ITEM_USE_THROW;
        case ITEM_MELLOW_BANANA:
            return ITEM_USE_HEAL;
    }

    if (item < ITEM_ATTACH_START || item > ITEM_FRUIT_OF_EDEN) {
        return ITEM_USE_NONE;
    }

    const ITEM_DATA &data = ITEM_LIST[item - ITEM_ATTACH_START];

    // coop-actorswap: with her throw on retail's path, everything retail's quick-use throws (CActiveItemPack::CheckStatusType's
    // ACTIVE_ITEM_ACTION: the gems and holy water too; Jep's playtest item 23, her quick-slot gem did nothing).
    if ((data.kind_flags & 2) != 0 && ActorPorted("throw")) {
        return ITEM_USE_THROW;
    }

    if ((data.kind_flags & ITEMKINDF_CONSUMABLE) != 0 && (data.use_flags & ITEMUSE_HEAL_HP) != 0) {
        return ITEM_USE_HEAL;
    }

    // Water, as usedActiveItem gives the player's: the partner drinks it itself.
    if ((data.kind_flags & ITEMKINDF_CONSUMABLE) != 0 && (data.use_flags & ITEMUSE_DRINK) != 0) {
        return ITEM_USE_DRINK;
    }

    return ITEM_USE_NONE;
}

// Life an item gives back, as usedActiveItem gives the player's.
int HealOf(int item) {
    return item == ITEM_MELLOW_BANANA ? 200 : ITEM_LIST[item - ITEM_ATTACH_START].vol;
}

void SpendItem(int item) {
    // coop-inventory: P2's own bag, from its selected quick-use slot first.
    if (!SpendFromSlot(item)) {
        DeleteItemAfterUseItem((short) item, &PartnerPack());
    }
}

bool ThrowItem(int item) {
    // coop-actorswap: retail's throw with her swapped in (ActorThrowItem): retail's item model, flight and landing effect.
    // DC_COOP_MUTATE=actorswap_throw_off: the mod's own throw below (twin).
    if (ActorPorted("throw")) {
        float yaw = g_combat.aiming ? g_combat.aim_yaw : Yaw();
        float to[4];
        bool  locked = LockPoint(to) && DistVector(to, g_partner.pos) <= kThrowReach;

        if (!locked) {
            to[0] = g_partner.pos[0] + kThrowAhead * sinf(yaw);
            to[1] = g_partner.pos[1];
            to[2] = g_partner.pos[2] + kThrowAhead * cosf(yaw);
        }

        return ActorThrowItem(1, item, to, locked) == 1;
    }

    for (Throw &thrown : g_combat.throws) {
        if (thrown.life > 0) {
            continue;
        }

        float yaw = g_combat.aiming ? g_combat.aim_yaw : Yaw();
        float to[4];

        if (!LockPoint(to) || DistVector(to, g_partner.pos) > kThrowReach) {
            to[0] = g_partner.pos[0] + kThrowAhead * sinf(yaw);
            to[1] = g_partner.pos[1];
            to[2] = g_partner.pos[2] + kThrowAhead * cosf(yaw);
        }

        float t = (float) kThrowTicks;

        thrown.pos[0] = g_partner.pos[0];
        thrown.pos[1] = g_partner.pos[1] + 12.0f;
        thrown.pos[2] = g_partner.pos[2];
        thrown.pos[3] = 1.0f;
        thrown.vector[0] = (to[0] - thrown.pos[0]) / t;
        thrown.vector[1] = (to[1] - thrown.pos[1] + 0.5f * kThrowGravity * t * t) / t;
        thrown.vector[2] = (to[2] - thrown.pos[2]) / t;
        thrown.vector[3] = 0.0f;
        thrown.item = item;
        thrown.life = kThrowTicks + 15;
        CoopPartnerSound(SE_THROW, thrown.pos, kCoopSoundThrow);
        return true;
    }

    return false;
}

// Lands a thrown item where it falls or strikes, as CActiveItemPack lands the player's.
void Land(Throw &thrown) {
    switch (thrown.item) {
        case ITEM_STONE:
            NowColData->Set(thrown.pos, 8, 5, 8.0f, 1.0f, 2, 2, 0, 0);
            break;
        case ITEM_GOOEY_PEACH:
            NowColData->Set(thrown.pos, 8, 5, 8.0f, 1.0f, 2, 2, 0x800, 0);
            break;
        case ITEM_THROBBING_CHERRY:
            NowColData->Set(thrown.pos, 2, 5, 8.0f, 1.0f, 2, 2, 0x100, 0);
            break;
        case ITEM_POISONOUS_APPLE:
            NowColData->Set(thrown.pos, 2, 5, 8.0f, 1.0f, 2, 2, 0x200, 0);
            break;
        default:
            SetBombEffect(thrown.pos, 3, (selectMapNo + 1) * 30, 1.0f);
            break;
    }

    std::printf("coop: P2 throw of item %d landed at (%.0f %.0f %.0f)\n", thrown.item, thrown.pos[0], thrown.pos[1], thrown.pos[2]);
    thrown.life = 0;
    g_combat.throws_landed++;
}

void StepThrows() {
    for (Throw &thrown : g_combat.throws) {
        if (thrown.life <= 0) {
            continue;
        }

        sceVu0FVECTOR hit_position;

        if (--thrown.life == 0 || checkCollision(hit_position, thrown.pos, thrown.vector, 2, 3.0f) != SHOT_COLLISION_NONE) {
            Land(thrown);
            continue;
        }

        thrown.pos[0] += thrown.vector[0];
        thrown.pos[1] += thrown.vector[1];
        thrown.pos[2] += thrown.vector[2];
        thrown.vector[1] -= kThrowGravity;
    }
}

// coop-playfix (Jep's 2026-10-31 Local playtest: "when P2 presses circle, they hear the noise that plays when you hit the back
// button in the main menu. Same with pressing square"): the field's Square with nothing it can use says nothing in retail
// (DunMoveChara's Square: no usable item, no sound); a menu's refusal is the menu's sound. `quick`: her field Square.
// DC_COOP_MUTATE=quick_refuse_sound (twin): the refusal's sound on her Square too, as before.
void Refuse(bool quick) {
    if (!quick || Mutated("quick_refuse_sound")) {
        SndSePlay(MENU_SOUND_REFUSE, -1, 0);
    }
}

// coop-p2throw (Jep's 2026-11-09 playtest: her quick-slot throw had no animation, no aim and went at once): BattleActionThlow's
// sequence for her. Square starts it: nothing locked, motion 26 holds while Square is held and the stick walks the landing point
// round (30 to 120 from her, the bomb cursor shows it); let go, motion 27 plays and the item leaves at the character's release
// frame, when the item is spent; locked on, Square goes straight to motion 27 at the lock. She stands through it (attacking).
// DC_COOP_MUTATE=throw_instant (twin): the old throw at the press.
constexpr float kThrowFrames[6] = {548.0f, 452.0f, 267.0f, 347.0f, 188.0f, 321.0f}; // BattleActionThlow's, per member
constexpr int   kThrowAimMotion = 26;
constexpr int   kThrowMotion = 27;
constexpr int   kThrowMaxTicks = 400; // (a motion that never reaches its frame lets go anyway)

bool ThrowAnimated() {
    return ActorPorted("throw") && !Mutated("item") && !Mutated("throw_instant") && HasMotion(kThrowMotion) && g_partner.loaded_chara >= 0 &&
           g_partner.loaded_chara < 6;
}

void ThrowMotion(int motion, int flags) {
    PlayMotion(motion, flags);
    g_partner.chara->motion_type.state.time = (float) g_partner.chara->motion_type.motion_info[motion].start;
}

void BeginThrow(int item) {
    float to[4];
    const bool locked = LockPoint(to) && DistVector(to, g_partner.pos) <= kThrowReach;
    const float yaw = Yaw();

    g_combat.throw_item = item;
    g_combat.throw_locked = locked;
    g_combat.throw_ticks = 0;
    g_partner.attacking = true;
    g_partner.attack_hit = false;
    g_partner.attack_ticks = 0;

    if (locked) {
        g_combat.throw_stage = 2;
        ThrowMotion(kThrowMotion, 4);
    } else {
        g_combat.throw_stage = 1;
        g_combat.throw_aim[0] = g_partner.pos[0] + 30.0f * sinf(yaw);
        g_combat.throw_aim[1] = g_partner.pos[1];
        g_combat.throw_aim[2] = g_partner.pos[2] + 30.0f * cosf(yaw);
        g_combat.throw_aim[3] = 1.0f;
        ThrowMotion(HasMotion(kThrowAimMotion) ? kThrowAimMotion : 0, 0);
    }

    std::fprintf(stderr, "coop-p2throw: begin item %d (%s) at link tick %lld, aim (%.0f %.0f)\n", item, locked ? "at her lock" : "aiming",
                 (long long) NetplayLinkTick(), g_combat.throw_aim[0], g_combat.throw_aim[2]);
}

void EndThrow() {
    g_combat.throw_stage = 0;
    g_combat.throw_item = -1;
    g_partner.attacking = false;
    PlayMotion(0, 0);
}

// The release: the item leaves her hand and is spent (BattleActionThlow's SpendActiveItem and SetThrowModel).
void ReleaseThrow() {
    const int item = g_combat.throw_item;
    float     to[4];
    bool      locked = g_combat.throw_locked && LockPoint(to);

    if (!locked) {
        std::memcpy(to, g_combat.throw_aim, sizeof to);
        if (g_combat.throw_locked) { // the lock went while she wound up: where she faces
            const float yaw = Yaw();

            to[0] = g_partner.pos[0] + kThrowAhead * sinf(yaw);
            to[1] = g_partner.pos[1];
            to[2] = g_partner.pos[2] + kThrowAhead * cosf(yaw);
        }
    }

    if (item >= 0 && ItemCount(item) > 0 && ActorThrowItem(1, item, to, locked) == 1) {
        SpendItem(item);
        g_combat.item_wait = 30;
        std::fprintf(stderr, "coop-p2throw: release item %d at link tick %lld, to (%.0f %.0f), %.0f from her%s\n", item, (long long) NetplayLinkTick(), to[0], to[2],
                     DistVector(to, g_partner.pos), locked ? ", locked" : "");
        std::printf("coop: P2 used item %d (throw, retail's wind-up), %d left, life %d/%d\n", item, ItemCount(item), Life(), MaxLife());
    }
}

void StepThrowAction() {
    CCharacter *partner = g_partner.chara;
    const MOTION_INFO &info = partner->motion_type.motion_info[kThrowMotion];

    g_combat.throw_ticks++;

    if (g_combat.throw_stage == 1) {
        if ((g_combat.buttons & kInputSquare) != 0 && g_combat.throw_ticks < kThrowMaxTicks) {
            // BattleActionThlow: the stick walks the landing point on, kept 30 to 120 from her.
            const float facing = SteerAngle(1, PartnerCameraAngleH()); // coop-wave3: the steer row
            float       aim[4] = {g_combat.throw_aim[0] + g_combat.stick_x * cosf(facing) + g_combat.stick_y * sinf(facing), g_combat.throw_aim[1],
                                  g_combat.throw_aim[2] + g_combat.stick_y * cosf(facing) - g_combat.stick_x * sinf(facing), 1.0f};
            const float away = DistVector(aim, g_partner.pos);

            if (!(away < 30.0f) && away <= 120.0f) {
                std::memcpy(g_combat.throw_aim, aim, sizeof aim);
            }

            partner->frame->SetRotation(0.0f, atan2f(g_combat.throw_aim[0] - g_partner.pos[0], g_combat.throw_aim[2] - g_partner.pos[2]), 0.0f);
            return;
        }

        g_combat.throw_stage = 2;
        g_combat.throw_ticks = 0;
        ThrowMotion(kThrowMotion, 4);
        return;
    }

    const float time = partner->motion_type.state.time;

    if (g_combat.throw_stage == 2) {
        if ((time >= kThrowFrames[g_partner.loaded_chara] && time <= (float) info.end) || g_combat.throw_ticks >= kThrowMaxTicks) {
            ReleaseThrow();
            g_combat.throw_stage = 3;
            g_combat.throw_ticks = 0;
        }
        return;
    }

    if (time >= (float) info.end - 2.0f || g_combat.throw_ticks >= kThrowMaxTicks) {
        EndThrow();
    }
}

void UseItem(int item, bool quick = false) {
    ItemUse use = UseOf(item);

    if (use == ITEM_USE_BOOST) {
        return; // coop-p2feather: the held Square runs it (CombatInput), a press alone does nothing
    }

    if (item < 0 || ItemCount(item) <= 0 || use == ITEM_USE_NONE || g_combat.item_wait > 0) {
        Refuse(quick);

        if (CoopDebug()) {
            std::printf("coop-debug: P2 item %d refused (%d in the pack, wait %d)\n", item, item < 0 ? 0 : ItemCount(item), g_combat.item_wait);
        }

        return;
    }

    // coop-actorswap: food, water and cures by retail's own rule and use with her swapped in as the main character
    // (ActorUseItem). DC_COOP_MUTATE=actorswap_items_off: the cases below (twin).
    if ((use == ITEM_USE_HEAL || use == ITEM_USE_DRINK || use == ITEM_USE_CURE) && ActorPorted("items") && !Mutated("item")) {
        const bool used = ActorUseItem(1, item, quick) == 1;

        if (!used) {
            std::printf("coop: P2 item %d refused by retail's rule\n", item);
            return;
        }

        SpendItem(item);
        g_combat.item_wait = 30;
        std::printf("coop: P2 used item %d (retail, swapped in), %d left, life %d/%d\n", item, ItemCount(item), Life(), MaxLife());
        return;
    }

    // coop-p2throw: her field Square throws by retail's sequence (BeginThrow); a menu's use and the parity rows keep the instant throw.
    if (use == ITEM_USE_THROW && quick && ThrowAnimated()) {
        BeginThrow(item);
        return;
    }

    if (use == ITEM_USE_CURE) {
        int  chara = g_partner.loaded_chara;
        s32 &ailments = UserStatus->ailments[chara];
        bool used = true;

        // checkItemUsed's and usedActiveItem's rules, for the partner's member.
        switch (item) {
            case ITEM_STAMINA_DRINK:
                PartnerSetAilment(AILMENT_STAMINA);
                SndSePlay(SE_POWER_UP, -1, 0);
                break;
            case ITEM_ANTIDOTE_DRINK:
                used = (ailments & AILMENT_POISON) != 0;
                break;
            case ITEM_SOAP:
                used = (ailments & AILMENT_GOO) != 0;
                break;
            default:
                used = (ailments & (AILMENT_FREEZE | AILMENT_POISON | AILMENT_CURSE | AILMENT_GOO)) != 0;
                break;
        }

        if (!used) {
            Refuse(quick);
            std::printf("coop: P2 item %d refused: nothing to cure\n", item);
            return;
        }

        if (item != ITEM_STAMINA_DRINK) {
            ailments = 0;
            UserStatus->ailment_frames[chara] = 0;
            PartnerVoiceSound(SE_CHARA_RECOVER); // coop-wellsound: her member's voice
        }
    } else if (use == ITEM_USE_REPAIR) {
        WEAPON_HAVE *weapon = (WEAPON_HAVE *) EquippedWeapon(g_partner.loaded_chara);

        // Repair Powder mends the partner's weapon to full, as the weapon menu's does a member's.
        if (weapon == nullptr || weapon->durability_f >= (float) weapon->durability) {
            Refuse(quick);
            std::printf("coop: P2 item %d refused: the weapon needs no repair\n", item);
            return;
        }

        weapon->durability_f = (float) weapon->durability;
        SndSePlay(SE_WEAPON_REPAIR, -1, 0);
    } else if (use == ITEM_USE_DRINK) {
        int chara = g_partner.loaded_chara;

        // usedActiveItem's rule (checkItemUsed): no drink while the gauge is within 0.2 of full.
        if (!(0.2f + UserStatus->water_now[chara] < UserStatus->water_max[chara])) {
            Refuse(quick);
            std::printf("coop: P2 item %d refused at full water\n", item);
            return;
        }

        UserStatus->AddDrink(chara, ITEM_LIST[item - ITEM_ATTACH_START].vol, 5.0f);
        PartnerVoiceSound(SE_CHARA_RECOVER); // coop-wellsound: her member's voice
    } else if (use == ITEM_USE_HEAL) {
        if (Life() >= MaxLife()) {
            Refuse(quick);
            std::printf("coop: P2 item %d refused at full life\n", item);
            return;
        }

        if (!Mutated("item")) {
            SetLife(std::min(MaxLife(), Life() + HealOf(item)));
        }

        PartnerVoiceSound(SE_CHARA_RECOVER); // coop-wellsound: her member's voice
    } else if (!Mutated("item") && !ThrowItem(item)) {
        Refuse(quick);
        return;
    }

    SpendItem(item);
    g_combat.item_wait = 30;
    std::printf("coop: P2 used item %d (%s), %d left, life %d/%d\n", item,
                use == ITEM_USE_HEAL     ? "heal"
                : use == ITEM_USE_DRINK  ? "drink"
                : use == ITEM_USE_REPAIR ? "repair"
                : use == ITEM_USE_CURE   ? "cure"
                                         : "throw",
                ItemCount(item),
                Life(), MaxLife());
}

// coop-actorswap: the items parity rows (DC_COOP_TEST_ITEM_PARITY=<item>[,<item>...]): once the partner has stood two
// seconds, each item is used once by player 1 the way retail's Square uses it (checkItemUsed on a quick-use slot, then
// usedActiveItem) and once by player 2 the way her Square does (UseItem), each from the same start for its member (half
// life, half water, poisoned), and what it changed is logged, then everything is put back:
// "coop-parity: item I player N (member M): used U, life +L (full F), water +W, ailments A". "coop-parity: done" after the last.
// coop-actorswap: the throw parity rows (DC_COOP_TEST_THROW_PARITY=<item>[,<item>...]): once the partner has stood two
// seconds, each item is thrown by player 1 (retail's thrown model from his hand: ActorThrowItem as the resident) and then by
// player 2 (her Square: UseItem), each 60 ahead of the thrower, one after the other, and each throw is followed until
// retail's item pool lands it: "coop-parity: throw I player N: thrown U (slot S, item C), landed L after T ticks, spent P".
// "coop-parity: throws done" after the last, "throws settled" three seconds later.
// coop-actorswap: the wear parity rows (DC_COOP_TEST_WEAR=1): once the partner has stood two seconds, each player's equipped
// weapon is worn once by 1.0 from just above a tenth of its durability, from just above a twentieth, and from 0.1 (it
// breaks), player 1's by retail's wear (BattleSubWeaponDmg) and player 2's by hers (PartnerWeaponWear), each put back after:
// "coop-parity: wear CASE player N (member M): result R, message G, durability D -> E, item I -> J". "coop-parity: wear done".
void WearParityTick() {
    static const bool on = TestEnvOn("DC_COOP_TEST_WEAR");
    static int        up = 0;

    if (!on || up < 0 || UserStatus == nullptr || g_partner.loaded_chara < 0 || ++up < 120) {
        return;
    }

    up = -1;

    for (const auto &[name, fraction] : {std::pair{"tenth", 0.1f}, std::pair{"twentieth", 0.05f}, std::pair{"break", 0.0f}}) {
        for (int player = 1; player <= 2; player++) {
            const int    chara = player == 1 ? UserStatus->cur_chara : g_partner.loaded_chara;
            WEAPON_HAVE *weapon = &UserStatus->chara_weapons[chara][(int) UserStatus->equipped_weapon_slot[chara]];
            const WEAPON_HAVE kept = *weapon;
            const s8          slot = UserStatus->equipped_weapon_slot[chara];
            const s32         message = DngMessMan.message, timer = DngMessMan.timer;

            weapon->durability_f = fraction > 0.0f ? fraction * weapon->durability + 0.05f : 0.1f;
            const float from = weapon->durability_f;
            const int   item = weapon->item_no;

            DngMessMan.message = 0;
            const int result = player == 1 ? BattleSubWeaponDmg(1.0f, 0) : PartnerWeaponWear(1.0f, 0);
            const WEAPON_HAVE &now = UserStatus->chara_weapons[chara][(int) UserStatus->equipped_weapon_slot[chara]];

            std::fprintf(stderr, "coop-parity: wear %s player %d (member %d): result %d, message %d, durability %.2f -> %.2f, item %d -> %d\n", name,
                         player, chara, result, (int) DngMessMan.message, from, now.durability_f, item, now.item_no);

            *weapon = kept;
            UserStatus->equipped_weapon_slot[chara] = slot;
            DngMessMan.message = message;
            DngMessMan.timer = timer;
        }
    }

    std::fprintf(stderr, "coop-parity: wear done\n");
}

void ThrowParityTick() {
    static const char *spec = TestEnv("DC_COOP_TEST_THROW_PARITY");
    static int         up = 0;
    static const char *at = nullptr;
    static int         item = -1, player = 0, slot = -1, ticks = 0, had = 0, kind = -1;

    if (spec == nullptr || UserStatus == nullptr || g_partner.loaded_chara < 0 || activeItem.models == nullptr) {
        return;
    }
    // Three seconds after the last throw (three lockstep checksums on a linked pair): "coop-parity: throws settled".
    if (up < 0) {
        if (--up == -180) {
            std::fprintf(stderr, "coop-parity: throws settled\n");
        }
        return;
    }
    if (up < 120) {
        if (++up == 120) {
            at = spec;
        }
        return;
    }

    CMainItemModel *pool = activeItem.models;

    // A throw under way: until its model slot is free again (landed), or 300 ticks.
    if (item >= 0 && player > 0) {
        ticks++;
        if (slot >= 0 && pool->model[slot] == ITEM_MODEL_THROWN && ticks < 300) {
            return;
        }

        const int spent = player == 2 ? had - ItemCount(item) : 0;

        std::fprintf(stderr, "coop-parity: throw %d player %d: thrown %d (slot %d, item %d), landed %d after %d ticks, spent %d\n", item, player,
                     slot >= 0 ? 1 : 0, slot, kind, slot >= 0 && pool->model[slot] != ITEM_MODEL_THROWN ? 1 : 0, ticks, spent);
        if (player == 1) {
            player = 2;
        } else {
            player = 0;
            item = -1;
        }
        slot = -1;
    }

    if (item < 0) {
        char *end = nullptr;

        item = at != nullptr ? (int) std::strtol(at, &end, 10) : -1;
        if (at == nullptr || end == at) {
            std::fprintf(stderr, "coop-parity: throws done\n");
            up = -1;
            return;
        }
        at = *end == ',' ? end + 1 : end;
        player = 1;
    }

    // The thrower's next throw: 60 ahead of where it faces, the slot it takes noted (the first thrown slot it adds).
    int before[16];

    std::memcpy(before, pool->model, sizeof before);
    ticks = 0;
    kind = -1;
    if (player == 1) {
        sceVu0FVECTOR turned;

        CharaMain.frame->GetRotation(turned);
        const float yaw = turned[1];
        const float to[3] = {CharaMain.pos[0] + 60.0f * sinf(yaw), CharaMain.pos[1], CharaMain.pos[2] + 60.0f * cosf(yaw)};

        ActorThrowItem(0, item, to, false);
    } else {
        PartnerGetItem(item, 1, false);
        had = ItemCount(item);
        g_combat.item_wait = 0;
        UseItem(item);
    }
    for (int i = 0; i < 16; i++) {
        if (pool->model[i] == ITEM_MODEL_THROWN && before[i] != ITEM_MODEL_THROWN) {
            slot = i;
            kind = pool->model_cash[i] >= 0 ? pool->cash_item[pool->model_cash[i]] : -1;
            break;
        }
    }
}

void ItemParityTick() {
    static const char *spec = TestEnv("DC_COOP_TEST_ITEM_PARITY");
    static int         up = 0;

    if (spec == nullptr || up < 0 || UserStatus == nullptr || g_partner.loaded_chara < 0 || ++up < 120) {
        return;
    }

    up = -1;

    struct Out {
        int   used, life;
        float water;
        int   ailments;
        int   full;
    };
    // The member's start, and its outcome as targets (a gauge stepping there counts as there).
    const auto start = [](int chara) {
        UserStatus->hp[chara] = UserStatus->next_hp[chara] = (s16) (UserStatus->max_hp[chara] / 2);
        UserStatus->life_step[chara] = 0;
        UserStatus->water_now[chara] = UserStatus->water_max[chara] / 2.0f;
        UserStatus->drink_next[chara] = (s16) UserStatus->water_now[chara];
        UserStatus->drink_step[chara] = 0;
        UserStatus->ailments[chara] = AILMENT_POISON;
        UserStatus->ailment_frames[chara] = 0;
    };
    const auto outcome = [](int chara, int used) {
        const int   life = UserStatus->life_step[chara] != 0 ? UserStatus->next_hp[chara] : UserStatus->hp[chara];
        const float water = UserStatus->drink_step[chara] != 0 ? (float) UserStatus->drink_next[chara] : UserStatus->water_now[chara];

        return Out{used, life - UserStatus->max_hp[chara] / 2, water - UserStatus->water_max[chara] / 2.0f, UserStatus->ailments[chara],
                   life >= UserStatus->max_hp[chara] ? 1 : 0};
    };

    for (const char *at = spec; *at != '\0';) {
        char     *end = nullptr;
        const int item = (int) std::strtol(at, &end, 10);

        if (end == at) {
            break;
        }
        at = *end == ',' ? end + 1 : end;

        const CUserStatus status = *UserStatus;
        const ITEM_PACK   bag = PartnerPack();
        int               stand_in[6];

        std::memcpy(stand_in, g_partner.stand_in_life, sizeof stand_in);

        // Player 1: retail's Square, his own member.
        const int one = UserStatus->cur_chara;

        start(one);
        s16 &slot = UserStatus->item_pack.quick_item_slot[0];
        const s16 kept = slot;

        slot = (s16) item;
        const int used_one = checkItemUsed(0);
        slot = kept;
        if (used_one != 0) {
            usedActiveItem(UserStatus, item);
        }
        const Out p1 = outcome(one, used_one);

        *UserStatus = status;

        // Player 2: her Square, her member.
        const int two = g_partner.loaded_chara;
        const int wait = g_combat.item_wait;

        start(two);
        PartnerGetItem(item, 1, false);
        g_combat.item_wait = 0;
        const int had = ItemCount(item);
        UseItem(item);
        const Out p2 = outcome(two, ItemCount(item) < had ? 1 : 0);

        g_combat.item_wait = wait;
        *UserStatus = status;
        PartnerPack() = bag;
        std::memcpy(g_partner.stand_in_life, stand_in, sizeof stand_in);

        for (const auto &[number, chara, o] : {std::tuple{1, one, p1}, std::tuple{2, two, p2}}) {
            std::fprintf(stderr, "coop-parity: item %d player %d (member %d): used %d, life %+d (full %d), water %+.1f, ailments %x\n", item, number,
                         chara, o.used, o.life, o.full, o.water, o.ailments);
        }
    }

    std::fprintf(stderr, "coop-parity: done\n");
}

void Equip(int slot) {
    int chara = g_partner.loaded_chara;

    if (UserStatus->equipped_weapon_slot[chara] == slot) {
        return;
    }

    // coop: gaps2
    // The player's weapon menu refuses a change in a floor's no-weapon-change zone and while the
    // member is cursed (battlemenu.cpp's WepMenu warnings 4 and 3); so does pad 2's.
    // DC_COOP_MUTATE=weaponzone lets it change, for the test's twin.
    if (!Mutated("weaponzone") &&
        (UserStatus->res_limit_zone_current == RES_LIMIT_ZONE_NO_WEAPON_CHANGE || (UserStatus->ailments[chara] & AILMENT_CURSE) != 0)) {
        SndSePlay(MENU_SOUND_REFUSE, -1, 0);
        std::printf("coop: P2's weapon change refused (zone %d, ailments %#x)\n", (int) UserStatus->res_limit_zone_current,
                    (unsigned) UserStatus->ailments[chara]);
        return;
    }

    UserStatus->equipped_weapon_slot[chara] = (s8) slot;
    SndSePlay(MENU_SOUND_CONFIRM, -1, 0);

    // The hand's model follows; the damage and the HUD already have the new weapon if it cannot.
    bool shown = !Mutated("weapon") && ReloadWeapon();

    if (shown) {
        g_combat.reloads++;
    }

    // The hand has this weapon now (or, with the weapon twin, is left as it was): the every-tick check
    // (StepThirst) is for changes made elsewhere.
    g_combat.held_item = UserStatus->chara_weapons[chara][slot].item_no;
    g_combat.held_slot = slot;

    std::printf("coop: P2 weapon slot %d (item %d, attack %d)%s\n", slot, UserStatus->chara_weapons[chara][slot].item_no, Damage(),
                shown ? "" : ", hand model unchanged");
}

// The panel's buttons: Up and Down the row, Left and Right the page, Cross equips or uses, Triangle
// or Circle closes.
void PanelInput(std::uint16_t pressed) {
    int rows[32];
    int count = g_combat.page == 0 ? WeaponSlots(rows, 11) : g_combat.page == 1 ? UsableItems(rows, 32) : BagItems(rows, 32); // coop-inventory

    if ((pressed & (kInputTriangle | kInputCircle)) != 0) {
        g_combat.panel = false;
        g_combat.circle_ticks = kLockReleaseTicks;
        SndSePlay(MENU_SOUND_REFUSE, -1, 0);
        return;
    }

    if ((pressed & (kInputLeft | kInputRight)) != 0) {
        // coop-inventory: a third page, the Bag, while the players have a bag each.
        g_combat.page = (g_combat.page + ((pressed & kInputRight) != 0 ? 1 : PanelPages() - 1)) % PanelPages();
        g_combat.row = 0;
        SndSePlay(MENU_SOUND_CURSOR, -1, 0);
        return;
    }

    if (count > 0 && (pressed & (kInputUp | kInputDown)) != 0) {
        g_combat.row = (g_combat.row + ((pressed & kInputDown) != 0 ? 1 : count - 1)) % count;
        SndSePlay(MENU_SOUND_CURSOR, -1, 0);
    }

    g_combat.row = count > 0 ? std::clamp(g_combat.row, 0, count - 1) : 0;

    // coop-inventory: Square on the Bag page puts the item in P2's selected quick-use slot.
    if (count > 0 && g_combat.page == 2 && (pressed & kInputSquare) != 0 && OwnBags()) {
        PartnerSlotAssign(rows[g_combat.row]);
        return;
    }

    // coop-inventory: Square on the Items page, and Cross on the Bag page, send the item to player 1.
    if (count > 0 && g_combat.page >= 1 && (pressed & (g_combat.page == 1 ? kInputSquare : kInputCross)) != 0 && OwnBags()) {
        SendItem(2, rows[g_combat.row]);
        return;
    }

    if (count > 0 && (pressed & kInputCross) != 0) {
        if (g_combat.page == 0) {
            Equip(rows[g_combat.row]);
        } else {
            g_combat.item = rows[g_combat.row];
            UseItem(rows[g_combat.row]);
        }
    }
}

// A new floor numbers its monsters afresh: what the partner held from the last one goes.
void CheckFloor() {
    int key = selectMapNo * 256 + UserStatus->cur_floor;

    if (key == g_floor_key) {
        return;
    }

    g_floor_key = key;
    UnloadRing();
    g_combat.lock = -1;
    g_combat.charging = false;
    g_combat.aiming = false;
    g_combat.panel = false;

    for (Throw &thrown : g_combat.throws) {
        thrown.life = 0;
    }
}

// Holds the swing at its charge: the levels come with time, and letting go of Cross releases the
// blow (or the shot) the level scales. Returns once the release has started the next motion.
void StepCharge() {
    CCharacter *partner = g_partner.chara;
    int         chara = g_partner.loaded_chara;

    g_combat.charge_ticks = std::min(g_combat.charge_ticks + 1, kChargeMost);

    if (g_combat.charge_level == 0 && g_combat.charge_ticks >= kChargeLevel1) {
        g_combat.charge_level = 1;
        SndSePlay(SE_LOCK_ON, -1, 0);
        std::printf("coop: P2 charge level 1\n");
    }

    if (g_combat.charge_level == 1 && g_combat.charge_ticks >= kChargeLevel2 && UserStatus->skill_owned[chara] != 0) {
        g_combat.charge_level = 2;
        SndSePlay(SE_LOCK_ON, -1, 0);
        std::printf("coop: P2 charge level 2\n");
    }

    // A swing without a charge motion of its own waits on its last frame.
    if (g_partner.motion == g_partner.attack_motion) {
        partner->motion_type.state.time = (float) partner->motion_type.motion_info[g_partner.attack_motion].end - 1.0f;
    }

    if (g_combat.held) {
        return;
    }

    int  next = g_partner.attack_stage + 1;
    bool staged = next < 2 && kTapMotions[chara][next] >= 0 && HasMotion(kTapMotions[chara][next]);

    g_combat.charging = false;
    g_combat.scale = g_combat.charge_level > 0 && !Mutated("charge") ? kChargeScale : 1.0f;
    g_partner.attack_ticks = 0;

    if (g_combat.charge_level == 2) {
        MarkHit(NowColData->Set(g_partner.pos, Damage(), 1, kWaveRadius, 0.0f, 2, 2, 0, 0));
        g_combat.waves++;
        g_combat.last_wave_damage = Damage();
        std::printf("coop: P2 charge wave, %d damage\n", Damage());
    }

    if (g_combat.charge_level > 0) {
        // A charged release wears the weapon as ToanKey_Play's does (2.0, or 3.0 at the second level).
        PartnerWeaponWear(g_combat.charge_level >= 2 ? 3.0f : 2.0f, 0);
        g_combat.charges++;
        std::printf("coop: P2 charged release %d, level %d, %d damage\n", g_combat.charges, g_combat.charge_level, Damage());
    }

    if (chara == CHARA_RUBY && HasMotion(kRubyChargedCast)) {
        // Ruby lets her charge go with her charged cast, as BattleActionPlay_Ruby goes from 14 to 16.
        g_combat.charged_blow = true;
        g_partner.attack_hit = false;
        g_partner.swings++;
        g_partner.since_attack = 0;
        g_partner.attack_motion = kRubyChargedCast;
        PlayMotion(kRubyChargedCast, 4);
        partner->motion_type.state.time = (float) partner->motion_type.motion_info[kRubyChargedCast].start;
    } else if (staged) {
        StartStage(next);
    } else if (g_combat.charge_level > 0) {
        g_combat.charged_blow = true;
        g_partner.attack_hit = false;
        g_partner.swings++;
        g_partner.since_attack = 0;

        if (HasMotion(kTapMotions[chara][0])) {
            StartStage(g_partner.attack_stage);
        } else {
            PlayMotion(1, 4);
        }
    } else {
        g_partner.attacking = false;
        PlayMotion(0, 0);
    }
}

void StartCharge() {
    int chara = g_partner.loaded_chara;

    g_combat.charging = true;
    g_combat.charge_ticks = 0;
    g_combat.charge_level = 0;
    g_combat.gauge = 0.0f; // a charge spends the whole gauge, as the player's (ToanKey_Play's action 14)

    if (chara == CHARA_XIAO && HasMotion(kXiaoHoldMotion)) {
        PlayMotion(kXiaoHoldMotion, 0);
    } else if (chara == CHARA_RUBY && HasMotion(kRubyChargeMotion)) {
        PlayMotion(kRubyChargeMotion, 0);
    } else if (!Ranged(chara) && HasMotion(kChargeMotion)) {
        PlayMotion(kChargeMotion, 0);
    }
}

} // namespace

const WEAPON_HAVE *EquippedWeapon(int chara) {
    if (chara < 0 || chara >= 6) {
        return nullptr;
    }

    int slot = UserStatus->equipped_weapon_slot[chara];

    if (slot < 0 || slot >= 11 || UserStatus->chara_weapons[chara][slot].item_no <= 0) {
        return nullptr;
    }

    return &UserStatus->chara_weapons[chara][slot];
}

// The weapon a member starts with: a stand-in that has not joined swings that one's base attack.
int StarterWeapon(int chara) {
    static const int kStarter[6] = {ITEM_WEAPON_DAGGER,    ITEM_WEAPON_WOODEN_SLINGSHOT, ITEM_WEAPON_MALLET,
                                    ITEM_WEAPON_GOLD_RING, ITEM_WEAPON_FIGHTING_STICK,   ITEM_WEAPON_MACHINE_GUN};

    return chara >= 0 && chara < 6 ? kStarter[chara] : -1;
}

// A blow's damage before the charge: the partner's own weapon as the player's key or battle action
// takes it (ToanKey_Play and BattleActionPlay_Jinn the attack, BattleActionShotRuby and
// BattleActionPlay_Ozumond half the attack and half the magic), or the starting weapon's for a
// member with none. Never the controlled character's weapon (NowWeaponHave).
int BaseAttack(int chara) {
    if (Mutated("stats")) {
        chara = UserStatus->cur_chara;
    }

    const WEAPON_HAVE *weapon = EquippedWeapon(chara);
    int                attack = 0;
    int                magic = 0;

    if (weapon != nullptr) {
        attack = weapon->attack;
        magic = weapon->magic;
    } else if (const COM_ITEM_INFO *info = GetCommonItemInfo(StarterWeapon(chara))) {
        // WeaponList is indexed by the item's table index, as the menus look it up (menu_draw.cpp).
        if (const WEAPON_DATA *data = GetWeaponDataInfo((s32) info->index)) {
            attack = data->attack;
            magic = data->magic;
        }
    }

    int damage = chara == CHARA_RUBY || chara == CHARA_OSMOND ? (attack >> 1) + (magic >> 1) : attack;

    return damage > 0 ? damage : 1;
}

// Stamina (AILMENT_STAMINA, from a Stamina Drink) doubles a member's damage and its guard, as
// StatusErrCheck does the player's; the partner's comes from its own ailments in the save.
bool Stamina(int chara) {
    return chara >= 0 && chara < 6 && (UserStatus->ailments[chara] & AILMENT_STAMINA) != 0;
}

// The defence a monster's hit on the partner is reduced by: its own, doubled by its own stamina.
int Guard(int chara) {
    if (Mutated("stats")) {
        chara = UserStatus->cur_chara;
    }

    return UserStatus->defense[chara] * (Stamina(chara) ? 2 : 1);
}

int Damage() {
    int chara = g_partner.loaded_chara;
    int damage = BaseAttack(chara) * (Stamina(chara) ? 2 : 1) + (Mutated("desync") ? 1 : 0);

    return (int) ((float) damage * g_combat.scale);
}

void PartnerSetAilment(int status) {
    int chara = g_partner.loaded_chara;

    if (chara < 0 || chara >= 6 || Mutated("ailment")) {
        return;
    }

    s32 &ailments = UserStatus->ailments[chara];

    // BtSetStatusErr's rules, for the partner's member.
    switch (status) {
        case AILMENT_FREEZE:
            ailments |= status;
            ailments &= ~(AILMENT_STAMINA | AILMENT_POISON | AILMENT_GOO);
            UserStatus->ailment_frames[chara] = 300;
            SndSePlay(SE_STATUS_AILMENT, -1, 0);
            break;
        case AILMENT_STAMINA:
            if ((ailments & AILMENT_FREEZE) == 0) {
                ailments |= status;
                UserStatus->ailment_frames[chara] = 1800;
            }

            break;
        case AILMENT_POISON:
            if ((ailments & (AILMENT_FREEZE | AILMENT_STAMINA)) == 0) {
                ailments |= status;
                ailments &= ~AILMENT_GOO;
                SndSePlay(SE_STATUS_AILMENT, -1, 0);
            }

            break;
        case AILMENT_CURSE:
            ailments |= status;
            SndSePlay(SE_STATUS_AILMENT, -1, 0);
            break;
        case AILMENT_GOO:
            if ((ailments & (AILMENT_FREEZE | AILMENT_STAMINA | AILMENT_POISON)) == 0) {
                ailments |= status;
                SndSePlay(SE_STATUS_AILMENT, -1, 0);
            }

            break;
    }

    std::printf("coop: P2 ailment %#x, now %#x\n", status, (unsigned) ailments);
}

bool PartnerAilment(int status) {
    int chara = g_partner.loaded_chara;

    return chara >= 0 && chara < 6 && (UserStatus->ailments[chara] & status) != 0;
}

bool PartnerHit(int owner) {
    return Enabled() && g_partner.visible && owner >= 0 && owner == g_partner.loaded_chara && owner != UserStatus->cur_chara;
}

int PartnerWeaponWear(float amount, int kind) {
    static const int kDefault[6] = {ITEM_WEAPON_DAGGER_BROKEN,     ITEM_WEAPON_WOODENSLINGSHOT_BROKEN, ITEM_WEAPON_MALLET_BROKEN,
                                    ITEM_WEAPON_GOLD_RING_BROKEN, ITEM_WEAPON_FIGHTING_STICK_BROKEN,  ITEM_WEAPON_MACHINE_GUN_BROKEN};
    int chara = g_partner.loaded_chara;

    if (chara < 0 || chara >= 6 || EquippedWeapon(chara) == nullptr || Mutated("wear")) {
        return 0;
    }

    // coop-actorswap: retail's own wear (BattleSubWeaponDmg) with her swapped in: her weapon worn by its attachments'
    // endurance, its auto-repair powder, its break and swap, and the "about to break" messages at a tenth and a twentieth
    // (Jep's playtest item 19: she had no warning), shown on her screen. DC_COOP_MUTATE=actorswap_wear_off: the copy below (twin).
    if (ActorPorted("wear")) {
        const WEAPON_HAVE &before = UserStatus->chara_weapons[chara][(int) UserStatus->equipped_weapon_slot[chara]];
        const int          item = before.item_no;
        const float        durability = before.durability_f;
        int                broke = 0;

        if (ActorSwap swap(1, "wear"); swap.in()) {
            broke = BattleSubWeaponDmg(amount, kind);
        }

        const WEAPON_HAVE &now = UserStatus->chara_weapons[chara][(int) UserStatus->equipped_weapon_slot[chara]];

        if (now.durability_f != durability || now.item_no != item) {
            g_combat.wear_ticks++;
        }
        if (broke != 0) {
            g_combat.breaks++;
            std::printf("coop: P2's weapon %d broke\n", item);
        }

        return broke;
    }

    WEAPON_HAVE *weapon = &UserStatus->chara_weapons[chara][(int) UserStatus->equipped_weapon_slot[chara]];
    int          default_weapon = kDefault[chara];

    if (weapon->item_no == ITEM_WEAPON_SERPENT_SWORD && SaveData->GetGameFlag(0x30) == 0) {
        return 0;
    }

    if ((weapon->flags & 0x200) != 0) {
        amount *= 0.5f;
    }

    if ((weapon->flags & 0x100) != 0) {
        amount *= 2.0f;
    }

    if (weapon->item_no == default_weapon) {
        return 0;
    }

    weapon->durability_f -= (1.5f - 0.01f * weapon->endurance) * amount + 0.1f * kind;
    g_combat.wear_ticks++;

    if (weapon->durability_f <= 0.0f) {
        int powder = ((CDngStatusData *) UserStatus)->CheckActItemSlot(ITEM_AUTO_REPAIR_POWDER);

        if (powder != -1) {
            DelActiveItem(powder + 1);
            weapon->durability_f = weapon->durability;
            DngMessMan.message = 0xBC;
            DngMessMan.timer = 0xF0;
            DngMessMan.steev_window = false;
            DngMesBy(2); // coop-itemprompt: on her screen only
            SndSePlay(SE_WEAPON_REPAIR, -1, 0);
            return 0;
        }
    }

    if (weapon->durability_f > 0.0f) {
        return 0;
    }

    // Broken, as BattleSubWeaponDmg breaks the player's: the member's first weapon turns into its broken
    // default; any other goes, and the default (or the first weapon) is taken up.
    weapon->durability_f = 0.0f;
    SndSePlay(SE_WEAPON_BREAK, -1, 0);
    g_combat.breaks++;
    std::printf("coop: P2's weapon %d broke\n", weapon->item_no);

    if (weapon->item_no == default_weapon + 1) {
        DngMessMan.insert_mes_1 = GetCommonItemDataSystemMsg(weapon->item_no);
        DngMessMan.insert_value_1 = weapon->level;
        DngMessMan.message = 0xA1;
        DngMessMan.timer = 0x1E0;
        DngMessMan.steev_window = false;
        DngMesBy(2); // coop-itemprompt: on her screen only
        WepDataListToHaveCopy(default_weapon, weapon);
        weapon->durability_f = 0.0f;
        return 1;
    }

    WEAPON_HAVE *replacement = UserStatus->chara_weapons[chara];

    for (int slot = 0; slot < 10; replacement++, slot++) {
        if (replacement->item_no == default_weapon || replacement->item_no == default_weapon + 1) {
            UserStatus->equipped_weapon_slot[chara] = (s8) slot;
            DngMessMan.insert_mes_1 = GetCommonItemDataSystemMsg(weapon->item_no);
            DngMessMan.insert_value_1 = weapon->level;
            DngMessMan.insert_mes_2 = GetCommonItemDataSystemMsg(replacement->item_no);
            DngMessMan.insert_value_2 = replacement->level;
            DngMessMan.message = 0xA0;
            DngMessMan.timer = 0x1E0;
            DngMessMan.steev_window = false;
            DngMesBy(2); // coop-itemprompt: on her screen only

            if (chara == CHARA_RUBY && replacement->item_no == default_weapon + 1) {
                replacement->best_elem = 0;
            }

            weapon->item_no = -1;
            return 2;
        }
    }

    weapon->item_no = -1;
    return 2;
}

// Gives one of the partner's hits its own weapon's element, monster bonuses and special flags, as
// basic_damage gives the player's theirs, and its character as the owner, as each character's own
// attacks carry theirs, so a monster it finishes records it as last_attacker and StepKills gives its
// weapon the ABS. attack_no stays -1, so no weapon wears from it (CheckDmg wears the controlled
// character's on attack 0); the water-on-kill flag (0x10) is left to StepKills, since the game's own
// kill code would give the water to the controlled character.
void MarkHit(int hit) {
    const WEAPON_HAVE *weapon = EquippedWeapon(g_partner.loaded_chara);

    if (hit < 0 || hit >= 96) {
        return;
    }

    if (!Mutated("abs")) {
        NowColData->hit[hit].owner = g_partner.loaded_chara;
    }

    if (weapon == nullptr) {
        return;
    }

    NowColData->hit[hit].vs_monster = (char *) weapon->vs_monster;
    NowColData->hit[hit].flags = GetWeaponElementAttr(weapon->best_elem);
    NowColData->hit[hit].weapon_flags = weapon->flags & ~0x10;
}

namespace {

// A monster the partner finished (last_attacker its character) gives the partner's weapon its ABS,
// as the game's kill code gives the controlled character's (CMonstorUnit, monstorunit.cpp ~2165): the
// monster's exp, doubled on the back dungeon and by a fifth more with weapon flag 0x2000, none for a
// member's default weapon or the Serpent Sword before its flag, taken away in the no-weapon-change
// zone, up to the weapon's next build-up; and the water a 0x10 weapon gives on a kill goes to the
// partner. Each monster once, until it is alive again.
void StepKills() {
    int chara = g_partner.loaded_chara;

    if (NowMonstorUnit == nullptr || chara < 0) {
        return;
    }

    for (int i = 0; i < 16; i++) {
        MONSTOR &monster = NowMonstorUnit->monster[i];

        if (monster.hp > 0) {
            g_combat.credited[i] = false;
            continue;
        }

        // Credited when the death has played out (state -1), as the game credits the player's.
        if (g_combat.credited[i] || monster.state != -1 || monster.last_attacker != chara || chara == UserStatus->cur_chara) {
            continue;
        }

        g_combat.credited[i] = true;

        int slot = UserStatus->equipped_weapon_slot[chara];

        if (slot < 0 || slot >= 11 || UserStatus->chara_weapons[chara][slot].item_no <= 0) {
            continue;
        }

        WEAPON_HAVE *weapon = &UserStatus->chara_weapons[chara][slot];
        bool         no_exp = ((CDngStatusData *) UserStatus)->CheckDefaultWeapon(chara) == 0 ||
                      (weapon->item_no == ITEM_WEAPON_SERPENT_SWORD && SaveData->GetGameFlag(0x30) == 0);
        int          exp = monster.exp;

        if (NowMonstorUnit->back_dungeon != 0) {
            exp *= 2;
        }

        if ((weapon->flags & 0x2000) != 0) {
            exp = (int) ((float) exp * 1.2f);
        }

        if (!no_exp) {
            int max_exp = GetWeaponMaxExp(weapon);

            if (UserStatus->res_limit_zone_current == RES_LIMIT_ZONE_NO_WEAPON_CHANGE) {
                weapon->experience = (s16) std::max(0, weapon->experience - exp);
            } else if (weapon->experience < max_exp) {
                weapon->experience = (s16) std::min(max_exp, weapon->experience + exp);

                if (weapon->experience >= max_exp) {
                    DngMessMan.insert_mes_1 = GetCommonItemDataSystemMsg(weapon->item_no);
                    DngMessMan.insert_value_1 = weapon->level;
                    DngMessMan.message = 150;
                    DngMessMan.timer = 480;
                    DngMessMan.steev_window = false;
                    DngMesBy(2); // coop-itemprompt: on her screen only
                }
            }
        }

        if ((weapon->flags & 0x10) != 0) {
            UserStatus->AddDrink(chara, 10, 255.0f);
        }

        g_combat.kills++;
        g_combat.last_kill_exp = no_exp ? 0 : exp;
        std::printf("coop: P2 killed monster %d: weapon %d ABS +%d -> %d\n", i, weapon->item_no, no_exp ? 0 : exp, weapon->experience);
    }
}

} // namespace

bool Ranged(int chara) {
    return chara == CHARA_XIAO || chara == CHARA_RUBY || chara == CHARA_OSMOND;
}

// coop-swapcombo (Jep's rc1 playtest, 2026-11-12: after a trade, player 2 as Toan "cannot do Toan's full weapon combo and cannot do
// a charge attack"). Not the trade: player 2's swing was one motion a press for every member (a press mid-swing was dropped,
// Toan's table held only 0x24) and her charged release played that swing again, for a lobby pick too. Each melee member's
// chain and charge here are its retail key's (charakey.cpp; the player runs them from DunMoveChara), on her own state: a press
// in a stage with gauge >= need queues the next stage, which spends cost of the gauge when it starts (ToanKey_On /
// _Play: 20 and 25, five stages 0x24 to 0x28, the fifth's blow damage * 1.8; UngagaKey: 40 and 20, 0x25 to 0x27; GoroKey:
// one swing on a full gauge, which it spends). The first stage held to its end (Goro's: to its motion time 95) goes to
// the charge pose; the first level comes after level1 ticks there, and letting go lands the charged blow in retail's
// release motions: Toan's leap (0xF, then the strike 3, its landing 0x11; with the second level his rising slash 0x18
// and the wave), Goro's smash (14), Ungaga's 0xF. A release short of the first level ends the attack (Goro's swings
// again for 15 of the gauge).
// Why not ToanKey_Play itself under an actor swap (p2-like-p1): it runs on BtActStatus, NowWeapon's dcol frame, the jump
// physics and the lock flag of DunMoveChara's player, none of which the actor swap carries yet (coop_actor.hpp); her
// step keeps its own blow (MeleeBlow, one hit a stage in front of her, as her tap's always was) and leaves out the leap's
// air time (0x10, 0x19: they wait on the player's jump physics) and Ungaga's held-pose shots.
// DC_COOP_MUTATE=melee_tap (the rows' twin): the old one-swing tap and its replayed charged swing.
struct MeleeRule {
    int   chain[5];
    float need;
    float cost;
    float last_scale;
    bool  full_gauge;
    float hold_at;
    int   wind;
    int   pose;
    int   level1;
    float level0_cost;
    int   release1[5];
    int   blow1;
    int   release2;
    // coop-comboaudio (Jep's rc3 playtest 11-13: "the stamina comes back too fast"): where the key empties the gauge for its
    // charge: ToanKey_Play at its charge pose (charakey.cpp:210), GoroKey_Play at the smash (charakey.cpp:1085), UngagaKey
    // never (it spends 20 a stage only).
    bool  spend_at_charge;
    bool  spend_at_release;
};

// coop-comboaudio: Toan's release is retail's whole leap, 0xF (the take-off at 187), 0x10 (in the air till the fall speed
// reaches 0.4), 3 (the strike), 0x19 (falling to the floor) and 0x11 (the landing): charakey.cpp:497-583.
constexpr MeleeRule kToanMelee = {{0x24, 0x25, 0x26, 0x27, 0x28}, 20.0f, 25.0f, 1.8f, false, -1.0f, -1, 14, kChargeLevel1, -1.0f, {0xF, 0x10, 3, 0x19, 0x11}, 2, 0x18, true, false};
constexpr MeleeRule kGoroMelee = {{0x24, -1, -1, -1, -1}, 0.0f, 0.0f, 1.0f, true, 95.0f, -1, 13, 45, 15.0f, {14, -1, -1, -1, -1}, 0, -1, false, true};
constexpr MeleeRule kUngagaMelee = {{0x25, 0x26, 0x27, -1, -1}, 40.0f, 20.0f, 1.0f, false, -1.0f, 13, 14, kChargeLevel1, -1.0f, {0xF, -1, -1, -1, -1}, 0, -1, false, false};

// coop-comboaudio twins: DC_COOP_MUTATE=leap_flat (her leap on the floor, no 0x10/0x19, as before), gauge_hold (her gauge
// held through the charge and the attack, shown as the charge's ticks, every charge emptying it, as before).
bool LeapFlat() {
    return Mutated("leap_flat");
}

bool GaugeHold() {
    return Mutated("gauge_hold");
}

const MeleeRule *MeleeRuleOf(int chara) {
    if (Mutated("melee_tap")) {
        return nullptr;
    }

    const MeleeRule *rule = chara == CHARA_TOAN ? &kToanMelee : chara == CHARA_GORO ? &kGoroMelee : chara == CHARA_UNGAGA ? &kUngagaMelee : nullptr;

    return rule != nullptr && HasMotion(rule->chain[0]) ? rule : nullptr;
}

bool PartnerMeleeKey() {
    return MeleeRuleOf(g_partner.loaded_chara) != nullptr;
}

// The stages this model can play (a model short of a motion ends its chain there).
int ChainLength(const MeleeRule &rule) {
    int n = 0;

    while (n < 5 && rule.chain[n] >= 0 && HasMotion(rule.chain[n])) {
        n++;
    }

    return n;
}

// The ticks of the charge pose to its first level (the HUD's gauge reads it too).
int ChargeLevel1Ticks(int chara) {
    const MeleeRule *rule = MeleeRuleOf(chara);

    return rule != nullptr ? rule->level1 : kChargeLevel1;
}

// One blow in front of the partner, at damage * the scale (the tap's blow, unchanged).
void MeleeBlow(float scale) {
    CCharacter   *partner = g_partner.chara;
    float         keep = g_combat.scale;
    float         reach = scale > 1.0f && g_combat.charged_blow ? 1.5f : 1.0f;
    sceVu0FVECTOR rotation;
    sceVu0FVECTOR at;

    g_combat.scale = scale;
    g_partner.attack_hit = true;
    g_combat.blows++;
    g_combat.last_blow_damage = Damage();
    partner->frame->GetRotation(rotation);
    at[0] = g_partner.pos[0] + 9.0f * reach * sinf(rotation[1]);
    at[1] = g_partner.pos[1] + 8.0f;
    at[2] = g_partner.pos[2] + 9.0f * reach * cosf(rotation[1]);
    at[3] = 1.0f;

    int hit = NowColData->Set(at, Damage(), 1, 7.0f * reach, 0.0f, 2, 2, 0, 0);

    MarkHit(hit);
    NowColData->SetKickBack(g_partner.pos, 1.5f * scale, 0.3f, 2);
    std::printf("coop: attack %d hit record %d, %d damage\n", g_partner.swings, hit, Damage());
    g_combat.scale = keep;
}

void PlayFromStart(int motion, int flags) {
    g_partner.attack_motion = motion;
    PlayMotion(motion, flags);
    g_partner.chara->motion_type.state.time = (float) g_partner.chara->motion_type.motion_info[motion].start;
}

void StartChainStage(const MeleeRule &rule, int stage) {
    g_partner.attack_stage = stage;
    g_partner.attack_ticks = 0;
    g_partner.attack_hit = false;
    PlayFromStart(rule.chain[stage], 4);
}

void EndMelee() {
    if (g_combat.chain_hits > 1) {
        std::printf("coop: P2 combo of %d hits (member %d)\n", g_combat.chain_hits, g_partner.loaded_chara);
    }

    g_combat.last_chain = g_combat.chain_hits;
    g_partner.attacking = false;
    g_combat.charging = false;
    g_combat.winding = false;
    g_combat.charged_blow = false;
    g_combat.combo_queued = false;
    g_combat.release_step = 0;
    g_combat.jumping = false; // coop-comboaudio
    g_combat.scale = 1.0f;
    PlayMotion(0, 0);
}

void StartMelee(const MeleeRule &rule) {
    if (rule.full_gauge) {
        g_combat.gauge = 0.0f; // GoroKey_On spends the whole gauge
    }

    g_combat.combo_queued = false;
    g_combat.chain_hits = 0;
    g_combat.winding = false;
    g_combat.release_step = 0;
    StartChainStage(rule, 0);
}

// A press while a stage plays: ToanKey_On's / UngagaKey_On's combo_queued.
void MeleePress() {
    const MeleeRule *rule = MeleeRuleOf(g_partner.loaded_chara);

    if (rule == nullptr || g_combat.charging || g_combat.release_step != 0 || g_combat.throw_stage != 0) {
        return;
    }

    if (g_partner.attack_stage + 1 < ChainLength(*rule) && g_combat.gauge >= rule->need) {
        g_combat.combo_queued = true;
    }
}

void StartMeleeCharge(const MeleeRule &rule) {
    g_combat.charging = true;
    g_combat.charge_ticks = 0;
    g_combat.charge_level = 0;
    if (rule.spend_at_charge || GaugeHold()) {
        g_combat.gauge = 0.0f; // the charge spends the whole gauge (ToanKey_Play's action 14; coop-comboaudio: Toan's alone)
    }

    if (rule.wind >= 0 && HasMotion(rule.wind)) {
        g_combat.winding = true;
        PlayFromStart(rule.wind, 4);
    } else if (HasMotion(rule.pose)) {
        g_combat.winding = false;
        g_partner.attack_motion = rule.pose;
        PlayMotion(rule.pose, 0);
    }
}

// The release's motions: the level's list, its blow's index.
const int *ReleaseMotions(const MeleeRule &rule, int *blow) {
    static const int kNone[5] = {-1, -1, -1, -1, -1};

    if (g_combat.release_two) {
        *blow = 0;
        return &rule.release2;
    }

    *blow = rule.blow1;
    return rule.release1[0] >= 0 ? rule.release1 : kNone;
}

int ReleaseCount(const MeleeRule &rule) {
    return g_combat.release_two ? 1 : 5;
}

// Plays the release's motion at index (skipping any the model lacks); false past the last.
bool PlayRelease(const MeleeRule &rule, int index) {
    int        blow = 0;
    const int *motions = ReleaseMotions(rule, &blow);

    for (; index < ReleaseCount(rule); index++) {
        bool air = motions[index] == 0x10 || motions[index] == 0x19; // coop-comboaudio: the leap's air time

        if (motions[index] >= 0 && HasMotion(motions[index]) && !(air && LeapFlat())) {
            g_combat.release_step = index + 1;
            g_partner.attack_ticks = 0;
            PlayFromStart(motions[index], 4);
            return true;
        }
    }

    return false;
}

void StepMeleeCharge(const MeleeRule &rule) {
    CCharacter *partner = g_partner.chara;
    int         chara = g_partner.loaded_chara;

    if (g_combat.winding) {
        const MOTION_INFO &info = partner->motion_type.motion_info[rule.wind];

        if (!g_combat.held) {
            EndMelee(); // UngagaKey_Play: let go in the wind-up, the attack ends
        } else if (partner->motion_type.state.time >= info.end - 2.0f) {
            g_combat.winding = false;
            g_partner.attack_motion = rule.pose;
            PlayMotion(rule.pose, 0);
        }

        return;
    }

    g_combat.charge_ticks = std::min(g_combat.charge_ticks + 1, kChargeMost);

    if (g_combat.charge_level == 0 && g_combat.charge_ticks >= rule.level1) {
        g_combat.charge_level = 1;
        SndSePlay(SE_LOCK_ON, -1, 0); // present: everyone (her charge level, as StepCharge's: a fight sound on every screen)
        std::printf("coop: P2 charge level 1\n");
    }

    if (g_combat.charge_level == 1 && g_combat.charge_ticks >= kChargeLevel2 && UserStatus->skill_owned[chara] != 0) {
        g_combat.charge_level = 2;
        SndSePlay(SE_LOCK_ON, -1, 0); // present: everyone (her charge level, as StepCharge's: a fight sound on every screen)
        std::printf("coop: P2 charge level 2\n");
    }

    if (g_combat.held) {
        return;
    }

    g_combat.charging = false;

    if (g_combat.charge_level == 0) {
        if (rule.level0_cost >= 0.0f) {
            // GoroKey_Play: let go short of the smash, he swings again.
            g_combat.gauge = std::max(0.0f, g_combat.gauge - rule.level0_cost);
            g_partner.swings++;
            g_partner.since_attack = 0;
            StartChainStage(rule, 0);
        } else {
            EndMelee();
        }

        return;
    }

    g_combat.scale = !Mutated("charge") ? kChargeScale : 1.0f;
    g_combat.charged_blow = true;
    g_combat.release_two = g_combat.charge_level == 2 && rule.release2 >= 0 && HasMotion(rule.release2);
    g_partner.attack_hit = false;
    g_partner.swings++;
    g_partner.since_attack = 0;

    if (g_combat.charge_level == 2) {
        MarkHit(NowColData->Set(g_partner.pos, Damage(), 1, kWaveRadius, 0.0f, 2, 2, 0, 0));
        g_combat.waves++;
        g_combat.last_wave_damage = Damage();
        std::printf("coop: P2 charge wave, %d damage\n", Damage());
    }

    PartnerWeaponWear(g_combat.charge_level >= 2 ? 3.0f : 2.0f, 0);
    g_combat.charges++;
    std::printf("coop: P2 charged release %d, level %d, %d damage\n", g_combat.charges, g_combat.charge_level, Damage());

    if (rule.spend_at_release && !GaugeHold()) {
        g_combat.gauge = 0.0f; // coop-comboaudio: GoroKey_Play's smash empties it (charakey.cpp:1085)
    }

    // DC_COOP_MUTATE=charge_replay (the charge rows' twin): no release motions, the blow at once in the pose, as the old replay.
    if (Mutated("charge_replay") || !PlayRelease(rule, 0)) {
        g_combat.release_blow_motion = g_partner.attack_motion;
        MeleeBlow(g_combat.scale);
        EndMelee();
    }
}

void StepMeleeRelease(const MeleeRule &rule) {
    CCharacter        *partner = g_partner.chara;
    int                blow = 0;
    const int         *motions = ReleaseMotions(rule, &blow);
    int                index = g_combat.release_step - 1;
    const MOTION_INFO &info = partner->motion_type.motion_info[g_partner.attack_motion];
    float              time = partner->motion_type.state.time;
    float              length = (float) (info.end - info.start);

    g_partner.attack_ticks++;

    // coop-comboaudio (Jep's rc3 playtest 11-13: "P2's charge attack plays the jumping animation but she doesn't gain height"): ToanKey_Play's
    // take-off, its 0xF at 187 (charakey.cpp:510-521): the jump's speed up from ParabolicInitialVector (16 ahead, level, gravity 0.1,
    // 40 ticks; its sideways parts zeroed), which her step adds and takes 0.1 off each tick (Walk, as DunMoveChara's, movechara.cpp:1131).
    if (g_partner.attack_motion == 0xF && index + 1 < ReleaseCount(rule) && motions[index + 1] == 0x10 && !LeapFlat() &&
        time >= 187.0f && time < 188.0f) {
        sceVu0FVECTOR from;
        sceVu0FVECTOR to;
        sceVu0FVECTOR rotation;
        float         velocity[4];

        sceVu0CopyVector(from, g_partner.pos);
        partner->frame->GetRotation(rotation);
        to[0] = from[0] + 16.0f * sinf(rotation[1]);
        to[1] = from[1];
        to[2] = from[2] + 16.0f * cosf(rotation[1]);
        to[3] = 1.0f;
        ParabolicInitialVector(velocity, from, to, 0.1f, 40.0f);
        g_combat.jumping = true;
        g_combat.jump_vy = velocity[1];
    }

    // The air motions end on the jump, not on their time: 0x10 once the fall speed reaches 0.4 (charakey.cpp:537), 0x19 once she is
    // within 2 of the floor (charakey.cpp:560, ground_height), the jump over there.
    if (g_partner.attack_motion == 0x10 || g_partner.attack_motion == 0x19) {
        bool up = g_partner.attack_motion == 0x10;
        bool over = !g_combat.jumping || (up ? g_combat.jump_vy <= -0.4f : g_combat.ground_height < 2.0f) || g_partner.attack_ticks >= 600;

        if (over) {
            if (!up) {
                g_combat.jumping = false;
            }

            if (!PlayRelease(rule, index + 1)) {
                g_combat.jumping = false;
                EndMelee();
            }
        }

        return;
    }

    if (index >= blow && !g_partner.attack_hit && (time >= info.start + 0.4f * length || g_partner.attack_ticks >= kAttackTicks / 2)) {
        g_combat.release_blow_motion = motions[index];
        MeleeBlow(g_combat.scale);
    }

    // coop-comboaudio: a release motion ends at its end, as the keys end theirs (GoroKey_Play's smash 14 at its end - 1,
    // charakey.cpp:1096-1100: about 110 ticks); the 90-tick swing cap cut Goro's smash 20 ticks short of P1's. 600: a backstop only,
    // as the leap's air motions'. DC_COOP_MUTATE=release_cap (the Goro row's twin): the 90-tick cap, as before.
    const int cap = Mutated("release_cap") ? kAttackTicks : 600;

    if (time >= info.end - 1.0f || g_partner.attack_ticks >= cap) {
        if (!PlayRelease(rule, index + 1)) {
            if (!g_partner.attack_hit) {
                g_combat.release_blow_motion = g_partner.attack_motion;
                MeleeBlow(g_combat.scale);
            }

            EndMelee();
        }
    }
}

// coop-comboaudio: ToanKey_Play's and UngagaKey_Play's swinging, set at the top of the key's step whenever the motion time lies
// inside one of their swing motions (Toan 0x25 to 0x28, Ungaga 0x25 to 0x27: charakey.cpp:153-162, 669-677), before the key
// moves on a stage; StepGauge reads and clears it as MotionDrive does the player's.
void KeySwinging() {
    const int chara = g_partner.loaded_chara;
    const int last = chara == CHARA_TOAN ? 0x28 : chara == CHARA_UNGAGA ? 0x27 : -1;
    const float time = g_partner.chara->motion_type.state.time;
    const auto &info = g_partner.chara->motion_type.motion_info;

    for (int motion = 0x25; motion <= last; motion++) {
        if (HasMotion(motion) && (float) info[motion].start < time && (float) info[motion].end > time) {
            g_combat.swinging = true;
        }
    }
}

void StepMelee(const MeleeRule &rule) {
    KeySwinging();

    if (g_combat.charging) {
        StepMeleeCharge(rule);
        return;
    }

    if (g_combat.release_step != 0) {
        StepMeleeRelease(rule);
        return;
    }

    CCharacter        *partner = g_partner.chara;
    const MOTION_INFO &info = partner->motion_type.motion_info[g_partner.attack_motion];
    float              time = partner->motion_type.state.time;
    float              length = (float) (info.end - info.start);
    int                stage = g_partner.attack_stage;
    int                stages = ChainLength(rule);
    bool               hold_inside = rule.hold_at > (float) info.start && rule.hold_at < (float) info.end;

    g_partner.attack_ticks++;

    // GoroKey_Play: still held at 95, the swing turns into the smash's wind-up.
    if (stage == 0 && hold_inside && g_combat.held && time >= rule.hold_at) {
        StartMeleeCharge(rule);
        return;
    }

    if (!g_partner.attack_hit && (time >= info.start + 0.4f * length || g_partner.attack_ticks >= kAttackTicks / 2)) {
        g_combat.chain_hits++;
        MeleeBlow(stage == stages - 1 && stages > 1 ? rule.last_scale : 1.0f);
    }

    if (time >= info.end - 1.0f || g_partner.attack_ticks >= kAttackTicks) {
        if (stage == 0 && g_combat.held && !hold_inside) {
            StartMeleeCharge(rule); // ToanKey_Play: action_step 0 at the first swing's end, the charge (14)
            return;
        }

        if (g_combat.combo_queued && stage + 1 < stages) {
            g_combat.gauge = std::max(0.0f, g_combat.gauge - rule.cost);
            g_combat.combo_queued = false;
            g_partner.swings++;
            g_partner.since_attack = 0;
            StartChainStage(rule, stage + 1);
            return;
        }

        EndMelee();
    }
}

// Starts one stage of the attack: its motion from its first frame.
void StartStage(int stage) {
    g_partner.attack_stage = stage;
    g_partner.attack_motion = kTapMotions[g_partner.loaded_chara][stage];
    PlayMotion(g_partner.attack_motion, 4);
    g_partner.chara->motion_type.state.time = (float) g_partner.chara->motion_type.motion_info[g_partner.attack_motion].start;
}

// coop-xiaostamina: Xiao's and Ruby's shot waits for a full gauge and spends all of it, as the player's
// (BattleActionOn_Jinn, BattleActionOn_Ruby); a melee member's tap spends none (ToanKey_On).
// The twin (DC_COOP_MUTATE=shot_free): her shot is free, as before (Jep's 2026-10-27 playtest).
bool ShotSpendsGauge(int chara) {
    return (chara == CHARA_XIAO || chara == CHARA_RUBY) && !Mutated("shot_free");
}

bool AttackReady() {
    const MeleeRule *rule = MeleeRuleOf(g_partner.loaded_chara); // coop-swapcombo: Goro's swing waits for a full gauge (GoroKey_On)

    if (rule != nullptr && rule->full_gauge) {
        return (int) g_combat.gauge == 100;
    }

    return !ShotSpendsGauge(g_partner.loaded_chara) || (int) g_combat.gauge == 100;
}

// Starts an attack with the character's own motions, or, for a model without them, the run.
// coop-p2eyehand: her attack runs on the view's hand's clock (StepAttack): her first-person view is on, Xiao's or Ruby's hand is
// stepped and plays the attack's motion (PartnerHandStep mirrors the body's motion a tick later; the hand has that motion).
static bool EyeShotStart(float *start); // coop-p2eyehand (below, by PartnerHandStep)
static bool PartnerEyeStep();

static bool EyeHandClock() {
    const int chara = g_partner.loaded_chara;

    if ((chara != CHARA_XIAO && chara != CHARA_RUBY) || !PartnerEyeStep() || !g_partner.hand_loaded || g_partner.hand == nullptr ||
        g_partner.hand_motion != g_partner.attack_motion || Mutated("p2_eye_body_clock")) {
        return false;
    }

    const MOTION_INFO *infos = g_partner.hand->motion_type.motion_info;

    return infos != nullptr && infos[g_partner.attack_motion].end > infos[g_partner.attack_motion].start;
}

void StartAttack() {
    if (ShotSpendsGauge(g_partner.loaded_chara)) {
        g_combat.gauge = 0.0f;
    }

    g_partner.attacking = true;
    g_partner.attack_ticks = 0;
    g_partner.attack_hit = false;
    g_partner.since_attack = 0;
    g_partner.swings++;
    if (CoopDebug()) {
        std::printf("coop: P2 starts swing %d\n", g_partner.swings); // coop-revivecross (the revive test reads it)
    }
    g_combat.held = true;
    g_combat.charging = false;
    g_combat.charge_level = 0;
    g_combat.charged_blow = false;
    g_combat.scale = 1.0f;

    if (const MeleeRule *rule = MeleeRuleOf(g_partner.loaded_chara)) { // coop-swapcombo
        StartMelee(*rule);
        return;
    }

    if (!HasMotion(kTapMotions[g_partner.loaded_chara][0])) {
        g_partner.attack_stage = 0;
        g_partner.attack_motion = 1;
        PlayMotion(1, 4);
        return;
    }

    StartStage(0);
}

// Launches a shot from the partner's hand, as BattleActionPlay_Jinn launches the player's at five
// units a tick: the way the partner aims under R2, at the locked monster, or the way it faces.
void Shoot() {
    float yaw = Yaw();

    for (int i = 0; i < kShots; i++) {
        Shot &shot = g_partner.shots[i];

        if (shot.life > 0) {
            continue;
        }

        float at[4];

        shot.pos[0] = g_partner.pos[0] + 4.0f * sinf(yaw);
        shot.pos[1] = g_partner.pos[1] + 10.0f;
        shot.pos[2] = g_partner.pos[2] + 4.0f * cosf(yaw);
        shot.pos[3] = 1.0f;

        // coop-p2eyehand (rc4 known issue: her first-person shot left from above her feet): in her view it starts where retail's
        // aimed shot does, her view's hand. DC_COOP_MUTATE=p2_shot_feet (twin): from her body, as before.
        float      retail[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        const bool has_eye_start = EyeShotStart(retail);
        const bool eye_start = has_eye_start && !Mutated("p2_shot_feet");

        if (eye_start) {
            sceVu0CopyVector(shot.pos, retail);
        }
        if (has_eye_start || TestEnvOn("DC_COOP_TEST_P2_EYE")) { // (x-p2-eye: the start against retail's aimed start)
            std::fprintf(stderr, "coop: test: p2 shot starts at (%.2f %.2f %.2f), retail's aimed start (%.2f %.2f %.2f), her feet (%.2f %.2f %.2f), %s, link tick %lld\n",
                         shot.pos[0], shot.pos[1], shot.pos[2], retail[0], retail[1], retail[2], g_partner.pos[0], g_partner.pos[1], g_partner.pos[2],
                         eye_start ? "her view's hand" : "her body", (long long) NetplayLinkTick());
        }

        if (g_combat.aiming) {
            float flat = cosf(g_combat.aim_pitch);
            // coop-p2eyehand: Xiao's shot from her view flies at retail's aimed 3.5 a tick (setShotVector(.., 3.5, H, V)), not the
            // thrown 5. DC_COOP_MUTATE=p2_eye_speed5 (twin): 5, as before.
            float speed = eye_start && g_partner.loaded_chara == CHARA_XIAO && !Mutated("p2_eye_speed5") ? 3.5f : kShotSpeed;

            shot.vector[0] = speed * flat * sinf(g_combat.aim_yaw);
            shot.vector[1] = speed * sinf(g_combat.aim_pitch);
            shot.vector[2] = speed * flat * cosf(g_combat.aim_yaw);
        } else if (LockPoint(at)) {
            float to[4] = {at[0] - shot.pos[0], at[1] - shot.pos[1], at[2] - shot.pos[2], 1.0f};

            sceVu0Normalize(to, to);
            shot.vector[0] = kShotSpeed * to[0];
            shot.vector[1] = kShotSpeed * to[1];
            shot.vector[2] = kShotSpeed * to[2];
        } else {
            shot.vector[0] = kShotSpeed * sinf(yaw);
            shot.vector[1] = 0.0f;
            shot.vector[2] = kShotSpeed * cosf(yaw);
        }

        // Osmond's gun fires at OzumondShot's ten units a tick.
        if (g_partner.loaded_chara == CHARA_OSMOND) {
            for (int axis = 0; axis < 3; axis++) {
                shot.vector[axis] *= 2.0f;
            }
        }

        shot.vector[3] = 0.0f;

        // Osmond's gun flashes at the muzzle as each bullet goes.
        if (g_partner.loaded_chara == CHARA_OSMOND) {
            float direction[3] = {shot.vector[0] / (2.0f * kShotSpeed), shot.vector[1] / (2.0f * kShotSpeed),
                                  shot.vector[2] / (2.0f * kShotSpeed)};

            ShootFlash(shot.pos, direction);
        }

        // Ruby's is her ring magic, from an effect of the partner's own; the plain shot only if it
        // cannot load.
        if (g_partner.loaded_chara == CHARA_RUBY) {
            float direction[3] = {shot.vector[0] / kShotSpeed, shot.vector[1] / kShotSpeed, shot.vector[2] / kShotSpeed};

            if (ShootRing(shot.pos, direction)) {
                CoopPartnerSound(SE_THROW, shot.pos, kCoopSoundThrow);
                return;
            }
        }

        shot.damage = Damage();
        shot.life = kShotLife;
        g_combat.shot_scale[i] = g_combat.scale;
        CoopPartnerSound(SE_THROW, shot.pos, kCoopSoundThrow);
        return;
    }
}

// Plays the attack's stages through and makes its one blow on the last stage, two fifths of the way
// in: a hit in front of the partner for a weapon in hand, a shot for Xiao's, Ruby's and Osmond's. The
// hit's owner is -1, as an item's: no member's weapon wears or levels from it. Cross held past a
// stage's end charges (StepCharge) before the next stage, or before a second, charged blow.
void StepAttack() {
    if (g_combat.throw_stage != 0) { // coop-p2throw
        StepThrowAction();
        return;
    }

    if (!g_combat.cross_down) {
        g_combat.held = false;
    }

    if (const MeleeRule *rule = MeleeRuleOf(g_partner.loaded_chara)) { // coop-swapcombo
        StepMelee(*rule);
        return;
    }

    if (g_combat.charging) {
        StepCharge();
        return;
    }

    CCharacter        *partner = g_partner.chara;
    // coop-p2eyehand: in her first-person view Xiao's and Ruby's shot runs on the view's hand, as retail's aimed shot does
    // (BattleActionPlay_Jinn(&CharaHand, 1), BattleActionPlay_Ruby(&CharaHand, 1)): the hand's motion clock times the stages and the
    // shot goes at retail's aimed moment on it (Xiao's motion 13 past 21, Ruby's motion 11 past 128), not 0.4 of the body's.
    // DC_COOP_MUTATE=p2_eye_body_clock (twin): the body's clock and 0.4, as before.
    CCharacter        *clock = EyeHandClock() ? g_partner.hand : partner;
    const MOTION_INFO &info = clock->motion_type.motion_info[g_partner.attack_motion];
    float              time = clock->motion_type.state.time;
    float              length = (float) (info.end - info.start);
    int                chara = g_partner.loaded_chara;
    int                next = g_partner.attack_stage + 1;
    bool               last = next >= 2 || kTapMotions[chara][next] < 0 || !HasMotion(kTapMotions[chara][next]);
    const float        blow_at = clock != partner ? (chara == CHARA_XIAO ? 21.0f : 128.0f) : info.start + 0.4f * length;

    g_partner.attack_ticks++;

    if (last && !g_partner.attack_hit && (time >= blow_at || g_partner.attack_ticks >= kAttackTicks / 2)) {
        if (clock != partner || TestEnvOn("DC_COOP_TEST_P2_EYE")) {
            std::fprintf(stderr, "coop: test: p2 shot released at link tick %lld, attack tick %d, %s clock motion %d time %.2f (%d-%d)\n",
                         (long long) NetplayLinkTick(), g_partner.attack_ticks, clock != partner ? "her view's hand" : "her body",
                         g_partner.attack_motion, time, (int) info.start, (int) info.end);
        }
        g_partner.attack_hit = true;
        g_combat.blows++;
        g_combat.last_blow_damage = Damage();

        if (Ranged(chara)) {
            // The weapon wears with each shot, as the player's does (BattleActionPlay_Jinn, _Ruby,
            // _Ozumond: SwordDmgCheck1 1.0, 0.8, 0.5).
            PartnerWeaponWear(chara == CHARA_XIAO ? 1.0f : chara == CHARA_RUBY ? 0.8f : 0.5f, 0);
            Shoot();
            std::printf("coop: attack %d shot, %d damage\n", g_partner.swings, Damage());
        } else {
            float         reach = g_combat.scale > 1.0f ? 1.5f : 1.0f;
            sceVu0FVECTOR rotation;
            sceVu0FVECTOR at;

            partner->frame->GetRotation(rotation);
            at[0] = g_partner.pos[0] + 9.0f * reach * sinf(rotation[1]);
            at[1] = g_partner.pos[1] + 8.0f;
            at[2] = g_partner.pos[2] + 9.0f * reach * cosf(rotation[1]);
            at[3] = 1.0f;

            int hit = NowColData->Set(at, Damage(), 1, 7.0f * reach, 0.0f, 2, 2, 0, 0);

            MarkHit(hit);

            NowColData->SetKickBack(g_partner.pos, 1.5f * g_combat.scale, 0.3f, 2);
            std::printf("coop: attack %d hit record %d, %d damage\n", g_partner.swings, hit, Damage());
        }
    }

    if (time >= info.end - 1.0f || g_partner.attack_ticks >= kAttackTicks) {
        if (g_combat.held && !g_combat.charged_blow && g_partner.attack_stage == 0) {
            StartCharge();
            return;
        }

        if (!last && g_partner.attack_ticks < kAttackTicks) {
            StartStage(next);
            return;
        }

        g_partner.attacking = false;
        g_combat.charged_blow = false;
        g_combat.scale = 1.0f;
        PlayMotion(0, 0);
    }
}

// Flies the partner's shots: CSHOT::step's flight and test (checkCollision against monsters and walls,
// two units round), then a hit where one strikes. Its thrown items fly here too.
void StepShots() {
    CheckFloor();
    StepThrows();
    StepKills();

    if (g_ring.loaded) {
        g_ring.effect.Step();
    }

    for (Shot &shot : g_partner.shots) {
        if (shot.life <= 0) {
            continue;
        }

        sceVu0FVECTOR         hit_position;
        SHOT_COLLISION_RESULT struck = checkCollision(hit_position, shot.pos, shot.vector, 2, 2.0f);

        if (CoopDebug() && struck != SHOT_COLLISION_NONE) {
            std::printf("coop-debug: shot struck %d at (%.0f %.0f %.0f)\n", (int) struck, hit_position[0], hit_position[1], hit_position[2]);
        }

        if (struck == SHOT_COLLISION_NONE) {
            shot.pos[0] += shot.vector[0];
            shot.pos[1] += shot.vector[1];
            shot.pos[2] += shot.vector[2];
            shot.life--;
        } else {
            MarkHit(NowColData->Set(shot.pos, shot.damage, 1, 3.0f, 0.0f, 2, 2, 0, 0));
            shot.life = 0;

            if (g_partner.loaded_chara == CHARA_OSMOND) {
                OzumondShotEffect.Set(hit_position);
            }

            if (struck == SHOT_COLLISION_MONSTER) {
                CoopPartnerSound(SE_MONSTER_HIT, shot.pos, kCoopSoundHit);
                g_combat.shots_hit++;
                std::printf("coop: P2 shot hit a monster for %d\n", shot.damage);
            }
        }

        if (CoopDebug()) {
            std::printf("coop-debug: shot (%.0f %.0f %.0f) life %d\n", shot.pos[0], shot.pos[1], shot.pos[2], shot.life);
        }
    }
}

// coop-p2iframes: the recoil BtCheckDamageProc gives the player after a hit that lands, by its kind (BtActStatus.recoil_frames
// 0xA0, 0x50, 8): the ticks every body waits out before the next hit can land on it.
static int RecoilTicks(int kind) {
    return kind == 3 ? 0xA0 : kind == 4 ? 8 : kind == 2 ? 0x50 : 0;
}

// Takes the monsters' hits as BtCheckDamageProc takes the player's: the hit's damage less the
// partner's own defence (a stand-in's is its character's starting one, CDngStatusData::Initialize), then its recoil by the hit's kind (RecoilTicks). A hit the partner takes is spent, as the
// player's are, so one blow does not land on both. Ailments and money drain stay the player's.
void TakeHits() {
    if (g_partner.invincible > 0) {
        return;
    }

    // Jep's rule (2026-10-30 playtest: she died while her Atla opened): a chest's, an Atla's or a gate key's presentation is
    // a cutscene for its opener, so she takes no blow until control comes back. PartnerPresenting is game state on both
    // copies, so they agree. DC_COOP_MUTATE=openhurt (twin): the blows land as before.
    // coop-pt1111d: and in her own prompt at a chest or gate (its questions are her opening: coop_pause.cpp, InOwnPrompt).
    if ((PartnerPresenting() || (UsePromptOpen(2) && !Mutated("prompt_runs"))) && !Mutated("openhurt")) {
        return;
    }

    int chara = g_partner.loaded_chara;
    int hit_no = NowColData->CheckHitUser(g_partner.pos, 1, kHeight[chara]);

    if (hit_no == -1) {
        return;
    }

    COLLISION_HIT *hit = &NowColData->hit[hit_no];

    // coop-rumble: her controller shakes as BtCheckDamageProc shakes the player's, a guarded blow too (coop_rumble.hpp).
    if (hit->kind == 3) {
        CoopRumbleFor(coop::PartnerPlayer() + 1, 1, 0xE6, 0x16);
    } else if (hit->kind == 2 || hit->kind == 4) {
        CoopRumbleFor(coop::PartnerPlayer() + 1, 1, 0xDC, 0xC);
    }

    // coop-swapfix: a blow (kind 3) on the held guard does no harm and spends the hit, as
    // BtCheckDamageProc has the player's guard (action_no 9); the guard drops, and R1 still held
    // raises it again.
    if (hit->kind == 3 && g_combat.guard == 9 && !Mutated("p2guard")) {
        if (hit->target_mask != 3) {
            NowColData->active[hit_no] = 0;
        }

        g_combat.blocks++;
        g_combat.guard = 0;
        CoopPartnerSound(SE_HIT_BLOCKED, g_partner.pos, kCoopSoundHit);
        std::printf("coop: partner blocked a blow (%d damage), life %d/%d\n", hit->damage, Life(), MaxLife());
        return;
    }

    // coop-p2hurt2 (p2-like-p1): a heavy hit (kind 2 or 4) on her guard in any of its phases (raising, held or lowering: retail's
    // action_on 6, her guard 8 to 10) does no harm and spends the hit, as BtCheckDamageProc's "blown" has the player's; no recoil
    // follows, and the guard drops as the player's does on any hit. DC_COOP_MUTATE=p2guardheavy (twin): it lands, as before.
    if ((hit->kind == 2 || hit->kind == 4) && g_combat.guard != 0 && !Mutated("p2guardheavy")) {
        if (hit->target_mask != 3) {
            NowColData->active[hit_no] = 0;
        }

        g_combat.blocks++;
        g_combat.guard = 0;
        CoopPartnerSound(SE_HIT_BLOCKED, g_partner.pos, kCoopSoundHit);
        std::fprintf(stderr, "coop-p2hurt2: player 2 blocked a heavy hit (kind %d)\n", hit->kind);
        return;
    }

    // coop-p2iframes (Jep's rc4 playtest: Rockanoff hit her again and again): BtCheckDamageProc's own kinds and recoil. Only a
    // blow (3) or a heavy hit (2, 4) does harm; any other kind it meets does none and starts no recoil. A monster's attack sets
    // its hit afresh every tick of its motion's window (CMonstorUnit::CheckDmg, life 2) with no list of whom it struck: what keeps
    // the player from a second hit is the recoil it gives (BtActStatus.recoil_frames: 160 ticks after a blow, 80 after kind 2,
    // 8 after kind 4), which the hit gate waits out. She waited out a flat 60 (kInvincibleTicks), so one Rockanoff swing hit her
    // 60 ticks apart while player 1, under the same swing, was hit once. DC_COOP_MUTATE=p2iframes (twin): the flat 60, every kind.
    const bool flat = Mutated("p2iframes");

    if (hit->kind != 2 && hit->kind != 3 && hit->kind != 4 && !flat) {
        return;
    }

    int damage = std::max(0, hit->damage - Guard(chara));

    // BtCheckDamageProc's halving hit (flags 0x80000): half of what she can hold, whatever her guard.
    if ((hit->flags & 0x80000) && !flat) {
        damage = MaxLife() >> 1;
    }

    int            life = std::max(0, Life() - damage);
    sceVu0FVECTOR  mark;

    if (hit->target_mask != 3) {
        NowColData->active[hit_no] = 0;
    }

    SetLife(life);
    g_combat.guard = 0; // coop-swapfix: a blow that lands drops the guard, as the player's
    CoopPartnerSound(SE_PLAYER_HIT, g_partner.pos, kCoopSoundHurt);

    // The hit's ailments, as BtCheckDamageProc gives the player them (the amulets on the player's belt
    // are the player's): stamina, freeze, poison, curse and goo, each on the same roll.
    {
        int roll = (int) (100.0f * (float) rand() / 2147483648.0f);

        if ((hit->flags & 0x1000) && roll < 0x41) {
            PartnerSetAilment(AILMENT_STAMINA);
        }

        if (hit->flags & 0x100000) {
            PartnerSetAilment(AILMENT_FREEZE);
        }

        if ((hit->flags & 0x100) && roll < 0x41) {
            PartnerSetAilment(AILMENT_FREEZE);
        }

        if ((hit->flags & 0x200) && roll < 0x41 && !PartnerAilment(AILMENT_POISON)) {
            PartnerSetAilment(AILMENT_POISON);
        }

        if ((hit->flags & 0x400) && roll < 0x41 && !PartnerAilment(AILMENT_CURSE)) {
            PartnerSetAilment(AILMENT_CURSE);
        }

        if ((hit->flags & 0x800) && roll < 0x41 && !PartnerAilment(AILMENT_GOO)) {
            PartnerSetAilment(AILMENT_GOO);
        }
    }
    g_partner.invincible = flat ? kInvincibleTicks : RecoilTicks(hit->kind);

    // coop-p2hurt2 (p2-like-p1): her model's red pulse after a hit that lands, BtCheckDamageProc's setUnitAmbientAnime for the
    // player: over 160 draws after a blow, 80 after kind 2, 10 after kind 4. DC_COOP_MUTATE=p2blink (twin): none, as before.
    if (!Mutated("p2blink") && hit->kind >= 2 && hit->kind <= 4) {
        PulseSet(2, hit->kind == 3 ? 160.0f : hit->kind == 4 ? 10.0f : 80.0f, 1.0f, 255.0f, 0.0f, 0.0f);
    }
    sceVu0CopyVector(mark, g_partner.pos);
    mark[1] += kHeight[chara];
    HitValueEntry(NowHitValue, mark, damage, HIT_VALUE_PLAYER, nullptr);
    std::printf("coop: partner hit for %d, life %d/%d\n", damage, life, MaxLife());

    if (life <= 0) {
        PartnerFalls("a blow");
        return;
    }

    // coop-p2hurt (Jep's playtest: Xiao played no knockdown when hit): a blow it lives through plays its hurt motion, as
    // BtCheckDamageProc has the player's: a blow (kind 3) the flinch (6), knocked back the way the hit flies (HitBlowVelocity)
    // and turned to face it (unitBlowActionRot); a heavy hit (kind 2, 4) the knockdown (4). Its swing, charge and aim end.
    // DC_COOP_MUTATE=p2hurt (twin): none, as before.
    int hurt = hit->kind == 3 ? 6 : hit->kind == 2 || hit->kind == 4 ? 4 : 0;

    if (hurt == 0 || !HasMotion(hurt) || Mutated("p2hurt")) {
        return;
    }

    g_combat.blow[0] = g_combat.blow[1] = g_combat.blow[2] = 0.0f;

    if (hurt == 6) {
        float away_x = hit->velocity[0];
        float away_z = hit->velocity[2];

        if (hit->ready_phase == 10) {
            away_x = g_partner.pos[0] - hit->pos[0];
            away_z = g_partner.pos[2] - hit->pos[2];

            float length = std::sqrt(away_x * away_x + away_z * away_z);

            if (length < 0.1f) {
                sceVu0FVECTOR rotation;

                g_partner.chara->frame->GetRotation(rotation);
                away_x = -std::sin(rotation[1]);
                away_z = -std::cos(rotation[1]);
                length = 1.0f;
            }

            away_x /= length;
            away_z /= length;
        }

        g_combat.blow[0] = away_x;
        g_combat.blow[2] = away_z;
    }

    BeginHurt(hurt, (int) hit->kind);
}

// coop-p2hurt: her hurt motion from the knock-back set in g_combat.blow (turned to face where the blow came from), as
// BtCheckDamageProc has the player's; its swing, charge and aim end. TakeHits, and the chest placement test's blow.
void BeginHurt(int hurt, int kind) {
    const float away_x = g_combat.blow[0];
    const float away_z = g_combat.blow[2];

    if (away_x != 0.0f || away_z != 0.0f) {
        float face = atan2f(away_x, away_z) - PI;

        ((CFrameVu1 *) g_partner.chara->frame)->SetRotation(0.0f, face <= -PI ? face + TWO_PI : face, 0.0f);
    }

    g_partner.attacking = false;
    g_combat.charging = false;
    g_combat.aiming = false;
    g_combat.hurt = hurt;
    g_combat.hurts++;
    PlayMotion(hurt, 2);
    g_partner.chara->motion_type.state.time = (float) g_partner.chara->motion_type.motion_info[hurt].start;
    std::fprintf(stderr, "coop-p2hurt: P2 hurt motion %d (hit kind %d), knock-back (%.2f %.2f)\n", hurt, kind, g_combat.blow[0],
                g_combat.blow[2]);
}

void PartnerFalls(const char *why) {
    g_partner.down = kDownTicks;
    g_partner.attacking = false;
    g_combat.charging = false;
    g_combat.aiming = false;
    g_combat.panel = false;
    g_combat.hurt = 0;
    if (!Mutated("down_lock")) { // coop-downmenu: her lock goes as she falls (StepHerLock keeps it gone while she lies there)
        g_combat.lock_shift_no = 0;
        g_combat.lock_shift_rot = 0;
        ReleaseLock("let go: she is down");
    }
    // Played once and held on its last frame (motion_flags 2) until the partner gets up.
    PlayMotion(kDownMotion, 2);

    // coop-joinfix (Jep's 2026-10-29 playtest, parked 4: revived after poison she was still poisoned, kept dying and took
    // damage lying down): her ailments end as she falls, as retail's player's do at his knockout (gameloop's GAME_TASK_DEAD
    // entry clears ailments and ailment_frames). DC_COOP_MUTATE=fall_keeps_ailments (twin): kept, as before.
    const int chara = g_partner.loaded_chara;
    unsigned  was = 0;

    if (chara >= 0 && chara < 6 && !Mutated("fall_keeps_ailments")) {
        was = (unsigned) UserStatus->ailments[chara];
        UserStatus->ailments[chara] = 0;
        UserStatus->ailment_frames[chara] = 0;
    }
    std::fprintf(stderr, "coop: P2 is down (%s), life %d/%d, ailments %#x cleared\n", why, Life(), MaxLife(), was);
}

bool StepHurt() {
    if (g_combat.hurt == 0) {
        return false;
    }

    const MOTION_INFO &info = g_partner.chara->motion_type.motion_info[g_combat.hurt];
    float              time = g_partner.chara->motion_type.state.time;

    // Over two frames before its end, as DunMoveChara ends action_on 5/4; something else playing (a swap, a down) ends it too.
    if (g_partner.down > 0 || g_partner.motion != g_combat.hurt || (time >= info.end - 2.0f && time <= info.end)) {
        std::fprintf(stderr, "coop-p2hurt: P2 hurt motion %d over (time %.1f of %.1f)\n", g_combat.hurt, time, (float) info.end);
        if (g_partner.down == 0 && g_partner.motion == g_combat.hurt) {
            PlayMotion(0, 0);
        }

        g_combat.hurt = 0;
        g_combat.blow[0] = g_combat.blow[1] = g_combat.blow[2] = 0.0f;
        return false;
    }

    return true;
}

// The knock-back slows 0.018 a tick to a stop, as DunMoveChara slows the player's blowVelo.
void HurtPush(float *velocity) {
    if (g_combat.hurt == 0) {
        return;
    }

    for (int i = 0; i < 3; i += 2) {
        float &speed = g_combat.blow[i];

        velocity[i] += speed;

        if (speed < 0.0f) {
            speed = speed + 0.018f >= -0.01f ? 0.0f : speed + 0.018f;
        } else if (speed > 0.0f) {
            speed = speed - 0.018f <= 0.01f ? 0.0f : speed - 0.018f;
        }
    }
}

namespace {

// Whenever pad 2 takes up a member: the stats its play reads, all of them that member's own (for the
// headless check that P2 never plays on the controlled character's).
void LogStats() {
    int                chara = g_partner.loaded_chara;
    const WEAPON_HAVE *weapon = EquippedWeapon(chara);

    // Test hook (DC_COOP_MOTIONS=1): the member's motion table, for checking its moves.
    if (TestEnv("DC_COOP_MOTIONS") != nullptr) {
        for (int i = 0; i < 48; i++) {
            const MOTION_INFO &info = g_partner.chara->motion_type.motion_info[i];

            if (info.end > info.start && info.start >= 0) {
                std::printf("coop-debug: chara %d motion %d frames %d-%d\n", chara, i, (int) info.start, (int) info.end);
            }
        }
    }

    std::printf("coop: P2 stats chara %d (%s): life %d/%d, defence %d, attack %d, weapon %d (attack %d magic %d element %d), skill %d; "
                "player chara %d life %d/%d defence %d\n",
                chara, OwnLife(chara) ? "joined" : "stand-in", Life(), MaxLife(), (int) UserStatus->defense[chara], BaseAttack(chara),
                weapon != nullptr ? weapon->item_no : StarterWeapon(chara), weapon != nullptr ? weapon->attack : -1,
                weapon != nullptr ? weapon->magic : -1, weapon != nullptr ? weapon->best_elem : -1, (int) UserStatus->skill_owned[chara],
                (int) UserStatus->cur_chara, UserStatus->hp[(int) UserStatus->cur_chara], UserStatus->max_hp[(int) UserStatus->cur_chara],
                (int) UserStatus->defense[(int) UserStatus->cur_chara]);
}

} // namespace

// coop: gaps2
bool SitsOutFightZone() {
    int zone = UserStatus->res_limit_zone_current;

    // coop-pt1111k/n: the sit-out of a player not on the floor's member, one switch, off by Jep's 11-11 decision (ZoneNonHoldersSitOut): the same rule as player 1's (ZoneSitsOut).
    return zone >= RES_LIMIT_ZONE_TOAN && zone <= RES_LIMIT_ZONE_OSMOND && ZoneSitsOut(1) && !Mutated("fightzone");
}

namespace {

// One stage of the guard's motion (8 up, 9 held, 10 down), held on its last frame but for the hold.
void GuardMotion(int stage) {
    g_combat.guard = stage;
    g_combat.guard_ticks = 0;

    if (HasMotion(stage)) {
        PlayMotion(stage, stage == 9 ? 0 : 2);
        g_partner.chara->motion_type.state.time = (float) g_partner.chara->motion_type.motion_info[stage].start;
    }
}

// Whether a stage's motion has played out (or, for a model without it, 8 ticks have gone).
bool GuardStageDone(int stage) {
    if (!HasMotion(stage)) {
        return g_combat.guard_ticks >= 8;
    }

    const MOTION_INFO &info = g_partner.chara->motion_type.motion_info[stage];
    return g_partner.chara->motion_type.state.time >= (float) info.end - 1.5f;
}

} // namespace

// The player's guard for the partner (DunMoveChara: R1 held with a lock, the motion's three stages):
// starts only at rest (not swinging, charging or aiming) and ends when R1 is let go or the lock goes.
// True while guarding.
bool StepGuard(std::uint16_t buttons) {
    bool held = (buttons & kInputR1) != 0 && g_combat.lock >= 0;

    if (g_combat.guard == 0) {
        if (!held || g_partner.attacking || g_combat.charging || g_combat.aiming || g_partner.down > 0) {
            return false;
        }

        GuardMotion(8);
        return true;
    }

    g_combat.guard_ticks++;

    if (g_combat.guard != 10 && !held) {
        GuardMotion(10);
    } else if (g_combat.guard == 8 && GuardStageDone(8)) {
        GuardMotion(9);
    } else if (g_combat.guard == 10 && GuardStageDone(10)) {
        g_combat.guard = 0;
        PlayMotion(0, 0);
        return false;
    }

    return true;
}

// coop-p2actions: her first-person view is on (every copy alike), and where it looks from: retail's EyeCamera, the eye at her
// feet plus her member's height (CharaHeight's table) less 1.5, looking 10 along the view's two angles.
bool PartnerFirstPerson() {
    return g_combat.aiming && g_partner.visible && g_partner.down == 0 && !Mutated("p2_aim_hold");
}

// coop-p2eyehand: the same, for the simulation's own reads in her step (her hand, her shot): g_partner.visible is this tick's
// draw mark, cleared at the step's start and set after her body's step, so her combat (before it) would read her view off.
static bool PartnerEyeStep() {
    return g_combat.aiming && g_partner.chara != nullptr && g_partner.down == 0 && !Mutated("p2_aim_hold");
}

// Retail's CharaHeight, for her member.
static float PartnerHeight() {
    constexpr float kHeight[6] = {16.0f, 14.0f, 16.0f, 16.0f, 18.0f, 15.0f};
    const int       chara = g_partner.loaded_chara;

    return chara >= 0 && chara < 6 ? kHeight[chara] : 16.0f;
}

void PartnerEyeView(float *eye, float *ref) {
    const float flat = cosf(g_combat.aim_pitch);

    eye[0] = g_partner.pos[0];
    eye[1] = g_partner.pos[1] + PartnerHeight() - 1.5f;
    eye[2] = g_partner.pos[2];
    eye[3] = 1.0f;
    ref[0] = eye[0] + 10.0f * flat * sinf(g_combat.aim_yaw);
    ref[1] = eye[1] + 10.0f * sinf(g_combat.aim_pitch);
    ref[2] = eye[2] + 10.0f * flat * cosf(g_combat.aim_yaw);
    ref[3] = 1.0f;
}

// coop-p2eyehand: her weapon hung from her view's hand's "pati1" (hold) or back in her body's "weapon" frame, as retail's
// EquipReAttach(NowWeapon, 1 / 0); held_out says which. PartnerHandStep's, and a state set's apply (PartnerSnapshotApplied) puts
// the sender's back with it. False when there is no weapon or frame to hang it from.
bool PartnerWeaponHold(bool hold) {
    CCharacter *weapon = g_partner.weapon;

    if (weapon == nullptr || weapon->frame == nullptr || (hold && (g_partner.hand == nullptr || g_partner.hand->frame == nullptr)) ||
        g_partner.chara == nullptr || g_partner.chara->frame == nullptr) {
        return false;
    }

    CFrame *holder = hold ? g_partner.hand->frame->SearchFrame((char *) "pati1") : g_partner.chara->frame->SearchFrame((char *) "weapon");

    if (holder == nullptr) {
        return false;
    }
    weapon->SetPosition(0.0f, 0.0f, 0.0f);
    weapon->SetRotation(0.0f, 0.0f, 0.0f);
    weapon->frame->SetReference(holder);
    weapon->motion_no = 0;
    weapon->motion_flags = 0;
    weapon->motion_speed = -1.0f;
    g_partner.held_out = hold;
    std::fprintf(stderr, "coop: P2 %s at link tick %lld\n", hold ? "holds her weapon out in her view's hand" : "has her weapon back in her body's hand",
                 (long long) NetplayLinkTick());
    return true;
}

// coop-p2eyehand: her first-person view's hand, stepped as DunMoveChara steps the player's CharaHand while CharaMainHandViewFlag
// (Xiao's in the view, Ruby's always): at her body's frame plus her member's height less 3, turned by the view's two angles
// (retail's viewAngleV is the opposite of aim_pitch: RotX(V) lifts the view for a negative V), playing her body's motion (retail's
// BtActStatus.motion_no, the body's and the hand's one number) and blending into a new one. Xiao's slingshot hangs from the
// hand's "pati1" while the view is on (EquipReAttach(NowWeapon, 1) at InitEyeCamera, (.., 0) as it ends) and plays the same
// motion, stepped here (retail steps NowWeapon in the step): the simulation's, on every copy alike, so the shot's start read
// from it agrees. Draw only reads them (CoopDraw).
void PartnerHandStep() {
    CCharacter *hand = g_partner.hand;
    CCharacter *weapon = g_partner.weapon;
    const int   chara = g_partner.loaded_chara;
    const bool  eye = PartnerEyeStep() && g_partner.hand_loaded && hand != nullptr;
    const bool  hold = eye && chara == CHARA_XIAO && g_partner.weapon_loaded && weapon != nullptr && weapon->frame != nullptr;

    if (hold != g_partner.held_out) {
        PartnerWeaponHold(hold);
    }

    if (!eye) {
        g_partner.hand_motion = -1;
        return;
    }

    sceVu0FVECTOR at;

    sceVu0CopyVector(at, g_partner.chara->frame->position);
    at[1] += PartnerHeight() - 3.0f;
    hand->SetPosition(at);
    hand->SetRotation(-g_combat.aim_pitch, g_combat.aim_yaw, 0.0f);

    const int motion = g_partner.chara->motion_no;

    if (motion != g_partner.hand_motion) {
        // (retail's blend: none as a shot starts (BattleActionOn_Jinn/_Ruby's driveNoInterpolate: 4), else a blend (0))
        const int blend = g_partner.attacking && g_partner.attack_stage == 0 && motion == g_partner.attack_motion ? 4 : 0;

        hand->motion_no = motion;
        hand->motion_flags = blend;
        hand->motion_speed = -1.0f;
        if (g_partner.held_out) {
            weapon->motion_no = motion;
            weapon->motion_flags = blend;
            weapon->motion_speed = -1.0f;
        }
        g_partner.hand_motion = motion;
    }

    if (g_partner.held_out) {
        weapon->Step();
    }
    hand->Step();

    // (x-p2-eye: the hand's pose every 10 ticks: its motion, and where its root, "pati1" and the slingshot's "eff30" are from her eye)
    if (static const bool traced = TestEnvOn("DC_COOP_TEST_P2_EYE"); traced && NetplayLinkTick() % 10 == 0) {
        float   offset[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        float   eye[4] = {g_partner.pos[0], g_partner.pos[1] + PartnerHeight() - 1.5f, g_partner.pos[2], 1.0f};
        float   root[4] = {};
        float   grip[4] = {};
        float   tip[4] = {};
        CFrame *pati = hand->frame->SearchFrame((char *) "pati1");
        CFrame *eff = g_partner.held_out ? weapon->frame->SearchFrame((char *) "eff30") : nullptr;

        hand->frame->GetWorldPosition(root, offset);
        if (pati != nullptr) {
            pati->GetWorldPosition(grip, offset);
        }
        if (eff != nullptr) {
            eff->GetWorldPosition(tip, offset);
        }
        std::fprintf(stderr, "coop: test: p2 hand pose: link tick %lld motion %d state %d time %.1f yaw %.2f pitch %.2f, from her eye: root (%.2f %.2f %.2f) pati1 (%.2f %.2f %.2f) eff30 (%.2f %.2f %.2f)\n",
                     (long long) NetplayLinkTick(), hand->motion_no, hand->motion_state, hand->motion_type.state.time, g_combat.aim_yaw, g_combat.aim_pitch,
                     root[0] - eye[0], root[1] - eye[1], root[2] - eye[2], grip[0] - eye[0], grip[1] - eye[1], grip[2] - eye[2], tip[0] - eye[0],
                     tip[1] - eye[1], tip[2] - eye[2]);
    }
}

// coop-p2eyehand: where her shot starts in her first-person view, as retail's aimed shots start: Xiao's from her slingshot's
// "eff30" (BattleActionPlay_Jinn, the weapon in the view's hand), Ruby's from the hand's "weapon" (BattleActionShotRuby),
// Osmond's 10 over her feet (BattleActionPlay_Ozumond). False when this member's view has no such start (the caller's own).
static bool EyeShotStart(float *start) {
    float     offset[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    const int chara = g_partner.loaded_chara;

    if (!PartnerEyeStep()) {
        return false;
    }

    if (chara == CHARA_OSMOND) {
        sceVu0CopyVector(start, g_partner.pos);
        start[1] += 10.0f;
        start[3] = 1.0f;
        return true;
    }

    CFrame *from = nullptr;

    if (chara == CHARA_XIAO && g_partner.held_out) {
        from = g_partner.weapon->frame->SearchFrame((char *) "eff30");
    } else if (chara == CHARA_RUBY && g_partner.hand_loaded && g_partner.hand_motion >= 0) {
        from = g_partner.hand->frame->SearchFrame((char *) "weapon");
    }

    if (from == nullptr) {
        return false;
    }

    sceVu0FMATRIX fresh;

    // (the slingshot hangs by reference, and a frame below it keeps a world matrix the draw's moves leave behind (CoopDraw
    // draws it in her body's hand on another copy): its root made again first clears its frames', so every copy reads the same)
    if (chara == CHARA_XIAO) {
        g_partner.weapon->frame->GetLWMatrix(fresh);
    }
    from->GetWorldPosition(start, offset);
    start[3] = 1.0f;
    return true;
}

// coop-p2actions (x-skel-door herclose): the ticks her combat input took a new Circle, Triangle or Square press, counted for the
// test's line alone (nothing reads it in play).
int g_combat_presses = 0;

int CombatPressCount() {
    return g_combat_presses;
}

bool CombatInput(const InputPadState &live_pad, bool live_cross_pressed) {
    g_combat.boost = false; // coop-p2feather: set again below while her Square holds the feather

    // coop-menuse: her pad's refuse, confirm and lock-on sounds are hers (Jep's 2026-10-25 "Xiao pressing square or
    // circle in the dungeon plays the menu cancel noise and p1 can hear it"); her swings and items stay everyone's.
    SoundOwner sound(2, true);

    g_trap_by_partner = false;

    // A test bot (DC_COOP_TEST, coop_combat_test.cpp) may hold pad 2's combat buttons instead.
    InputPadState pad = live_pad;
    bool          cross_pressed = live_cross_pressed;

    if (CombatTestPad(pad) || InventoryTestPad(pad) || CamTestPad(pad.buttons)) { // coop-inventory, coop-p2circle: their bots too
        cross_pressed = (pad.buttons & kInputCross) != 0 && (g_combat.buttons & kInputCross) == 0;
    }

    // Frozen, the partner can do nothing until it thaws.
    if (PartnerAilment(AILMENT_FREEZE)) {
        CombatCancel(pad.buttons);
        return true;
    }

    // coop-party: pad 2's wheel owns its D-pad, Cross and Circle while open; what combat had going stops.
    // The tick it closes is the wheel's too: the Cross that picks a member must not also swing.
    bool wheel = WheelOpen();

    if (wheel || g_combat.wheel) {
        g_combat.wheel = wheel;
        CombatCancel(pad.buttons);
        return true;
    }

    std::uint16_t buttons = pad.buttons;
    std::uint16_t was = g_combat.buttons;
    std::uint16_t pressed = buttons & ~was;

    g_combat_presses += (pressed & (kInputCircle | kInputTriangle | kInputSquare)) != 0 ? 1 : 0; // (coop-p2actions: x-skel-door's count)
    g_combat.buttons = buttons;
    g_combat.cross_down = (buttons & kInputCross) != 0;
    g_combat.stick_x = Stick(pad.left_x); // coop-p2throw: the stick walks her landing point
    g_combat.stick_y = Stick(pad.left_y);

    if (g_combat.throw_stage != 0 && !g_partner.attacking) { // something else (a blow, a swap) ended her throw
        g_combat.throw_stage = 0;
        g_combat.throw_item = -1;
    }

    if (g_combat.chara != g_partner.loaded_chara) {
        g_combat.chara = g_partner.loaded_chara;

        // Test hook (DC_COOP_SKILL=1): the member pad 2 takes up has its event skill, for the charge's
        // second level.
        if (TestEnv("DC_COOP_SKILL") != nullptr && g_combat.chara >= 0) {
            UserStatus->skill_owned[g_combat.chara] = 1;
        }

        LogStats();
        g_combat.guard = 0; // coop-swapfix
        g_combat.charging = false;
        g_combat.aiming = false;
        g_combat.panel = false;
        g_combat.scale = 1.0f;
        g_combat.charged_blow = false;
    }

    if (g_combat.item_wait > 0) {
        g_combat.item_wait--;
    }

    ItemParityTick();  // coop-actorswap: its test hooks
    ThrowParityTick();
    WearParityTick();

    // coop-inventory: Square's item is the one in P2's selected quick-use slot, which D-pad Left and Right move
    // (PartnerSlotItem), as player 1's are.
    g_combat.item = PartnerSlotItem(g_partner.attacking || g_combat.panel ? 0 : pressed);

    // coop-p2controls: her Triangle opened pad 2's first panel (weapons and items under her life bar, from before her own menus)
    // when it reached here: with co-op menus off, on a tick his Triangle took the game's pad from her claim (Route) but the game
    // opened no menu for him (he was walking). Player 1's Triangle has no other use than the menu (DunMoveChara), so hers here
    // does nothing; nothing opens the panel now (its state stays for the reset test's bot, coop_combat_test.cpp, which sets it).

    if (g_combat.panel) {
        PanelInput(pressed);

        // The panel's mutation twin holds the game still while it is open, as a pause would.
        if (Mutated("panel") && g_combat.panel) {
            driveStepHold = 1;
        }

        return true;
    }

    // Circle: a tap locks on (or moves the lock along), a hold lets go.
    // coop-lockpress: with nothing locked, the press itself locks, as retail's PadInput_NO does on its Down; that press is
    // spent (its hold and its release do nothing more). Locking only at the release read as "hold Circle to lock on" to
    // Jep's friend over Steam (2026-10-28), the lock showing a tap's length plus the link's delay after the press.
    // DC_COOP_MUTATE=lockpress locks at the release only, as before (the twin).
    // coop-p2actions (Jep's rc3 playtest, 11-13: "Xiao goes into first person in the base game while aiming her slingshot"): R2 is
    // retail's first-person view for her as for player 1. DunMoveChara: GamePad.Down(PAD_R2) goes in (InitEyeCamera: the view starts
    // on the character's heading, level; the lock let go; the body hidden) and GAME_TASK_EYE_CAMERA's Down of R2, L1 or Circle
    // comes out; EyeCamera turns the view by the left stick at 0.02 a tick, 1.0 up and 0.65 down; Cross shoots from the view for
    // Xiao, Ruby and Osmond alone. Her R2 was a third-person aim while held. The view's state is the aim's (g_combat.aiming and its
    // two angles, in the checksum), stepped on every copy: the other copy sees her stand and turn with it, her own copy draws from
    // her eye with her body left out (coop_camera's CoopViewChoose, draw only). The view's hand and slingshot (retail's CharaHand)
    // are hers since coop-p2eyehand (PartnerHandStep, DrawPartnerEyeHand). DC_COOP_MUTATE=p2_aim_hold (twin): the held aim from behind her, as before.
    const bool eye_view = !Mutated("p2_aim_hold");
    bool       eye_left = false;

    if (eye_view && g_combat.aiming && (pressed & (kInputR2 | kInputL1 | kInputCircle)) != 0) {
        g_combat.aiming = false;
        eye_left = true;
        g_combat.circle_ticks = kLockReleaseTicks; // (a Circle that left the view is spent: its release locks nothing)
        PartnerCameraBehind(); // (retail's camera follows again from behind the heading the view left her on)
        std::fprintf(stderr, "coop: P2 first-person view off (her %s) at link tick %lld, heading %.2f\n",
                     (pressed & kInputR2) != 0 ? "R2" : (pressed & kInputL1) != 0 ? "L1" : "Circle", (long long) NetplayLinkTick(), g_combat.aim_yaw);
    }

    if (eye_view && (g_combat.aiming || eye_left)) {
        // (the view has no lock-on, as retail's)
    } else {
        // coop-p2controls3: player 1's lock-on controls (DunMoveChara): Circle locks the monster ready or lets go; L1 while locked,
        // not mid-action, moves to the next one. (Her tap that moved the lock and her hold that let go are gone.)
        TestLockFront();
        if ((pressed & kInputCircle) != 0) {
            HerLockCircle();
        }
        if ((pressed & kInputL1) != 0 && g_combat.lock >= 0 && !g_partner.attacking) {
            HerLockL1();
        }
    }

    // coop: gaps2
    // On a floor only one member may fight (res_limit_zone_current 0 to 5, e.g. Ungaga's), P2 never is
    // that member (the two are never the same), so it sits out: it walks and opens things, but its
    // swing, shot, aim and items are refused with the game's refuse sound, as the player's character
    // menu refuses there. DC_COOP_MUTATE=fightzone lets it fight, for the test's twin.
    const bool sitting_out = SitsOutFightZone();

    if (sitting_out && (pressed & (kInputSquare | kInputR2)) != 0) {
        SndSePlay(MENU_SOUND_REFUSE, -1, 0);
        std::printf("coop: P2 sits out this floor (zone %d)\n", (int) UserStatus->res_limit_zone_current);
    }

    if (eye_view) {
        if (!g_combat.aiming && !eye_left && (pressed & kInputR2) != 0 && !sitting_out && !g_partner.attacking && g_combat.guard == 0) {
            g_combat.aiming = true;
            g_combat.aim_yaw = Yaw();
            g_combat.aim_pitch = 0.0f;
            g_combat.charging = false;
            if (g_combat.lock >= 0) {
                ReleaseLock("first person"); // (retail's LockOffTargte)
            }
            std::fprintf(stderr, "coop: P2 first-person view on at link tick %lld, heading %.2f\n", (long long) NetplayLinkTick(), g_combat.aim_yaw);
        }
        if (g_combat.aiming && !Mutated("aim")) {
            g_combat.aim_yaw += 0.02f * Stick(pad.left_x);
            g_combat.aim_pitch = std::clamp(g_combat.aim_pitch - 0.02f * Stick(pad.left_y), -0.65f, 1.0f);
        }
    } else if ((buttons & kInputR2) != 0 && !sitting_out) {
        if (!g_combat.aiming) {
            g_combat.aiming = true;
            g_combat.aim_yaw = Yaw();
            g_combat.aim_pitch = 0.0f;
        }

        if (!Mutated("aim")) {
            g_combat.aim_yaw += 0.04f * Stick(pad.left_x);
            g_combat.aim_pitch = std::clamp(g_combat.aim_pitch - 0.02f * Stick(pad.left_y), -0.5f, 0.5f);
        }
    } else {
        g_combat.aiming = false;
    }

    // coop-swapfix: R1 held while locked on guards (StepGuard); the partner walks slowly and neither
    // swings nor uses items until the guard is down.
    if (StepGuard(buttons)) {
        return false;
    }

    if (!g_partner.attacking) {
        if ((pressed & kInputSquare) != 0 && !sitting_out) {
            // coop-inventory: with Select held, Square sends the item to player 1 instead of using it.
            if ((buttons & kInputSelect) != 0 && OwnBags()) {
                SendItem(2, g_combat.item);
            } else if (OpenKeyPick()) { // coop-keypick: her own picker at a gate's lock, the game plays on
                return true;
            } else if (const int task_before = MenuTaskNow(); MenuNotePrompt(InteractItem(), task_before)) { // coop-p2keys: a gate's lock that wants an item
                return true;
            } else {
                UseItem(g_combat.item, true); // (coop-playfix: the field's Square, quiet when it has nothing to use)
            }
        }

        // coop-p2feather: DunMoveChara's boost: Square held, nothing locked on, a feather in her selected slot, one use spent each
        // frame, run at 3.0 (coop_core's walk); the last use drops the slot's feather as the player's does.
        if ((buttons & kInputSquare) != 0 && (buttons & kInputSelect) == 0 && !sitting_out && g_combat.lock < 0 && g_combat.guard == 0 && g_combat.item == ITEM_DRAN_S_FEATHER &&
            UseOf(g_combat.item) == ITEM_USE_BOOST && SpendBoostFrame()) {
            g_combat.boost = true;
        }
    }

    if (g_partner.attacking) {
        if (cross_pressed) {
            MeleePress(); // coop-swapcombo: a press mid-swing queues the chain's next stage
        }

        return true;
    }

    if (cross_pressed) {
        const int task_before = MenuTaskNow(); // coop-pt1109c

        // coop-revivecross: as retail's Cross takes the interaction in reach instead of a swing, a player lying down that is
        // offered to her (ReviveOffered: nearer than any chest or door) takes it: the revive's question (PlayerDownStep).
        const bool revive = !g_combat.aiming && ReviveTakesCross(2);

        ReviveCrossDecided(2, revive); // one Cross, one action
        if (revive) {
            return true;
        }

        // Aiming, Cross only fires.
        if (g_combat.aiming || !MenuNotePrompt(Interact(), task_before)) { // coop-menus: her prompt
            if (sitting_out) { // coop: gaps2
                SndSePlay(MENU_SOUND_REFUSE, -1, 0);
                std::printf("coop: P2 sits out this floor (zone %d)\n", (int) UserStatus->res_limit_zone_current);
            } else if (eye_view && g_combat.aiming && !Ranged(g_partner.loaded_chara)) {
                // coop-p2actions: retail's view shoots for Xiao, Ruby and Osmond alone; the others only look
            } else if (AttackReady()) { // coop-xiaostamina: a shot waits for her full gauge
                ZoneNoteSwing(1);
                StartAttack();
            }
        }

        return true;
    }

    return g_combat.aiming;
}

void RingJobLeftovers(int *loaded, int *shots, int *flashes) {
    *loaded = g_ring.loaded;
    *shots = g_ring.shots;
    *flashes = g_ring.flashes;
}

void CombatResetForSession() {
    int lock = g_combat.lock;

    g_heal.active = 0;

    g_combat = Combat{};
    g_floor_key = -1;
    CoopTargetReset();
    UnloadRing();
    g_ring.shots = 0;
    g_ring.flashes = 0;

    // The reset test's mutation twin: the lock target survives.
    if (Mutated("resetlock")) {
        g_combat.lock = lock;
    }
}

int PartnerGauge() {
    // coop-comboaudio (Jep's rc3 playtest 11-13): a melee member's panel is retail's, the gauge itself through the charge as DunMainDraw
    // draws BtActStatus.action_gauge (gameloop.cpp:2561-2576); it showed the charge's ticks, full half a second into the hold.
    if (g_combat.charging && (MeleeRuleOf(g_partner.loaded_chara) == nullptr || GaugeHold())) {
        int full = UserStatus->skill_owned[g_partner.loaded_chara] != 0 ? kChargeLevel2 : ChargeLevel1Ticks(g_partner.loaded_chara);

        return std::min(100, g_combat.charge_ticks * 100 / full);
    }

    // The twin (DC_COOP_MUTATE=gauge_swing): every swing empties it, as before (Jep's playtest note 31).
    if (g_partner.attacking && Mutated("gauge_swing")) {
        return std::min(100, g_partner.attack_ticks * 100 / kAttackTicks);
    }

    return std::clamp((int) g_combat.gauge, 0, 100);
}

// MotionDrive's refill: the weapon's speed / 30 a tick (at least 1), Toan's and Ungaga's a quarter of
// that and none mid-swing, Xiao's and Ruby's half again; not while charging.
void StepGauge() {
    int              chara = g_partner.loaded_chara;
    const MeleeRule *rule = chara >= 0 && !GaugeHold() ? MeleeRuleOf(chara) : nullptr;

    // coop-comboaudio: a melee member's gauge fills through its charge too, as MotionDrive fills the player's every tick it is under
    // 100 (motiondrive.cpp:502), whatever the key does.
    if ((g_combat.charging && rule == nullptr) || g_combat.gauge >= 100.0f || chara < 0) {
        return;
    }

    const WEAPON_HAVE *weapon = EquippedWeapon(chara);
    WEAPON_HAVE        starter{};

    if (weapon == nullptr && StarterWeapon(chara) > 0) {
        WepDataListToHaveCopy(StarterWeapon(chara), &starter);
        weapon = &starter;
    }

    float speed = weapon != nullptr ? (float) weapon->speed : 0.0f;
    float gain = speed / 30.0f;

    // coop-xiaostamina: goo halves the refill and stamina doubles it, as motionDrive's for the player.
    if (PartnerAilment(AILMENT_GOO)) {
        gain /= 2.0f;
    }

    if (PartnerAilment(AILMENT_STAMINA)) {
        gain *= 2.0f;
    }

    gain = std::max(1.0f, gain);

    if (chara == CHARA_TOAN || chara == CHARA_UNGAGA) {
        // coop-comboaudio: none only on a tick a chain swing plays, 0x25 to 0x28 (ToanKey_Play / UngagaKey_Play set swinging inside
        // those motions' frames, charakey.cpp:153-162, 669-677; Toan's first swing and his leap fill it), a quarter otherwise.
        // The flag is her key step's (StepMelee: KeySwinging), read and cleared here as MotionDrive reads and clears the player's
        // (motiondrive.cpp:525-541): read at her refill's own time it was set one tick late at each stage's start, an extra quarter fill
        // a stage (Toan's combo ended 2.33 above P1's, by numbers).
        bool swinging = g_partner.attacking;

        if (rule != nullptr) {
            swinging = g_combat.swinging;
            g_combat.swinging = false;
        }

        gain = swinging ? 0.0f : gain * 0.25f;
    } else if (chara == CHARA_XIAO || chara == CHARA_RUBY) {
        gain *= 1.5f;
    }

    g_combat.gauge = std::min(100.0f, g_combat.gauge + gain);
}

int RingShots(int *active, int *element) {
    *active = 0;

    for (int i = 0; i < 8; i++) {
        *active += g_ring.loaded && g_ring.effect.active[i] != 0 ? 1 : 0;
    }

    *element = g_ring.loaded ? g_ring.element : -1;
    return g_ring.shots;
}

int PartnerEffectBlock() {
    return kRingTextureBlock;
}

const std::vector<u_long> &PartnerEffectLoaded() {
    return g_ring.textures;
}

int MuzzleFlashes(bool *loaded_osmond) {
    *loaded_osmond = g_ring.loaded && g_ring.entry != nullptr && g_ring.entry == OsmondEffect();
    return g_ring.flashes;
}

bool CombatIsFresh(char *why, int size) {
    if (!(g_combat == Combat{})) {
        std::snprintf(why, size, "g_combat differs from a fresh Combat (lock %d, charging %d, aim %d, panel %d, item %d, buttons %x)",
                      g_combat.lock, (int) g_combat.charging, (int) g_combat.aiming, (int) g_combat.panel, g_combat.item,
                      (unsigned) g_combat.buttons);
        return false;
    }

    if (g_floor_key != -1) {
        std::snprintf(why, size, "the floor key is %d", g_floor_key);
        return false;
    }

    for (int i = 0; i < 16; i++) {
        if (CoopMonsterTarget(i) != 0) {
            std::snprintf(why, size, "monster %d still goes after the partner", i);
            return false;
        }
    }

    return true;
}

void CombatCancel(std::uint16_t buttons) {
    // The wheel test's mutation twin: nothing is dropped.
    if (Mutated("wheelcancel")) {
        return;
    }

    g_combat.guard = 0; // coop-swapfix

    if (g_combat.throw_stage != 0) { // coop-p2throw: a throw wound up and not let go is dropped, its item kept
        g_combat.throw_stage = 0;
        g_combat.throw_item = -1;
        g_partner.attacking = false;
        PlayMotion(0, 0);
    }

    // A charge held when something else takes pad 2 is dropped without its blow; a swing already
    // under way plays out.
    if (g_combat.charging) {
        g_combat.charging = false;
        g_combat.charged_blow = false;
        g_combat.scale = 1.0f;
        g_partner.attacking = false;
        PlayMotion(0, 0);
    }

    g_combat.held = false;
    g_combat.aiming = false;
    g_combat.panel = false;
    g_combat.circle_ticks = kLockReleaseTicks;

    // Buttons held now are not pressed again when pad 2 comes back.
    g_combat.buttons = buttons;
    g_combat.cross_down = false;
}

namespace {

// Whether pos stands in a part's healing zone or its healing water: CheckHealZone's and
// CheckHealingWater's tests, taken at the partner's position (theirs read the player's and set the
// player's in_water and splash, which this leaves alone).
bool InHealingWater(const float *pos) {
    if (NowDngMap == nullptr) {
        return false;
    }

    int column = (int) ((80.0f + pos[0]) / 160.0f);
    int row = (int) ((80.0f + pos[2]) / 160.0f);

    if (column >= 0 && column < 20 && row >= 0 && row < 20) {
        int parts_no = NowDngMap->cells[row * 20 + column].parts_no;

        if (parts_no != MAP_PARTS_NONE) {
            const CDungeonParts &part = NowDngMap->parts[parts_no];

            if (part.heal_on != 0) {
                float cx = part.heal_pos[0] + 160.0f * column;
                float cz = part.heal_pos[2] + 160.0f * row;

                if (pos[0] >= cx - part.heal_width / 2.0f && pos[0] <= cx + part.heal_width / 2.0f && pos[2] >= cz - part.heal_depth / 2.0f &&
                    pos[2] < cz + part.heal_depth / 2.0f && pos[1] < part.heal_pos[1]) {
                    return true;
                }
            }
        }
    }

    const PARTS_WATER *nearest = nullptr;
    float              nearest_distance = 160.0f;
    float              water_at[4] = {0.0f, 0.0f, 0.0f, 1.0f};

    for (int r = 0; r < 16; r++) {
        for (int c = 0; c < 16; c++) {
            int parts_no = NowDngMap->cells[r * 20 + c].parts_no;

            if (parts_no == -1 || NowDngMap->parts[parts_no].water.used == 0) {
                continue;
            }

            const PARTS_WATER &water = NowDngMap->parts[parts_no].water;
            float              at[4] = {160.0f * c, water.vertex[0][1], 160.0f * r, 1.0f};
            float              distance = DistVector((float *) pos, at);

            if (distance < nearest_distance) {
                nearest = &water;
                nearest_distance = distance;
                sceVu0CopyVector(water_at, at);
            }
        }
    }

    return nearest != nullptr && pos[0] >= water_at[0] + nearest->vertex[0][0] && pos[0] < water_at[0] + nearest->vertex[3][0] &&
           pos[2] >= water_at[2] + nearest->vertex[0][2] && pos[2] < water_at[2] + nearest->vertex[3][2] && pos[1] < water_at[1];
}

}  // namespace

CHealEffect g_heal;

void PartnerSpringHeal() {
    int chara = g_partner.loaded_chara;

    // HealingWater's sparkle, at the partner: set off when it is not already playing and the
    // partner's life is not full.
    if (g_heal.active == 0 && Life() != MaxLife() && !Mutated("sparkle")) {
        g_heal.Set(g_partner.pos);
        PartnerVoiceSound(SE_CHARA_RECOVER); // coop-wellsound: her member's voice
    }

    UserStatus->AddDrink(chara, 0xFF, 255.0f);
    SetLife(MaxLife());
    g_combat.heal_wait = 10;
}

namespace {

// The partner's own thirst, CUserStatus::Step's for the controlled character applied to the
// partner's water (UserStatus' water_now of its member): it drains while the partner moves (the
// player's drains while the stick is pushed), at the dungeon's rate, five times faster in a thirst
// zone and as its own weapon's flags say; empty, it costs a point of life every two seconds. Healing
// water refills it and heals it every ten ticks, as HealingWater does the player's.
void StepThirst() {
    int chara = g_partner.loaded_chara;

    if (chara < 0) {
        return;
    }

    // coop-pt1109c test hook (DC_COOP_TEST_DRY=K, x-atla-beside): at K on the press clock, both players' water to 0, both copies alike.
    if (static const int dry_at = TestEnvInt("DC_COOP_TEST_DRY", -1); dry_at >= 0) {
        static bool done = false;

        if (!done && NetplayTestPressClock() >= dry_at) {
            done = true;
            UserStatus->water_now[chara] = 0.0f;
            UserStatus->water_now[UserStatus->cur_chara] = 0.0f;
            std::fprintf(stderr, "coop-drymes: test: both players' water to 0 at clock %lld\n", (long long) NetplayTestPressClock());
        }
    }

    PartnerDryStep(chara, UserStatus->water_max[chara], UserStatus->water_now[chara]); // coop-p2hurt: her thirst's messages

    // The weapon in the partner's hand follows the save: a change made anywhere (the Start menu's
    // equip, build-up or synthesis, a break) loads the new one.
    const WEAPON_HAVE *equipped = EquippedWeapon(chara);
    int                held = equipped != nullptr ? equipped->item_no : -1;
    int                slot = UserStatus->equipped_weapon_slot[chara];

    if ((held != g_combat.held_item || slot != g_combat.held_slot) && chara == g_combat.chara) {
        if (g_combat.held_item != -2 && !Mutated("stale")) {
            std::printf("coop: P2's weapon is now %d (slot %d): hand model %s\n", held, slot, ReloadWeapon() ? "reloaded" : "unchanged");
            g_combat.reloads++;
        }

        g_combat.held_item = held;
        g_combat.held_slot = slot;
    }

    // A weapon that heals (flag 0x800) gives its holder a point of life every four seconds, as the
    // game's tick gives the player's.
    if (equipped != nullptr && (equipped->flags & 0x800) != 0 && g_partner.down == 0 && Life() > 0) {
        if (++g_combat.regen_ticks >= 240) {
            g_combat.regen_ticks = 0;
            SetLife(std::min(MaxLife(), Life() + 1));
        }
    }

    // Ailments, as BtStatusErrStep and the battle tick run the player's: poison takes 4% of max life
    // every three seconds; freeze and stamina wear off with their frames; goo halves the walk.
    {
        s32 &ailments = UserStatus->ailments[chara];

        // Jep's rule: no poison bite through her own presentation either (its clock waits, as for blows in TakeHits).
        bool opening = (PartnerPresenting() || (UsePromptOpen(2) && !Mutated("prompt_runs"))) && !Mutated("openhurt");

        if ((ailments & AILMENT_POISON) != 0 && !opening && ++g_combat.poison_ticks >= 180) {
            g_combat.poison_ticks = 0;

            int damage = (int) (0.04 * UserStatus->max_hp[chara]);
            int life = std::max(0, Life() - std::max(1, damage));

            SetLife(life);
            std::printf("coop: P2 poison costs %d, life %d/%d (clock %lld)\n", std::max(1, damage), life, MaxLife(),
                        (long long) NetplayTestPressClock());

            // coop-p2hurt: the bite shows its number over her, as BtStatusErrStep shows the player's; at no life she lies
            // down, as the player's poison ends in his death. DC_COOP_MUTATE=poisondown (twin): she stands on at 0.
            sceVu0FVECTOR mark;

            sceVu0CopyVector(mark, g_partner.pos);
            mark[1] += kHeight[chara];
            HitValueEntry(NowHitValue, mark, std::max(1, damage), HIT_VALUE_PLAYER, nullptr);

            if (life <= 0 && g_partner.down == 0 && !Mutated("poisondown")) {
                PartnerFalls("poison");
                return;
            }
        }

        if ((ailments & (AILMENT_FREEZE | AILMENT_STAMINA)) != 0 && UserStatus->ailment_frames[chara] > 0 &&
            --UserStatus->ailment_frames[chara] == 0) {
            ailments = 0;
        }

        if ((ailments & AILMENT_GOO) != 0) {
            g_partner.velocity[0] *= 0.5f;
            g_partner.velocity[2] *= 0.5f;
        }
    }

    if (UserStatus->step_disable != 0 || Mutated("thirst")) {
        return;
    }

    if (g_combat.heal_wait > 0) {
        g_combat.heal_wait--;
    } else if (!Mutated("spring") && InHealingWater(g_partner.pos)) {
        PartnerSpringHeal();
    }

    // The partner's heal sparkle plays on in the tick, as the game steps the player's.
    if (g_heal.active != 0) {
        g_heal.Step();
    }

    bool moving = g_partner.velocity[0] != 0.0f || g_partner.velocity[2] != 0.0f;

    if (!moving || g_partner.down > 0 || (PartnerPresenting() && !Mutated("openhurt"))) { // and no thirst through her presentation
        return;
    }

    float &water = UserStatus->water_now[chara];
    float  drain = 0.003f * (1.0f + 0.2f * UserStatus->cur_georama);

    if (water > 0.0f) {
        const WEAPON_HAVE *weapon = EquippedWeapon(chara);

        if (UserStatus->res_limit_zone_current == RES_LIMIT_ZONE_THIRST) {
            drain *= 5.0f;
        }

        if (weapon != nullptr && (weapon->flags & 0x8) != 0) {
            drain *= 0.8f;
        }

        if (weapon != nullptr && (weapon->flags & 0x10) != 0) {
            drain *= 2.0f;
        }

        water = std::max(0.0f, water - drain);
        return;
    }

    water = 0.0f;
    UserStatus->thirst_damage[chara] += 0.008333334f;

    if (UserStatus->thirst_damage[chara] >= 1.0f) {
        UserStatus->thirst_damage[chara] = 0.0f;

        int life = std::max(0, Life() - 1);

        SetLife(life);
        std::printf("coop: P2 thirst costs a point of life, %d/%d\n", life, MaxLife());

        if (life <= 0) {
            PartnerFalls("thirst");
        }
    }
}

} // namespace

// Osmond's propeller ("tukene1", and "obj46" in its shadow) turns 0.357 a tick, as the battle tick
// turns the player's; angle is the model's own. The dungeon partner's (StepParts) and the town
// partner's (coop_town.cpp: retail never has Osmond walk a town, so nothing turned it there).
void SpinPropeller(CCharacter *chara, float *angle) {
    if (chara == nullptr || chara->frame == nullptr || Mutated("propeller")) {
        return;
    }

    CFrame *propeller = chara->frame->SearchFrame((char *) "tukene1");
    CFrame *shadow = chara->shadow_frame != nullptr ? chara->shadow_frame->SearchFrame((char *) "obj46") : nullptr;

    *angle += 0.35699910f;

    if (!(*angle < PI_SHORT)) {
        *angle -= TWO_PI_SHORT;
    }

    if (propeller != nullptr) {
        propeller->SetRotType(2);
        propeller->SetRotation(0.0f, AngleLimit(*angle), 0.0f);
    }

    if (shadow != nullptr) {
        shadow->SetRotType(2);
        shadow->SetRotation(0.0f, AngleLimit(*angle), 0.0f);
    }
}

// The propeller's turn as drawn: its world x axis in its parent's world axes, -9 when the model has
// no propeller. What the tests read, rather than the rotation asked for.
float PropellerTurn(CCharacter *chara) {
    CFrame *propeller = chara != nullptr && chara->frame != nullptr ? chara->frame->SearchFrame((char *) "tukene1") : nullptr;

    if (propeller == nullptr || propeller->parent == nullptr) {
        return -9.0f;
    }

    sceVu0FMATRIX own;
    sceVu0FMATRIX parent;

    propeller->GetLWMatrix(own);
    propeller->parent->GetLWMatrix(parent);
    return std::atan2(sceVu0InnerProduct(own[0], parent[2]), sceVu0InnerProduct(own[0], parent[0]));
}

// The parts the game turns in code for the controlled character, for the partner's model: Osmond's
// propeller ("tukene1", and "obj46" in its shadow) spins 0.357 a tick as the battle tick spins the
// player's; Xiao locked on looks at the monster ("jnt2_1"), as the player's does.
void StepParts() {
    CCharacter *partner = g_partner.chara;

    if (partner == nullptr || partner->frame == nullptr) {
        return;
    }

    if (g_partner.loaded_chara == CHARA_OSMOND) {
        SpinPropeller(partner, &g_combat.propeller);
    }

    float at[4];

    if (g_partner.loaded_chara == CHARA_XIAO && LockPoint(at)) {
        partner->motion_type.state.look_frame = (CFrameVu1 *) partner->frame->SearchFrame((char *) "jnt2_1");
        sceVu0CopyVector(partner->motion_type.state.look_position, at);
        partner->motion_type.state.look_at = true;
        partner->motion_type.state.look_constraint = FRAME_CONSTRAINT_X;
    } else {
        partner->motion_type.state.look_at = false;
    }
}

// coop-pt1111f test hook (DC_COOP_TEST_LIMIT_ZONE=Z): once a state file is put back, the floor is in limited zone Z
// (res_limit_zone_current, as a floor's zone script sets it) and its message is raised (CDngMessageMan::LimmitZone, as the
// floor's start does: 10, "Limited zone / Cannot disarm weapon"). Both linked copies on the same tick.
static void LimitZoneForTest() {
    static const int zone = TestEnvInt("DC_COOP_TEST_LIMIT_ZONE", -1);
    static bool      done = false;

    if (zone < 0 || done || UserStatus == nullptr || !NetplayStateFileLoaded()) {
        return;
    }
    done = true;
    UserStatus->res_limit_zone_current = zone;
    // coop-pt1111h: DC_COOP_TEST_LIMIT_ZONE_QUIET=1: the zone alone, no message (the players on the floor without its start:
    // arrived on it, as a merge or a join brings them).
    if (static const bool quiet = TestEnvOn("DC_COOP_TEST_LIMIT_ZONE_QUIET"); !quiet) {
        DngMessMan.LimmitZone();
    }
    std::fprintf(stderr, "coop-test: limited zone %d: the dungeon's message %d raised (timer %d) at link tick %lld\n", zone, (int) DngMessMan.message,
                 (int) DngMessMan.timer, (long long) NetplayLinkTick());
    std::fflush(stderr);
}

void CombatFace() {
    LimitZoneForTest(); // coop-pt1111f
    StepThirst();
    StepParts();

    // The motion test's twin: the partner's motion stands still on its frame.
    if (Mutated("motion") && g_partner.chara != nullptr) {
        g_partner.chara->motion_type.state.time = (float) g_partner.chara->motion_type.motion_info[g_partner.motion].start;
    }

    if (g_partner.down > 0 || g_combat.hurt != 0) { // coop-p2hurt: she faces the blow
        return;
    }

    CFrameVu1 *frame = (CFrameVu1 *) g_partner.chara->frame;
    float      at[4];

    if (g_combat.aiming) {
        frame->SetRotation(0.0f, g_combat.aim_yaw, 0.0f);
    } else if (LockPoint(at)) {
        float face = atan2f(at[0] - g_partner.pos[0], at[2] - g_partner.pos[2]);

        // coop-strafe: stepping, she faces the monster outright, as the player does (DunMoveChara: stick_strength over 0);
        // at rest she turns to it a step a tick.
        if (Strafing() && (g_partner.velocity[0] != 0.0f || g_partner.velocity[2] != 0.0f)) {
            frame->SetRotation(0.0f, face, 0.0f);
        } else {
            frame->SetRotation(0.0f, unitRotation(frame, face), 0.0f);
        }
    }
}

bool SpeedBoosted() {
    return g_combat.boost;
}

bool Strafing() {
    return g_combat.lock >= 0 && !g_combat.aiming && g_partner.down == 0 && !Mutated("strafe");
}

// DunMoveChara's lock-on motions: the battle stance (0x12) at rest; stepping, by the angle between the way she faces
// and the way she steps, forward 0x15, back 0x16, to either side 0x13 or 0x14, played at 0.2 plus the stick's
// strength (at most 1).
int StrafeMotion(float move_x, float move_z, int walk) {
    float at[4];

    if (!LockPoint(at)) {
        return walk;
    }

    MOTION_INFO *info = g_partner.chara->motion_type.motion_info;
    float        face = atan2f(at[0] - g_partner.pos[0], at[2] - g_partner.pos[2]);
    float        strength = sqrtf(move_x * move_x + move_z * move_z);
    int          motion = 0x12;

    if (strength > 0.0f) {
        float turn = face - atan2f(move_x, move_z);

        if (turn < -PI_SHORT) {
            turn += TWO_PI_SHORT;
        }

        if (turn > PI_SHORT) {
            turn -= TWO_PI_SHORT;
        }

        if (turn > -1.2f && turn < 1.2f) {
            motion = 0x15;
        }

        if (turn < -2.6f || turn > 2.6f) {
            motion = 0x16;
        }

        if (turn > 1.2f && turn < 2.6f) {
            motion = 0x13;
        }

        if (turn < -1.2f && turn > -2.6f) {
            motion = 0x14;
        }
    }

    if (!HasMotion(motion)) {
        return walk;
    }

    info[0x12].speed = 0.2f;

    if (strength > 0.0f) {
        info[motion].speed = std::min(0.2f + strength, 1.0f);
    }

    return motion;
}

int UsableItems(int *out, int max) {
    const ITEM_PACK &pack = PartnerPack(); // coop-inventory: P2's own bag
    int              count = 0;

    for (int i = 0; i < pack.num && i < 103 && count < max; i++) {
        int item = pack.item[i];

        if (UseOf(item) == ITEM_USE_NONE || std::find(out, out + count, item) != out + count) {
            continue;
        }

        out[count++] = item;
    }

    return count;
}

int ItemCount(int item) {
    // coop-inventory: P2's own bag, its quick-use slots counted.
    return BagCount(item);
}

int WeaponSlots(int *out, int max) {
    int chara = g_partner.loaded_chara;
    int count = 0;

    for (int slot = 0; slot < 11 && count < max; slot++) {
        if (UserStatus->chara_weapons[chara][slot].item_no > 0) {
            out[count++] = slot;
        }
    }

    return count;
}

} // namespace coop

using namespace coop;

void CoopDrawShots() {
    CoopPerfScope perf(kCoopPerfDraw); // coop: perf
    if (!g_partner.visible) {
        return;
    }

    // CSHOT::draw's sprite: the cell of basefx01 the character's slingshot ammunition picks.
    int                chara = g_partner.loaded_chara;
    const WEAPON_HAVE *weapon = EquippedWeapon(chara);
    int                cell = chara == CHARA_XIAO && weapon != nullptr ? std::max(0, weapon->item_no - 300) : 0;

    for (int i = 0; i < kShots; i++) {
        Shot &shot = g_partner.shots[i];

        if (shot.life <= 0) {
            continue;
        }

        // Osmond's bullets fly unseen, as CSHOT_MACHINGUN's do, but for a spark of basefx00 at the tip.
        if (chara == CHARA_OSMOND) {
            set3DCellModel(shot.pos, (char *) "basefx00", 2.5f * g_combat.shot_scale[i], 0, 0x70, 0x10, 0x10, 0x80);
        } else {
            set3DCellModel(shot.pos, (char *) "basefx01", g_combat.shot_scale[i] * (chara == CHARA_RUBY ? 1.6f : 1.0f), (cell % 4) << 5,
                           (cell / 4) << 5, 32, 32, 128);
        }
    }

    for (Throw &thrown : g_combat.throws) {
        if (thrown.life > 0) {
            set3DCellModel(thrown.pos, (char *) "basefx01", 1.5f, 0, 0, 32, 32, 128);
        }
    }

    if (g_ring.loaded) {
        g_ring.effect.Draw();
    }

    // The partner's heal sparkle, as CHealEffect::Draw draws the player's, around the partner (Draw
    // itself centres it on the player).
    if (g_heal.active != 0) {
        CTexture *texture = TexManager.GetTexture("basefx00", -1); // CHealEffect::Draw's (HealEffectTextureName at 6ec3b808)

        sceVu0CopyVector(g_heal.position, g_partner.pos);
        g_heal_drawn_at[0] = g_partner.pos[0];
        g_heal_drawn_at[1] = g_partner.pos[1];
        g_heal_drawn_at[2] = g_partner.pos[2];

        for (int i = 0; i < 32; i++) {
            if (g_heal.phase[i] < PI) {
                float world[4] = {g_heal.position[0] + g_heal.particle_offset[i][0], g_heal.position[1] + g_heal.particle_offset[i][1],
                                  g_heal.position[2] + g_heal.particle_offset[i][2], 1.0f};

                BtSet3DCellModel(world, texture, g_heal.size[i], 0x10, 0, 0x20, 0x20, (int) g_heal.alpha[i]);
            }
        }
    }

    // coop-p2throw: her throw's landing point, retail's bomb cursor (BombInfo.aiming's), on both copies.
    if (g_combat.throw_stage == 1 && bombCursorFrame != nullptr) {
        bombCursorFrame->SetPosition(g_combat.throw_aim);
        MGDraw(bombCursorFrame);
    }

    // Aiming a shot, a line of dots along its way.
    if (g_combat.aiming && Ranged(chara)) {
        float flat = cosf(g_combat.aim_pitch);
        float dot[4];

        for (int i = 1; i <= 10; i++) {
            dot[0] = g_partner.pos[0] + 12.0f * i * flat * sinf(g_combat.aim_yaw);
            dot[1] = g_partner.pos[1] + 10.0f + 12.0f * i * sinf(g_combat.aim_pitch);
            dot[2] = g_partner.pos[2] + 12.0f * i * flat * cosf(g_combat.aim_yaw);
            dot[3] = 1.0f;
            set3DCellModel(dot, (char *) "basefx01", 0.4f, 0, 0, 32, 32, 96);
        }
    }

    // The partner's lock-on cursor: DrawtargetCursor's four corners round the monster, tinted dim so it
    // reads apart from the player's. coop-lockhud: on her own copy at full strength, and on the player's
    // only while the monster is near him (LockHudPartnerCursor).
    float at[4];

    if (!LockPoint(at)) {
        return;
    }

    int alpha = LockHudPartnerCursor(g_combat.lock);

    if (alpha != 0) {
        // coop-lockon1118: sized by her count, as setTargetCursor sizes his (12 + count by 6 + count / 2): it closes in on a new lock
        // and opens out as the lock slips (it was a still 14 by 7).
        const float count = g_combat.lock_cursor;

        DrawLockCorners(at, (12.0f + count) * NowMonstorUnit->monster[g_combat.lock].lockon_scale_x,
                        (6.0f + count / 2.0f) * NowMonstorUnit->monster[g_combat.lock].lockon_scale_y, alpha, LockHudP2Teal());
    }
}

// The trap a chest the partner opened springs (event scripts 0xF and 0x10, coop_interact): its bomb and
// its ailment go off where the partner stands and on the partner, not on the player. The flag is set
// when the partner starts the script and dropped once play (and pad 2) is back.
namespace dc::game {
extern s32 gameTask;         // ps2/src/dun/gameloop.cpp (gaps2)
extern const std::array<s32, 6> defWeapon__6; // game/dun/gameloop.cpp: each member's broken starter weapon (gaps2)
} // namespace dc::game

namespace coop {

bool g_trap_by_partner = false;

void TrapOpenedByPartner() {
    g_trap_by_partner = !Mutated("trap");
}

// coop: gaps2
// Retail's trap circles test only where the player stands (CDungeonMap::DistTrapCircle) and act on the
// controlled character (Run_TrapCircle, ps2/src/dun/gameloop.cpp). This is the same for the partner:
// within 5 units of a ready circle (state 1) it goes off (state 2, StepTrapCircle plays it out), with
// the circle's message and sound, on the partner's member: Stamina on its own ailments, the weapon
// circles (ABS full, WHP up, cure, down, quarter, stat down) on its own equipped weapon, money up and
// down on its purse, all monsters' stamina as retail. A member whose weapon is its broken starter gets
// Stamina, as retail gives the player. DC_COOP_MUTATE=trapcircle leaves the circles to the player.

void PartnerTrapCircle() {
    int chara = g_partner.loaded_chara;

    if (Mutated("trapcircle") || chara < 0 || chara >= 6 || NowDngMap == nullptr || gameTask != GAME_TASK_PLAY) {
        return;
    }

    MAP_TRAP_CIRCLE *trap = nullptr;

    for (MAP_TRAP_CIRCLE &circle : NowDngMap->trap_circle) {
        if (circle.state == 1 && DistVector(circle.pos, g_partner.pos) <= 5.0f) {
            circle.state = 2;
            trap = &circle;
            break;
        }
    }

    if (trap == nullptr) {
        return;
    }

    WEAPON_HAVE *weapon = (WEAPON_HAVE *) EquippedWeapon(chara);
    int          kind = trap->kind;
    int          element = -1;
    int          se = 0;
    int          gilda = PartnerGilda();

    if (weapon != nullptr && (weapon->item_no == defWeapon__6[chara] || weapon->item_no == defWeapon__6[chara] + 1)) {
        kind = TRAP_CIRCLE_STAMINA;
    }

    switch (kind) {
        case TRAP_CIRCLE_STAMINA:
            PartnerSetAilment(AILMENT_STAMINA);
            se = 0xE1;
            break;
        case TRAP_CIRCLE_MONEY_UP:
            PartnerSetGilda(gilda + (int) (gilda * 1.2f) + 10);
            se = 0xE1;
            break;
        case TRAP_CIRCLE_ABS_FULL:
            element = RGATE_ABS_FULL;
            se = 0xE1;
            break;
        case TRAP_CIRCLE_WHP_UP:
            element = RGATE_WHP_UP;
            se = 0xE1;
            break;
        case TRAP_CIRCLE_WHP_CURE:
            element = RGATE_WHP_CURE;
            se = 0xE1;
            break;
        case TRAP_CIRCLE_ALL_MONSTER_STAMINA:
            NowMonstorUnit->AllBin2();
            se = 0xE2;
            break;
        case TRAP_CIRCLE_MONEY_DOWN:
            PartnerSetGilda((int) (gilda - 0.2f * gilda));
            se = 0xE2;
            break;
        case TRAP_CIRCLE_STAT_DOWN:
            element = RGATE_STAT_DOWN;
            se = 0xE2;
            break;
        case TRAP_CIRCLE_WHP_DOWN:
            element = RGATE_WHP_DOWN;
            se = 0xE2;
            break;
        case TRAP_CIRCLE_WHP_QUARTER:
            element = RGATE_WHP_QUARTER;
            se = 0xE2;
            break;
    }

    if (element != -1 && weapon != nullptr) {
        WeaponDataChangeByRGate(weapon, element);
    }

    SndSePlay(se, -1, 0);
    {
        MessageBy by(2); // coop-itemprompt: on her screen only
        SetSystemMes(kind + 0x12C, 0x96, MES_POS_BOTTOM, 0, NULL, NULL);
    }
    DngMessMan.hide_timer = 0x96;
    std::printf("coop: P2 springs trap circle kind %d (as %d) at (%.0f %.0f %.0f)\n", trap->kind, kind, trap->pos[0], trap->pos[1],
                trap->pos[2]);
}

} // namespace coop

namespace {

int TrapStackInt(RS_STACKDATA *argument) {
    return argument->type == RS_FLOAT ? (int) argument->f : argument->i;
}

} // namespace

namespace dc::game { // repin: was PORT_OVERRIDE; a recipe removes the game's own definition
int _BOM_SET(RS_STACKDATA *stack, int argument_count) {
    sceVu0FVECTOR pos;
    sceVu0FVECTOR push = {0.0f, 0.0f, 1.0f, 1.0f};
    sceVu0FVECTOR direction;

    // coop: combat (the partner's trap goes off at the partner)
    sceVu0CopyVector(pos, coop::g_trap_by_partner ? coop::g_partner.pos : CharaMain.pos);
    pos[1] += 16.0f;
    pos[2] -= 3.0f;
    int bomb = SetBombEffect(pos, 1, selectMapNo * 20 + 20, 0.8f);

    if (bomb != -1) {
        CCollisionData *collision = NowColData;
        sceVu0Normalize(direction, push);
        sceVu0ScaleVectorXYZ(collision->hit[bomb].velocity, direction, 0.8f);
    }

    BtActStatus.event_block_frames = 30;
    return 1;
}
} // namespace dc::game

namespace dc::game { // repin: was PORT_OVERRIDE; a recipe removes the game's own definition
int _SET_STATUS_ERR(RS_STACKDATA *stack, int argument_count) {
    sceVu0FVECTOR pos;

    // coop: combat (the partner's trap ails the partner)
    if (coop::g_trap_by_partner) {
        coop::PartnerSetAilment(TrapStackInt(stack));
        sceVu0CopyVector(pos, coop::g_partner.pos);
    } else {
        BtSetStatusErr(TrapStackInt(stack));
        sceVu0CopyVector(pos, CharaMain.pos);
    }

    pos[1] += 16.0f;
    NowColData->Set(pos, 2, 2, 5.0f, 0.0f, 1, 2, 0, 0);
    return 1;
}
} // namespace dc::game

// coop-menus: player 2's overlay (coop_menu.cpp) uses an item and equips a weapon as the panel does.
namespace coop {
bool MenuPartnerUseItem(int item) {
    int before = ItemCount(item);

    UseItem(item);
    return ItemCount(item) < before;
}

void MenuPartnerEquip(int slot) {
    Equip(slot);
}
} // namespace coop

namespace coop {

// The player's own down in co-op (Jep's rule): while the partner is up, a player with no life left
// does not end the run; it lies down as the partner does (the dead pose, no moving or acting, no hits)
// until player 2 uses a Revive Powder on it (half its life; coop-revive2), or, in debug mode only, after kDownTicks
// (3 s) with a quarter. When both are down the game's own death runs, to its game over
// and out of the dungeon. A Revival Powder still works first, as the game's death would use it.
bool PlayerGoesDown() {
    if (!Enabled() || !g_partner.visible || g_partner.loaded_chara < 0 || g_partner.down > 0 || Life() <= 0 || Mutated("p1down")) {
        return false;
    }

    if (((CDngStatusData *) UserStatus)->CheckActItemSlot(ITEM_REVIVAL_POWDER) != -1) {
        return false;
    }

    int chara = UserStatus->cur_chara;

    UserStatus->hp[chara] = 0;
    UserStatus->ailments[chara] = 0;
    UserStatus->ailment_frames[chara] = 0;
    LockOffTargte();
    g_combat.player_down = kDownTicks;
    g_combat.player_revive = 0;
    BtActStatus.movement_locked = 1;
    BtActStatus.can_act = 0;
    BtActStatus.action_on = 0;
    BtActStatus.invincible_frames = std::max<int>(BtActStatus.invincible_frames, 2);
    SndSePlay(SE_CHARA_KNOCKED_OUT, -1, 0);
    std::printf("coop: the player is down; P2 plays on\n");
    return true;
}

namespace {

void PlayerGetsUp(int life) {
    int chara = UserStatus->cur_chara;

    UserStatus->hp[chara] = (s16) std::max(1, life);
    UserStatus->next_hp[chara] = UserStatus->hp[chara];
    UserStatus->life_step[chara] = 0;
    g_combat.player_down = 0;
    g_combat.player_revive = 0;
    BtActStatus.movement_locked = 0;
    BtActStatus.can_act = 1;
    BtActStatus.motion_no = 0;
    // coop-p2hurt2: up as retail revives him (GAME_TASK_REVIVE_DONE): invincible 0xA0 and his model's red pulse over 160 draws
    // (it was 120 and no pulse); the partner's GetUp does the same for hers.
    BtActStatus.invincible_frames = kReviveInvincible;
    setUnitAmbientAnime(160.0f, 1.0f, 255.0f, 0.0f, 0.0f);
    std::fprintf(stderr, "coop-p2hurt2: player 1 up: invincible %d\n", (int) BtActStatus.invincible_frames);
    std::printf("coop: the player gets up, life %d/%d\n", (int) UserStatus->hp[chara], (int) UserStatus->max_hp[chara]);
}

} // namespace

int PlayerDownStep() {
    if (g_combat.player_down <= 0) {
        ReviveAsk(2, 1, false); // coop-revive: up some other way: no question left waiting
        return 0;
    }

    int chara = UserStatus->cur_chara;

    // Both down: the game's own death, to its game over.
    if (!g_partner.visible || g_partner.down > 0) {
        g_combat.player_down = 0;
        BtActStatus.movement_locked = 0;
        BtActStatus.can_act = 1;
        std::printf("coop: both players are down\n");
        return 2;
    }

    UserStatus->hp[chara] = 0;
    BtActStatus.movement_locked = 1;
    BtActStatus.can_act = 0;
    BtActStatus.invincible_frames = std::max<int>(BtActStatus.invincible_frames, 2);

    // coop-revivecross: the one rule the red mark reads too (near, up, and no nearer chest or door in reach of her Cross)
    const bool near = ReviveOffered(2, 1);

    // coop-revive2 (Jep 11-18): player 2 near asks with Cross whether to use a Revive Powder (coop_prompt.hpp); the free get-up after
    // kDownTicks (3 s) only in debug mode (ReviveFreeGetUp; DC_COOP_MUTATE=getup_base, the twin: in the base game too, as before).
    if (ReviveAsk(2, 1, near)) {
        PlayerGetsUp(UserStatus->max_hp[chara] / 2);
        std::printf("coop: P2 revives the player with a Revive Powder\n");
    } else if ((ReviveFreeGetUp() || Mutated("getup_base")) && --g_combat.player_down == 0) {
        PlayerGetsUp(UserStatus->max_hp[chara] / 4);
    }

    return 1;
}

bool PlayerLiesDown() {
    return g_combat.player_down > 0 && !Mutated("p1spin");
}

bool PlayerDown(int player) {
    if (!Enabled()) {
        return false;
    }
    if (player == 1) {
        return PlayerLiesDown();
    }
    return player == 2 && g_partner.visible && g_partner.down > 0;
}

void DownPlayerLeaves(int player) {
    if (!PlayerDown(player) || UserStatus == nullptr) {
        return;
    }
    if (player == 1) {
        PlayerGetsUp(UserStatus->max_hp[(int) UserStatus->cur_chara] / 4);
    } else if (player == 2) {
        PartnerUpToLeave(MaxLife() / 4);
    }
    std::printf("coop-downmenu: player %d, down, leaves the dungeon by the menu and gets up\n", player);
}

void DownTestTick() {
    static const char *spec = TestEnv("DC_COOP_TEST_DOWN");
    static const bool  trace = TestEnvOn("DC_COOP_TEST_DOWNTRACE");
    const long         now = (long) NetplayTestPressClock();
    int                who = 0;
    long               when = -1;

    if (spec != nullptr && std::sscanf(spec, "%d:%ld", &who, &when) == 2 && now == when && UserStatus != nullptr) {
        if (who == 1 && !PlayerDown(1)) {
            UserStatus->hp[(int) UserStatus->cur_chara] = 0;
            BtActStatus.invincible_frames = 0;
            std::printf("coop-downmenu: test: player 1 goes down at press clock %ld: %s\n", now, PlayerGoesDown() ? "down" : "refused");
        } else if (who == 2 && g_partner.visible && g_partner.down == 0) {
            SetLife(0);
            PartnerFalls("the downmenu test");
            std::printf("coop-downmenu: test: player 2 goes down at press clock %ld: down\n", now);
        }
    }

    if (!trace || now < 0 || now % 30 != 0 || NowMonstorUnit == nullptr) {
        return;
    }
    for (int player = 1; player <= kMaxPlayers; player++) {
        if (player > 2 || (player == 2 && !g_partner.visible)) {
            continue; // (no body on the floor past player 2 yet)
        }
        const int lock = player == 1 ? (lockOnTargetFlag != 0 ? (int) lockOnTargetNo : -1) : g_combat.lock;
        const int ready = player == 1 && lockOnTargetFlag == 0 ? (int) lockOnTargetNo : -1;
        int       after = 0;

        for (int i = 0; i < 16; i++) {
            after += NowMonstorUnit->monster[i].state == 2 && NowMonstorUnit->monster[i].hp > 0 && CoopMonsterTarget(i) == player - 1 ? 1 : 0;
        }
        std::printf("coop-downmenu: clock %ld player %d down %d lock %d ready %d monsters after it %d\n", now, player, (int) PlayerDown(player), lock,
                    ready, after);
    }
}

bool BothDown() {
    return Enabled() && g_partner.loaded_chara >= 0 && (g_partner.down > 0 || !g_partner.visible) && !Mutated("bothdown");
}

} // namespace coop

// coop-kojoin: P2 came to the floor of a knocked-out P1. Apart, alone on it, his knockout ran the game's own way, to the
// member change ("Use Stand-in Powder / Leave"); once she is linked there with him, the co-op down rule holds instead: he
// lies down and she plays on. His knockout's member change asks this each tick (DunMoveChara's GAME_TASK_DEAD_CHANGE_CHARA
// and its menu's key step, CharaChangeKey): true ends it as a cancel does, and the game's own check of his life then takes
// him down (coop::PlayerGoesDown). The same game state on both copies, so the same tick on both. Before this, her copy
// showed his menu over black (the menu draws over a frame the game captured as it opened, and her copy never drew that
// frame) and her Xiao never came. DC_COOP_MUTATE=ko_stays (twin): his menu stays, as before.
bool CoopKnockoutEndsForPartner() {
    using namespace coop;

    if (!Enabled() || (gameTask != GAME_TASK_DEAD_CHANGE_CHARA && gameTask != GAME_TASK_CHARA_SELECT_LOOP) || UserStatus == nullptr ||
        UserStatus->hp[UserStatus->cur_chara] > 0 || !NetplayConfigured() || CoopWorldsApart() || !NetplayPartnerLinked() || g_partner.down > 0 ||
        Mutated("ko_stays")) {
        return false;
    }

    std::fprintf(stderr, "coop: P2 is here with the knocked-out player: his member change ends (task %d), he lies down\n", (int) gameTask);
    return true;
}

namespace coop {

// coop-pt1111b (item 14, DC_COOP_DEBUG): a thunder hit's bolts as this copy drew them (weaponelement.cpp's Draw_Thunder), summed
// over its frames and said once every 30 frames it drew any: how many it drew, how many had an end behind this copy's view and
// how many of those it drew all the same (0 unless DC_COOP_MUTATE=bolts_behind, retail's). The rows read it.
void ElementBoltsDrawn(int kind, int drawn, int behind, int behind_drawn, const float *at) {
    static const bool debug = CoopDebug();
    static int        frames = 0, total = 0, behind_total = 0, behind_drawn_total = 0;

    if (!debug || drawn + behind == 0) {
        return;
    }
    frames++;
    total += drawn;
    behind_total += behind;
    behind_drawn_total += behind_drawn;
    if (frames == 1 || frames % 30 == 0) {
        std::fprintf(stderr, "coop-fx: p%d element %d bolts at (%.0f %.0f %.0f) over %d frames: drawn %d, an end behind the view %d, of those drawn %d\n",
                     NetplayLocalPlayer(), kind, at[0], at[1], at[2], frames, total, behind_total, behind_drawn_total);
    }
}

} // namespace coop
