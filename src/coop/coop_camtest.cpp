#include "coop/game_ns.hpp"
#include "coop/coop_camtest.hpp"

#include <libvu0.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "btactstatus.hpp"
#include "camerafollow.hpp"
#include "character.hpp"
#include "dun/gameloop.hpp"
#include "dungeonmap.hpp"
#include "mathutil.hpp"
#include "monstorunit.hpp"
#include "randomitem.hpp"
#include "snd.hpp"
#include "userstatus.hpp"
#include "platform/config.hpp"

#include "coop/coop.hpp"
#include "coop/coop_camera.hpp"
#include "coop/coop_combat.hpp"
#include "coop/coop_core.hpp"
#include "coop/coop_lockhud.hpp" // coop-lockhud
#include "coop/coop_interact.hpp" // coop-p2atla
#include "coop/coop_menu.hpp"     // coop-p2atla
#include "coop/coop_prompt.hpp"   // coop-p2atla
#include "coop/netplay.hpp"

namespace dc::game {
extern s32   gameTask;
extern s32   lockOnTargetNo;  // ps2/src/dun/gameloop.cpp (coop-lockhud)
extern float targetCursorCnt; // ps2/src/dun/gameloop.cpp (coop-lockhud)
} // namespace dc::game

using namespace coop;

namespace {

const char *Test() {
    static const char *test = TestEnv("DC_COOP_CAMTEST");
    return test;
}

bool Is(const char *name) {
    return Test() != nullptr && std::strcmp(Test(), name) == 0;
}

long End(long otherwise) {
    static const char *end = TestEnv("DC_COOP_CAMTEST_END");
    return end != nullptr ? std::atol(end) : otherwise;
}

float DistXZ(const float *a, const float *b) {
    return std::hypot(a[0] - b[0], a[2] - b[2]);
}

struct State {
    long tick = 0;
    bool done = false;

    // cameras
    int   follow_n = 0, follow_bad = 0, move_n = 0, move_bad = 0;
    float forward[2] = {0.0f, 0.0f};
    bool  have_forward = false;
    float last[4] = {};
    bool  have_last = false;

    // pullback
    long  placed = -1;
    float closest = 1.0e9f;

    // intro
    bool opening = false;
    int   opening_ticks = 0, arrival_ticks = 0, moved_n = 0, moved_ok = 0;
    int   referenced = 0, beside = 0, jumps = 0;
    float last_rel[2] = {};
    float last_player[2] = {};
    float path[600][2] = {};
    int   path_n = 0, path_checked = 0, on_path = 0, reach_checked = 0, reachable = 0;
    bool  have_rel = false;

    // sfx
    int steps = 0, throws = 0, hits = 0, hurts = 0;

    // wake
    int   monster = -1;
    long  woken = -1;
    float clip = 0.0f;

    // lockcam
    long  locked = -1;
    float start_error = 0.0f, worst = 0.0f;
    int   behind_n = 0, behind_ok = 0, relocks = 0;
    // coop-dungeonp2: wakedown (the partner knocked down once the monster is up), drops
    long  knocked = -1;
    long  dropped = -1;
    float drop[4] = {};
    int   drawn = 0;

    // coop-lockhud: lockhud (her lock is monster, his monster2)
    int  monster2 = -1;
    long placed2 = -1, locked2 = -1;
    int  hud_n = 0, hud_ok = 0, hud_near = 0, relocks2 = 0;
    int  bad_name = 0, bad_bar = 0, bad_own = 0, bad_other = 0;
    int  colour_n = 0, bad_colour = 0; // coop-lockcolour: frames whose bar or corners were not their player's colour
    char first_bad[160] = "";

    // coop-p2atla: the step, its tick, the Atla's event slot; copy B's frames drawn from her camera (hers) of
    // those drawn through her first take and through his presentation
    int  atla_step = 0;
    long atla_at = 0;
    int  atla_event = -1;
    int  take_frames = 0, take_hers = 0, scene_frames = 0, scene_hers = 0;

    // coop-p2circle: circlecam (the tick she was turned, the tick Circle let go, her camera's error from behind
    // her just before it, and the buttons held on pad 2)
    long           turned = -1, tapped = -1;
    int            ready_ticks = 0;
    float          tap_error = 0.0f;
    long           square = -1;          // coop-squarequiet: the tick her Square was let go, after the Circle's result
    float          circle_error = -1.0f; //   her camera's error 90 ticks after the Circle (the Circle's result)
    unsigned short buttons = 0;

    // coop-strafe: strafe (the stick held this tick on her camera's axes, the phase it belongs to, her place last tick;
    // per phase, the ticks she stepped, faced the monster, went the way pushed and played a strafe motion, the distance
    // stepped, and each motion's count)
    float stick[2] = {};
    int   phase = -1, last_phase = -1;
    float last_pos[4] = {};
    int   stepped[3] = {}, faced[3] = {}, went[3] = {}, strafed[3] = {};
    float distance[3] = {};
    int   motions[3][32] = {};

