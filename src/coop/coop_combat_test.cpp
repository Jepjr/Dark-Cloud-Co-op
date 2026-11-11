// The co-op combat test bot (DC_COOP_TEST=charge|lock|aim|item|weapon|panel|stats): an in-game
// player for pad 2's combat buttons that steps one feature through with timeouts and asserts on the
// game's own state, then prints "RESULT: PASS <test>" or "RESULT: FAIL <test>: why" and ends the
// run. scenarios/combat/run.ps1 runs each test and its mutation twin (DC_COOP_MUTATE, which must
// make it fail). The bot reads only game state and holds only pad 2's buttons, so it is the same in
// every copy; nothing here runs unless DC_COOP_TEST is set.

#include "coop/game_ns.hpp"
#include "coop/coop.hpp"

#include <libvu0.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "btactstatus.hpp"
#include "character.hpp"
#include "dun/gameloop.hpp"
#include "framevu1.hpp"
#include "app/gameloop.hpp"
#include "itemdata.hpp"
#include "mathutil.hpp"
#include "monstorunit.hpp"
#include "userstatus.hpp"
#include "hitmark.hpp"
#include "dungeonparts.hpp"
#include "dungeonmap.hpp"
#include "runscript_opcodes.hpp"
#include "btsysscript.hpp"
#include "runscript.hpp"
#include "collisiondata.hpp"
#include "collision.hpp"
#include "dngstatusdata.hpp"
#include "menu_inventory.hpp"

#include "coop/coop_combat.hpp"
#include "coop/coop_core.hpp"
#include "coop/coop_actor.hpp" // coop-actorswap
#include "coop/coop_party.hpp"
#include "coop/coop_wheel.hpp"
#include "coop/netplay.hpp"

namespace dc::game {
extern CFrameVu1 *CharaFrame;
extern s32        gameTask;
} // namespace dc::game

namespace coop {

namespace {

// The whole run's limit, in ticks of play.
constexpr int kRunTicks = 9000;

struct Bot {
    const char   *test = nullptr;
    bool          mode = false;
    bool          playing = false; // the floor's first tick of play seen (Started)
    bool          done = false;
    int           stop_in = 0;
    int           ticks = 0;
    int           step = 0;
    int           t = 0;
    std::uint16_t buttons = 0;
    bool          steer = false;
    std::uint8_t  lx = kInputAxisCentre;
    std::uint8_t  ly = kInputAxisCentre;

    // Per test.
    int   chara = -1;
    int   expect = 0;
    int   count0 = 0;
    int   value0 = 0;
    int   target = -1;
    int   first = -1;
    int   candidates = 0;
    int   tries = 0;
    float yaw0 = 0.0f;
    float swing = 0.0f;
    float p1[4] = {};
    float p2[4] = {};
    float p1_last[4] = {};
    int   moving = 0;
    int   seen = 0;
    int   last = -1;

