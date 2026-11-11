#include "coop/game_ns.hpp"
#include "coop/coop.hpp"
#include "coop/coop_audio.hpp" // coop-menuse
#include "coop/coop_perf.hpp"
#include "coop/netplay.hpp"
#include "coop/coop_combat.hpp" // coop: p2weapon (StarterWeapon, EquippedWeapon)
#include "coop/coop_camera.hpp" // coop-joinvisuals: PlayerDrawnPlace
#include "coop/coop_worlds.hpp" // coop-joinvisuals: CoopWorldsEntryStandIn

#include <SDL3/SDL.h>
#include <libvu0.h>


#include <algorithm>
#include <string>
#include <vector>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <new>
#include <system_error>

#include "btactstatus.hpp"
#include "text/caption_text.hpp"
#include "rect.hpp"
#include "shot_freefuncs.hpp"
#include "snd.hpp"
#include "btitem.hpp"
#include "btmisc.hpp"
#include "camerafollow.hpp"
#include "character.hpp"
#include "clothread.hpp"
#include "collision.hpp"
#include "collisiondata.hpp"
#include "common.h" // coop-hudicon: GS_Y_OFFSET
#include "dataalloc.hpp"
#include "dataread.hpp"
#include "dngstatusdata.hpp"
#include "platform/files.hpp"
#include "savedata.hpp"
#include "dranmapfield.hpp"
#include "dun/gameloop.hpp"
#include "dngmessageman.hpp"
#include "dungeoneventdata.hpp"
#include "dungeoneventman.hpp"
#include "dungeonmap.hpp"
#include "sysmes.hpp"
#include "frame.hpp"
#include "frameattr.hpp"
#include "gamepad.hpp"
#include "gameutil.hpp"
#include "hitvalue.hpp"
#include "itemdata.hpp"
#include "menu_draw.hpp"
#include "menu_inventory.hpp" // WepDataListToHaveCopy (the stand-in wipe test hook)
#include "mglib.hpp"
#include "monstorunit.hpp"
#include "input/mouse_collision.hpp"
#include "shot_utils.hpp"
#include "platform/config.hpp"
#include "platform/input.hpp"
#include "main.hpp" // coop-keydoor: GAME_MODE_DUNGEON
#include "texture.hpp"
#include "render/texture_registry.hpp" // coop-keydoor: PortMoveKeys (the HUD sheet after a join)
#include "textureanime.hpp"
#include "userstatus.hpp"

#include "coop/coop_core.hpp"
#include "coop/coop_camtest.hpp"
#include "coop/coop_party.hpp"
#include "coop/coop_actor.hpp" // coop-pt1109b: ActorLocalIndex
#include "coop/coop_inventory.hpp" // coop-revive (the test hook's powders)
#include "coop/coop_prompt.hpp"    // coop-revive
#include "coop/coop_interact.hpp"  // coop-openercam: DrawPartnerPresentation

namespace dc::game {
int keyCtrl(float x, float y, MOTION_INFO *motion);
extern const std::array<const char *, 6> charaNameTbl; // btitem.cpp: each member's .chr pack

extern CFrameVu1 *CharaFrame;
extern int        statusAlarmRate;
extern s32        gameTask;
extern s32        iventMarker;
extern s32        mode; // coop-keydoor: the HUD sheet after a join, dungeons only
} // namespace dc::game

void PortWeaponFxSlotSet(int slot); // coop-kocatchup (coop_combat_damage.cpp)
int  PortWeaponFxSlot();