    // coop-lockpress: lockpress (the tick copy B's own pad first held Circle and when it let go, the tick Circle reached
    // the game and the lock was taken, whether the lock was there when Circle let go, the second hold's start)
    long  press_at = -1, release_at = -1, seen_at = -1, lock_at = -1, hold_at = -1, hold_seen = -1, dropped_at = -1;
    bool  held_at_release = false;
    unsigned short own_buttons = 0;
};

State g;

void Verdict(bool pass, const char *format, ...) {
    if (g.done) {
        return;
    }

    char    detail[512];
    va_list args;

    va_start(args, format);
    std::vsnprintf(detail, sizeof detail, format, args);
    va_end(args);
    g.done = true;
    std::printf("camtest: %s %s %s (player %d, tick %ld)\n", Test(), pass ? "PASS" : "FAIL", detail, NetplayLocalPlayer(), g.tick);
    std::fflush(stdout);
}

float *LocalPosition() {
    return NetplayLocalPlayer() == 2 ? g_partner.pos : CharaMain.pos;
}

bool Playing() {
    return gameTask == GAME_TASK_PLAY && !FloorIntro() && g_partner.visible;
}

// coop-fx51d: a check's start, playing. On a state fixture (DC_STATE_LOAD_AT) that is the event itself, the fixture put
// back (past the floor's opening and her arrival, the partner on the floor), and the copy says so once ("ready at tick",
// the link tick: the shortest line's anchor); without one, two seconds of play as before (the arrival's lead-in, no anchor).
bool Ready() {
    static const bool fixture = TestEnv("DC_STATE_LOAD_AT") != nullptr;
    static bool       ready = false;

    if (!Playing()) {
        return false;
    }

    if (!ready) {
        if (fixture ? !NetplayStateFileLoaded() || NetplayStateLoadPending() : g.tick < 120) {
            return false;
        }

        ready = true;

        if (fixture) {
            std::printf("camtest: %s ready at tick %lld (the state fixture put back, the partner on the floor)\n", Test(),
                        (long long) NetplayLinkTick());
            std::fflush(stdout);
        }
    }

    return true;
}

void PutPartner(const float *pos) {
    for (int i = 0; i < 3; i++) {
        g_partner.pos[i] = pos[i];
    }

    g_partner.velocity[0] = g_partner.velocity[1] = g_partner.velocity[2] = 0.0f;
    g_partner.chara->frame->SetPosition(g_partner.pos);
}

// Each tick a player's stick points: its step must go that way on the camera this copy drew.
void Cameras() {
    if (Playing()) {
        InputPadState pad;
        float        *pos = LocalPosition();

        if (NetplayPad(NetplayLocalPlayer() - 1, pad) && g.have_last && g.have_forward) {
            float lx = Stick(pad.left_x);
            float ly = Stick(pad.left_y);
            float dx = pos[0] - g.last[0];
            float dz = pos[2] - g.last[2];
            float moved = std::hypot(dx, dz);
            float fx = g.forward[0];
            float fz = g.forward[1];
            float ex = -ly * fx - lx * fz;
            float ez = -ly * fz + lx * fx;
            float want = std::hypot(ex, ez);

            if (std::hypot(lx, ly) > 0.5f && moved > 0.5f && want > 0.0f) {
                g.move_n++;
                g.move_bad += (dx * ex + dz * ez) / (moved * want) < 0.7f;
            }
        }

        std::memcpy(g.last, pos, sizeof g.last);
        g.have_last = true;
    }

    if (g.tick >= End(2400)) {
        bool pass = g.follow_n >= 100 && g.follow_bad * 20 <= g.follow_n && g.move_n >= 30 && g.move_bad * 5 <= g.move_n;

        Verdict(pass, "drawn camera on own player %d/%d (apart >= 100), steps along own camera %d/%d", g.follow_n - g.follow_bad,
                g.follow_n, g.move_n - g.move_bad, g.move_n);
    }
}

// The partner put more than 400 units from the player, on an event's spot (on the floor): it must stay.
void Pullback() {
    if (g.placed < 0) {
        if (!Ready()) {
            return;
        }

        int   best = -1;
        float best_dist = 1.0e9f;

        for (int i = 0; i < 48; i++) {
            float dist = DistXZ(NowDngMap->events[i].pos, CharaMain.pos);

            if (NowDngMap->events[i].kind != DNG_EVENT_NONE && dist > 450.0f && dist < best_dist) {
                best = i;
                best_dist = dist;
            }
        }

        if (best < 0) {
            Verdict(false, "precondition: no floor spot more than 450 from the player");
            return;
        }

        PutPartner(NowDngMap->events[best].pos);
        g.placed = g.tick;

        float dist = DistXZ(g_partner.pos, CharaMain.pos);

        if (dist <= 400.0f || g_partner.pos[1] < -100.0f) {
            Verdict(false, "precondition: partner %.0f from the player (height %.0f) when put there", dist, g_partner.pos[1]);
            return;
        }

        std::printf("camtest: pullback partner put on event %d, %.0f from the player\n", best, best_dist);
        return;
    }

    float dist = DistXZ(g_partner.pos, CharaMain.pos);

    {
        g.closest = std::min(g.closest, dist);

        if (dist < 400.0f) {
            Verdict(false, "partner moved back to %.0f from the player %ld ticks after the placement", dist, g.tick - g.placed);
        } else if (g.tick >= g.placed + 600) {
            Verdict(true, "partner stayed apart for 600 ticks (closest %.0f)", g.closest);
        }
    }
}

// Where the player is drawn: its frame's world place, read here and not through PlayerDrawnPlace, so a
// twin that changes that helper cannot change what the check measures.
void DrawnPlayer(float *place) {
    sceVu0FMATRIX world;

    CharaMain.frame->GetLWMatrix(world);
    place[0] = world[3][0];
    place[1] = world[3][1];
    place[2] = world[3][2];
}

// Through the floor's opening. Wise Owl Forest (DC_COOP_CAMTEST_MAP=1, the default): the partner plays
// motion 100, her own arrival. Divine Beast Cave (0): a scene's node carries the player in (its frame
// references the node), and she walks his way (within 8 of where he was drawn). On both: the partner
// stays within 24 of the player where it is drawn, never moves round him (no tick turns her place
// from him by more than 8, but where he cuts), and copy B looks at the partner (the shown camera moved
// by where she stands from the drawn player).
void Intro() {
    static const char *map_env = TestEnv("DC_COOP_CAMTEST_MAP");
    static const int   map = map_env != nullptr ? std::atoi(map_env) : 1;

    if (FloorIntro()) {
        float player[4];

        DrawnPlayer(player);

        float rel[2] = {g_partner.pos[0] - player[0], g_partner.pos[2] - player[2]};
        bool  cut = g.have_rel && std::hypot(player[0] - g.last_player[0], player[2] - g.last_player[1]) > 8.0f;

        g.last_player[0] = player[0];
        g.last_player[1] = player[2];

        // A cut (the scene's node taking the player far off) may turn her round him with it.
        if (g.have_rel && !cut && std::hypot(rel[0] - g.last_rel[0], rel[1] - g.last_rel[1]) > 8.0f) {
            g.jumps++;
            std::printf("camtest: intro: the partner moved %.0f round the player at opening tick %d\n",
                        std::hypot(rel[0] - g.last_rel[0], rel[1] - g.last_rel[1]), g.opening_ticks);
        }

        g.last_rel[0] = rel[0];
        g.last_rel[1] = rel[1];
        g.have_rel = true;
        g.opening = true;
        g.opening_ticks++;
        g.beside += std::hypot(rel[0], rel[1]) <= 24.0f;

        // On his own feet, she stands where he could walk to (no rock or wall between them). Once a node
        // has carried him she walks his way down the stairs, which the on-his-way count checks instead
        // (the stairs' steps stand between them for PathClear).
        if (g.referenced == 0 && g.opening_ticks > 1) {
            g.reach_checked++;
            g.reachable += PathClear(player, g_partner.pos);
        }

        // Carried in (the cave), past the first 25 ticks: she walks where he walked (within 8 of a
        // point of his drawn way), not in the walls beside it.
        if (CharaMain.frame->reference != 0) {
            g.referenced++;

            if (g.path_n < (int) (sizeof g.path / sizeof g.path[0])) {
                g.path[g.path_n][0] = player[0];
                g.path[g.path_n][1] = player[2];
                g.path_n++;
            }

            if (g.referenced > 25) {
                float nearest = 1.0e9f;

                for (int i = 0; i < g.path_n; i++) {
                    nearest = std::min(nearest, std::hypot(g_partner.pos[0] - g.path[i][0], g_partner.pos[2] - g.path[i][1]));
                }

                g.path_checked++;
                g.on_path += nearest <= 8.0f;
            }
        }
        g.arrival_ticks += g_partner.motion == 100 && g_partner.chara->motion_no == 100;
        return;
    }

    if (g.opening) {
        bool pre = selectMapNo == map && g_partner.loaded_chara == CHARA_XIAO && g.opening_ticks >= 30 && (map != 0 || g.referenced >= 30);
        bool view = NetplayLocalPlayer() != 2 || (g.moved_n >= 30 && g.moved_ok * 20 >= g.moved_n * 19);
        bool arrival = map != 1 || g.arrival_ticks * 2 >= g.opening_ticks;
        bool beside = g.beside * 20 >= g.opening_ticks * 19 && g.jumps == 0;
        bool path = g.path_checked == 0 || g.on_path * 20 >= g.path_checked * 19;
        bool reach = g.reachable * 20 >= g.reach_checked * 19;
        bool pass = pre && arrival && beside && path && reach && view;

        Verdict(pass, "%sopening %d ticks (player on a scene's node %d), partner arriving (motion 100) %d, beside the drawn player %d, "
                "moved round him %d, on his way %d/%d, where he could walk %d/%d, copy B's opening camera on the partner %d/%d",
                pre ? "" : map == 0 ? "precondition failed (Divine Beast Cave, Xiao, the player carried in): " : "precondition failed (Wise Owl Forest, Xiao): ",
                g.opening_ticks, g.referenced, g.arrival_ticks, g.beside, g.jumps, g.on_path, g.path_checked, g.reachable, g.reach_checked,
                g.moved_ok, g.moved_n);
    } else if (g.tick >= End(1500)) {
        Verdict(false, "precondition: no floor opening seen");
    }
}

void Sfx() {
    // CCharacter::Step sets foot_sound_wait to 5 on a footstep and counts it down once in the same step.
    g.steps += g_partner.chara->foot_sound_wait == 4;

    if (g.steps > 0 && g.throws > 0 && g.hits > 0 && g.hurts > 0) {
        Verdict(true, "footsteps %d, throws %d, monster hits %d, hurts %d", g.steps, g.throws, g.hits, g.hurts);
    } else if (g.tick >= End(5000)) {
        Verdict(false, "%sfootsteps %d, throws %d, monster hits %d, hurts %d", g_partner.loaded_chara == CHARA_XIAO ? "" : "precondition (Xiao): ",
                g.steps, g.throws, g.hits, g.hurts);
    }
}

// A sleeping monster the player is far from: the partner is put inside its wake distance, alone.
void Wake() {
    if (g.monster < 0) {
        if (!Ready()) {
            return;
        }

        float best = 1.0e9f;

        for (int i = 0; i < 16; i++) {
            MONSTOR &monster = NowMonstorUnit->monster[i];
            float    position[4];

            if (monster.state != 1) {
                continue;
            }

            NowMonstorUnit->chara[i][0].GetPosition(position);
            float dist = DistXZ(position, CharaMain.pos);

            if (dist > monster.clip_distance + 80.0f && dist < best) {
                best = dist;
                g.monster = i;
            }
        }

        if (g.monster < 0) {
            Verdict(false, "precondition: no sleeping monster far enough from the player");
            return;
        }

        float position[4];
        float target[4];

        NowMonstorUnit->chara[g.monster][0].GetPosition(position);
        g.clip = NowMonstorUnit->monster[g.monster].clip_distance;

        for (int i = 0; i < 3; i++) {
            // coop-dungeonp2: wakedown stands her 30 from the monster, on its own floor: further out the
            // line can cross a gap, where she lies down in the air and falls through.
            target[i] = position[i] + (CharaMain.pos[i] - position[i]) * (Is("wakedown") ? 30.0f : g.clip - 60.0f) / best;
        }

        target[1] = position[1] + 10.0f;
        PutPartner(target);
        if (Is("wakedown")) {
            // coop-fixtures (2026-11-01): and turned to the monster, so her swings (-FarSound's far hits) go at it. She kept
            // the heading she had, which hit it only when the floor pick happened to leave her facing that way: from the
            // dun1-f1 fixture she faced up the corridor and no shot of hers ever landed.
            float dx = position[0] - target[0];
            float dz = position[2] - target[2];
            float len = std::sqrt(dx * dx + dz * dz);
            if (len > 0.0f) {
                g_partner.heading[0] = dx / len;
                g_partner.heading[1] = 0.0f;
                g_partner.heading[2] = dz / len;
                g_partner.chara->frame->SetRotation(0.0f, std::atan2(dx, dz), 0.0f); // (her shots go the way her body faces: Yaw)
            }
        }
        g.placed = g.tick;
        std::printf("camtest: wake partner put %.0f from monster %d (wakes at %.0f), player %.0f from it\n", Is("wakedown") ? 30.0f : g.clip - 60.0f, g.monster,
                    g.clip, best);
        return;
    }

    float position[4];

    NowMonstorUnit->chara[g.monster][0].GetPosition(position);
    float player = DistXZ(position, CharaMain.pos);

    // coop-dungeonp2: wakedown. Once Xiao has woken it she is knocked down where she stands (both copies,
    // the same tick); the monster must still be up 200 ticks later, the player still far.
    if (Is("wakedown") && g.knocked < 0 && NowMonstorUnit->monster[g.monster].state == 2) {
        g_partner.down = kDownTicks;
        g.knocked = g.tick;
        std::printf("camtest: wakedown partner knocked down beside monster %d at tick %ld, y %.1f\n", g.monster, g.tick, g_partner.pos[1]);
        return;
    }

    if (Is("wakedown")) {
        if (g.knocked < 0) {
            if (g.tick >= g.placed + 400) {
                Verdict(false, "precondition: monster %d never woke", g.monster);
            }
        } else if (g.tick - g.knocked <= 60 && (g.tick - g.knocked) % 10 == 0) {
            std::printf("camtest: wakedown partner at (%.1f %.1f %.1f) velocity y %.2f, %ld ticks down\n", g_partner.pos[0],
                        g_partner.pos[1], g_partner.pos[2], g_partner.velocity[1], g.tick - g.knocked);
        }

        if (g.knocked < 0) {
        } else if (NowMonstorUnit->monster[g.monster].state != 2) {
            Verdict(false, "monster %d asleep %ld ticks after the partner went down, partner %.0f from it (y %.1f), player %.0f",
                    g.monster, g.tick - g.knocked, DistXZ(position, g_partner.pos), g_partner.pos[1], player);
        } else if (g.tick >= g.knocked + 200) {
            bool pre = player > g.clip && g_partner.down > 0;

            Verdict(pre, "%smonster %d still up 200 ticks after the partner went down, partner %.0f from it, player %.0f (wakes at %.0f)",
                    pre ? "" : "precondition (player far, partner down): ", g.monster, DistXZ(position, g_partner.pos), player, g.clip);
        }

        return;
    }

    if (NowMonstorUnit->monster[g.monster].state == 2) {
        bool pre = player > g.clip;

        Verdict(pre, "%smonster %d awake at tick %ld, partner %.0f from it, player %.0f (wakes at %.0f)", pre ? "" : "precondition (player far): ",
                g.monster, g.tick, DistXZ(position, g_partner.pos), player, g.clip);
    } else if (g.tick >= g.placed + 400) {
        Verdict(false, "monster %d still asleep 400 ticks later, partner %.0f from it", g.monster, DistXZ(position, g_partner.pos));
    }
}

// Whether the partner could lock the monster on (coop_combat's Lockable, at its own range).
bool Lockable(int i) {
    const MONSTOR &monster = NowMonstorUnit->monster[i];
    float          position[4];

    if (monster.state != 2 || monster.hp <= 0 || monster.revealed == 0 || monster.lockon_enabled == 0 ||
        monster.kind == MONSTER_KIND_NO_LOCK_ON) {
        return false;
    }

    NowMonstorUnit->chara[i][0].GetPosition(position);
    float dy = position[1] - g_partner.pos[1];

    return std::hypot(position[0] - g_partner.pos[0], dy, position[2] - g_partner.pos[2]) < monster.lock_range && std::fabs(dy) < 80.0f;
}

// How far the partner's camera is turned from behind the partner as it looks at the monster (the angle
// autoCamTrial pulls a locked-on camera round to), in radians.
float BehindError(int i) {
    float angle = 0.0f;
    float eye[4];
    float position[4];

    CoopPartnerCamera(&angle, eye);
    NowMonstorUnit->chara[i][0].GetPosition(position);

    float want = atan2f(position[0] - g_partner.pos[0], position[2] - g_partner.pos[2]) - PI;

    return std::fabs(std::remainder(angle - want, 2.0f * PI));
}

// The partner locked on to a monster with her camera turned 140 degrees from behind her: as the
// player's does, the camera swings round behind her, toward the monster, and stays there.
void LockCam() {
    // Wakes a sleeping monster far from the player, as Wake does.
    if (g.monster < 0) {
        Wake();
        return;
    }

    if (g.locked < 0) {
        if (g.tick >= g.placed + 600) {
            Verdict(false, "precondition: monster %d never came awake and lockable by the partner", g.monster);
            return;
        }

        if (NowMonstorUnit->monster[g.monster].state != 2 || NowMonstorUnit->monster[g.monster].revealed == 0) {
            return;
        }

        float angle = 0.0f;
        float eye[4];
        float position[4];

        if (!CoopPartnerCamera(&angle, eye)) {
            Verdict(false, "precondition: the partner has no camera of her own");
            return;
        }

        NowMonstorUnit->chara[g.monster][0].GetPosition(position);
        const MONSTOR &monster = NowMonstorUnit->monster[g.monster];
        float          away = std::clamp(0.6f * monster.lock_range, 35.0f, 55.0f);
        bool           placed = false;

        // The partner stood away from the monster, facing it, the way that leaves her camera 140 degrees
        // from behind her (either side, whichever she could walk to).
        // coop-fx51d: neither side clear (dun1-f1's monster 3 from her camera at the load): the first clear way of eight round
        // the monster, and her camera turned 140 degrees from behind her there, as circlecam turns it (SetAngleSoon); the
        // lock waits a few ticks for the turn to land.
        static long turned_at = -1;

        if (turned_at >= 0) {
            if (g.tick < turned_at + 4) {
                return;
            }
            placed = true;
        }
        for (float side : {1.0f, -1.0f}) {
            if (placed) {
                break;
            }
            float yaw = angle + PI + side * 2.44f;
            float spot[4] = {position[0] - away * sinf(yaw), position[1], position[2] - away * cosf(yaw), 1.0f};

            if (PathClear(position, spot)) {
                PutPartner(spot);
                g_partner.chara->frame->SetRotation(0.0f, yaw, 0.0f);
                placed = true;
                break;
            }
        }
        for (int w = 0; w < 8 && !placed && turned_at < 0; w++) {
            float yaw = (float) w * 0.7853982f;
            float spot[4] = {position[0] - away * sinf(yaw), position[1], position[2] - away * cosf(yaw), 1.0f};

            if (PathClear(position, spot)) {
                PutPartner(spot);
                g_partner.chara->frame->SetRotation(0.0f, yaw, 0.0f);
                CoopCameraObject()->SetAngleSoon(yaw - PI + 2.44f);
                turned_at = g.tick;
                return;
            }
        }

        if (!placed || !Lockable(g.monster)) {
            // (coop-fx51d: which of the two: on dun1-f1 both lockcam and lockpress stopped here at tick 121 on monster 3)
            Verdict(false, "precondition: no clear spot %.0f from monster %d (lock range %.0f) to lock it from (%s; state %d hp %d revealed %d lockon %d)",
                    away, g.monster, monster.lock_range, placed ? "placed, not lockable" : "no clear path", (int) monster.state, (int) monster.hp,
                    (int) monster.revealed, (int) monster.lockon_enabled);
            return;
        }

        g_combat.lock = g.monster;
        g.locked = g.tick;
        g.start_error = BehindError(g.monster);
        std::printf("camtest: lockcam partner locked monster %d from %.0f, camera %.2f rad from behind her\n", g.monster, away,
                    g.start_error);
        return;
    }

    // The lock held for the whole check (a lock let go by range is taken again while the monster can be).
    if (g_combat.lock != g.monster) {
        if (!Lockable(g.monster)) {
            Verdict(false, "precondition: monster %d no longer lockable %ld ticks after the lock", g.monster, g.tick - g.locked);
            return;
        }

        g_combat.lock = g.monster;
        g.relocks++;
    }

    float position[4];

    NowMonstorUnit->chara[g.monster][0].GetPosition(position);
    float error = BehindError(g.monster);

    // autoCamTrial leaves a camera alone once within 20 to 90 degrees of behind (nearer, wider) and
    // not at all inside 30 units: after two seconds it must be within 95 degrees while she is further.
    if (g.tick >= g.locked + 120 && DistXZ(position, g_partner.pos) > 30.0f) {
        g.behind_n++;
        g.behind_ok += error < 1.65f;
        g.worst = std::max(g.worst, error);
    }

    if (g.tick >= g.locked + 420) {
        bool pre = g.start_error > 2.0f && g.behind_n >= 100;
        bool pass = pre && g.behind_ok * 10 >= g.behind_n * 9;

        Verdict(pass, "%scamera %.2f rad from behind at the lock; behind her (under 1.65 rad) %d/%d ticks, worst %.2f, relocks %d",
                pre ? "" : "precondition (turned away at the lock, 100+ ticks measured): ", g.start_error, g.behind_ok, g.behind_n,
                g.worst, g.relocks);
    }
}

// coop-strafe: locked on, pad 2's stick pushed to her side (150 ticks one way, 120 the other) and then away from the
// monster (40): as the player does locked on (DunMoveChara's lock-on branch), she faces the monster throughout, goes
// the way her stick points on her own camera, at the stick's pace (1 a tick, not the run's 1.45), playing the side
// motions (0x13, 0x14, one each way) and the back one (0x16).
constexpr long kStrafeSettle = 60;
constexpr long kStrafeEnds[3] = {kStrafeSettle + 150, kStrafeSettle + 270, kStrafeSettle + 310};

int StrafePhase() {
    if (g.locked < 0) {
        return -1;
    }

    long since = g.tick - g.locked;

    for (int i = 0; i < 3; i++) {
        if (since >= (i == 0 ? kStrafeSettle : kStrafeEnds[i - 1]) && since < kStrafeEnds[i]) {
            return i;
        }
    }

    return -1;
}

int Majority(const int *counts) {
    int best = 0;

    for (int i = 1; i < 32; i++) {
        if (counts[i] > counts[best]) {
            best = i;
        }
    }

    return best;
}

void Strafe() {
    // Wakes a monster far from Toan, as Wake does, then stands her facing it from the first clear spot round it (any of
    // 16 ways, 40 or 30 away: room to step round it) and locks her on.
    if (g.monster < 0) {
        Wake();
        return;
    }

    if (g.locked < 0) {
        if (g.tick >= g.placed + 600) {
            Verdict(false, "precondition: monster %d never came awake and lockable by the partner", g.monster);
            return;
        }

        if (NowMonstorUnit->monster[g.monster].state != 2 || NowMonstorUnit->monster[g.monster].revealed == 0) {
            return;
        }

        float at[4];

        NowMonstorUnit->chara[g.monster][0].GetPosition(at);

        for (float away : {40.0f, 30.0f}) {
            for (int k = 0; k < 16 && g.locked < 0; k++) {
                float yaw = k * (2.0f * PI / 16.0f);
                float spot[4] = {at[0] - away * sinf(yaw), at[1], at[2] - away * cosf(yaw), 1.0f};

                if (!PathClear(at, spot)) {
                    continue;
                }

                PutPartner(spot);
                g_partner.chara->frame->SetRotation(0.0f, yaw, 0.0f);

                if (Lockable(g.monster)) {
                    g_combat.lock = g.monster;
                    g.locked = g.tick;
                    std::printf("camtest: strafe partner locked monster %d from %.0f\n", g.monster, away);
                }
            }
        }

        if (g.locked < 0) {
            Verdict(false, "precondition: no clear spot round monster %d to lock it from", g.monster);
        }

        return;
    }

    if (g_combat.lock != g.monster) {
        if (!Lockable(g.monster)) {
            Verdict(false, "precondition: monster %d no longer lockable %ld ticks after the lock", g.monster, g.tick - g.locked);
            return;
        }

        g_combat.lock = g.monster;
        g.relocks++;
    }

    float position[4];

    NowMonstorUnit->chara[g.monster][0].GetPosition(position);

    // What this tick's Walk did with the stick set last tick.
    if (g.phase >= 0 && g_partner.down == 0 && !g_partner.attacking) {
        int   i = g.phase;
        float moved[2] = {g_partner.pos[0] - g.last_pos[0], g_partner.pos[2] - g.last_pos[2]};
        float step = std::hypot(moved[0], moved[1]);

        if (step > 0.3f) {
            sceVu0FVECTOR rotation;

            g_partner.chara->frame->GetRotation(rotation);
            float face = atan2f(position[0] - g_partner.pos[0], position[2] - g_partner.pos[2]);
            float angle = PartnerCameraAngleH();
            float want = atan2f(g.stick[0] * cosf(angle) + g.stick[1] * sinf(angle), g.stick[1] * cosf(angle) - g.stick[0] * sinf(angle));

            g.stepped[i]++;
            g.distance[i] += step;
            g.faced[i] += std::fabs(std::remainder(rotation[1] - face, 2.0f * PI)) < 0.35f;
            g.went[i] += std::fabs(std::remainder(atan2f(moved[0], moved[1]) - want, 2.0f * PI)) < 0.5f;
            g.motions[i][g_partner.motion & 31]++;

            int motion = g_partner.motion;

            g.strafed[i] += i < 2 ? (motion == 0x13 || motion == 0x14) : motion == 0x16;
        }
    }

    // The stick for the next tick's Walk: to her side or away, on the axes of her own camera (Walk's move_x, move_z).
    g.phase = StrafePhase();
    sceVu0CopyVector(g.last_pos, g_partner.pos);

    if (g.phase >= 0) {
        float to[2] = {position[0] - g_partner.pos[0], position[2] - g_partner.pos[2]};
        float length = std::hypot(to[0], to[1]);
        float side = g.phase == 0 ? 1.0f : -1.0f;
        float push[2] = {0.0f, 0.0f};

        if (length > 0.0f) {
            if (g.phase < 2) {
                push[0] = side * to[1] / length;
                push[1] = -side * to[0] / length;
            } else {
                push[0] = -to[0] / length;
                push[1] = -to[1] / length;
            }
        }

        float angle = PartnerCameraAngleH();

        g.stick[0] = push[0] * cosf(angle) - push[1] * sinf(angle);
        g.stick[1] = push[0] * sinf(angle) + push[1] * cosf(angle);
    }

    if (g.tick - g.locked < kStrafeEnds[2] + 2) {
        return;
    }

    int  right = Majority(g.motions[0]);
    int  left = Majority(g.motions[1]);
    bool pre = g.stepped[0] >= 20 && g.stepped[1] >= 20 && g.stepped[2] >= 15;
    bool pass = pre;
    char line[3][96];

    for (int i = 0; i < 3; i++) {
        int   n = std::max(g.stepped[i], 1);
        float pace = g.distance[i] / n;

        pass = pass && g.faced[i] * 10 >= n * 9 && g.went[i] * 10 >= n * 7 && g.strafed[i] * 10 >= n * 9 && pace > 0.8f &&
               pace < 1.15f;
        std::snprintf(line[i], sizeof line[i], "%s: stepped %d, faced %d, went %d, strafe motion %d (most 0x%x), pace %.2f",
                      i == 0 ? "side" : i == 1 ? "other side" : "back", g.stepped[i], g.faced[i], g.went[i], g.strafed[i],
                      Majority(g.motions[i]), pace);
    }

    pass = pass && right != left;
    Verdict(pass, "%s%s; %s; %s; relocks %d", pre ? "" : "precondition (20, 20 and 15 ticks stepped; a wall may stop a side): ", line[0], line[1], line[2],
            g.relocks);
}

// coop-p2circle: how far the partner's camera is turned from behind her facing, in radians.
float FacingError() {
    float         angle = 0.0f;
    float         eye[4];
    sceVu0FVECTOR rotation;

    CoopPartnerCamera(&angle, eye);
    g_partner.chara->frame->GetRotation(rotation);
    return std::fabs(std::remainder(angle - (rotation[1] - PI), 2.0f * PI));
}

// coop-p2circle: with no monster she could lock on, the partner's camera is turned 140 degrees from behind her and
// pad 2 taps Circle: her camera comes round behind her, as DunMoveChara's PadInput_NO brings the player's.
void CircleCam() {
    g.buttons = 0;

    float angle = 0.0f;
    float eye[4];

    if (!Playing() || !CoopPartnerCamera(&angle, eye)) {
        g.ready_ticks = 0;

        if (g.tick > 3000) {
            Verdict(false, "precondition: never playing with her own camera");
        }

        return;
    }

    if (g.turned < 0) {
        // Two seconds into play, the opening and her arrival over (coop-fx51d: on a state fixture, its load: Ready).
        static const bool fixture = TestEnv("DC_STATE_LOAD_AT") != nullptr;

        if (fixture ? !Ready() : ++g.ready_ticks < 120) {
            return;
        }

        for (int i = 0; i < 16; i++) {
            if (Lockable(i)) {
                Verdict(false, "precondition: monster %d is lockable by her, so Circle would lock on", i);
                return;
            }
        }

        // Her camera turned, as her right stick would, since her facing goes back to her heading when set alone.
        sceVu0FVECTOR rotation;

        g_partner.chara->frame->GetRotation(rotation);
        CoopCameraObject()->SetAngleSoon(rotation[1] - PI + 2.44f);
        g.turned = g.tick;
        std::printf("camtest: circlecam partner turned, camera %.2f rad from behind her, nothing lockable\n", FacingError());
        return;
    }

    // A tap, as LockNext takes it: Circle held four ticks and let go, half a second after she turned. Her camera is read
    // before CombatInput sees the press (CamTestPad's buttons reach it next tick): coop-lockpress acts on the press.
    if (g.tapped < 0) {
        long since = g.tick - g.turned;

        if (since == 30) {
            g.tap_error = FacingError();
        }

        if (since >= 30 && since < 34) {
            g.buttons = kInputCircle;
        } else if (since == 34) {
            g.tapped = g.tick;
            std::printf("camtest: circlecam Circle tapped, camera %.2f rad from behind her\n", g.tap_error);
        }

        return;
    }

    // Retail sets the camera's angle to come round to; CCameraFollow eases it there.
    if (g.tick == g.tapped + 90) {
        g.circle_error = FacingError();
    }

    // coop-squarequiet (Jep's 2026-10-31 Local playtest: her Square, like her Circle, played the menu's back sound): then her
    // field Square with nothing it can use (her quick slot as the floor's start leaves it), held four ticks: retail's Square says
    // nothing then (DunMoveChara: no usable item, no sound). The test reads the copies' sound lines from her turn to the verdict.
    if (g.tick >= g.tapped + 100 && g.square < 0) {
        long since = g.tick - (g.tapped + 100);

        if (since < 4) {
            g.buttons = kInputSquare;
        } else {
            g.square = g.tick;
            std::printf("camtest: circlecam Square tapped, her quick item %d\n", g_combat.item);
        }
        return;
    }

    if (g.square >= 0 && g.tick >= g.square + 30) {
        float error = g.circle_error;
        bool  pre = g.tap_error > 2.0f && g_combat.lock < 0;
        bool  pass = pre && error < 0.35f;

        Verdict(pass, "%scamera %.2f rad from behind her at the tap, %.2f rad 90 ticks later (needs under 0.35)",
                pre ? "" : "precondition (turned away at the tap, no lock): ", g.tap_error, error);
    }
}

// coop-lockpress: a monster awake and lockable in front of her (Wake's, far from Toan) and copy B's own pad, read by the
// link as her controller is (its delay included), holds Circle for kPressHold ticks: the lock must be taken on the tick
// Circle reaches the game, while it is still held, and still be there when it lets go (that press is spent, so its hold
// is not a let-go). Half a second later a second hold of kSecondHold ticks must let go at retail's 24.
constexpr long kPressHold = 60;
constexpr long kSecondGap = 30;
constexpr long kSecondHold = 40;

void LockPress() {
    g.own_buttons = 0;

    if (g.monster < 0) {
        Wake();
        return;
    }

    // Nothing knocks her down (she would stop reading her pad), on both copies alike.
    g_partner.invincible = std::max(g_partner.invincible, 2);

    // Placed as lockcam places her: away from the monster, facing it, on a clear line.
    if (g.locked < 0) {
        if (g.tick >= g.placed + 600) {
            Verdict(false, "precondition: monster %d never came awake beside the partner", g.monster);
            return;
        }

        if (NowMonstorUnit->monster[g.monster].state != 2 || NowMonstorUnit->monster[g.monster].revealed == 0) {
            return;
        }

        float angle = 0.0f;
        float eye[4];
        float position[4];

        if (!CoopPartnerCamera(&angle, eye)) {
            Verdict(false, "precondition: the partner has no camera of her own");
            return;
        }

        NowMonstorUnit->chara[g.monster][0].GetPosition(position);
        const MONSTOR &monster = NowMonstorUnit->monster[g.monster];
        float          away = std::clamp(0.6f * monster.lock_range, 35.0f, 55.0f);
        bool           placed = false;

        // (coop-fx51d: on dun1-f1's floor neither of lockcam's two sides of monster 3 was clear; lockpress reads the press and
        // the lock, not the camera, so any side will do after those two)
        for (float side : {2.44f, -2.44f, 1.57f, -1.57f, 0.0f, 3.14f, 0.79f, -0.79f}) {
            float yaw = angle + PI + side;
            float spot[4] = {position[0] - away * sinf(yaw), position[1], position[2] - away * cosf(yaw), 1.0f};

            if (PathClear(position, spot)) {
                PutPartner(spot);
                g_partner.chara->frame->SetRotation(0.0f, yaw, 0.0f);
                placed = true;
                break;
            }
        }

        if (!placed || !Lockable(g.monster)) {
            // (coop-fx51d: which of the two: on dun1-f1 both lockcam and lockpress stopped here at tick 121 on monster 3)
            Verdict(false, "precondition: no clear spot %.0f from monster %d (lock range %.0f) to lock it from (%s; state %d hp %d revealed %d lockon %d)",
                    away, g.monster, monster.lock_range, placed ? "placed, not lockable" : "no clear path", (int) monster.state, (int) monster.hp,
                    (int) monster.revealed, (int) monster.lockon_enabled);
            return;
        }

        g.locked = g.tick;
        std::printf("camtest: lockpress partner put %.0f from monster %d, facing it\n", away, g.monster);
        return;
    }

    if (g.press_at < 0) {
        if (g.tick < g.locked + 30) {
            return;
        }

        if (g_combat.lock >= 0 || !Lockable(g.monster)) {
            Verdict(false, "precondition: before the press her lock is %d (needs none) and monster %d lockable %d", g_combat.lock, g.monster,
                    (int) Lockable(g.monster));
            return;
        }

        g.press_at = g.tick;
        std::printf("camtest: lockpress Circle held on copy B's own pad from tick %ld, nothing locked\n", g.tick);
    }

    long       since = g.tick - g.press_at;
    const bool circle = (g_combat.buttons & kInputCircle) != 0; // what CombatInput read this tick

    if (since < kPressHold || (since >= kPressHold + kSecondGap && since < kPressHold + kSecondGap + kSecondHold)) {
        g.own_buttons = kInputCircle;
    }

    if (g.seen_at < 0 && circle) {
        g.seen_at = g.tick;
        std::printf("camtest: lockpress Circle reached the game at tick %ld (%ld after the pad), lock %d\n", g.tick, g.tick - g.press_at,
                    g_combat.lock);
    }

    if (g.lock_at < 0 && g_combat.lock >= 0) {
        g.lock_at = g.tick;
        std::printf("camtest: lockpress locked monster %d at tick %ld, Circle %s\n", g_combat.lock, g.tick, circle ? "held" : "up");
    }

    if (g.seen_at >= 0 && g.release_at < 0 && !circle) {
        g.release_at = g.tick;
        g.held_at_release = g_combat.lock >= 0;
        std::printf("camtest: lockpress Circle let go at tick %ld, lock %d\n", g.tick, g_combat.lock);
    }

    if (g.release_at >= 0 && g.hold_at < 0 && since >= kPressHold + kSecondGap) {
        g.hold_at = g.tick;
    }

    if (g.hold_at >= 0 && g.hold_seen < 0 && circle) {
        g.hold_seen = g.tick;
    }

    if (g.hold_seen >= 0 && g.dropped_at < 0 && g_combat.lock < 0) {
        g.dropped_at = g.tick;
        std::printf("camtest: lockpress second hold let go %ld ticks in\n", g.tick - g.hold_seen);
    }

    if (since >= kPressHold + kSecondGap + kSecondHold + 40) {
        long reach = g.seen_at >= 0 ? g.seen_at - g.press_at : -1;
        long lag = g.lock_at >= 0 && g.seen_at >= 0 ? g.lock_at - g.seen_at : -1;
        long drop = g.dropped_at >= 0 ? g.dropped_at - g.hold_seen : -1;
        bool pre = g.seen_at >= 0 && g.hold_seen >= 0;
        bool pass = pre && g.lock_at >= 0 && lag <= 1 && (g.release_at < 0 || g.lock_at < g.release_at) && g.held_at_release && drop >= 22 &&
                    drop <= 26;

        Verdict(pass, "%sCircle reached the game %ld ticks after the pad; lock taken %ld ticks after it (needs 0-1, while held), %s when it "
                      "let go; second hold let go %ld ticks in (needs 22-26)",
                pre ? "" : "precondition (both holds reached the game): ", reach, lag, g.held_at_release ? "kept" : "gone", drop);
    }
}

// coop-lockhud: whether the player could hold a lock on monster i (setTargetCursor's keep: awake, within its lock
// range and not 80 above or below).
bool PlayerLockable(int i) {
    const MONSTOR &monster = NowMonstorUnit->monster[i];
    float          position[4];

    if (monster.state != 2 || monster.hp <= 0 || monster.revealed == 0 || monster.lockon_enabled == 0 ||
        monster.kind == MONSTER_KIND_NO_LOCK_ON) {
        return false;
    }

    NowMonstorUnit->chara[i][0].GetPosition(position);
    return DistVector(position, CharaMain.pos) < monster.lock_range && std::fabs(position[1] - CharaMain.pos[1]) < 80.0f;
}

// A clear spot away from position, and the yaw that faces position from it; false when none of eight ways is clear.
bool FacingSpot(const float *position, float away, float *spot, float *yaw) {
    for (int k = 0; k < 8; k++) {
        float y = 0.785398f * k;

        spot[0] = position[0] - away * sinf(y);
        spot[1] = position[1];
        spot[2] = position[2] - away * cosf(y);
        spot[3] = 1.0f;

        if (PathClear(position, spot)) {
            *yaw = y;
            return true;
        }
    }

    return false;
}

// coop-lockhud: lockhud. Xiao locks on a monster (Wake's, far from Toan) and Toan, put beside another far from her,
// locks on that one: each copy's HUD must show its own player's lock (the name and bar over it, its corners) and not
// the other player's corners, which are far from it (coop_lockhud.cpp). Sampled from what each copy drew; the steps
// and the verdict's tick are the same on both copies. coop-lockcolour: player 2's bar and corners are teal, player 1's
// retail's, on either copy (the other's corners when near).
void LockHud() {
    if (g.monster < 0) {
        Wake();
        return;
    }

    if (g.locked < 0) {
        if (g.tick >= g.placed + 600) {
            Verdict(false, "precondition: monster %d never came awake for the partner", g.monster);
            return;
        }

        if (NowMonstorUnit->monster[g.monster].state != 2 || NowMonstorUnit->monster[g.monster].revealed == 0) {
            return;
        }

        float position[4];
        float spot[4];
        float yaw = 0.0f;

        NowMonstorUnit->chara[g.monster][0].GetPosition(position);
        float away = std::clamp(0.6f * NowMonstorUnit->monster[g.monster].lock_range, 35.0f, 55.0f);

        if (!FacingSpot(position, away, spot, &yaw)) {
            Verdict(false, "precondition: no clear spot %.0f from monster %d", away, g.monster);
            return;
        }

        PutPartner(spot);
        g_partner.chara->frame->SetRotation(0.0f, yaw, 0.0f);

        if (!Lockable(g.monster)) {
            Verdict(false, "precondition: monster %d not lockable by the partner from %.0f", g.monster, away);
            return;
        }

        g_combat.lock = g.monster;
        g.locked = g.tick;

        // His monster: the lockable kind furthest from her, past twice its lock range.
        float best = 0.0f;

        for (int i = 0; i < 16; i++) {
            const MONSTOR &monster = NowMonstorUnit->monster[i];
            float          at[4];

            if (i == g.monster || monster.state == -1 || monster.revealed == 0 || monster.hp <= 0 || monster.lockon_enabled == 0 ||
                monster.kind == MONSTER_KIND_NO_LOCK_ON) {
                continue;
            }

            NowMonstorUnit->chara[i][0].GetPosition(at);
            float dist = DistXZ(at, g_partner.pos);

            if (dist > 2.0f * monster.lock_range + 60.0f && dist > best) {
                best = dist;
                g.monster2 = i;
            }
        }

        std::printf("camtest: lockhud partner locked monster %d from %.0f; player's monster %d, %.0f from her\n", g.monster, away, g.monster2, best);

        if (g.monster2 < 0) {
            Verdict(false, "precondition: no second lockable monster far from the partner");
        }

        return;
    }

    if (g.locked2 < 0) {
        if (g.placed2 < 0) {
            float position[4];
            float spot[4];
            float yaw = 0.0f;

            NowMonstorUnit->chara[g.monster2][0].GetPosition(position);
            float away = std::clamp(0.6f * NowMonstorUnit->monster[g.monster2].lock_range, 35.0f, 55.0f);

            if (!FacingSpot(position, away, spot, &yaw)) {
                Verdict(false, "precondition: no clear spot %.0f from monster %d for the player", away, g.monster2);
                return;
            }

            spot[1] += 10.0f;
            CharaMain.SetPosition(spot);
            sceVu0CopyVector(CharaMain.pos, spot);
            CharaMain.frame->SetRotation(0.0f, yaw, 0.0f);
            g.placed2 = g.tick;
            std::printf("camtest: lockhud player put %.0f from monster %d\n", away, g.monster2);
            return;
        }

        if (!PlayerLockable(g.monster2)) {
            if (g.tick >= g.placed2 + 400) {
                Verdict(false, "precondition: monster %d never lockable by the player", g.monster2);
            }

            return;
        }

        lockOnTargetNo = g.monster2;
        lockOnTargetFlag = 1;
        targetCursorCnt = 8.0f;
        g.locked2 = g.tick;
        std::printf("camtest: lockhud player locked monster %d at tick %ld\n", g.monster2, g.tick);
        return;
    }

    // Both locks held to the end (a lock let go is taken again while it can be), on both copies alike.
    if (g_combat.lock != g.monster && Lockable(g.monster)) {
        g_combat.lock = g.monster;
        g.relocks++;
    }

    if ((lockOnTargetFlag == 0 || lockOnTargetNo != g.monster2) && PlayerLockable(g.monster2)) {
        lockOnTargetNo = g.monster2;
        lockOnTargetFlag = 1;
        g.relocks2++;
    }

    // What this copy drew last frame, from two seconds after his lock.
    if (g.tick >= g.locked2 + 120) {
        bool                    own_p2 = NetplayLocalPlayer() == 2;
        int                     own = own_p2 ? g_combat.lock : lockOnTargetFlag != 0 ? lockOnTargetNo : -1;
        int                     other = own_p2 ? (lockOnTargetFlag != 0 ? lockOnTargetNo : -1) : g_combat.lock;
        const LockHudShown     &s = LockHudLastShown();
        float                   at[4];
        const float            *me = own_p2 ? g_partner.pos : CharaMain.pos;
        int                     own_marker = own_p2 ? s.partner_marker : s.player_marker;
        int                     other_marker = own_p2 ? s.player_marker : s.partner_marker;
        bool                    far = false;

        if (own >= 0 && other >= 0) {
            NowMonstorUnit->chara[other][0].GetPosition(at);
            far = DistVector(at, (float *) me) > NowMonstorUnit->monster[other].lock_range;
        }

        // coop-lockcolour: each bar and corner drawn this frame in its player's colour (player 2's teal).
        bool colour = (s.bar < 0 || !s.bar_teal) && (s.player_marker < 0 || !s.player_teal) && (s.partner_marker < 0 || s.partner_teal);

        g.colour_n += s.bar >= 0 || s.player_marker >= 0 || s.partner_marker >= 0;
        g.bad_colour += !colour;

        if (!colour && g.first_bad[0] == 0) {
            std::snprintf(g.first_bad, sizeof g.first_bad, "tick %ld own %d: colours bar %d teal %d, corners p1 %d teal %d, p2 %d teal %d", g.tick, own,
                          s.bar, s.bar_teal ? 1 : 0, s.player_marker, s.player_teal ? 1 : 0, s.partner_marker, s.partner_teal ? 1 : 0);
        }

        if (!far) {
            g.hud_near++;
        } else {
            bool name = s.name == own;
            bool bar = s.bar == own;
            bool mine = own_marker == own;
            bool theirs = other_marker == -1 && (!own_p2 || s.preview == -1);

            g.hud_n++;
            g.hud_ok += name && bar && mine && theirs && colour;
            g.bad_name += !name;
            g.bad_bar += !bar;
            g.bad_own += !mine;
            g.bad_other += !theirs;

            if (!(name && bar && mine && theirs) && g.first_bad[0] == 0) {
                std::snprintf(g.first_bad, sizeof g.first_bad, "tick %ld own %d other %d: name %d bar %d corners p1 %d p2 %d preview %d", g.tick, own,
                              other, s.name, s.bar, s.player_marker, s.partner_marker, s.preview);
            }
        }
    }

    if (g.tick >= g.locked2 + 420) {
        bool pre = g.hud_n >= 150;
        bool pass = pre && g.hud_ok * 10 >= g.hud_n * 9 && g.bad_colour * 10 <= g.colour_n;

        Verdict(pass, "%sown lock shown, other's far lock hidden on %d/%d frames (other near %d; wrong name %d, bar %d, own corners %d, other shown %d, "
                      "colour %d of %d; relocks %d %d)%s%s",
                pre ? "" : "precondition (150+ frames with both locked, the other far): ", g.hud_ok, g.hud_n, g.hud_near, g.bad_name, g.bad_bar,
                g.bad_own, g.bad_other, g.bad_colour, g.colour_n, g.relocks, g.relocks2, g.first_bad[0] != 0 ? "; first wrong: " : "", g.first_bad);
    }
}

// coop-dungeonp2: drops. Xiao is put 190 from Toan and an item dropped 25 beyond her, over 200 from
// Toan: on her own copy (B) it must draw (CRandomItem::Draw, within 200 of the one the floor is drawn
// round); on Toan's copy (A) it must not, as retail. Both copies drop it on the same tick.
void Drops() {
    if (!Ready()) {
        return;
    }

    if (g.dropped < 0) {
        if (g.placed < 0) {
            // On a map event (a chest, a gate: on the floor) 215 or more from the player, the nearest such; a point in
            // the open on a line from the player could be over a gap, and she fell back beside him.
            int   chosen = -1;
            float best = 1.0e9f;

            for (int i = 0; i < 48; i++) {
                const float dist = DistXZ(NowDngMap->events[i].pos, CharaMain.pos);

                if (NowDngMap->events[i].kind != 0 && dist >= 215.0f && dist < best) {
                    best = dist;
                    chosen = i;
                }
            }

            if (chosen < 0) {
                Verdict(false, "precondition: no map event 215 or more from the player");
                return;
            }

            float target[4];

            for (int i = 0; i < 4; i++) {
                target[i] = NowDngMap->events[chosen].pos[i];
            }

            target[1] += 10.0f;
            PutPartner(target);
            g.placed = g.tick;
            return;
        }

        if (g.tick < g.placed + 60) { // landed
            return;
        }

        float dx = g_partner.pos[0] - CharaMain.pos[0];
        float dz = g_partner.pos[2] - CharaMain.pos[2];
        float len = std::hypot(dx, dz);

        if (len < 1.0f) {
            Verdict(false, "precondition: the partner stayed on the player");
            return;
        }

        for (int i = 0; i < 4; i++) {
            g.drop[i] = g_partner.pos[i];
        }

        g.drop[0] += dx / len * 10.0f; // just beyond her, away from the player
        g.drop[2] += dz / len * 10.0f;
        RandomItem->Set(g.drop, 99, 10, -1);
        g.dropped = g.tick;
        std::printf("camtest: drops item put %.0f from the player, %.0f from the partner (tick %ld)\n", DistXZ(g.drop, CharaMain.pos),
                    DistXZ(g.drop, g_partner.pos), g.tick);
        return;
    }

    if (g.tick < g.dropped + 135) {
        return;
    }

    float player = DistXZ(g.drop, CharaMain.pos);
    float partner = DistXZ(g.drop, g_partner.pos);
    bool  pre = player > 200.0f && partner < 200.0f;
    bool  own = NetplayLocalPlayer() == 2;
    bool  pass = pre && (own ? g.drawn > 0 : g.drawn == 0);

    Verdict(pass, "%sitem %.0f from the player, %.0f from the partner, drawn on %d frames on copy %s (want %s)",
            pre ? "" : "precondition (over 200 from the player, within 200 of the partner): ", player, partner, g.drawn,
            own ? "B" : "A", own ? "some" : "none");
}

// A session reset (resync or reconnect) once the two cameras point different ways: copy B's camera
// module comes back as a fresh copy's would, then both copies reset theirs at the same tick, as
// CoopResetForSession does. The link's checksum (which holds the partner's camera) must stay equal.
void Resync() {
    static const char *at = TestEnv("DC_COOP_RESYNC_TICK");
    long               tick = at != nullptr ? std::atol(at) : 1000;

    if (g.tick == tick) {
        float angle = 0.0f;
        float eye[4];
        bool  own = CoopPartnerCamera(&angle, eye);
        float apart = std::fabs(std::remainder(angle - MainCamera__4.angle, 2.0f * PI));

        if (!own || apart < 0.3f) {
            Verdict(false, "precondition: the cameras point the same way (%.2f rad apart) before the reset", apart);
            return;
        }

        if (NetplayLocalPlayer() == 2) {
            CameraResetForSession();
        }

        if (!CoopMutate("no_camera_reset")) {
            CameraResetForSession();
        }

        std::printf("camtest: resync reset at tick %ld, cameras %.2f rad apart\n", g.tick, apart);
    } else if (g.tick >= tick + 700) {
        Verdict(true, "reset at tick %ld, play went on 700 ticks", tick);
    }
}

// coop-p2atla: an Atla on the floor still to take (its event slot), the first, or -1.
int FreeAtla() {
    for (int i = 0; i < 48; i++) {
        if (NowDngMap->events[i].kind == DNG_EVENT_ATRA && NowDngMap->atra[NowDngMap->events[i].index].used != 0) {
            return i;
        }
    }

    return -1;
}

// coop-p2atla: the Atla in event slot i put back on the floor (the floor has fewer Atlas than the check
// takes), on both copies alike.
void RearmAtla(int i) {
    NowDngMap->atra[NowDngMap->events[i].index].used = 1;
    NowDngMap->events[i].kind = DNG_EVENT_ATRA;
}

// coop-p2atla: player 2's Cross with her standing on the Atla in event slot i, as Walk's Cross does it.
void PartnerCrossAt(int i) {
    float target[4];

    sceVu0CopyVector(target, NowDngMap->events[i].pos);
    target[0] += std::min(10.0f, NowDngMap->events[i].radius * 0.5f);
    PutPartner(target);
    MenuNotePrompt(Interact());
}

// coop-p2atla (Jep 2026-10-25, notes 27-29, 32): Xiao takes an Atla with her Cross; pad 2 swaps to Toan (the
// player becomes Xiao) and takes one as Toan; the player, Toan again, takes one through retail's presentation.
// Each take: the ball gone and the task still play (no presentation) on both copies; copy B draws from her
// own camera through her take and through his presentation. Copy A's prompt cover is the test script's.
void P2Atla() {
    static const char *const kStep[] = {"", "Xiao's take", "swap to Toan", "Toan's take", "swap back", "his presentation"};
    long                     since = g.tick - g.atla_at;

    switch (g.atla_step) {
        case 0: {
            if (!Ready()) {
                return;
            }

            g.atla_event = FreeAtla();

            if (g.atla_event < 0 || g_partner.loaded_chara != CHARA_XIAO || UserStatus->party_size < 2) {
                Verdict(false, "precondition: an Atla on the floor (event %d), Xiao the partner (%d), a party of two (%d)", g.atla_event,
                        g_partner.loaded_chara, (int) UserStatus->party_size);
                return;
            }

            std::printf("camtest: p2atla Atla event %d ball %d at (%.0f %.0f %.0f), radius %.0f\n", g.atla_event,
                        NowDngMap->events[g.atla_event].index, NowDngMap->events[g.atla_event].pos[0], NowDngMap->events[g.atla_event].pos[1],
                        NowDngMap->events[g.atla_event].pos[2], NowDngMap->events[g.atla_event].radius);
            PartnerCrossAt(g.atla_event);
            break;
        }
        case 1:
        case 3: {
            if (since == 2) {
                int  ball = NowDngMap->events[g.atla_event].index;
                bool taken = NowDngMap->atra[ball].used == 0 && NowDngMap->events[g.atla_event].kind == DNG_EVENT_NONE;

                std::printf("camtest: p2atla %s: ball %d used %d, task %d, partner %d, player %d\n", kStep[g.atla_step], ball,
                            NowDngMap->atra[ball].used, (int) gameTask, g_partner.loaded_chara, (int) UserStatus->cur_chara);

                if (!taken || gameTask != GAME_TASK_PLAY) {
                    Verdict(false, "%s: ball %d used %d, task %d (want taken, task %d)", kStep[g.atla_step], ball, NowDngMap->atra[ball].used,
                            (int) gameTask, GAME_TASK_PLAY);
                }

                return;
            }

            // After Toan's take, 200 ticks of play (the swap's crash came as the presentation started). (coop-opt7 step 3: her
            // presentation is her copy's engine, held until her own close, the row's Crosses: the swap waits for its end.)
            if (since < (g.atla_step == 1 ? 30 : 200) || !Playing() || PartnerPresenting()) {
                return;
            }

            int chara = g.atla_step == 1 ? CHARA_XIAO : CHARA_TOAN;

            if (!PromptChangePlayer(chara)) {
                Verdict(false, "precondition: the player cannot become %d", chara);
                return;
            }

            g_partner.choice = chara == CHARA_XIAO ? CHARA_TOAN : CHARA_XIAO;
            std::printf("camtest: p2atla %s: the player becomes %d, pad 2 takes %d (tick %ld)\n", kStep[g.atla_step + 1], chara,
                        g_partner.choice, g.tick);
            break;
        }
        case 2:
        case 4: {
            int partner = g.atla_step == 2 ? CHARA_TOAN : CHARA_XIAO;

            if (since > 400) {
                Verdict(false, "precondition: %s not done 400 ticks later (partner %d, player %d, task %d)", kStep[g.atla_step],
                        g_partner.loaded_chara, (int) UserStatus->cur_chara, (int) gameTask);
                return;
            }

            if (!Playing() || g_partner.loaded_chara != partner || UserStatus->cur_chara == partner || since < 30) {
                return;
            }

            int next = FreeAtla();

            if (next < 0) {
                RearmAtla(g.atla_event);
                next = g.atla_event;
            }

            g.atla_event = next;

            if (g.atla_step == 2) {
                PartnerCrossAt(next);
            } else {
                // The player's Cross on it, as DunMoveChara's does.
                iventActive = next;
                BtActStatus.motion_no = 0;
                gameTask = GAME_TASK_ATRA_GET_INIT;
                std::printf("camtest: p2atla his presentation starts at event %d (tick %ld)\n", next, g.tick);
            }

            break;
        }
        case 5: {
            if (since < 150) {
                return;
            }

            bool own = NetplayLocalPlayer() == 2;
            bool pass = g.scene_frames >= 100 && (!own || (g.scene_hers == g.scene_frames && g.take_hers == g.take_frames && g.take_frames >= 25));

            Verdict(pass, "Xiao and Toan took theirs; copy %s drew %d of %d frames of her take and %d of %d of his presentation from her camera%s",
                    own ? "B" : "A", g.take_hers, g.take_frames, g.scene_hers, g.scene_frames, own ? " (want all)" : " (not checked on A)");
            return;
        }
        default:
            return;
    }

    g.atla_step++;
    g.atla_at = g.tick;
}

} // namespace

