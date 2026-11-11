#pragma once

#include "coop/game_ns.hpp"

// What the co-op units (coop_*.cpp) share: the partner's state, its constants, and the core's helpers.
// coop.hpp is the public header; nothing outside port/src/coop includes this.

#include <libvu0.h>

#include <cstdint>

#include "character.hpp"
#include "collision.hpp"
#include "dataalloc.hpp"
#include "platform/input.hpp"
#include "textureanime.hpp"

namespace coop {

// The texture block the partner's pictures live in. GameInit re-initialises the manager on every floor, which
// empties it; an empty block is the sign to load. Not 0x40, which co-op once took for unused: Divine Beast Cave
// loads its waterfall (d01b20) there, and the partner's load emptied it, so the waterfall drew P2's face (Jep,
// 2026-10-21). 0x34, and 0x35 for the weapon, are clear of the blocks the floors were seen to enter (coop-texdup).
// DC_COOP_MUTATE=oldblock: 0x40 and 0x41 again (twin of v-partner-block).
int PartnerTextureBlock();

// The partner's model, motions and pictures: CharaModelBuffer's 0x33450 quadwords, with headroom.
constexpr int kModelQuads = 0x40000;

// The largest character pack on the disc is c04b.chr, 2196000 bytes.
constexpr int kPackBytes = 4 * 1024 * 1024;

// How far from the player the partner appears, and how far it may stray before it is brought
// back beside the player.
constexpr float kSpawnDistance = 14.0f;
constexpr float kLeashDistance = 220.0f;

// The player's walking pace (DunMoveChara's run_speed without a speed boost).
constexpr float kRunSpeed = 1.45f;

// DunMoveChara's run_speed with a speed boost (the Dran's Feather).
constexpr float kBoostSpeed = 3.0f;

// Each character's attack motions, as its key or battle action plays them (charakey.cpp,
// BattleActionPlay_*), -1 past the last: Toan, Xiao (wind-up, throw), Goro, Ruby (wind-up, shot),
// Ungaga, Osmond.
constexpr int kAttackMotions[6][2] = {{0x24, -1}, {11, 13}, {0x24, -1}, {11, 14}, {0x25, -1}, {11, -1}};

// Shots: BattleActionPlay_Jinn's speed and life, at most four in the air.
constexpr float kShotSpeed = 5.0f;
constexpr int   kShotLife = 0x78;
constexpr int   kShots = 4;

// The dead motion DunMoveChara plays at no life.
constexpr int kDownMotion = 0x17;

// Ticks the partner cannot be hit after a hit, lies down at no life, and swings at most.
constexpr int kInvincibleTicks = 60;
constexpr int kDownTicks = 600;
constexpr int kAttackTicks = 90;

// coop-party: kStandInLife (Toan's 70 for everyone) is gone; a stand-in has its own character's most life.

// coop-party: the texture block the partner's weapon lives in, beside its body's.
int PartnerWeaponBlock();

// coop-party: the texture block pad 2's character wheel keeps its faces in (coop_wheel.cpp). Its own:
// it shared 0x42 with P2's effect (coop_combat.cpp), so opening the wheel freed the effect's textures
// under it (WheelBlock).
constexpr int kWheelTextureBlock = 0x44;

// coop-party: how near the player stands still, and for how long, to get a downed partner up.
constexpr float kReviveDistance = 30.0f;
constexpr int   kReviveTicks = 120;
// coop-revive: with the Revive Powder rule on, how long the player stands still beside it before being asked.
constexpr int   kReviveAskTicks = 30;

// coop-party: the nearest the partner stands to the player's centre (KeepApart).
constexpr float kApart = 10.0f;

// Body height the monsters' hits are tested against (CharaHeight's table).
constexpr float kHeight[6] = {16.0f, 14.0f, 16.0f, 16.0f, 18.0f, 15.0f};

struct Shot {
    sceVu0FVECTOR pos = {0.0f, 0.0f, 0.0f, 1.0f};
    sceVu0FVECTOR vector = {0.0f, 0.0f, 0.0f, 0.0f};
    int           damage = 0;
    int           life = 0;
};

struct Partner {
    CCharacter    *chara = nullptr;
    CTexAnimeData *tex_anime = nullptr;
    CDataAlloc2<1> arena;
    u_int         *pack = nullptr;
    u_char        *model = nullptr;
    int            loaded_chara = -1;
    int            motion = -1;
    // coop-p1follows: the body's motion clock and facing as the state set was taken (the body is each copy's own, not in the
    // set), so the taking copy's body, loaded again, goes on from them (PartnerMotionTake, PartnerSnapshotApplied).
    struct BodyKept {
        bool         set = false;
        float        yaw = 0.0f;
        int          no = -1, flags = 0;
        float        speed = -1.0f;
        float        time = 0.0f, blend = 1.0f, blend_step = 0.0f;
        unsigned int frame = 0, next_frame = 0;
        int          asked = -1, playing = -1, blending = 0;
    } body_kept;
    bool           visible = false;
    sceVu0FVECTOR  pos = {0.0f, 0.0f, 0.0f, 1.0f};
    sceVu0FVECTOR  velocity = {0.0f, 0.0f, 0.0f, 0.0f};
    sceVu0FVECTOR  heading = {0.0f, 0.0f, 1.0f, 1.0f};