namespace coop {

bool BlockEmpty() {
    for (const CTexture &texture : TexManager.textures) {
        if (texture.block == PartnerTextureBlock()) {
            return false;
        }
    }

    return true;
}

// Whether pad 2 may take chara: it has joined (in a party of one, any member stands in, as a fresh
// save plays together), the player is not playing it, and it is not lying down (no life left in the
// party status, or a down count still running from when it was left).
// A party of one lends pad 2 Xiao (or Toan) as a stand-in, never a later member: Goro and the others
// wait for the story (Jep, 2026-10-25: no Goro stand-in, and no mallet in the starting inventory).
// DC_COOP_MUTATE=standin_any: any member, as before.
bool Pickable(int chara) {
    bool joined = chara < UserStatus->party_size || (UserStatus->party_size <= 1 && (chara <= CHARA_XIAO || Mutated("standin_any")));

    return chara >= 0 && chara < 6 && joined && chara != UserStatus->cur_chara && !(OwnLife(chara) && UserStatus->hp[chara] <= 0) &&
           g_partner.down_left[chara] == 0;
}

// The partner: the member pad 2 picked (StepSwap), else Xiao when she has joined and is not in
// control, else the next member who has joined. When the player takes pad 2's member, pad 2 moves
// on to the next one it may take, and keeps it. A downed partner stays where it lies until it gets
// up. A party of one still gets Xiao (or Toan, when Xiao is in control), so that a fresh save can be
// played together. Reads only party status and choice, so linked copies pick alike.
// Test hooks, once: DC_COOP_PARTY=N makes the first N members joined (a --jump save has Toan
// alone), DC_COOP_PARTNER=n has pad 2 start as member n.
// coop-chestsync (train 49, q-chest-face-xiao): they ran at the first pick, which a catch-up host (alone on its floor
// until player 2 comes) reaches only at the join, inside the lockstep: on the host alone, whose env has them (status,
// save and choice parted at tick 1980). Now at the floor's start, before the host's checkpoint and recording, so its
// state set carries them; a joiner taking that state spends them unapplied. (Its twin party_hook_late retired with
// q-chest-face-xiao~partyhook, coop-fx51b 2026-11-05: the ordering exists only with a test-hook party.)
void PartyTestHooks(bool apply) {
    static bool hooked = false;

    if (!hooked) {
        hooked = true;

        if (!apply) {
            if (TestEnv("DC_COOP_PARTY") != nullptr || TestEnv("DC_COOP_PARTNER") != nullptr || TestEnv("DC_COOP_TEST_P1_LIFE") != nullptr ||
                TestEnv("DC_COOP_TEST_WEAPONFX_SLOT") != nullptr) {
                std::printf("coop: party test hooks not applied: this copy takes the host's game\n");
            }
            return;
        }

        if (const char *party = TestEnv("DC_COOP_PARTY")) {
            static const int starter[6] = {ITEM_WEAPON_DAGGER,        ITEM_WEAPON_WOODEN_SLINGSHOT, ITEM_WEAPON_MALLET,
                                           ITEM_WEAPON_GOLD_RING,     ITEM_WEAPON_FIGHTING_STICK,   ITEM_WEAPON_MACHINE_GUN};

            UserStatus->party_size = (s8) std::clamp(std::atoi(party), 1, 6);

            // Each member joined this way gets its starting weapon, as joining the party gives it, so
            // the player's own swap onto it finds one equipped.
            for (int i = 1; i < UserStatus->party_size; i++) {
                if (UserStatus->equipped_weapon_slot[i] < 0) {
                    ((CDngStatusData *) UserStatus)->GetItem(starter[i], 0);
                    UserStatus->equipped_weapon_slot[i] = 0;
                }
            }
        }

        if (const char *chosen = TestEnv("DC_COOP_PARTNER")) {
            g_partner.choice = std::clamp(std::atoi(chosen), 0, 5);
        }

        // DC_COOP_TEST_P1_LIFE=N: the player's member starts with N life of N (coop-chestsync: a catch-up host waiting
        // alone on B1 through a slow join is not knocked out first, in a row that tests something else). In the save,
        // before the checkpoint, so the joiner takes it with the host's state.
        if (const char *life = TestEnv("DC_COOP_TEST_P1_LIFE")) {
            const int chara = UserStatus->cur_chara;
            const s16 n = (s16) std::clamp(std::atoi(life), 1, 9999);

            UserStatus->max_hp[chara] = n;
            UserStatus->hp[chara] = n;
            UserStatus->next_hp[chara] = n;
        }
        // DC_COOP_TEST_WEAPONFX_SLOT=n (coop-kocatchup): the slot the next elemental hit lights (CWeaponElFx) starts at n, as
        // a host that fought alone up to a catch-up join leaves it; at the floor's start, so the joiner takes it with his state.
        if (const char *slot = TestEnv("DC_COOP_TEST_WEAPONFX_SLOT")) {
            PortWeaponFxSlotSet(std::atoi(slot));
            std::fprintf(stderr, "coop: party test hooks: weapon element slot %d\n", PortWeaponFxSlot());
        }
        std::printf("coop: party test hooks applied: party %d, choice %d, player life %d/%d\n", (int) UserStatus->party_size, g_partner.choice,
                    (int) UserStatus->hp[UserStatus->cur_chara], (int) UserStatus->max_hp[UserStatus->cur_chara]);
    }
}

int PickPartner() {
    PartyTestHooks(true);

    int current = UserStatus->cur_chara;
    int party = std::min<int>(UserStatus->party_size, 6);
    int loaded = g_partner.loaded_chara;

    if (party <= 1) {
        if (g_partner.down > 0 && loaded >= 0 && loaded != current) {
            return loaded;
        }

        return Pickable(g_partner.choice) ? g_partner.choice : current == CHARA_XIAO ? CHARA_TOAN : CHARA_XIAO;
    }

    if (g_partner.down > 0 && loaded >= 0 && loaded < party && loaded != current) {
        return loaded;
    }

    if (Pickable(g_partner.choice) || (Mutated("p1takes") && g_partner.choice >= 0)) {
        return g_partner.choice;
    }

    int start = g_partner.choice >= 0 ? g_partner.choice : current != CHARA_XIAO ? CHARA_XIAO : CHARA_TOAN;

    for (int pass = 0; pass < 2; pass++) {
        for (int k = 0; k < 6; k++) {
            int i = (start + k) % 6;

            // The second pass takes a member lying down when no other is standing.
            if (pass == 0 ? Pickable(i) : i < party && i != current) {
                g_partner.choice = i;
                return i;
            }
        }
    }

    return current == CHARA_XIAO ? CHARA_TOAN : CHARA_XIAO;
}

// Pad 2's member swap: press L3 for the next member who may play, Select+L3 for the one before (pad 2's
// other buttons belong to its camera and combat). Not while lying down or mid-swing; the new member
// loads on the next tick where the old one stood (CoopStep reloads the model when PickPartner's
// answer changes).
void StepSwap(const InputPadState &pad) {
    SoundOwner sound(2); // coop-menuse: pad 2's swap
    bool l3 = (pad.buttons & kInputL3) != 0;
    int  step = !l3 || Mutated("swap") ? 0 : (pad.buttons & kInputSelect) != 0 ? -1 : 1;
    bool pressed = step != 0 && !g_partner.swap_held;

    g_partner.swap_held = step != 0;

    if (!pressed || g_partner.down > 0 || g_partner.attacking || g_partner.loaded_chara < 0) {
        return;
    }

    for (int k = 1; k < 6; k++) {
        int i = (g_partner.loaded_chara + step * k + 6) % 6;

        if (Pickable(i)) {
            g_partner.choice = i;
            SndSePlay(MENU_SOUND_CONFIRM, -1, 0);
            std::printf("coop: pad 2 swaps to %d (%s)\n", i, charaNameTbl[i]);
            return;
        }
    }
}

namespace {

void GetUp(int life) {
    g_partner.down = 0;
    g_partner.revive = 0;
    SetLife(std::max(1, life));
    g_partner.invincible = kInvincibleTicks * 2;
    PlayMotion(0, 0);
}

} // namespace

// The revive rule. A partner with no life left lies down for kDownTicks (10 s), then gets up with a
// quarter of its life. The player can get it up sooner: standing still within kReviveDistance of it,
// with life of its own, for kReviveTicks (2 s) brings it up with half its life; moving or stepping
// away starts the count again. No button, so nothing either pad already does is taken. Each member
// keeps its own down count: one the player takes over while it lies down (down_left) goes on counting
// and gets up the same way. Everything here reads the shared simulation (positions, party status), so
// linked copies agree. True while the partner lies down.
bool StepDown() {
    float moved_x = CharaFrame->position[0] - g_partner.player_last[0];
    float moved_z = CharaFrame->position[2] - g_partner.player_last[2];
    bool  still = moved_x * moved_x + moved_z * moved_z < 0.01f;

    g_partner.player_last[0] = CharaFrame->position[0];
    g_partner.player_last[2] = CharaFrame->position[2];

    // coop-revive: test hook (DC_COOP_REVIVETEST=<case>, x-revive.ps1). On the 60th tick of play the reviver gets one
    // Revive Powder (case p2revives: player 2's bag, nopowder: none, else player 1's pack); from tick 90 the partner is
    // held 15 beside the player, and on tick 150 the partner goes down (p2revives: the player's life runs out instead).
    // Ticks of the shared simulation, so linked copies do it alike. coop_prompt.cpp answers the question.
    static const char *revive_test = TestEnv("DC_COOP_REVIVETEST");
    static int         revive_ticks = 0;

    if (revive_test != nullptr && revive_ticks < 1000) {
        std::string test = revive_test;
        bool        p2 = test == "p2revives";
        int         tick = ++revive_ticks;

        if (tick == 60) {
            if (p2) {
                PartnerGetItem(ITEM_REVIVAL_POWDER, -1, false);
            } else if (test != "nopowder") {
                ((CDngStatusData *) UserStatus)->GetItem(ITEM_REVIVAL_POWDER, 0);
            }

            std::printf("coop-debug: revivetest %s: switch %s, powders P1 %d P2 %d\n", revive_test, RevivePowderOn() ? "on" : "off",
                        RevivePowders(1), RevivePowders(2));
        }

        if (tick >= 90 && tick <= 150) {
            g_partner.pos[0] = CharaFrame->position[0] + 15.0f;
            g_partner.pos[1] = CharaFrame->position[1];
            g_partner.pos[2] = CharaFrame->position[2];
            g_partner.chara->frame->SetPosition(g_partner.pos);
        }

        if (tick == 150 && p2) {
            UserStatus->hp[(int) UserStatus->cur_chara] = 0;
            BtActStatus.invincible_frames = 0;
            std::printf("coop-debug: revivetest: the player's life runs out\n");
        } else if (tick == 150 && g_partner.down == 0) {
            SetLife(0);
            g_partner.down = kDownTicks;
            g_partner.attacking = false;
            PlayMotion(kDownMotion, 2);
            std::printf("coop-debug: revivetest: partner knocked down\n");
        }
    }

    // Test hook (DC_COOP_DOWN_AT=N): the partner is knocked down on its Nth tick of play.
    // coop-fx51d: DC_COOP_DOWN_AT=loaded:N counts its ticks from a state file's load (x-dungeon-descent-down -Fixture).
    static const char *down_at = TestEnv("DC_COOP_DOWN_AT");
    static const bool  down_loaded = down_at != nullptr && std::strncmp(down_at, "loaded:", 7) == 0;
    static int         played = 0;

    if (down_at != nullptr && !(down_loaded && NetplayStateLoadPending()) && ++played == std::atoi(down_loaded ? down_at + 7 : down_at) &&
        g_partner.down == 0) {
        SetLife(0);
        g_partner.down = kDownTicks;
        g_partner.attacking = false;
        PlayMotion(kDownMotion, 2);
        std::printf("coop-debug: partner knocked down\n");
    }

    // Test hook (DC_COOP_RELOAD_WEAPON=N): once, the partner's member gets its next two weapons, and the
    // three are equipped and reloaded N times in the order w01 w02 w03 w01 w03 w02 ...; the arena's use
    // must stay at the body plus the weapon in hand, never growing over the cycles.
    static const char *reloads = TestEnv("DC_COOP_RELOAD_WEAPON");
    static bool        reloaded = false;

    if (reloads != nullptr && !reloaded && g_partner.loaded_chara > 0 && OwnLife(g_partner.loaded_chara)) {
        static const int starter[6] = {ITEM_WEAPON_DAGGER,    ITEM_WEAPON_WOODEN_SLINGSHOT, ITEM_WEAPON_MALLET,
                                       ITEM_WEAPON_GOLD_RING, ITEM_WEAPON_FIGHTING_STICK,   ITEM_WEAPON_MACHINE_GUN};
        static const int order[6] = {0, 1, 2, 0, 2, 1};
        int              chara = g_partner.loaded_chara;
        int              used[3] = {-1, -1, -1};
        bool             steady = true;

        reloaded = true;
        ((CDngStatusData *) UserStatus)->GetItem(starter[chara] + 1, 0);
        ((CDngStatusData *) UserStatus)->GetItem(starter[chara] + 2, 0);

        for (int i = 0, n = std::atoi(reloads); i < n; i++) {
            int slot = order[i % 6];

            UserStatus->equipped_weapon_slot[chara] = (s8) slot;
            ReloadWeapon();

            if (used[slot] == -1) {
                used[slot] = g_partner.arena.used;
            } else if (used[slot] != g_partner.arena.used) {
                steady = false;
            }
        }

        std::printf("coop-debug: weapon reloaded %d times, body %d, arena with w0 %d w1 %d w2 %d, %s; slot %d holds item %d\n",
                    std::atoi(reloads), g_partner.weapon_mark, used[0], used[1], used[2], steady ? "steady" : "GROWING",
                    (int) UserStatus->equipped_weapon_slot[chara],
                    UserStatus->chara_weapons[chara][(int) UserStatus->equipped_weapon_slot[chara]].item_no);
    }

    // Test hook (DC_COOP_PARTYSAVE_TEST=<dir>): once, pad 2's pick saved into dir, forgotten and loaded
    // back; then the player's member written as the pick, which loading must skip.
    static const char *party_dir = TestEnv("DC_COOP_PARTYSAVE_TEST");
    static bool        party_tested = false;

    if (party_dir != nullptr && !party_tested && g_partner.loaded_chara > 0) {
        party_tested = true;

        int picked = g_partner.choice;

        CoopPartyDelete(party_dir); // nothing from an earlier run

        CoopPartySave(party_dir);
        g_partner.choice = -1;
        CoopPartyLoad(party_dir);
        std::printf("coop-debug: party save test: picked %d, after load %d\n", picked, g_partner.choice);

        g_partner.choice = UserStatus->cur_chara;
        CoopPartySave(party_dir);
        CoopPartyLoad(party_dir);
        std::printf("coop-debug: party save test: player's member saved, after load %d\n", g_partner.choice);
        g_partner.choice = picked;
    }

    // Test hook (DC_COOP_DOWN_FLOOR): the partner is held down at no life for as long as the party is
    // on the floor the run started on, so only a floor change can bring it up.
    static const bool down_floor = TestEnv("DC_COOP_DOWN_FLOOR") != nullptr;
    static int        start_floor = -100;

    if (down_floor) {
        if (start_floor == -100) {
            start_floor = UserStatus->cur_floor;
        }

        if (UserStatus->cur_floor == start_floor) {
            if (g_partner.down == 0) {
                SetLife(0);
                g_partner.attacking = false;
                PlayMotion(kDownMotion, 2);
                std::printf("coop-debug: partner held down on floor %d\n", start_floor);
            }

            g_partner.down = kDownTicks;
            g_partner.revive = -1;
        }
    }

    for (int i = 0; i < 6; i++) {
        if (i == g_partner.loaded_chara || g_partner.down_left[i] == 0) {
            continue;
        }

        if (--g_partner.down_left[i] == 0 && OwnLife(i) && UserStatus->hp[i] <= 0 && i != UserStatus->cur_chara) {
            UserStatus->AddNowLife(i, (s16) std::max(1, UserStatus->max_hp[i] / 4), 0.0f);
        }
    }

    if (g_partner.down == 0) {
        g_partner.revive = 0;
        ReviveAsk(0, false); // coop-revive: up some other way: no question left waiting
        return false;
    }

    float dx = g_partner.pos[0] - CharaFrame->position[0];
    float dz = g_partner.pos[2] - CharaFrame->position[2];
    bool  beside = dx * dx + dz * dz <= kReviveDistance * kReviveDistance && UserStatus->hp[(int) UserStatus->cur_chara] > 0;

    // coop-revive: with the Revive Powder rule on, standing still beside it asks "Use Revive Powder?" instead
    // (coop_prompt.hpp); there is no free revive.
    if (RevivePowderOn()) {
        g_partner.revive = beside && still ? g_partner.revive + 1 : 0;

        if (ReviveAsk(0, g_partner.revive >= kReviveAskTicks)) {
            GetUp(MaxLife() / 2);
            std::printf("coop: player revives partner with a Revive Powder, life %d/%d\n", Life(), MaxLife());
            return false;
        }
    } else {
        bool holding = beside && still && !Mutated("revive");

        g_partner.revive = holding ? g_partner.revive + 1 : 0;

        if (g_partner.revive >= kReviveTicks) {
            GetUp(MaxLife() / 2);
            std::printf("coop: player revives partner, life %d/%d\n", Life(), MaxLife());
            return false;
        }
    }

    if (--g_partner.down == 0) {
        GetUp(Mutated("getup") ? MaxLife() / 2 : MaxLife() / 4);
        std::printf("coop: partner gets up, life %d/%d\n", Life(), MaxLife());
        return false;
    }

    return true;
}

// The partner's life is its character's own in the party status, as the party menu shows it, so it
// carries between floors and the game revives it as it does any member. A character that has not
// joined has no life there; it stands in with a pool of its own for the floor, as large as that
// character's own most life (CDngStatusData::Initialize gives every member its starting life, Xiao 60,
// Goro 100, ...), kept for each member across swaps. coop-party
bool OwnLife(int chara) {
    return chara >= 0 && chara < 6 && chara < UserStatus->party_size && UserStatus->max_hp[chara] > 0;
}

int MaxLife() {
    int chara = g_partner.loaded_chara;

    if (chara < 0 || chara >= 6) {
        return 1;
    }

    return Mutated("standin") ? 70 : UserStatus->max_hp[chara] > 0 ? UserStatus->max_hp[chara] : 70;
}

int Life() {
    int chara = g_partner.loaded_chara;

    if (OwnLife(chara)) {
        return UserStatus->hp[chara];
    }

    if (chara < 0 || chara >= 6) {
        return 0;
    }

    return g_partner.stand_in_life[chara] < 0 ? MaxLife() : std::min(g_partner.stand_in_life[chara], MaxLife());
}

void SetLife(int life) {
    int chara = g_partner.loaded_chara;

    if (OwnLife(chara)) {
        UserStatus->AddNowLife(chara, (s16) (life - UserStatus->hp[chara]), 0.0f);
    } else if (chara >= 0 && chara < 6) {
        g_partner.stand_in_life[chara] = std::clamp(life, 0, MaxLife());
    }
}

void PlayMotion(int motion, int flags) {
    CCharacter *partner = g_partner.chara;

    partner->motion_no = motion;
    partner->motion_flags = flags;
    partner->motion_speed = -1.0f;
    g_partner.motion = motion;
}

bool HasMotion(int motion) {
    const MOTION_INFO &info = g_partner.chara->motion_type.motion_info[motion];
    return info.end > info.start && info.start >= 0;
}

// The two do not stand in each other. coop-nopush (Jep's 2026-10-27 playtest: "P1 can push P2 around, while P1 is solid
// to P2"; his rule: neither pushes the other): each is a wall in the other's walk (CoopPartnerWall, and the player's
// body in Walk), so walking into the other stops, 12 from them (the body's 7 and MoveCheck's 5). Only a pair already in
// each other (a placement, a landing on the other's spot) gets here: the partner steps out of the player to kApart,
// through MoveCheck so it crosses no wall (wedged in, it may stay closer). Exactly on top, it steps back from the way it
// faces. The player is the game's and is never moved. Reads only simulation state.
void KeepApart(CCPoly *polys, int count) {
    if (Mutated("apart")) {
        return;
    }

    float dx = g_partner.pos[0] - CharaFrame->position[0];
    float dz = g_partner.pos[2] - CharaFrame->position[2];
    float distance = sqrtf(dx * dx + dz * dz);

    if (distance >= kApart || std::fabs(g_partner.pos[1] - CharaFrame->position[1]) > kApart) {
        return;
    }

    float ux = -g_partner.heading[0];
    float uz = -g_partner.heading[2];

    if (distance > 0.01f) {
        ux = dx / distance;
        uz = dz / distance;
    } else if (ux * ux + uz * uz < 0.01f) {
        ux = 1.0f;
        uz = 0.0f;
    }

    sceVu0FVECTOR push = {ux * (kApart - distance), 0.0f, uz * (kApart - distance), 0.0f};
    sceVu0FVECTOR moved;
    MoveCheckInfo info;

    MoveCheck(g_partner.pos, push, moved, &info, polys, count, 1);
    sceVu0CopyVector(g_partner.pos, moved);
}

void PlaceBesidePlayer(const char *reason) {
    sceVu0FVECTOR rotation;

    // Beside the player, at its right hand: the way it faces is (sin, cos) of its yaw.
    CharaFrame->GetRotation(rotation);
    g_partner.pos[0] = CharaFrame->position[0] - kSpawnDistance * cosf(rotation[1]);
    g_partner.pos[1] = CharaFrame->position[1];
    g_partner.pos[2] = CharaFrame->position[2] + kSpawnDistance * sinf(rotation[1]);
    g_partner.pos[3] = 1.0f;
    g_partner.velocity[0] = g_partner.velocity[1] = g_partner.velocity[2] = 0.0f;

    CFrame *frame = g_partner.chara->frame;
    frame->SetPosition(g_partner.pos);
    frame->SetRotation(0.0f, rotation[1], 0.0f);
    g_partner.chara->SetPosition(g_partner.pos);
    g_partner.chara->SetRotation(0.0f, rotation[1], 0.0f);
    std::printf("coop: P2 placed beside P1 (%s) at (%.0f %.0f %.0f)\n", reason, g_partner.pos[0], g_partner.pos[1], g_partner.pos[2]);
}

// Loads the partner's equipped weapon from commenu/weapon into its arena and weapon block, as
// LoadWeapon2 loads the player's into WeaponModelBuffer and block 0x1D, and puts it in the hand
// ("weapon" frame): Goro's hammer, Ruby's ring, Ungaga's staff, Osmond's gun. A member with no weapon
// equipped (one who has not joined) holds its character's first sound weapon (model 1). body is the model that
// holds it: the dungeon partner by default, the town partner in town (coop_town.cpp).
void LoadWeapon(int chara, CCharacter *body) {
    static const int first[6] = {ITEM_WEAPON_DAGGER_BROKEN,     ITEM_WEAPON_WOODENSLINGSHOT_BROKEN, ITEM_WEAPON_MALLET_BROKEN,
                                 ITEM_WEAPON_GOLD_RING_BROKEN, ITEM_WEAPON_FIGHTING_STICK_BROKEN,  ITEM_WEAPON_MACHINE_GUN_BROKEN};

    g_partner.weapon_loaded = false;

    // The weapon goes just above the body every time, in its own block, so a reload takes no more room.
    if (!Mutated("reload")) {
        g_partner.arena.used = g_partner.weapon_mark;
    }
    TexManager.DeleteTextureBlock(PartnerWeaponBlock());
    TexManager.CleanUpBuffer();
    TexManager.CleanUpTextureList();

    if (g_partner.weapon == nullptr) {
        g_partner.weapon = new CCharacter();
    }

    // In town UserStatus is unset; the save's dungeon status is the same party.
    CUserStatus *status = UserStatus != nullptr ? UserStatus : SaveData->GetDngStatus();
    int          model_no = 1;
    int          slot = status->equipped_weapon_slot[chara];

    if (chara < status->party_size && slot >= 0 && slot < 11) {
        int item_no = status->chara_weapons[chara][slot].item_no;

        if (item_no >= first[chara] && item_no - first[chara] < 64) {
            model_no = item_no - first[chara];
        }
    }

    char name[64];
    char cfg[64];
    char path[128];
    int  size = 0;

    BtGetWeaponNamePath2(name, cfg, chara, model_no);
    std::snprintf(path, sizeof path, "commenu/weapon/%s", name);

    if (LoadFile(path, g_partner.pack, &size) == 0 || size <= 0 || size > kPackBytes) {
        std::fprintf(stderr, "coop: cannot load %s\n", path);
        return;
    }

    CCharacter *weapon = g_partner.weapon;
    CCharacter *holder = body != nullptr ? body : g_partner.chara;
    CFrame     *hand = holder->frame->SearchFrame((char *) "weapon");

    weapon->Initialize();
    weapon->LoadPackData3(g_partner.pack, cfg, &g_partner.arena, PartnerWeaponBlock(), &g_partner.arena, 1, 0);

    if (weapon->frame == nullptr || hand == nullptr) {
        std::fprintf(stderr, "coop: %s cannot hold %s\n", charaNameTbl[chara], path);
        return;
    }

    weapon->SetPosition(0.0f, 0.0f, 0.0f);
    weapon->SetRotation(0.0f, 0.0f, 0.0f);
    weapon->frame->SetReference(hand);
    weapon->Step();
    g_partner.weapon_loaded = true;
    std::printf("coop: partner weapon %s (slot %d), arena %d\n", path, slot, g_partner.arena.used);
}

// Puts the weapon pad 2's member has equipped now in its hand, in place of the one it held: the arena
// rewinds to just above the body and block 0x41 is reused, so it may be called on any tick, as often as
// wanted (combat calls it when pad 2 equips). Loads a file and reads only party status, so linked
// copies agree. False when no partner is loaded or the weapon could not be.
bool ReloadWeapon() {
    if (g_partner.chara == nullptr || g_partner.chara->frame == nullptr || g_partner.loaded_chara < 0) {
        return false;
    }

    LoadWeapon(g_partner.loaded_chara);
    return g_partner.weapon_loaded;
}

namespace {

// "P2St" in a weapon's unused tail (WEAPON_HAVE::unk_F4): the starting weapon co-op gave a stand-in.
constexpr char kStandInMark[4] = {'P', '2', 'S', 't'};

bool StandInMarked(const WEAPON_HAVE &weapon) {
    return std::memcmp(weapon.unk_F4, kStandInMark, sizeof kStandInMark) == 0;
}

// The starting weapon, or its broken model one below it.
bool IsStarter(int chara, int item) {
    int starter = StarterWeapon(chara);
    return starter > 0 && (item == starter || item == starter - 1);
}

} // namespace

// A stand-in P2 that has not joined (Xiao on a fresh save) gets its character's starting weapon in the
// save, equipped, as joining the party gives it: it gains ABS, wears and shows in P2's HUD as a
// member's does (Jep, 2026-10-20). The weapon is marked, so the story's own grant when the member
// joins is undone (StandInWeaponStep), and the mark comes off at the first partner load after the
// join. Party status only, at a partner load, so linked copies agree. DC_COOP_MUTATE=standin_weapon: none.
void GrantStandInWeapon(int chara) {
    CDngStatusData *status = (CDngStatusData *) UserStatus;

    for (int c = 1; c < 6; c++) {
        if (c < status->party_size) {
            for (WEAPON_HAVE &weapon : status->chara_weapons[c]) {
                if (StandInMarked(weapon)) {
                    std::memset(weapon.unk_F4, 0, sizeof weapon.unk_F4);
                }
            }
        }
    }

    // Xiao alone is a stand-in (Pickable), so only her slingshot is given; never Goro's mallet or a later
    // member's weapon.
    if (chara <= 0 || chara >= 6 || (chara != CHARA_XIAO && !Mutated("standin_any")) || chara < status->party_size || EquippedWeapon(chara) != nullptr || Mutated("standin_weapon")) {
        return;
    }

    auto find = [&] {
        for (int slot = 0; slot < 10; slot++) {
            if (IsStarter(chara, status->chara_weapons[chara][slot].item_no)) {
                return slot;
            }
        }

        return -1;
    };

    int slot = find();

    if (slot < 0) {
        int overflow = status->overflow_flag;

        status->GetItem(StarterWeapon(chara), 0);
        status->overflow_flag = overflow;
        slot = find();
    }

    if (slot < 0) {
        return;
    }

    std::memcpy(status->chara_weapons[chara][slot].unk_F4, kStandInMark, sizeof kStandInMark);
    status->equipped_weapon_slot[chara] = (s8) slot;
    std::printf("coop: stand-in %d gets its starting weapon %d in the save (slot %d)\n", chara, status->chara_weapons[chara][slot].item_no,
                slot);
}

// The story's grant of a starting weapon to a member co-op already gave it to (GrantStandInWeapon):
// the new copy, which carries no mark, goes again and the marked one, hers from the start with its
// levels, ABS and wear, stays (equipped where the copy was; Jep, 2026-10-25: never remove the one she
// had from the beginning). The game rebuilds a weapon from its data, mark and all, when it breaks, is
// repaired or builds up (WepDataListToHaveCopy, weapon_buildup), so while the member is still a
// stand-in the weapon she holds is marked again each tick (DC_COOP_MUTATE=standin_remark: not, and a
// wiped mark leaves both copies at the join). Each tick, in town and in the dungeon. Party status
// only, so linked copies agree.
void StandInWeaponStep() {
    if (!Enabled() || UserStatus == nullptr) {
        return;
    }

    CDngStatusData *status = (CDngStatusData *) UserStatus;

    // A stand-in (not in the party) holds weapons only because co-op gave them: if none is marked any
    // more, the one she holds (equipped, or else her starting weapon in either model) is marked again.
    // Only when none is: a copy the story gives before the party takes her stays unmarked.
    for (int c = 1; c < 6 && !Mutated("standin_remark"); c++) {
        WEAPON_HAVE *weapons = status->chara_weapons[c];
        int          hold = -1;

        if (c < status->party_size) {
            continue;
        }

        for (int slot = 0; slot < 10; slot++) {
            if (weapons[slot].item_no > 0 && StandInMarked(weapons[slot])) {
                hold = -2;
                break;
            }

            if (hold == -1 && IsStarter(c, weapons[slot].item_no)) {
                hold = slot;
            }
        }

        int equipped = status->equipped_weapon_slot[c];

        if (hold != -2 && equipped >= 0 && equipped < 10 && weapons[equipped].item_no > 0) {
            hold = equipped;
        }

        if (hold >= 0) {
            std::memcpy(weapons[hold].unk_F4, kStandInMark, sizeof kStandInMark);
            std::printf("coop: stand-in %d's weapon %d (slot %d) marked again\n", c, weapons[hold].item_no, hold);
        }
    }

    // Test hooks: DC_COOP_STANDIN_WIPE=1, the game rebuilding the stand-in's weapon from its data, as a
    // repair does (its mark goes), then four levels gained on it (a level-up keeps the mark); once.
    // DC_COOP_STORY_JOIN=1: the story's join of the next member, a stand-in co-op armed, as its event
    // does it: the starting weapon again, and the party takes the member (with the wipe, on a later tick).
    static bool wiped = false;
    const bool  wiped_before = wiped;

    if (!wiped && TestEnvOn("DC_COOP_STANDIN_WIPE")) {
        int c = status->party_size;

        for (int slot = 0; c >= 1 && c < 6 && slot < 10; slot++) {
            WEAPON_HAVE &weapon = status->chara_weapons[c][slot];

            if (StandInMarked(weapon)) {
                WepDataListToHaveCopy(weapon.item_no, &weapon);
                weapon.level = 4;
                wiped = true;
                std::printf("coop-debug: stand-in %d's weapon rebuilt (slot %d, mark gone), level %d\n", c, slot, weapon.level);
                break;
            }
        }
    }

    if (TestEnvOn("DC_COOP_STORY_JOIN") && (wiped_before || !TestEnvOn("DC_COOP_STANDIN_WIPE"))) {
        int c = status->party_size;

        if (c >= 1 && c < 6) {
            for (const WEAPON_HAVE &weapon : status->chara_weapons[c]) {
                if (weapon.item_no > 0) {
                    status->GetItem(StarterWeapon(c), 0);
                    status->SetPartySize((s8) (c + 1));
                    std::printf("coop-debug: story join of member %d (its starting weapon given again)\n", c);
                    break;
                }
            }
        }
    }

    if (Mutated("standin_dedupe")) {
        return;
    }

    for (int c = 1; c < 6; c++) {
        WEAPON_HAVE *weapons = status->chara_weapons[c];
        int          marked = -1;

        for (int slot = 0; slot < 10; slot++) {
            if (weapons[slot].item_no > 0 && StandInMarked(weapons[slot]) && (marked < 0 || IsStarter(c, weapons[slot].item_no))) {
                marked = slot;

                if (IsStarter(c, weapons[slot].item_no)) {
                    break;
                }
            }
        }

        if (marked < 0) {
            continue;
        }

        for (int slot = 0; slot < 10; slot++) {
            if (slot != marked && weapons[slot].item_no == StarterWeapon(c) && !StandInMarked(weapons[slot])) {
                if (status->equipped_weapon_slot[c] == slot) {
                    status->equipped_weapon_slot[c] = (s8) marked;
                }

                weapons[slot] = WEAPON_HAVE{};
                std::printf("coop: member %d got its starting weapon again (slot %d); co-op's (slot %d, level %d) stays\n", c, slot,
                            marked, weapons[marked].level);

                for (WEAPON_HAVE &weapon : status->chara_weapons[c]) {
                    std::memset(weapon.unk_F4, 0, sizeof weapon.unk_F4);
                }

                break;
            }
        }
    }
}

// coop-fx51b (tests; v-standin-weapon on a fixture): DC_COOP_TEST_STANDIN_REGRANT=1: once a state fixture is put back,
// the stand-in's co-op weapon comes out and GrantStandInWeapon gives it again, as a fresh party's partner load does. The
// fixture was taken with the grant in its save already, so without this a row starting from it (and its standin_weapon
// twin, no grant) would see the maker's grant, not its own. The host alone, before its checkpoint: a joiner takes it.
void StandInRegrantAfterLoad() {
    if (!TestEnvOn("DC_COOP_TEST_STANDIN_REGRANT") || UserStatus == nullptr || g_partner.loaded_chara <= 0) {
        return;
    }

    CDngStatusData *status = (CDngStatusData *) UserStatus;
    const int       chara = g_partner.loaded_chara;
    int             removed = 0;

    for (int slot = 0; slot < 10; slot++) {
        WEAPON_HAVE &weapon = status->chara_weapons[chara][slot];

        if (weapon.item_no > 0 && StandInMarked(weapon)) {
            if (status->equipped_weapon_slot[chara] == slot) {
                status->equipped_weapon_slot[chara] = -1;
            }
            std::memset(&weapon, 0, sizeof weapon);
            removed++;
        }
    }

    std::printf("coop: test: stand-in %d's co-op weapon taken out after the state load (%d), given again\n", chara, removed);
    GrantStandInWeapon(chara);
    ReloadWeapon();
}

// How many of its starting weapon (either model) a member holds, for the test log.
int StarterCopies(int chara) {
    int count = 0;

    for (int slot = 0; chara > 0 && chara < 6 && slot < 10; slot++) {
        count += IsStarter(chara, UserStatus->chara_weapons[chara][slot].item_no) ? 1 : 0;
    }

    return count;
}

// A new floor (another floor number, or GameInit emptied the partner's block): nobody arrives lying
// down. The down counts and the revive are dropped, a stand-in's pool is full again, and a member the
// party status still has at no life (the game refills it on the way out; this is the backstop) gets up
// with a quarter, as the timer would give it. CoopStep calls it each tick before PickPartner.
void NewFloorCheck() {
    if (Mutated("floor")) {
        return;
    }

    int key = selectMapNo * 256 + UserStatus->cur_floor;

    if (key == g_partner.floor && !(g_partner.chara != nullptr && BlockEmpty())) {
        return;
    }

    bool was_down = g_partner.down > 0;
    int  chara = g_partner.loaded_chara;

    g_partner.floor = key;
    g_partner.down = 0;
    g_partner.revive = 0;
    g_partner.invincible = 0;
    for (int &life : g_partner.stand_in_life) {
        life = -1;
    }

    for (int &left : g_partner.down_left) {
        left = 0;
    }

    if (chara < 0) {
        return;
    }

    if (OwnLife(chara) && UserStatus->hp[chara] <= 0) {
        UserStatus->AddNowLife(chara, (s16) std::max(1, UserStatus->max_hp[chara] / 4), 0.0f);
    }

    if (was_down && g_partner.chara != nullptr) {
        PlayMotion(0, 0);
    }

    std::printf("coop: floor %d, partner standing%s, life %d/%d\n", (int) UserStatus->cur_floor, was_down ? " (was down)" : "", Life(), MaxLife());
}

// Loads the partner's pack into its own arena and texture block, as LoadChara2 loads the player's
// into CharaModelBuffer and block 0x11, then its weapon, without Xiao's raised hand. A swap (another
// member on the same floor) puts the new one where the old one stood; otherwise it starts beside the
// player.
bool Load(int chara) {
    if (g_partner.chara == nullptr) {
        g_partner.chara = new CCharacter();
        g_partner.tex_anime = new CTexAnimeData[64];
        g_partner.pack = reinterpret_cast<u_int *>(new (std::align_val_t{64}) u_char[kPackBytes]);
        g_partner.model = new (std::align_val_t{64}) u_char[static_cast<size_t>(kModelQuads) * 16];
    }

    int size = 0;

    if (LoadFile(const_cast<char *>(charaNameTbl[chara]), g_partner.pack, &size) == 0 || size <= 0 || size > kPackBytes) {
        std::fprintf(stderr, "coop: cannot load %s\n", charaNameTbl[chara]);
        return false;
    }

    // An empty block means GameInit ran; either that or another floor number means a new floor.
    bool          floor_load = BlockEmpty();
    int           was_loaded = g_partner.loaded_chara;
    bool          swap = g_partner.loaded_chara >= 0 && g_partner.loaded_chara != chara && !floor_load &&
                         g_partner.load_floor == selectMapNo * 256 + UserStatus->cur_floor;
    sceVu0FVECTOR stood;
    sceVu0FVECTOR rotation = {0.0f, 0.0f, 0.0f, 0.0f};

    if (swap) {
        sceVu0CopyVector(stood, g_partner.pos);
        g_partner.chara->frame->GetRotation(rotation);
    }

    // What the load empties that is not a character's (c0..): a floor's texture in the partner's block (v-partner-block).
    for (const CTexture &texture : TexManager.textures) {
        if (texture.name[0] != 0 && (texture.block == PartnerTextureBlock() || texture.block == PartnerWeaponBlock()) &&
            std::strncmp(texture.name, "c0", 2) != 0) {
            std::printf("coop: the partner's load empties %s (block %d), not the partner's\n", texture.name, texture.block);
        }
    }

    TexManager.DeleteTextureBlock(PartnerTextureBlock());
    TexManager.DeleteTextureBlock(PartnerWeaponBlock());
    TexManager.CleanUpBuffer();
    TexManager.CleanUpTextureList();

    g_partner.arena.base = g_partner.model;
    g_partner.arena.buffer = g_partner.model;
    g_partner.arena.used = 0;
    g_partner.arena.limit = kModelQuads;

    CCharacter *partner = g_partner.chara;

    partner->Initialize();

    for (int i = 0; i < 64; i++) {
        g_partner.tex_anime[i].Initialize();
    }

    partner->InitializeTexAnime(g_partner.tex_anime, 64);
    partner->LoadPackData2(g_partner.pack, (char *) "base.cfg", &g_partner.arena, PartnerTextureBlock(), &g_partner.arena, 0);

    if (partner->frame == nullptr) {
        std::fprintf(stderr, "coop: %s has no model\n", charaNameTbl[chara]);
        return false;
    }

    CFrameAttr attr;

    attr.fog_enable = true;
    attr.clip_enable = false;
    attr.program_option = 0;
    partner->frame->SetAttr(attr, 1, 4);
    // Footsteps, by the ground under the partner (Walk). The motions' own sound events stay off: they are
    // the character's voice, from the bank of the character in control.
    partner->FootSoundEnable(CoopMutate("no_sfx") ? 0 : 1);
    partner->EventEnable(0);

    // Each member keeps its own down count; the one left goes on counting in StepDown.
    if (g_partner.loaded_chara != chara) {
        if (g_partner.loaded_chara >= 0) {
            g_partner.down_left[g_partner.loaded_chara] = g_partner.down;
        }

        g_partner.down = g_partner.down_left[chara];
        g_partner.down_left[chara] = 0;
        g_partner.revive = 0;
    }

    g_partner.load_floor = selectMapNo * 256 + UserStatus->cur_floor;
    g_partner.loaded_chara = chara;
    g_partner.ex_chara = -1;
    g_partner.busy = 0;
    g_partner.motion = -1;
    g_partner.attacking = false;

    for (Shot &shot : g_partner.shots) {
        shot.life = 0;
    }

    g_partner.weapon_mark = g_partner.arena.used;
    GrantStandInWeapon(chara);
    LoadWeapon(chara);

    if (swap) {
        sceVu0CopyVector(g_partner.pos, stood);
        g_partner.velocity[0] = g_partner.velocity[1] = g_partner.velocity[2] = 0.0f;
        partner->frame->SetPosition(g_partner.pos);
        partner->frame->SetRotation(0.0f, rotation[1], 0.0f);
        partner->SetPosition(g_partner.pos);
        partner->SetRotation(0.0f, rotation[1], 0.0f);
    } else if (g_partner.arrive_floor == g_partner.load_floor) {
        // coop-cutscenejoin: she joined his floor by his state: where she came onto it (CoopWorldsArriverStand), once.
        g_partner.arrive_floor = -100;
        g_partner.pos[0] = g_partner.arrive_pos[0];
        g_partner.pos[1] = g_partner.arrive_pos[1];
        g_partner.pos[2] = g_partner.arrive_pos[2];
        g_partner.pos[3] = 1.0f;
        g_partner.velocity[0] = g_partner.velocity[1] = g_partner.velocity[2] = 0.0f;
        partner->frame->SetPosition(g_partner.pos);
        partner->frame->SetRotation(0.0f, g_partner.arrive_yaw, 0.0f);
        partner->SetPosition(g_partner.pos);
        partner->SetRotation(0.0f, g_partner.arrive_yaw, 0.0f);
        std::fprintf(stderr, "coop: P2 placed where she arrived at (%.0f %.0f %.0f)\n", g_partner.pos[0], g_partner.pos[1], g_partner.pos[2]);
    } else {
        PlaceBesidePlayer(was_loaded < 0 ? "load: first" : floor_load ? "load: new floor" : "load: floor changed");
    }

    // A member with no life left in the party status comes in lying down.
    if (g_partner.down == 0 && OwnLife(chara) && UserStatus->hp[chara] <= 0) {
        g_partner.down = kDownTicks;
    }

    if (g_partner.down > 0) {
        PlayMotion(kDownMotion, 2);
    }

    int steps = 0;

    for (const CHARA_FOOT_SOUND &sound : partner->foot_sound) {
        steps += sound.frame >= 0;
    }

    std::printf("coop: partner %d (%s), %d quadwords, %d footsteps\n", chara, charaNameTbl[chara], g_partner.arena.used, steps);
    return true;
}

// coop-joinvisuals (Jep's 2026-10-27 run: P1 joined her floor and saw only her shadow): this copy's partner, loaded on its own
// floor before the join, kept its model while the state set brought her copy's texture table, whose partner entries named
// pixels at her copy's addresses. As the town does (CoopTownSnapshotTexturesOut / Applied): those entries out before the
// registry is made again, her model loaded again after it. DC_COOP_MUTATE=partner_stale_tex (twin): left as it was.
void PartnerSnapshotTexturesOut() {
    if (Mutated("partner_stale_tex") || g_partner.chara == nullptr) {
        return;
    }
    TexManager.DeleteTextureBlock(PartnerTextureBlock());
    TexManager.DeleteTextureBlock(PartnerWeaponBlock());
    TexManager.CleanUpTextureList();
}

// coop-keydoor (Jep's 2026-10-27 run: "Interacting with this bone key door breaks the use item hud for P1, who is the joiner";
// x-floor-join -BoneDoor showed his HUD's pictures gone from the merge on, door or no door: the hearts, water drops, quick-use
// cells, floor badge, weapon icon and both faces). Pictures entered from a file read into a load buffer or a buffer of the mod's
// own (not in the state set) came with the host's texture table in the join's state set: the re-key could not carry their pixels
// and made placeholders (blank pictures) of the ones this copy had not entered itself, and their loaders, finding the names in
// the table, never entered them again. They are:
//   itempack   the HUD's sheet (hearts, drops, cells, badge, frames), a fixed picture the dungeon's start enters from
//              itempack.img; a joiner that took the state set in its arrival never ran that start.
//   0x43       coop_hud's icon sheets (wepicon, itemicon) from itemlst.img.
//   0x44       coop_wheel's faces (quickchara and its pack's sheets) from commenu/quickchr.img.
// Each file is entered again here and each picture it enters moved onto the table's entry of its name (its keys), the new entries
// emptied and the manager's counters put back, so the table stays the host's (a picture loaded fresh at a key of this copy's own
// drew nothing on the joiner's HUD: its faces, tried that way first). The ring's effect (0x42) and the key picker's window (0x45)
// load lazily where their blocks are empty, so those blocks just go. The HUD's item cells are filled again once the icon sheet
// is back. DC_COOP_MUTATE=hudstale (twin): all left as the re-key leaves them.
namespace {

// Enters a file's pictures again (enter) and moves each onto the table's entry of the same name and block. The pictures moved.
template <typename Enter>
int ReloadAtTableKeys(int block, Enter enter) {
    bool                used[196];
    const CTextureBlock kept = block >= 0 && block < 72 ? TexManager.blocks[block] : CTextureBlock{};
    const int           fix = TexManager.vram_fix;
    const int           buffer_used = TexManager.buffer_used;
    const int           texture_max = TexManager.texture_max;
    int                 moved = 0;

    for (int i = 0; i < 196; i++) {
        used[i] = TexManager.textures[i].name[0] != 0;
    }

    enter();

    for (int i = 0; i < 196; i++) {
        CTexture &fresh = TexManager.textures[i];

        if (used[i] || fresh.name[0] == 0) {
            continue;
        }

        const sceGsTex0 keys = *reinterpret_cast<const sceGsTex0 *>(&fresh.tex0);
        bool            done = false;

        for (int j = 0; j < 196 && !done; j++) {
            const CTexture &old = TexManager.textures[j];

            if (used[j] && old.block == fresh.block && std::strcmp(old.name, fresh.name) == 0) {
                const sceGsTex0 &at = *reinterpret_cast<const sceGsTex0 *>(&old.tex0);

                done = dc::render::PortMoveKeys(keys.TBP0, at.TBP0, keys.CBP, at.CBP);
            }
        }

        if (!done) {
            dc::render::PortReleaseKey(keys.TBP0);
            if (keys.CBP != 0) {
                dc::render::PortReleaseKey(keys.CBP);
            }
        }
        moved += done ? 1 : 0;
        fresh.Initialize();
    }

    if (block >= 0 && block < 72) {
        TexManager.blocks[block] = kept;
    }
    TexManager.vram_fix = fix;
    TexManager.buffer_used = buffer_used;
    TexManager.texture_max = texture_max;
    return moved;
}

bool TableHasBlock(int block) {
    for (const CTexture &texture : TexManager.textures) {
        if (texture.name[0] != 0 && texture.block == block) {
            return true;
        }
    }

    return false;
}

} // namespace

void DrawBlocksSnapshotApplied() {
    if (Mutated("hudstale")) {
        return;
    }

    TexManager.DeleteTextureBlock(0x42);
    TexManager.DeleteTextureBlock(0x45);
    CoopHudIconsAgain();

    static std::vector<u_long128> file;
    // (64-byte aligned, as the loaders' own buffers)
    auto data = [] { return reinterpret_cast<u_char *>(file.data()) + (64 - reinterpret_cast<std::uintptr_t>(file.data()) % 64) % 64; };

    file.assign((512 * 1024 + 64) / sizeof(u_long128), u_long128{});

    int  size = 0;
    char itempack[] = "itempack.img";
    char icons[] = "itemlst.img";

    if (mode == GAME_MODE_DUNGEON && TexManager.GetTexture((char *) "itempack", -1) != nullptr &&
        (size = LoadFileMenuData(itempack, reinterpret_cast<unsigned int *>(data()))) > 0) {
        const int moved = ReloadAtTableKeys(-1, [&] { TexManager.EnterIMGFile(data(), -1, 0, 0); });
        std::fprintf(stderr, "coop: after the state set: the HUD sheet (itempack.img) entered again, %d picture(s) at the table's keys\n", moved);
    }

    if (TableHasBlock(0x43) && (size = LoadFileMenuData(icons, reinterpret_cast<unsigned int *>(data()))) > 0) {
        LOADTEXTURE_INFO2 table[2] = {{(char *) data(), 0x43, 0}, {nullptr, 0, 0}};
        const int         moved = ReloadAtTableKeys(0x43, [&] { TexManager.LoadTextureBlockEX(0x43, table); });
        HudIconSheetEntered();
        std::fprintf(stderr, "coop: after the state set: the HUD's icon sheets (itemlst.img) entered again, %d picture(s) at the table's keys\n", moved);
    }

    if (TableHasBlock(kWheelTextureBlock) && LoadFile((char *) "commenu/quickchr.img", data(), &size) != 0 && size > 0) {
        LOADTEXTURE_INFO2 table[2] = {{(char *) data(), kWheelTextureBlock, 0}, {nullptr, 0, 0}};
        const int         moved = ReloadAtTableKeys(kWheelTextureBlock, [&] { TexManager.LoadTextureBlockEX(kWheelTextureBlock, table); });
        std::fprintf(stderr, "coop: after the state set: the faces (quickchr.img) entered again, %d picture(s) at the table's keys\n", moved);
    }
}

void PartnerSnapshotApplied() {
    const int chara = g_partner.loaded_chara;

    // Test hook (DC_COOP_TEST_NO_PARTNER_BODY=1; coop-stayerhello, x-floor-join-reverse-nobody): at its first state set this
    // copy has no partner body of its own, as his copy alone on her floor had (the old body is let go, not freed: a test's).
    if (static bool done = false; !done && TestEnvOn("DC_COOP_TEST_NO_PARTNER_BODY") && g_partner.chara != nullptr) {
        done = true;
        g_partner.chara = nullptr;
        g_partner.tex_anime = nullptr;
        g_partner.pack = nullptr;
        g_partner.model = nullptr;
        std::printf("coop: test: no partner body here at the state set\n");
    }

    // coop-stayerhello (x-floor-join-reverse-start, OUT OF SYNC at the lockstep's first checksum): a copy with no partner body
    // of its own yet (his copy on the floor alone, her state taken) loads it here too. Left to its next frame's load, the body
    // went beside the player ("load: floor changed"), not where the state set has her. DC_COOP_MUTATE=partner_unloaded_skip
    // (twin): skipped, as before.
    if (Mutated("partner_stale_tex") || (g_partner.chara == nullptr && Mutated("partner_unloaded_skip")) || chara < 0 || chara > 5 ||
        UserStatus == nullptr) {
        return;
    }

    // The state set's fields are the pair's (the lockstep reads them): only the model, its arenas and what they hold are this
    // copy's own. The load's placing, motion and counters go back to the state set's.
    const Partner kept = g_partner;
    sceVu0FVECTOR facing = {0.0f, 0.0f, 0.0f, 0.0f};

    if (g_partner.chara != nullptr && g_partner.chara->frame != nullptr) {
        g_partner.chara->frame->GetRotation(facing);
    }
    std::printf("coop: partner %d loaded again for the floor's state set%s\n", chara, g_partner.chara == nullptr ? " (no body here before)" : "");
    std::fflush(stdout);
    if (!Load(chara)) {
        g_partner.loaded_chara = -1;
        return;
    }

    Partner &now = g_partner;
    Partner  fresh = now;

    now = kept;
    now.chara = fresh.chara;
    now.tex_anime = fresh.tex_anime;
    now.arena = fresh.arena;
    now.pack = fresh.pack;
    now.model = fresh.model;
    now.weapon = fresh.weapon;
    now.weapon_loaded = fresh.weapon_loaded;
    now.weapon_mark = fresh.weapon_mark;
    now.ex_pack = fresh.ex_pack;
    now.ex_model = fresh.ex_model;
    now.ex_arena = fresh.ex_arena;
    now.ex_chara = fresh.ex_chara; // (the chest motions went with the old model)

    // coop-p1follows (w-town-apart -P1Follows, OUT OF SYNC at the merge's first checksum): the body goes on from the sender's
    // motion clock and facing. Restarted at its first frame, a swing under way at the take reached its blow (StepAttack: 0.4
    // of the motion's length) ticks later here: her Xiao's shot and its weapon wear on her copy only; faced as this copy's old
    // body faced, her shot flew another way (Shoot reads the body's yaw). DC_COOP_MUTATE=partner_motion_restart (twin): this
    // copy's own facing and a restart, as before.
    const Partner::BodyKept &m = kept.body_kept;
    const bool               go_on = m.set && m.no >= 0 && !Mutated("partner_motion_restart");
    const float              yaw = go_on ? m.yaw : facing[1];
    CCharacter              *body = now.chara;

    body->frame->SetPosition(now.pos);
    body->frame->SetRotation(0.0f, yaw, 0.0f);
    body->SetPosition(now.pos);
    body->SetRotation(0.0f, yaw, 0.0f);

    if (go_on) {
        MOTION_STATE &state = body->motion_type.state;

        body->motion_no = m.no;
        body->motion_flags = m.flags;
        body->motion_speed = m.speed;
        state.time = m.time;
        state.blend = m.blend;
        state.blend_step = m.blend_step;
        state.frame = m.frame;
        state.next_frame = m.next_frame;
        state.motion_no = m.asked;
        state.playing_no = m.playing;
        state.blending = m.blending;
        std::printf("coop: partner motion %d goes on at time %.3f yaw %.3f (the state set's)\n", m.no, m.time, yaw);
    } else if (kept.motion >= 0) {
        PlayMotion(kept.motion, 0);
        std::printf("coop: partner motion %d starts again at time %.3f yaw %.3f\n", kept.motion, body->motion_type.state.time, yaw);
    }
    std::fflush(stdout);
}

// coop-p1follows: as the state set is taken on a floor, the partner body's motion clock and facing into the set
// (PartnerSnapshotApplied).
void PartnerMotionTake() {
    Partner::BodyKept &m = g_partner.body_kept;

    m = Partner::BodyKept{};
    if (g_partner.chara == nullptr || g_partner.loaded_chara < 0) {
        return;
    }

    const CCharacter   &body = *g_partner.chara;
    const MOTION_STATE &state = body.motion_type.state;
    sceVu0FVECTOR       rotation = {0.0f, 0.0f, 0.0f, 0.0f};

    body.frame->GetRotation(rotation);
    m.set = true;
    m.yaw = rotation[1];
    m.no = body.motion_no;
    m.flags = body.motion_flags;
    m.speed = body.motion_speed;
    m.time = state.time;
    m.blend = state.blend;
    m.blend_step = state.blend_step;
    m.frame = state.frame;
    m.next_frame = state.next_frame;
    m.asked = state.motion_no;
    m.playing = state.playing_no;
    m.blending = state.blending;
    std::printf("coop: partner motion %d taken at time %.3f yaw %.3f\n", m.no, m.time, m.yaw);
    std::fflush(stdout);
}

// coop-joinvisuals: her member drawn in the hidden player's place through her floor's entrance (CoopWorldsEntryStandIn), on
// player 2's copy apart: a model of its own (its own arena and texture block, nothing of the partner's or the state's),
// standing where the game draws its player (a scene's node carries him in the cave) and playing what he plays when her
// model has that motion, else standing. Loaded once a floor; its textures out once the entrance is over.
namespace {

constexpr int kStandInBlock = 0x3C; // (no other user on a floor: the partner's are 0x34 and 0x35, the town's villagers 0x36 on)

struct StandIn {
    CCharacter    *body = nullptr;
    CTexAnimeData *tex_anime = nullptr;
    u_int         *pack = nullptr;
    u_char        *model = nullptr;
    CDataAlloc2<1> arena;
    int            chara = -1;
    int            floor = -1000;
} g_stand_in;

bool StandInReady(int chara) {
    const int floor = selectMapNo * 256 + UserStatus->cur_floor;
    bool      block_there = false;

    for (const CTexture &texture : TexManager.textures) {
        block_there = block_there || (texture.name[0] != 0 && texture.block == kStandInBlock);
    }
    if (g_stand_in.chara == chara && g_stand_in.floor == floor && block_there) {
        return true;
    }
    if (g_stand_in.body == nullptr) {
        g_stand_in.body = new CCharacter();
        g_stand_in.tex_anime = new CTexAnimeData[64];
        g_stand_in.pack = reinterpret_cast<u_int *>(new (std::align_val_t{64}) u_char[kPackBytes]);
        g_stand_in.model = new (std::align_val_t{64}) u_char[static_cast<size_t>(kModelQuads) * 16];
    }
    g_stand_in.chara = -1;

    int size = 0;

    if (LoadFile(const_cast<char *>(charaNameTbl[chara]), g_stand_in.pack, &size) == 0 || size <= 0 || size > kPackBytes) {
        return false;
    }
    TexManager.DeleteTextureBlock(kStandInBlock);
    TexManager.CleanUpBuffer();
    TexManager.CleanUpTextureList();
    g_stand_in.arena.base = g_stand_in.model;
    g_stand_in.arena.buffer = g_stand_in.model;
    g_stand_in.arena.used = 0;
    g_stand_in.arena.limit = kModelQuads;

    CCharacter *body = g_stand_in.body;

    body->Initialize();
    for (int i = 0; i < 64; i++) {
        g_stand_in.tex_anime[i].Initialize();
    }
    body->InitializeTexAnime(g_stand_in.tex_anime, 64);
    body->LoadPackData2(g_stand_in.pack, (char *) "base.cfg", &g_stand_in.arena, kStandInBlock, &g_stand_in.arena, 0);
    if (body->frame == nullptr) {
        return false;
    }

    CFrameAttr attr;

    attr.fog_enable = true;
    attr.clip_enable = false;
    attr.program_option = 0;
    body->frame->SetAttr(attr, 1, 4);
    body->FootSoundEnable(0);
    body->EventEnable(0);
    g_stand_in.chara = chara;
    g_stand_in.floor = floor;
    std::printf("coop: her member %d (%s) loaded to stand in for the hidden player through the floor's entrance\n", chara, charaNameTbl[chara]);
    return true;
}

// coop-entryshadow test measure: the stand-in's body and shadow as posed this frame, each frame's world origin in its tree
// (CFrame::GetLWMatrix), the widest flat reach from the body's place (a bind T-pose's hands reach out, a walk's stay in),
// and the mean gap between the frames of one name in both trees. The shadow model's frames are its own, so a frame only
// in one tree counts in its reach, not in the gap.
EntryStandInPoseInfo g_stand_in_pose;
int                  g_stand_in_posed = 0; // poses since the start

struct PoseNode {
    std::string name;
    float       x, y, z;
};

void PoseFrames(CFrame *frame, std::vector<PoseNode> &out) {
    for (; frame != nullptr; frame = frame->brother) {
        sceVu0FMATRIX world;

        frame->GetLWMatrix(world);
        out.push_back({frame->name, world[3][0], world[3][1], world[3][2]});
        PoseFrames(frame->child, out);
    }
}

void StandInPoseMeasure(CCharacter *body, const sceVu0FVECTOR place) {
    static const bool on = TestEnv("DC_WORLDS_TEST_ENTRY_SHOT") != nullptr;

    if (!on || body->frame == nullptr) {
        return;
    }

    std::vector<PoseNode> bones;
    std::vector<PoseNode> shadow;

    PoseFrames(body->frame, bones);
    if (body->shadow_frame != nullptr) {
        PoseFrames(body->shadow_frame, shadow);
    }

    const auto reach = [&](const std::vector<PoseNode> &nodes) {
        float most = 0.0f;

        for (const auto &node : nodes) {
            most = std::max(most, std::hypot(node.x - place[0], node.z - place[2]));
        }
        return most;
    };

    EntryStandInPoseInfo info;
    float                gaps = 0.0f;

    info.frame = ++g_stand_in_posed;
    info.motion = body->motion_no;
    info.time = body->motion_type.state.time;
    info.body_reach = reach(bones);
    info.shadow_reach = reach(shadow);
    info.shadow_frames = static_cast<int>(shadow.size());
    for (const auto &node : shadow) {
        for (const auto &bone : bones) {
            if (node.name == bone.name) {
                gaps += std::hypot(node.x - bone.x, node.y - bone.y, node.z - bone.z);
                info.matched++;
                break;
            }
        }
    }
    info.gap = info.matched > 0 ? gaps / static_cast<float>(info.matched) : -1.0f;
    g_stand_in_pose = info;
}

// Posed where the game draws its player, this frame's motion.
CCharacter *StandInPosed() {
    const int chara = CoopWorldsEntryStandIn();

    // (loaded and put away on the tick, EntryStandInTick: a texture table cleaned up inside a draw left that frame black)
    if (chara < 0 || chara != g_stand_in.chara || UserStatus == nullptr || g_stand_in.floor != selectMapNo * 256 + UserStatus->cur_floor) {
        return nullptr;
    }

    CCharacter   *body = g_stand_in.body;
    sceVu0FVECTOR place;
    float         axis_x[2];
    float         axis_z[2];

    PlayerDrawnPlace(place, axis_x, axis_z);

    const float yaw = atan2f(axis_z[0], axis_z[1]);

    body->frame->SetPosition(place);
    body->frame->SetRotation(0.0f, yaw, 0.0f);
    body->SetPosition(place);
    body->SetRotation(0.0f, yaw, 0.0f);

    const MOTION_INFO &info = body->motion_type.motion_info[CharaMain.motion_no];
    const int          motion = CharaMain.motion_no >= 0 && info.end > info.start && info.start >= 0 ? CharaMain.motion_no : 0;

    if (body->motion_no != motion) {
        body->motion_no = motion;
        body->motion_flags = motion == CharaMain.motion_no ? CharaMain.motion_flags : 0;
        body->motion_speed = -1.0f;
    }
    body->Step();
    // coop-entryshadow (Jep's 2026-10-27 run: "her shadow is T-posing" in her entrance): the shadow is a model of its own
    // with its own motions, set to the body's motion and frame by ShadowStep, as the partner's and the town body's are
    // each tick. Never stepped, it stood in its bind pose. DC_COOP_MUTATE=entry_shadow_bind (twin): not stepped, as before.
    if (!Mutated("entry_shadow_bind")) {
        body->ShadowStep();
    }
    StandInPoseMeasure(body, place);
    return body;
}

} // namespace

bool EntryStandInPose(EntryStandInPoseInfo *out) {
    if (g_stand_in_pose.frame < 0) {
        return false;
    }
    *out = g_stand_in_pose;
    return true;
}

void EntryStandInTick() {
    const int chara = CoopWorldsEntryStandIn();

    if (chara >= 0 && UserStatus != nullptr && CoopWorldsHereFloor() != -1000) { // (on a floor)
        StandInReady(chara);
    } else if (g_stand_in.chara >= 0) {
        g_stand_in.chara = -1;
        TexManager.DeleteTextureBlock(kStandInBlock);
        TexManager.CleanUpTextureList();
    }
}

} // namespace coop