bool CoopMutate(const char *name) {
    static const char *mutate = TestEnv("DC_COOP_MUTATE");

    if (mutate == nullptr) {
        return false;
    }

    std::string list = std::string(",") + mutate + ",";
    return list.find(std::string(",") + name + ",") != std::string::npos;
}

void CamTestTick() {
    if (Test() == nullptr || g.done) {
        return;
    }

    // An online host (peer any) and a joiner from the title's Join (DC_NETPLAY set) are not linked until
    // the join: the check starts then, and the test's own timeout covers a join that never comes.
    static const bool online = TestEnv("DC_NETPLAY") != nullptr;

    if (NetplayLocalPlayer() == 0 && online) {
        return;
    }

    g.tick++;

    // The mode first: linked, co-op on, the cameras setting on, the partner on the floor.
    const dc::platform::Config &config = dc::platform::ConfigGet();

    // pullback's own result tells a camera each (no leash) from a shared camera, so it runs either way.
    if (NetplayLocalPlayer() == 0 || !config.coop || (!IndependentCamerasSetting(config) && !Is("pullback"))) {
        Verdict(false, "precondition: not linked with independent cameras on");
        return;
    }

    if (Is("cameras")) {
        Cameras();
    } else if (Is("pullback")) {
        Pullback();
    } else if (Is("intro")) {
        Intro();
    } else if (Is("sfx")) {
        Sfx();
    } else if (Is("wake") || Is("wakedown")) {
        Wake();
    } else if (Is("drops")) {
        Drops();
    } else if (Is("resync")) {
        Resync();
    } else if (Is("lockcam")) {
        LockCam();
    } else if (Is("lockhud")) {
        LockHud();
    } else if (Is("p2atla")) {
        P2Atla();
    } else if (Is("circlecam")) {
        CircleCam();
    } else if (Is("strafe")) {
        Strafe(); // coop-strafe
    } else if (Is("lockpress")) {
        LockPress(); // coop-lockpress
    } else {
        Verdict(false, "unknown check");
    }
}