    // The swing under way: its motion, ticks played, and whether its hit has been recorded. Each
    // swing records one hit, which the monster it lands on consumes (CMonstorUnit::CheckDmg).
    bool attacking = false;
    int  attack_motion = 0;
    int  attack_stage = 0;
    int  attack_ticks = 0;
    bool attack_hit = false;
    int  since_attack = 3600;
    int  swings = 0;

    int  invincible = 0;
    int  down = 0;
    int  stand_in_life[6] = {-1, -1, -1, -1, -1, -1}; // coop-party: each stand-in's life, -1 full
    bool cross_held = false;
    Shot shots[kShots];

    // coop-party: the member pad 2 last picked (-1 for none yet), whether its swap buttons were held
    // last tick, each member's ticks left lying down (down is the loaded one's), and how long the
    // player has stood still beside the downed partner, and where the player stood last tick.
    int   choice = -1;
    bool  swap_held = false;
    int   down_left[6] = {};
    int   revive = 0;
    float player_last[3] = {};

    // coop-party: the partner's equipped weapon, held in its "weapon" frame.
    CCharacter *weapon = nullptr;
    bool        weapon_loaded = false;

    // coop-party: the arena's use just after the body (the weapon goes above it, and a reload rewinds
    // to it), the floor NewFloorCheck last saw and the floor of the last Load.
    int weapon_mark = 0;
    int floor = -100;
    int load_floor = -100;

    // coop-party: the chest motions (its character's _ex00 pack, added as an extra motion set) in an
    // arena of their own, which member they are for (-1 none), and the ticks left of an opening, during
    // which the partner stands still playing it.
    u_int         *ex_pack = nullptr;
    u_char        *ex_model = nullptr;
    CDataAlloc2<1> ex_arena;
    int            ex_chara = -1;
    int            busy = 0;
    // coop-atlaopen: her chest opening as retail's (BtGetTreasurebox*_Loop): ticks into it (-1 none), its box and item,
    // whether it holds on the item (its "acquired" message up) until her Cross or Circle, and her buttons last tick.
    int            open_tick = -1;
    int            open_box = -1;
    int            open_item = -1;
    int            open_volume = 0;
    bool           open_big = false;
    bool           open_hold = false;
    int            open_buttons = 0;
    int            open_message = -1;
    // coop-p2hurt: her gate key's presentation as retail's (BtGetGateKey_Loop): the key, its stage (0 none, 1 the pickup
    // motion 0x22, 2 holding it up, 0x23, with its message until her Cross or Circle) and that message.
    int            key_item = -1;
    int            key_stage = 0;
    int            key_message = -1;

    // The camera distance DunMoveChara left, put back after motionDrive has stepped the camera.
    bool  camera_widened = false;
    float camera_distance = 0.0f;

    // How far (0 to 1) the camera's look point has moved from the player toward the point between the
    // two, and whether the player could last walk straight to the partner.
    float camera_shift = 0.0f;
    bool  midpoint_clear = false;
    int   midpoint_wait = 0;

    // coop: gaps2 (FollowAfterEvent): whether an event is under way that hides the partner, and where
    // the player stood when it began.
    bool  event_held = false;
    float event_from[3] = {};
    // coop-startcell: where the player stood the tick before while the event runs (a cut in the scene she stands through).
    float event_last[3] = {};

    // coop-cutscenejoin: where player 2 came onto the floor (her arrival's end, by the floor's entrance), for a join by
    // player 1's state (selectMapNo * 256 + floor, -100 none): the partner's first Load on that floor stands her there,
    // not beside him. Set on his copy as the join begins, so his state set carries it to hers.
    int   arrive_floor = -100;
    float arrive_pos[3] = {};
    float arrive_yaw = 0.0f;
};

extern Partner g_partner;

bool Enabled();

float Stick(std::uint8_t axis);

// coop-party: test hook (DC_COOP_MUTATE=feature): that feature is broken on purpose, so each party
// test's mutation twin can show the test fails without it. Off unless the variable is set.
bool Mutated(const char *feature);

// The collision polygons around pos, gathered the way DunMoveChara gathers the player's.
int GatherPolys(const float *pos, CCPoly **out);

// coop: gaps2: GatherPolys leaves the Atlas out while this is set (the Atla test's measure), and the
// monster-script test's once-a-second line (coop_target.cpp).
extern bool g_skip_atla;
void        TargetOpcodeCheck();

// coop-warmreset (test-time plan WS7 step 1): a warm pair's job start (coop_core.cpp CoopResetForJob): each module's own scratch a
// job left behind, outside the state set, as a fresh copy has it; and the part of the job's leftovers line each reports.
void PartyResetForJob();     // the presence clock of the DC_COOP_BLIP drop-out (coop_party.cpp)
int  PartyJobPresenceCalls();
void AudioResetForJob();     // the partner voice's edge memory (coop_audio.cpp)
void MessageResetForJob();   // the system message's owner tag and her thirst slot (coop_message.cpp)
void CamtestResetForJob();   // the camera tests' accumulator (coop_camtest.cpp)
int  WheelJobOpens();        // both wheels' opens (coop_wheel.cpp)
long long WheelJobStepped(); // the link tick a wheel last stepped on (-10: none since the reset)
int  TargetsOnPartner();     // monsters going after the partner (coop_target.cpp)
void RingJobLeftovers(int *loaded, int *shots, int *flashes); // the partner's shot ring (coop_combat.cpp)
int  VoiceJobChara();        // the partner voice's character (coop_audio.cpp)
int  MessageJobDry();        // her thirst slot's message (coop_message.cpp)

} // namespace coop