using namespace coop;

// coop-nopush: the partner's body as a wall in the player's walk (port/src/dun/movechara.cpp's DunMoveChara, before each MoveCheck), as
// EdMoveChara walls a villager's: he stops at her. Only while he moves: standing still, the wall's MoveCheck would step
// him off her as she walks up (she stops at him in Walk). Her place is simulation state, the same on linked copies.
// DC_COOP_MUTATE=push_through (twin): no wall, and she steps aside as he walks into her.
int CoopPartnerWall(float *pos, const float *velocity, CCPoly *out) {
    if (!Enabled() || !g_partner.visible || g_partner.chara == nullptr || Mutated("push_through") || (velocity[0] == 0.0f && velocity[2] == 0.0f)) {
        return 0;
    }

    return g_partner.chara->PickUpPoly(pos, out);
}

// coop-nopush test hook (DC_COOP_P1_WALK_INTO=K): from the Kth walk of the player's with the partner on the floor, for 360
// walks, his stick (as his walk reads it, on both copies alike) points him at her, as a walk into her by hand (after a second's walk away from her). Every 30
// steps (stderr): his place and hers, the flat distance, the nearest he has come, and her body's width.
void CoopP1WalkIntoForTest(const float *pos, float angle, float *lx, float *ly) {
    static const int at = TestEnvInt("DC_COOP_P1_WALK_INTO", -1);
    static int       walks = 0;
    static float     nearest = 0.0f;

    if (at < 0 || !g_partner.visible || g_partner.chara == nullptr) {
        return;
    }

    const int steps = walks++ - at;

    if (steps < 0 || steps >= 360) {
        return;
    }

    const float dx = g_partner.pos[0] - pos[0];
    const float dz = g_partner.pos[2] - pos[2];
    const float flat = std::hypot(dx, dz);

    nearest = steps <= 60 ? flat : std::min(nearest, flat);
    if (steps % 30 == 0) {
        std::fprintf(stderr, "coop: test: P1 walks into her at step %d: he at (%.1f %.1f %.1f), she at (%.1f %.1f %.1f), %.1f flat, nearest %.1f, her width %.1f\n", steps,
                     pos[0], pos[1], pos[2], g_partner.pos[0], g_partner.pos[1], g_partner.pos[2], flat, nearest, g_partner.chara->body_width);
    }

    if (flat < 0.5f) {
        *lx = *ly = 0.0f;
        return;
    }

    const float away = steps < 60 ? -1.0f : 1.0f; // first a second's walk away from her, for a run-up
    const float mx = away * dx / flat;
    const float mz = away * dz / flat;

    *lx = mx * cosf(angle) - mz * sinf(angle);
    *ly = mx * sinf(angle) + mz * cosf(angle);
}