bool CamTestPad(unsigned short &buttons) {
    if (!Is("circlecam") || g.done || g.turned < 0) {
        return false;
    }

    buttons = g.buttons;
    return true;
}

void CamTestOwnPad(int player, unsigned short &buttons) {
    if (player == 2 && Is("lockpress") && !g.done) {
        buttons |= g.own_buttons;
    }
}

void CamTestStick(float &lx, float &ly) {
    if (!Is("strafe") || g.done || g.phase < 0) {
        return;
    }

    lx = g.stick[0];
    ly = g.stick[1];
}

void CamTestView(CCameraFollow *drawn, CCameraFollow *shown) {
    if (Test() == nullptr || g.done || NetplayLocalPlayer() == 0 || !g_partner.visible) {
        return;
    }

    if (Is("cameras") && Playing()) {
        float fx = drawn->ref[0] - drawn->pos[0];
        float fz = drawn->ref[2] - drawn->pos[2];
        float length = std::hypot(fx, fz);

        if (length > 0.0f) {
            g.forward[0] = fx / length;
            g.forward[1] = fz / length;
            g.have_forward = true;
        }

        if (DistXZ(CharaMain.pos, g_partner.pos) >= 100.0f) {
            g.follow_n++;
            g.follow_bad += DistXZ(drawn->ref, LocalPosition()) > 45.0f;
        }
    }

    // coop-p2atla: whose camera copy B draws from (its own, when the drawn one is not the shown one) through her
    // first take and through his presentation; copy A counts his presentation's frames.
    if (Is("p2atla")) {
        bool hers = NetplayLocalPlayer() == 2 && drawn != shown;

        if (g.atla_step == 1 && g.tick - g.atla_at <= 30) {
            g.take_frames++;
            g.take_hers += hers;
        } else if (g.atla_step == 5 && (gameTask == GAME_TASK_ATRA_GET_INIT || gameTask == GAME_TASK_ATRA_GET_LOOP)) {
            g.scene_frames++;
            g.scene_hers += hers;
        }
    }

    // Copy B shows the opening as the shown camera moved by where the partner stands from the player, so
    // it frames the partner as copy A frames the player.
    if (Is("intro") && FloorIntro() && NetplayLocalPlayer() == 2) {
        float error = 0.0f;
        float shift = 0.0f;
        float player[4];

        DrawnPlayer(player);

        for (int i = 0; i < 3; i++) {
            float want = g_partner.pos[i] - player[i];

            error = std::max({error, std::fabs(drawn->pos[i] - shown->pos[i] - want), std::fabs(drawn->ref[i] - shown->ref[i] - want)});
            shift = std::max(shift, std::fabs(want));
        }

        g.moved_n++;
        g.moved_ok += shift > 5.0f && error < 1.0f;
    }
}

void CoopPartnerSound(int se_no, float *position, CoopSound kind) {
    if (CoopMutate("no_sfx")) {
        return;
    }

    SndSePlay(se_no, position, 50.0f, 300.0f);
    (kind == kCoopSoundThrow ? g.throws : kind == kCoopSoundHit ? g.hits : g.hurts)++;
}

// coop-dungeonp2: CRandomItem::Draw tells the drops check it drew the check's item.
void CamTestDropDrawn(const float *pos) {
    if (Is("drops") && g.dropped >= 0 && g.tick > g.dropped + 45 && !g.done && std::fabs(pos[0] - g.drop[0]) < 1.0f && std::fabs(pos[2] - g.drop[2]) < 1.0f) {
        g.drawn++;
    }
}

// coop-warmreset (WS7): a camera test's second job on a warm pair found the first one's verdict (done) and never said its own.
void coop::CamtestResetForJob() {
    g = State{};
}