    // Pad 2 as the script holds it (the wheel reads it, not the bot), and what the wheel test saw.
    std::uint16_t live = 0;
    std::uint16_t live_was = 0;
    bool          opened_charging = false;
};

Bot g_bot;

void Next() {
    g_bot.step++;
    g_bot.t = 0;
    g_bot.buttons = 0;
    g_bot.steer = false;
}

void Finish(bool pass, const char *why) {
    std::printf("RESULT: %s %s%s%s\n", pass ? "PASS" : "FAIL", g_bot.test, pass ? "" : ": ", pass ? "" : why);
    std::fflush(stdout);
    g_bot.done = true;
    g_bot.stop_in = 30;
    g_bot.buttons = 0;
    g_bot.steer = false;
}

void Fail(const char *why) {
    Finish(false, why);
}

// Holds mask on ticks t0 to t0+3 of the step, so it is one press.
void TapAt(int t0, std::uint16_t mask) {
    if (g_bot.t >= t0 && g_bot.t < t0 + 4) {
        g_bot.buttons |= mask;
    } else if (g_bot.t >= t0 + 4 && g_bot.t < t0 + 6) {
        g_bot.buttons &= ~mask;
    }
}

bool Idle() {
    return !g_partner.attacking && !g_combat.charging && g_partner.down == 0 && !g_combat.panel;
}

// The blow a member makes before any charge, worked out here from the save and the game's tables on
// their own (not through the combat code): its equipped weapon, or its starting weapon's table entry,
// Ruby and Osmond half attack and half magic, doubled by its own stamina.
int ExpectedAttack(int chara) {
    static const int kStarter[6] = {ITEM_WEAPON_DAGGER,    ITEM_WEAPON_WOODEN_SLINGSHOT, ITEM_WEAPON_MALLET,
                                    ITEM_WEAPON_GOLD_RING, ITEM_WEAPON_FIGHTING_STICK,   ITEM_WEAPON_MACHINE_GUN};
    int slot = UserStatus->equipped_weapon_slot[chara];
    int attack = 0;
    int magic = 0;

    if (slot >= 0 && slot < 11 && UserStatus->chara_weapons[chara][slot].item_no > 0) {
        attack = UserStatus->chara_weapons[chara][slot].attack;
        magic = UserStatus->chara_weapons[chara][slot].magic;
    } else if (COM_ITEM_INFO *info = GetCommonItemInfo(kStarter[chara])) {
        if (WEAPON_DATA *data = GetWeaponDataInfo((s32) info->index)) {
            attack = data->attack;
            magic = data->magic;
        }
    }

    int damage = chara == CHARA_RUBY || chara == CHARA_OSMOND ? (attack >> 1) + (magic >> 1) : attack;

    damage = std::max(damage, 1);
    return (UserStatus->ailments[chara] & AILMENT_STAMINA) != 0 ? damage * 2 : damage;
}

bool Candidate(int i, float range_scale) {
    const MONSTOR &monster = NowMonstorUnit->monster[i];
    float          position[4];

    if (monster.state != 2 || monster.hp <= 0 || monster.revealed == 0 || monster.lockon_enabled == 0 ||
        monster.kind == MONSTER_KIND_NO_LOCK_ON) {
        return false;
    }

    NowMonstorUnit->chara[i][0].GetPosition(position);
    return DistVector(position, g_partner.pos) < monster.lock_range * range_scale && std::fabs(position[1] - g_partner.pos[1]) < 60.0f;
}

int CountCandidates() {
    int count = 0;

    for (int i = 0; i < 16; i++) {
        count += Candidate(i, 0.9f) ? 1 : 0;
    }

    return count;
}

float Wrap(float angle) {
    while (angle > PI) {
        angle -= 2.0f * PI;
    }

    while (angle < -PI) {
        angle += 2.0f * PI;
    }

    return angle;
}

float Yaw() {
    sceVu0FVECTOR rotation;

    g_partner.chara->frame->GetRotation(rotation);
    return rotation[1];
}

// --- charge: a level-one release lands base x1.5; a level-two release lands base x1.5 and a wave
// of base x1.5.
void Charge() {
    int chara = g_partner.loaded_chara;

    switch (g_bot.step) {
        case 0:
            if (g_bot.t >= 60 && Idle()) {
                g_bot.chara = chara;
                g_bot.expect = (int) ((float) ExpectedAttack(chara) * 1.5f);
                std::printf("coop-test: charge chara %d base %d, expect %d at both levels\n", chara, ExpectedAttack(chara), g_bot.expect);
                Next();
            }
            break;
        case 1:
            g_bot.buttons = kInputCross;

            if (g_combat.charging && g_combat.charge_level >= 1) {
                g_bot.buttons = 0;
                g_bot.count0 = g_combat.blows;
                Next();
            } else if (g_bot.t > 400) {
                Fail("the charge never reached level 1");
            }
            break;
        case 2:
            if (g_combat.blows > g_bot.count0) {
                std::printf("coop-test: level-1 release %d damage (expect %d)\n", g_combat.last_blow_damage, g_bot.expect);

                if (g_combat.last_blow_damage != g_bot.expect) {
                    Fail("level-1 release damage is not base x1.5");
                    return;
                }

                Next();
            } else if (g_bot.t > 240) {
                Fail("no blow after the level-1 release");
            }
            break;
        case 3:
            if (g_bot.t >= 30 && Idle()) {
                // Setup: the member's event skill, which the second level needs.
                UserStatus->skill_owned[chara] = 1;
                g_bot.value0 = g_combat.waves;
                Next();
            }
            break;
        case 4:
            g_bot.buttons = kInputCross;

            if (g_combat.charging && g_combat.charge_level >= 2) {
                g_bot.buttons = 0;
                g_bot.count0 = g_combat.blows;
                Next();
            } else if (g_bot.t > 600) {
                Fail("the charge never reached level 2");
            }
            break;
        case 5:
            if (g_combat.blows > g_bot.count0 && g_combat.waves > g_bot.value0) {
                std::printf("coop-test: level-2 release %d damage, wave %d (expect %d each)\n", g_combat.last_blow_damage,
                            g_combat.last_wave_damage, g_bot.expect);

                if (g_combat.last_blow_damage != g_bot.expect || g_combat.last_wave_damage != g_bot.expect) {
                    Fail("level-2 release or wave damage is not base x1.5");
                    return;
                }

                Finish(true, "");
            } else if (g_bot.t > 240) {
                Fail("no blow and wave after the level-2 release");
            }
            break;
    }
}

// --- lock: Circle's tap locks a monster in range, a second tap moves to another when there is one,
// the partner faces the locked monster, and holding Circle lets go.
void Lock() {
    switch (g_bot.step) {
        case 0:
            g_bot.candidates = CountCandidates();

            // Waits for a monster in range, and up to 20 s more for a second, so "next" can be checked.
            if (g_bot.candidates >= 2 || (g_bot.candidates == 1 && g_bot.value0++ > 1200)) {
                std::printf("coop-test: %d monsters in lock range\n", g_bot.candidates);
                Next();
            } else if (g_bot.t > 6000) {
                Fail("no monster came into lock range");
            }
            break;
        case 1:
            TapAt(0, kInputCircle);

            if (g_bot.t == 14) {
                if (g_combat.lock < 0 || !Candidate(g_combat.lock, 1.0f)) {
                    Fail("Circle's tap did not lock a monster in range");
                    return;
                }

                g_bot.first = g_combat.lock;
                std::printf("coop-test: locked monster %d\n", g_bot.first);
                Next();
            }
            break;
        case 2:
            TapAt(0, kInputCircle);

            if (g_bot.t == 14) {
                if (g_combat.lock < 0) {
                    Fail("the second tap dropped the lock");
                    return;
                }

                if (CountCandidates() >= 2 && g_combat.lock == g_bot.first) {
                    Fail("the second tap did not move the lock to another monster");
                    return;
                }

                std::printf("coop-test: second tap: monster %d (%s)\n", g_combat.lock,
                            g_combat.lock == g_bot.first ? "only one in range" : "moved on");
                Next();
            }
            break;
        case 3:
            if (g_bot.t == 40) {
                float position[4];

                if (g_combat.lock < 0) {
                    Fail("the lock was lost before the facing check");
                    return;
                }

                NowMonstorUnit->chara[g_combat.lock][0].GetPosition(position);

                float want = atan2f(position[0] - g_partner.pos[0], position[2] - g_partner.pos[2]);
                float error = std::fabs(Wrap(want - Yaw()));

                std::printf("coop-test: facing error %.2f rad\n", error);

                if (error > 0.35f) {
                    Fail("the partner does not face the locked monster");
                    return;
                }

                Next();
            }
            break;
        case 4:
            g_bot.buttons = g_bot.t < 40 ? kInputCircle : 0;

            if (g_bot.t == 46) {
                if (g_combat.lock >= 0) {
                    Fail("holding Circle did not let go of the lock");
                    return;
                }

                Finish(true, "");
            }
            break;
    }
}

// --- aim: under R2 the left stick turns and tilts the aim (the bot turns well away first, so the
// stick must do the work) and a shot fired along it strikes the monster.
void Aim() {
    float at[4];

    if (g_bot.step >= 1 && (g_bot.target < 0 || NowMonstorUnit->monster[g_bot.target].hp <= 0 ||
                            NowMonstorUnit->monster[g_bot.target].state != 2)) {
        Fail("the target monster went away");
        return;
    }

    if (g_bot.target >= 0) {
        NowMonstorUnit->chara[g_bot.target][0].GetPosition(at);
        at[1] += 8.0f;
    }

    float dx = g_bot.target >= 0 ? at[0] - g_partner.pos[0] : 0.0f;
    float dz = g_bot.target >= 0 ? at[2] - g_partner.pos[2] : 0.0f;
    float want_yaw = atan2f(dx, dz);
    float want_pitch = g_bot.target >= 0 ? atan2f(at[1] - (g_partner.pos[1] + 10.0f), sqrtf(dx * dx + dz * dz)) : 0.0f;

    switch (g_bot.step) {
        case 0: {
            // Close enough that the trees between are unlikely; the awake monster walks up to the pair.
            float best = 80.0f;

            for (int i = 0; i < 16; i++) {
                float position[4];

                if (NowMonstorUnit->monster[i].state != 2 || NowMonstorUnit->monster[i].hp <= 0) {
                    continue;
                }

                NowMonstorUnit->chara[i][0].GetPosition(position);

                if (DistVector(position, g_partner.pos) < best) {
                    best = DistVector(position, g_partner.pos);
                    g_bot.target = i;
                }
            }

            if (g_bot.target >= 0 && Idle()) {
                std::printf("coop-test: aiming at monster %d, %.0f away\n", g_bot.target, best);
                Next();
            } else if (g_bot.t > 6000) {
                Fail("no awake monster came within 80");
            }
            break;
        }
        case 1:
            g_bot.buttons = kInputR2;

            if (g_bot.t == 3) {
                g_bot.yaw0 = g_combat.aim_yaw;
            }

            // Turns well away from the monster first.
            if (g_bot.t > 3 && g_bot.t < 40 && std::fabs(Wrap(want_yaw - g_combat.aim_yaw)) < 0.8f) {
                g_bot.steer = true;
                g_bot.lx = 255;
                g_bot.ly = kInputAxisCentre;
            } else if (g_bot.t >= 40) {
                Next();
                g_bot.buttons = kInputR2;
            }
            break;
        case 2: {
            float yaw_error = Wrap(want_yaw - g_combat.aim_yaw);
            float pitch_error = want_pitch - g_combat.aim_pitch;

            g_bot.buttons = kInputR2;
            g_bot.steer = true;
            g_bot.swing = std::max(g_bot.swing, std::fabs(Wrap(g_combat.aim_yaw - g_bot.yaw0)));
            g_bot.lx = std::fabs(yaw_error) <= 0.05f ? kInputAxisCentre : yaw_error > 0.0f ? 255 : 0;
            g_bot.ly = std::fabs(pitch_error) <= 0.03f ? kInputAxisCentre : pitch_error > 0.0f ? 0 : 255;

            if (std::fabs(yaw_error) <= 0.05f && std::fabs(pitch_error) <= 0.03f && g_bot.t > 5) {
                g_bot.count0 = g_combat.shots_hit;
                Next();
                g_bot.buttons = kInputR2;
            } else if (g_bot.t > 600) {
                Fail("the aim never came round to the monster");
            }
            break;
        }
        case 3:
            g_bot.buttons = g_bot.t < 4 ? (kInputR2 | kInputCross) : kInputR2;

            if (g_combat.shots_hit > g_bot.count0) {
                std::printf("coop-test: aimed shot hit after the aim turned %.2f rad\n", g_bot.swing);

                if (g_bot.swing < 0.3f) {
                    Fail("the aim hit without the stick turning it");
                    return;
                }

                Finish(true, "");
            } else if (g_bot.t > 120) {
                if (++g_bot.tries >= 6) {
                    Fail("six aimed shots and none hit the monster");
                    return;
                }

                g_bot.step = 2;
                g_bot.t = 0;
            }
            break;
    }
}

// --- item: Right picks bread from the pack and Square heals the partner by its volume, one fewer
// in the pack; then a bomb is thrown, lands and leaves the pack.
void Item() {
    switch (g_bot.step) {
        case 0:
            if (g_bot.t >= 60 && Idle()) {
                if (ItemCount(ITEM_BREAD) <= 0 || ItemCount(ITEM_BOMB) <= 0) {
                    Fail("setup: the pack holds no bread or no bomb (--give 148,159)");
                    return;
                }

                // Setup: the partner hurt, so bread has something to heal.
                SetLife(std::max(1, MaxLife() - 30));
                Next();
            }
            break;
        case 1:
        case 4: {
            int want = g_bot.step == 1 ? ITEM_BREAD : ITEM_BOMB;

            if (g_combat.item == want && (g_bot.buttons & kInputRight) == 0) {
                g_bot.value0 = Life();
                g_bot.count0 = ItemCount(want);
                g_bot.tries = g_combat.throws_landed;
                Next();
            } else {
                TapAt((g_bot.t / 14) * 14, kInputRight);

                if (g_bot.t > 14 * 10) {
                    Fail("Right never picked the item");
                }
            }
            break;
        }
        case 2:
            TapAt(0, kInputSquare);

            // coop-actorswap: retail's use moves the life gauge over its steps (usedActiveItem): read it once settled.
            if (g_bot.t == (ActorPorted("items") ? 120 : 20)) {
                int heal = ITEM_LIST[ITEM_BREAD - ITEM_ATTACH_START].vol;
                int expect = std::min(MaxLife(), g_bot.value0 + heal);

                std::printf("coop-test: bread life %d -> %d (expect %d), %d -> %d in the pack\n", g_bot.value0, Life(), expect,
                            g_bot.count0, ItemCount(ITEM_BREAD));

                if (Life() != expect || ItemCount(ITEM_BREAD) != g_bot.count0 - 1) {
                    Fail("bread did not heal the partner by its volume and leave the pack");
                    return;
                }

                Next();
            }
            break;
        case 3:
            if (g_bot.t >= 40) {
                Next();
            }
            break;
        case 5:
            TapAt(0, kInputSquare);

            // coop-actorswap: retail's thrown item (ActorThrowItem) lands in retail's item pool, 45 ticks on: the bomb left the pack.
            if (ActorPorted("throw") ? g_bot.t > 60 : g_combat.throws_landed > g_bot.tries && g_bot.t > 6) {
                std::printf("coop-test: bomb landed, %d -> %d in the pack\n", g_bot.count0, ItemCount(ITEM_BOMB));

                if (ItemCount(ITEM_BOMB) != g_bot.count0 - 1) {
                    Fail("the thrown bomb did not leave the pack");
                    return;
                }

                Finish(true, "");
            } else if (g_bot.t > 150) {
                Fail("the bomb was not thrown or never landed");
            }
            break;
    }
}

// --- weapon: Down equips the next weapon (the slot, its damage and the hand model all follow), Up
// the one before.
void Weapon() {
    int chara = g_partner.loaded_chara;
    int slots[11];
    int count = WeaponSlots(slots, 11);

    switch (g_bot.step) {
        case 0:
            if (g_bot.t >= 60 && Idle()) {
                if (count < 2) {
                    Fail("setup: the partner has fewer than two weapons (--give 300,301)");
                    return;
                }

                Next();
            }
            break;
        case 1:
        case 2: {
            if (g_bot.t == 1) {
                g_bot.value0 = UserStatus->equipped_weapon_slot[chara];
                g_bot.count0 = g_combat.reloads;
            }

            std::uint16_t button = g_bot.step == 1 ? kInputDown : kInputUp;

            TapAt(2, button);

            if (g_bot.t == 24) {
                // From no weapon (a stand-in's slot -1) Down takes the first and Up the last.
                int at = g_bot.step == 1 ? count - 1 : 0;

                for (int i = 0; i < count; i++) {
                    if (slots[i] == g_bot.value0) {
                        at = i;
                    }
                }

                int want = slots[(at + (g_bot.step == 1 ? 1 : count - 1)) % count];
                int slot = UserStatus->equipped_weapon_slot[chara];

                std::printf("coop-test: %s: slot %d -> %d (expect %d), damage %d (expect %d), hand reloads %d -> %d, model %d\n",
                            g_bot.step == 1 ? "Down" : "Up", g_bot.value0, slot, want, Damage(), ExpectedAttack(chara), g_bot.count0,
                            g_combat.reloads, (int) g_partner.weapon_loaded);

                if (slot != want) {
                    Fail("the D-pad did not equip the next weapon");
                    return;
                }

                if (Damage() != ExpectedAttack(chara)) {
                    Fail("the damage does not follow the new weapon");
                    return;
                }

                if (g_combat.reloads != g_bot.count0 + 1 || !g_partner.weapon_loaded) {
                    Fail("the hand model was not reloaded");
                    return;
                }

                if (g_bot.step == 2) {
                    Finish(true, "");
                } else {
                    Next();
                }
            }
            break;
        }
    }
}

// coop: gaps2
// --- weaponzone: in a floor's no-weapon-change zone (res_limit_zone_current 10, set here as a zone
// script sets it) pad 2's D-pad does not change the partner's weapon, as the player's weapon menu
// refuses; once the zone is gone it does.
void WeaponZone() {
    int chara = g_partner.loaded_chara;
    int slots[11];
    int count = WeaponSlots(slots, 11);

    switch (g_bot.step) {
        case 0:
            if (g_bot.t >= 60 && Idle()) {
                if (count < 2) {
                    Fail("setup: the partner has fewer than two weapons (--give 300,301)");
                    return;
                }

                UserStatus->res_limit_zone_current = RES_LIMIT_ZONE_NO_WEAPON_CHANGE;
                std::printf("coop-test: weaponzone: in the no-weapon-change zone, slot %d\n", UserStatus->equipped_weapon_slot[chara]);
                Next();
            }
            break;
        case 1:
        case 2:
            if (g_bot.t == 1) {
                g_bot.value0 = UserStatus->equipped_weapon_slot[chara];
            }

            TapAt(2, kInputDown);

            if (g_bot.t == 24) {
                int slot = UserStatus->equipped_weapon_slot[chara];

                std::printf("coop-test: weaponzone: %s: slot %d -> %d\n", g_bot.step == 1 ? "in the zone" : "out of it", g_bot.value0, slot);

                if (g_bot.step == 1 && slot != g_bot.value0) {
                    Fail("the weapon changed in the no-weapon-change zone");
                    return;
                }

                if (g_bot.step == 2 && slot == g_bot.value0) {
                    Fail("the weapon did not change once the zone was gone");
                    return;
                }

                if (g_bot.step == 1) {
                    UserStatus->res_limit_zone_current = RES_LIMIT_ZONE_NONE;
                    Next();
                } else {
                    Finish(true, "");
                }
            }
            break;
    }
}

// coop: gaps2
// --- fightzone: on a floor only one member may fight (res_limit_zone_current 0 to 5; set here to the
// player's member, as a zone script sets it), pad 2's Cross does not swing; once the zone is gone it does.
void FightZone() {
    switch (g_bot.step) {
        case 0:
            if (g_bot.t >= 60 && Idle()) {
                UserStatus->res_limit_zone_current = UserStatus->cur_chara;
                std::printf("coop-test: fightzone: only member %d may fight; P2 is %d, swings %d\n", (int) UserStatus->cur_chara,
                            g_partner.loaded_chara, g_partner.swings);
                Next();
            }
            break;
        case 1:
        case 2:
            if (g_bot.t == 1) {
                g_bot.count0 = g_partner.swings;
            }

            TapAt(2, kInputCross);

            if (g_bot.t == 60) {
                std::printf("coop-test: fightzone: %s: swings %d -> %d\n", g_bot.step == 1 ? "in the zone" : "out of it", g_bot.count0,
                            g_partner.swings);

                if (g_bot.step == 1 && g_partner.swings != g_bot.count0) {
                    Fail("P2 swung on a floor only another member may fight");
                    return;
                }

                if (g_bot.step == 2 && g_partner.swings == g_bot.count0) {
                    Fail("P2 did not swing once the zone was gone");
                    return;
                }

                if (g_bot.step == 1) {
                    UserStatus->res_limit_zone_current = RES_LIMIT_ZONE_NONE;
                    Next();
                } else {
                    Finish(true, "");
                }
            }
            break;
    }
}

// --- panel: Triangle opens the partner's panel; while it is open the game keeps going (the scripted
// player walks on, the task stays play) and the partner stands; its pages turn; Triangle closes it.
void Panel() {
    switch (g_bot.step) {
        case 0:
            if (g_bot.t >= 60 && Idle() && g_bot.moving >= 10) {
                Next();
            } else if (g_bot.t > 4000) {
                Fail("setup: the scripted player never walked");
            }
            break;
        case 1:
            TapAt(0, kInputTriangle);

            if (g_bot.t == 8) {
                if (!g_combat.panel) {
                    Fail("Triangle did not open the panel");
                    return;
                }

                sceVu0CopyVector(g_bot.p1, CharaFrame->position);
                sceVu0CopyVector(g_bot.p2, g_partner.pos);
                Next();
            }
            break;
        case 2:
            TapAt(20, kInputRight);
            TapAt(60, kInputDown);
            TapAt(80, kInputLeft);

            if (g_bot.t == 35 && g_combat.page != 1) {
                Fail("Right did not turn the panel's page");
                return;
            }

            if (g_bot.t == 120) {
                float p1_moved = DistVector(g_bot.p1, CharaFrame->position);
                float p2_moved = DistVector(g_bot.p2, g_partner.pos);

                std::printf("coop-test: panel open 120 ticks: P1 moved %.1f, P2 moved %.1f, task %d, page %d\n", p1_moved, p2_moved,
                            (int) gameTask, g_combat.page);

                if (gameTask != GAME_TASK_PLAY || p1_moved < 8.0f) {
                    Fail("the game did not keep going while the panel was open");
                    return;
                }

                if (p2_moved > 2.0f) {
                    Fail("the partner moved while its panel was open");
                    return;
                }

                Next();
            }
            break;
        case 3:
            TapAt(0, kInputTriangle);

            if (g_bot.t == 8) {
                if (g_combat.panel) {
                    Fail("Triangle did not close the panel");
                    return;
                }

                Finish(true, "");
            }
            break;
    }
}

// --- wheel: the scripted R3 does not open the wheel mid-charge; opened mid-aim it drops the aim, the
// partner stands and neither blows nor shots go off while it is open (the bot keeps pressing Cross),
// and the Cross held when the scripted Circle closes it does not fire. The script presses R3 at frames
// 1000 and 1400 and Circle at 1600 (wheel.txt); the bot follows them by what the game shows.
void Wheel() {
    bool r3 = (g_bot.live & kInputR3) != 0 && (g_bot.live_was & kInputR3) == 0;

    switch (g_bot.step) {
        case 0:
            if (g_bot.t >= 30 && Idle()) {
                Next();
            }
            break;
        case 1:
            // Charges, through the script's first R3.
            g_bot.buttons = kInputCross;

            if (g_combat.charging && WheelOpen()) {
                g_bot.opened_charging = true;
            }

            if (r3) {
                if (!g_combat.charging) {
                    Fail("setup: the bot was not charging at the first R3");
                    return;
                }

                g_bot.value0 = 1;
            }

            if (g_bot.value0 != 0 && ++g_bot.count0 > 30) {
                if (g_bot.opened_charging || WheelOpen()) {
                    Fail("the wheel opened mid-charge");
                    return;
                }

                std::printf("coop-test: R3 mid-charge left the wheel shut\n");
                Next();
            } else if (g_bot.t > 1500) {
                Fail("the script's first R3 never came");
            }
            break;
        case 2:
            if (g_bot.t >= 20 && Idle()) {
                Next();
            } else if (g_bot.t > 400) {
                Fail("the charged release never ended");
            }
            break;
        case 3:
            // Aims, turning slowly, until the script's second R3 opens the wheel.
            g_bot.buttons = kInputR2;
            g_bot.steer = true;
            g_bot.lx = 200;
            g_bot.ly = kInputAxisCentre;

            if (WheelOpen()) {
                g_bot.count0 = g_combat.blows;
                g_bot.value0 = g_combat.shots_hit;
                g_bot.tries = 0;
                sceVu0CopyVector(g_bot.p2, g_partner.pos);
                Next();
            } else if (g_bot.t > 1500) {
                Fail("the script's second R3 never opened the wheel");
            }
            break;
        case 4:
            // The wheel is open: the bot holds R2 and presses Cross over and over, then holds Cross down
            // through the close (as a Cross that picks a member would be).
            g_bot.buttons = g_bot.t >= 20 || (g_bot.t / 6) % 2 == 0 ? (kInputR2 | kInputCross) : kInputR2;

            if (WheelOpen()) {
                if (g_combat.aiming || g_combat.charging || g_partner.attacking) {
                    Fail("the open wheel left the aim, a charge or a swing going");
                    return;
                }

                if (g_combat.blows != g_bot.count0) {
                    Fail("a blow or shot went off while the wheel was open");
                    return;
                }

                g_bot.tries++;
            } else {
                // Closed by the script's Circle; Cross is held through it.
                g_bot.buttons = kInputCross;
                std::printf("coop-test: wheel open %d ticks, P2 moved %.1f\n", g_bot.tries, DistVector(g_bot.p2, g_partner.pos));

                if (g_bot.tries < 30 || DistVector(g_bot.p2, g_partner.pos) > 2.0f) {
                    Fail("the wheel was open too briefly, or the partner moved while it was open");
                    return;
                }

                Next();
                g_bot.buttons = kInputCross;
            }

            if (g_bot.t > 1200) {
                Fail("the script's Circle never closed the wheel");
            }
            break;
        case 5:
            g_bot.buttons = kInputCross;

            if (g_partner.attacking || g_combat.blows != g_bot.count0) {
                Fail("the Cross held through the wheel fired when it closed");
                return;
            }

            if (g_bot.t >= 60) {
                Finish(true, "");
            }
            break;
    }
}

// --- reset: mid-fight, with a charge under way and every other piece of combat state dirtied (the
// rest written directly), CombatResetForSession leaves it all as a fresh boot has it, and P2 fights on.
void Reset() {
    char why[160];

    switch (g_bot.step) {
        case 0:
            if (g_bot.t >= 60 && Idle()) {
                Next();
            }
            break;
        case 1:
            g_bot.buttons = kInputCross;

            if (g_combat.charging) {
                // Setup: everything else dirty, as a long fight would leave it.
                g_combat.lock = 0;
                g_combat.circle_ticks = 5;
                g_combat.aiming = true;
                g_combat.aim_yaw = 1.0f;
                g_combat.aim_pitch = 0.2f;
                g_combat.item = ITEM_BREAD;
                g_combat.item_wait = 9;
                g_combat.panel = true;
                g_combat.page = 1;
                g_combat.row = 2;
                g_combat.shot_scale[0] = 1.5f;
                g_combat.throws[0].life = 3;
                g_combat.wheel = true;
                g_combat.blows = g_combat.blows + 1;
                g_combat.reloads = 4;

                if (CombatIsFresh(why, sizeof why)) {
                    Fail("setup: the state did not get dirty");
                    return;
                }

                CombatResetForSession();

                if (!CombatIsFresh(why, sizeof why)) {
                    std::printf("coop-test: after the reset: %s\n", why);
                    Fail("the reset left state a fresh boot does not have");
                    return;
                }

                std::printf("coop-test: reset mid-charge left combat as a fresh boot has it\n");
                Next();
            } else if (g_bot.t > 400) {
                Fail("setup: the charge never started");
            }
            break;
        case 2:
            if (g_bot.t >= 30 && Idle()) {
                g_bot.count0 = g_combat.blows;
                Next();
            } else if (g_bot.t > 600) {
                Fail("the partner never came back to rest after the reset");
            }
            break;
        case 3:
            TapAt(0, kInputCross);

            if (g_combat.blows > g_bot.count0) {
                Finish(true, "");
            } else if (g_bot.t > 240) {
                Fail("the partner could not swing after the reset");
            }
            break;
    }
}

// --- ring: Ruby's shot is her ring magic, from the partner's own effect for her weapon's element, not
// the plain shot (basefx01) nor the controlled character's effect.
void Ring() {
    int active = 0;
    int element = -1;
    int shots = RingShots(&active, &element);

    switch (g_bot.step) {
        case 0:
            if (g_partner.loaded_chara != CHARA_RUBY) {
                if (g_bot.t > 600) {
                    Fail("setup: P2 is not Ruby (DC_COOP_PARTY=6 DC_COOP_PARTNER=3)");
                }

                return;
            }

            if (g_bot.t >= 60 && Idle()) {
                g_bot.count0 = shots;
                g_bot.value0 = 0;
                Next();
            }
            break;
        case 1: {
            TapAt(0, kInputCross);

            const WEAPON_HAVE *weapon = EquippedWeapon(CHARA_RUBY);
            int                want = weapon != nullptr && weapon->best_elem >= 0 && weapon->best_elem <= 4 ? weapon->best_elem : 0;

            for (const Shot &shot : g_partner.shots) {
                if (shot.life > 0) {
                    Fail("Ruby's shot went out as the plain shot");
                    return;
                }
            }

            if (active > 0) {
                g_bot.value0 = std::max(g_bot.value0, active);
            }

            if (shots > g_bot.count0 && g_bot.value0 > 0) {
                std::printf("coop-test: ring shot %d, element %d (weapon's %d), %d in flight\n", shots, element, want, g_bot.value0);

                if (element != want) {
                    Fail("the ring loaded is not her weapon's element");
                    return;
                }

                Finish(true, "");
            } else if (g_bot.t > 240) {
                Fail("no ring magic went out");
            }
            break;
        }
    }
}

// --- flash: Osmond's shot sets off his own gun's muzzle flash (his main effect, loaded into the
// partner's effect, on the gun's dcol0 frame), and his bullet still goes.
void Flash() {
    bool osmond = false;
    int  flashes = MuzzleFlashes(&osmond);

    switch (g_bot.step) {
        case 0:
            if (g_partner.loaded_chara != CHARA_OSMOND) {
                if (g_bot.t > 600) {
                    Fail("setup: P2 is not Osmond (DC_COOP_PARTY=6 DC_COOP_PARTNER=5)");
                }

                return;
            }

            if (g_bot.t >= 60 && Idle()) {
                g_bot.count0 = flashes;
                g_bot.value0 = g_combat.blows;
                Next();
            }
            break;
        case 1:
            TapAt(0, kInputCross);

            if (flashes > g_bot.count0 && g_combat.blows > g_bot.value0) {
                std::printf("coop-test: muzzle flash %d, Osmond's own effect %d, gun model %d\n", flashes, (int) osmond,
                            (int) g_partner.weapon_loaded);

                if (!osmond) {
                    Fail("the flash is not Osmond's gun's effect");
                    return;
                }

                Finish(true, "");
            } else if (g_bot.t > 240) {
                Fail(g_combat.blows > g_bot.value0 ? "Osmond fired without a muzzle flash" : "Osmond never fired");
            }
            break;
    }
}

// --- effectblock: P2's own effect (Osmond's muzzle flash, in the partner's effect block) keeps its
// textures when pad 2 opens its character wheel. When the wheel's faces shared that block, opening the
// wheel freed the effect's textures under it while the effect stayed loaded, its texture copies went
// on into handles the next textures took, and a floor's waterfall showed P2's faces, boots and cloth
// (Jep, 2026-10-20).
std::vector<u_long> EffectTextures() {
    std::vector<u_long> keys;

    for (const CTexture &texture : TexManager.textures) {
        if (texture.name[0] != 0 && texture.block == PartnerEffectBlock()) {
            keys.push_back(texture.tex0);
        }
    }

    return keys;
}

std::vector<u_long> g_effect_textures;

void EffectBlock() {
    bool osmond = false;
    int  flashes = MuzzleFlashes(&osmond);

    switch (g_bot.step) {
        case 0:
            if (g_partner.loaded_chara != CHARA_OSMOND) {
                if (g_bot.t > 600) {
                    Fail("setup: P2 is not Osmond (DC_COOP_PARTY=6 DC_COOP_PARTNER=5)");
                }

                return;
            }

            if (g_bot.t >= 60 && Idle()) {
                g_bot.count0 = flashes;
                Next();
            }
            break;
        case 1:
            TapAt(0, kInputCross);

            if (flashes > g_bot.count0 && osmond) {
                // coop-osmondflash: the reference is what the effect itself loaded (PartnerEffectLoaded), not the block as it
                // stands after the shot: since the HUD's faces (coop-hudicon) a face block shared with it is filled again at
                // once, so the block before and with the wheel open were both the faces, and the twin passed.
                g_effect_textures = PartnerEffectLoaded();
                std::sort(g_effect_textures.begin(), g_effect_textures.end());

                if (g_effect_textures.empty()) {
                    Fail("setup: Osmond's effect has no texture in its block");
                    return;
                }

                Next();
            } else if (g_bot.t > 240) {
                Fail("setup: Osmond never fired his own muzzle flash");
            }
            break;
        case 2:
            if (g_bot.t >= 30 && Idle()) {
                Next();
            }
            break;
        case 3:
            // The script's pad 2 R3 opens the wheel.
            if (WheelOpen()) {
                std::vector<u_long> now = EffectTextures();

                std::sort(now.begin(), now.end());

                std::printf("coop-test: effect textures %zu before the wheel, %zu with it open, same %d\n", g_effect_textures.size(), now.size(),
                            (int) (now == g_effect_textures));

                if (now != g_effect_textures) {
                    Fail("opening the wheel freed P2's effect textures");
                    return;
                }

                Finish(true, "");
            } else if (g_bot.t > 1200) {
                Fail("setup: the script's R3 never opened the wheel");
            }
            break;
    }
}


// --- weaponpanel (linked): on the copy whose player is P2 the bottom-left panel is P2's (its weapon,
// its gauge, dropping while it charges and full again after); on player 1's copy it is the
// player's. Both copies hold the same pad 2 buttons, so they stay in step. A stand-in with no weapon
// equipped (a party of one) shows its character's starting weapon.
void WeaponPanel() {
    int                local = NetplayLocalPlayer();
    WEAPON_HAVE        starter{};
    const WEAPON_HAVE *weapon = EquippedWeapon(g_partner.loaded_chara);

    if (weapon == nullptr && StarterWeapon(g_partner.loaded_chara) > 0) {
        WepDataListToHaveCopy(StarterWeapon(g_partner.loaded_chara), &starter);
        weapon = &starter;
    }

    int                want_item = weapon != nullptr ? weapon->item_no : -1;
    const auto        &shown = g_weapon_panel_shown;

    switch (g_bot.step) {
        case 0:
            if (local == 0) {
                Fail("setup: this copy is not linked (game.netplay)");
                return;
            }

            // Game state only, so both copies start the charge on the same tick.
            if (g_bot.t >= 60 && Idle()) {
                Next();
            }
            break;
        case 1:
            g_bot.buttons = kInputCross;

            if (g_combat.charging && g_combat.charge_ticks >= 20) {
                std::printf("coop-test: copy of player %d, charging: panel drawn %d, item %d (P2's %d), gauge %d (P2's %d), icon %d\n", local,
                            (int) shown.drawn, shown.item, want_item, shown.gauge, PartnerGauge(), (int) shown.icon);

                if (local == 2 && (!shown.drawn || shown.item != want_item || shown.gauge >= 100 || shown.gauge <= 0)) {
                    Fail("player 2's copy does not show P2's weapon and charging gauge");
                    return;
                }

                if (local == 1 && shown.drawn) {
                    Fail("player 1's copy shows P2's panel");
                    return;
                }

                // The picture, and the top-left status: P2's life and weapon line on its copy, the
                // player's on the other.
                const auto &top = g_top_status_shown;

                std::printf("coop-test: copy of player %d: icon item %d, top-left drawn %d chara %d life %d/%d (P2's %d/%d) WHP %d/%d (P2's %d)\n",
                            local, shown.icon_item, (int) top.drawn, top.chara, top.life, top.life_max, Life(), MaxLife(), top.whp,
                            top.whp_max, weapon != nullptr ? (int) weapon->durability : 0);

                if (local == 2 && (shown.icon_item != want_item || !shown.icon)) {
                    Fail("player 2's copy does not show P2's weapon picture");
                    return;
                }

                if (local == 2 && (!top.drawn || top.chara != g_partner.loaded_chara || top.life != Life() || top.life_max != MaxLife() ||
                                   weapon == nullptr || top.whp_max != weapon->durability || top.whp != (int) weapon->durability_f ||
                                   std::fabs(top.water - UserStatus->water_now[g_partner.loaded_chara]) > 0.05f)) {
                    Fail("player 2's copy does not show P2's life and weapon line top left");
                    return;
                }

                if (local == 1 && top.drawn) {
                    Fail("player 1's copy shows P2's status top left");
                    return;
                }

                Next();
            } else if (g_bot.t > 400) {
                Fail("setup: the charge never started");
            }
            break;
        case 2:
            // The charge emptied the gauge; it fills back at the weapon's speed (StepGauge), and the
            // panel draws it a frame later.
            g_bot.seen = PartnerGauge() >= 100 ? g_bot.seen + 1 : 0;

            if (g_bot.t >= 30 && Idle() && g_bot.seen >= 3) {
                if (local == 2 && (!shown.drawn || shown.gauge != 100)) {
                    Fail("player 2's copy does not show P2's gauge full again at rest");
                    return;
                }

                std::printf("coop-test: copy of player %d at rest: panel drawn %d, gauge %d\n", local, (int) shown.drawn, shown.gauge);
                Finish(true, "");
            } else if (g_bot.t > 600) {
                Fail("the partner never came back to rest");
            }
            break;
    }
}

// --- swapicon (linked): on the copy whose player is P2 the weapon picture by the gauge is P2's
// member's, copied from the menus' icon sheet (itemlst.img's, which shares the HUD's
// palette; allicon.img's in a block of its own drew Xiao's slingshot in the wrong colours), and after
// pad 2 swaps member (L3) it is the new member's, from the same sheet. Both copies hold the same pad 2
// buttons, so they stay in step; player 1's copy checks only that it never draws P2's panel.
void SwapIcon() {
    constexpr int kHudSheet = 1; // itemlst.img's (g_weapon_panel_shown.icon_sheet)
    int           local = NetplayLocalPlayer();
    int           chara = g_partner.loaded_chara;
    int           want = EquippedWeapon(chara) != nullptr ? EquippedWeapon(chara)->item_no : StarterWeapon(chara);
    const auto   &shown = g_weapon_panel_shown;
    bool          right = shown.drawn && shown.icon && shown.icon_item == want && shown.icon_sheet == kHudSheet;

    if (local == 1 && shown.drawn) {
        Fail("player 1's copy shows P2's panel");
        return;
    }

    switch (g_bot.step) {
        case 0:
            if (local == 0) {
                Fail("setup: this copy is not linked (game.netplay)");
                return;
            }

            if (g_bot.t >= 300 && Idle() && g_partner.visible) {
                g_bot.chara = chara;
                Next();
            }
            break;
        case 1:
        case 3:
            // The picture settles within two seconds of the member (re)loading.
            if (g_bot.t == 120) {
                std::printf("coop-test: copy of player %d, %s: member %d, weapon %d, panel drawn %d, icon %d item %d sheet %d\n", local,
                            g_bot.step == 1 ? "before the swap" : "after the swap", chara, want, (int) shown.drawn, (int) shown.icon,
                            shown.icon_item, shown.icon_sheet);

                if (local == 2 && !right) {
                    Fail(g_bot.step == 1 ? "player 2's copy does not show P2's weapon picture from the HUD's sheet"
                                         : "player 2's copy does not show the new member's weapon picture from the HUD's sheet after the swap");
                    return;
                }

                if (g_bot.step == 3) {
                    Finish(true, "");
                    return;
                }

                Next();
            }
            break;
        case 2:
            // The script taps L3 on pad 2 at a fixed frame (the bot's buttons reach combat, not the swap).
            if (chara != g_bot.chara && chara >= 0 && g_partner.visible && Idle()) {
                std::printf("coop-test: pad 2 swapped from member %d to %d\n", g_bot.chara, chara);
                Next();
            } else if (g_bot.t > 3000) {
                Fail("setup: pad 2's swap never loaded another member");
            }
            break;
    }
}

// --- swapspeed (linked): a tap of pad 2's Cross costs P2's "speed" gauge what a tap costs the player's
// for that member, before and after pad 2 swaps member (the script taps L3 on copy B): a melee
// member's swing let go before its end spends nothing (ToanKey_Play), Xiao's and Ruby's shot spends
// the whole gauge (BattleActionOn_Jinn, _Ruby). It used to show a whole gauge spent on every swing
// (Jep's playtest note 31), then Xiao's shot spent none (Jep's 2026-10-27 playtest, coop-xiaostamina).
// Both copies hold the same pad 2 buttons and read only the shared simulation, so they stay in step.
void SwapSpeed() {
    int local = NetplayLocalPlayer();
    int chara = g_partner.loaded_chara;

    switch (g_bot.step) {
        case 0:
            if (local == 0) {
                Fail("setup: this copy is not linked (game.netplay)");
                return;
            }

            // From play on (the bot starts under the floor's arrival scene, which takes no swing).
            if (gameTask != GAME_TASK_PLAY) {
                g_bot.t = 0;
            }

            // Pad 2 walks away from the player first: Cross beside the player asks the swap question
            // instead of swinging.
            g_bot.steer = g_bot.t >= 100 && g_bot.t < 160;
            g_bot.lx = kInputAxisCentre;
            g_bot.ly = 0;

            if (g_bot.t >= 300 && Idle() && g_partner.visible && PartnerGauge() >= 100) {
                g_bot.chara = chara;
                g_bot.value0 = 100;
                Next();
            }
            break;
        case 1:
        case 3:
            // One tap; the lowest gauge from the swing's start until 30 ticks after it ends.
            TapAt(10, kInputCross);

            if (g_bot.t > 10) {
                g_bot.value0 = std::min(g_bot.value0, PartnerGauge());
                g_bot.seen = g_partner.attacking ? 1 : g_bot.seen;
            }

            if (g_bot.seen == 1 && !g_partner.attacking && Idle()) {
                g_bot.tries++;
            }

            if (g_bot.tries >= 30) {
                int cost = 100 - g_bot.value0;

                std::printf("coop-test: copy %d, %s: member %d, a tap's swing cost %d of the gauge (P1's gauge %.0f)\n", local,
                            g_bot.step == 1 ? "before the swap" : "after the swap", chara, cost, BtActStatus.action_gauge);

                // The bot looks a tick after the shot spends it, so a refill tick (1.5 or more) is back by then.
                if ((chara == CHARA_XIAO || chara == CHARA_RUBY) && cost < 95) {
                    Fail("P2's shot does not spend her whole gauge");
                    return;
                }

                if (chara != CHARA_XIAO && chara != CHARA_RUBY && cost >= 50) {
                    Fail("a tap's swing costs P2 half its gauge or more");
                    return;
                }

                if (g_bot.step == 3) {
                    Finish(true, "");
                    return;
                }

                Next();
                g_bot.seen = 0;
                g_bot.tries = 0;
            } else if (g_bot.t > 600) {
                Fail("setup: the tap's swing never ended");
            }
            break;
        case 2:
            // The script taps L3 on pad 2 at a fixed frame.
            if (chara != g_bot.chara && chara >= 0 && g_partner.visible && Idle() && PartnerGauge() >= 100) {
                std::printf("coop-test: pad 2 swapped from member %d to %d\n", g_bot.chara, chara);
                g_bot.value0 = 100;
                Next();
            } else if (g_bot.t > 3000) {
                Fail("setup: pad 2's swap never loaded another member");
            }
            break;
    }
}

// --- p2guard: P2 guards as the player does (R1 held while locked on) and a blow (hit kind 3) that lands
// on the held guard does no harm. With DC_COOP_SEEK=passive P2 walks up to the nearest awake monster;
// the bot taps Circle until it is locked on and holds R1. A monster facing a raised guard waits (as it
// waits on the player's), so the blows are the bot's own: a monster's blow record (Set, players' mask,
// kind 3) on P2's body once the guard has held for 30 ticks, and again with R1 let go and the guard
// down, which must hurt (so the blow is a real one). It fails on harm through the held guard.
bool g_guard_held_hurt = false; // (P2Guard: the held guard's blow hurt, said after the guard-down blow)

void P2Guard() {
    auto blow = []() {
        float at[4] = {g_partner.pos[0], g_partner.pos[1] + 6.0f, g_partner.pos[2], 1.0f};
        return NowColData->Set(at, 10, 4, 4.0f, 0.0f, 1, 3, 0, 0);
    };

    switch (g_bot.step) {
        case 0:
            if (gameTask != GAME_TASK_PLAY) {
                g_bot.t = 0;
            }

            if (g_bot.t >= 60 && g_partner.visible) {
                Next();
            }
            break;
        case 1:
            // Circle tapped every 30 ticks while not locked (a tap locks on as it is let go), R1 held.
            g_bot.buttons = kInputR1 | (g_combat.lock < 0 && g_bot.t % 30 < 4 ? kInputCircle : 0);
            g_bot.seen = g_combat.guard == 9 ? g_bot.seen + 1 : 0;

            if (g_bot.seen >= 30 && g_partner.invincible == 0) {
                g_bot.count0 = Life();
                g_bot.value0 = g_combat.blocks;
                g_guard_held_hurt = false;
                std::printf("coop-test: P2 guard up (lock %d), life %d/%d; a blow on it\n", g_combat.lock, Life(), MaxLife());
                g_bot.target = blow();
                Next();
                g_bot.buttons = kInputR1;
            } else if (g_bot.t > 7500) {
                Fail("setup: P2 never held its guard up for 30 ticks");
            }
            break;
        case 2:
            g_bot.buttons = kInputR1;

            if (g_bot.t == 20) {
                std::printf("coop-test: on the guard: blocks %d, life %d/%d (%d before)\n", g_combat.blocks - g_bot.value0, Life(), MaxLife(),
                            g_bot.count0);

                // (coop-twinfix: the held guard's verdict waits for the guard-down blow, the test's mode evidence that the
                // bot's blow is a real one; failing here ended the run first and the p2guard twin was TWIN-NOT-RUN)
                g_guard_held_hurt = Life() < g_bot.count0 || g_combat.blocks <= g_bot.value0;
                Next();
            }
            break;
        case 3:
            // R1 let go: the guard comes down, and a blow then hurts.
            g_bot.buttons = 0;
            g_bot.seen = g_combat.guard == 0 ? g_bot.seen + 1 : 0;

            if (g_bot.seen >= 30 && g_partner.invincible == 0) {
                g_bot.count0 = Life();
                blow();
                Next();
            } else if (g_bot.t > 600) {
                Fail("setup: P2's guard never came down");
            }
            break;
        case 4:
            if (g_bot.t == 20) {
                std::printf("coop-test: guard down: life %d/%d (%d before)\n", Life(), MaxLife(), g_bot.count0);

                if (Life() >= g_bot.count0) {
                    Fail("setup: the bot's blow did not hurt P2 with its guard down");
                    return;
                }
                if (g_guard_held_hurt) {
                    Fail("a blow on P2's held guard hurt it");
                    return;
                }

                Finish(true, "");
            }
            break;
    }
}

// --- thirst: P2's own water drains while it walks (the script walks pad 2), the player's stands
// still with the player; P2 drinks water from the pack; empty, P2 loses life and the player does not.
void Thirst() {
    int chara = g_partner.loaded_chara;
    int player = UserStatus->cur_chara;

    switch (g_bot.step) {
        case 0:
            if (g_bot.t >= 60 && g_partner.down == 0) {
                if (ItemCount(ITEM_REGULAR_WATER) <= 0) {
                    Fail("setup: no water in the pack (--give 145)");
                    return;
                }

                g_bot.p1[0] = UserStatus->water_now[chara];
                g_bot.p1[1] = UserStatus->water_now[player];
                Next();
            }
            break;
        case 1:
            if (g_bot.t == 600) {
                float p2 = UserStatus->water_now[chara];
                float p1 = UserStatus->water_now[player];

                std::printf("coop-test: after 600 ticks walking: P2 water %.2f -> %.2f, P1 water %.2f -> %.2f\n", g_bot.p1[0], p2, g_bot.p1[1],
                            p1);

                if (!(p2 < g_bot.p1[0] - 0.5f)) {
                    Fail("P2's own water did not drain while it walked");
                    return;
                }

                if (std::fabs(p1 - g_bot.p1[1]) > 0.001f) {
                    Fail("the player's water changed with P2's walking");
                    return;
                }

                g_bot.p2[0] = p2;
                Next();
            }
            break;
        case 3:
            TapAt(2, kInputSquare);

            if (g_bot.t == 240) {
                {
                    {
                        int   vol = ITEM_LIST[ITEM_REGULAR_WATER - ITEM_ATTACH_START].vol;
                        float now = UserStatus->water_now[chara];

                        std::printf("coop-test: water item: P2 water %.2f -> %.2f (volume %d), %d -> %d in the pack\n", g_bot.p2[0], now, vol,
                                    g_bot.count0, ItemCount(ITEM_REGULAR_WATER));

                        if (!(now > g_bot.p2[0] + 0.5f * (float) std::min(vol, 15)) || ItemCount(ITEM_REGULAR_WATER) != g_bot.count0 - 1) {
                            Fail("P2 did not drink the water itself");
                            return;
                        }

                        Next();
                    }
                }
            }
            break;
        case 2:
            if (g_combat.item == ITEM_REGULAR_WATER && (g_bot.buttons & kInputRight) == 0) {
                // Setup: room in P2's gauge for the drink.
                UserStatus->water_now[chara] = 10.0f;
                g_bot.p2[0] = UserStatus->water_now[chara];
                g_bot.count0 = ItemCount(ITEM_REGULAR_WATER);
                Next();
            } else {
                TapAt((g_bot.t / 14) * 14, kInputRight);

                if (g_bot.t > 14 * 10) {
                    Fail("Right never picked the water");
                }
            }
            break;
        case 4:
            if (g_bot.t == 1) {
                // Setup: P2's water gone.
                UserStatus->water_now[chara] = 0.0f;
                g_bot.value0 = Life();
                g_bot.count0 = UserStatus->hp[player];
            }

            if (g_bot.t == 300) {
                std::printf("coop-test: empty: P2 life %d -> %d, P1 life %d -> %d\n", g_bot.value0, Life(), g_bot.count0,
                            (int) UserStatus->hp[player]);

                if (!(Life() < g_bot.value0)) {
                    Fail("P2 lost no life with its water empty");
                    return;
                }

                if (UserStatus->hp[player] != g_bot.count0) {
                    Fail("the player lost life to P2's thirst");
                    return;
                }

                Finish(true, "");
            }
            break;
    }
}

// The ABS a kill gives a member's equipped weapon by the game's rule, worked out here on its own.
int ExpectedAbs(int chara, int monster_exp) {
    int slot = UserStatus->equipped_weapon_slot[chara];

    if (slot < 0 || slot >= 11) {
        return 0;
    }

    const WEAPON_HAVE &weapon = UserStatus->chara_weapons[chara][slot];

    if (((CDngStatusData *) UserStatus)->CheckDefaultWeapon(chara) == 0) {
        return 0;
    }

    int exp = monster_exp * (NowMonstorUnit->back_dungeon != 0 ? 2 : 1);

    if ((weapon.flags & 0x2000) != 0) {
        exp = (int) ((float) exp * 1.2f);
    }

    return std::min(exp, std::max(0, GetWeaponMaxExp((WEAPON_HAVE *) &weapon) - weapon.experience));
}

int WeaponExp(int chara) {
    int slot = UserStatus->equipped_weapon_slot[chara];

    return slot >= 0 && slot < 11 ? UserStatus->chara_weapons[chara][slot].experience : 0;
}

// --- abs: the killing blow decides whose weapon takes the ABS: a monster P2 finishes gives P2's
// weapon exactly its ABS and the player's none; one the player finishes (a hit of the player's,
// placed on it) gives the player's weapon its ABS by the game's own rule and P2's none.
void Abs() {
    int chara = g_partner.loaded_chara;
    int player = UserStatus->cur_chara;

    switch (g_bot.step) {
        case 0: {
            // The awake monster nearest P2 (the game keeps a far one asleep), one point of life left;
            // then a hit on it of P2's own, marked as its blows and shots are (MarkHit), for "abs", or
            // of the player's (owner the controlled character, as basic_damage records) for "absp1".
            float best = 1.0e9f;

            g_bot.target = -1;

            for (int i = 0; i < 16; i++) {
                float position[4];

                if (NowMonstorUnit->monster[i].state != 2 || NowMonstorUnit->monster[i].hp <= 0) {
                    continue;
                }

                NowMonstorUnit->chara[i][0].GetPosition(position);

                if (DistVector(position, g_partner.pos) < best) {
                    best = DistVector(position, g_partner.pos);
                    g_bot.target = i;
                }
            }

            if (g_bot.target >= 0 && g_bot.t >= 60 && Idle()) {
                // Setup: P2 holds a weapon that is not its default (default weapons take no ABS), given
                // here as joining the party gives one (a --give at start lays the floor out otherwise).
                int slots[11];
                int count = WeaponSlots(slots, 11);
                bool has = false;

                for (int i = 0; i < count; i++) {
                    has = has || UserStatus->chara_weapons[chara][slots[i]].item_no == ITEM_WEAPON_STEEL_SLINGSHOT;
                }

                if (!has) {
                    ((CDngStatusData *) UserStatus)->GetItem(ITEM_WEAPON_STEEL_SLINGSHOT, 0);
                    count = WeaponSlots(slots, 11);
                }

                for (int i = 0; i < count; i++) {
                    if (UserStatus->chara_weapons[chara][slots[i]].item_no == ITEM_WEAPON_STEEL_SLINGSHOT) {
                        UserStatus->equipped_weapon_slot[chara] = (s8) slots[i];
                    }
                }

                g_bot.first = std::strcmp(g_bot.test, "absp1") == 0 ? player : chara;
                NowMonstorUnit->monster[g_bot.target].hp = 1;
                g_bot.value0 = WeaponExp(chara);
                g_bot.count0 = WeaponExp(player);
                g_bot.expect = ExpectedAbs(g_bot.first, NowMonstorUnit->monster[g_bot.target].exp);
                std::printf("coop-test: %s to finish monster %d (exp %d): P2 ABS %d, P1 ABS %d, expect +%d to %s\n",
                            g_bot.first == chara ? "P2" : "P1", g_bot.target, NowMonstorUnit->monster[g_bot.target].exp, g_bot.value0,
                            g_bot.count0, g_bot.expect, g_bot.first == chara ? "P2" : "P1");
                Next();
            } else if (g_bot.t > 6000) {
                Fail("no awake monster on the floor");
            }
            break;
        }
        case 1:
            if (g_bot.t % 10 == 1 && NowMonstorUnit->monster[g_bot.target].hp > 0) {
                float position[4];

                NowMonstorUnit->chara[g_bot.target][0].GetPosition(position);
                position[1] += 8.0f;

                int hit = NowColData->Set(position, 5, 1, 12.0f, 0.0f, 2, 2, 0, 0);

                if (g_bot.first == chara) {
                    MarkHit(hit);
                } else if (hit >= 0) {
                    NowColData->hit[hit].owner = player;
                }
            }

            if (NowMonstorUnit->monster[g_bot.target].hp <= 0 && g_bot.t > 2) {
                Next();
            } else if (g_bot.t > 600) {
                Fail("the hit never finished the monster");
            }
            break;
        case 2:
            // The game credits a kill once the death has played out (state -1).
            if (NowMonstorUnit->monster[g_bot.target].state != -1) {
                if (g_bot.t > 900) {
                    Fail("the monster's death never played out");
                }

                break;
            }

            if (++g_bot.tries == 10) {
                int p2_gain = WeaponExp(chara) - g_bot.value0;
                int p1_gain = WeaponExp(player) - g_bot.count0;

                std::printf("coop-test: monster %d down, last attacker %d: P2 ABS +%d, P1 ABS +%d (expect +%d to %s)\n", g_bot.target,
                            NowMonstorUnit->monster[g_bot.target].last_attacker, p2_gain, p1_gain, g_bot.expect,
                            g_bot.first == chara ? "P2" : "P1");

                if (NowMonstorUnit->monster[g_bot.target].last_attacker != g_bot.first) {
                    Fail("the killing blow was not recorded as the killer's");
                    return;
                }

                if (g_bot.first == chara ? (p2_gain != g_bot.expect || p1_gain != 0) : (p1_gain != g_bot.expect || p2_gain != 0)) {
                    Fail("the ABS did not go to the killer's weapon alone");
                    return;
                }

                Finish(true, "");
            }
            break;
    }
}

// The durability of a member's equipped weapon (0 with none).
float Durability(int chara) {
    int slot = UserStatus->equipped_weapon_slot[chara];

    return slot >= 0 && slot < 11 && UserStatus->chara_weapons[chara][slot].item_no > 0 ? UserStatus->chara_weapons[chara][slot].durability_f
                                                                                         : 0.0f;
}

// --- wear: as P2 (Goro, DC_COOP_PARTNER=2) with a mallet, P2's hits on a monster wear P2's weapon and
// not the player's (the game's CheckDmg wears the controlled character's for owners 0, 2 and 4);
// Repair Powder from the pack mends it; worn out it breaks, the member's weapon changes and the hand
// model follows.
void Wear() {
    int chara = g_partner.loaded_chara;
    int player = UserStatus->cur_chara;

    switch (g_bot.step) {
        case 0: {
            float best = 1.0e9f;

            g_bot.target = -1;

            for (int i = 0; i < 16; i++) {
                float position[4];

                if (NowMonstorUnit->monster[i].state != 2 || NowMonstorUnit->monster[i].hp <= 0) {
                    continue;
                }

                NowMonstorUnit->chara[i][0].GetPosition(position);

                if (DistVector(position, g_partner.pos) < best) {
                    best = DistVector(position, g_partner.pos);
                    g_bot.target = i;
                }
            }

            if (chara != CHARA_GORO) {
                if (g_bot.t > 600) {
                    Fail("setup: P2 is not Goro (DC_COOP_PARTNER=2)");
                }

                return;
            }

            if (g_bot.target >= 0 && g_bot.t >= 60 && Idle()) {
                // Setup: Goro holds a mallet (not his default, so it wears), and the monster has life
                // enough for the hits.
                ((CDngStatusData *) UserStatus)->GetItem(ITEM_WEAPON_MALLET, 0);

                int slots[11];
                int count = WeaponSlots(slots, 11);

                for (int i = 0; i < count; i++) {
                    if (UserStatus->chara_weapons[chara][slots[i]].item_no == ITEM_WEAPON_MALLET) {
                        UserStatus->equipped_weapon_slot[chara] = (s8) slots[i];
                    }
                }

                NowMonstorUnit->monster[g_bot.target].hp = 999;
                g_bot.p1[0] = Durability(chara);
                g_bot.p1[1] = Durability(player);
                Next();
            } else if (g_bot.t > 6000) {
                Fail("no awake monster on the floor");
            }
            break;
        }
        case 1:
            // Five hits of P2's on the monster, a few ticks apart.
            if (g_bot.t % 12 == 1 && g_bot.t < 60) {
                float position[4];

                NowMonstorUnit->chara[g_bot.target][0].GetPosition(position);
                position[1] += 8.0f;
                MarkHit(NowColData->Set(position, 1, 1, 12.0f, 0.0f, 2, 2, 0, 0));
            }

            if (g_bot.t == 90) {
                std::printf("coop-test: after P2's hits: P2 weapon %d durability %.2f -> %.2f, P1's %.2f -> %.2f\n",
                            EquippedWeapon(chara) != nullptr ? EquippedWeapon(chara)->item_no : -1, g_bot.p1[0], Durability(chara),
                            g_bot.p1[1], Durability(player));

                if (!(Durability(chara) < g_bot.p1[0] - 0.01f)) {
                    Fail("P2's hits did not wear P2's weapon");
                    return;
                }

                if (Durability(player) != g_bot.p1[1]) {
                    Fail("P2's hits wore the player's weapon");
                    return;
                }

                ((CDngStatusData *) UserStatus)->GetItem(ITEM_REPAIR_POWDER, 0);
                Next();
            }
            break;
        case 2:
            if (g_combat.item == ITEM_REPAIR_POWDER && (g_bot.buttons & kInputRight) == 0) {
                Next();
            } else {
                TapAt((g_bot.t / 14) * 14, kInputRight);

                if (g_bot.t > 14 * 12) {
                    Fail("Right never picked the Repair Powder");
                }
            }
            break;
        case 3:
            TapAt(2, kInputSquare);

            if (g_bot.t == 30) {
                const WEAPON_HAVE *weapon = EquippedWeapon(chara);

                std::printf("coop-test: Repair Powder: P2 durability %.2f of %d\n", Durability(chara), weapon != nullptr ? weapon->durability : 0);

                if (weapon == nullptr || Durability(chara) != (float) weapon->durability) {
                    Fail("Repair Powder did not mend P2's weapon");
                    return;
                }

                // Setup: the mallet all but worn out.
                ((WEAPON_HAVE *) weapon)->durability_f = 0.05f;
                g_bot.value0 = g_combat.breaks;
                g_bot.count0 = g_combat.reloads;
                g_bot.first = weapon->item_no;
                Next();
            }
            break;
        case 4:
            if (g_bot.t % 12 == 1 && g_combat.breaks == g_bot.value0) {
                float position[4];

                NowMonstorUnit->chara[g_bot.target][0].GetPosition(position);
                position[1] += 8.0f;
                MarkHit(NowColData->Set(position, 1, 1, 12.0f, 0.0f, 2, 2, 0, 0));
            }

            if (g_combat.breaks > g_bot.value0 && g_bot.tries++ == 20) {
                const WEAPON_HAVE *weapon = EquippedWeapon(chara);
                int                now = weapon != nullptr ? weapon->item_no : -1;

                std::printf("coop-test: broke: P2 weapon %d -> %d, hand reloads %d -> %d, model %d\n", g_bot.first, now, g_bot.count0,
                            g_combat.reloads, (int) g_partner.weapon_loaded);

                if (now == g_bot.first) {
                    Fail("the broken weapon is still the one equipped");
                    return;
                }

                if (g_combat.reloads <= g_bot.count0) {
                    Fail("the hand model did not follow the weapon change");
                    return;
                }

                Finish(true, "");
            } else if (g_bot.t > 600) {
                Fail("the worn-out weapon never broke");
            }
            break;
    }
}

// A monster's hit with flags at the partner, as a monster's attack records one (users' mask).
void MonsterHitOnPartner(int flags) {
    float position[4];

    sceVu0CopyVector(position, g_partner.pos);
    position[1] += 8.0f;
    NowColData->Set(position, 1, 6, 8.0f, 0.0f, 1, 2, flags, 0);
}

// --- ailment: a poisoning hit poisons P2 (its member's ailment, not the player's), the poison bites
// 4% of its life every three seconds while the player is untouched, an Antidote from the pack cures it;
// a freezing hit freezes it, and frozen it cannot swing until it thaws.
void Ailment() {
    int chara = g_partner.loaded_chara;
    int player = UserStatus->cur_chara;

    switch (g_bot.step) {
        case 0:
            if (g_bot.t >= 60 && Idle()) {
                UserStatus->ailments[chara] = 0;
                g_bot.value0 = UserStatus->hp[player];
                g_bot.count0 = UserStatus->ailments[player];
                Next();
            }
            break;
        case 1:
            // Hits until the 65% roll poisons it (the partner is not made invincible in this test).
            if (g_bot.t % 70 == 1 && !PartnerAilment(AILMENT_POISON)) {
                MonsterHitOnPartner(0x200);
            }

            if (PartnerAilment(AILMENT_POISON)) {
                SetLife(MaxLife());
                g_bot.tries = Life();
                Next();
            } else if (g_bot.t > 1400) {
                Fail("poisoning hits never poisoned P2");
            }
            break;
        case 2:
            if (g_bot.t == 400) {
                int bite = std::max(1, (int) (0.04 * UserStatus->max_hp[chara]));

                std::printf("coop-test: poisoned: P2 life %d -> %d (bite %d each 180 ticks), P1 life %d -> %d, P1 ailments %#x -> %#x\n",
                            g_bot.tries, Life(), bite, g_bot.value0, (int) UserStatus->hp[player], g_bot.count0,
                            (unsigned) UserStatus->ailments[player]);

                if (Life() != g_bot.tries - 2 * bite) {
                    Fail("the poison did not bite P2 4% every three seconds");
                    return;
                }

                if (UserStatus->hp[player] != g_bot.value0 || UserStatus->ailments[player] != g_bot.count0) {
                    Fail("P2's poison touched the player");
                    return;
                }

                ((CDngStatusData *) UserStatus)->GetItem(ITEM_ANTIDOTE_DRINK, 0);
                Next();
            }
            break;
        case 3:
            if (g_combat.item == ITEM_ANTIDOTE_DRINK && (g_bot.buttons & kInputRight) == 0) {
                Next();
            } else {
                TapAt((g_bot.t / 14) * 14, kInputRight);

                if (g_bot.t > 14 * 12) {
                    Fail("Right never picked the Antidote");
                }
            }
            break;
        case 4:
            TapAt(2, kInputSquare);

            if (g_bot.t == 30) {
                if (PartnerAilment(AILMENT_POISON)) {
                    Fail("the Antidote did not cure P2");
                    return;
                }

                MonsterHitOnPartner(0x100000);
                g_bot.count0 = g_combat.blows;
                Next();
            }
            break;
        case 5:
            if (g_bot.t < 200) {
                g_bot.buttons = (g_bot.t / 6) % 2 == 0 ? kInputCross : 0;

                if (g_bot.t == 10 && !PartnerAilment(AILMENT_FREEZE)) {
                    Fail("a freezing hit did not freeze P2");
                    return;
                }

                if (g_combat.blows != g_bot.count0 || g_partner.attacking) {
                    Fail("frozen P2 swung");
                    return;
                }
            } else if (!PartnerAilment(AILMENT_FREEZE)) {
                std::printf("coop-test: P2 thawed after %d ticks\n", g_bot.t);
                Finish(true, "");
            } else if (g_bot.t > 600) {
                Fail("P2 never thawed");
            }
            break;
    }
}

// --- trap: a trapped chest P2 opened (TrapOpenedByPartner, as coop_interact marks it) springs on P2:
// the game's own trap opcodes put the bomb at P2 and the ailment on P2, the player untouched.
void Trap() {
    int chara = g_partner.loaded_chara;
    int player = UserStatus->cur_chara;

    if (g_bot.step == 0 && g_bot.t >= 60 && Idle()) {
        UserStatus->ailments[chara] = 0;
        UserStatus->ailments[player] = 0;
        TrapOpenedByPartner();

        RS_STACKDATA argument;

        argument.type = RS_INT;
        argument.i = AILMENT_POISON;
        _SET_STATUS_ERR(&argument, 1);
        _BOM_SET(&argument, 0);

        int   bomb = NowColData->now_hit;
        float at[4];

        sceVu0CopyVector(at, NowColData->hit[bomb].pos);
        std::printf("coop-test: trap: P2 ailments %#x, P1 ailments %#x; bomb at (%.0f %.0f %.0f), %.0f from P2, %.0f from P1\n",
                    (unsigned) UserStatus->ailments[chara], (unsigned) UserStatus->ailments[player], at[0], at[1], at[2],
                    DistVector(at, g_partner.pos), DistVector(at, CharaMain.pos));

        if ((UserStatus->ailments[chara] & AILMENT_POISON) == 0 || (UserStatus->ailments[player] & AILMENT_POISON) != 0) {
            Fail("the trap's ailment did not go to P2 alone");
            return;
        }

        if (!(DistVector(at, g_partner.pos) < 20.0f) || (DistVector(g_partner.pos, CharaMain.pos) > 25.0f && DistVector(at, CharaMain.pos) < 20.0f)) {
            Fail("the trap's bomb did not go off at P2");
            return;
        }

        Finish(true, "");
    }
}

// --- spring: P2 standing in a heal spring's zone (the bot finds one on the floor and holds P2 there)
// is healed and its water refilled by its own position; the hurt player, away from it, is not.
void Spring() {
    int chara = g_partner.loaded_chara;
    int player = UserStatus->cur_chara;

    switch (g_bot.step) {
        case 0:
            if (g_bot.t >= 60 && Idle()) {
                // A part on the floor with a healing zone, and the zone's centre.
                g_bot.target = -1;

                for (int i = 0; i < 400 && g_bot.target < 0; i++) {
                    int parts_no = NowDngMap->cells[i].parts_no;

                    if (parts_no != MAP_PARTS_NONE && NowDngMap->parts[parts_no].heal_on != 0) {
                        const CDungeonParts &part = NowDngMap->parts[parts_no];

                        g_bot.target = i;
                        g_bot.p2[0] = part.heal_pos[0] + 160.0f * (i % 20);
                        g_bot.p2[1] = part.heal_pos[1] - 2.0f;
                        g_bot.p2[2] = part.heal_pos[2] + 160.0f * (i / 20);
                    }
                }

                if (g_bot.target < 0) {
                    Fail("setup: no heal spring on this floor");
                    return;
                }

                // Setup: both hurt and P2 thirsty.
                SetLife(std::max(1, MaxLife() - 20));
                UserStatus->water_now[chara] = 10.0f;
                UserStatus->hp[player] = (s16) std::max(1, UserStatus->max_hp[player] - 10);
                g_bot.value0 = Life();
                g_bot.count0 = UserStatus->hp[player];
                g_bot.p1[0] = UserStatus->water_now[chara];
                std::printf("coop-test: heal spring at cell %d (%.0f %.0f %.0f); P2 life %d water %.1f, P1 life %d\n", g_bot.target,
                            g_bot.p2[0], g_bot.p2[1], g_bot.p2[2], g_bot.value0, g_bot.p1[0], g_bot.count0);
                Next();
            }
            break;
        case 1:
            // Setup: P2 held in the zone.
            g_partner.pos[0] = g_bot.p2[0];
            g_partner.pos[1] = g_bot.p2[1];
            g_partner.pos[2] = g_bot.p2[2];

            if (g_bot.t == 120) {
                std::printf("coop-test: in the spring: P2 life %d -> %d of %d, water %.1f -> %.1f; P1 life %d -> %d\n", g_bot.value0, Life(),
                            MaxLife(), g_bot.p1[0], UserStatus->water_now[chara], g_bot.count0, (int) UserStatus->hp[player]);

                if (Life() != MaxLife() || !(UserStatus->water_now[chara] > g_bot.p1[0] + 5.0f)) {
                    Fail("the spring did not heal P2 and refill its water");
                    return;
                }

                if (DistVector(g_partner.pos, CharaMain.pos) > 60.0f && UserStatus->hp[player] != g_bot.count0) {
                    Fail("P2's spring healed the player");
                    return;
                }

                Finish(true, "");
            }
            break;
    }
}

// --- motion: the member pad 2 plays (DC_COOP_PARTNER) walks with its run or walk motion playing on
// (its frame advancing), and after a tapped attack goes back to standing or moving within two seconds,
// not held on an attack pose. The script walks pad 2.
void Motion() {
    CCharacter *partner = g_partner.chara;
    float       time = partner->motion_type.state.time;
    bool        moving = g_partner.velocity[0] != 0.0f || g_partner.velocity[2] != 0.0f;

    switch (g_bot.step) {
        case 0:
            if (g_bot.t >= 60 && Idle() && moving) {
                g_bot.p1[0] = time;
                g_bot.value0 = 0;
                Next();
            } else if (g_bot.t > 2000) {
                Fail("setup: P2 never walked");
            }
            break;
        case 1:
            // Counts the ticks the motion's frame moved on while walking, and how far.
            if (moving && time != g_bot.p1[0]) {
                g_bot.value0++;

                if (time > g_bot.p1[0]) {
                    g_bot.p1[1] += time - g_bot.p1[0];
                }
            }

            g_bot.p1[0] = time;

            if (g_bot.t == 60) {
                std::printf("coop-test: chara %d walking: motion %d, frame %.1f, frame moved on %d of 60 ticks, %.3f frames a tick\n",
                            g_partner.loaded_chara, g_partner.motion, time, g_bot.value0, g_bot.p1[1] / 60.0f);

                if (g_partner.motion != 1 && g_partner.motion != 2) {
                    Fail("P2 walks without its walk or run motion");
                    return;
                }

                if (g_bot.value0 < 40) {
                    Fail("P2's walk motion does not play (it slides)");
                    return;
                }

                Next();
            }
            break;
        case 2: {
            // A tapped attack plays only the motions the game's tap plays for the character (Xiao's
            // wind-up and throw; Ruby's and Osmond's one shot motion; the others' swing), never a
            // charge or hold pose.
            static const int kTap[6][2] = {{0x24, -1}, {11, 13}, {0x24, -1}, {11, -1}, {0x25, -1}, {11, -1}};
            int              chara = g_partner.loaded_chara;

            TapAt(0, kInputCross);

            if (g_partner.attacking && g_partner.motion != kTap[chara][0] && g_partner.motion != kTap[chara][1] && g_partner.motion != 1 &&
                g_partner.motion != 0) {
                std::printf("coop-test: chara %d's tapped attack played motion %d\n", chara, g_partner.motion);
                Fail("a tapped attack played a motion the game's tap does not");
                return;
            }

            if (g_bot.t > 6 && !g_partner.attacking) {
                std::printf("coop-test: chara %d's tapped attack took %d ticks\n", chara, g_bot.t);
                Next();
            } else if (g_bot.t > 400) {
                Fail("the attack never ended");
            }
            break;
        }
        case 3:
            if (g_partner.motion == 0 || g_partner.motion == 1 || g_partner.motion == 2) {
                std::printf("coop-test: chara %d back to motion %d %d ticks after the attack\n", g_partner.loaded_chara, g_partner.motion, g_bot.t);
                Finish(true, "");
            } else if (g_bot.t > 120) {
                std::printf("coop-test: chara %d still on motion %d (frame %.1f) after the attack\n", g_partner.loaded_chara, g_partner.motion, time);
                Fail("P2 stays on an attack pose after the attack");
            }
            break;
    }
}

// --- propeller: P2 Osmond's propeller (the "tukene1" frame) turns from tick to tick, as the player's.
void Propeller() {
    CFrame *propeller = g_partner.chara != nullptr && g_partner.chara->frame != nullptr
                            ? g_partner.chara->frame->SearchFrame((char *) "tukene1")
                            : nullptr;

    if (g_partner.loaded_chara != CHARA_OSMOND || propeller == nullptr) {
        if (g_bot.t > 600) {
            Fail("setup: P2 is not Osmond with a propeller frame (DC_COOP_PARTY=6 DC_COOP_PARTNER=5)");
        }

        return;
    }

    // The turn as drawn, not the rotation asked for.
    sceVu0FVECTOR rotation = {0.0f, PropellerTurn(g_partner.chara), 0.0f, 0.0f};

    if (g_bot.t > 60 && g_bot.t <= 120) {
        if (std::fabs(rotation[1] - g_bot.p1[0]) > 0.01f) {
            g_bot.value0++;
        }
    }

    g_bot.p1[0] = rotation[1];

    if (g_bot.t == 121) {
        std::printf("coop-test: Osmond's propeller turned on %d of 60 ticks (now %.2f)\n", g_bot.value0, rotation[1]);

        if (g_bot.value0 < 50) {
            Fail("P2 Osmond's propeller does not spin");
            return;
        }

        Finish(true, "");
    }
}

// --- sparkle: a heal spring's heal on P2 (PartnerSpringHeal, as standing in one does) sets off P2's
// own heal sparkle, drawn around P2, and not the player's.
void Sparkle() {
    switch (g_bot.step) {
        case 0:
            if (g_bot.t >= 60 && Idle()) {
                // Setup: the partner hurt, so the spring heals it and the sparkle plays.
                SetLife(std::max(1, MaxLife() - 20));
                PartnerSpringHeal();
                Next();
            }
            break;
        case 1:
            if (g_bot.t == 20) {
                float at[4] = {g_heal_drawn_at[0], g_heal_drawn_at[1], g_heal_drawn_at[2], 1.0f};

                std::printf("coop-test: P2 sparkle %d drawn at (%.0f %.0f %.0f), %.1f from P2, %.1f from P1; the player's sparkle %d\n",
                            (int) g_heal.active, at[0], at[1], at[2], DistVector(at, g_partner.pos), DistVector(at, CharaMain.pos),
                            (int) HealEffect.active);

                if (g_heal.active == 0 || DistVector(at, g_partner.pos) > 3.0f) {
                    Fail("P2's spring heal did not set off a sparkle around P2");
                    return;
                }

                if (HealEffect.active != 0) {
                    Fail("P2's spring heal set off the player's sparkle");
                    return;
                }

                Finish(true, "");
            }
            break;
    }
}

// --- leash: P2 walks away from the player (the script walks pad 2 each way in turn) and is never put
// back beside it: it gets well past the old leash (220) and no tick moves it more than a step.
void Leash() {
    float dx = g_partner.pos[0] - CharaMain.pos[0];
    float dz = g_partner.pos[2] - CharaMain.pos[2];
    float apart = sqrtf(dx * dx + dz * dz);

    if (g_bot.t > 60) {
        float step = sqrtf((g_partner.pos[0] - g_bot.p1[0]) * (g_partner.pos[0] - g_bot.p1[0]) +
                           (g_partner.pos[2] - g_bot.p1[2]) * (g_partner.pos[2] - g_bot.p1[2]));

        // A pull-back: P2 far out (past where a floor's arrival places it) and then moved in by more than
        // a step in one tick. The arrival's own placement, near the player, is not one.
        if (step > 20.0f && g_partner.pos[1] > -90.0f && g_bot.p2[0] > 150.0f && apart < g_bot.p2[0]) {
            std::printf("coop-test: P2 jumped %.0f in a tick at %.0f apart (tick %d)\n", step, g_bot.p2[0], g_bot.t);
            Fail("P2 was put back beside the player");
            return;
        }

        g_bot.p2[1] = std::max(g_bot.p2[1], apart);
    }

    g_bot.p2[0] = apart;
    sceVu0CopyVector(g_bot.p1, g_partner.pos);

    if (g_bot.t == 2400) {
        std::printf("coop-test: P2 got %.0f from the player (the old leash was 220)\n", g_bot.p2[1]);

        if (g_bot.p2[1] < 240.0f) {
            Fail("P2 never got past the old leash distance");
            return;
        }

        Finish(true, "");
    }
}

// --- p1down: the player's life runs out while P2 is up and beside it: the game plays on (no death, no
// leaving the dungeon), the player lies down, and P2 standing by gets it up.
void P1Down() {
    switch (g_bot.step) {
        case 0:
            if (g_bot.t >= 60 && Idle()) {
                // Setup: the player's life gone, as a monster's last blow would leave it.
                UserStatus->hp[UserStatus->cur_chara] = 0;
                BtActStatus.invincible_frames = 0;
                Next();
            }
            break;
        case 1:
            if (gameTask != GAME_TASK_PLAY) {
                std::printf("coop-test: task %#x with P2 up\n", (int) gameTask);
                Fail("the player's death ended the run while P2 was up");
                return;
            }

            if (g_combat.player_down > 0) {
                // p1still (Jep's 2026-10-20 playtest: down, Toan still spun with the stick): the script holds
                // pad 1's stick over from tick 700, and the player lying down must not turn.
                sceVu0FVECTOR rotation;

                CharaFrame->GetRotation(rotation);
                if (g_bot.value0 == 0) {
                    g_bot.yaw0 = rotation[1];
                    g_bot.seen = 0;
                } else if (std::strcmp(g_bot.test, "p1still") == 0 && std::fabs(rotation[1] - g_bot.yaw0) > 0.01f) {
                    std::printf("coop-test: the player turned while down (%.3f to %.3f)\n", g_bot.yaw0, rotation[1]);
                    Fail("the player turned while down");
                    return;
                }
                g_bot.seen++;
                g_bot.value0 = 1;
            }

            if (g_bot.value0 != 0 && g_combat.player_down == 0 && UserStatus->hp[UserStatus->cur_chara] > 0) {
                std::printf("coop-test: the player was down %d ticks and got up after %d ticks, life %d/%d\n", g_bot.seen, g_bot.t,
                            (int) UserStatus->hp[UserStatus->cur_chara], (int) UserStatus->max_hp[UserStatus->cur_chara]);
                Finish(true, "");
            } else if (g_bot.t > 900) {
                Fail("the player never got up");
            }
            break;
    }
}

// --- p1poison / p1fall (Jep's 2026-10-24 playtest: Toan died to poison and no death animation played):
// the player's life runs out while P2 is up, to poison (p1poison: the drain shown over frames, so the
// game counts it dead while the bar still moves) or all at once (p1fall); it goes down and its fall,
// the death motion (0x17), plays to its end, as the game's own death plays it. p1press (Jep's retest:
// he could still attack while down, in slow attack motions): as p1fall with pad 1 mashing Cross (attack) and
// the stick held (p1-press-down.txt); lying down it never acts, its motion only the fall.
void P1Fall() {
    bool poison = std::strcmp(g_bot.test, "p1poison") == 0;
    bool press = std::strcmp(g_bot.test, "p1press") == 0;
    int  chara = UserStatus->cur_chara;

    switch (g_bot.step) {
        case 0:
            if (g_bot.t >= (press ? 300 : 60) && Idle()) {
                if (poison) {
                    UserStatus->hp[chara] = 1;
                    UserStatus->next_hp[chara] = 1;
                    UserStatus->life_step[chara] = 0;
                    UserStatus->ailments[chara] |= AILMENT_POISON;
                    std::printf("coop-test: the player poisoned at life 1/%d\n", (int) UserStatus->max_hp[chara]);
                } else {
                    UserStatus->hp[chara] = 0;
                }
                BtActStatus.invincible_frames = 0;
                g_bot.value0 = 0;
                g_bot.count0 = 0;
                g_bot.swing = -1.0f;
                Next();
            }
            break;
        case 1: {
            if (gameTask != GAME_TASK_PLAY) {
                std::printf("coop-test: task %#x with P2 up\n", (int) gameTask);
                Fail("the player's death ended the run while P2 was up");
                return;
            }

            if (g_combat.player_down <= 0) {
                if (g_bot.value0 != 0) {
                    std::printf("coop-test: up after %d ticks down; furthest into the fall %.1f\n", g_bot.count0, g_bot.swing);
                    Fail("the player got up before its fall played");
                } else if (g_bot.t > 600) {
                    Fail("the player never went down");
                }
                return;
            }

            float start = CharaMain.motion_type.motion_info[23].start;
            float end = CharaMain.motion_type.motion_info[23].end;
            float time = CharaMain.motion_type.state.time;

            if (g_bot.value0 == 0) {
                std::printf("coop-test: the player is down at tick %d (motion %d, time %.1f, fall %.1f to %.1f)\n", g_bot.t,
                            (int) CharaMain.motion_no, time, start, end);
                g_bot.value0 = 1;
            }

            g_bot.count0++;

            // Lying down, no attack, item or action starts and no other motion plays.
            if (g_bot.count0 >= 3 && (BtActStatus.action_on != 0 || CharaMain.motion_no != 0x17)) {
                std::printf("coop-test: %d ticks down: action %d, motion %d\n", g_bot.count0, (int) BtActStatus.action_on,
                            (int) CharaMain.motion_no);
                Fail("the player acted while down");
                return;
            }

            if (CharaMain.motion_no == 0x17) {
                g_bot.swing = std::max(g_bot.swing, time - start);
            }

            // At the fall's end it lies still on its last frame (no second fall) for 20 ticks.
            if (g_bot.value0 >= 2) {
                if (CharaMain.motion_no != 0x17 || time < end - 3.0f || time > end + 1.0f) {
                    std::printf("coop-test: %d ticks after the fall: motion %d, time %.1f of %.1f\n", g_bot.value0 - 2,
                                (int) CharaMain.motion_no, time, end);
                    Fail("the player did not lie still after its fall");
                } else if (++g_bot.value0 >= 22) {
                    Finish(true, "");
                }
            } else if (CharaMain.motion_no == 0x17 && time >= end - 3.0f && time <= end + 1.0f) {
                std::printf("coop-test: the fall played to its end (%.1f of %.1f) %d ticks after going down\n", time, end, g_bot.count0);
                g_bot.value0 = 2;
            } else if (g_bot.count0 > 100) {
                std::printf("coop-test: down %d ticks: motion %d, time %.1f, furthest into the fall %.1f of %.1f\n", g_bot.count0,
                            (int) CharaMain.motion_no, time, g_bot.swing, end - start);
                Fail("the player went down without its fall");
            }
            break;
        }
    }
}

// --- bothdown: the player down while P2 is up, then P2 down too: the game's death runs to its game
// over (out of the dungeon), not to another member, though the party has others alive.
void BothDownTest() {
    switch (g_bot.step) {
        case 0:
            if (g_bot.t >= 60 && Idle()) {
                UserStatus->hp[UserStatus->cur_chara] = 0;
                BtActStatus.invincible_frames = 0;
                Next();
            }
            break;
        case 1:
            if (g_bot.t == 30) {
                // Setup: P2 knocked down too.
                SetLife(0);
                g_partner.down = kDownTicks;
                g_partner.attacking = false;
                PlayMotion(kDownMotion, 2);
            }

            if (gameTask == GAME_TASK_DEAD_GAMEOVER) {
                std::printf("coop-test: both down: the game over after %d ticks\n", g_bot.t);
                Finish(true, "");
            } else if (gameTask == GAME_TASK_DEAD_CHANGE_CHARA) {
                Fail("both down went to another member, not the game over");
            } else if (g_bot.t > 900) {
                std::printf("coop-test: task %#x\n", (int) gameTask);
                Fail("both down never reached the game over");
            }
            break;
    }
}

// --- deathwall: a bat the bot kills five units off a wall, its knockback thrown at the wall, falls to
// the ground and is gone as one killed in the open is: on the ground within kDeathFallTicks of the kill
// (the script's first 33 ticks are its death cry, the fall from flying height about 20 more), gone
// (state -1) within kDeathGoneTicks. Retail hung it in the air until its death script's timeout (180
// ticks) and then put it on the ground (Jep's playtest note 39); DC_COOP_MUTATE=deathwall is that, the twin.
// Setup: the floor's bats sleep far off, so the bot takes one to a spot near the player (where it wakes)
// with a wall within reach, and holds it there until it is awake and its height above the ground measured.
// The kill is the game's own: life 0, the hit's knockback, and the monster's script 120 (its death), as
// CheckDmg's kill has CMonstorUnit::Step run it.
constexpr int kDeathFallTicks = 120;
constexpr int kDeathGoneTicks = 170;
constexpr int kDeathPolys = 4096;

CCPoly g_death_polys[kDeathPolys];

bool IsBat(int i) {
    const MONSTOR &monster = NowMonstorUnit->monster[i];
    int            base = monster.base_model;

    if (base < 0 || base >= 9) {
        return false;
    }

    const char *script = NowMonstorUnit->model[base].script_name;
    return std::strcmp(script, "e60a") == 0 || std::strcmp(script, "e61a") == 0;
}

// The nearest wall within 28 units of position, as a unit direction and a distance, looked for at the
// height a monster's own wall check uses (5 above it), in sixteen directions.
bool NearWall(const float *position, float *direction, float *distance) {
    float at[4];
    float from[4];
    float to[4];
    float hit[4];

    sceVu0CopyVector(at, (float *) position);
    int count = setCollisionData(NowDngMap, g_death_polys, at, 30.0f, 2.0f);

    if (count <= 0 || count >= kDeathPolys) {
        return false;
    }

    float best = 1.0e9f;

    for (int k = 0; k < 16; k++) {
        float angle = (float) k * (2.0f * PI / 16.0f);
        float d[4] = {std::sin(angle), 0.0f, std::cos(angle), 0.0f};

        sceVu0CopyVector(from, at);
        from[1] += 5.0f;
        sceVu0CopyVector(to, from);
        to[0] += 28.0f * d[0];
        to[2] += 28.0f * d[2];

        if (CheckHit(g_death_polys, count, from, to, hit, 1, 0) >= 0) {
            float h = std::hypot(hit[0] - from[0], hit[2] - from[2]);

            if (h < best) {
                best = h;
                sceVu0CopyVector(direction, d);
            }
        }
    }

    *distance = best;
    return best < 28.0f;
}

void DeathWall() {
    MONSTOR *bat = g_bot.target >= 0 ? &NowMonstorUnit->monster[g_bot.target] : nullptr;

    BtActStatus.invincible_frames = std::max((int) BtActStatus.invincible_frames, 2); // P1 waits unharmed

    switch (g_bot.step) {
        case 0: { // a bat, and a spot by the player 5 units off a wall at a bat's flying height
            if (g_bot.t < 60) {
                break;
            }

            for (int i = 0; i < 16 && g_bot.target < 0; i++) {
                const MONSTOR &monster = NowMonstorUnit->monster[i];

                if (monster.state != -1 && monster.hp > 0 && monster.revealed != 0 && IsBat(i)) {
                    g_bot.target = i;
                }
            }

            if (g_bot.target < 0) {
                Fail("precondition: no bat on the floor");
                return;
            }

            for (int r = 20; r <= 140; r += 20) {
                for (int k = 0; k < 16; k++) {
                    float angle = (float) k * (2.0f * PI / 16.0f);
                    float spot[4] = {CharaMain.pos[0] + r * std::sin(angle), CharaMain.pos[1] + 21.0f, CharaMain.pos[2] + r * std::cos(angle), 1.0f};
                    float direction[4];
                    float distance;

                    if (!NearWall(spot, direction, &distance) || distance < 6.0f) {
                        continue;
                    }

                    spot[0] += (distance - 5.0f) * direction[0];
                    spot[2] += (distance - 5.0f) * direction[2];
                    sceVu0CopyVector(g_bot.p1, spot);
                    sceVu0CopyVector(g_bot.p2, direction);
                    std::printf("coop-test: deathwall: bat %d (%s) taken to (%.0f %.0f %.0f), %d from the player, 5 off a wall (%.0f %.0f)\n",
                                g_bot.target, NowMonstorUnit->model[NowMonstorUnit->monster[g_bot.target].base_model].script_name, spot[0], spot[1],
                                spot[2], r, direction[0] * 100.0f, direction[2] * 100.0f);
                    Next();
                    return;
                }
            }

            Fail("precondition: no wall within reach of a spot near the player");
            break;
        }
        case 1: // held at the spot until awake and measured, then the kill
            NowMonstorUnit->chara[g_bot.target][0].SetPosition(g_bot.p1);

            if (bat->state == 2 && g_bot.t >= 30 && bat->ground_distance >= 10.0f) {
                sceVu0CopyVector(bat->knockback_direction, g_bot.p2);
                bat->knockback[0] = 1.5f;
                bat->knockback[1] = 0.15f;
                bat->last_attacker = UserStatus->cur_chara;
                bat->hp = 0;
                NowMonstorUnit->alive_count--;

                int saved = NowMonstorUnit->current_monster;

                NowMonstorUnit->current_monster = g_bot.target;
                NowMonstorUnit->interpreter[g_bot.target].run(120);
                NowMonstorUnit->script_state[g_bot.target] = 1;
                NowMonstorUnit->current_monster = saved;
                g_bot.first = -1;
                std::printf("coop-test: deathwall: bat %d (%s) killed %.1f above the ground, 5 off a wall, knocked at it\n", g_bot.target,
                            NowMonstorUnit->model[bat->base_model].script_name, bat->ground_distance);
                Next();
            } else if (g_bot.t > 600) {
                char why[128];

                std::snprintf(why, sizeof why, "precondition: the bat at the spot never woke in the air (state %d, %.1f above the ground)", bat->state,
                              bat->ground_distance);
                Fail(why);
            }
            break;
        case 2: // the fall, then gone
            if (g_bot.first < 0 && bat->state == 2 && bat->ground_distance < 5.0f) {
                g_bot.first = g_bot.t;
                std::printf("coop-test: deathwall: on the ground %d ticks after the kill\n", g_bot.t);
            }

            if (bat->state == -1) {
                std::printf("coop-test: deathwall: gone %d ticks after the kill, on the ground at %d\n", g_bot.t, g_bot.first);

                if (g_bot.first < 0 || g_bot.first > kDeathFallTicks) {
                    Fail("the bat hung in the air against the wall instead of falling");
                } else if (g_bot.t > kDeathGoneTicks) {
                    Fail("the bat took too long to go");
                } else {
                    Finish(true, "");
                }
            } else if (g_bot.t > 600) {
                Fail("the bat never went");
            }
            break;
    }
}

// --- stats: each member pad 2 takes up (the script presses L3) plays on its own life, defence and
// attack, worked out here from the save on their own; none on the controlled character's (Toan's).
void Stats() {
    int chara = g_partner.loaded_chara;
    int player = UserStatus->cur_chara;

    if (chara == g_bot.last || !Idle() || g_combat.scale != 1.0f) {
        if (g_bot.t > 7000) {
            Fail("fewer than five members were taken up");
        }

        return;
    }

    g_bot.last = chara;

    int  life_max = UserStatus->max_hp[chara];
    int  guard = UserStatus->defense[chara] * ((UserStatus->ailments[chara] & AILMENT_STAMINA) != 0 ? 2 : 1);
    int  attack = ExpectedAttack(chara);
    bool ok = MaxLife() == life_max && Life() <= MaxLife() && Guard(chara) == guard && Damage() == attack;

    std::printf("coop-test: stats chara %d: life max %d (save %d), guard %d (save %d), attack %d (expect %d); player chara %d max %d "
                "guard %d attack %d -> %s\n",
                chara, MaxLife(), life_max, Guard(chara), guard, Damage(), attack, player, UserStatus->max_hp[player],
                UserStatus->defense[player], ExpectedAttack(player), ok ? "ok" : "WRONG");

    if (!ok) {
        Fail("a member's stats are not its own");
        return;
    }

    if (chara != player) {
        g_bot.seen |= 1 << chara;
    }

    int members = 0;

    for (int i = 0; i < 6; i++) {
        members += (g_bot.seen >> i) & 1;
    }

    if (members >= 5) {
        Finish(true, "");
    }
}

} // namespace

void CombatTestTick() {
    static const char *test = TestEnv("DC_COOP_TEST");

    if (test == nullptr) {
        return;
    }

    g_bot.test = test;

    if (g_bot.done) {
        if (g_bot.stop_in > 0 && --g_bot.stop_in == 0) {
            dc::app::GameRequestStop();
        }

        return;
    }

    // The mode first: a dungeon floor, co-op on and the partner on it.
    if (!g_bot.mode) {
        if (!Enabled() || NowDngMap == nullptr || CharaFrame == nullptr || !g_partner.visible || g_partner.loaded_chara < 0) {
            return;
        }

        g_bot.mode = true;
        std::printf("coop-test: mode dungeon %d floor %d, task %d, P2 chara %d (%s), P1 chara %d, test %s, mutation %s\n",
                    (int) selectMapNo, (int) UserStatus->cur_floor, (int) gameTask, g_partner.loaded_chara,
                    OwnLife(g_partner.loaded_chara) ? "joined" : "stand-in", (int) UserStatus->cur_chara, test,
                    TestEnvOr("DC_COOP_MUTATE", "none"));
        sceVu0CopyVector(g_bot.p1_last, CharaFrame->position);
    }

    // coop-osmondflash: the steps begin at the floor's first tick of play. The partner is up from the floor's arrival
    // scene on (B1's script 100, some 350 ticks, the partner standing in it), and a bot that started there pressed while
    // neither player could act: effectblock's one Cross went nowhere and it gave up before play began. Once in play, a
    // task the test itself starts (a menu, a death) no longer holds it. DC_COOP_MUTATE=bot_in_scene: the steps start
    // under the scene, as before (twin).
    if (!g_bot.playing) {
        if (gameTask != GAME_TASK_PLAY && !Mutated("bot_in_scene")) {
            return;
        }

        g_bot.playing = true;
        std::printf("coop-test: play from task %d\n", (int) gameTask);
    }

    if (++g_bot.ticks > kRunTicks) {
        char why[96];

        std::snprintf(why, sizeof why, "timed out at step %d", g_bot.step);
        Fail(why);
        return;
    }

    g_bot.t++;

    // The scripted player's walk, for the panel test.
    g_bot.moving = DistVector(g_bot.p1_last, CharaFrame->position) > 0.3f ? g_bot.moving + 1 : 0;
    sceVu0CopyVector(g_bot.p1_last, CharaFrame->position);

    // Setup for the single-player feature tests: the partner takes no harm, so a monster cannot
    // knock it down mid-test. Not for the tests that read life (stats, ailment, bothdown, p2guard).
    if (std::strcmp(test, "stats") != 0 && std::strcmp(test, "ailment") != 0 && std::strcmp(test, "bothdown") != 0 &&
        std::strcmp(test, "p2guard") != 0) {
        g_partner.invincible = std::max(g_partner.invincible, 2);
    }

    if (std::strcmp(test, "charge") == 0) {
        Charge();
    } else if (std::strcmp(test, "lock") == 0) {
        Lock();
    } else if (std::strcmp(test, "aim") == 0) {
        Aim();
    } else if (std::strcmp(test, "item") == 0) {
        Item();
    } else if (std::strcmp(test, "weapon") == 0) {
        Weapon();
    } else if (std::strcmp(test, "weaponzone") == 0) { // coop: gaps2
        WeaponZone();
    } else if (std::strcmp(test, "fightzone") == 0) { // coop: gaps2
        FightZone();
    } else if (std::strcmp(test, "panel") == 0) {
        Panel();
    } else if (std::strcmp(test, "stats") == 0) {
        Stats();
    } else if (std::strcmp(test, "wheel") == 0) {
        Wheel();
    } else if (std::strcmp(test, "reset") == 0) {
        Reset();
    } else if (std::strcmp(test, "ring") == 0) {
        Ring();
    } else if (std::strcmp(test, "flash") == 0) {
        Flash();
    } else if (std::strcmp(test, "effectblock") == 0) {
        EffectBlock();
    } else if (std::strcmp(test, "weaponpanel") == 0) {
        WeaponPanel();
    } else if (std::strcmp(test, "swapicon") == 0) {
        SwapIcon();
    } else if (std::strcmp(test, "swapspeed") == 0) {
        SwapSpeed();
    } else if (std::strcmp(test, "p2guard") == 0) {
        P2Guard();
    } else if (std::strcmp(test, "thirst") == 0) {
        Thirst();
    } else if (std::strcmp(test, "abs") == 0 || std::strcmp(test, "absp1") == 0) {
        Abs();
    } else if (std::strcmp(test, "wear") == 0) {
        Wear();
    } else if (std::strcmp(test, "ailment") == 0) {
        Ailment();
    } else if (std::strcmp(test, "trap") == 0) {
        Trap();
    } else if (std::strcmp(test, "spring") == 0) {
        Spring();
    } else if (std::strcmp(test, "motion") == 0) {
        Motion();
    } else if (std::strcmp(test, "propeller") == 0) {
        Propeller();
    } else if (std::strcmp(test, "sparkle") == 0) {
        Sparkle();
    } else if (std::strcmp(test, "leash") == 0) {
        Leash();
    } else if (std::strcmp(test, "p1down") == 0 || std::strcmp(test, "p1still") == 0) {
        P1Down();
    } else if (std::strcmp(test, "p1poison") == 0 || std::strcmp(test, "p1fall") == 0 || std::strcmp(test, "p1press") == 0) {
        P1Fall();
    } else if (std::strcmp(test, "bothdown") == 0) {
        BothDownTest();
    } else if (std::strcmp(test, "deathwall") == 0) { // coop: deathwall
        DeathWall();
    } else {
        Fail("unknown test");
    }
}

bool CombatTestPad(InputPadState &pad) {
    g_bot.live_was = g_bot.live;
    g_bot.live = pad.buttons;

    if (g_bot.test == nullptr || !g_bot.mode || std::strcmp(g_bot.test, "stats") == 0) {
        return false;
    }

    pad.buttons = g_bot.buttons;

    if (g_bot.steer) {
        pad.left_x = g_bot.lx;
        pad.left_y = g_bot.ly;
    }

    return true;
}

} // namespace coop