bool coop::OtherBeyondClip() {
    static bool far_now = false;

    if (mode != GAME_MODE_DUNGEON || !NetplayHasPlayer() || !g_partner.visible || Mutated("farplayer_drawn")) {
        far_now = false;
        return false;
    }

    const bool   mine_is_partner = ActorLocalIndex() == 1;
    const float *mine = mine_is_partner ? g_partner.pos : CharaMain.pos;
    const float *other = mine_is_partner ? CharaMain.pos : g_partner.pos;
    const float  away = DistVector(const_cast<float *>(mine), const_cast<float *>(other));
    const bool   was = far_now;

    far_now = far_now ? away >= 300.0f : away > 310.0f; // (retail: awake inside 300, asleep past 310)
    if (far_now != was) {
        std::fprintf(stderr, "coop-farplayer: the other player is %s at %.0f from this copy's own, link tick %lld\n", far_now ? "hidden" : "drawn", away, (long long) NetplayLinkTick());
        std::fflush(stderr);
    }
    return far_now;
}

void CoopDrawShadow() {
    // coop-joinvisuals: her member in the hidden player's place, its shadow and body (the partner's draw is off apart).
    if (CCharacter *body = StandInPosed(); body != nullptr) {
        if (BtActStatus.shadow_visible != 0) {
            float light[4][4];
            float colour[4][4];
            float saved[4][4];

            MGGetPLight(light, colour);
            sceVu0CopyMatrix(saved, light);
            light[0][0] *= 0.2f;
            light[2][0] *= 0.2f;
            MGSetPLight(light, colour);
            TexManager.ReloadTexture(Vif1Packet, 15);
            MGBeginDrawShadow(*(sceGsTex0 *) &TexManager.GetTexture((char *) "shadow_buf", -1)->tex0);
            body->DrawShadow();
            MGEndDrawShadow(0x40);
            sceVu0CopyMatrix(light, saved);
            MGSetPLight(light, colour);
        }
        TexManager.ReloadTexture(Vif1Packet, kStandInBlock);
        body->TextureAnime(kStandInBlock);
        body->Draw();
    }

    CoopPerfScope perf(kCoopPerfDraw); // coop: perf
    if (!g_partner.visible || BtActStatus.shadow_visible == 0 || (coop::ActorLocalIndex() == 0 && coop::OtherBeyondClip())) { // coop-pt1109b: past the draw distance
        return;
    }

    float light[4][4];
    float colour[4][4];
    float saved[4][4];

    MGGetPLight(light, colour);
    sceVu0CopyMatrix(saved, light);
    light[0][0] *= 0.2f;
    light[2][0] *= 0.2f;
    MGSetPLight(light, colour);
    TexManager.ReloadTexture(Vif1Packet, 15);
    MGBeginDrawShadow(*(sceGsTex0 *) &TexManager.GetTexture((char *) "shadow_buf", -1)->tex0);
    g_partner.chara->DrawShadow();
    MGEndDrawShadow(0x40);
    sceVu0CopyMatrix(light, saved);
    MGSetPLight(light, colour);
}

void CoopDraw() {
    CoopPerfScope perf(kCoopPerfDraw); // coop: perf
    if (!g_partner.visible) {
        return;
    }

    // coop-pt1109b: his copy draws her only inside the distance retail draws a monster at (OtherBeyondClip)
    // coop-cand56 (the merge's x-chest-seen/x-atla-open reds; Jep 11-09: the other sees the world half of an opening): only her body
    // goes; her chest's or Atla's box, glow and item are the world's and still draw. DC_COOP_MUTATE=farplayer_hides_world (twin): all of it went.
    if (coop::ActorLocalIndex() == 0 && coop::OtherBeyondClip()) {
        if (!Mutated("farplayer_hides_world")) {
            DrawPartnerPresentation(); // coop-openercam
        }
        return;
    }

    // The partner blinks while it cannot be hit.
    if (g_partner.down == 0 && g_partner.invincible > 0 && (g_partner.invincible & 4) != 0) {
        return;
    }

    // coop-hudicon (Jep's 2026-10-27 playtest: Xiao gets no stamina glow, Toan does): her member's ailment tint, as
    // Draw_MainUnit gives the player's (BtStatusErrColorSet, its stamina orange among them), from her own ailments.
    // Draw only: the save's cur_chara is hers for the one call. DC_COOP_MUTATE=partnerglow: no tint, as before.
    float saved_ambient[4];
    float ambient[4];
    bool  tinted = false;

    MGGetAmbient(saved_ambient);
    MGGetAmbient(ambient);

    // coop-pt1109b (Jep's 2026-11-09 playtest: no glow on her with Dran's Feather): Draw_MainUnit's speed boost, the model pulsing blue
    // while her Square runs the feather (SpeedBoosted); her ailment tint below overrides it, as retail's status colour does.
    // DC_COOP_MUTATE=featherglow_off (twin): no pulse, as before.
    bool boosted = false;

    if (BtEventMode == 0 && BtActStatus.in_presentation == 0 && SpeedBoosted() && !Mutated("featherglow_off")) {
        static float rate = 0.0f;

        if (rate >= 3.14159274f) {
            rate -= 3.14159274f;
        } else {
            rate += 0.09817477f;
        }

        ambient[0] = 96.0f;
        ambient[1] = 96.0f;
        ambient[2] = 96.0f + 96.0f * sinf(rate);
        MGSetAmbient(ambient);
        boosted = true;
    }

    if (BtEventMode == 0 && BtActStatus.in_presentation == 0 && g_partner.loaded_chara >= 0 && !Mutated("partnerglow")) {
        s8 cur_chara = UserStatus->cur_chara;

        UserStatus->cur_chara = (s8) g_partner.loaded_chara;
        tinted = BtStatusErrColorSet() != 0;
        UserStatus->cur_chara = cur_chara;

        if (tinted) {
            ambient[0] = StatusColor[0];
            ambient[1] = StatusColor[1];
            ambient[2] = StatusColor[2];
            MGSetAmbient(ambient);
        }
    }

    static int log_wait = 0;

    // The test's report (DC_COOP_GLOW): the tint, and where her feet and her pos + 12 come out on this copy's screen (the
    // 2D drawing's 640-wide coordinates, as CoopDrawWheel works them out), for its frame check.
    if (TestEnv("DC_COOP_GLOW") != nullptr && --log_wait <= 0) {
        float feet[4] = {g_partner.pos[0], g_partner.pos[1], g_partner.pos[2], 1.0f};
        float head[4] = {g_partner.pos[0], g_partner.pos[1] + 12.0f, g_partner.pos[2], 1.0f};
        int   at_feet[4] = {};
        int   at_head[4] = {};
        bool  seen = MGRotTransPers(at_feet, feet, 0) != 0 && MGRotTransPers(at_head, head, 0) != 0;

        log_wait = 30;
        std::fprintf(stderr, "coop-glow: partner %d ailments %#x tint %s (%.0f %.0f %.0f), screen %d feet (%d %d) head (%d %d), copy of p%d\n",
                     g_partner.loaded_chara, g_partner.loaded_chara >= 0 ? (unsigned) UserStatus->ailments[g_partner.loaded_chara] : 0u,
                     tinted ? "on" : boosted ? "boost" : "off", ambient[0], ambient[1], ambient[2], seen ? 1 : 0, (at_feet[0] - 0x6C00) >> 4,
                     ((at_feet[1] - GS_Y_OFFSET) >> 4) * 2, (at_head[0] - 0x6C00) >> 4, ((at_head[1] - GS_Y_OFFSET) >> 4) * 2,
                     NetplayLocalPlayer() == 2 ? 2 : 1);
    }

    // coop-atlaopen: her Atla's light on her body, as retail's lights the opener (its "light01" point light).
    float pres_light[4][4];
    float pres_colour[4][4];
    float saved_light[4][4];
    float saved_colour[4][4];

    MGGetPLight(pres_light, pres_colour);
    sceVu0CopyMatrix(saved_light, pres_light);
    sceVu0CopyMatrix(saved_colour, pres_colour);

    const bool lit = PartnerPresentationLight(g_partner.chara->frame->position, pres_light, pres_colour);

    if (lit) {
        MGSetPLight(pres_light, pres_colour);
    }

    TexManager.ReloadTexture(Vif1Packet, PartnerTextureBlock());
    g_partner.chara->TextureAnime(PartnerTextureBlock());
    g_partner.chara->Draw();

    if (lit) {
        MGSetPLight(saved_light, saved_colour);
    }

    // The weapon follows the hand the body's Step just posed.
    if (g_partner.weapon_loaded) {
        g_partner.weapon->Step();
        TexManager.ReloadTexture(Vif1Packet, PartnerWeaponBlock());
        g_partner.weapon->Draw();
    }

    MGSetAmbient(saved_ambient);
    DrawPartnerPresentation(); // coop-openercam
}

namespace coop {

int PartnerTextureBlock() {
    static const int block = Mutated("oldblock") ? 0x40 : 0x34;
    return block;
}

int PartnerWeaponBlock() {
    static const int block = Mutated("oldblock") ? 0x41 : 0x35;
    return block;
}

bool Mutated(const char *feature) {
    COOP_TEST_SWITCH(const char *, mutation, TestEnv("DC_COOP_MUTATE")); // (a job's own list on a warm pair)

    if (mutation == nullptr) {
        return false;
    }

    // coop-onher: a comma list turns several on (a twin of a bug two fixes now guard).
    const std::size_t length = std::strlen(feature);

    for (const char *at = mutation; *at != '\0';) {
        const char *end = std::strchr(at, ',');
        const std::size_t size = end != nullptr ? (std::size_t) (end - at) : std::strlen(at);

        if (size == length && std::strncmp(at, feature, length) == 0) {
            return true;
        }
        at += size + (end != nullptr ? 1 : 0);
    }

    return false;
}

} // namespace coop

namespace coop {

// Session reset (CoopResetForSession, at a resync): the partner as at the start of a session, so both
// copies, which restart from the host's save, pick and load it alike on the next tick. Its buffers
// are kept; everything it was doing is forgotten: no member loaded (the next CoopStep loads it again,
// beside the player), no pick (choice -1), nobody lying down or getting up, each stand-in's life full,
// no swing, shot, chest opening or swap button held. The wheel closes with WheelResetForSession.
void PartyResetForSession() {
    Partner fresh;

    fresh.chara = g_partner.chara;
    fresh.tex_anime = g_partner.tex_anime;
    fresh.pack = g_partner.pack;
    fresh.model = g_partner.model;
    fresh.weapon = g_partner.weapon;
    fresh.ex_pack = g_partner.ex_pack;
    fresh.ex_model = g_partner.ex_model;
    g_partner = fresh;
    std::printf("coop: party reset for the session\n");
}

} // namespace coop

// Pad 2's pick beside the save slot (saves/<N>/coop-party.dat): written with the save, read as it
// loads, removed with it. The pick comes back only if that member has joined and is not the player's;
// a slot without the file starts with no pick (Xiao, or the next member, as on a new game).
namespace {

constexpr std::uint32_t kPartyMagic = 0x31505043; // "CPP1"
const char *const       kPartyFile = "coop-party.dat";

} // namespace

bool CoopPartySave(const std::filesystem::path &save_directory) {
    // The host writes it; a joiner's copy (player 2 of a link) writes none of its own.
    if (!coop::Enabled() || NetplayLocalPlayer() == 2 || coop::Mutated("partysave")) {
        return true;
    }

    std::uint32_t data[2] = {kPartyMagic, (std::uint32_t) (coop::g_partner.choice >= 0 ? coop::g_partner.choice : 0xFF)};

    if (FilesWrite(save_directory / kPartyFile, data, sizeof data, true) != FilesResult::kWritten) {
        std::fprintf(stderr, "coop: could not write %s\n", (save_directory / kPartyFile).string().c_str());
        return false;
    }

    std::printf("coop: pad 2's pick %d saved beside the slot\n", coop::g_partner.choice);
    return true;
}

void CoopPartyLoad(const std::filesystem::path &save_directory) {
    std::uint32_t data[2] = {};
    std::size_t   size = FilesRead(save_directory / kPartyFile, data, sizeof data);

    coop::g_partner.choice = -1;

    if (size != sizeof data || data[0] != kPartyMagic || data[1] > 5) {
        return;
    }

    CDngStatusData *status = SaveData->GetDngStatus();
    int             pick = (int) data[1];

    if (status == nullptr || pick >= status->party_size || pick == status->cur_chara) {
        std::printf("coop: pad 2's saved pick %d skipped (not joined, or the player's)\n", pick);
        return;
    }

    coop::g_partner.choice = pick;
    std::printf("coop: pad 2's pick %d restored from the slot\n", pick);
}

void CoopPartyDelete(const std::filesystem::path &save_directory) {
    std::error_code error;

    std::filesystem::remove(save_directory / kPartyFile, error);
}

namespace {
// coop-lobby: the lobby's picks for a new game, applied once the new game's party is made (CoopPartyNewGame); -1 none.
int g_lobby_host = -1;
int g_lobby_guest = -1;
} // namespace

void CoopPartyNewGame() {
    coop::g_partner.choice = -1;
    if (g_lobby_host >= 0 || g_lobby_guest >= 0) {
        const int host = g_lobby_host, guest = g_lobby_guest;

        g_lobby_host = g_lobby_guest = -1;
        CoopPartyLobbyPicks(host, guest, false);
    }
}

// coop-lobby (plan 4.4): the lobby's locked members become the game's at its start, on the host (the joiner takes them with the
// host's state): player 2's member is pad 2's pick (as if chosen, so no swap question at the start), player 1's the save's
// player member when it has joined the party (a stand-in member is pad 2's only). A new game's wait for its party.
void CoopPartyLobbyPicks(int host, int guest, bool new_game) {
    if (new_game) {
        g_lobby_host = host;
        g_lobby_guest = guest;
        std::fprintf(stderr, "lobby: picks held for the new game (player 1 %d, player 2 %d)\n", host, guest);
        return;
    }
    CDngStatusData *status = SaveData != nullptr ? SaveData->GetDngStatus() : nullptr;

    if (status != nullptr && host >= 0 && host < status->party_size && host != status->cur_chara) {
        status->cur_chara = (s8) host;
    }
    if (guest >= 0 && guest < 6 && (status == nullptr || guest != status->cur_chara)) {
        coop::g_partner.choice = guest;
    }
    std::fprintf(stderr, "lobby: picks applied: player 1 plays %d (asked %d), player 2's pick %d (asked %d)\n", status != nullptr ? status->cur_chara : -1, host,
                 coop::g_partner.choice, guest);
}

namespace coop {

// Whether a player 2 is there to play the partner: on a link (any copy with game.netplay set), only
// while it is up (the guest has connected; not while an online host waits, and not after the link goes
// down, which netplay decides on its own timeout); otherwise while pad 2 has a controller (a scripted
// pad counts). A plain solo game has no
// partner. Read from the link's shared state or the local pads, never from anything one linked copy
// alone sees.
//
// Latched: player 2 joins at once but leaves only after kAbsentTicks without a controller (or link) in a
// row, so a pad that drops out for a moment (a wireless pad's hiccup, a device re-enumerating) or a link
// flag that blinks for a tick changes nothing: the partner stays loaded where it stands. Nothing here
// moves the partner, even after a real absence; it comes back where it was left. Counted in calls (one a
// tick of play), so linked copies count alike.
//
// DC_COOP_MUTATE=presence shows the partner always (twin of the gate). DC_COOP_MUTATE=blip drops the
// latch and puts a returning partner beside the player, as before (twin of p2-blip-stays-put).
// DC_COOP_BLIP=start,ticks (test hook): pad 2 or the link reads as absent for that many calls from the
// start'th. Logs each join and leave.
namespace {

constexpr int kAbsentTicks = 300; // 5 s

int g_presence_was = -1;   // -1 never decided, else whether player 2 counted as there
int g_presence_absent = 0; // calls in a row without a controller or link
int g_presence_calls = 0;

} // namespace

bool StepPresence(bool *rejoined) {
    bool present;

    if (rejoined != nullptr) {
        *rejoined = false;
    }

    if (Mutated("presence")) {
        present = true;
    } else if (NetplayConfigured() && !Mutated("presence_pad")) {
        // coop-p2absent: Online and Linked copies have a player 2 only while the link is up, on the
        // same tick on both (the session's own state). A host waiting for its guest, or after the guest
        // left, has none, whatever pads this PC has (it read its own pad 2 then: Xiao stood in the town
        // before anyone joined). DC_COOP_MUTATE=presence_pad is that old reading, for the test's twin.
        // coop-xiaoback: nor while a catch-up host records its solo play (NetplayLocalPlayer reads 1 then, e25c5592:
        // Xiao stood beside the lone host again). DC_COOP_MUTATE=presence_catchup is that reading, for the twin.
        present = Mutated("presence_catchup") ? NetplayLocalPlayer() != 0 : NetplayPartnerLinked() || NetplayStayerMerge(); // coop-localfix2
    } else if (NetplayOwnsPads()) {
        InputPadState state;

        present = NetplayPad(1, state);
    } else {
        present = InputGetPad(1).connected;
    }

    static int blip_start = -1;
    static int blip_ticks = 0;

    if (blip_start == -1) {
        blip_start = -2;

        if (const char *blip = TestEnv("DC_COOP_BLIP")) {
            std::sscanf(blip, "%d,%d", &blip_start, &blip_ticks);
        }
    }

    g_presence_calls++;

    if (blip_start >= 0 && g_presence_calls >= blip_start && g_presence_calls < blip_start + blip_ticks) {
        if (g_presence_calls == blip_start) {
            std::printf("coop: test: player 2 drops out for %d ticks at %d\n", blip_ticks, g_presence_calls);
        }

        present = false;
    }

    bool latch = !Mutated("blip");

    g_presence_absent = present ? 0 : g_presence_absent + 1;

    bool counted = present || (latch && g_presence_was == 1 && g_presence_absent < kAbsentTicks);

    if ((int) counted != g_presence_was) {
        if (g_presence_was != -1 || counted) {
            std::printf("coop: player 2 %s\n", counted ? "joins" : "leaves");
            std::fflush(stdout); // coop-p2absent: the tests stop an online host by its PID
        }

        // The old behaviour, for the twin only: back again beside the player.
        if (counted && g_presence_was == 0 && !latch && rejoined != nullptr) {
            *rejoined = true;
        }

        g_presence_was = counted;
    }

    return counted;
}

// coop-xiaoback: "link" is the player this copy plays in lockstep, 0 while a catch-up host records alone.
void PresenceTrace(const char *where, bool drawn) {
    static const bool on = TestEnvOn("DC_COOP_PRESENCE_TRACE");
    static int        calls = 0;

    if (!on || calls++ % 60 != 0) {
        return;
    }

    std::printf("coop-presence: %s call %d link %d counted %d drawn %d\n", where, calls, NetplayPartnerLinked() ? NetplayLocalPlayer() : 0, g_presence_was, (int) drawn);
    std::fflush(stdout); // the tests stop an online host by its PID
}

void PresenceGet(int *was, int *absent) {
    *was = g_presence_was;
    *absent = g_presence_absent;
}

void PresenceSet(int was, int absent) {
    g_presence_was = was;
    g_presence_absent = absent;
}

void PresenceResetForSession() {
    g_presence_was = -1;
    g_presence_absent = 0;
}

// coop-warmreset (WS7): the DC_COOP_BLIP drop-out counts its ticks from the job's start, as from a fresh copy's.
void PartyResetForJob() {
    g_presence_calls = 0;
}

int PartyJobPresenceCalls() {
    return g_presence_calls;
}

} // namespace coop
