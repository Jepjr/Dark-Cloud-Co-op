#include "coop/game_ns.hpp"
#include "coop/coop_opener.hpp" // coop-opt7
#include "coop/coop.hpp"
#include "coop/coop_perf.hpp"
#include "coop/coop_writeguard.hpp"
#include "coop/netplay.hpp"

#include <SDL3/SDL.h>
#include <libvu0.h>

#include <algorithm>
#include <optional> // coop-tripwires3
#include <string>
#include <vector>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

#include "btactstatus.hpp"
#include "text/caption_text.hpp"
#include "rect.hpp"
#include "shot_freefuncs.hpp"
#include "snd.hpp"
#include "btitem.hpp"
#include "camerafollow.hpp"
#include "character.hpp"
#include "cloth.hpp" // coop-entrance9: the poncho put on at rest (CCloth::Clear)
#include "clothread.hpp"
#include "collision.hpp"
#include "collisiondata.hpp"
#include "dataalloc.hpp"
#include "dataread.hpp"
#include "dataset.hpp" // candfix2: WorkBuffer (CameraLineClear)
#include "dranmapfield.hpp"
#include "dun/gameloop.hpp"
#include "dispctrl.hpp" // coop-openercam: DispFade__3
#include "dngmessageman.hpp"
#include "dungeoneventdata.hpp"
#include "dungeoneventman.hpp"
#include "dungeonmap.hpp"
#include "edit.hpp"      // coop-arrivalcam: EdEventInfo
#include "mathconst.h"   // coop-arrivalcam: PI_SHORT, TWO_PI
#include "sysmes.hpp"
#include "frame.hpp"
#include "frameattr.hpp"
#include "framevu1.hpp" // coop-desync1116: PoseFrame walks the draw's frames
#include "gamepad.hpp"
#include "gameutil.hpp"
#include "hitvalue.hpp"
#include "itemdata.hpp"
#include "menu_draw.hpp"
#include "mglib.hpp"
#include "monstorunit.hpp"
#include "npcharacter.hpp" // coop-entrance2: NPCUnit (the Moon Sea beam actor)
#include "input/mouse_collision.hpp"
#include "shot_utils.hpp"
#include "platform/config.hpp"
#include "platform/input.hpp"
#include "texture.hpp"
#include "textureanime.hpp"
#include "userstatus.hpp"

#include "coop/coop_core.hpp"
#include "coop/coop_camera.hpp"
#include "coop/coop_camtest.hpp"
#include "coop/coop_combat.hpp"
#include "coop/coop_interact.hpp" // coop-openercam: OpeningCamera
#include "coop/coop_party.hpp"
#include "coop/coop_actor.hpp" // coop-pt1109b: ActorLocalIndex
#include "coop/coop_worlds.hpp" // coop-fpvisible
#include "coop/netplay_test.hpp" // coop-arrivalfull: NetplayMutated
#include "main.hpp"               // coop-arrivalfull: GAME_MODE_DUNGEON
#include "ebattle.hpp"            // coop-wave3: EdMoveCharaInfo (the town walk's camera, the steer row)
#include "editloop.hpp"           // coop-wave3: MainCamera, EditCamera
#include "editmapscript.hpp"      // coop-wave3: GameMode (the Georama)
#include "coop/coop_town.hpp"      // coop-wave3: CoopTownPartnerCameraAngle
#include "coop/coop_georama.hpp"   // coop-integrate1118: CoopP2GeoramaCameraAngle (her Georama's steer row)
#include "render/gs_state.hpp"

namespace dc::game {
int keyCtrl(float x, float y, MOTION_INFO *motion);

extern CFrameVu1 *CharaFrame;
extern int        statusAlarmRate;
extern s32        gameTask;
extern s32        mode; // coop-arrivalfull
extern s32        iventMarker;

// autoCamTrial's inputs (ps2/src/dun/gameloop.cpp).
extern float         inputH1;
extern float         camera_far_dist_limmit;
extern sceVu0FVECTOR ref_off;
extern s32           lockOnTargetNo;

// The minimap's switch (SetMIniMapStatus): the presentations turn it off (ps2/src/dun/gameloop.cpp).
extern s32 infoMap;
extern s32 infoMapOld;

unsigned int PortRandState();
CCameraFollow *CoopRoomCamera(); // coop-wave3 test hook (edit_in.cpp: the room's camera)
void         PortRandSetState(unsigned int state);
void         EdGetFadeColor(float *colour); // editloop3: the event fade (coop-camflash2: CoopScreenBlack)
} // namespace dc::game

namespace {
CFrame *StairDoor(); // (coop-entrance2 round 7, below: the stairwell's door, in_door)

// coop-entrance2 round 7: the walk-in's door, as its script slides it: the Sun and Moon Temple's two halves (in_door_r and in_door_l,
// slid along x from 0 shut to 15 open: script 100's functions 0x25868 open and 0x26268 shut, the shut called as the lead stands at
// 42 inside, before he walks on), else the stairwell's in_door (y). Its pieces in out (2 at most), the axis they slide on; 0 none.
// g_door_script: the script's own value (a frame value other than the hold's last write is the script's), g_door_written the hold's.
float g_door_script = 0.0f;
float g_door_written = 1e9f;
// coop-entrance8 (Jep 11-12, temple clip e55: "the door animation is now worse than the game's own: it warps mid-animation and then
// slides wrong afterwards; play the game's door close animation UNCHANGED, just started later"): the hold wrote the halves on the
// tick while the script's close loop wrote them too, so the drawn frame flipped between the two. Now nothing writes the halves but
// the script; the delay is draw-only: each tick the walk-in says what the door is drawn at (g_door_draw, 1e9: as the game has it),
// set just before the floor is drawn and put back after (CoopViewBegin/End). Until the last walker reaches where the lead stood as
// the script began to shut it, it stands fully open (as retail has it before its close); then the script's own close, recorded tick
// by tick, plays from its first frame in order, one a tick. DC_COOP_MUTATE=entrance_door_lead_only (twin): the game's door.
float g_door_draw = 1e9f;
float g_door_kept[2][3] = {};
int   g_door_kept_n = 0;
// coop-entrance9 (Jep 11-12, clip e57: "the door STILL WARPS in the middle of the cutscene"): the two halves slide opposite ways
// (in_door_r to +15, in_door_l to -15: probe e59d, the script's own positions 15.0/-15.0 open); the draw override put both at
// +15, so the left half jumped onto the right's side the draw it began (draw 142: 15.0/-15.0 then 15.0/15.0). Each half now
// takes the door's value on its own side (g_door_side: +1, -1). And, every draw of the temple's opening, each half as drawn:
// its largest step from the draw before, its side changes, its re-openings once its close began (x-entrance reads them).
// DC_COOP_MUTATE=door_halves_one_side (twin): both halves the same value, as e57.
float g_door_side[2] = {1.0f, -1.0f};
struct DoorDrawn {
    int   draws = 0;
    float last[2] = {0.0f, 0.0f};
    float most_step[2] = {0.0f, 0.0f};
    int   side_changes[2] = {0, 0};
    int   reopened[2] = {0, 0};
    bool  closing[2] = {false, false};
} g_door_drawn;

int EntranceDoors(CFrame **out, int *axis) {
    static char right[] = "in_door_r";
    static char left[] = "in_door_l";

    if (mode == GAME_MODE_DUNGEON && NowDngMap != nullptr && selectMapNo == 3) {
        out[0] = NowDngMap->GetFrameSearch(right);
        out[1] = NowDngMap->GetFrameSearch(left);
        *axis = 0;
        return out[0] != nullptr && out[1] != nullptr ? 2 : 0;
    }
    out[0] = StairDoor();
    *axis = 1;
    return out[0] != nullptr ? 1 : 0;
}
} // namespace

namespace coop {

namespace {
unsigned int g_draw_rng = 0x2f6b9d43u; // the draw stream's state (per copy; not in the state set or the checksum)
unsigned int g_draw_rng_step = 0;     // the lockstep generator's, while a draw holds the stream
int          g_draw_rng_depth = 0;
} // namespace

void DrawRngEnter() {
    if (g_draw_rng_depth == 0) {
        WriteGuardEnter(kGuardDraw, nullptr, __builtin_return_address(0)); // coop-review1109 (a test's: DC_QA_WRITE_GUARD)
    }
    if (g_draw_rng_depth++ != 0 || CoopMutate("draw_rng_shared")) {
        return;
    }
    g_draw_rng_step = PortRandState();
    PortRandSetState(g_draw_rng);
}

bool DrawRngHeld() {
    return g_draw_rng_depth > 0 && !CoopMutate("draw_rng_shared");
}

void DrawRngLeave() {
    if (g_draw_rng_depth == 1) {
        WriteGuardLeave(kGuardDraw); // coop-review1109
    }
    if (g_draw_rng_depth == 0 || --g_draw_rng_depth != 0 || CoopMutate("draw_rng_shared")) {
        return;
    }
    g_draw_rng = PortRandState();
    PortRandSetState(g_draw_rng_step);
}

// Whether the player could walk from from to to: the way DunMoveChara moves the player (MoveCheck
// against the polygons round each eight-unit step), stopped short by a wall or a tree. Used to keep
// the camera's look point out of what stands between the two players.
bool PathClear(const float *from, const float *to) {
    float dx = to[0] - from[0];
    float dz = to[2] - from[2];
    float length = sqrtf(dx * dx + dz * dz);

    if (length < 1.0f) {
        return true;
    }

    int           steps = (int) ceilf(length / 8.0f);
    sceVu0FVECTOR pos = {from[0], from[1], from[2], 1.0f};
    sceVu0FVECTOR step = {dx / steps, 0.0f, dz / steps, 0.0f};

    for (int i = 0; i < steps; i++) {
        CCPoly       *polys;
        int           count = GatherPolys(pos, &polys);
        sceVu0FVECTOR moved;
        MoveCheckInfo info;

        MoveCheck(pos, step, moved, &info, polys, count, 1);

        float mx = moved[0] - pos[0];
        float mz = moved[2] - pos[2];

        if (mx * step[0] + mz * step[2] < 0.8f * (step[0] * step[0] + step[2] * step[2])) {
            return false;
        }

        pos[0] = moved[0];
        pos[2] = moved[2];
    }

    return true;
}

// candfix2 (camtest's circlecam): whether a camera line from from to to meets nothing autoCamTrial pulls a camera in by: the
// floor's camera collision (its parts' camera_collision frames, the cells within 240 of from on a cell-built floor), as
// autoCamTrial gathers it, hit by CheckHits. A walk's walls (PathClear) are not the camera's: trees and the stairs' wall
// stopped the camera where she could walk. For a test hook (both copies run it alike); WorkBuffer is the scratch
// autoCamTrial itself starts over each call.
bool CameraLineClear(const float *from, const float *to) {
    if (NowDngMap == nullptr || WorkBuffer == nullptr) {
        return true;
    }

    CBoxVu0 box;

    for (int i = 0; i < 3; i++) {
        box.max[i] = std::max(from[i], to[i]);
        box.min[i] = std::min(from[i], to[i]);
    }

    WorkBuffer->used = 0;
    CCPoly *poly = (CCPoly *) WorkBuffer->Alloc(0x280);
    int     count = 0;

    if (NowDngMap->map_type != 1) {
        for (int i = 0; NowDngMap->parts[i].frame[0] != nullptr; i++) {
            CDungeonParts *part = &NowDngMap->parts[i];
            CFrame        *frame = part->camera_collision;

            if (frame == nullptr) {
                continue;
            }
            int turn = (int) part->frame_turn[0] + part->camera_collision_turn;

            turn = turn > 3 ? turn - 3 : turn;
            turn = turn == 3 ? -1 : turn;
            frame->SetRotation(0.0f, PI * (-90.0f * turn) / 180.0f, 0.0f);
            frame->SetPosition(part->frame_offset[0]);
            count += frame->PickUpNearPoly(&poly[count], box);
        }
    } else {
        for (int j = 0; j < 20; j++) {
            for (int i = 0; i < 20; i++) {
                const MAP_CELL &cell = NowDngMap->cells[i + j * 20];
                const int       parts_no = cell.parts_no;
                CFrame         *frame = parts_no == -1 ? nullptr : NowDngMap->parts[parts_no].camera_collision;
                const float     delta[4] = {160.0f * i - from[0], 0.0f, 160.0f * j - from[2], 0.0f};

                if (frame == nullptr || !(DistVector(const_cast<float *>(delta)) <= 240.0f)) {
                    continue;
                }
                int turn = cell.direction + NowDngMap->parts[parts_no].camera_collision_turn;

                turn = turn > 3 ? turn - 3 : turn;
                turn = turn == 3 ? -1 : turn;
                frame->SetRotation(0.0f, PI * (-90.0f * turn) / 180.0f, 0.0f);
                frame->SetPosition(160.0f * i, 0.0f, 160.0f * j);
                count += frame->PickUpNearPoly(&poly[count], box);
            }
        }
    }

    int   hit_poly[32];
    float hit_point[32][4];

    return CheckHits(poly, count, const_cast<float *>(from), const_cast<float *>(to), 32, hit_poly, hit_point, 1, 0) <= 0;
}

} // namespace coop

using namespace coop;

void CoopBeforeMotion() {
    CoopPerfScope perf(kCoopPerfStep); // coop: perf
    ArrivalWalkPlayerOne(); // coop-arrivalfull: player 1's arrival, played again once his catch-up is over
    // Diagnostic (DC_COOP_CAMERA=player): the player's camera alone, as without co-op.
    static const bool player_camera = TestEnv("DC_COOP_CAMERA") != nullptr;

    if (player_camera || !g_partner.visible || BtActStatus.camera_hold != 0 || lockOnTargetFlag != 0 || IndependentCameras()) {
        return;
    }

    CCameraFollow *camera = NowCamera__3;
    float          dx = g_partner.pos[0] - CharaFrame->position[0];
    float          dz = g_partner.pos[2] - CharaFrame->position[2];
    float          apart = sqrtf(dx * dx + dz * dz);
    float          follow_x = camera->follow[0];
    float          follow_z = camera->follow[2];

    // The camera looks at the point between the two, and draws back as they part, by up to 120
    // units past the player's own distance; the leash keeps the partner within reach of that.
    // The shift eases in while the player could walk straight to the partner, and back out when a tree
    // or a wall stands between them, so the look point never sits in one and neither player is hidden
    // behind one.
    if (--g_partner.midpoint_wait <= 0) {
        g_partner.midpoint_clear = PathClear(CharaFrame->position, g_partner.pos);
        g_partner.midpoint_wait = 6;
    }

    g_partner.camera_shift = std::clamp(g_partner.camera_shift + (g_partner.midpoint_clear ? 0.05f : -0.1f), 0.0f, 1.0f);
    camera->follow[0] += 0.5f * dx * g_partner.camera_shift;
    camera->follow[2] += 0.5f * dz * g_partner.camera_shift;
    g_partner.camera_distance = camera->GetDistance();
    g_partner.camera_widened = true;

    float wanted = std::max(g_partner.camera_distance, g_partner.camera_distance + std::clamp(0.7f * (apart - 30.0f), 0.0f, 120.0f));

    // Both go through the mouse zoom's wall test (camera_zoom.cpp): the eye, its look ray and the way
    // the camera eases there must clear the floor's walls. The distance comes in as far as they need;
    // where even the player's own distance is blocked round the point between the two, or the geometry
    // cannot be checked in the work budget, the camera stays the player's for this tick.
    static std::vector<CCPoly> polys;
    CBoxVu0                    box{};
    float                      radius = std::max({std::fabs(camera->distance), wanted,
                                                  std::hypot(camera->pos[0] - camera->ref[0], camera->pos[2] - camera->ref[2]),
                                                  std::hypot(camera->next_pos[0] - camera->next_ref[0], camera->next_pos[2] - camera->next_ref[2])}) +
                    20.0f;

    for (int i = 0; i < 3; ++i) {
        box.min[i] = std::min({camera->pos[i], camera->next_pos[i], camera->ref[i], camera->next_ref[i], camera->follow[i]}) - radius - std::fabs(camera->height);
        box.max[i] = std::max({camera->pos[i], camera->next_pos[i], camera->ref[i], camera->next_ref[i], camera->follow[i]}) + radius + std::fabs(camera->height);
    }

    int   count = dc::input::MouseCameraPolys(*NowDngMap, box, polys);
    float allowed = count < 0 ? 0.0f : dc::input::MouseCameraClampDistance(*camera, wanted, polys.data(), count, 5.0f);

    if (allowed < g_partner.camera_distance - 0.5f) {
        camera->follow[0] = follow_x;
        camera->follow[2] = follow_z;
        allowed = g_partner.camera_distance;
    }

    static int report = 0;

    if (allowed < wanted - 1.0f && --report <= 0 && CoopDebug()) {
        std::printf("coop-debug: camera wants %.0f, walls allow %.0f (player's %.0f)%s\n", wanted, allowed, g_partner.camera_distance,
                    camera->follow[0] == follow_x && camera->follow[2] == follow_z ? ", on the player" : "");
        report = 30;
    }

    camera->SetDistance(allowed);
}

// coop: cameras
namespace {

// DunMoveChara's limit on the height the stick gives the camera.
constexpr float kCameraHeightMax = 30.0f;

// How far the partner may be from where its camera looks before the camera cuts to it.
constexpr float kCameraCut = 200.0f;

// The partner's own camera: built as MainCamera__4 is, taken from the player's when it first follows
// the partner, then stepped from pad 2's stick on both copies, as the player's is from pad 1's.
struct PartnerCamera {
    CCameraFollow *camera = nullptr;
    bool           ready = false;
    bool           intro = false;
    bool           clear_at_cut = false; // coop-dungeonreds: a test's put asks its next cut for a clear start (PartnerCameraClearAtCut)
    int            idle = 0;
    sceVu0FVECTOR  last = {0.0f, 0.0f, 0.0f, 1.0f};
};

PartnerCamera g_camera;

// What DunMainDraw draws from this tick: the camera it found, and, for the floor's opening on the
// partner's copy, that camera moved over onto the partner.
bool           g_view_active = false;
bool           g_view_swapped = false;
CCameraFollow *g_view_shown = nullptr;
CCameraFollow *g_view_moved = nullptr;
CCameraFollow *g_view_scene = nullptr;  // coop-cutsceneview: the other copy's scene camera (CoopWorldsSceneView)
CCameraFollow *g_view_eye = nullptr;     // coop-p2actions: her first-person view on her own copy (PartnerEyeView), draw only
bool           g_view_eye_on = false;    // this draw is from it (her body left out: CoopPartnerEyeDrawn)
int            g_view_eye_skips = 0;     // the draws her body was left out of (the test's line)
CCameraFollow *g_view_arrival = nullptr; // coop-arrivalcam: the arrival's own camera, for the arriver's walk (ArrivalWalkView)
bool           g_view_own_frame = false; // the floor drawn round this copy's player all the same
bool           g_view_private = false;   // coop-gapmark: this draw is its player's own scene (ViewPrivateScene)

// Whether the floor's opening ran last tick (StandBesideArrival).
bool g_opening = false;
// Past the opening script's cut from the map's corner: the walk-in proper (ArrivalTrailTick; the bodies' and the far gate's span).
bool g_walked_in = false;

// coop-openercam: what player 1's presentation (retail's chest, Atla, gate key and attachment tasks) does to the screen,
// put back on player 2's copy for its draw (Jep's 2026-10-27 playtest: "if P1 opens a chest, P2 has to watch the chest
// opening animation; if P1 opens an Atla, P2's camera briefly fades"): the shared words as the task left them, restored
// in CoopViewEnd. Draw-only.
struct OtherPresentation {
    bool on = false;
    int  monsters_hidden = 0;
    int  effects_hidden = 0;
    int  minimap = 0;
    DispFadeMode fade_mode = DispFadeMode::None;
    bool tail = false; // his presentation's fade still to finish after his task (the Atla's fade back in)
};

OtherPresentation g_other_pres;

// coop-hischest (Jep, 2026-10-27: "Xiaos camera is still zoomed out while Toan is opening an Atla"): a presentation's motion
// camera keys its field of view too (MOTION_KEY_CAMERA_FOV: MGSetProjection, the Atla's effect saget.chr), and the draw's one
// projection is stepped on both copies. The other player's copy draws with the projection play had before it (play: kept
// from each draw with no presentation), the presentation's put back in CoopViewEnd. Draw-only.
// DC_COOP_MUTATE=presfov: the presentation's projection on both screens, as before (the twin).
struct ViewProjection {
    float play = 0.0f;
    float shown = 0.0f;
    bool  swapped = false;
    bool  tail = false; // the other's presentation just ended, its projection not yet back
};

ViewProjection g_view_projection;

// Player 1's presentation tasks (their INIT tick too).
bool PresentationTask(int task) {
    switch (task) {
        case GAME_TASK_BOX_BIG_INIT:
        case GAME_TASK_BOX_BIG_LOOP:
        case GAME_TASK_BOX_SMALL_INIT:
        case GAME_TASK_BOX_SMALL_LOOP:
        case GAME_TASK_ATRA_GET_INIT:
        case GAME_TASK_ATRA_GET_LOOP:
        case GAME_TASK_GATE_KEY_LOOP:
        case GAME_TASK_ATTACH_LOOP:
            return true;
    }

    return false;
}

// The dungeon's screen fade steps in its draw (CDispCtrl::FadeIn, FadeOut): step it here as that draw would, and keep the
// draw from drawing it (its mode off until CoopViewEnd), so the fade's state stays what both copies agree on.
void StepFadeUndrawn() {
    CDispCtrl &fade = DispFade__3;

    if (fade.mode == DispFadeMode::FadeIn && !(fade.rate == 0.0f && fade.speed == 0.0f)) {
        fade.rate -= fade.speed;
        if (fade.rate <= 0.0f) {
            fade.rate = 0.0f;
            fade.speed = 0.0f;
        }
    } else if (fade.mode == DispFadeMode::FadeOut && !(fade.rate == 0.0f && fade.speed == 0.0f)) {
        fade.rate += fade.speed;
        if (fade.rate >= 128.0f) {
            fade.rate = 128.0f;
            fade.speed = 0.0f;
        }
    }

    g_other_pres.fade_mode = fade.mode;
    fade.mode = DispFadeMode::Fixed;
}

// On player 2's copy through player 1's presentation: her screen as if it were not running.
void OtherPresentationBegin() {
    g_other_pres.on = true;
    g_other_pres.monsters_hidden = CMonUnitHyde;
    g_other_pres.effects_hidden = CEffectHyde;
    g_other_pres.minimap = infoMap;
    CMonUnitHyde = 0;
    CEffectHyde = 0;
    if (infoMap == 0 && infoMapOld != 0) {
        infoMap = infoMapOld;
    }
    StepFadeUndrawn();
    g_other_pres.tail = true;
}

// After his presentation's task: the rest of its fade (the Atla's fade back in runs on after it) stepped undrawn too.
void OtherPresentationTail() {
    if (!g_other_pres.tail) {
        return;
    }

    if ((DispFade__3.mode != DispFadeMode::FadeOut && DispFade__3.mode != DispFadeMode::FadeIn) || DispFade__3.speed == 0.0f) {
        g_other_pres.tail = false;
        return;
    }

    g_other_pres.on = true;
    g_other_pres.monsters_hidden = CMonUnitHyde;
    g_other_pres.effects_hidden = CEffectHyde;
    g_other_pres.minimap = infoMap;
    StepFadeUndrawn();
}

void OtherPresentationEnd() {
    if (!g_other_pres.on) {
        return;
    }

    CMonUnitHyde = g_other_pres.monsters_hidden;
    CEffectHyde = g_other_pres.effects_hidden;
    infoMap = g_other_pres.minimap;
    DispFade__3.mode = g_other_pres.fade_mode;
    g_other_pres.on = false;
}

// coop-hischest: the other player's presentation draws with this copy's play projection (ViewProjection). Hers is her
// opening (her chest's and her Atla's motion cameras, stepped on both copies); his, his presentation task. own: this copy's
// player's presentation, which keeps its own.
void KeepPlayProjection(bool other, bool own) {
    ViewProjection &view = g_view_projection;

    view.swapped = false;

    // The tick the other's presentation ends its last key's projection may stand a tick more (x-opener-view atla -Who 2:
    // one draw at 377): the play projection holds while it differs, until this copy's own presentation.
    view.tail = !own && (other || (view.tail && MGGetProjection() != view.play));
    other = other || view.tail;

    if (!other && !own) {
        view.play = MGGetProjection();
        return;
    }

    if (other && !own && view.play > 0.0f && view.play != MGGetProjection() && !CoopMutate("presfov")) {
        view.shown = MGGetProjection();
        MGSetProjection(view.play);
        view.swapped = true;
    }
}

void PutPresentationProjection() {
    if (g_view_projection.swapped) {
        MGSetProjection(g_view_projection.shown);
        g_view_projection.swapped = false;
    }
}

// Everything Step and the draw read, CCamera's and CCameraFollow's, but the frame.
void CopyCamera(CCameraFollow *to, CCameraFollow *from) {
    sceVu0CopyVector(to->pos, from->pos);
    sceVu0CopyVector(to->ref, from->ref);
    sceVu0CopyVector(to->next_pos, from->next_pos);
    sceVu0CopyVector(to->next_ref, from->next_ref);
    to->roll = from->roll;
    to->limit_move = from->limit_move;
    to->speed = from->speed;
    to->angle_h = from->angle_h;
    to->angle_v = from->angle_v;
    to->snap_range = from->snap_range;
    sceVu0CopyVector(to->follow, from->follow);
    to->distance = from->distance;
    to->height = from->height;
    to->next_angle = from->next_angle;
    to->angle = from->angle;
    to->follow_on = from->follow_on;
}

// DrawMap's distance from the camera to each cell, which the player's collision and autoCamTrial
// read on the next tick (cells within 240 units): as the camera the game shows puts it.
void CellDistances(CCameraFollow *camera) {
    float cam_pos[4];

    camera->GetPos(cam_pos);

    for (int row = 0; row < 20; row++) {
        for (int col = 0; col < 20; col++) {
            float cell_delta[4] = {160.0f * col - cam_pos[0], 0.0f, 160.0f * row - cam_pos[2], 0.0f};

            NowDngMap->cells[col + row * 20].camera_dist = DistVector(cell_delta);
        }
    }
}

// autoCamTrial, retail's camera against the walls, floors and trees of the floor (the eye drawn in to
// what stands between it and the look point, lifted over the floor, its height eased back to 5, the
// camera turned as its walker goes sideways, swung round behind it while it is locked on), on the
// partner's camera: for the one call it reads the partner where it reads the player (its place, its
// facing, and its own lock: the monster lock, or -1 for none), and the cells' distances from this
// camera. All it borrows is put back, on both copies alike.
void AutoCamera(CCameraFollow *camera, float lx, const float *moved, int lock) {
    float          cell_dist[400];
    float          cam_pos[4];
    CCameraFollow *shown = NowCamera__3;
    sceVu0FVECTOR  player;
    sceVu0FVECTOR  player_moved;
    float          player_lx = inputH1;
    float          far_limit = camera_far_dist_limmit;
    s32            lock_on = lockOnTargetFlag;
    s32            lock_no = lockOnTargetNo;
    CFrame        *player_frame = CharaMain.frame;
    s32            slow = BtActStatus.slow_walking;

    camera->GetPos(cam_pos);

    for (int i = 0; i < 400; i++) {
        float cell_delta[4] = {160.0f * (i % 20) - cam_pos[0], 0.0f, 160.0f * (i / 20) - cam_pos[2], 0.0f};

        cell_dist[i] = NowDngMap->cells[i].camera_dist;
        NowDngMap->cells[i].camera_dist = DistVector(cell_delta);
    }

    sceVu0CopyVector(player, CharaMain.pos);
    sceVu0CopyVector(player_moved, ref_off);
    sceVu0CopyVector(CharaMain.pos, g_partner.pos);
    sceVu0CopyVector(ref_off, (float *) moved);
    NowCamera__3 = camera;
    inputH1 = lx;
    lockOnTargetFlag = lock >= 0 ? 1 : 0;
    lockOnTargetNo = lock;
    CharaMain.frame = g_partner.chara->frame;
    BtActStatus.slow_walking = 0;

    autoCamTrial();

    NowCamera__3 = shown;
    inputH1 = player_lx;
    camera_far_dist_limmit = far_limit;
    lockOnTargetFlag = lock_on;
    lockOnTargetNo = lock_no;
    CharaMain.frame = player_frame;
    BtActStatus.slow_walking = slow;
    sceVu0CopyVector(CharaMain.pos, player);
    sceVu0CopyVector(ref_off, player_moved);

    for (int i = 0; i < 400; i++) {
        NowDngMap->cells[i].camera_dist = cell_dist[i];
    }
}

} // namespace

// coop-floorcam (rc3: "at a floor's start the camera visibly pops"; retail's own: ps2/src/dun/gameloop.cpp's "go dungeon"
// is the same): a floor's opening scene hands the camera back to play (BtSystemScriptAfter: behind the player at its kept
// distance, set at once) and that tick draws it there, before any wall pass. autoCamTrial, which puts the eye on what stands
// between it and the player (SetPos on the wall's hit, or the distance drawn in), runs only in the next tick's play, so the
// next frame jumped forward with no cut (B1's stair wall: 21.20 on both copies). Here the wall pass runs once as the camera
// comes back, from the eye it was just given (the cells' distances from that eye, as the next tick's DrawMap would leave them,
// put back after), so the first frame of play is already where play puts it. Both copies run it alike (the camera is the
// world's). DC_COOP_MUTATE=floorstart_cam_late (twin): the wall pass waits for the next tick, as retail's.
void CoopPlayCameraBack() {
    if (CoopMutate("floorstart_cam_late") || NowCamera__3 != &MainCamera__4 || NowDngMap == nullptr || NowDngMap->map_type != 1) {
        return;
    }
    float cell_dist[400];
    float cam_pos[4];

    MainCamera__4.GetPos(cam_pos);
    for (int i = 0; i < 400; i++) {
        float cell_delta[4] = {160.0f * (i % 20) - cam_pos[0], 0.0f, 160.0f * (i / 20) - cam_pos[2], 0.0f};

        cell_dist[i] = NowDngMap->cells[i].camera_dist;
        NowDngMap->cells[i].camera_dist = DistVector(cell_delta);
    }
    autoCamTrial();
    // (A wall between the near and far limits puts the eye on it at once, SetPos, as every tick of play does; a nearer one
    // draws the distance in, which the eye takes here at once too, a cut as the scene's own hand-back is.)
    float placed[4];

    MainCamera__4.GetPos(placed);
    if (placed[0] == cam_pos[0] && placed[1] == cam_pos[1] && placed[2] == cam_pos[2]) {
        MainCamera__4.Step(-1);
    }
    for (int i = 0; i < 400; i++) {
        NowDngMap->cells[i].camera_dist = cell_dist[i];
    }
}

namespace coop {

bool IndependentCameras() {
    return Enabled() && NetplayLocalPlayer() != 0;
}

bool FloorIntro() {
    // coop-b2join: not once the arrival plays a scene (B2's cat): the scene's camera on both screens, not one moved to her
    // (her screen showed her own back in front of the cat), and she stands through it (CoopWorldsPartnerStandsInScene).
    return IndependentCameras() && BtEventMode != 0 && BtEventInfo.running_script_no == 100 && !CoopWorldsArrivalPlaysScene();
}

// The partner's own arrival on the floor: the opening loads the player's from its character's pack in
// dun/d0N/stair_mos (motion 100, coming down the stairs, in motion set 3), and the partner loads its
// own the same way into its arena. Wise Owl Forest has them for Toan, Xiao and Goro; elsewhere, or for
// a member without one, the partner stands.
void LoadArrival() {
    static const char *const kCodes[6] = {"c01", "c04", "c06", "c05", "c10", "c18"};
    constexpr int            kArrivalBytes = 4 * 1024 * 1024;
    static u_int            *pack = nullptr;
    int                      chara = g_partner.loaded_chara;
    char                     path[64];
    int                      size = 0;

    // Once for the model as loaded: a floor that keeps the partner's model keeps its arrival too, and
    // loading it again would fill the arena.
    static int loaded_used = -1;
    static int loaded_chara = -1;
    static int loaded_map = -1;

    if (chara < 0 || chara > 5 || (g_partner.arena.used == loaded_used && chara == loaded_chara && selectMapNo == loaded_map)) {
        return;
    }

    std::snprintf(path, sizeof path, "dun/d0%d/stair_mos/%sd0%ds.chr", selectMapNo + 1, kCodes[chara], selectMapNo + 1);

    if (pack == nullptr) {
        pack = reinterpret_cast<u_int *>(new (std::align_val_t{64}) u_char[kArrivalBytes]);
    }

    if (LoadFile2(path, pack, &size, 0) == 0 || size <= 0 || size > kArrivalBytes) {
        return;
    }

    g_partner.chara->LoadPackData2(pack, (char *) "info.cfg", &g_partner.arena, PartnerTextureBlock(), &g_partner.arena, 0);
    loaded_used = g_partner.arena.used;
    loaded_chara = chara;
    loaded_map = selectMapNo;
    std::printf("coop: partner arrival %s, %d quadwords\n", path, g_partner.arena.used);
}

// Where the player is drawn, and its own x and z axes on the ground (x, z), unit long. Under a scene's
// reference that is its frame's world matrix (row 3 the place, rows 0 and 2 the axes); otherwise
// CharaMain.pos and its yaw, as RotMatrixY lays them out. The twin (opening_local) always reads the
// latter, as the opening did before the cave was handled.
void PlayerDrawnPlace(float *place, float *axis_x, float *axis_z) {
    CFrame *frame = CharaMain.frame;

    if (frame != nullptr && frame->reference != 0 && !CoopMutate("opening_local")) {
        sceVu0FMATRIX world;

        frame->GetLWMatrix(world);

        float lx = std::hypot(world[0][0], world[0][2]);
        float lz = std::hypot(world[2][0], world[2][2]);

        if (lx > 0.001f && lz > 0.001f) {
            place[0] = world[3][0];
            place[1] = world[3][1];
            place[2] = world[3][2];
            place[3] = 1.0f;
            axis_x[0] = world[0][0] / lx;
            axis_x[1] = world[0][2] / lx;
            axis_z[0] = world[2][0] / lz;
            axis_z[1] = world[2][2] / lz;
            return;
        }
    }

    sceVu0FVECTOR rotation;

    CharaMain.GetRotation(rotation);
    sceVu0CopyVector(place, CharaMain.pos);
    place[3] = 1.0f;
    axis_x[0] = cosf(rotation[1]);
    axis_x[1] = -sinf(rotation[1]);
    axis_z[0] = sinf(rotation[1]);
    axis_z[1] = cosf(rotation[1]);
}

// coop-entrance (Jep 11-11, playtest item 21, clips62 leave-floor: "Xiao should peek out from behind the log like Toan, she stands
// in front of it"): a floor's walk-in (script 100) stands the lead in its entrance's niche, still (Wise Owl Forest's stairs:
// CharaMain.pos (823 0 975) the whole walk-in), and the arrival motion (100) carries his body out of it. The side table below
// asks where his walk reaches: in the niche nothing does but his front (right n left n behind n front y), so the second walker
// stood 14 before him, out past the log, between him and the camera, through the whole shot. Jep's rule: each entrance has its
// own hand-picked spot for every walker after the lead, picked by its frames (in frame, behind the prop like him, not in the
// prop, not covering him). In the lead's frame: right of him, along his way (+ before him), and her facing turned from his.
// The side table is the fallback for an entrance with no entry. DC_COOP_MUTATE=entrance_table_off (twin): the side table
// everywhere, as before. DC_COOP_TEST_ENTRANCE_SPOT=map:right:along:turn (a probe, picking spots): that spot for walker 2 on map.
// [dungeon][walker - 2]: walkers 2 and 3, picked by frames on each dungeon's floor-1 walk-in (debug\scratch\entrance\spots.png;
// "right" is his right hand, so a lead facing the camera has walker 2 on the screen's left). An along of NaN: no entry.
constexpr float kNoSpot = 1e9f;
const EntranceSpot kEntranceSpots[6][2] = {
    // Divine Beast Cave: a scene's node carries the lead down the stairs. coop-entrance2 (Jep 11-12: "w2 does not show Xiao until
    // frame ~300"; probe e1: drawn throughout, but 7 to his right of his way, in the stairwell's wall, till the stairs' foot): the
    // walkers walk his own way down after him, and stand either side of him at the foot (the side 7 the trail had).
    // Round 6 (Jep 11-12, clip e36: "they now run right on top of each other"; "the LIVE version (v0.1.1) was already fine: Xiao
    // runs down slightly behind and slightly to Toan's right"): v0.1.1's trail exactly, not an own entrance: kTrailTicks 20 behind
    // him, 7 to his right, whoever is first in the order (no option C here). Hips (v0.1.1 replayed, its partner's place alike at
    // every tick sampled): 15-17 apart down the stairs, 7.0 beside him at the stairs' foot from tick 195 (his stop).
    {{7.0f, 0.0f, 0.0f, false, 0.0f}, {-7.0f, 0.0f, 0.0f, false, 0.0f}},
    // Wise Owl Forest: coop-entrance2 (Jep 11-12: "copy the exact spot single-player Xiao enters at"): retail plays the same walk-in
    // for whoever is the player (P2 leading apart: her copy's walk-in as Xiao, c04d02s, from the same niche spot and to the same end
    // as his, 343 ticks), so a walker with its own stairs motion starts at his very place playing its own; an overlap is a delay,
    // never a move (the forest's own step, kForestOrderStep). Where it ends: walker 2 on his right, a little behind; walker 3 behind
    // on his left.
    {{10.0f, -12.0f, 0.0f, true, 0.0f}, {-4.0f, -22.0f, 0.0f, true, 0.0f}},
    // Shipwreck: the lift. His right hand is the hook and its rope: both walkers on his left, on the lift's boards. (Parked.)
    {{-14.0f, 0.0f, 0.0f, false}, {-22.0f, 0.0f, 0.0f, false}},
    // Sun and Moon Temple: coop-entrance2 (Jep 11-12: "she comes out and then seems to stand in front of the wall": her spot was 14
    // beside his way, by the wall): the walkers come out of the door his way, after him, 7 to his side (in his very steps the
    // entrance's camera, straight down the stairs, had him hide her: probe e3), and stand either side of him at the end.
    {{14.0f, 0.0f, 0.0f, true, 7.0f}, {-14.0f, 0.0f, 0.0f, true, -7.0f}},
    // Moon Sea: the pad's hexagon by character instead (MoonSpot); these are the fallback when the pad's frame is missing.
    {{14.0f, 0.0f, 0.0f, false}, {-14.0f, 0.0f, 0.0f, false}},
    // Gallery of Time: the hall, one either side of him.
    {{14.0f, 0.0f, 0.0f, false}, {-14.0f, 0.0f, 0.0f, false}},
};

const EntranceSpot *EntranceSpotFor(int map, int walker) {
    static EntranceSpot probe;
    static int          probe_map = [] {
        const char *spec = TestEnv("DC_COOP_TEST_ENTRANCE_SPOT");
        int         m = -1;

        int own = 0;

        if (spec != nullptr && std::sscanf(spec, "%d:%f:%f:%f:%d", &m, &probe.right, &probe.along, &probe.turn, &own) >= 4) {
            probe.own = own != 0;
            probe.turn *= PI / 180.0f;
            return m;
        }
        return -1;
    }();

    if (map < 0 || map > 5 || walker < 2 || walker > 3) {
        return nullptr;
    }
    if (map == probe_map && walker == 2) {
        return &probe;
    }

    const EntranceSpot *spot = &kEntranceSpots[map][walker - 2];

    return std::isnan(spot->along) ? nullptr : spot;
}

// coop-entrance2 (Jep 11-12, on the entrance spots and the Moon Sea pad). Two rules, both keyed by CHARACTER (Toan 0, Xiao 1,
// Goro 2, Ruby 3, Ungaga 4, Osmond 5), never by player number or arrival order:
// 1. Delays: only players who reach a floor TOGETHER (one starts it, the descend/level-start prompt, and everyone is pulled in on
//    the same tick: one copy, the partner beside the lead from the opening's first tick, StandBesideArrival) get one: each
//    entrance starts its character's delay (index x kEntranceDelayStep, 0.4 s; Osmond +2.0 s) after that shared start, the lead's
//    own too. Players who arrive SEPARATELY (the entrance window's walkers, EntranceWalkStart) get no character delay (rule 3).
//    DC_COOP_MUTATE=entrance_delay_apart (twin): separate arrivals delayed by character too. Single player: retail, no delay.
// 3. (Jep 11-12) Separate arrivals within kArrivalWindow of the first (each on its own between-floors screen, the same pick at
//    nearly one time) go by ARRIVAL order, not character: the first's entrance starts as it arrived, the second's kArrivalStep
//    after the first's, the third's 2 x kArrivalStep, ... (EntranceArrivalOffsets); a later arrival's starts as it arrived. Each
//    plays its own entrance and sees the others'. DC_COOP_MUTATE=entrance_arrivals_unstaggered (twin): each as it arrived.
// 2. Moon Sea's pad (dun/d05, script 100's entrance: the retail beam, event actor 0 = dun/d05/effect/d05taiho.chr playing its
//    motion 2 at the pad's centre, the player shown 100 ticks into it): in a linked session every character has its own spot
//    round the pad, a hexagon (kMoonRadius from the centre, kMoonFirst + index x 60 degrees in the pad's frame), and appears in
//    its own beam (the retail model and motion, one posed per character, EntranceBeamsDraw) at its own time; before it a
//    character is not drawn. DC_COOP_MUTATE=moon_one_beam (twin): the retail beam alone at the centre, everyone drawn throughout.
constexpr int   kEntranceDelayStep = 24; // ticks (0.4 s at 60)
// Wise Owl Forest's step (Jep 11-12: "Xiao must start at her normal (retail) start spot, peeking behind the log, playing her own
// motion, with a delay just long enough that she never clips Toan"; the step is per floor). Both stairs motions start at the
// niche's one place; the second's held first frame (its body peeking behind the log, (823 957) for 200 ticks of its motion) is
// where the first's body passes on its way out (5.5 apart at tick 186). Hips from its start on (DC_COOP_TEST_ENTRANCE_BODIES,
// lags 150-300): 200 6.3, 215 7.6, 220 8.2, 225 8.8; so 220, and each one waiting is not drawn (EntranceWaitHidden). Its
// walk-in then ends still peeking (17.9 from him), and the camera's cut at the opening's end hides its step to the table's spot.
// DC_COOP_MUTATE=forest_order_step (twin): the forest at kEntranceDelayStep too (round 4: 1.3 apart at the end).
// Round 6 (x-entrwin-p2first: Xiao first, Toan 220 after: 5.0 apart; his body walks out of the niche sooner in his motion than hers):
// Toan after Xiao, by step: 260 7.7, 270 9.3, 280 9.7, 300 10.9. One step for any order: 270 (Xiao after Toan at 275: 16.2).
constexpr int   kForestOrderStep = 270;
// Round 7 (Jep 11-12, clip e49: "at the end Xiao pushes Toan aside: awkward"; "the earlier walkers walk FORWARD at the end of their
// walk-in to make room, because up to 5 more players may come behind"): in Wise Owl Forest every walker but the last, its stairs
// motion played out, walks on (its own walk motion, kForwardStep a tick) to a forward end (ForestChooseSpots), the k-th to finish
// to end k, so the niche's one end is free for the next; the last walks on to its own out of the opening's view.
constexpr float kForwardStep = 0.8f;

// coop-entrance10 (Jep 11-12, clip e59: "Toan walking forward clips him into a TREE, and then he turns around at the end, which looks
// odd"): straight on 30 from the niche's end ran into the tree beside it (probe e69: the walk stopped before 20, 3-5 of 8 sides
// clear); the turn-back was only there for the camera. Now the walk-on's end is chosen on the floor, once an opening, from the
// game's own data (both copies alike): by the smallest turn from his facing (0, then 15 each side, to 90) and the shortest way
// (20 to 60), the first end where (a) he walks there (PathClear: the game's MoveCheck, 8 a step), his body has room all round
// at the end (8 sides of 8 clear) and along the way from 10 on (7 of 8 sides of 6), (b) a follow camera behind him (25 back, 13.5
// up, CameraLineClear from his head) has a clear line, so he keeps facing the way he walked, and (c) he ends out of the
// opening's view: 55 degrees or more off its camera's axis, or behind it (probe e67s with e57's view log: his waist left the
// frame at 23 walked straight, 27 to 59 degrees off). Up to six ends, each 10 from the others; the last walker walks to the first,
// the one before to the second (in e71's niche only one lane is clear, -30 to -45 degrees: the ends lie along it, the earlier
// walkers the farther), and no walk passes within 8 of an end an earlier walker stands at (a walker who
// finishes first of two walks to the second). None clear: straight on 30 (said). DC_COOP_MUTATE=forest_spot_straight (twin): straight on
// 30 always, as e57.
struct ForestSpot {
    float end[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float dir = 0.0f;  // the walk's yaw
    float dist = 0.0f;
    int   deg = 0;      // its turn from his facing
    int   room_end = 0; // sides of 8 clear at the end
    int   room_way = 0; // the least along the way from 10 on
    bool  camera = false;
    float off = 0.0f;   // degrees off the opening camera's axis (180: behind it)
};
ForestSpot g_forest_spots[6];
int        g_forest_spot_n = -1; // -1 not chosen this opening
int        g_forest_lead_spot = 0; // the lead's (the last walker's: the first end)

int ForestRoom(const float *at, float radius) {
    int room = 0;

    for (int k = 0; k < 8; k++) {
        const float b = k * PI / 4.0f;
        const float out[4] = {at[0] + radius * sinf(b), at[1], at[2] + radius * cosf(b), 1.0f};

        room += PathClear(at, out) ? 1 : 0;
    }
    return room;
}

// The body's clearance at a point: the least, over 8 sides, of how far (to 12, by 1) the game's MoveCheck lets him on from it
// (MoveCheck keeps his own collision radius off a wall or a tree, so this is room beyond his body). For the walk-on's log.
float ForestClearance(const float *at) {
    float least = 12.0f;

    for (int k = 0; k < 8; k++) {
        const float b = k * PI / 4.0f;

        for (int r = 1; r <= 12; r++) {
            const float out[4] = {at[0] + r * sinf(b), at[1], at[2] + r * cosf(b), 1.0f};

            if (!PathClear(at, out)) {
                least = std::min(least, static_cast<float>(r - 1));
                break;
            }
        }
    }
    return least;
}

// How far back (from the camera's own distance, by 2, to 10) a follow eye at angle and up above follow sees him clearly: the
// floor's camera collision (CameraLineClear), as the game's own pull-in sees it.
float ForestCameraBack(const float *follow, float angle, float up, float most) {
    float back = most;

    for (; back > 10.0f; back -= 2.0f) {
        const float eye[4] = {follow[0] + back * sinf(angle), follow[1] + up, follow[2] + back * cosf(angle), 1.0f};

        if (CameraLineClear(follow, eye)) {
            break;
        }
    }
    return back;
}

// The nearest a walk from a to b comes to point c (on the ground).
float ForestPathNear(const float *a, const float *b, const float *c) {
    const float d[2] = {b[0] - a[0], b[2] - a[2]};
    const float l2 = d[0] * d[0] + d[1] * d[1];
    const float u = l2 > 0.0f ? std::clamp(((c[0] - a[0]) * d[0] + (c[2] - a[2]) * d[1]) / l2, 0.0f, 1.0f) : 0.0f;

    return std::hypot(a[0] + u * d[0] - c[0], a[2] + u * d[1] - c[2]);
}

void ForestChooseSpots(const float *start, float face) {
    sceVu0FVECTOR eye = {start[0], start[1], start[2], 1.0f};
    sceVu0FVECTOR ref = {start[0], start[1], start[2] + 1.0f, 1.0f};

    if (NowCamera__3 != nullptr) {
        NowCamera__3->GetPos(eye);
        NowCamera__3->GetRef(ref);
    }
    const float fwd[2] = {ref[0] - eye[0], ref[2] - eye[2]};
    const float fl = std::hypot(fwd[0], fwd[1]);
    const bool  straight = CoopMutate("forest_spot_straight");
    // (a follow camera 25 back and 13.5 up, the after-script height, must see him: its eye is set at its clear length, ForestCameraClear)
    const float cam_back = 25.0f;
    const float cam_up = 13.5f;
    static const int kDegs[] = {0, -15, 15, -30, 30, -45, 45, -60, 60, -75, 75, -90, 90};

    g_forest_spot_n = 0;
    for (const int deg : kDegs) {
        for (int dist = 20; dist <= 60 && g_forest_spot_n < 6; dist += 5) {
            if (straight && (deg != 0 || dist != 30)) {
                continue;
            }
            ForestSpot s;

            s.dir = face + deg * PI / 180.0f;
            s.dist = static_cast<float>(dist);
            s.deg = deg;
            s.end[0] = start[0] + s.dist * sinf(s.dir);
            s.end[1] = start[1];
            s.end[2] = start[2] + s.dist * cosf(s.dir);
            const bool walk = PathClear(start, s.end);

            s.room_end = ForestRoom(s.end, 8.0f);
            s.room_way = 8;
            for (float along = 10.0f; along < s.dist; along += 8.0f) {
                const float at[4] = {start[0] + along * sinf(s.dir), start[1], start[2] + along * cosf(s.dir), 1.0f};

                s.room_way = std::min(s.room_way, ForestRoom(at, 6.0f));
            }
            const float head[4] = {s.end[0], s.end[1] + 13.0f, s.end[2], 1.0f}; // (the follow point, as the reset sets it)
            const float cam[4] = {s.end[0] - cam_back * sinf(s.dir), s.end[1] + 13.0f + cam_up, s.end[2] - cam_back * cosf(s.dir), 1.0f};

            s.camera = CameraLineClear(head, cam);
            const float v[2] = {s.end[0] - eye[0], s.end[2] - eye[2]};
            const float depth = fl > 0.0f ? (v[0] * fwd[0] + v[1] * fwd[1]) / fl : 0.0f;
            const float side = fl > 0.0f ? (v[0] * fwd[1] - v[1] * fwd[0]) / fl : 0.0f;

            s.off = depth <= 0.0f ? 180.0f : atan2f(std::fabs(side), depth) * 180.0f / PI;
            bool spaced = true;

            // (10 from every end chosen before it, and their walks 8 clear of it: the ends are walked to in the opposite order, the
            // last walker to the first end, so an earlier walker stands at this one while those walk on)
            for (int k = 0; k < g_forest_spot_n; k++) {
                spaced = spaced && std::hypot(s.end[0] - g_forest_spots[k].end[0], s.end[2] - g_forest_spots[k].end[2]) >= 10.0f &&
                         ForestPathNear(start, g_forest_spots[k].end, s.end) >= 8.0f;
            }
            if (TestEnv("DC_COOP_TEST_FOREST_SPOTS") != nullptr) { // (probe: every end tried)
                std::fprintf(stderr, "coop: forest spot %+d deg %d: walk %d room %d/8 way %d/8 camera %d off %.0f spaced %d\n", deg, dist, walk ? 1 : 0,
                             s.room_end, s.room_way, s.camera ? 1 : 0, s.off, spaced ? 1 : 0);
            }
            if (straight || (walk && s.room_end == 8 && s.room_way >= 7 && s.camera && s.off >= 55.0f && spaced)) {
                g_forest_spots[g_forest_spot_n++] = s;
            }
        }
    }
    if (g_forest_spot_n == 0) {
        ForestSpot s;

        s.dir = face;
        s.dist = 30.0f;
        s.end[0] = start[0] + 30.0f * sinf(face);
        s.end[1] = start[1];
        s.end[2] = start[2] + 30.0f * cosf(face);
        s.room_end = ForestRoom(s.end, 8.0f);
        g_forest_spots[g_forest_spot_n++] = s;
        std::fprintf(stderr, "coop: opening: map 1 walk-on: no clear end out of the opening's view: straight on 30\n");
    }
    for (int k = 0; k < g_forest_spot_n; k++) {
        const ForestSpot &s = g_forest_spots[k];

        std::fprintf(stderr, "coop: opening: map 1 walk-on spot %d: %+d degrees from his facing, %.0f on, at (%.0f %.0f): body room %d/8 at the end, %d/8 "
                             "on the way; camera line %s (%.0f back, %.0f up); %.0f degrees off the opening's view\n",
                     k + 1, s.deg, s.dist, s.end[0], s.end[2], s.room_end, s.room_way, s.camera ? "clear" : "blocked", cam_back, cam_up, s.off);
    }
}
bool            g_wait_hidden[2] = {false, false}; // (the lead, the partner: waiting for its start, not drawn; draw only)
constexpr int   kArrivalWindow = 6;      // rule 3: ticks (0.1 s) after the first arrival
constexpr int   kArrivalStep = 24;       // rule 3: ticks (0.4 s) between entrances by arrival order (the forest's kForestOrderStep)
constexpr float kMoonRadius = 12.0f;     // inside the pad's ring
constexpr float kMoonFirst = 2.0f * PI / 3.0f; // Toan's spot: the pad's far side from the entrance camera (-66 25 45 of the pad)
constexpr int   kMoonMap = 4;

const char *EntranceCharaName(int chara) {
    static const char *const kNames[6] = {"Toan", "Xiao", "Goro", "Ruby", "Ungaga", "Osmond"};

    return chara >= 0 && chara < 6 ? kNames[chara] : "none";
}

int EntranceCharaDelay(int chara) {
    return chara >= 0 && chara < 6 ? chara * kEntranceDelayStep : 0;
}

// coop-entrance2 round 3 (Jep 11-12: "one cutscene that always plays the same way"): players together are ordered by the fixed list
// Toan, Xiao, Goro, Ruby, Ungaga, Osmond; the first present starts at 0 and each next present one kEntranceDelayStep after the one
// before (Xiao, Goro, Ruby together: 0, 24, 48; Toan, when there, always 0). Each still at its own spot. out[i] for charas[i];
// a tie (one character twice) by its place in the list. DC_COOP_MUTATE=entrance_fixed_delay (twin): each character's fixed delay
// (index x step), as round 2 had it.
// The order's step on this floor (the forest's own, kForestOrderStep; rule 3's arrivals too).
int EntranceOrderStep(int fallback) {
    // (DC_COOP_TEST_FOREST_STEP=N: a probe's step)
    const char *probe_step = TestEnv("DC_COOP_TEST_FOREST_STEP");
    const int   probe = probe_step != nullptr ? std::atoi(probe_step) : 0;

    return selectMapNo == 1 && !CoopMutate("forest_order_step") ? (probe > 0 ? probe : kForestOrderStep) : fallback;
}

void EntranceOrderOffsets(const int *charas, int n, int *out) {
    for (int i = 0; i < n; i++) {
        int rank = 0;

        for (int j = 0; j < n; j++) {
            rank += j != i && (charas[j] < charas[i] || (charas[j] == charas[i] && j < i));
        }
        out[i] = CoopMutate("entrance_fixed_delay") ? EntranceCharaDelay(charas[i]) : rank * EntranceOrderStep(kEntranceDelayStep);
    }
}

// Rule 3: in[i].start the tick arrival i's walk-in began (one clock), -1 none; out[i] its entrance start after the first's, -1 none.
void EntranceArrivalOffsets(const EntranceArrival *in, int n, int *out) {
    int first = -1;

    for (int i = 0; i < n; i++) {
        if (in[i].start >= 0 && (first < 0 || in[i].start < in[first].start || (in[i].start == in[first].start && in[i].player < in[first].player))) {
            first = i;
        }
    }
    for (int i = 0; i < n; i++) {
        out[i] = -1;
        if (first < 0 || in[i].start < 0) {
            continue;
        }
        const int after = in[i].start - in[first].start;

        if (after > kArrivalWindow || CoopMutate("entrance_arrivals_unstaggered")) {
            out[i] = after;
            continue;
        }
        int rank = 0; // arrivals within the window before this one (a tie: the lower player number first)

        for (int j = 0; j < n; j++) {
            rank += j != i && in[j].start >= 0 && in[j].start - in[first].start <= kArrivalWindow &&
                    (in[j].start < in[i].start || (in[j].start == in[i].start && in[j].player < in[i].player));
        }
        out[i] = rank * EntranceOrderStep(kArrivalStep);
    }
}

// cand1112c: a window walker's entrance start after the lead's (EntranceWalkStart): the lead the window named comes first on a tie
// in ticks (x-entrance-forest-window: player 1 walked in 0 ticks after player 2, who led by 1 ms, and the tie went to the lower
// player number: the walker's entrance began with the lead's, 0.0 apart); a gap below 0 (the walker's clock a tick ahead) is 0.
// DC_COOP_MUTATE=entrance_offset_tie (twin, x-entrwin-plan): the gap as it came, the tie by player number, as before.
int EntranceWalkerOffset(int player, int gap) {
    const int             after = CoopMutate("entrance_offset_tie") || gap > 0 ? gap : player < 3 - player ? 1 : 0;
    const EntranceArrival arrivals[2] = {{3 - player, 0}, {player, after}};
    int                   at[2] = {0, -1};

    EntranceArrivalOffsets(arrivals, 2, at);
    return at[1];
}

// A character's offset among the players on this floor's opening together (the lead and the partner: the two there are).
int EntranceTogetherOffset(int chara) {
    const int present[2] = {EntranceLeadChara(), g_partner.chara != nullptr ? g_partner.loaded_chara : -1};
    const int n = present[1] >= 0 ? 2 : 1;
    int       out[2] = {0, 0};

    EntranceOrderOffsets(present, n, out);
    for (int i = 0; i < n; i++) {
        if (present[i] == chara) {
            return out[i];
        }
    }
    return 0;
}

bool MoonEntrance() {
    return selectMapNo == kMoonMap && Enabled() && NetplayHasPlayer() && mode == GAME_MODE_DUNGEON && NowDngMap != nullptr &&
           !CoopMutate("moon_one_beam");
}

// The pad's frame (the script's world coordinates: _SET_WORLD_COORD from the map part in_2): its place and yaw. False if none.
bool MoonPad(float *centre, float *yaw) {
    static char name[] = "in_2";
    CFrame     *pad = NowDngMap != nullptr ? NowDngMap->GetFrameSearch(name) : nullptr;

    if (pad == nullptr) {
        return false;
    }
    sceVu0FMATRIX world;

    pad->GetLWMatrix(world);
    centre[0] = world[3][0];
    centre[1] = world[3][1];
    centre[2] = world[3][2];
    centre[3] = 1.0f;
    *yaw = atan2f(world[2][0], world[2][2]);
    return true;
}

// A character's spot round a pad at centre turned pad_yaw (place, w 1).
void MoonSpotAt(int chara, const float *centre, float pad_yaw, float *place) {
    const float angle = kMoonFirst + chara * (PI / 3.0f);
    const float lx = kMoonRadius * sinf(angle); // in the pad's frame (x right, z ahead), as retail's (0 -1 0.1) for the player
    const float lz = kMoonRadius * cosf(angle);

    place[0] = centre[0] + lx * cosf(pad_yaw) + lz * sinf(pad_yaw);
    place[1] = centre[1] - 1.0f;
    place[2] = centre[2] - lx * sinf(pad_yaw) + lz * cosf(pad_yaw);
    place[3] = 1.0f;
}

// A character's spot round the floor's pad and the way it faces (the pad's, as retail's _SET_NPC_ROT(-1, 0 0 0)).
bool MoonSpot(int chara, float *place, float *yaw) {
    float centre[4];
    float pad_yaw = 0.0f;

    if (chara < 0 || chara > 5 || !MoonPad(centre, &pad_yaw)) {
        return false;
    }
    MoonSpotAt(chara, centre, pad_yaw, place);
    *yaw = pad_yaw;
    return true;
}

namespace {

// The Moon Sea entrance's clock, on each copy: ticks since the retail beam (event actor 0's motion 2) began, read from that
// actor's own motion time while it plays (game state, the same on both copies once merged), counted on after it holds on its
// last frame; -1 before it. reveal: the clock when the script showed the player (_NPC_DRAW(1, -1)), -1 till then.
struct MoonClock {
    int  key = -1000; // the floor (FloorKey) this clock is for
    int  clock = -1;
    int  reveal = -1;
    int  length = 0;  // the beam motion's length in ticks
} g_moon;

// Rule 1: this floor's opening began with the partner beside the lead on this copy (both pulled in on one tick), floor key.
int g_together = -1000;

// coop-entrance2 round 3, option C (Jep 11-12): together on a way the script walks the game's player along (the Temple, the cave), when
// the partner's character comes first in the order, she plays the scripted walk-in (on his live way, the first at 0) and the
// game's player is drawn as a stand-in of his member on that way his offset behind (the game's player itself not drawn, as a
// scene's stand-in hides it): draw only, his place and control untouched (the handover of coop-sceneowner, as the screen has it).
struct SwapStandIn {
    bool  on = false;
    int   chara = -1;
    float place[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float yaw = 0.0f;
    int   motion = 0;
    int   flags = 0;
    bool  waiting = false; // (at his way's first sample till his offset has passed: not drawn, EntranceSwapStandInWaiting)
} g_swap_stand_in;

// coop-entrance2 round 3, the forest's end (Jep 11-12, option A: "her own walk-in curved over its last ~0.5 s to end beside Toan, no
// hop"): her stairs motion carries her body from the niche as his carries his (the same walk-in, the same end), so over its last
// kForestCurve ticks her frame slides by the end spot's offset (the table's, beside him) and she ends there; an opening that ends
// first leaves her motion playing to its end (no hop to the spot), then she stands where her body is. Judged on frames (Jep's
// condition: only if smooth): NOT the default. The opening ends on a camera cut to behind him, so the hop happens across the cut
// and is not seen, while the curve's last ticks play off the new view and she pops in beside him ~20 frames later (spots2's
// forest A-vs-hop rows). DC_COOP_MUTATE=forest_curve: the curve, for the comparison.
// coop-entrance2 round 6 (Jep 11-12, clip e36: "the opening ends too early, when Toan's walk-in finishes instead of Xiao's"): the
// opening now lasts till the last walker's motion ends, so the curve plays inside it: the default for a partner that walks in last
// (DC_COOP_MUTATE=forest_hop: her hop at the end, as round 5), over kForestCurve ticks (DC_COOP_TEST_FOREST_CURVE=N, a probe).
int ForestCurveTicks() {
    static const int ticks = [] {
        const char *probe = TestEnv("DC_COOP_TEST_FOREST_CURVE");

        return probe != nullptr && std::atoi(probe) > 0 ? std::atoi(probe) : 100; // (probe e41: 30 1.9, 60 5.9, 100 8.2 apart at the nearest)
    }();

    return ticks;
}
struct ForestTail {
    bool  on = false;  // her motion plays on past the opening's end
    float niche[3] = {};
    float side[2] = {}; // the end spot's offset in his frame there
    float yaw = 0.0f;
} g_forest_tail;

// The curve's share at her motion's time now: 0 till its last kForestCurve ticks, then up to 1 (smooth); 2 once it has ended.
float ForestCurve(CCharacter *her) {
    int                   index = -1;
    const tagMOTION_TYPE *m = her != nullptr && her->motion_no == 100 ? her->GetMotionParam(100, &index, nullptr, nullptr, nullptr) : nullptr;

    if (m == nullptr || m->motion_info == nullptr || index < 0) {
        return 2.0f;
    }
    const MOTION_INFO &info = m->motion_info[index];
    const float        speed = info.speed > 0.0f ? info.speed : 1.0f;
    const float        left = (info.end - 1 - m->state.time) / speed; // ticks to its last frame
    if (left <= 0.5f || her->motion_state == 3) {
        return 2.0f;
    }
    const float u = std::clamp(1.0f - left / (float) ForestCurveTicks(), 0.0f, 1.0f);

    return u * u * (3.0f - 2.0f * u);
}

} // namespace

const float *ArrivalRecordEnd(); // (below: the walk-in record's end and the facing there)
float        ArrivalRecordEndYaw();

bool ForestTailOn() {
    return g_forest_tail.on;
}

namespace {

constexpr int kMoonAfter = 400; // the clock runs this long past the script's end (the last delayed beam), then stops

// The beam actor's motion 2 (index, start frame, speed), nullptr when actor 0 is not the beam.
tagMOTION_TYPE *BeamMotion(CCharacter *beam, int *index) {
    if (beam == nullptr || beam->frame == nullptr || beam->motion_no != 2) {
        return nullptr;
    }
    tagMOTION_TYPE *motion = beam->GetMotionParam(2, index, nullptr, nullptr, nullptr);

    return motion != nullptr && motion->motion_info != nullptr && *index >= 0 ? motion : nullptr;
}

} // namespace

int MoonEntranceClock() {
    return g_moon.key == (UserStatus != nullptr ? selectMapNo * 256 + UserStatus->cur_floor : -1000) ? g_moon.clock : -1;
}

int MoonEntranceReveal() {
    return g_moon.reveal;
}


bool EntranceWindowSeen(); // (below)

// coop-entrance9 (Jep 11-12, clip e57; checked by numbers): through the forest's opening (from its stairs motions) and 90 ticks after it, the drawn
// lead's cloths on him each tick (his poncho: none may drop) and, each draw after control returns, the camera's eye (no back and
// forth: the frame-to-frame step's sign changes on x and z). Said once the 150 are up (x-entrance-forest reads it).
struct ForestAfter {
    int   since = -1;       // ticks since the opening ended, -1 not in one
    bool  tracking = false; // from the lead's walk-on
    int   cloths_first = -1;
    int   cloths_least = 99;
    int   cloth_tick = -1;
    int   walk_ticks = 0;
    int   all = 0;
    int   draws = 0;
    float eye_last[3] = {0.0f, 0.0f, 0.0f};
    float step_last[3] = {0.0f, 0.0f, 0.0f};
    int   reversals[3] = {0, 0, 0};
    float step_most = 0.0f;
} g_forest_after;

void ForestAfterTick(bool opening) {
    ForestAfter &f = g_forest_after;

    if (!f.tracking) {
        return;
    }
    if (!opening && f.since < 0) {
        f.since = 0;
    } else if (f.since >= 0) {
        f.since++;
    }
    int       all = 0;
    const int on = DrawnLeadClothsOn(&all);

    // (counted from the lead's walk-on, the stand-in drawn in his place from its third tick, drawn and stepped; else from the end)
    if (f.since < 0 && (!g_swap_stand_in.on || ++f.walk_ticks < 3)) {
        return;
    }

    f.all = std::max(f.all, all);
    if (f.cloths_first < 0) {
        f.cloths_first = on;
    }
    if (on < f.cloths_least) {
        f.cloths_least = on;
        f.cloth_tick = f.since;
    }
    if (f.since == 90) {
        std::fprintf(stderr, "coop: opening: map 1 the lead's cloths on him from the walk-on to 90 after: %d at first, %d at the least (%d after the end; "
                             "he has %d)\n",
                     f.cloths_first, f.cloths_least, f.cloth_tick, f.all);
        std::fprintf(stderr, "coop: opening: map 1 the camera after control returned: %d draws, its eye's step reversed %d/%d/%d times (x/y/z), "
                             "its largest step %.1f\n",
                     f.draws, f.reversals[0], f.reversals[1], f.reversals[2], f.step_most);
        f.tracking = false;
    }
}

void ForestAfterDraw(CCameraFollow *camera) {
    ForestAfter &f = g_forest_after;

    if (!f.tracking || f.since < 0 || camera == nullptr) {
        return;
    }
    sceVu0FVECTOR eye;

    camera->GetPos(eye);
    for (int i = 0; i < 3 && f.draws > 0; i++) {
        const float step = eye[i] - f.eye_last[i];

        if (f.draws > 1 && std::fabs(step) > 0.05f && std::fabs(f.step_last[i]) > 0.05f && (step > 0.0f) != (f.step_last[i] > 0.0f)) {
            f.reversals[i]++;
        }
        f.step_last[i] = step;
    }
    if (f.draws > 0) {
        f.step_most = std::max(f.step_most, std::hypot(eye[0] - f.eye_last[0], eye[2] - f.eye_last[2]));
    }
    if (TestEnv("DC_COOP_TEST_ENTRANCE_BODIES") != nullptr) {
        std::fprintf(stderr, "coop: forest after draw %d: eye (%.2f %.2f %.2f) player (%.1f %.1f)\n", f.draws, eye[0], eye[1], eye[2], CharaMain.pos[0],
                     CharaMain.pos[2]);
    }
    for (int i = 0; i < 3; i++) {
        f.eye_last[i] = eye[i];
    }
    f.draws++;
}

void StandBesideArrival() {
    bool          was = g_opening;
    sceVu0FVECTOR rotation;

    g_opening = FloorIntro();
    bool opening = g_opening;

    if (opening && !was && selectMapNo == 1) { // (coop-entrance9: every forest opening tracked from its start)
        g_forest_spot_n = -1; // (coop-entrance10: chosen again this opening)
        g_forest_lead_spot = 0;
        g_forest_after = ForestAfter{};
        g_forest_after.tracking = true;
    }
    ForestAfterTick(opening); // (coop-entrance9)

    if (g_forest_tail.on && g_partner.chara != nullptr) { // (the forest's end, option A: her motion to its end, curving to her spot)
        ForestTail &ft = g_forest_tail;
        const float u = ForestCurve(g_partner.chara);

        if (u > 1.5f || opening) {
            // its end: she stands where her body is (her spot beside his end: the record's end and his facing there)
            const float y = ArrivalRecordEndYaw();
            const float *end = ArrivalRecordEnd();

            g_partner.pos[0] = end[0] + ft.side[0];
            g_partner.pos[1] = end[1];
            g_partner.pos[2] = end[2] + ft.side[1];
            g_partner.pos[3] = 1.0f;
            g_partner.chara->frame->SetPosition(g_partner.pos);
            g_partner.chara->frame->SetRotation(0.0f, y, 0.0f);
            PlayMotion(0, 0);
            ft.on = false;
            std::fprintf(stderr, "coop: opening: the forest's end: her walk-in ended beside him at (%.0f %.0f %.0f), curved (no hop)\n", g_partner.pos[0],
                         g_partner.pos[1], g_partner.pos[2]);
        } else {
            g_partner.pos[0] = ft.niche[0] + u * ft.side[0];
            g_partner.pos[1] = ft.niche[1];
            g_partner.pos[2] = ft.niche[2] + u * ft.side[1];
            g_partner.pos[3] = 1.0f;
            g_partner.chara->frame->SetPosition(g_partner.pos);
        }
        return;
    }

    if (!opening && !was) {
        return;
    }
    // coop-entrwin: a walker's own entrance in the floor's entrance window places the walkers (ArrivalWalkPartner, ArrivalWalkPlayerOne;
    // player 1's by the partner while her walk-in is hers, EntranceWalkByPartner) and leaves the lead where its walk-in leaves it
    // (her, the partner after the handover: this pass at the opening's end put her back beside his spot, sr0).
    if (EntranceWalkOn()) {
        return;
    }
    // coop-entrance2, rule 1: an opening that begins with the partner here began for both on one tick (together): delays apply.
    // One whose floor saw an entrance window's join on this copy (the other's state taken mid-walk-in, before the lockstep starts
    // the window's walk on both copies) is no together start: nothing here till it ends (x-entrwin-p2first went OUT OF SYNC at
    // tick 60 when one copy alone began a trail and held her motion at that merge).
    static bool joined_mid = false;

    if (opening && !was) {
        joined_mid = EntranceWindowSeen();
        std::fprintf(stderr, "coop: opening: map %d floor key %d begins on this copy: %s\n", selectMapNo, UserStatus != nullptr ? selectMapNo * 256 + UserStatus->cur_floor : -1,
                     joined_mid ? "joined mid-way in an entrance window (no together start)" : "together start");
        if (!joined_mid) {
            g_together = UserStatus != nullptr ? selectMapNo * 256 + UserStatus->cur_floor : -1000; // (FloorKey)
        }
    }
    if (joined_mid) {
        joined_mid = opening;
        return;
    }
    // coop-entrance2, rule 2: Moon Sea's pad: the partner at its character's spot round it, standing, not drawn before its beam
    // (EntrancePartnerHidden); the lead is put at its own by MoonEntranceTick.
    if (MoonEntrance() && g_partner.chara != nullptr) {
        float yaw = 0.0f;

        if (MoonSpot(g_partner.loaded_chara, g_partner.pos, &yaw)) {
            g_partner.velocity[0] = g_partner.velocity[1] = g_partner.velocity[2] = 0.0f;
            g_partner.chara->frame->SetPosition(g_partner.pos);
            g_partner.chara->frame->SetRotation(0.0f, yaw, 0.0f);
            if (opening && g_partner.motion != 0) {
                PlayMotion(0, 0);
            }
            if (opening && !was) {
                std::fprintf(stderr, "coop: opening: map %d partner %s at its pad spot (%.1f %.1f %.1f), together: delay %d, lead %s delay %d\n", selectMapNo,
                             EntranceCharaName(g_partner.loaded_chara), g_partner.pos[0], g_partner.pos[1], g_partner.pos[2],
                             EntranceTogetherOffset(g_partner.loaded_chara), EntranceCharaName(EntranceLeadChara()), EntranceTogetherOffset(EntranceLeadChara()));
            }
            return;
        }
    }

    // Where the player is drawn and which way it faces. An opening that carries the player on a scene's
    // node (Divine Beast Cave's in01: its frame references the node, CharaMain.pos is (0 0 0) under it
    // until the script lets go) draws it far from CharaMain.pos; the partner walks in beside the
    // drawn player. The twin reads CharaMain.pos and its own rotation, as before (beside the map's
    // corner through the cave's opening).
    sceVu0FVECTOR player;
    float         axis_x[2];
    float         axis_z[2];

    CharaMain.GetRotation(rotation);
    PlayerDrawnPlace(player, axis_x, axis_z);

    // At the player's right hand, else its left, behind it or before it: the first the player could
    // walk to, so the partner and its camera do not start in a rock or a tree. Picked when the opening
    // starts and again when a scene's node takes the player (the cut to where it carries him), then
    // held to the end: picked again each tick, the side flipped as the player walked in and she
    // jumped round him.
    // A cut while the opening runs (Wise Owl Forest's script puts the player at the stairs a few ticks
    // in; she kept the side picked where he stood before, in a rock there) picks again; the twin
    // (opening_cut) does not. The cut at its end, to where his arrival left him, keeps the side.
    static const float kSides[4] = {0.0f, PI, HALF_PI, -HALF_PI};
    static const char *const kSideNames[4] = {"right", "left", "front", "behind"};
    static int         held = 0;
    static bool        was_carried = false;
    static float       last[2] = {0.0f, 0.0f};
    bool               carried = CharaMain.frame != nullptr && CharaMain.frame->reference != 0;
    bool               cut = std::hypot(player[0] - last[0], player[2] - last[1]) > 8.0f && !CoopMutate("opening_cut");
    bool               pick = (opening && (!was || cut)) || (carried && !was_carried);

    last[0] = player[0];
    last[1] = player[2];

    // A carried player walks a way the scene laid down (the cave's stairs, too narrow for her at his
    // side: she walked in the stair wall). From the first tick a node carries him to the opening's end,
    // she follows his own way kTrailTicks behind him, kTrailSide to his right, playing what he played
    // then; before he has gone that far, she is that far back along the way he first faced. The twin
    // (opening_trail) keeps her at his side.
    // coop-entrance2, rule 1: together, she follows his way her character's delay from his (Toan 0, Xiao 24: the 20 ticks the cave
    // had); on an entrance of the lead's own (EntranceSpot::own: Wise Owl Forest, the Temple) at his very place, playing what he
    // played that long before (her own stairs motion), and at its end she stands at the table's spot beside him.
    // DC_COOP_MUTATE=entrance_delay_off (twin): the cave's 20 behind, the forest and temple at the table's spot from the start.
    // (her own stairs motion loaded first: the check below reads it on the opening's first tick)
    if (opening && !was && !CoopMutate("no_arrival")) {
        LoadArrival();
    }
    const EntranceSpot *own_spot = EntranceSpotFor(selectMapNo, 2);
    int                 own_motion = -1;
    const bool          own = own_spot != nullptr && own_spot->own && !CoopMutate("entrance_delay_off") && g_partner.chara != nullptr &&
                     (selectMapNo != 1 || g_partner.chara->GetMotionParam(100, &own_motion, nullptr, nullptr, nullptr) != nullptr);
    const int           lag = CoopMutate("entrance_delay_off") || selectMapNo == 0 ? 20 // (the cave: v0.1.1's trail, round 6)
                                                               : std::max(1, std::abs(EntranceTogetherOffset(g_partner.loaded_chara) - EntranceTogetherOffset(EntranceLeadChara())));
    const int           kTrailTicks = lag;
    constexpr int       kTrailSize = 160;
    // (DC_COOP_MUTATE=cave_trail_side_off (twin): the cave's trail on his very way, no side: she walks into him at the stairs' foot)
    const float         kTrailSide = own ? own_spot->way_right : selectMapNo == 0 && CoopMutate("cave_trail_side_off") ? 0.0f : 7.0f;
    // (option C: a way the script walks him along, her first in the order: she on his live way, he drawn behind her. DC_COOP_MUTATE=
    // entrance_lead_plays (twin): he plays it whoever comes first, she trails him, as round 2 had it.)
    const bool          swap = own && selectMapNo != 1 && EntranceTogetherOffset(g_partner.loaded_chara) < EntranceTogetherOffset(EntranceLeadChara()) &&
                      !CoopMutate("entrance_lead_plays");
    constexpr float kTrailStep = 0.67f; // the cave's node, units a tick
    struct TrailSample {
        float place[3];
        float axis_x[2];
        float axis_z[2];
        int   motion;
        int   flags;
        float door; // (round 7: the stairwell door's height then, in_door; 0 none)
    };
    static TrailSample trail[kTrailSize];
    static int         trail_n = 0;
    static bool        trailing = false;
    static float       own_start[2] = {0.0f, 0.0f}; // (where he stood as the opening began: an own entrance's way moves him or not)
    // coop-entrance2 (Jep 11-12, cave clip: "Xiao and Toan 100% on top of each other"): on his way down the stairs she is 18 behind
    // (her 24 ticks), but at the stairs' foot he stops and she came on, then eased to the table's spot 7 to his side: the hips 5.3
    // apart (tick 211), 7.0 standing, 4.2 as he moves on (tick 256). Now she waits on his way while her next step would bring her
    // within kHoldBack of him (her delay grows by the ticks she waits: hold_lag) and goes on when he does; no ease to his side,
    // and the table's spot once the walk-in ends (the end only: the camera's cut from the stairs' fixed camera to the follow one).
    // DC_COOP_MUTATE=entrance_trail_close (twin): she comes on and eases to his side, as before.
    constexpr float    kHoldBack = 10.0f;
    static int         hold_lag = 0;

    if (opening && !was) {
        trailing = own; // (the cave's starts when the node takes him)
        trail_n = 0;
        hold_lag = 0;
        g_walked_in = false;
        if (swap) {
            std::fprintf(stderr, "coop: opening: map %d together: %s plays the walk-in at 0 (first in the order), %s %d ticks after, drawn in the "
                                 "game's player's place\n",
                         selectMapNo, EntranceCharaName(g_partner.loaded_chara), EntranceCharaName(EntranceLeadChara()), lag);
        } else if (selectMapNo == 0 && !own) {
            std::fprintf(stderr, "coop: opening: map 0 together: v0.1.1's trail: partner %s %d ticks behind %s, %.0f to his right\n",
                         EntranceCharaName(g_partner.loaded_chara), lag, EntranceCharaName(EntranceLeadChara()), kTrailSide);
        } else if (own) {
            std::fprintf(stderr, "coop: opening: map %d partner %s together at the lead's own entrance, %d ticks after %s (delays %d and %d)\n", selectMapNo,
                         EntranceCharaName(g_partner.loaded_chara), lag, EntranceCharaName(EntranceLeadChara()), EntranceTogetherOffset(g_partner.loaded_chara),
                         EntranceTogetherOffset(EntranceLeadChara()));
        }
    }

    if (carried && !was_carried && !CoopMutate("opening_trail")) {
        trailing = true;
    }

    was_carried = carried;

    const TrailSample *follow = nullptr;
    float              back = 0.0f;
    bool               trail_wait = false; // (an own entrance's follower still at his way's first sample, its delay to come: not drawn)
    int                follow_tick = 0;    // (round 7: the trail tick the follower stands at)

    // (an own entrance's early cut, the script standing him at the map's corner first: his way starts again from where it puts him)
    static float own_ease = 0.0f; // (0 on his way, 1 at the table's spot beside him: eased once he stops)
    static float forest_lead_from[3] = {0.0f, 0.0f, 0.0f}; // (coop-entrance10: where the lead's walk-on began)
    static bool  forest_partner_done = false; // (the forest: the partner's stairs motion played out while the lead's walk-in runs)
    static float forest_walked = 0.0f;        // (round 7: how far the one played out first has walked on, and whether it still walks)
    static bool  forest_walking = false;
    static bool  forest_lead_on = false;      // (round 7: the lead, played out first, walks on as his stand-in while held)
    static float forest_lead_walked = 0.0f;
    const ForestSpot *forest_partner_spot = nullptr; // (coop-entrance10: the partner's walk-on end this tick, when it walks on)
    bool              forest_partner_stops = false;  // (and whether it reaches it this tick)

    g_walked_in = g_walked_in || (opening && was && cut);
    if (own && ((cut && trail_n < 30) || (opening && !was))) {
        trail_n = 0;
        hold_lag = 0;
        own_start[0] = player[0];
        own_start[1] = player[2];
        own_ease = 0.0f;
    }
    const bool hold_back = own && selectMapNo == 3 && !CoopMutate("entrance_trail_close"); // (the temple; the cave is v0.1.1's, round 6)

    if (trailing && !(own && !opening)) { // (an own entrance's end: beside him, the table's spot)
        TrailSample &now = trail[trail_n % kTrailSize];

        for (int i = 0; i < 3; i++) {
            now.place[i] = player[i];
        }

        for (int i = 0; i < 2; i++) {
            now.axis_x[i] = axis_x[i];
            now.axis_z[i] = axis_z[i];
        }

        now.motion = CharaMain.motion_no;
        {
            CFrame *pieces[2] = {nullptr, nullptr};
            int     axis = 1;

            if (EntranceDoors(pieces, &axis) > 0 && pieces[0]->position[axis] != g_door_written) {
                g_door_script = pieces[0]->position[axis];
            }
            now.door = g_door_script;
        }
        now.flags = CharaMain.motion_flags;
        trail_n++;

        if (trail_n > kTrailTicks + hold_lag) {
            // (the cave: she waits where her next step would bring her within kHoldBack of him)
            // (never past the record's first sample: a negative index read outside the ring, x-entrance-temple's crash)
            // (round 6, the temple: her start itself 7 beside him while he stands in the door: 7.0 at tick 30; she waits there, not
            // drawn, till her first step clears kHoldBack)
            if (hold_back && kTrailTicks + hold_lag < kTrailSize - 2) {
                const TrailSample &next = trail[(trail_n - 1 - kTrailTicks - hold_lag) % kTrailSize];
                const float        nx = next.place[0] - kTrailSide * next.axis_x[0];
                const float        nz = next.place[2] - kTrailSide * next.axis_x[1];

                hold_lag += std::hypot(nx - player[0], nz - player[2]) < kHoldBack ? 1 : 0;
            }
            const int index = trail_n - 1 - kTrailTicks - hold_lag;

            follow = &trail[std::max(index, 0) % kTrailSize];
            follow_tick = std::max(index, 0);
            trail_wait = own && opening && index < 0; // (held at his start: not drawn)
        } else {
            follow = &trail[0];
            back = own ? 0.0f : kTrailStep * (kTrailTicks - (trail_n - 1));
            // (coop-entrance2: an own entrance's follower waits its delay at his start, where he sets out from: not drawn till it
            // sets out, as the forest's one waiting; cave-p1: the stand-in stood on her start, 0.2 apart)
            trail_wait = own && opening;
        }
        // coop-entrance2 round 7 (Jep 11-12, temple clip: "the door closes too soon: after the lead walks through, before the later
        // walker is through"): the script slides the stairwell's door (in_door) as the lead's walk-in goes, so it shut on the one
        // behind him. While a follower is behind, the door stands where it was at the follower's moment of his way whenever that
        // is more open than now (further from where it stood as the trail began): it shuts behind the last walker. On both copies
        // alike (the trail is game state). DC_COOP_MUTATE=entrance_door_lead_only (twin): the lead's door, as before.
        {
            static float door_start = 0.0f;
            static float door_most = 0.0f;  // (the live door's furthest from its start so far)
            static int   lead_closing = -1; // (the trail tick the live door began to close: the lead's closing point)
            static int   live_closed = -1;  // (the tick it begins closing as drawn)
            static int   through = -1;      // (the tick the follower reached the lead's closing point)
            static bool  logged = false;
            static float curve[256];        // (coop-entrance8: the script's close, recorded from its first frame, one a tick)
            static int   curve_n = 0;
            static bool  replayed = false;
            CFrame      *pieces[2] = {nullptr, nullptr};
            int          axis = 1;
            const int    door_n = EntranceDoors(pieces, &axis);
            CFrame      *door = door_n > 0 ? pieces[0] : nullptr;

            if (trail_n == 1) {
                door_start = door_most = now.door;
                lead_closing = live_closed = through = -1;
                logged = replayed = false;
                curve_n = 0;
                g_door_written = 1e9f;
                g_door_draw = 1e9f;
                g_door_drawn = DoorDrawn{};
            }
            if (door != nullptr && opening) {
                const float live = g_door_script; // (the script's own: the lead's door)

                if (std::fabs(live - door_start) >= std::fabs(door_most - door_start)) {
                    door_most = live;
                } else if (lead_closing < 0 && std::fabs(door_most - door_start) > 1.0f) {
                    lead_closing = trail_n - 1;
                    curve[curve_n++] = door_most; // (its first frame: fully open)
                }
                if (lead_closing >= 0 && curve_n < 256 && (curve_n == 0 || live != curve[curve_n - 1] || std::fabs(live - door_start) > 0.01f)) {
                    curve[curve_n++] = live;
                }
                if (through < 0 && lead_closing >= 0 && follow_tick >= lead_closing) {
                    through = trail_n - 1;
                }
                // (drawn: as the game has it till the script begins its close; then fully open till the follower is through; then the
                // recorded close from its first frame, one a tick; as the game has it once played)
                const int   k = through >= 0 ? trail_n - 1 - through : -1;
                const bool  delay = lead_closing >= 0 && !CoopMutate("entrance_door_lead_only") && follow != nullptr && k < curve_n;
                const float held = !delay ? live : k < 0 ? door_most : curve[std::min(k, curve_n - 1)];

                g_door_draw = delay ? held : 1e9f;
                if (k == 0) {
                    replayed = true;
                }
                // (as it has played: the close's frames in order, one a tick, from its first after the follower was through; x-entrance
                // reads it. The game's own close had ended by then: the script's value back where it began)
                if (replayed && k == curve_n && std::fabs(live - door_start) < 0.01f && !CoopMutate("entrance_door_lead_only")) {
                    std::fprintf(stderr, "coop: opening: map %d door: retail's close played in order, %d frames from %.1f to %.1f, from trail tick %d; "
                                         "the follower through at %d, the lead's close began at %d\n",
                                 selectMapNo, curve_n, curve[0], curve[curve_n - 1], through, through, lead_closing);
                    std::fprintf(stderr, "coop: opening: map %d door drawn: %d draws, the halves' largest step %.2f/%.2f, side changes %d/%d, opened again %d/%d\n",
                                 selectMapNo, g_door_drawn.draws, g_door_drawn.most_step[0], g_door_drawn.most_step[1], g_door_drawn.side_changes[0],
                                 g_door_drawn.side_changes[1], g_door_drawn.reopened[0], g_door_drawn.reopened[1]);
                }
                if (TestEnv("DC_COOP_TEST_ENTRANCE_BODIES") != nullptr && (trail_n - 1) % 20 == 0) {
                    std::fprintf(stderr, "coop: entrance door: trail %d pieces %d script %.1f drawn %.1f follower's %.1f (at its tick %d)\n", trail_n - 1, door_n, live,
                                 held, follow != nullptr ? follow->door : -999.0f, follow_tick);
                }
                if (live_closed < 0 && lead_closing >= 0 && std::fabs(held - door_start) < std::fabs(door_most - door_start) - 0.5f) {
                    live_closed = trail_n - 1;
                }
                if (!logged && live_closed >= 0) {
                    logged = true;
                    std::fprintf(stderr, "coop: opening: map %d door: the lead's closing at trail tick %d, the follower through at %d, it closes as drawn at %d\n",
                                 selectMapNo, lead_closing, through, live_closed);
                }
            }
        }
        // (option C: she on his live way; the lagged sample is his stand-in's, at the follower's side of the way)
        g_swap_stand_in.on = swap && opening;
        if (g_swap_stand_in.on) {
            g_swap_stand_in.chara = EntranceLeadChara();
            g_swap_stand_in.place[0] = follow->place[0] - kTrailSide * follow->axis_x[0];
            g_swap_stand_in.place[1] = follow->place[1];
            g_swap_stand_in.place[2] = follow->place[2] - kTrailSide * follow->axis_x[1];
            g_swap_stand_in.yaw = atan2f(follow->axis_z[0], follow->axis_z[1]);
            g_swap_stand_in.motion = follow->motion;
            g_swap_stand_in.flags = trail_n <= kTrailTicks ? (follow->flags | 1) : follow->flags; // (still till his offset has passed)
            g_swap_stand_in.waiting = trail_wait && !CoopMutate("entrance_wait_drawn");
            follow = &now;
        }
    } else {
        g_swap_stand_in.on = false;
    }

    if (follow != nullptr) {
        const float side_now = g_swap_stand_in.on ? 0.0f : kTrailSide; // (option C: she first, on his very way)

        g_partner.pos[0] = follow->place[0] - side_now * follow->axis_x[0] - back * follow->axis_z[0];
        g_partner.pos[1] = follow->place[1];
        g_partner.pos[2] = follow->place[2] - side_now * follow->axis_x[1] - back * follow->axis_z[1];
        // coop-entrance2: an own entrance whose way moves him (the Temple's door and steps; the forest's niche does not: his motion
        // carries his body there) has her come up to his side as she closes on him (inside two spawn distances), so where he stops
        // first she stands beside him at the table's spot, not in him (probe e1: she reached his very place and stood inside him).
        // (he stops: his place 10 ticks ago within 1; the ease moves 1/20 a tick, so she walks over rather than jumps)
        const TrailSample &ago = trail[(trail_n - 1 - std::min(trail_n - 1, 10) + kTrailSize) % kTrailSize];
        const bool         stopped = trail_n > 10 && std::hypot(player[0] - ago.place[0], player[2] - ago.place[2]) < 1.0f;

        if (own && !hold_back && std::hypot(player[0] - own_start[0], player[2] - own_start[1]) > 1.0f) {
            const float gap = std::hypot(g_partner.pos[0] - player[0], g_partner.pos[2] - player[2]);
            const float want = stopped ? std::clamp(2.0f - gap / kSpawnDistance, 0.0f, 1.0f) : 0.0f;

            own_ease = want > own_ease ? std::min(want, own_ease + 0.05f) : want;
            const float s = own_ease;
            const float side[2] = {-own_spot->right * axis_x[0] + own_spot->along * axis_z[0], -own_spot->right * axis_x[1] + own_spot->along * axis_z[1]};

            g_partner.pos[0] = g_partner.pos[0] + s * (player[0] + side[0] - g_partner.pos[0]);
            g_partner.pos[2] = g_partner.pos[2] + s * (player[2] + side[1] - g_partner.pos[2]);
        }
        // coop-entrance2 (the forest's step, kForestOrderStep; x-entrance-forest-p1: P1 Xiao leads, P2 Toan first): the lead held
        // 220 ticks holds its script, so the partner's motion ends first, at the niche's one end, where the lead's own then comes (0.0
        // apart at tick 558). Played out, the partner walks over to the table's spot beside him (eased, 1/20 a tick) while the
        // lead's walk-in still runs. DC_COOP_MUTATE=forest_partner_stays (twin): stays at its motion's end, as before.
        // Round 7: it walks on to the first forward spot (ForestChooseSpots: the first end), its own walk, not eased.
        if (own && selectMapNo == 1 && opening && EntranceTogetherOffset(g_partner.loaded_chara) < EntranceTogetherOffset(EntranceLeadChara()) &&
            !CoopMutate("forest_partner_stays")) {
            forest_walked = forest_partner_done ? forest_walked + kForwardStep : 0.0f;
            // (coop-entrance10: the ends chosen on the floor from the niche's end, the lead's game place while his walk-in is drawn;
            // the first to finish of two takes the second end, the lead, last, the first; not on the opening's first tick, where
            // forest_partner_done is still the last opening's till it is cleared below and the lead still at his arrival's old place)
            if (g_forest_spot_n < 0 && forest_partner_done && was) {
                ForestChooseSpots(player, atan2f(axis_z[0], axis_z[1]));
            }
            if (g_forest_spot_n > 0) {
                const ForestSpot &spot0 = g_forest_spots[std::min(1, g_forest_spot_n - 1)];
                const float       s = std::min(forest_walked, spot0.dist) / spot0.dist;

                g_partner.pos[0] = spot0.end[0] - (1.0f - s) * spot0.dist * sinf(spot0.dir);
                g_partner.pos[2] = spot0.end[2] - (1.0f - s) * spot0.dist * cosf(spot0.dir);
                forest_walking = forest_walked < spot0.dist;
                forest_partner_spot = &spot0;
                forest_partner_stops = forest_walked >= spot0.dist && forest_walked - kForwardStep < spot0.dist;
            }
        }
    } else if (own && selectMapNo == 1 && !opening && was && !CoopMutate("forest_lead_stays")) {
        // (round 7: the forest's opening ends with every walker where its walk left it: the last at the niche's end, the others
        // on their forward spots; nobody hops)
        held = -1;
    } else if (const EntranceSpot *spot = EntranceSpotFor(selectMapNo, 2); spot != nullptr) {
        // coop-entrance: the entrance's own spot for this walker, in the lead's frame (no walk check: the walk-in is the
        // arrival motion's, which carries the body out of the stairs' niche whatever stands round the spot).
        g_partner.pos[0] = player[0] - spot->right * axis_x[0] + spot->along * axis_z[0];
        g_partner.pos[1] = player[1];
        g_partner.pos[2] = player[2] - spot->right * axis_x[1] + spot->along * axis_z[1];
        held = -1;
    } else {
        for (int i = pick ? 0 : held < 0 ? 0 : held; i < 4; i++) {
            float side_x = -kSpawnDistance * cosf(kSides[i]);
            float side_z = kSpawnDistance * sinf(kSides[i]);

            g_partner.pos[0] = player[0] + side_x * axis_x[0] + side_z * axis_z[0];
            g_partner.pos[1] = player[1];
            g_partner.pos[2] = player[2] + side_x * axis_x[1] + side_z * axis_z[1];

            held = i;

            if (!pick || PathClear(player, g_partner.pos)) {
                break;
            }
        }
    }

    // coop-entrance: each pick (the opening's start, a cut), where the walker starts in the lead's frame (right of him, along
    // his way: + before him) and from what (the entrance's table, the side table, his trail), with which sides his walk
    // reaches; on stderr, beside the opening's other lines (x-entrance reads it).
    if (pick) {
        const float rx = g_partner.pos[0] - player[0];
        const float rz = g_partner.pos[2] - player[2];
        char        clear[4];

        for (int i = 0; i < 4; i++) {
            const float sx = -kSpawnDistance * cosf(kSides[i]);
            const float sz = kSpawnDistance * sinf(kSides[i]);
            const float at[4] = {player[0] + sx * axis_x[0] + sz * axis_z[0], player[1], player[2] + sx * axis_x[1] + sz * axis_z[1], 1.0f};

            clear[i] = PathClear(player, at) ? 'y' : 'n';
        }
        std::fprintf(stderr, "coop: opening: map %d walker 2 starts %.0f right, %.0f along of the player (%s) at (%.0f %.0f %.0f); player at (%.0f %.0f %.0f) "
                             "motion %d; sides clear right %c left %c front %c behind %c\n",
                     selectMapNo, -(rx * axis_x[0] + rz * axis_x[1]), rx * axis_z[0] + rz * axis_z[1],
                     follow != nullptr ? "trail" : held < 0 ? "entrance table" : kSideNames[held], g_partner.pos[0], g_partner.pos[1], g_partner.pos[2],
                     player[0], player[1], player[2], CharaMain.motion_no, clear[0], clear[1], clear[2], clear[3]);
    }

    const float *facing = follow != nullptr ? follow->axis_z : axis_z;

    g_partner.pos[3] = 1.0f;
    g_partner.velocity[0] = g_partner.velocity[1] = g_partner.velocity[2] = 0.0f;
    g_partner.chara->frame->SetPosition(g_partner.pos);
    const EntranceSpot *turned = follow == nullptr && held < 0 ? EntranceSpotFor(selectMapNo, 2) : nullptr;

    // coop-entrance10 (forest-p1: Toan, the partner, played out first, walked on to his end at -30 degrees still facing the lead's
    // way, 30 off his walk): the partner walking on faces the way it walks, as the lead does. DC_COOP_MUTATE=forest_partner_faces_lead
    // (twin): the lead's way, as before.
    const float partner_yaw = forest_partner_spot != nullptr && !CoopMutate("forest_partner_faces_lead")
                                  ? forest_partner_spot->dir
                                  : atan2f(facing[0], facing[1]) + (turned != nullptr ? turned->turn : 0.0f);

    g_partner.chara->frame->SetRotation(0.0f, partner_yaw, 0.0f);
    // (as it stops: its body's clearance there and on the way from 10, by the game's MoveCheck, and its facing against its walk; a log
    // for x-entrance-forest-p1)
    if (forest_partner_stops) {
        const ForestSpot &s = *forest_partner_spot;
        float             way_least = 12.0f;

        for (float along = 10.0f; along < s.dist; along += 4.0f) {
            const float at[4] = {s.end[0] - (s.dist - along) * sinf(s.dir), s.end[1], s.end[2] - (s.dist - along) * cosf(s.dir), 1.0f};

            way_least = std::min(way_least, ForestClearance(at));
        }
        float turn = std::fabs(std::remainder(partner_yaw - s.dir, 2.0f * PI));

        std::fprintf(stderr, "coop: opening: map 1 the partner's walk-on ended %.0f on at %+d degrees: body clearance %.0f at the end, %.0f at the least on "
                             "the way from 10; facing %.0f degrees from the way it walked\n",
                     s.dist, s.deg, ForestClearance(s.end), way_least, turn * 180.0f / PI);
    }

    // The partner arrives as the player does: the motions the opening plays on the player (100, its
    // arrival, held on its last frame), from their first frame, where the partner's model has them.
    if (opening && !was && !CoopMutate("no_arrival")) {
        LoadArrival();
    }

    // Following his way, what he played when he was where she is.
    int index = -1;
    int motion = follow != nullptr ? follow->motion : CharaMain.motion_no;
    int flags = follow != nullptr ? follow->flags : CharaMain.motion_flags;

    // coop-entrance2: on an own entrance, before her delay has passed (his way's first sample stands in for hers), she holds
    // what he began with on its first frame (the forest's stairs motion: her body still in the niche), and starts it when it has
    // (probe e3: the cut at the opening's fourth tick put his stairs motion in that first sample, and she set out with him).
    static bool own_held = false;
    bool        hold = own && opening && follow != nullptr && trail_n <= kTrailTicks && !g_swap_stand_in.on;

    // coop-entrance2, rule 1 for the lead too (Jep 11-12: "the lead waits its own delay too": P1 Xiao and P2 Toan together, Toan
    // at 0 and Xiao 24 after): where the stairs motion carries the body from a still place (Wise Owl Forest), each one's motion
    // starts its character's delay after his began (the shared start), his held on its first frame for his own delay, hers
    // held till her own: no trail of his, so she may go first. DC_COOP_MUTATE=entrance_lead_unheld (twin): his not held, as
    // before (her after him whatever their delays). (A way the script walks him along, the temple's, the cave's: she still
    // trails him, her delay after his: holding his walk would hold the scene.)
    static int  own_tick = 0;
    static int  shared_start = -1; // (own_tick his stairs motion began, -1 not yet)
    static int  started[2] = {-1, -1}; // (own_tick his and her motion started)
    static float lead_played = 0.0f;   // (ticks of his stairs motion played as his hold ends: ~0 when it held)
    const bool  motion_way = own && selectMapNo == 1;

    g_wait_hidden[0] = false;
    g_wait_hidden[1] = trail_wait && !g_swap_stand_in.on; // (the follower waiting at his start: above)

    if (opening && !was) {
        own_tick = 0;
        forest_partner_done = false;
        forest_walked = 0.0f;
        forest_walking = false;
        forest_lead_on = false;
        forest_lead_walked = 0.0f;
        shared_start = -1;
        started[0] = started[1] = -1;
    }
    own_tick += opening ? 1 : 0;
    if (motion_way && opening) {
        if (shared_start < 0 && CharaMain.motion_no == 100) {
            shared_start = own_tick;
        }
        if (shared_start >= 0) {
            const int lead_wait = CoopMutate("entrance_lead_unheld") ? 0 : EntranceTogetherOffset(EntranceLeadChara());
            int       partner_wait = EntranceTogetherOffset(g_partner.loaded_chara);

            // (a probe's other lag after his start: DC_COOP_TEST_FOREST_LAG=N, read each tick)
            if (const char *lag_test = TestEnv("DC_COOP_TEST_FOREST_LAG"); lag_test != nullptr) {
                partner_wait = lead_wait + std::atoi(lag_test);
            }

            int             lead_index = -1;
            tagMOTION_TYPE *lead_motion = CharaMain.GetMotionParam(100, &lead_index, nullptr, nullptr, nullptr);

            if (own_tick < shared_start + lead_wait) {
                // (held: its time put back to its first frame each tick, whoever steps it: the event's player is stepped by the game
                // with its own flags each tick, so a hold flag of ours was lost, probe: 25 ticks played in a 24-tick hold)
                if (lead_motion != nullptr && lead_motion->motion_info != nullptr && lead_index >= 0) {
                    lead_motion->state.time = static_cast<float>(lead_motion->motion_info[lead_index].start);
                }
            } else if (started[0] < 0) {
                started[0] = own_tick - shared_start;
                lead_played = lead_motion != nullptr && lead_motion->motion_info != nullptr && lead_index >= 0
                                  ? (CharaMain.GetNowTime() - lead_motion->motion_info[lead_index].start) /
                                        std::max(lead_motion->motion_info[lead_index].speed, 0.01f)
                                  : -1.0f;
            }
            motion = 100;
            flags = CharaMain.motion_flags & ~1;
            hold = own_tick < shared_start + partner_wait;
            // (each one waiting for its start is not drawn: EntranceWaitHidden)
            g_wait_hidden[0] = own_tick < shared_start + lead_wait;
            g_wait_hidden[1] = hold;
            // (the partner's own stairs motion played to its end: walks over to the table's spot, above)
            int             partner_index = -1;
            tagMOTION_TYPE *partner_motion = g_partner.chara->GetMotionParam(100, &partner_index, nullptr, nullptr, nullptr);

            // (sticky for the opening: once played out it stays so, whatever she plays next)
            forest_partner_done = forest_partner_done ||
                                  (!hold && partner_motion != nullptr && partner_motion->motion_info != nullptr && partner_index >= 0 && g_partner.chara->motion_no == 100 &&
                                   g_partner.chara->GetNowTime() >= partner_motion->motion_info[partner_index].end - 1.0f);
            // coop-entrance2 round 6 (Jep 11-12: "the opening should end after the LAST walker's animation ends"): the game's script
            // ends the opening when the lead's stairs motion has ended (forest-p1: his held 220 ticks, its opening 563), so a lead
            // that walks in first is held on its last frame till the partner's has played out. DC_COOP_MUTATE=forest_opening_lead_ends
            // (twin): the opening ends with the lead's, as round 5 (her hop at the cut).
            if (partner_wait > lead_wait && started[0] >= 0 && !forest_partner_done && !CoopMutate("forest_opening_lead_ends") && lead_motion != nullptr &&
                lead_motion->motion_info != nullptr && lead_index >= 0 && CharaMain.motion_no == 100 &&
                lead_motion->state.time >= lead_motion->motion_info[lead_index].end - 2.0f) {
                lead_motion->state.time = static_cast<float>(lead_motion->motion_info[lead_index].end) - 2.0f;
                // (round 7: meanwhile he walks on to the first forward spot, drawn as his stand-in walking (his walk motion, then
                // standing); the game's player is put there as the opening ends. DC_COOP_MUTATE=forest_lead_stays (twin): he
                // stands at the niche's end, her walk-in coming out onto him.)
                if (!forest_lead_on && !CoopMutate("forest_lead_stays")) {
                    forest_lead_on = true;
                    forest_lead_from[0] = player[0];
                    forest_lead_from[1] = player[1];
                    forest_lead_from[2] = player[2];
                    g_forest_after.walk_ticks = 0; // (the stand-in's first ticks: drawn and stepped from its third)
                    std::fprintf(stderr, "coop: opening: map 1 the lead's walk-in played out at tick %d: he walks on to the first forward spot\n", own_tick);
                }
            }
            if (forest_lead_on) {
                forest_lead_walked += kForwardStep;
                if (g_forest_spot_n < 0) { // (coop-entrance10: the end chosen on the floor, his facing as his walk-in left him)
                    ForestChooseSpots(player, atan2f(axis_z[0], axis_z[1]));
                }
                const ForestSpot &mine = g_forest_spots[g_forest_lead_spot];
                const float       way[2] = {mine.end[0] - player[0], mine.end[2] - player[2]};
                const float       far = CoopMutate("forest_walk_in_view") ? 8.0f : std::hypot(way[0], way[1]);
                const float       yaw0 = std::hypot(way[0], way[1]) > 0.0f ? atan2f(way[0], way[1]) : mine.dir;
                const float       to[2] = {far * sinf(yaw0), far * cosf(yaw0)};
                const float s = far > 0.0f ? std::min(forest_lead_walked, far) / far : 0.0f;

                g_swap_stand_in.on = true;
                g_swap_stand_in.chara = EntranceLeadChara();
                g_swap_stand_in.place[0] = player[0] + s * to[0];
                g_swap_stand_in.place[1] = player[1];
                g_swap_stand_in.place[2] = player[2] + s * to[1];
                g_swap_stand_in.place[3] = 1.0f;
                g_swap_stand_in.yaw = atan2f(to[0], to[1]);
                g_swap_stand_in.motion = forest_lead_walked < far ? 1 : 0;
                g_swap_stand_in.flags = 0;
                g_swap_stand_in.waiting = false;
                // (as he stops: whether this copy's opening camera still draws any of him, feet, waist or head; a log, draw only, for
                // x-entrance-forest's "out of view" check)
                if (forest_lead_walked >= far && forest_lead_walked - kForwardStep < far) {
                    int in_view = 0;

                    for (const float up : {0.0f, 8.0f, 18.0f}) {
                        float point[4] = {g_swap_stand_in.place[0], g_swap_stand_in.place[1] + up, g_swap_stand_in.place[2], 1.0f};
                        int   screen[4] = {0, 0, 0, 0};
                        const int on = MGRotTransPers(screen, point, 0);
                        const int x = (screen[0] - 0x6C00) >> 4;
                        const int y = ((screen[1] - GS_Y_OFFSET) >> 4) * 2;

                        in_view += on != 0 && x >= -20 && x <= 660 && y >= -40 && y <= 488 ? 1 : 0;
                    }
                    std::fprintf(stderr, "coop: opening: map 1 the lead stops %.0f on at tick %d, %s the opening's view on this copy (%d of feet, waist, head in it)\n",
                                 far, own_tick, in_view == 0 ? "out of" : "still in", in_view);
                }
                // (the probe's view check: where this copy's opening camera draws him; draw only, a log)
                if (TestEnv("DC_COOP_TEST_ENTRANCE_BODIES") != nullptr && own_tick % 5 == 0) {
                    float waist[4] = {g_swap_stand_in.place[0], g_swap_stand_in.place[1] + 18.0f, g_swap_stand_in.place[2], 1.0f}; // (his head)
                    int   screen[4] = {0, 0, 0, 0};
                    const int on = MGRotTransPers(screen, waist, 0);
                    const int x = (screen[0] - 0x6C00) >> 4;
                    const int y = ((screen[1] - GS_Y_OFFSET) >> 4) * 2;

                    std::fprintf(stderr, "coop: forest walk-on: tick %d walked %.1f at (%.0f %.0f) screen %d (%d %d) %s\n", own_tick, forest_lead_walked,
                                 g_swap_stand_in.place[0], g_swap_stand_in.place[2], on, x, y,
                                 on != 0 && x >= -20 && x <= 660 && y >= -40 && y <= 488 ? "in view" : "out of view");
                }
            }
            if (!hold && started[1] < 0) {
                started[1] = own_tick - shared_start;
            }
            if (started[0] >= 0 && started[1] >= 0 && (started[0] == own_tick - shared_start || started[1] == own_tick - shared_start)) {
                std::fprintf(stderr, "coop: opening: together on map %d: the stairs motions start %s %d, %s %d ticks after the shared start (the lead's played "
                                     "%.0f ticks at its start)\n",
                             selectMapNo, EntranceCharaName(EntranceLeadChara()), started[0], EntranceCharaName(g_partner.loaded_chara), started[1], lead_played);
            }
        }
    }
    if (motion_way && forest_partner_done && EntranceTogetherOffset(g_partner.loaded_chara) < EntranceTogetherOffset(EntranceLeadChara()) &&
        !CoopMutate("forest_partner_stays")) { // (round 7: the partner first, walking on, then standing)
        motion = forest_walking ? 1 : 0;
        flags = 0;
    }
    const bool has = g_partner.chara->GetMotionParam(motion, &index, nullptr, nullptr, nullptr) != nullptr && index >= 0;

    if (opening && has && (motion != g_partner.motion || hold != own_held)) {
        PlayMotion(motion, hold ? (flags | 4 | 1) : (flags | 4));
        if (own && hold != own_held) {
            std::fprintf(stderr, "coop: opening: partner's motion %d %s at trail %d (his at time %.1f)\n", motion, hold ? "held on its first frame" : "starts",
                         trail_n, CharaMain.GetNowTime());
        }
    }
    own_held = hold;


    // (the forest's end, option A: the curve while the opening runs, and her motion carried past its end)
    // (round 7: the earlier walker walks on instead: Jep, "Xiao pushes Toan aside"; the curve below is round 6's, kept for reading
    // only: cand1112c: its switch removed, no row turned it on)
    if (constexpr bool kForestCurve = false; kForestCurve && motion_way && shared_start >= 0 && g_partner.chara != nullptr) {
        const float side[2] = {-own_spot->right * axis_x[0] + own_spot->along * axis_z[0], -own_spot->right * axis_x[1] + own_spot->along * axis_z[1]};
        // (round 6: inside the opening, by her own ticks into her stairs motion (kForestWalkTicks long): over its last
        // ForestCurveTicks, smooth; ForestCurve's motion time came only on its last ticks, probe curve80: 0.0 apart at tick 556)
        constexpr float kForestWalkTicks = 343.0f;
        const float     partner_ticks = started[1] >= 0 ? static_cast<float>(own_tick - shared_start - started[1]) : -1.0f;
        const float     v = std::clamp((partner_ticks - (kForestWalkTicks - ForestCurveTicks())) / ForestCurveTicks(), 0.0f, 1.0f);
        const float     u = partner_ticks < 0.0f ? ForestCurve(g_partner.chara) : v * v * (3.0f - 2.0f * v);


        if (opening && !hold && u > 0.0f && u <= 1.0f) {
            g_partner.pos[0] = player[0] + u * side[0];
            g_partner.pos[2] = player[2] + u * side[1];
            g_partner.chara->frame->SetPosition(g_partner.pos);
        }
        if (!opening && was && u <= 1.0f) {
            g_forest_tail.on = true;
            g_forest_tail.niche[0] = trail[0].place[0];
            g_forest_tail.niche[1] = trail[0].place[1];
            g_forest_tail.niche[2] = trail[0].place[2];
            g_forest_tail.side[0] = side[0];
            g_forest_tail.side[1] = side[1];
            // (her place back in the niche, her motion where it was: the hop's placement above undone)
            g_partner.pos[0] = g_forest_tail.niche[0] + std::max(u, 0.0f) * side[0];
            g_partner.pos[1] = g_forest_tail.niche[1];
            g_partner.pos[2] = g_forest_tail.niche[2] + std::max(u, 0.0f) * side[1];
            g_partner.chara->frame->SetPosition(g_partner.pos);
            std::fprintf(stderr, "coop: opening: the forest's end: her walk-in plays on past his (%.2f of its curve), no hop\n", std::max(u, 0.0f));
        }
    }

    // (after every placement this tick: the curve's too, round 6)
    // coop-entrance2 (Jep 11-12, forest clipping: "around f240 they start clipping into each other"): both bodies through a together
    // opening: their frames' first child (the hips, where the stairs motions carry them), the nearest they come and the first tick
    // closer than kBodyApart while both are drawn past the script's cut (before it both stand at the map's corner; one waiting for
    // its start is not drawn; option C: his stand-in's place), said at its end (stderr: x-entrance reads it); every tick with
    // DC_COOP_TEST_ENTRANCE_BODIES=1.
    constexpr float kBodyApart = 8.0f;
    static float    near_min = 1e9f;
    static int      near_tick = -1;
    static int      overlap_tick = -1;

    if (opening && !was) {
        near_min = 1e9f;
        near_tick = overlap_tick = -1;
    }
    if (opening && g_partner.chara != nullptr && g_partner.chara->frame != nullptr && CharaMain.frame != nullptr &&
        CharaMain.frame->child != nullptr && g_partner.chara->frame->child != nullptr) {
        sceVu0FMATRIX his;
        sceVu0FMATRIX hers;

        CharaMain.frame->child->GetLWMatrix(his);
        g_partner.chara->frame->child->GetLWMatrix(hers);
        if (g_swap_stand_in.on) { // (option C: the game's player is drawn as the stand-in on his lagged way, the partner on the live one)
            his[3][0] = g_swap_stand_in.place[0];
            his[3][2] = g_swap_stand_in.place[2];
        }
        const float apart = std::hypot(his[3][0] - hers[3][0], his[3][2] - hers[3][2]);

        const bool both_seen = g_walked_in && !EntranceLeadHidden() && !EntrancePartnerHidden() && !EntranceSwapStandInWaiting();

        if (both_seen && apart < near_min) {
            near_min = apart;
            near_tick = own_tick;
        }
        if (both_seen && apart < kBodyApart && overlap_tick < 0) {
            overlap_tick = own_tick;
        }
        if (TestEnv("DC_COOP_TEST_ENTRANCE_BODIES") != nullptr) {
            std::fprintf(stderr, "coop: entrance bodies: tick %d lead (%.1f %.1f) partner (%.1f %.1f) apart %.1f motions %d/%d drawn %d/%d\n", own_tick,
                         his[3][0], his[3][2], hers[3][0], hers[3][2], apart, CharaMain.motion_no, g_partner.chara->motion_no, !EntranceLeadHidden(),
                         !EntrancePartnerHidden());
        }
    }
    if (!opening && was && forest_lead_on) { // (round 7: the game's player where his stand-in walked on to)
        // coop-entrance9 (Jep 11-12, clip e57: "when control returns, Toan's camera twitches rapidly"; by numbers: 150 draws, the eye's
        // step reversed 47/46 times on y/z, a 38 step): the script's end set the follow camera behind him where he stood (facing out
        // of the niche, the camera inside it, BtSystemScriptAfter); placed 30 on, still facing on, the camera sought a spot behind him
        // inside the niche's wall and was pushed out each few draws. He now turns back to face the niche (waiting for the next one
        // out) and the follow camera is set behind him at his spot, as the script's end sets it: in the open. Both copies alike
        // (MainCamera__4 is game state).
        // coop-entrance10 (Jep 11-12, clip e59: "then he turns around at the end, which looks odd"): he keeps facing the way he
        // walked (the end's camera line is clear: ForestChooseSpots) and the follow camera is set behind him at its clear length
        // (below). Left as the script set it, the camera now follows him out calmly too (e74: largest step 7.4, 2 reversals; set:
        // 0.8, none), so the old twin forest_camera_unreset is retired. DC_COOP_MUTATE=forest_turn_back (twin): turned to face
        // back, as e59.
        const float yaw = CoopMutate("forest_turn_back") ? (g_swap_stand_in.yaw > 0.0f ? g_swap_stand_in.yaw - PI : g_swap_stand_in.yaw + PI)
                                                          : g_swap_stand_in.yaw;
        {
            float turn = std::fabs(yaw - g_swap_stand_in.yaw);

            turn = turn > PI ? 2.0f * PI - turn : turn;
            const ForestSpot &s = g_forest_spots[g_forest_lead_spot];
            const float      *end = g_swap_stand_in.place;
            const float       way[2] = {end[0] - forest_lead_from[0], end[2] - forest_lead_from[2]};
            const float       walked = std::hypot(way[0], way[1]);
            float             way_least = 12.0f;

            for (float along = 10.0f; along < walked; along += 4.0f) {
                const float at[4] = {forest_lead_from[0] + way[0] * along / walked, end[1], forest_lead_from[2] + way[1] * along / walked, 1.0f};

                way_least = std::min(way_least, ForestClearance(at));
            }
            const float head[4] = {end[0], end[1] + 13.0f, end[2], 1.0f};
            const float back = ForestCameraBack(head, yaw + PI, MainCamera__4.GetHeight(), MainCamera__4.GetDistance());

            std::fprintf(stderr, "coop: opening: map 1 the lead's walk-on ended %.0f on at %+d degrees: body clearance %.0f at the end, %.0f at the least on "
                                 "the way from 10; camera line behind him clear to %.0f back; facing %.0f degrees from the way he walked\n",
                         walked, s.deg, ForestClearance(end), way_least, back, turn * 180.0f / PI);
        }

        CharaMain.SetPosition(g_swap_stand_in.place);
        if (CharaFrame != nullptr) {
            CharaFrame->SetPosition(g_swap_stand_in.place);
            CharaFrame->SetRotation(0.0f, yaw, 0.0f);
        }
        CharaMain.SetRotation(0.0f, yaw, 0.0f);
        // (his own poncho, a cloth, put on him at rest where he now stands, not swung 30 across from the niche: coop-entrance9)
        if (!CoopMutate("standin_cloth_unstepped")) {
            for (int i = 0; CharaMain.cloth != nullptr && i < 8 && CharaMain.cloth[i] != nullptr; i++) {
                CharaMain.cloth[i]->Clear();
            }
        }
        {
            float angle = yaw + PI_SHORT;

            if (!(angle <= PI_SHORT)) {
                angle -= TWO_PI;
            }
            MainCamera__4.SetAngleSoon(angle);
            MainCamera__4.SetFollow(g_swap_stand_in.place[0], g_swap_stand_in.place[1] + 16.0f - 3.0f, g_swap_stand_in.place[2]);
            // coop-entrance10 (x-entrance-forest e68: the eye put 80 back, its distance, through the trees behind him, and pulled in
            // to 51 the next draw, a 29 jump): the eye set no farther than the camera's line from him is clear (the floor's camera
            // collision, CameraLineClear, as the game's own pull-in sees it), so nothing pulls it on the next draw; the game's
            // CameraAutoMove eases it on from there. DC_COOP_MUTATE=forest_camera_through (twin): its whole distance, as e68.
            if (!CoopMutate("forest_camera_through")) {
                const float follow[4] = {g_swap_stand_in.place[0], g_swap_stand_in.place[1] + 13.0f, g_swap_stand_in.place[2], 1.0f};
                const float up = MainCamera__4.GetHeight();
                const float back = ForestCameraBack(follow, angle, up, MainCamera__4.GetDistance());

                std::fprintf(stderr, "coop: opening: map 1 the follow camera set %.0f back (of %.0f), %.0f up, its line to him clear\n", back,
                             MainCamera__4.GetDistance(), up);
                MainCamera__4.SetDistance(back);
            }
            MainCamera__4.Step(-1);
        }
        g_swap_stand_in.on = false;
        forest_lead_on = false;
        std::fprintf(stderr, "coop: opening: map 1 the lead walked on to (%.0f %.0f %.0f) as the opening ended\n", g_swap_stand_in.place[0],
                     g_swap_stand_in.place[1], g_swap_stand_in.place[2]);
    }
    if (!opening && was && selectMapNo == 1 && own) { // (round 6: the opening lasts till the last walker's walk-in has played out)
        std::fprintf(stderr, "coop: opening: map 1 ended at tick %d with the partner's walk-in %s\n", own_tick, forest_partner_done ? "played out" : "still playing");
    }
    if (!opening && was && near_tick >= 0) {
        std::fprintf(stderr, "coop: opening: together on map %d: the bodies came %.1f apart at their nearest (tick %d), first closer than %.0f at %d\n",
                     selectMapNo, near_min, near_tick, kBodyApart, overlap_tick);
    }

    // DC_COOP_DEBUG: the opening tick by tick, where the player is drawn (its frame's world place,
    // which a scene's reference moves) beside where the game keeps it, and where the partner stands.
    static int opening_tick = 0;

    opening_tick = opening && !was ? 0 : opening_tick + 1;

    if (CoopDebug() && (opening_tick < 8 || opening_tick % 10 == 0 || !opening)) {
        std::printf("coop-debug: opening %d%s: player pos (%.1f %.1f %.1f) drawn (%.1f %.1f %.1f) yaw %.2f drawn yaw %.2f ref %d motion %d; "
                    "partner (%.1f %.1f %.1f) %s %d motion %d/%d\n",
                    opening_tick, opening ? "" : " (ended)", CharaMain.pos[0], CharaMain.pos[1], CharaMain.pos[2], player[0], player[1], player[2],
                    rotation[1], atan2f(axis_z[0], axis_z[1]), CharaMain.frame->reference, CharaMain.motion_no, g_partner.pos[0],
                    g_partner.pos[1], g_partner.pos[2], follow != nullptr ? "trail" : "side", follow != nullptr ? trail_n : held,
                    g_partner.motion, g_partner.chara->motion_no);
    }
}

void CameraResetForSession() {
    g_camera.ready = false;
    g_camera.intro = false;
    g_camera.idle = 0;

    for (float &axis : g_camera.last) {
        axis = 0.0f;
    }

    g_camera.last[3] = 1.0f;
    g_view_active = false;
    g_view_swapped = false;
    g_view_shown = nullptr;
    g_opening = false;
    g_partner.camera_widened = false;
    g_partner.camera_distance = 0.0f;
    g_partner.camera_shift = 0.0f;
    g_partner.midpoint_clear = false;
    g_partner.midpoint_wait = 0;
}

CCameraFollow *CoopCameraObject() {
    if (g_camera.camera == nullptr) {
        g_camera.camera = new CCameraFollow(20.0f, 15.0f, 0.0f, 8.0f);
    }
    return g_camera.camera;
}

namespace {
CCameraFollow *g_own_camera = nullptr; // this copy's object, across an apply
}

void CoopCameraSnapshotBefore() {
    g_own_camera = CoopCameraObject();
}

void CoopCameraSnapshotAfter() {
    CCameraFollow *host = g_camera.camera; // the host's pointer, as its state had it

    g_camera.camera = g_own_camera;
    if (g_view_shown == host && host != nullptr) {
        g_view_shown = g_own_camera;
    }
    if (g_view_moved == host && host != nullptr) {
        g_view_moved = g_own_camera;
    }
}

std::int32_t SteerQuantize(float yaw) {
    return (std::int32_t) std::lround(std::remainder(yaw, 2.0f * PI) * (65536.0f / (2.0f * PI)));
}

float SteerOwnAngle(int port) {
    if (mode == GAME_MODE_DUNGEON && NowCamera__3 != nullptr) {
        return port == 0 ? NowCamera__3->GetAngleH() : PartnerCameraAngleH();
    }
    if (port != 0) {
        // coop-integrate1118: in her own Georama the view she sees is her editor's (its cursor reads her row: MoveEditCursor)
        if (float georama = 0.0f; CoopP2GeoramaCameraAngle(&georama)) {
            return georama;
        }
        return CoopTownPartnerCameraAngle();
    }
    if (GameMode == ED_MODE_GEORAMA) { // his Georama: the cursor moves by the Georama view's angle (MoveEditCursor), retail's GetAngle
        return EditCamera.GetAngle();
    }
    return EdMoveCharaInfo.camera != nullptr ? EdMoveCharaInfo.camera->GetAngleH() : MainCamera.GetAngleH();
}

bool CollisionCellNear(int x, int z, const float *pos, float camera_dist, bool wide) {
    if (CoopMutate("collision_cells_camera")) {
        return camera_dist <= 240.0f;
    }
    const float reach = wide ? 400.0f : 240.0f;

    return std::fabs(160.0f * (float) x - pos[0]) <= reach && std::fabs(160.0f * (float) z - pos[2]) <= reach;
}

// coop-wave3 test hook (DC_COOP_TEST_CAMERA_TURN=1): the tick a camera of this copy's own player first turns by the right stick.
void CameraTurnTrace(int port, float rx) {
    static const bool on = TestEnv("DC_COOP_TEST_CAMERA_TURN") != nullptr;
    static bool       said = false;
    const int         player = port == 0 ? NetplayGamePadPlayer() : 3 - NetplayGamePadPlayer();

    if (on && !said && rx != 0.0f && player == NetplayLocalPlayer()) {
        said = true;
        std::fprintf(stderr, "coop-wave3: test: player %d's camera turns by the right stick at link tick %lld\n", player, (long long) NetplayLinkTick());
    }
}

float CameraRX(int port, float game) {
    InputPadState pad;
    const float   rx = NetplayCameraPad(port, pad) ? Stick(pad.right_x) : game;

    CameraTurnTrace(port, rx);
    return rx;
}

float CameraRY(int port, float game) {
    InputPadState pad;

    return NetplayCameraPad(port, pad) ? Stick(pad.right_y) : game;
}

int CameraOn(int port, int game, CameraButton button) {
    InputPadState pad;

    if (!NetplayCameraPad(port, pad)) {
        return game;
    }
    const std::uint16_t bit = button == kCameraR1 ? kInputR1 : button == kCameraL1 ? kInputL1 : kInputL2;

    return (pad.buttons & bit) != 0 ? 1 : 0;
}

float SteerAngle(int port, float camera) {
    const std::int32_t steer = NetplaySteer(port);

    return steer == kInputNoSteer ? camera : (float) steer * (2.0f * PI / 65536.0f);
}

// coop-wave3 test hook (DC_COOP_TEST_CAMERA_SCRAMBLE=1, on player 2's copy alone): every 90 ticks every camera of this copy (his
// dungeon and script cameras, her own, the town's and hers there) turns by half a radian on this copy alone, as a local camera may.
// The cameras are each player's own (out of the checksum), so a step that still reads one parts the copies' world: the tripwire for
// a step read of a camera (the test's red, DC_NETPLAY_MUTATE=steer_off: the stick read against the copy's own camera).
void CameraScrambleTick(long long tick) {
    static const char *value = TestEnv("DC_COOP_TEST_CAMERA_SCRAMBLE");
    static const bool  on = value != nullptr;
    static const bool  far = on && std::strcmp(value, "far") == 0;
    static int         turned = 0;

    // DC_COOP_TEST_CAMERA_SCRAMBLE=far: instead, every tick his camera on her copy stands 500 back from him (the cells' distances
    // her copy puts back after its draw are from it): a step that picks his collision's cells near the camera loses his own cell
    // there (the test's red: DC_COOP_MUTATE=collision_cells_camera).
    if (far && NetplayLocalPlayer() == 2 && mode == GAME_MODE_DUNGEON) {
        MainCamera__4.distance = 500.0f;
        if (turned++ == 0) {
            std::fprintf(stderr, "coop-wave3: test: his camera stands 500 back on this copy alone from link tick %lld\n", tick);
        }
        return;
    }
    if (!on || far || NetplayLocalPlayer() != 2 || tick % 90 != 45) {
        return;
    }
    CCameraFollow *cameras[] = {&MainCamera__4, &SubCamera, g_camera.camera, &MainCamera, CoopTownPlayerFollowCamera(1), CoopRoomCamera()};

    for (CCameraFollow *camera : cameras) {
        if (camera != nullptr) {
            camera->angle += 0.5f;
            camera->next_angle += 0.5f;
        }
    }
    turned++;
    std::fprintf(stderr, "coop-wave3: test: this copy's cameras turned alone at link tick %lld (%d times)\n", tick, turned);
}

float PartnerCameraAngleH() {
    if (!IndependentCameras() || !g_camera.ready || CoopMutate("shared_cameras")) {
        return NowCamera__3->GetAngleH();
    }

    return g_camera.camera->GetAngleH();
}

void PartnerCameraTick() {
    g_camera.idle++;
}

// coop-entrance10b (the candidate's landing set, 11-13: v-hud-face "copy B: her body turns orange on screen", x-p2-circle-cam
// 0.55 rad): her camera starts from the player's (CopyCamera) as an opening ends or a load puts her beside him, and keeps his
// angle; since entrance10 his is set behind his walk-on, toward the niche's boulders, at the length clear from HIM (46): from
// her, 14 to 30 aside, the same line runs into the boulders (fixture dun1-f1: angle 2.62, 49 back, her body hidden). Now her
// camera keeps his angle only when its line from her own follow point is clear; else it goes behind her, then round from
// there by 45 degrees each side, to the first angle whose line is clear 30 or more (the longest if none), at that clear
// length up to the game's own 60 (MainCamera__4's at the floor's load), his height kept. Both copies alike (game state).
// DC_COOP_MUTATE=p2_camera_copied (twin): his angle and length as copied.
void PartnerCameraClearStart(CCameraFollow *camera) {
    if (CoopMutate("p2_camera_copied") || g_partner.chara == nullptr || g_partner.chara->frame == nullptr || NowDngMap == nullptr) {
        return;
    }
    sceVu0FVECTOR rotation;

    g_partner.chara->frame->GetRotation(rotation);
    const float follow[4] = {g_partner.pos[0], g_partner.pos[1] + 13.5f, g_partner.pos[2], 1.0f};
    const float copied = camera->angle;
    const float behind = rotation[1] + PI;
    const float tries[] = {copied, behind, behind + PI / 4.0f, behind - PI / 4.0f, behind + PI / 2.0f, behind - PI / 2.0f,
                           behind + 3.0f * PI / 4.0f, behind - 3.0f * PI / 4.0f, behind + PI};
    float       best = copied;
    float       best_back = -1.0f;

    for (const float a : tries) {
        const float back = ForestCameraBack(follow, a, camera->height, 60.0f);

        if (back > best_back) {
            best = a;
            best_back = back;
        }
        if (back >= 30.0f) {
            break;
        }
    }
    best = std::remainder(best, 2.0f * PI);
    std::fprintf(stderr, "coop: p2 camera from the player's: his angle %.2f clear %.0f from her; set %.2f (%.0f from behind her), %.0f back, %.1f up\n",
                 copied, ForestCameraBack(follow, copied, camera->height, 60.0f), best,
                 std::fabs(std::remainder(best - behind, 2.0f * PI)) * 180.0f / PI, best_back, camera->height);
    camera->angle = best;
    camera->next_angle = best;
    camera->distance = best_back;
    camera->SetFollow(follow[0], follow[1], follow[2]);
}

void PartnerCameraClearAtCut() {
    g_camera.clear_at_cut = true;
}

void PartnerCameraFromPlayer() {
    if (g_camera.camera == nullptr) {
        g_camera.camera = new CCameraFollow(20.0f, 15.0f, 0.0f, 8.0f);
    }
    CopyCamera(g_camera.camera, &MainCamera__4);
    PartnerCameraClearStart(g_camera.camera);
    g_camera.ready = true;
    g_camera.intro = FloorIntro();
    g_camera.idle = 0;
    sceVu0CopyVector(g_camera.last, g_partner.pos);
}

// coop-floorexit: her member, the partner until now, becomes the game's player where she stands (she stays on a floor he left):
// the player's camera goes on from hers as it is (no cut on her screen), and the cells' distances from it, which the player's
// collision reads on a cell-built floor, are hers from this tick. Left at his gate, the camera was too far from her for her
// floor's cells: she fell through (Divine Beast Cave, its fall respawn over and over).
void PlayerCameraFromPartner() {
    // (Apart, the link is down and IndependentCameras off: her camera as the link left it.)
    if (g_camera.ready && g_camera.camera != nullptr) {
        CopyCamera(&MainCamera__4, g_camera.camera);
    }

    if (NowDngMap != nullptr && NowDngMap->map_type == 1) {
        CellDistances(&MainCamera__4);
    }
}

void PartnerCameraBehind() {
    if (!IndependentCameras() || !g_camera.ready) {
        return;
    }

    sceVu0FVECTOR rotation;

    g_partner.chara->frame->GetRotation(rotation);
    g_camera.camera->SetAngle(rotation[1] - PI);
}

void StepPartnerCamera(bool playing) {
    if (!IndependentCameras()) {
        g_camera.ready = false;
        return;
    }

    if (g_camera.camera == nullptr) {
        g_camera.camera = new CCameraFollow(20.0f, 15.0f, 0.0f, 8.0f);
        std::printf("coop: independent cameras on (player %d)\n", NetplayLocalPlayer());
    }

    CCameraFollow *camera = g_camera.camera;
    bool           cut = false;

    // The opening leaves the player's camera behind the player (BtSystemScriptAfter); the partner's
    // starts again from it.
    if (g_camera.intro && !FloorIntro()) {
        g_camera.ready = false;
    }

    g_camera.intro = FloorIntro();

    if (!g_camera.ready) {
        CopyCamera(camera, &MainCamera__4);
        PartnerCameraClearStart(camera);
        g_camera.ready = true;
        cut = true;
    }

    // Back from an event or a floor change, or put back beside the player: the camera cuts to the
    // partner, keeping its angle and height.
    float away[4] = {g_partner.pos[0] - camera->follow[0], 0.0f, g_partner.pos[2] - camera->follow[2], 0.0f};

    if (g_camera.idle > 1 || DistVector(away) > kCameraCut) {
        cut = true;
    }

    g_camera.idle = 0;

    // DunMoveChara's ref_off: how far the partner moved this tick, which the camera looks ahead by.
    sceVu0FVECTOR moved = {0.0f, 0.0f, 0.0f, 0.0f};

    if (!cut) {
        sceVu0SubVector(moved, g_partner.pos, g_camera.last);
    }

    sceVu0CopyVector(g_camera.last, g_partner.pos);
    camera->SetSpeed(8.0f);

    float lx = 0.0f;

    // The partner's own lock-on (coop_combat), which her camera swings round to as the player's does
    // round his. The mutation twin keeps the camera as it was, as if she were not locked on.
    int lock = g_combat.lock >= 0 && NowMonstorUnit != nullptr && !CoopMutate("p2_lock_camera") ? g_combat.lock : -1;

    if (playing) {
        // coop-wave3 part 2: her own controller as read now on her copy (the right stick, R1, L1, L2), the agreed pad elsewhere
        InputPadState        linked;
        const InputPadState &pad = NetplayCameraPad(1, linked) || NetplayPad(1, linked) ? linked : InputGetPad(1);
        float                rx = Stick(pad.right_x);

        CameraTurnTrace(1, rx); // coop-wave3 test hook
        float                ry = Stick(pad.right_y);

        // autoCamTrial turns the camera with a partner walking sideways.
        lx = Stick(pad.left_x);

        // The retail sums, without the mouse's share the port's AddHeight and AddAngle take for the
        // game's own cameras.
        camera->height -= ry;
        camera->next_angle += 0.04f * -rx;

        // R1 and L1 turn the camera only while not locked on, as DunMoveChara has them.
        if (lock < 0) {
            if ((pad.buttons & kInputR1) != 0) {
                camera->next_angle -= 0.034906585f;
            }

            if ((pad.buttons & kInputL1) != 0) {
                camera->next_angle += 0.034906585f;
            }
        }

        // L2 brings the camera round behind the partner.
        if ((pad.buttons & kInputL2) != 0) {
            sceVu0FVECTOR rotation;

            g_partner.chara->frame->GetRotation(rotation);
            camera->next_angle = rotation[1] - PI;
        }
    }

    // DunMoveChara's cap on the stick's height.
    camera->height = std::min(camera->height, kCameraHeightMax);

    // DunMoveChara's follow point: ahead of the partner by its step, at the player's look height.
    camera->SetFollow(g_partner.pos[0] + 7.0f * moved[0], g_partner.pos[1] + 6.0f + 2.0f * moved[1] + 7.5f,
                      g_partner.pos[2] + 7.0f * moved[2]);

    // Locked on, DunMoveChara's follow point leans toward the monster, by up to 20 units, level.
    if (lock >= 0) {
        sceVu0FVECTOR target;
        sceVu0FVECTOR to_target;

        NowMonstorUnit->chara[lock][0].GetPosition(target);

        float dist = std::min(DistVector(target, g_partner.pos), 20.0f);

        to_target[0] = target[0] - g_partner.pos[0];
        to_target[1] = target[1] - g_partner.pos[1];
        to_target[2] = target[2] - g_partner.pos[2];
        to_target[3] = 1.0f;
        sceVu0Normalize(to_target, to_target);
        sceVu0ScaleVectorXYZ(to_target, to_target, dist);
        camera->SetFollow(g_partner.pos[0] + to_target[0], g_partner.pos[1] + 6.0f + 7.5f, g_partner.pos[2] + to_target[2]);
    }

    if (cut && g_camera.clear_at_cut) {
        g_camera.clear_at_cut = false;
        // (the clear start's search, from the angle the cut keeps; DC_COOP_MUTATE=put_camera_kept (twin): the angle as kept)
        const float follow[4] = {g_partner.pos[0], g_partner.pos[1] + 13.5f, g_partner.pos[2], 1.0f};
        std::fprintf(stderr, "coop: p2 camera at a test's put: angle %.2f clear %.0f from her%s\n", camera->angle,
                     ForestCameraBack(follow, camera->angle, camera->height, 60.0f), CoopMutate("put_camera_kept") ? ", kept (put_camera_kept)" : "");
        if (!CoopMutate("put_camera_kept")) {
            PartnerCameraClearStart(camera);
        }
    }
    if (cut) {
        camera->Step(-1);
    }

    AutoCamera(camera, lx, moved, lock);
    camera->Step(1);

    static int report = 0;
    // (coop-warmreset, WS7: put back at each warm-pair job as a fresh copy has it; trace_once_kept twin: kept)
    COOP_TEST_TRACE_RESET(report, 0);

    if (CoopDebug() && (cut || --report <= 0)) {
        std::printf("coop-debug: p2 camera%s angle %.2f distance %.1f height %.1f eye (%.0f %.0f %.0f) lock %d\n", cut ? " cut" : "",
                    camera->angle, camera->distance, camera->height, camera->pos[0], camera->pos[1], camera->pos[2], lock);
        report = 60;
    }
}

} // namespace coop

// coop-arrivalfull (Jep, 2026-10-27: "let the catch up finish in the background and then just not skip the animation"): a
// floor join's catch-up ends by putting the other copy's state over the arriver's, which has no arrival running for him, so
// the arrival was cut (or its last frame stood still). Now each copy keeps the arrival it last played on a floor (the
// floor's system script 100: where its player was drawn and what it played, tick by tick, and where play started), in the
// state set (this file's globals are), so the arriver takes the record of that very floor from the one there first with
// the merge. Once both copies are in step (EnterLockstep), both play it for the arriver from its start: player 1 as the
// game's player, player 2 as the partner; the arriver's pad is neutral to its end, the other plays on (ArrivalWalkPlayer).
// The floor's own script is not run again (it loads the floor, and its event would hold both players).
// DC_NETPLAY_MUTATE=arrival_skipped (twin): no walk, as before.
// coop-arrivalcam (Jep's 2026-10-27 playtest: after B1's lesson Toan walked down the stairs "in the plain in-game walk, clipping
// through the dungeon wall, not the cinematic version"): the arrival is a scene of script 100 (B1: dun/d01/stair_mos/in01, the
// player on its node chara01a with the walk motion, a camera standing in the stairwell at (0 15 60) of the stairs' frame that
// keeps looking at him, then RESET_CAMERA_ANGLE puts the follow camera behind him as the script ends). The record now keeps
// that camera too (the event's NowCamera__3, SubCamera, each tick), and the arriver's copy draws its walk from it, not from
// its follow camera behind him in the stairwell's walls; at the walk's end the arriver's follow camera is reset as the script's
// end resets it (BtSystemScriptAfter). The other copy keeps its own camera. DC_NETPLAY_MUTATE=arrival_follow_cam (twin): the
// follow camera through the walk, as before; arrival_path_side (twin): the walk 12 units off the record's way.
namespace {

struct ArrivalSample {
    float place[3];
    float yaw;
    int   motion;
    float eye[3]; // coop-arrivalcam: the event's camera, where it stood and looked
    float ref[3];
    float door; // coop-joinatla: the stairwell door's height (in_door), as the script slides it open
    int   moon; // coop-entrance2: Moon Sea's beam clock at this tick (MoonEntranceClock), -1 none
    float halves; // cand1112c: the walk-in's door as its script has it (EntranceDoors: the temple's two halves), for a window walker's hold
};

constexpr int kArrivalSamples = 900; // the arrival is some 350 ticks (Divine Beast Cave B1); a longer one is cut here

struct ArrivalTrail {
    int           key = -1000;       // the floor (selectMapNo * 256 + floor) of the arrival recorded, -1000 none
    int           recording = -1000; // the floor of the arrival under way, -1000 none
    int           half = 0;          // coop-backmerge: the floor's half it was made in (BtUraDongeon)
    int           count = 0;
    float         end[3] = {};
    float         end_yaw = 0.0f;
    int           reset_angle = 0;  // coop-arrivalcam: the script's camera reset at its end (EdEventInfo.reset_camera_angle)
    float         reset_yaw = 0.0f; // and its yaw from the player's (reset_camera_yaw)
    ArrivalSample samples[kArrivalSamples];
};

ArrivalTrail g_arrival_trail;

struct ArrivalWalk {
    int   player = 0; // the arriver (1 or 2) while its arrival plays, 0 none
    int   tick = 0;
    bool  entrance = false; // coop-entrwin: a walker's own entrance in a floor's entrance window (EntranceWalkStart)
    int   walker = 2;       // and its walker number (EntranceSpotFor)
    int   shift = 0;        // coop-entrance2 rule 3: its entrance's start after the walk's (+ held at its start, - begun that far in)
    int   first_at = -1;    // round 6: the record's tick it began at, and whether it has stepped on from there (not drawn till then)
    bool  stepped = false;
    bool  door = false; // coop-joinatla: the stairwell door replayed, and its height before the walk (put back at its end)
    float door_before = 0.0f;
    float start[3] = {0.0f, 0.0f, 0.0f}; // candfix2b: the walker's start (its spot at the record's first sample), and whether the
    bool  drawn_said = false;            // partner has been drawn there yet (EntranceWalkDrawnTrace)
    bool  cut = false;                   // coop-b4arrive: an arrival the merge cut on the arriver's copy (ArrivalWalkStart)
};

// coop-joinatla (Jep, 2026-10-28: P1 skipped B1's scene, P2 "joined late ... the door was closed during their animation ... so they
// walked through the wall"): an arrival's script stands the stairwell's door ('in_door') open (height 0) as the player comes down
// and slides it shut (-40) behind him; the replay moved the player alone, so the door stood shut in the way. The floor's door,
// nullptr if it has none.
CFrame *StairDoor() {
    static char name[] = "in_door";

    return mode == GAME_MODE_DUNGEON && NowDngMap != nullptr ? NowDngMap->GetFrameSearch(name) : nullptr;
}

ArrivalWalk g_arrival_walk;

// coop-latefall: one bit for each of the event script's chr packs (_LOAD_CHR_FILE's 16) that is not the stairwell's (ArrivalChrFile).
unsigned g_scene_pack_bits = 0;

int FloorKey() {
    return UserStatus != nullptr ? selectMapNo * 256 + UserStatus->cur_floor : -1000;
}

// The motion a sample played, where this character has it; else walking (1) while the way moves on, or standing (0).
int WalkMotion(CCharacter *chara, int motion, bool moving) {
    int index = -1;

    if (motion > 0 && chara != nullptr && chara->GetMotionParam(motion, &index, nullptr, nullptr, nullptr) != nullptr && index >= 0) {
        return motion;
    }
    return moving ? 1 : 0;
}

// coop-entrance2 round 6 (x-entrwin-near on the forest, 1.1 apart at walk tick 321): a walker held at its start (rule 3's shift, not
// drawn) had its stairs motion played on meanwhile, so it came out with its body already at the niche's one end, on the lead. Held,
// its motion stays on its first frame (as the together lead's hold). DC_COOP_MUTATE=entrance_walk_motion_runs (twin): it runs, as before.
void WalkMotionHeld(CCharacter *chara, int motion) {
    int             index = -1;
    tagMOTION_TYPE *m = chara != nullptr ? chara->GetMotionParam(motion, &index, nullptr, nullptr, nullptr) : nullptr;

    if (!g_arrival_walk.entrance || m == nullptr || m->motion_info == nullptr || index < 0 || CoopMutate("entrance_walk_motion_runs")) {
        return;
    }
    // (and on the walk's own clock, not the body's: after the scene's handover player 1's walker is the game's player, whose stairs
    // motion ran on from its own walk-in: x-entrwin-p2first 0.7 apart; the stairs motion only, a walk cycle runs on its own)
    if (g_arrival_walk.tick <= EntranceWalkDelay()) {
        m->state.time = static_cast<float>(m->motion_info[index].start);
    } else if (motion == 100) {
        const float speed = m->motion_info[index].speed > 0.0f ? m->motion_info[index].speed : 1.0f;

        m->state.time = std::min(static_cast<float>(m->motion_info[index].start) + (g_arrival_walk.tick - EntranceWalkDelay()) * speed,
                                 static_cast<float>(m->motion_info[index].end) - 1.0f);
    }
}

bool SampleMoves(int tick) {
    const ArrivalTrail &t = g_arrival_trail;

    if (tick + 1 >= t.count) {
        return false;
    }
    const ArrivalSample &a = t.samples[tick];
    const ArrivalSample &b = t.samples[tick + 1];

    return std::hypot(b.place[0] - a.place[0], b.place[2] - a.place[2]) > 0.05f;
}

// coop-entrwin: a walker's spot in the lead's frame at (place, yaw): its entrance's own (EntranceSpotFor), else kEntranceSide to his
// right (an entrance with no entry: Divine Beast Cave's stairs, where StandBesideArrival's trail walks her 7 to his right too).
constexpr float kEntranceSide = 7.0f;

void EntranceSpotPlace(const float *at, float yaw, int walker, float *place, float *turn, bool end = true) {
    const EntranceSpot *spot = EntranceSpotFor(selectMapNo, walker);

    // coop-entrance2: Moon Sea's pad: the walker's character's own spot round it (rule 2), its whole entrance.
    float moon_yaw = 0.0f;

    if (MoonEntrance() && MoonSpot(EntranceWalkChara(), place, &moon_yaw)) {
        *turn = moon_yaw - yaw;
        return;
    }
    // coop-entrance2: an entrance of the lead's own (EntranceSpot::own): the walk at his very place (its own arrival motion), the
    // table's spot only where it ends.
    if (!end && spot != nullptr && spot->own && !CoopMutate("entrance_delay_off")) {
        place[0] = at[0] - spot->way_right * cosf(yaw);
        place[1] = at[1];
        place[2] = at[2] + spot->way_right * sinf(yaw);
        place[3] = 1.0f;
        *turn = 0.0f;
        return;
    }
    const float         right = spot != nullptr ? spot->right : kEntranceSide * (walker == 3 ? -1.0f : 1.0f);
    const float         along = spot != nullptr ? spot->along : 0.0f;
    // (PlayerDrawnPlace's axes from a yaw: x (cos, -sin), z (sin, cos))
    const float axis_x[2] = {cosf(yaw), -sinf(yaw)};
    const float axis_z[2] = {sinf(yaw), cosf(yaw)};

    place[0] = at[0] - right * axis_x[0] + along * axis_z[0];
    place[1] = at[1];
    place[2] = at[2] - right * axis_x[1] + along * axis_z[1];
    place[3] = 1.0f;
    *turn = spot != nullptr ? spot->turn : 0.0f;
}

// Where the walk puts the arriver at a sample (the twin arrival_path_side: 12 units off the way, to the record's right).
// coop-entrwin: a walker's own entrance at its spot beside the lead's way.
void SamplePlace(const ArrivalSample &s, float *place) {
    place[0] = s.place[0];
    place[1] = s.place[1];
    place[2] = s.place[2];
    place[3] = 1.0f;

    if (g_arrival_walk.entrance) {
        float turn = 0.0f;

        EntranceSpotPlace(s.place, s.yaw, g_arrival_walk.walker, place, &turn, false);
        return;
    }
    if (NetplayMutated("arrival_path_side")) {
        place[0] += 12.0f * cosf(s.yaw);
        place[2] -= 12.0f * sinf(s.yaw);
    }
}

// coop-entrwin: the way the arriver faces at a sample (its spot's turn on a walker's entrance).
float SampleYaw(const ArrivalSample &s) {
    if (g_arrival_walk.entrance) {
        float place[4];
        float turn = 0.0f;

        EntranceSpotPlace(s.place, s.yaw, g_arrival_walk.walker, place, &turn, false);
        return s.yaw + turn;
    }
    return s.yaw;
}

// The record's tick a walk stands at (coop-entrance2 rule 1's twin, entrance_delay_apart: a separate arrival held its character's delay
// at the record's first tick).
int WalkAt() {
    const int delay = g_arrival_walk.entrance ? EntranceWalkDelay() : 0;

    return std::clamp(g_arrival_walk.tick - delay, 0, std::max(g_arrival_trail.count - 1, 0));
}

// The script's end puts the arriver's follow camera where the arrival leaves it (BtSystemScriptAfter's two resets: behind the
// player at its yaw plus reset_yaw, or where the event's camera stood, following on from there).
void ResetArrivalCamera(CCameraFollow *camera, const float *place, float yaw) {
    const ArrivalTrail &t = g_arrival_trail;
    sceVu0FVECTOR       pos;
    sceVu0FVECTOR       ref;

    if (camera == nullptr || t.count <= 0 || NetplayMutated("arrival_follow_cam")) {
        return;
    }

    if (t.reset_angle > 0) {
        float angle = yaw + t.reset_yaw;

        if (!(angle <= PI_SHORT)) {
            angle -= TWO_PI;
        }

        camera->SetAngleSoon(angle);
        camera->SetFollow(place[0], place[1] + 16.0f - 3.0f, place[2]);
        camera->Step(-1);
    } else if (t.reset_angle < 0) {
        const ArrivalSample &s = t.samples[t.count - 1];

        pos[0] = s.eye[0], pos[1] = s.eye[1], pos[2] = s.eye[2], pos[3] = 1.0f;
        ref[0] = s.ref[0], ref[1] = s.ref[1], ref[2] = s.ref[2], ref[3] = 1.0f;
        camera->FollowOff();
        camera->SetPos(pos);
        camera->SetRef(ref);
        camera->Step(-1);
        camera->Step(1);
        camera->FollowOn();
        camera->SetHeight(pos[1] - ref[1]);
        camera->SetFollow(ref[0], ref[1], ref[2]);
        camera->SetAngleSoon(atan2(pos[0] - ref[0], pos[2] - ref[2]));
        camera->Step(-1);
        camera->Step(1);
    }
}

} // namespace

namespace coop {

// This copy's own memory (outside the state set: a merge's state must not take it away) of an entrance window's join on this floor.
int g_entrance_local_window = -1000;

const float *ArrivalRecordEnd() {
    return g_arrival_trail.end;
}

float ArrivalRecordEndYaw() {
    return g_arrival_trail.end_yaw;
}

bool EntranceWindowSeen() {
    return g_entrance_local_window == FloorKey();
}

bool EntranceSwapStandInWaiting() {
    return g_swap_stand_in.on && g_swap_stand_in.waiting && FloorIntro();
}

// coop-entrancereds: the together walk-in draws both players: past the script's cut from the map's corner (the bodies' span;
// before it the cave's in01 holds both at (0 0 0) for a tick, the camera still where the last floor left it) or on Moon Sea's
// pad, and neither waiting for its start or its beam. The far gate's walk-in count reads it (OtherBeyondClip).
bool EntranceWalkInShowsBoth() {
    return FloorIntro() && (g_walked_in || MoonEntrance()) && !EntranceLeadHidden() && !EntrancePartnerHidden() && !EntranceSwapStandInWaiting();
}

bool EntranceSwapStandIn(int *chara, float *place, float *yaw, int *motion, int *flags) {
    const SwapStandIn &s = g_swap_stand_in;

    if (!s.on || !FloorIntro()) {
        return false;
    }
    *chara = s.chara;
    for (int i = 0; i < 4; i++) {
        place[i] = s.place[i];
    }
    *yaw = s.yaw;
    *motion = s.motion;
    *flags = s.flags;
    return true;
}

bool EntranceTogether() {
    return g_together == FloorKey();
}

void ArrivalTrailTick() {
    if (CoopWorldsWalkJoinLatched() || EntranceWalkOn()) {
        g_entrance_local_window = FloorKey();
    }
    MoonEntranceTick();
    MoonShownTick();
    ArrivalTrail &t = g_arrival_trail;
    const bool    intro = BtEventMode != 0 && BtEventInfo.running_script_no == 100 && mode == GAME_MODE_DUNGEON && UserStatus != nullptr &&
                       CharaFrame != nullptr && CharaMain.frame != nullptr && (g_arrival_walk.player == 0 || g_arrival_walk.entrance);
    // (coop-entrwin: a walker's own entrance plays the lead's walk-in as it is recorded, behind it: the record goes on.)

    if (intro) {
        // (a new floor under the same script, as from the entrance's menu: the record starts again there)
        if (t.recording != FloorKey()) {
            t.recording = FloorKey();
            t.count = 0;
            t.key = -1000;
            t.half = BtUraDongeon;
            g_together = -1000; // (a new opening: StandBesideArrival says whether it is together)
        }
        sceVu0FVECTOR place;
        float         axis_x[2], axis_z[2];

        PlayerDrawnPlace(place, axis_x, axis_z);
        // (the script's first ticks stand the player at the map's corner, (0 0 0) or (200 0 150), before it puts him on the
        // stairs or a scene's node takes him: not his way. A cut that early starts the record again from it.)
        const bool origin = place[0] == 0.0f && place[1] == 0.0f && place[2] == 0.0f;

        if (!origin && t.count > 0 && t.count < 30 &&
            std::hypot(place[0] - t.samples[t.count - 1].place[0], place[2] - t.samples[t.count - 1].place[2]) > 100.0f) {
            t.count = 0;
        }
        if (t.count < kArrivalSamples && !origin) {
            ArrivalSample &s = t.samples[t.count++];
            sceVu0FVECTOR  eye = {};
            sceVu0FVECTOR  ref = {};

            if (NowCamera__3 != nullptr) {
                NowCamera__3->GetPos(eye);
                NowCamera__3->GetRef(ref);
            }

            s.place[0] = place[0];
            s.place[1] = place[1];
            s.place[2] = place[2];
            s.yaw = atan2f(axis_z[0], axis_z[1]);
            s.motion = CharaMain.motion_no;
            s.moon = MoonEntranceClock();
            if (const CFrame *door = StairDoor()) {
                s.door = door->position[1];
            } else {
                s.door = 0.0f;
            }
            {
                // (cand1112c: the script's own door, not a window walker's hold: a value other than the hold's last write is the script's)
                CFrame *pieces[2] = {nullptr, nullptr};
                int     axis = 1;

                if (EntranceDoors(pieces, &axis) > 0 && pieces[0]->position[axis] != g_door_written) {
                    g_door_script = pieces[0]->position[axis];
                }
                s.halves = g_door_script;
            }

            for (int i = 0; i < 3; i++) {
                s.eye[i] = eye[i];
                s.ref[i] = ref[i];
            }

            // Test hook (DC_COOP_TEST_ARRIVAL_VIEW=1, x-floor-join -Seamless): the event's own camera and way, every 10th sample.
            if (TestEnv("DC_COOP_TEST_ARRIVAL_VIEW") != nullptr && (t.count - 1) % 10 == 0) {
                std::fprintf(stderr, "coop: arrival record %d place (%.1f %.1f %.1f) eye (%.1f %.1f %.1f) ref (%.1f %.1f %.1f)\n", t.count - 1, s.place[0],
                             s.place[1], s.place[2], s.eye[0], s.eye[1], s.eye[2], s.ref[0], s.ref[1], s.ref[2]);
            }
        }
        t.reset_angle = EdEventInfo.reset_camera_angle;
        t.reset_yaw = EdEventInfo.reset_camera_yaw;
        return;
    }
    if (t.recording != -1000 && BtEventMode == 0 && CharaFrame != nullptr) {
        sceVu0FVECTOR rotation;

        CharaFrame->GetRotation(rotation);
        t.key = t.count >= 30 && t.recording == FloorKey() ? t.recording : -1000;
        t.recording = -1000;
        t.end[0] = CharaMain.pos[0];
        t.end[1] = CharaMain.pos[1];
        t.end[2] = CharaMain.pos[2];
        t.end_yaw = rotation[1];
        std::fprintf(stderr, "coop: arrival recorded on floor %d: %d ticks, from (%.0f %.0f %.0f) to (%.0f %.0f %.0f)\n", (t.key & 255) + 1, t.count,
                     t.samples[0].place[0], t.samples[0].place[1], t.samples[0].place[2], t.end[0], t.end[1], t.end[2]);
    }
}

static void ArrivalStand(int player, float *end); // (below)

void ArrivalWalkStart(int player, bool cut, int from) {
    const ArrivalTrail &t = g_arrival_trail;

    if (NetplayMutated("arrival_skipped")) {
        std::fprintf(stderr, "coop: arrival walk: none for player %d (arrival_skipped)\n", player);
        return;
    }
    if (mode != GAME_MODE_DUNGEON || t.key != FloorKey() || t.count <= 0 || (player != 1 && player != 2)) {
        std::fprintf(stderr, "coop: arrival walk: none for player %d (no arrival of this floor recorded: here %d, recorded %d)\n", player, FloorKey(), t.key);
        return;
    }
    // coop-backmerge (x-back-half -Away: player 1 came back to the floor, went in by the back-half door, and merged there): the
    // record is the floor's front arrival (its opening, script 100); in the back half it put him on the front's entrance spot,
    // over the back half's water, and the cave's fall took him to the floor's start. Merged in the other half, the arriver came by
    // that door's own scene and stands where it left him. DC_NETPLAY_MUTATE=arrival_any_half (twin): the record, as before.
    if (t.half != BtUraDongeon && !NetplayMutated("arrival_any_half")) {
        std::fprintf(stderr, "coop: arrival walk: none for player %d (its floor's arrival was in the other half: it stands where it came in)\n", player);
        return;
    }
    // coop-joinlate (Jep, 2026-11-02, on the walk coop-arrivalonce hid: "i wanted it not to happen, xiao already walked down the
    // stairs, she shouldn't do it a second time"): the arriver has had its arrival on its own copy (the game's floor entrance), so
    // at the lockstep it stands where that arrival ended, on both copies, and plays on from there: no walk again (it held her
    // screen black some 7 s more at the game's pace). DC_NETPLAY_MUTATE=arrival_walk_twice (twin): the walk again, as before.
    // coop-b4arrive (Jep's rc3 playtest: "P2 joined P1 on B4 and the floor entrance animation didn't play"; Jep 11-13: "start
    // playing the animation, and sync while the animation is playing"): not when the merge landed over its arrival as it played (a
    // pick join, CoopWorldsPickJoining): its own screen showed the game's entrance up to `from` (the record's tick the state set
    // landed at: the host's word in the last blob, NetplayEntranceFrom), so on both copies it goes on from there, the other
    // playing on. DC_NETPLAY_MUTATE=cut_arrival_stands (twin, CoopWorldsArrivalCut): stood at its end, as before.
    if (!cut && !NetplayMutated("arrival_walk_twice")) {
        sceVu0FVECTOR end;

        ArrivalStand(player, end);
        std::fprintf(stderr, "coop: arrival walk: none again for player %d (its own copy had its arrival): it stands at (%.0f %.0f %.0f)\n", player,
                     end[0], end[1], end[2]);
        return;
    }
    g_arrival_walk.player = player;
    // (coop-b4arrive: a cut arrival goes on from the record's tick its own screen had reached; DC_NETPLAY_MUTATE=cut_arrival_from_start
    // (twin): from its start, the frames its screen showed played twice)
    g_arrival_walk.tick = cut && !NetplayMutated("cut_arrival_from_start") ? std::clamp(from, 0, std::max(t.count - 1, 0)) : 0;
    g_arrival_walk.cut = cut;
    if (player == 2 && g_partner.chara != nullptr) {
        LoadArrival(); // her own stairs motion, where her member has one
    }
    // coop-joinatla: the stairwell door as the record has it, tick by tick (on both copies: the door is game state), and back to
    // where it stood at the end. DC_NETPLAY_MUTATE=arrival_door_shut (twin): left as it stands.
    CFrame *door = StairDoor();

    g_arrival_walk.door = door != nullptr;
    if (g_arrival_walk.door) {
        float lowest = t.samples[0].door;

        for (int i = 0; i < t.count; i++) {
            lowest = std::min(lowest, t.samples[i].door);
        }
        g_arrival_walk.door_before = door->position[1];
        door->SetPosition(door->position[0], t.samples[g_arrival_walk.tick].door, door->position[2]);
        std::fprintf(stderr, "coop: arrival walk: the stairwell door as recorded, from %.1f (at %.1f till now) down to %.1f\n", t.samples[g_arrival_walk.tick].door,
                     g_arrival_walk.door_before, lowest);
    }
    std::fprintf(stderr, "coop: arrival walk: player %d arrives from the stairs on both copies, %d ticks, from (%.0f %.0f %.0f)\n", player, t.count,
                 t.samples[0].place[0], t.samples[0].place[1], t.samples[0].place[2]);
    if (cut) {
        std::fprintf(stderr, "coop: arrival walk: player %d's own arrival goes on from record tick %d (its screen showed %d before the state set), on its own screen too\n",
                     player, g_arrival_walk.tick, from);
    }
}

// coop-b4arrive: this copy's own arrival on its floor as far as it has played (its record's ticks, the game's floor entrance under
// way), 0 none: what its screen showed of it before a merge's state set landed over it.
int ArrivalOwnShown() {
    const ArrivalTrail &t = g_arrival_trail;

    return t.recording == FloorKey() ? t.count : 0;
}

bool ArrivalWalkCut() {
    return g_arrival_walk.player != 0 && g_arrival_walk.cut;
}

int ArrivalWalkRecordTick() {
    return g_arrival_walk.player != 0 ? WalkAt() : -1;
}

int ArrivalWalkPlayer() {
    return g_arrival_walk.player;
}

// coop-b9arrive (Jep's rc1 B9: after the resync neither player could drive the floor menu): a walk is its floor's, and the arriver's
// pad is neutral on both copies while it plays (Route). A resync on the floor in a walker's entrance leaves for the dungeon's
// entrance (its floor menu, which the host's pad answers: menu_owner 1): same mode, same map, the save's floor unchanged, so the
// walk's own "left the floor" end (ArrivalWalkTick: mode or floor key changed) never came; the walk went on, his pad neutral for
// good, and the menu answered nobody. Ended here on both copies alike, as the state is taken. DC_COOP_MUTATE=walk_past_resync
// (twin): it goes on, as before.
void ArrivalWalkEndAtResync() {
    if (g_arrival_walk.player == 0) {
        return;
    }
    if (CoopMutate("walk_past_resync")) {
        std::fprintf(stderr, "coop: %s walk: player %d's goes on past the resync (walk_past_resync)\n", g_arrival_walk.entrance ? "entrance" : "arrival",
                     g_arrival_walk.player);
        return;
    }
    std::fprintf(stderr, "coop: %s walk: player %d's ended by the resync (at its tick %d): the resync leaves the floor\n",
                 g_arrival_walk.entrance ? "entrance" : "arrival", g_arrival_walk.player, g_arrival_walk.tick);
    g_arrival_walk = ArrivalWalk{};
}

void EntranceWalkStart(int player, int walker, int gap) {
    const ArrivalTrail &t = g_arrival_trail;
    const int           key = FloorKey();
    const bool          recording = t.recording == key;

    if (mode != GAME_MODE_DUNGEON || (t.key != key && !recording) || t.count <= 0 || (player != 1 && player != 2)) {
        std::fprintf(stderr, "coop: entrance walk: none for player %d (no walk-in of this floor recorded: here %d, recorded %d, recording %d)\n", player, key,
                     t.key, t.recording);
        if (t.key == key && (player == 1 || player == 2)) {
            sceVu0FVECTOR end;

            ArrivalStand(player, end);
        }
        return;
    }
    g_arrival_walk = ArrivalWalk{};
    g_arrival_walk.player = player;
    g_arrival_walk.tick = 0;
    g_arrival_walk.entrance = true;
    g_arrival_walk.walker = walker;
    // Rule 3: the lead's walk-in began t.count ticks before this lockstep (its record so far, alike on both copies) and the walker's
    // gap after it. Within the window the walker's entrance starts its arrival-order offset after the lead's: held at its start
    // till then, or begun that far in already (the merge's black took longer). Outside it, from its start now, as before.
    {
        int at[2] = {0, EntranceWalkerOffset(player, gap)}; // (cand1112c: the window's lead first on a tie)
        const bool staggered = recording && at[1] >= 0 && gap <= kArrivalWindow && !CoopMutate("entrance_arrivals_unstaggered");

        // coop-entrance2 round 6 (Jep 11-12, "no clipping anywhere"; x-entrwin-late on the forest: 6.3 apart, her held first frame on
        // his way out 181 ticks after his start): a later arrival's entrance never starts sooner than the floor's step after the lead's
        // (the forest's 220; elsewhere 24, which a later arrival has passed anyway). DC_COOP_MUTATE=entrance_late_unspaced (twin).
        const int  step = EntranceOrderStep(kArrivalStep);
        const bool spaced = !staggered && recording && t.count < step && !CoopMutate("entrance_late_unspaced") && !CoopMutate("entrance_arrivals_unstaggered");

        g_arrival_walk.shift = staggered ? at[1] - t.count : spaced ? step - t.count : 0;
        std::fprintf(stderr, "coop: entrance walk: player %d arrived %d ticks after the lead (window %d): %s, the lead %d ticks in: %s %d\n", player, gap,
                     kArrivalWindow, staggered ? "by arrival order" : spaced ? "spaced to the floor's step" : "as it arrived", t.count,
                     g_arrival_walk.shift >= 0 ? "held at its start" : "begun into it", std::abs(g_arrival_walk.shift));
        std::fprintf(stderr, "coop: entrance walk: player %d's entrance starts %d ticks after the lead's\n", player, t.count + g_arrival_walk.shift);
    }
    if (player == 2 && g_partner.chara != nullptr) {
        LoadArrival(); // her own stairs motion, where her member has one
    }
    // (The stairwell's door: the lead's walk-in, still under way, slides it as its script does; a record already ended, as the
    // arrival walk replays it.)
    CFrame *door = recording ? nullptr : StairDoor();

    g_arrival_walk.door = door != nullptr;
    if (g_arrival_walk.door) {
        g_arrival_walk.door_before = door->position[1];
        door->SetPosition(door->position[0], t.samples[0].door, door->position[2]);
    }
    sceVu0FVECTOR place;

    SamplePlace(t.samples[0], place);
    for (int i = 0; i < 3; i++) {
        g_arrival_walk.start[i] = place[i];
    }
    std::fprintf(stderr, "coop: entrance walk: player %d walks in from its start as walker %d on both copies, at (%.0f %.0f %.0f), the lead's way "
                         "from (%.0f %.0f %.0f), %d ticks of it recorded (the lead %s)\n",
                 player, walker, place[0], place[1], place[2], t.samples[0].place[0], t.samples[0].place[1], t.samples[0].place[2], t.count,
                 recording ? "still walking in" : "walked in already");
    // coop-entrance2, rule 1: a separate arrival's entrance takes no character delay (rule 3's order instead).
    std::fprintf(stderr, "coop: entrance walk: player %d as %s, a separate arrival: delay %d\n", player, EntranceCharaName(EntranceWalkChara()),
                 EntranceWalkDelay() - g_arrival_walk.shift);
    std::fflush(stderr);
}

bool EntranceWalkOn() {
    return g_arrival_walk.player != 0 && g_arrival_walk.entrance;
}

// candfix2b (x-entrwin-p2near-temple, red 1 run in 4: "draws the walker at its spot"): the first tick a walker the partner draws
// stands drawn at its start, said once (CoopStep, after the partner's step): the row read it from the once-a-second draw line
// instead, which caught the walker's 25-45 ticks at its start only when its phase fell there.
void EntranceWalkDrawnTrace() {
    ArrivalWalk &w = g_arrival_walk;

    if (!EntranceWalkOn() || w.drawn_said || (w.player != 2 && !EntranceWalkByPartner()) || !g_partner.visible ||
        std::hypot(g_partner.pos[0] - w.start[0], g_partner.pos[2] - w.start[2]) > 1.0f) {
        return;
    }
    w.drawn_said = true;
    std::fprintf(stderr, "coop: entrance walk: player %d drawn at its start (%.0f %.0f %.0f) on walk tick %d\n", w.player, g_partner.pos[0], g_partner.pos[1],
                 g_partner.pos[2], w.tick);
}

// coop-entrwin: the entrance window for any number of players (design for three). The first to walk in leads (a tie: the lower
// player number); each other whose walk-in begins before the lead's ends is a walker, numbered 2, 3 ... in the order they began
// (a tie: the lower player number first), at its own entrance spot (EntranceSpotFor); any later is a scene-arriver (0).
// DC_COOP_MUTATE=entrance_plan_pairs (twin): every walker is walker 2, the two-player rule (a third stands in the second).
void EntrancePlan(const EntranceArrival *in, int n, int lead_length, int *out) {
    int lead = -1;

    for (int i = 0; i < n; i++) {
        out[i] = 0;
        if (in[i].start >= 0 && (lead < 0 || in[i].start < in[lead].start || (in[i].start == in[lead].start && in[i].player < in[lead].player))) {
            lead = i;
        }
    }
    if (lead < 0) {
        return;
    }
    out[lead] = 1;
    for (int i = 0; i < n; i++) {
        if (i == lead || in[i].start < 0 || in[i].start >= in[lead].start + lead_length) {
            continue;
        }
        int before = 0; // walkers who began before this one

        for (int j = 0; j < n; j++) {
            if (j != i && j != lead && in[j].start >= 0 && in[j].start < in[lead].start + lead_length &&
                (in[j].start < in[i].start || (in[j].start == in[i].start && in[j].player < in[i].player))) {
                before++;
            }
        }
        out[i] = CoopMutate("entrance_plan_pairs") ? 2 : 2 + before;
    }
}

// Test hook (DC_COOP_TEST_ENTRANCE_PLAN=1, the row x-entrwin-plan): the plan for three players in four cases, and each dungeon's
// walker spots (2 and 3) beside a lead at (0 0 0) facing +z, then the copy ends.
void EntrancePlanTest() {
    struct Case {
        const char     *name;
        EntranceArrival in[3];
        int             length;
    };
    static const Case kCases[] = {
        {"all within", {{1, 0}, {2, 6}, {3, 60}}, 357},
        {"third late", {{1, 0}, {2, 6}, {3, 400}}, 357},
        {"tie first", {{1, 30}, {2, 0}, {3, 0}}, 357},
        {"second late", {{1, 120}, {2, 500}, {3, 100}}, 357},
    };
    for (const Case &c : kCases) {
        int out[3];

        EntrancePlan(c.in, 3, c.length, out);
        std::fprintf(stderr, "coop: entrance plan: %s:", c.name);
        for (int i = 0; i < 3; i++) {
            std::fprintf(stderr, " p%d %s%s", c.in[i].player, out[i] == 1 ? "lead" : out[i] >= 2 ? "walker " : "arriver",
                         out[i] >= 2 ? std::to_string(out[i]).c_str() : "");
        }
        std::fprintf(stderr, "\n");
    }
    const int map_was = selectMapNo;

    for (int map = 0; map < 6; map++) {
        const float at[3] = {0.0f, 0.0f, 0.0f};
        float       two[4], three[4], turn = 0.0f;

        selectMapNo = map;
        EntranceSpotPlace(at, 0.0f, 2, two, &turn);
        EntranceSpotPlace(at, 0.0f, 3, three, &turn);
        std::fprintf(stderr, "coop: entrance table: map %d walker 2 at (%.0f %.0f) walker 3 at (%.0f %.0f), %.0f apart\n", map, two[0], two[2], three[0],
                     three[2], std::hypot(two[0] - three[0], two[2] - three[2]));
    }
    selectMapNo = map_was;
    // coop-entrance2: the six characters' rules: each one's delay together (none apart), and its spot round Moon Sea's pad (centre
    // (0 0 0), the pad facing +z), with the nearest other spot (the beams' room).
    const float centre[4] = {0.0f, 0.0f, 0.0f, 1.0f};

    for (int chara = 0; chara < 6; chara++) {
        float spot[4];
        float nearest = 1e9f;

        MoonSpotAt(chara, centre, 0.0f, spot);
        for (int other = 0; other < 6; other++) {
            float there[4];

            if (other != chara) {
                MoonSpotAt(other, centre, 0.0f, there);
                nearest = std::min(nearest, std::hypot(spot[0] - there[0], spot[2] - there[2]));
            }
        }
        std::fprintf(stderr, "coop: entrance chara: %s apart %d, moon spot (%.1f %.1f) %.1f from the centre, nearest other %.1f\n",
                     EntranceCharaName(chara), CoopMutate("entrance_delay_apart") ? EntranceCharaDelay(chara) : 0, spot[0], spot[2],
                     std::hypot(spot[0], spot[2]), nearest);
    }
    // rule 3: arrivals (player @ the tick its walk-in began) and each one's entrance start by arrival order
    static const EntranceArrival kArrivalCases[][3] = {{{1, 0}, {2, 6}, {3, -1}}, {{1, 6}, {2, 0}, {3, -1}}, {{1, 0}, {2, 3}, {3, 6}},
                                                       {{1, 6}, {2, 3}, {3, 0}},  {{1, 0}, {2, 6}, {3, 30}}, {{1, 0}, {2, 30}, {3, -1}}};

    for (const auto &arrival_case : kArrivalCases) {
        int out[3];

        EntranceArrivalOffsets(arrival_case, 3, out);
        std::fprintf(stderr, "coop: entrance arrivals:");
        for (int i = 0; i < 3; i++) {
            if (arrival_case[i].start >= 0) {
                std::fprintf(stderr, " p%d@%d %d", arrival_case[i].player, arrival_case[i].start, out[i]);
            }
        }
        std::fprintf(stderr, "\n");
    }
    // cand1112c: a window walker's start after the lead's, by the walker's player and its gap in ticks (the lead first on a tie)
    std::fprintf(stderr, "coop: entrance walker offsets:");
    for (int player = 1; player <= 2; player++) {
        for (int gap : {-1, 0, 3}) {
            std::fprintf(stderr, " p%d@%d %d", player, gap, EntranceWalkerOffset(player, gap));
        }
    }
    std::fprintf(stderr, "\n");
    // the order together (round 3): each case's characters, as listed, and their offsets
    static const int kOrderCases[][4] = {{0, 1, -1, -1}, {1, 0, -1, -1}, {1, 2, 3, -1}, {3, 2, 1, -1}, {5, 0, 4, 2}, {4, -1, -1, -1}};

    for (const auto &order_case : kOrderCases) {
        int n = 0;
        int out[4] = {};

        while (n < 4 && order_case[n] >= 0) {
            n++;
        }
        EntranceOrderOffsets(order_case, n, out);
        std::fprintf(stderr, "coop: entrance order:");
        for (int i = 0; i < n; i++) {
            std::fprintf(stderr, " %s %d", EntranceCharaName(order_case[i]), out[i]);
        }
        std::fprintf(stderr, "\n");
    }
    std::fflush(stderr);
}

// coop-latefall (Jep's 2026-10-27 run: P1 joined B1 where P2 already played; Xiao fell through the floor, then the copies parted):
// every floor's arrival, script 100, loads its stairwell as an event actor (B1: dun/d01/stair_mos/in01.chr into NPC 1), so
// coop-b2join's "an actor loaded" took each plain walk-in for a scene like B2's cat (dun/d01/event/e614c04cat.chr). A scene's own
// pack is what tells them apart.
void ArrivalChrFile(int index, const char *path) {
    if (index < 0 || index >= 16) {
        return;
    }
    // coop-entrance2: nor Moon Sea's beam (dun/d05/effect/d05taiho.chr, its entrance's own effect: script 100's function 0xc428 loads it
    // as actor 0): read as a scene, its walk-in took the scene join, FloorIntro was off there and the partner stood on the pad from the
    // first tick (spots.png's moon rows). DC_COOP_MUTATE=moon_beam_scene (twin): a scene, as before.
    const bool entrance_effect = path != nullptr && std::strstr(path, "/effect/") != nullptr && std::strncmp(path, "dun/d0", 6) == 0; // (cand1112c: its switch removed, no row turned it on)
    const bool scene = path != nullptr && path[0] != 0 && std::strstr(path, "/stair_mos/") == nullptr && !entrance_effect;

    g_scene_pack_bits = scene ? g_scene_pack_bits | (1u << index) : g_scene_pack_bits & ~(1u << index);
}

bool ArrivalScenePackLoaded() {
    return g_scene_pack_bits != 0;
}

// coop-entrwin: player 1 walks in as a walker while player 2 leads the walk-in and it is still hers (coop-sceneowner: her floor
// merge in her floor's opening keeps her the game's player till its end, CoopWorldsSceneOwner): the game's player is her then, so
// his entrance is the partner's, drawn as his member (coop_core's partner pick, EntranceWalkMember); at her walk-in's end the
// handover makes him the game's player where his entrance has come to (g_scene_handover.toan, kept here each tick) and his walk
// goes on as player 1's. Probed (qr0, P2 2.3 s ahead): his walk moved the game's player, hers, to his spot, nothing of him was drawn,
// and at the handover she was put at his spot and fell through the floor.
// candfix2b (x-entrance-cave-p1-window, red whenever player 1 was the walker): her scene is read from its handover (due) itself,
// not CoopWorldsSceneOwner, whose "not apart" comes a tick after the lockstep's start: on the walk's first tick his walk took the
// game's player as his and wrote his start into it (ArrivalWalkPlayerOne). The game's player was her, carried by the cave's stairs
// node, so her local place became his start (1181 24 1861) under the node: she was drawn some 2000 away through the whole stairs
// on both screens (his walker too, on her record of it), and hidden as far away on his. The walker's entrance runs only linked
// (both copies), so the handover's due is her scene there. DC_COOP_MUTATE=entrance_owner_late (twin): CoopWorldsSceneOwner, as before.
bool EntranceWalkByPartner() {
    const bool handed_scene = CoopMutate("entrance_owner_late") ? CoopWorldsSceneOwner() == 2 : g_scene_handover.due != 0 && mode == GAME_MODE_DUNGEON;

    return g_arrival_walk.player == 1 && g_arrival_walk.entrance && handed_scene && !CoopMutate("entrance_his_as_player");
}

int EntranceWalkMember() {
    return EntranceWalkByPartner() ? g_scene_handover.member : -1;
}

// Player 1's arrival: the game's player put on the record's way before its motion steps (CoopBeforeMotion).
void ArrivalWalkPlayerOne() {
    const ArrivalTrail &t = g_arrival_trail;
    const ArrivalWalk  &w = g_arrival_walk;

    if (w.player != 1 || CharaFrame == nullptr || t.count <= 0 || EntranceWalkByPartner()) {
        return;
    }
    const ArrivalSample &s = t.samples[WalkAt()];
    sceVu0FVECTOR        place;

    SamplePlace(s, place);
    CharaFrame->SetPosition(place);
    CharaFrame->SetRotation(0.0f, SampleYaw(s), 0.0f);
    CharaMain.SetPosition(place);
    BtActStatus.motion_no = WalkMotion(&CharaMain, s.motion, SampleMoves(WalkAt()));
    if (CoopWorldsSceneOwner() != 2) { // (the game's player his: under entrance_his_as_player it is hers, her walk-in's motion her own)
        WalkMotionHeld(&CharaMain, BtActStatus.motion_no);
    }
}

// Player 2's arrival: the partner on the record's way, playing it (CoopStep, in Walk's place). False when hers is not playing.
bool ArrivalWalkPartner() {
    const ArrivalTrail &t = g_arrival_trail;
    const ArrivalWalk  &w = g_arrival_walk;

    const bool by_partner = EntranceWalkByPartner();

    if ((w.player != 2 && !by_partner) || g_partner.chara == nullptr || t.count <= 0) {
        return false;
    }
    const ArrivalSample &s = t.samples[WalkAt()];

    // (round 6, x-entrwin-p2first 0.7 apart: player 1's walker drawn by the partner as his member had no stairs motion of its own
    // loaded, only player 2's walker loaded one, so his body stood idle at the niche's one end while hers walked out onto it.
    // LoadArrival loads it once for the member the partner shows.)
    if (by_partner && w.entrance && !CoopMutate("entrance_walk_motion_runs")) {
        LoadArrival();
    }
    const int motion = WalkMotion(g_partner.chara, s.motion, SampleMoves(WalkAt()));

    SamplePlace(s, g_partner.pos);
    if (by_partner) { // (where he becomes the game's player at her walk-in's end)
        g_scene_handover.toan[0] = g_partner.pos[0];
        g_scene_handover.toan[1] = g_partner.pos[1];
        g_scene_handover.toan[2] = g_partner.pos[2];
        g_scene_handover.yaw = SampleYaw(s);
    }
    g_partner.velocity[0] = g_partner.velocity[1] = g_partner.velocity[2] = 0.0f;
    g_partner.chara->frame->SetPosition(g_partner.pos);
    g_partner.chara->frame->SetRotation(0.0f, SampleYaw(s), 0.0f);
    if (motion != g_partner.motion) {
        PlayMotion(motion, 4);
    }
    WalkMotionHeld(g_partner.chara, motion);
    return true;
}

// Once a tick (CoopStep, both copies): the walk goes on, and at its end the arriver stands where play started.
void ArrivalWalkTick() {
    const ArrivalTrail &t = g_arrival_trail;
    ArrivalWalk        &w = g_arrival_walk;

    if (w.player == 0) {
        return;
    }
    // (coop-entrwin: a walker's entrance plays the lead's walk-in while it is still recorded, behind it)
    const bool recording = w.entrance && t.recording == FloorKey();

    if (mode != GAME_MODE_DUNGEON || (t.key != FloorKey() && !recording)) {
        std::fprintf(stderr, "coop: arrival walk: player %d's ended early (left the floor)\n", w.player);
        w = ArrivalWalk{};
        return;
    }
    CFrame *door = w.door ? StairDoor() : nullptr;

    if (recording && WalkAt() + 1 >= t.count) {
        return; // (the record's next tick not made yet: never while the lead walks in ahead of the walker)
    }
    // cand1112c (Jep 11-12: players who pick the same floor play their entrances in the window, by arrival order; it gets the together
    // walk-in's treatment): the temple's door (EntranceDoors' two halves) shut behind the lead, before the walker was through
    // (coop-entrance2 round 7's cause, together). While a walker plays its entrance the door stands where the walker's moment of the
    // lead's way had it whenever that is more open than the script's own now: it shuts behind the last walker, on both copies alike
    // (the record is game state). DC_COOP_MUTATE=entrance_door_lead_only (twin, the together one's): the lead's door.
    if (w.entrance && selectMapNo == 3 && t.count > 0) {
        static float door_most = 0.0f;
        static int   lead_closing = -1;
        static int   live_closed = -1;
        static int   through = -1;
        static bool  logged = false;
        CFrame      *pieces[2] = {nullptr, nullptr};
        int          axis = 1;
        const int    door_n = EntranceDoors(pieces, &axis);
        const float  start = t.samples[0].halves;

        if (w.tick == 0) {
            door_most = start;
            lead_closing = live_closed = through = -1;
            logged = false;
            g_door_written = 1e9f;
        }
        if (door_n > 0) {
            if (pieces[0]->position[axis] != g_door_written) {
                g_door_script = pieces[0]->position[axis];
            }
            const float live = g_door_script;
            const int   at = WalkAt();
            const float mine = t.samples[std::min(at, t.count - 1)].halves;

            if (std::fabs(live - start) >= std::fabs(door_most - start)) {
                door_most = live;
            } else if (lead_closing < 0 && std::fabs(door_most - start) > 1.0f) {
                lead_closing = recording ? t.count - 1 : at;
            }
            const bool  ended = w.tick + 1 >= t.count + EntranceWalkDelay();

            (void) mine;
            if (through < 0 && lead_closing >= 0 && at >= lead_closing) {
                through = w.tick;
            }
            // (coop-entrance8: draw-only, as together: fully open till the walker reaches the lead's closing point, then the record's
            // close (the lead's door from its closing tick on), from its first frame, one a tick; nothing writes the halves but the script)
            const int   k = through >= 0 ? w.tick - through : -1;
            const int   at_close = lead_closing >= 0 && k >= 0 ? lead_closing + k : -1;
            const bool  delay = lead_closing >= 0 && !ended && !CoopMutate("entrance_door_lead_only") && (k < 0 || at_close < t.count) &&
                               std::fabs(live - start) < std::fabs(door_most - start) + 0.01f;
            const float held = !delay ? live : k < 0 ? door_most : t.samples[at_close].halves;

            g_door_draw = delay ? held : 1e9f;
            {
                // (as it has played: the record's close from the lead's closing tick, in order, one a tick; x-entrance/x-entrwin read it)
                static int close_n = -1;

                if (w.tick == 0) {
                    close_n = -1;
                    g_door_drawn = DoorDrawn{};
                }
                if (close_n < 0 && lead_closing >= 0) { // (once the record holds the close to shut)
                    int n = 0;

                    while (lead_closing + n < t.count && std::fabs(t.samples[lead_closing + n].halves - start) > 0.01f) {
                        n++;
                    }
                    close_n = lead_closing + n < t.count ? n + 1 : -1;
                }
                if (close_n > 0 && k == close_n && !CoopMutate("entrance_door_lead_only")) {
                    std::fprintf(stderr, "coop: entrance walk: map 3 door: retail's close played in order, %d frames from %.1f to %.1f, from its tick %d; the "
                                         "walker through at %d, the lead's close began at record tick %d\n",
                                 close_n, t.samples[lead_closing].halves, t.samples[lead_closing + close_n - 1].halves, through, through, lead_closing);
                    std::fprintf(stderr, "coop: entrance walk: map 3 door drawn: %d draws, the halves' largest step %.2f/%.2f, side changes %d/%d, opened again %d/%d\n",
                                 g_door_drawn.draws, g_door_drawn.most_step[0], g_door_drawn.most_step[1], g_door_drawn.side_changes[0],
                                 g_door_drawn.side_changes[1], g_door_drawn.reopened[0], g_door_drawn.reopened[1]);
                }
            }
            if (live_closed < 0 && lead_closing >= 0 && std::fabs(held - start) < std::fabs(door_most - start) - 0.5f) {
                live_closed = w.tick;
            }
            if (!logged && (live_closed >= 0 || ended)) {
                logged = true;
                std::fprintf(stderr, "coop: entrance walk: map 3 door: the lead's closing at record tick %d, the walker through at its tick %d, it closes as "
                                     "drawn at its tick %d\n",
                             lead_closing, through, live_closed);
            }
        }
    }
    // coop-entrance2 (Jep 11-12, "no clipping anywhere"): a window walker's entrance, the two bodies (hips) while both are drawn: the
    // nearest and the first tick closer than 8, said as it plays out (x-entrwin reads it); every tick with DC_COOP_TEST_ENTRANCE_BODIES.
    static float walk_near = 1e9f;
    static int   walk_near_tick = -1;
    static int   walk_close_tick = -1;

    if (w.tick == 0) {
        walk_near = 1e9f;
        walk_near_tick = walk_close_tick = -1;
    }
    if (w.entrance && g_partner.chara != nullptr && g_partner.chara->frame != nullptr && g_partner.chara->frame->child != nullptr &&
        CharaMain.frame != nullptr && CharaMain.frame->child != nullptr && !EntranceLeadHidden() && !EntrancePartnerHidden()) {
        sceVu0FMATRIX his;
        sceVu0FMATRIX hers;

        CharaMain.frame->child->GetLWMatrix(his);
        g_partner.chara->frame->child->GetLWMatrix(hers);
        const float apart = std::hypot(his[3][0] - hers[3][0], his[3][2] - hers[3][2]);

        if (apart < walk_near) {
            walk_near = apart;
            walk_near_tick = w.tick;
        }
        if (apart < 8.0f && walk_close_tick < 0) {
            walk_close_tick = w.tick;
        }
        if (TestEnv("DC_COOP_TEST_ENTRANCE_BODIES") != nullptr) {
            std::fprintf(stderr, "coop: entrance walk bodies: tick %d game's player (%.1f %.1f) partner (%.1f %.1f) apart %.1f\n", w.tick, his[3][0], his[3][2],
                         hers[3][0], hers[3][2], apart);
        }
    }
    // coop-entrance2 round 6 (Jep 11-12, "no clipping anywhere"; x-entrwin-near-temple 7.0, p2near-temple 0.2): the temple's walker
    // waits on the way while its next step would bring it within kWalkHoldBack of the other (its delay grows by the ticks it waits;
    // not drawn while that keeps it at its start), as together. DC_COOP_MUTATE=entrance_trail_close (twin, the together one's too).
    constexpr float kWalkHoldBack = 10.0f;

    // (only while the lead still walks in: stopped at the end, the walker beside him would wait for ever: x-entrwin-p2near-temple)
    // cand1112c (x-entrance-forest-window: 0.2 apart, the walker on the lead stood at the niche's end, his walk-in played out; the
    // window's lead plays on, not held as together's stand-in that walks on): the forest's walker holds back the same way.
    const bool holds_back = selectMapNo == 3 || selectMapNo == 1;

    if (w.entrance && holds_back && recording && !CoopMutate("entrance_trail_close") && w.tick >= EntranceWalkDelay() && t.count > 0) {
        sceVu0FVECTOR next;

        SamplePlace(t.samples[std::min(WalkAt() + 1, t.count - 1)], next);
        const float *other = w.player == 2 || EntranceWalkByPartner() ? CharaMain.pos : g_partner.pos;

        w.shift += std::hypot(next[0] - other[0], next[2] - other[2]) < kWalkHoldBack ? 1 : 0;
    } else if (w.entrance && holds_back && !recording && !CoopMutate("entrance_trail_close") && w.tick > EntranceWalkDelay() && t.count > 0) {
        // (the lead's walk-in over and stopped at its end: a next step within kWalkHoldBack ends the walk where it is bound, its spot
        // beside him (14), not 7 at his side on his way: x-entrwin-near-temple, 7.0 at walk tick 435)
        sceVu0FVECTOR next;

        SamplePlace(t.samples[std::min(WalkAt() + 1, t.count - 1)], next);
        const float *other = w.player == 2 || EntranceWalkByPartner() ? CharaMain.pos : g_partner.pos;

        if (std::hypot(next[0] - other[0], next[2] - other[2]) < kWalkHoldBack) {
            w.tick = t.count + EntranceWalkDelay() - 1;
        }
    }
    if (w.entrance && w.tick > EntranceWalkDelay()) { // (round 6: it has stepped once its record's tick moves on from where it began)
        w.first_at = w.first_at < 0 ? WalkAt() : w.first_at;
        w.stepped = w.stepped || WalkAt() > w.first_at || selectMapNo != 3;
    }
    // coop-b9arrive test hook (DC_COOP_TEST_DESYNC_IN_WALK=1, x-entrwin -Case resyncwalk): 100 ticks into a walker's entrance (past the catch-up's first sync),
    // every copy but the walker's own takes a point of its player's life (DC_NETPLAY_PERTURB's own desync), once: the checksum's OUT OF SYNC
    // and the resync come while the walk plays, as on Jep's rc1 B9.
    if (static bool desync_done = false; !desync_done && w.entrance && w.tick == 100 && NetplayHasPlayer() && NetplayLocalPlayer() != w.player && UserStatus != nullptr &&
                                         TestEnv("DC_COOP_TEST_DESYNC_IN_WALK") != nullptr) {
        desync_done = true;
        UserStatus->hp[UserStatus->cur_chara] -= 1;
        std::fprintf(stderr, "coop: test: a desync in player %d's entrance walk (player %d's copy alone)\n", w.player, NetplayLocalPlayer());
    }
    // coop-b4arrive: what the arriver's own screen showed a second into its cut arrival (its rows read it: shown, not black).
    if (w.cut && w.tick == 60 && w.player == NetplayLocalPlayer()) {
        std::fprintf(stderr, "coop: arrival walk: player %d's own screen at walk tick 60: %s\n", w.player, NetplayArrivalBlack() ? "black" : "shown");
    }
    if (++w.tick < t.count + (w.entrance ? EntranceWalkDelay() : 0)) {
        if (door != nullptr) {
            door->SetPosition(door->position[0], t.samples[WalkAt()].door, door->position[2]);
        }
        if (CoopDebug() && w.tick % 30 == 0) {
            const ArrivalSample &s = t.samples[WalkAt()];

            std::fprintf(stderr, "coop: arrival walk: player %d tick %d at (%.0f %.0f %.0f) motion %d door %.1f\n", w.player, w.tick, s.place[0],
                         s.place[1], s.place[2], s.motion, StairDoor() != nullptr ? StairDoor()->position[1] : -999.0f);
        }
        return;
    }
    if (door != nullptr) {
        door->SetPosition(door->position[0], w.door_before, door->position[2]);
    }
    sceVu0FVECTOR end;

    ArrivalStand(w.player, end);
    std::fprintf(stderr, "coop: %s walk: player %d's %s played out (%d ticks): at (%.0f %.0f %.0f)\n", w.entrance ? "entrance" : "arrival", w.player,
                 w.entrance ? "own entrance" : "arrival", w.tick, end[0], end[1], end[2]);
    // coop-b4arrive: a cut arrival's end is where the floor's start ("go dungeon", movechara's) puts up the floor's title and slides the
    // HUD in, which the other's state had taken the arriver's copy past: its own screen gets them now (draw-only counters, which no
    // checksum reads; the other copy's screen keeps its own, its title long over). DC_NETPLAY_MUTATE=cut_arrival_no_title (twin): none.
    if (w.cut && w.player == NetplayLocalPlayer() && !NetplayMutated("cut_arrival_no_title")) {
        FloorTitleOn();
        std::fprintf(stderr, "coop: arrival walk: player %d's floor title up on its own screen (floor %d)\n", w.player,
                     UserStatus != nullptr ? UserStatus->cur_floor + 1 : -1);
    }
    if (w.entrance && walk_near_tick >= 0) {
        std::fprintf(stderr, "coop: entrance walk: on map %d the bodies came %.1f apart at their nearest (tick %d), first closer than 8 at %d\n", selectMapNo,
                     walk_near, walk_near_tick, walk_close_tick);
    }
    std::fflush(stderr);
    w = ArrivalWalk{};
}

// The arriver where its arrival ends (end: where): where play started on the floor, beside the other player when that one stands
// there, with its camera reset behind it.
static void ArrivalStand(int player, float *end) {
    const ArrivalTrail &t = g_arrival_trail;

    end[0] = t.end[0];
    end[1] = t.end[1];
    end[2] = t.end[2];
    end[3] = 1.0f;

    // coop-entrwin: a walker's own entrance ends at its spot where the lead's walk-in ended (in the lead's frame there), beside the
    // lead if he still stands there; moved aside only when another player stands on that very spot.
    const bool entrance = g_arrival_walk.entrance && g_arrival_walk.player == player;
    float      end_yaw = t.end_yaw;

    if (entrance) {
        float turn = 0.0f;

        EntranceSpotPlace(t.end, t.end_yaw, g_arrival_walk.walker, end, &turn);
        end_yaw = t.end_yaw + turn;
    }

    // The other player may stand at or near where play starts (the one there first, not moved since its own arrival): the arriver ends
    // beside it, at its right hand or else its left (the way clear), not inside or behind it (x-floor-join -JoinVisuals: Toan hidden
    // under Xiao, then behind her camera, on her screen).
    const float *other = player == 1 ? g_partner.pos : CharaMain.pos;

    if (std::hypot(other[0] - end[0], other[2] - end[2]) < (entrance ? 0.5f : 2.5f) * kSpawnDistance) {
        const float side[2] = {cosf(t.end_yaw), -sinf(t.end_yaw)};

        for (const float sign : {-1.0f, 1.0f}) {
            sceVu0FVECTOR beside = {other[0] + sign * kSpawnDistance * side[0], end[1], other[2] + sign * kSpawnDistance * side[1], 1.0f};

            if (PathClear(other, beside) || sign > 0.0f) {
                sceVu0CopyVector(end, beside);
                break;
            }
        }
    }

    if (player == 1 && CharaFrame != nullptr) {
        CharaFrame->SetPosition(end);
        CharaFrame->SetRotation(0.0f, end_yaw, 0.0f);
        CharaMain.SetPosition(end);
        BtActStatus.motion_no = 0;
        ResetArrivalCamera(&MainCamera__4, end, end_yaw); // coop-arrivalcam: on both copies (his camera is game state)
    } else if (player == 2 && g_partner.chara != nullptr) {
        sceVu0CopyVector(g_partner.pos, end);
        g_partner.chara->frame->SetPosition(g_partner.pos);
        g_partner.chara->frame->SetRotation(0.0f, end_yaw, 0.0f);
        PlayMotion(0, 0);
        ResetArrivalCamera(g_camera.ready ? g_camera.camera : nullptr, end, end_yaw); // coop-arrivalcam: hers, stepped on both
    } else if (player == 2) {
        // coop-joinlate: her member not loaded yet at the lockstep (a join by the host's state, "the copies start apart": it loads
        // after, on both copies, and its first load put her beside him, 149 from the stairs): that load puts her here, once
        // (coop_party.cpp's arrive_floor, as a join from her own arrival does).
        g_partner.arrive_floor = FloorKey();
        g_partner.arrive_pos[0] = end[0];
        g_partner.arrive_pos[1] = end[1];
        g_partner.arrive_pos[2] = end[2];
        g_partner.arrive_yaw = t.end_yaw;
        std::fprintf(stderr, "coop: arrival walk: player 2's member loads at (%.0f %.0f %.0f), where her arrival ended\n", end[0], end[1], end[2]);
    }
}

// coop-arrivalcam: the camera this copy draws the walk from, when it is the arriver's copy: the event's own, as the record has it at
// this tick of the walk. False otherwise (the other copy keeps its own camera) and for the twin arrival_follow_cam.
bool ArrivalWalkView(float *eye, float *ref) {
    const ArrivalTrail &t = g_arrival_trail;
    const ArrivalWalk  &w = g_arrival_walk;

    if (w.player == 0 || w.player != NetplayLocalPlayer() || t.count <= 0 || NetplayMutated("arrival_follow_cam")) {
        return false;
    }
    const ArrivalSample &s = t.samples[WalkAt()];
    // coop-entrwin: a walker's own entrance: the walk-in's camera moved by its spot from the lead's way (as FloorIntro's view
    // moves the opening's camera by where the partner stands), so its screen frames it as the lead's framed him.
    sceVu0FVECTOR place;

    SamplePlace(s, place);
    for (int i = 0; i < 3; i++) {
        const float shift = w.entrance ? place[i] - s.place[i] : 0.0f;

        eye[i] = s.eye[i] + shift;
        ref[i] = s.ref[i] + shift;
    }
    eye[3] = ref[3] = 1.0f;
    return true;
}

// Test hook (DC_COOP_TEST_ARRIVAL_VIEW=1): through the walk, every 10th tick, the camera this copy draws from and where it has
// the arriver.
void ArrivalViewLog(CCameraFollow *camera) {
    static int         logged = -1;
    const ArrivalWalk &w = g_arrival_walk;

    if (TestEnv("DC_COOP_TEST_ARRIVAL_VIEW") == nullptr || w.player == 0 || camera == nullptr || w.tick % 10 != 0 || w.tick == logged) {
        return;
    }
    logged = w.tick;

    sceVu0FVECTOR eye;
    sceVu0FVECTOR ref;
    const float  *drawn = w.player == 1 ? CharaMain.pos : g_partner.pos;

    camera->GetPos(eye);
    camera->GetRef(ref);
    std::fprintf(stderr, "coop: arrival view %d p%d of p%d eye (%.1f %.1f %.1f) ref (%.1f %.1f %.1f) drawn (%.1f %.1f %.1f)\n", w.tick,
                 NetplayLocalPlayer(), w.player, eye[0], eye[1], eye[2], ref[0], ref[1], ref[2], drawn[0], drawn[1], drawn[2]);
}

// coop-entrance2: the character the floor's lead is drawn as (the game's player: his, or hers while her walk-in is hers or her copy
// stands her member in for his), and a walker's (its own entrance in the window).
int EntranceLeadChara() {
    if (CoopWorldsSceneOwner() == 2) {
        return g_scene_handover.her;
    }
    if (const int stand_in = CoopWorldsEntryStandIn(); stand_in >= 0) {
        return stand_in;
    }
    return UserStatus != nullptr ? UserStatus->cur_chara : 0;
}

int EntranceWalkChara() {
    const ArrivalWalk &w = g_arrival_walk;

    if (w.player == 2) {
        return g_partner.loaded_chara;
    }
    if (EntranceWalkByPartner()) {
        return g_scene_handover.member;
    }
    return UserStatus != nullptr ? UserStatus->cur_chara : -1;
}

// Rule 1's twin (DC_COOP_MUTATE=entrance_delay_apart): a separate arrival's entrance delayed by its character too.
// With rule 3's shift: + held at the record's first tick, - begun that far into it.
int EntranceWalkDelay() {
    return g_arrival_walk.shift + (CoopMutate("entrance_delay_apart") ? EntranceCharaDelay(EntranceWalkChara()) : 0);
}

// The Moon Sea clock as a walker's entrance stands in it (the lead's, as the record has it at the walk's tick), its delay taken.
int MoonWalkClock() {
    const ArrivalTrail &t = g_arrival_trail;
    const ArrivalWalk  &w = g_arrival_walk;

    if (!EntranceWalkOn() || t.count <= 0) {
        return -1;
    }
    const int at = w.tick - EntranceWalkDelay();

    return at < 0 ? -1 : t.samples[std::min(at, t.count - 1)].moon;
}

// Once a tick, every copy (ArrivalTrailTick): the clock, the script's reveal, the lead at its own spot round the pad (game state:
// both copies alike, from the beam actor's state), and the retail beam at the centre not drawn (each character has its own).
void MoonEntranceTick() {
    if (!MoonEntrance() || UserStatus == nullptr) {
        return;
    }
    const int  key = FloorKey();
    const bool script = BtEventMode != 0 && BtEventInfo.running_script_no == 100;

    if (script && g_moon.key != key) {
        g_moon = MoonClock{};
        g_moon.key = key;
    }
    if (g_moon.key != key) {
        return;
    }
    int             index = -1;
    tagMOTION_TYPE *motion = script ? BeamMotion(&NPCUnit[0], &index) : nullptr;

    if (motion != nullptr) {
        const MOTION_INFO &info = motion->motion_info[index];
        const float        speed = info.speed > 0.0f ? info.speed : 1.0f;
        const int          length = static_cast<int>((info.end - info.start) / speed);
        const int          at = static_cast<int>((motion->state.time - info.start) / speed + 0.5f);

        g_moon.length = length;
        // (on its last frame the motion holds: the clock counts on)
        g_moon.clock = g_moon.clock >= 0 && at <= g_moon.clock ? g_moon.clock + 1 : std::max(at, 0);
        EdEventInfo.npc_draw[0] = 0; // (its beam is drawn per character, EntranceBeamsDraw)
    } else if (g_moon.clock >= 0 && g_moon.clock < g_moon.length + kMoonAfter) {
        g_moon.clock++;
    }
    if (script && g_moon.clock >= 0 && g_moon.reveal < 0 && EdEventInfo.player_draw != 0) {
        g_moon.reveal = g_moon.clock;
        std::fprintf(stderr, "coop: moon entrance: the script shows the player at beam tick %d (beam %d ticks); %s\n", g_moon.reveal, g_moon.length,
                     EntranceTogether() ? "together: each at its character's delay" : "no delays");
    }
    // The lead at its character's spot from the beam's start (the script stood it at the centre just before) to the script's end.
    sceVu0FVECTOR place;
    float         yaw = 0.0f;

    if (script && g_moon.clock >= 0 && !(EntranceWalkOn() && g_arrival_walk.player == 1 && !EntranceWalkByPartner()) && CharaFrame != nullptr &&
        MoonSpot(EntranceLeadChara(), place, &yaw)) {
        CharaFrame->SetPosition(place);
        CharaFrame->SetRotation(0.0f, yaw, 0.0f);
        CharaMain.SetPosition(place);
        if (g_moon.clock == 0) {
            std::fprintf(stderr, "coop: moon entrance: lead %s at its spot (%.1f %.1f %.1f)\n", EntranceCharaName(EntranceLeadChara()), place[0], place[1], place[2]);
        }
    }
}

namespace {

// The moment a character's beam stands at (ticks into the beam, -1 not begun) and whether it is drawn yet.
int LeadBeamTick() {
    const int clock = MoonEntranceClock();

    if (clock < 0) {
        return -1;
    }
    return clock - (EntranceTogether() ? EntranceTogetherOffset(EntranceLeadChara()) : 0);
}

int PartnerBeamTick() {
    if (EntranceWalkOn() && (g_arrival_walk.player == 2 || EntranceWalkByPartner())) {
        return MoonWalkClock();
    }
    const int clock = MoonEntranceClock();

    return clock < 0 || !EntranceTogether() ? -1 : clock - EntranceTogetherOffset(g_partner.loaded_chara);
}

bool MoonOpening() {
    return MoonEntrance() && (FloorIntro() || EntranceWalkOn() || (MoonEntranceClock() >= 0 && MoonEntranceClock() < g_moon.length + kMoonAfter));
}

bool BeamHides(int beam_tick) {
    return beam_tick < 0 || g_moon.reveal < 0 || beam_tick < g_moon.reveal;
}

} // namespace

// coop-entrance2 (the forest's step, kForestOrderStep): one waiting for its own start is not drawn: together, the lead or the partner
// held before its stairs motion (StandBesideArrival's g_wait_hidden); a walker in the entrance window held at its start (rule 3's
// shift). DC_COOP_MUTATE=entrance_wait_drawn (twin): drawn while waiting, as before.
bool EntranceWaitHidden(bool lead) {
    if (CoopMutate("entrance_wait_drawn")) {
        return false;
    }
    if (EntranceWalkOn() && (g_arrival_walk.tick <= EntranceWalkDelay() || !g_arrival_walk.stepped)) { // (round 6: and held at its start)
        const bool walker_partner = g_arrival_walk.player == 2 || EntranceWalkByPartner();

        return lead ? !walker_partner : walker_partner;
    }
    return FloorIntro() && g_wait_hidden[lead ? 0 : 1];
}

bool EntranceLeadHidden() {
    if (EntranceWaitHidden(true)) {
        return true;
    }
    if (!MoonOpening()) {
        return false;
    }
    if (EntranceWalkOn() && g_arrival_walk.player == 1 && !EntranceWalkByPartner()) {
        return BeamHides(MoonWalkClock()); // (he walks in as a walker of hers on his own: the game's player)
    }
    return BtEventMode != 0 || MoonEntranceClock() >= 0 ? BeamHides(LeadBeamTick()) && MoonEntranceClock() >= 0 : false;
}

bool EntrancePartnerHidden() {
    if (EntranceWaitHidden(false)) {
        return true;
    }
    if (!MoonOpening()) {
        return false;
    }
    if (EntranceWalkOn() && (g_arrival_walk.player == 2 || EntranceWalkByPartner())) {
        return BeamHides(MoonWalkClock());
    }
    return EntranceTogether() && BeamHides(PartnerBeamTick());
}

// Every tick (ArrivalTrailTick): each character's showing on the pad, on stderr (x-entrance's moon cases read it): who, as the
// lead or the partner, at which beam clock, from which spot.
void MoonShownTick() {
    static int  key = -1000;
    static bool shown[2] = {false, false};

    if (!MoonEntrance() || MoonEntranceClock() < 0 || g_moon.reveal < 0) {
        return;
    }
    if (key != g_moon.key) {
        key = g_moon.key;
        shown[0] = shown[1] = false;
    }
    const int   chara[2] = {EntranceLeadChara(), g_partner.loaded_chara};
    const bool  hidden[2] = {EntranceLeadHidden(), EntrancePartnerHidden()};
    const char *role[2] = {"lead", "partner"};

    for (int i = 0; i < 2; i++) {
        sceVu0FVECTOR spot;
        float         yaw = 0.0f;

        if (!shown[i] && !hidden[i] && (i == 0 || g_partner.chara != nullptr) && MoonSpot(chara[i], spot, &yaw)) {
            shown[i] = true;
            std::fprintf(stderr, "coop: moon entrance: %s %s shown at beam clock %d (the script's reveal %d), its spot (%.1f %.1f %.1f)\n", role[i],
                         EntranceCharaName(chara[i]), MoonEntranceClock(), g_moon.reveal, spot[0], spot[1], spot[2]);
        }
    }
}

namespace {

// The beam's own model (the retail actor's pack and motion), one posed per character: draw only, loaded once a floor.
constexpr int kBeamBlock = 0x3F; // (on a floor, no other user: the lobby's sixth model's, in the title)

struct BeamModel {
    CCharacter    *body = nullptr;
    CTexAnimeData *tex_anime = nullptr;
    u_int         *pack = nullptr;
    u_char        *model = nullptr;
    CDataAlloc2<1> arena;
    int            floor = -1000;
} g_beam;

bool BeamReady() {
    const int floor = FloorKey();
    bool      block_there = false;

    for (const CTexture &texture : TexManager.textures) {
        block_there = block_there || (texture.name[0] != 0 && texture.block == kBeamBlock);
    }
    if (g_beam.floor == floor && block_there) {
        return true;
    }
    if (g_beam.body == nullptr) {
        g_beam.body = new CCharacter();
        g_beam.tex_anime = new CTexAnimeData[64];
        g_beam.pack = reinterpret_cast<u_int *>(new (std::align_val_t{64}) u_char[kPackBytes]);
        g_beam.model = new (std::align_val_t{64}) u_char[static_cast<size_t>(kModelQuads) * 16];
    }
    g_beam.floor = -1000;

    int  size = 0;
    char path[] = "dun/d05/effect/d05taiho.chr";

    if (LoadFile(path, g_beam.pack, &size) == 0 || size <= 0 || size > kPackBytes) {
        return false;
    }
    TexManager.DeleteTextureBlock(kBeamBlock);
    TexManager.CleanUpBuffer();
    TexManager.CleanUpTextureList();
    g_beam.arena.base = g_beam.model;
    g_beam.arena.buffer = g_beam.model;
    g_beam.arena.used = 0;
    g_beam.arena.limit = kModelQuads;

    CCharacter *body = g_beam.body;

    body->Initialize();
    for (int i = 0; i < 64; i++) {
        g_beam.tex_anime[i].Initialize();
    }
    body->InitializeTexAnime(g_beam.tex_anime, 64);
    body->LoadPackData2(g_beam.pack, (char *) "info.cfg", &g_beam.arena, kBeamBlock, &g_beam.arena, 0);
    if (body->frame == nullptr) {
        return false;
    }
    body->FootSoundEnable(0);
    body->EventEnable(0);
    g_beam.floor = floor;
    std::printf("coop: moon entrance: the beam's model loaded for the characters' own beams\n");
    return true;
}

// One beam, posed beam_tick ticks into the retail motion 2, at a character's spot.
void DrawBeam(int chara, int beam_tick) {
    sceVu0FVECTOR place;
    float         yaw = 0.0f;
    CCharacter   *body = g_beam.body;
    int           index = -1;

    body->motion_no = 2;
    tagMOTION_TYPE *motion = body->GetMotionParam(2, &index, nullptr, nullptr, nullptr);

    if (motion == nullptr || motion->motion_info == nullptr || index < 0 || !MoonSpot(chara, place, &yaw)) {
        return;
    }
    const MOTION_INFO &info = motion->motion_info[index];
    const float        speed = info.speed > 0.0f ? info.speed : 1.0f;

    motion->state.motion_no = index;
    motion->state.playing_no = index;
    motion->state.blending = 0;
    motion->state.time = std::min(info.start + beam_tick * speed, static_cast<float>(info.end) - 0.01f);
    place[1] += 1.0f; // (the beam at the pad's height: the script stands the player 1 below it)
    body->SetPosition(place);
    body->SetRotation(0.0f, yaw, 0.0f);
    body->fade[0] = 1.0f;
    SetMotionEX(body->frame, motion, motion->motion_info, &motion->state, motion->frame_info);
    body->Draw();
}

} // namespace

// Draw only (CoopDraw, every copy): the beam of each character on the floor whose beam plays now, at its own spot.
void EntranceBeamsDraw() {
    if (!MoonOpening() || g_moon.length <= 0) {
        return;
    }
    struct Beam {
        int chara;
        int tick;
    } beams[3];
    int n = 0;

    beams[n++] = {EntranceLeadChara(), EntranceWalkOn() && g_arrival_walk.player == 1 && !EntranceWalkByPartner() ? MoonWalkClock() : LeadBeamTick()};
    if (g_partner.chara != nullptr && g_partner.loaded_chara >= 0) {
        beams[n++] = {g_partner.loaded_chara, PartnerBeamTick()};
    }
    bool any = false;

    for (int i = 0; i < n; i++) {
        any = any || (beams[i].tick >= 0 && beams[i].tick < g_moon.length);
    }
    if (!any || !BeamReady()) {
        return;
    }
    TexManager.ReloadTexture(Vif1Packet, kBeamBlock);
    g_beam.body->TextureAnime(kBeamBlock);
    for (int i = 0; i < n; i++) {
        if (beams[i].tick >= 0 && beams[i].tick < g_moon.length) {
            DrawBeam(beams[i].chara, beams[i].tick);
        }
    }
}

} // namespace coop

// coop-camflash2 (the cave entry camflash measured: P2 following P1 in saw her own arrival's camera for a frame before his opening
// scene): what this tick's floor draw was drawn from: 1 the other's scene view, 0 another camera, -1 no floor draw this tick (a
// menu's). The arrival's black (ArrivalHoldNow) lifts for that view, and it asks after the draw, in the present (GameRenderTick,
// and the display frames up to the next tick), while the view's freshness (CoopWorldsSceneView: his latest view heard within
// 500 ms) can change in between: his first view landed after her draw had chosen her own arrival's camera, so that frame came
// out of the black. The hold now goes by the view the frame was drawn from. Set by the floor draw (CoopViewChoose), cleared as
// each tick starts (CoopViewTickStart, from NetplayFrameStart), so it is always the tick's own, up to the next one.
static int g_view_drawn_from = -1;
static int g_view_his_black = -1; // (the black of the other's view this tick's draw took; -1 none)
static int g_view_black = 0;      // (this tick's floor draw's screen black, the other's curtain included)

int CoopViewDrewScene() {
    return g_view_drawn_from;
}

void CoopViewTickStart() {
    g_view_drawn_from = -1;
    g_view_his_black = -1;
}

int CoopScreenBlack() {
    const CDispCtrl &curtain = DispFade__3;
    // (the curtain is drawn while it fades, in or out, even held at its end: CDispCtrl::FadeIn, FadeOut)
    const bool  fading = (curtain.mode == DispFadeMode::FadeIn || curtain.mode == DispFadeMode::FadeOut) &&
                        !(curtain.rate == 0.0f && curtain.speed == 0.0f);
    const float c = fading ? std::clamp(curtain.rate, 0.0f, 128.0f) / 128.0f : 0.0f;
    float       fade[4] = {};

    EdGetFadeColor(fade);
    const float e = std::clamp(fade[3], 0.0f, 128.0f) / 128.0f;

    return (int) std::lround(128.0f * (1.0f - (1.0f - c) * (1.0f - e)));
}

// coop-menutpose: the point the last floor draw's camera looked at (her screen's view behind her menu: x-descent's row).
static sceVu0FVECTOR g_view_drawn_ref = {};

static int g_view_chosen = 0;

int CoopViewChosenWhy() {
    return g_view_chosen;
}

void CoopViewDrawnRef(float *ref) {
    sceVu0CopyVector(ref, g_view_drawn_ref);
}

int CoopViewBlack(int *his) {
    if (his != nullptr) {
        *his = g_view_his_black;
    }
    return g_view_black;
}

// The other's view drawn as black as the other's screen showed it: a curtain over this frame's own black, last in the floor's
// draw as the game's own fades are (DrawDungeon ends with them).
static void SceneViewCurtain() {
    const int mine = CoopScreenBlack();

    g_view_black = mine;
    if (g_view_drawn_from != 1 || g_view_his_black <= mine || !CoopWorldsSceneBlackKept()) {
        return;
    }
    // (over a black already this dark: the rest of the way to the other's)
    const float open = (1.0f - g_view_his_black / 128.0f) / (1.0f - std::min(mine, 127) / 128.0f);
    const auto  alpha = (unsigned char) std::lround(128.0f * std::clamp(1.0f - open, 0.0f, 1.0f));

    for (int x = 0; x < 640; x += 64) {
        for (int y = 0; y < SCREEN_HALF_HEIGHT; y += 32) {
            CRect_i_ rect;

            rect.x = x * 16; // (12.4 fixed point, as DrawCurtain's)
            rect.y = y * 16;
            rect.width = 1024;
            rect.height = 512;
            MGFillBox(rect, 0, 0, 0, alpha);
        }
    }
    g_view_black = g_view_his_black;
}

static void CoopViewChoose() {
    // coop-cutsceneview: this copy's floor start held for the other's opening scene of it: that scene's camera, as the
    // other copy draws it, on this copy's build of the same floor (its player stands in for the other's: ScenePoseTick).
    // Apart, the link down: not only with a camera each.
    sceVu0FVECTOR eye;
    sceVu0FVECTOR ref;
    float         roll = 0.0f;
    const bool    scene = CoopWorldsSceneView(eye, ref, &roll);

    g_view_drawn_from = scene ? 1 : 0; // coop-camflash2: what this tick's floor draw took (CoopViewDrewScene)
    g_view_his_black = scene ? CoopWorldsSceneViewBlack() : -1; // (and how black the other's screen showed it)

    sceVu0FVECTOR arrival_eye;
    sceVu0FVECTOR arrival_ref;
    const bool    arrival = !scene && ArrivalWalkView(arrival_eye, arrival_ref);
    // coop-xiaogap2: this copy's player jumping a Xiao gap: the cutscene's camera rig, posed at the jump's tick (draw only)
    CCameraFollow *const gap = scene || arrival ? nullptr : SpotJumpCamera();

    // coop-menutpose (the developer's rc4 playtest: "if P1 leaves while P2 has the menu open, they get warped to the floor exit"): her stay
    // waiting under her menu keeps her body where she stood (CoopWorldsStayKeepsBody); apart there are no cameras each (the link is
    // down), so the floor drew from the game's camera, which follows the one who left (his Toan at the gate he took). Her own camera,
    // as the link left it, till her close's stay makes it the game's (PlayerCameraFromPartner). Draw only.
    const bool kept_body = CoopWorldsStayKeepsBody() && g_camera.camera != nullptr && g_camera.ready;

    // coop-chestcam (Jep's rc4 playtest: "Xiao opened a chest after P1 left the dungeon and it didn't play the cinematic camera
    // change when the opening animation was playing"): this copy's own presentation engine frames its view whether or not the
    // cameras are independent. Apart (the link down) NetplayLocalPlayer() is 0, so IndependentCameras() is off; her engine ran
    // (coop-bigchestp2) and set its camera, but the draw never got past this line to take it: her play camera through the opening.
    // Any copy whose own player opens, any player, linked or apart. DC_COOP_MUTATE=chestcam_linked_only (twin): as before.
    const bool engine = PresentEngineCamera() != nullptr && !CoopMutate("chestcam_linked_only");

    g_view_active = IndependentCameras() || scene || arrival || gap != nullptr || kept_body || engine;
    g_view_swapped = false;
    g_view_private = false;
    g_view_eye_on = false;

    if (!g_view_active) {
        return;
    }

    g_view_shown = NowCamera__3;

    if (scene) {
        if (g_view_scene == nullptr) {
            g_view_scene = new CCameraFollow(20.0f, 15.0f, 0.0f, 8.0f);
        }

        CopyCamera(g_view_scene, NowCamera__3);
        g_view_scene->follow_on = 0;
        g_view_scene->roll = roll;
        sceVu0CopyVector(g_view_scene->pos, eye);
        sceVu0CopyVector(g_view_scene->next_pos, eye);
        sceVu0CopyVector(g_view_scene->ref, ref);
        sceVu0CopyVector(g_view_scene->next_ref, ref);
        NowCamera__3 = g_view_scene;
        g_view_swapped = true;
        g_view_own_frame = true;
        CamTestView(NowCamera__3, g_view_shown);
        return;
    }

    if (kept_body) {
        NowCamera__3 = g_camera.camera;
        g_view_swapped = true;
        return;
    }

    // coop-arrivalcam: the arriver's walk from the stairs, on its own copy: the arrival's own camera (draw only).
    if (arrival) {
        if (g_view_arrival == nullptr) {
            g_view_arrival = new CCameraFollow(20.0f, 15.0f, 0.0f, 8.0f);
        }

        CopyCamera(g_view_arrival, NowCamera__3);
        g_view_arrival->follow_on = 0;
        g_view_arrival->roll = 0.0f;
        sceVu0CopyVector(g_view_arrival->pos, arrival_eye);
        sceVu0CopyVector(g_view_arrival->next_pos, arrival_eye);
        sceVu0CopyVector(g_view_arrival->ref, arrival_ref);
        sceVu0CopyVector(g_view_arrival->next_ref, arrival_ref);
        NowCamera__3 = g_view_arrival;
        g_view_swapped = true;
        // (the floor drawn round the arriver: her member on her copy, while it is loaded; his player on his)
        g_view_own_frame = NetplayLocalPlayer() != 2 || g_partner.chara == nullptr || g_partner.chara->frame == nullptr;
        CamTestView(NowCamera__3, g_view_shown);
        return;
    }

    // coop-xiaogap2: the gap's cutscene camera on the jumper's copy (the view put back after the draw, ViewEnd).
    if (gap != nullptr) {
        NowCamera__3 = gap;
        g_view_swapped = true;
        g_view_private = true;
        CamTestView(NowCamera__3, g_view_shown);
        return;
    }

    // coop-opt7: this copy's own presentation engine (its player's chest, run on this copy alone): its camera, draw only.
    if (CCameraFollow *own = PresentEngineCamera(); own != nullptr) {
        NowCamera__3 = own;
        g_view_swapped = true;
        g_view_private = true;
        CamTestView(NowCamera__3, g_view_shown);
        return;
    }

    // coop-openercam: her chest opening, on her copy: its own camera, as his presentation has his (OpeningCamera).
    if (NetplayLocalPlayer() == 2 && g_partner.visible && OpeningCamera() != nullptr) {
        NowCamera__3 = OpeningCamera();
        g_view_swapped = true;
        g_view_private = true;
        CamTestView(NowCamera__3, g_view_shown);
        return;
    }

    if (NetplayLocalPlayer() != 2 || !g_camera.ready || !g_partner.visible || CoopMutate("shared_cameras")) {
        CamTestView(NowCamera__3, g_view_shown);
        return;
    }

    // coop-p2atla: through the player's Atla presentation (its own camera, SubCamera, framing him) her copy
    // keeps her camera (Jep: "P2's camera still switches to Toan to watch the Toan Atla opening animation").
    // DC_COOP_MUTATE=atlacam shows his presentation, as before (the twin).
    const bool atla_task = gameTask == GAME_TASK_ATRA_GET_INIT || gameTask == GAME_TASK_ATRA_GET_LOOP;
    bool       atla = atla_task && !CoopMutate("atlacam");

    // coop-openercam: and through his chest, gate key and attachment presentations too (their camera is SubCamera, framing
    // him); his presentation's hidden monsters, effects and minimap and its fade stay off her screen. DC_COOP_MUTATE=openercam
    // shows his presentation on her screen, as before (the twin; atlacam still shows his Atla's).
    bool his = PresentationTask(gameTask) && !CoopMutate("openercam") && (!atla_task || atla);

    if (his) {
        OtherPresentationBegin();
    } else {
        OtherPresentationTail();
    }

    // coop-p2actions: her first-person view (her R2, retail's GAME_TASK_EYE_CAMERA for her): her own copy draws from her eye along
    // the view's angles, as EyeCamera puts retail's camera, and leaves her body out (CoopDraw). Draw only: the view's state is the
    // step's, the camera here a copy for this draw. The other copy keeps its own camera and sees her stand and turn.
    if (PartnerFirstPerson() && NowCamera__3 == &MainCamera__4) {
        sceVu0FVECTOR eye_at;
        sceVu0FVECTOR eye_ref;

        if (g_view_eye == nullptr) {
            g_view_eye = new CCameraFollow(20.0f, 15.0f, 0.0f, 8.0f);
        }

        PartnerEyeView(eye_at, eye_ref);
        CopyCamera(g_view_eye, g_camera.camera);
        g_view_eye->follow_on = 0;
        g_view_eye->roll = 0.0f;
        sceVu0CopyVector(g_view_eye->pos, eye_at);
        sceVu0CopyVector(g_view_eye->next_pos, eye_at);
        sceVu0CopyVector(g_view_eye->ref, eye_ref);
        sceVu0CopyVector(g_view_eye->next_ref, eye_ref);
        NowCamera__3 = g_view_eye;
        g_view_swapped = true;
        g_view_eye_on = true;

        // (x-p2-eye: this draw's view, once every 10 draws)
        if (static const bool traced = TestEnvOn("DC_COOP_TEST_P2_EYE"); traced) {
            static int draws = 0;

            if (draws++ % 10 == 0) {
                std::fprintf(stderr, "coop: test: p2 eye: her copy draws from her eye (%.1f %.1f %.1f) to (%.1f %.1f %.1f), her at (%.1f %.1f %.1f), her body left out of %d draws\n",
                             eye_at[0], eye_at[1], eye_at[2], eye_ref[0], eye_ref[1], eye_ref[2], g_partner.pos[0], g_partner.pos[1], g_partner.pos[2], g_view_eye_skips);
            }
        }
        CamTestView(NowCamera__3, g_view_shown);
        return;
    }

    if (NowCamera__3 == &MainCamera__4 || atla || his) {
        NowCamera__3 = g_camera.camera;
        g_view_swapped = true;
    } else if (FloorIntro()) {
        // The opening's own camera, moved by where the partner stands from the player.
        if (g_view_moved == nullptr) {
            g_view_moved = new CCameraFollow(20.0f, 15.0f, 0.0f, 8.0f);
        }

        CopyCamera(g_view_moved, NowCamera__3);

        // From where the player is drawn: under the cave's scene node CharaMain.pos is (0 0 0).
        sceVu0FVECTOR player;
        float         axis_x[2];
        float         axis_z[2];

        PlayerDrawnPlace(player, axis_x, axis_z);

        for (int i = 0; i < 3; i++) {
            float shift = g_partner.pos[i] - player[i];

            g_view_moved->pos[i] += shift;
            g_view_moved->ref[i] += shift;
            g_view_moved->next_pos[i] += shift;
            g_view_moved->next_ref[i] += shift;
            g_view_moved->follow[i] += shift;
        }

        NowCamera__3 = g_view_moved;
        g_view_swapped = true;
    }

    CamTestView(NowCamera__3, g_view_shown);
}

// Test hook (DC_COOP_TEST_OPENER_VIEW=1, x-opener-view): each draw on a floor, what it drew from and what a presentation
// left on the screen, by the press clock. Read only.
static void LogOpenerView() {
    static const bool log = TestEnv("DC_COOP_TEST_OPENER_VIEW") != nullptr;

    if (!log || NowDngMap == nullptr) {
        return;
    }

    const char *camera = NowCamera__3 == &MainCamera__4 ? "own"
                         : NowCamera__3 == g_camera.camera && g_camera.camera != nullptr ? "own"
                         : NowCamera__3 == &SubCamera ? "presentation"
                         : NowCamera__3 == g_view_scene && g_view_scene != nullptr ? "scene"
                         : NowCamera__3 == g_view_moved && g_view_moved != nullptr ? "opening-moved"
                         : NowCamera__3 == OpeningCamera() && OpeningCamera() != nullptr ? "opening"
                                                                                       : "other";
    const CDispCtrl &fade = DispFade__3;

    // coop-atlaopen: and the projection drawn with (her Atla's zoom keys), the scale her chest's item was last drawn at,
    // whether she holds it up, and where her body stands and turns.
    float turn[4] = {};
    // coop-openpos: and where her frame stands and where its skeleton's root is drawn (her opening's motion carries the body
    // out from the frame on the box), and his frame and root, for retail's own presentation beside hers.
    float frame_at[2] = {};
    float body_at[2] = {};
    float his_at[2] = {CharaFrame->position[0], CharaFrame->position[2]};
    float his_body[2] = {his_at[0], his_at[1]};
    auto  root = [](CFrame *frame, float *at) {
        if (frame != nullptr && frame->child != nullptr) {
            sceVu0FMATRIX matrix;

            frame->child->GetLWMatrix(matrix);
            at[0] = matrix[3][0];
            at[1] = matrix[3][2];
        }
    };

    if (g_partner.chara != nullptr && g_partner.chara->frame != nullptr) {
        g_partner.chara->frame->GetRotation(turn);
        frame_at[0] = body_at[0] = g_partner.chara->frame->position[0];
        frame_at[1] = body_at[1] = g_partner.chara->frame->position[2];
        root(g_partner.chara->frame, body_at);
    }

    root(CharaFrame, his_body);

    std::fprintf(stderr, "coop-debug: opener view clock %lld p%d task 0x%x opening %d camera %s monsters %s fade %d proj %.1f item %.2f "
                 "hold %d her (%.1f %.1f) turn %.2f light %.0f frame (%.1f %.1f) body (%.1f %.1f) his (%.1f %.1f) his body (%.1f %.1f) "
                 "box %.1f curtain %.0f glow %.1f opener_pres %d\n",
                 (long long) NetplayTestPressClock(), NetplayLocalPlayer() == 2 ? 2 : 1, (unsigned) gameTask, PartnerPresenting() ? 1 : 0,
                 camera, CMonUnitHyde == 0 && CEffectHyde == 0 ? "shown" : "hidden", fade.mode == DispFadeMode::FadeOut || fade.mode == DispFadeMode::FadeIn ? (int) fade.rate : 0,
                 MGGetProjection(), PartnerItemDrawn(), g_partner.open_hold ? 1 : 0, g_partner.pos[0], g_partner.pos[2], turn[1],
                 PartnerLightDrawn(), frame_at[0], frame_at[1], body_at[0], body_at[1], his_at[0], his_at[1], his_body[0], his_body[1], PartnerBoxDrawn(),
                 0.0f, // (coop-atlaswap's curtain on her copy, gone in opt7 step 4: retail's fade there is the fade field)
                 PartnerGlowDrawn(), PlayerPresenting(1) ? 1 : 0); // (coop-opt7: his own presentation, the world's bit)

    // coop-atlacam: the drawn camera itself (eye, what it looks at), the game's player's frame, drawn body (its skeleton's root)
    // and turn, and the partner's drawn body, so a test reads a presentation's framing against its opener, start to end and
    // through the hand-back. Read only.
    if (NowCamera__3 != nullptr) {
        float his_turn[4] = {};

        CharaFrame->GetRotation(his_turn);
        std::fprintf(stderr, "coop-debug: opener camera clock %lld p%d task 0x%x camera %s eye (%.2f %.2f %.2f) at (%.2f %.2f %.2f) proj %.1f "
                     "his (%.2f %.2f %.2f) his body (%.2f %.2f) her body (%.2f %.2f) turn %.3f member %d engine %d\n",
                     (long long) NetplayTestPressClock(), NetplayLocalPlayer() == 2 ? 2 : 1, (unsigned) gameTask, camera, NowCamera__3->pos[0],
                     NowCamera__3->pos[1], NowCamera__3->pos[2], NowCamera__3->ref[0], NowCamera__3->ref[1], NowCamera__3->ref[2], MGGetProjection(),
                     CharaFrame->position[0], CharaFrame->position[1], CharaFrame->position[2], his_body[0], his_body[1], body_at[0], body_at[1], his_turn[1],
                     UserStatus != nullptr ? (int) UserStatus->cur_chara : -1, PresentEngineCamera() != nullptr ? 1 : 0);
    }
}

// coop-camflash (Jep's 11-16 playtest: a chest's opening "plays a frame of a different camera angle or something before it starts
// the animation"). The renderer draws each display frame between the last tick's view and this one's, and takes a view as a cut
// only when it moved more than 200 or turned more than 45 degrees (mglib.cpp CheckCameraCut). A presentation that takes the view
// onto its own camera is a cut the game makes, often under both (P1's chest on dun1-f1: SubCamera 51 away and 7.7 degrees round
// from his play camera, x-camflash), so the display frames of its first tick drew cameras between the two. The game knows: a
// tick drawn from another camera than the last tick's is a cut, and says so (MGPortCutCameraInterpolation). A switch between two
// cameras that stand alike costs only that tick's in-between frames. DC_COOP_MUTATE=camflash_blend (twin): left to the
// renderer's distance test, as before ("coop-camflash: ... (camflash_blend)").
static void CameraSwitchCut(const CCameraFollow *drawn) {
    static const CCameraFollow *last = nullptr;

    if (last != nullptr && drawn != last) {
        if (CoopMutate("camflash_blend")) {
            if (drawn == PresentEngineCamera()) { // (the twin's stop line: its chest's first frame)
                std::fprintf(stderr, "coop-camflash: the switch to the presentation's camera left to the renderer's distance test at link tick %lld (camflash_blend)\n",
                             (long long) NetplayLinkTick());
            }
        } else {
            MGPortCutCameraInterpolation();
        }
    }
    last = drawn;
}

// coop-tripwires3: the opener's engine's own screen state through this copy's floor draw (coop_opener.cpp PresentScreenIn).
static std::optional<PresentScreenIn> g_view_screen;

void CoopViewBegin() {
    g_view_screen.emplace(); // coop-tripwires3
    DrawRngEnter(); // coop-drawrng: every copy's floor draw, linked or not, cameras or not
    // coop-entrance8: the walk-in's door drawn later than the game has it (g_door_draw), put back in CoopViewEnd: draw only
    g_door_kept_n = 0;
    if (g_door_draw < 1e8f && (FloorIntro() || EntranceWalkOn())) {
        CFrame *pieces[2] = {nullptr, nullptr};
        int     axis = 1;
        const int n = EntranceDoors(pieces, &axis);

        for (int i = 0; i < n; i++) {
            for (int j = 0; j < 3; j++) {
                g_door_kept[i][j] = pieces[i]->position[j];
            }
            float at[3] = {pieces[i]->position[0], pieces[i]->position[1], pieces[i]->position[2]};

            at[axis] = CoopMutate("door_halves_one_side") ? g_door_draw : g_door_draw * g_door_side[i];
            pieces[i]->SetPosition(at[0], at[1], at[2]);
        }
        g_door_kept_n = n;
    }
    // (each draw of the temple's opening: the halves as drawn, for the warp check; DC_COOP_TEST_DOOR_DRAW=1 says each)
    if (mode == GAME_MODE_DUNGEON && selectMapNo == 3 && (FloorIntro() || EntranceWalkOn())) {
        CFrame *pieces[2] = {nullptr, nullptr};
        int     axis = 1;

        if (EntranceDoors(pieces, &axis) == 2) {
            DoorDrawn &d = g_door_drawn;

            for (int i = 0; i < 2; i++) {
                const float v = pieces[i]->position[axis];

                if (d.draws > 0) {
                    d.most_step[i] = std::max(d.most_step[i], std::fabs(v - d.last[i]));
                    d.side_changes[i] += (v > 0.01f && d.last[i] < -0.01f) || (v < -0.01f && d.last[i] > 0.01f) ? 1 : 0;
                    if (std::fabs(v) < std::fabs(d.last[i]) - 0.001f) {
                        d.closing[i] = true;
                    } else if (d.closing[i] && std::fabs(v) > std::fabs(d.last[i]) + 0.001f) {
                        d.reopened[i]++;
                    }
                }
                d.last[i] = v;
            }
            d.draws++;
            if (TestEnv("DC_COOP_TEST_DOOR_DRAW") != nullptr) {
                std::fprintf(stderr, "coop: door draw %d: override %.1f script %.1f drawn %.2f/%.2f\n", d.draws, g_door_draw < 1e8f ? g_door_draw : -1.0f,
                             g_door_script, pieces[0]->position[axis], pieces[1]->position[axis]);
            }
        }
    }
    PartnerBoxDrawBegin(); // coop-chestfix
    CoopViewChoose();
    sceVu0CopyVector(g_view_drawn_ref, NowCamera__3->ref); // coop-menutpose: what this draw looked at (CoopViewDrawnRef)
    g_view_chosen = (g_view_active ? 1 : 0) + (g_view_swapped ? 2 : 0) + (NowCamera__3 == g_camera.camera ? 4 : 0);
    CameraSwitchCut(NowCamera__3); // coop-camflash: a tick drawn from another camera is a cut
    ArrivalViewLog(NowCamera__3); // coop-arrivalcam: test hook
    ForestAfterDraw(NowCamera__3); // coop-entrance9: the camera after the forest's opening

    // coop-drawtwice (x-draw-twice): DC_COOP_TEST_DRAW_ROLL=1: the floor's draw rolls the generator once, as a draw-time
    // rand caller does (CWater's culling, the town's smoke), so the self-test has a roll to catch when the draw's state is
    // not put back (its twin, draw_rng_shared).
    if (static const bool roll = TestEnvOn("DC_COOP_TEST_DRAW_ROLL"); roll && g_view_active) {
        static bool said = false;

        rand();
        if (!said) {
            said = true;
            std::fprintf(stderr, "coop-drawtwice: test: the floor's draw rolls the generator\n");
        }
    }
    if (g_view_active) {
        const bool two = NetplayLocalPlayer() == 2;

        KeepPlayProjection(two ? g_other_pres.on : PartnerPresenting(),
                           (two ? OpeningCamera() != nullptr : PresentationTask(gameTask) || PresentEngineCamera() != nullptr) || SpotJumpCameraOn()); // (coop-opt7: his engine's; coop-xiaogap2: the gap's camera, its own projection put back in ViewEnd)
    }
    if (g_view_active || PresentationTask(gameTask)) {
        LogOpenerView();
    }
}

static void ViewEnd();
static void DrawTwiceCheck();

void CoopViewEnd() {
    if (g_door_kept_n > 0) { // (coop-entrance8: the game's own door back)
        CFrame *pieces[2] = {nullptr, nullptr};
        int     axis = 1;

        if (EntranceDoors(pieces, &axis) == g_door_kept_n) {
            for (int i = 0; i < g_door_kept_n; i++) {
                pieces[i]->SetPosition(g_door_kept[i][0], g_door_kept[i][1], g_door_kept[i][2]);
            }
        }
        g_door_kept_n = 0;
    }
    PartnerBoxDrawEnd(); // coop-chestfix
    SceneViewCurtain();  // coop-camflash2: the other's view no lighter than his screen showed it
    ViewEnd();
    DrawRngLeave();
    g_view_screen.reset(); // coop-tripwires3
    DrawTwiceCheck();
    NetplayUnitTick(); // coop-pilot: DC_UNIT's cases, after a whole floor frame
}

static void ViewEnd() {
    if (!g_view_active) {
        return;
    }

    OtherPresentationEnd();
    PutPresentationProjection(); // coop-hischest
    SpotJumpViewEnd();           // coop-xiaogap2: the play projection back after the gap camera's draw

    if (g_view_swapped) {
        NowCamera__3 = g_view_shown;

        // The view and clip matrices the draw leaves are read by the next tick (MGClipBox: CWater's
        // and the town ground's culling, which draw rand): set the shown camera's, as the other copy
        // drew it, keeping this copy's view for the display frames' camera-cut test.
        sceVu0FMATRIX camera;
        sceVu0FMATRIX unit;
        sceVu0FMATRIX view;
        sceVu0FVECTOR eye;

        if (!CoopMutate("no_view_restore")) {
            MGPortHoldTickView();
            g_view_shown->GetPos(eye);
            g_view_shown->GetCameraMatrix(camera);
            sceVu0UnitMatrix(unit);
            sceVu0MulMatrix(view, unit, camera);
            MGPortSetViewMatrixNoCut(view, eye);
        }

        if (BtAllDrawFlag != 0 && NowDngMap != nullptr && NowDngMap->map_type == 1 && !CoopMutate("draw_leak")) {
            CellDistances(g_view_shown);
        }
    }

    g_view_active = false;
    g_view_swapped = false;
    g_view_own_frame = false;
    g_view_private = false;
}

// coop-gapmark: this floor draw is its copy's player's own scene: the Xiao gap's cutscene camera (SpotJumpCamera), its own
// presentation engine's (a chest or Atla it opens), or her gate key's (OpeningCamera). Set by CoopViewChoose, off at ViewEnd.
namespace coop {
bool ViewPrivateScene() {
    return g_view_active && g_view_private;
}
} // namespace coop

// coop-drawtwice (root fix 5's self-test, Jep 2026-10-30): drawing must never change what the step reads (the random
// generator, the view, a camera, a presentation's state), or two linked copies whose screens differ part (the LAN
// playtest's B3 desyncs: only the generator, only her camera). DC_COOP_TEST_DRAW_TWICE=N: on every Nth floor frame the
// floor's whole draw runs once more right after, and the lockstep checksum before and after that second draw must be
// equal; a change is logged with the checksum's parts (DC_SNAPSHOT_TRIP_PARTS) to name what the draw touched.
namespace dc::game {
void DunMainDraw();
} // namespace dc::game

static void DrawTwiceCheck() {
    static const int every = TestEnvInt("DC_COOP_TEST_DRAW_TWICE", 0);
    static bool      inside = false;
    static long      frames = 0;
    static long      checked = 0;
    static long      changed = 0;

    if (every <= 0 || inside || NetplayLocalPlayer() == 0 || ++frames % every != 0) {
        return;
    }

    inside = true;
    const std::uint32_t before = NetplayTestChecksum();
    DunMainDraw();
    const std::uint32_t after = NetplayTestChecksum();
    inside = false;
    checked++;

    if (before != after) {
        changed++;
        std::fprintf(stderr, "coop-drawtwice: the floor's draw changed the checksum %08x -> %08x (frame %ld)\n", before, after, frames);
    }

    if (checked % 60 == 1) {
        std::fprintf(stderr, "coop-drawtwice: %ld floor draws checked twice, %ld changed the checksum\n", checked, changed);
    }
}

CFrameVu1 *CoopViewFrame() {
    return g_view_swapped && !g_view_own_frame ? (CFrameVu1 *) g_partner.chara->frame : CharaFrame;
}

// coop-p2actions: this copy's draw is from her first-person view: her body is left out of it (retail's player_visible 0).
bool CoopPartnerEyeDrawn() {
    g_view_eye_skips += g_view_eye_on ? 1 : 0;
    return g_view_eye_on;
}

bool CoopPlayerShown() {
    // coop-entrance2: Moon Sea's pad: the lead is not drawn before its own beam (draw only).
    if (EntranceLeadHidden() || (g_swap_stand_in.on && FloorIntro())) { // (option C: his stand-in drawn in his place on his lagged way)
        return false;
    }
    if (BtActStatus.player_visible != 0) {
        return true;
    }

    // coop-cutsceneview: the other's player in the other's scene, which this copy's stands in for.
    if (CoopWorldsSceneShowsOther()) {
        return true;
    }

    // His first-person look (InitEyeCamera hides him on both copies) on her copy, which shows her camera.
    return CoopEyePlay() && IndependentCameras() && NetplayLocalPlayer() == 2 && g_partner.visible && !CoopWorldsApart() &&
           !CoopMutate("fp_hidden");
}

static int g_player_screen[2] = {-1, -1};

// coop-desync1116 (Jep's 0.1.6 playtest 11-16, OUT OF SYNC at tick 7560: the random generator alone apart, every other part alike):
// the player's draw (Draw_MainUnit: CCharacter::Draw's place on the frame, then MGDraw's CFrameVu1::DrawVu1) leaves every frame of
// his body and his weapon with its world matrix cached (GetLWMatrix), and the next tick reads those caches: Xiao's shot starts at
// her slingshot's "eff30" (BattleActionPlay_Jinn, GetWorldPosition), Toan's and the others' hits at their weapons' frames. Her copy
// left his whole draw out past the floor's draw distance (coop-pt1109b), so there his caches were never made: her copy read his
// slingshot where the tick put it (recomputed, this tick's pose) and his copy where his last draw had (measured: eff30 cached at
// (413 7 138) on his copy, (803.6 7.2 1001.7), his fixture's start, then none, on hers; 1148 of 1152 ticks apart). His shot left
// from two places, met the monster 4 ticks apart (his copy's hit 3 drawn from link tick 7511, hers from 7515), and the hit's rolls
// (CheckDmg, the element burst's sparks) came on different ticks among the monsters': the generator parted, nothing else.
// So a draw left out by distance still poses him: the draw's frame writes, nothing drawn, as coop-farcull2 keeps a hidden
// monster's (monstorunit.cpp). DC_COOP_MUTATE=farplayer_unposed (twin): his draw left out whole, as before.
namespace {

// CFrameVu1::DrawVu1's own walk, its matrix part alone: each frame's world matrix made and cached (GetLWMatrix), a frame that
// draws none of its own (draw_on 2 without 1) stops the walk, and a child marked 4 is passed over. A plain CFrame draws nothing
// (its DrawVu1 returns 0), so it makes none. A billboard's turn to the view is this copy's draw's own, not posed here.
void PoseFrame(CFrame *frame) {
    if (frame == nullptr || dynamic_cast<CFrameVu1 *>(frame) == nullptr) {
        return;
    }

    sceVu0FMATRIX matrix;

    frame->GetLWMatrix(matrix);
    if (!(frame->attr.draw_on & 1) && (frame->attr.draw_on & 2)) {
        return;
    }
    if (!(frame->attr.draw_on & 2)) {
        for (CFrame *child = frame->child; child != nullptr; child = child->brother) {
            if (child->attr.draw_on & 4) {
                continue;
            }
            PoseFrame(child);
        }
    }
}

// CCharacter::Draw's frame writes: its place, turn and scale on the frame, then the draw's matrices.
void PoseCharacter(CCharacter *character) {
    if (character == nullptr || character->frame == nullptr) {
        return;
    }
    character->frame->SetPosition(character->pos[0], character->pos[1], character->pos[2]);
    character->frame->SetRotation(character->rotation.x, character->rotation.y, character->rotation.z);
    character->frame->SetScale(character->scale[0], character->scale[1], character->scale[2]);
    PoseFrame(character->frame);
}

// Draw_MainUnit's frame writes, nothing drawn: his body, then his weapon while it shows (its own rules, as it draws them).
void PoseMainUnit() {
    if (BtActStatus.player_visible == 0 && EdEventInfo.player_draw != 0 && EdEventInfo.player_shadow_draw != 0) {
        return;
    }
    PoseCharacter(&CharaMain);
    if (NowWeapon != nullptr && NowWeapon->frame != nullptr && BtActStatus.weapon_visible != 0) {
        PoseCharacter(NowWeapon);
    }
}

} // namespace

void CoopDrawPlayer() {
    // coop-pt1109b: her copy draws him only inside the distance retail draws a monster at (OtherBeyondClip); coop-desync1116: past
    // it he is still posed (PoseMainUnit), as his draw would have.
    const bool far = ActorLocalIndex() == 1 && coop::OtherBeyondClip();

    if (far && CoopMutate("farplayer_unposed")) {
        static bool said = false;

        if (!said) {
            said = true;
            std::fprintf(stderr, "coop-farplayer: the other player left out whole, unposed (farplayer_unposed)\n");
        }
        return;
    }

    // Draw-only: the shared flag is put back before anything else reads it.
    const auto visible = BtActStatus.player_visible;
    float      waist[4];
    int        screen[4];
    if (!far) { // (his screen spot: a drawn player's alone)
        {
            // (coop-entrance2, DCO-45: where he is drawn, not CharaMain.pos, (0 0 0) under a scene's node)
            float axis_x[2];
            float axis_z[2];

            PlayerDrawnPlace(waist, axis_x, axis_z);
            waist[1] += 6.0f;
            waist[3] = 1.0f;
        }

        g_player_screen[0] = g_player_screen[1] = -1;
        if (MGRotTransPers(screen, waist, 0) != 0) {
            g_player_screen[0] = (screen[0] - 0x6C00) >> 4;
            g_player_screen[1] = ((screen[1] - GS_Y_OFFSET) >> 4) * 2;
        }
    }

    // coop-soakfix1 (test_both_eye_menu: OUT OF SYNC at tick 360, his height apart in its last bits): CCharacter::Draw puts the
    // character's place, turn and scale on its frame (frame->SetPosition ...), and the next tick's MoveChara starts from the
    // frame's (pos = CharaFrame->position). In his first-person look only her copy draws him, so her copy alone wrote his frame:
    // harmless while the two agree, but after his lockstep menu beside hers his frame held a step of the world's gravity his
    // place did not, and her draw put it back on her copy alone as his look came back. The frame's transform is put back too.
    // DC_COOP_MUTATE=fp_draw_moves (twin): her draw of him writes his frame, as before.
    struct Transform {
        sceVu0FMATRIX world, inverse, local;
        sceVu0FVECTOR scale, position, rotation;
        int           world_valid, no_rotation, rot_type, srt;
    } kept{};
    CFrame *const frame = CoopMutate("fp_draw_moves") ? nullptr : CharaMain.frame;

    if (frame != nullptr) {
        std::memcpy(kept.world, frame->world, sizeof kept.world);
        std::memcpy(kept.inverse, frame->inverse, sizeof kept.inverse);
        std::memcpy(kept.local, frame->local, sizeof kept.local);
        sceVu0CopyVector(kept.scale, frame->scale);
        sceVu0CopyVector(kept.position, frame->position);
        sceVu0CopyVector(kept.rotation, frame->rotation);
        kept.world_valid = frame->world_valid;
        kept.no_rotation = frame->no_rotation;
        kept.rot_type = frame->rot_type;
        kept.srt = frame->srt;
    }

    BtActStatus.player_visible = 1;
    if (far) {
        PoseMainUnit(); // coop-desync1116: posed as drawn, nothing drawn
    } else {
        Draw_MainUnit();
    }
    BtActStatus.player_visible = visible;

    if (frame != nullptr) {
        std::memcpy(frame->world, kept.world, sizeof kept.world);
        std::memcpy(frame->inverse, kept.inverse, sizeof kept.inverse);
        std::memcpy(frame->local, kept.local, sizeof kept.local);
        sceVu0CopyVector(frame->scale, kept.scale);
        sceVu0CopyVector(frame->position, kept.position);
        sceVu0CopyVector(frame->rotation, kept.rotation);
        frame->world_valid = kept.world_valid;
        frame->no_rotation = kept.no_rotation;
        frame->rot_type = kept.rot_type;
        frame->srt = kept.srt;
    }
}

static int g_screen_spots[8] = {-1, -1, -1, -1, -1, -1, -1, -1};

void CoopScreenSpots(int out[8]) {
    std::memcpy(out, g_screen_spots, sizeof g_screen_spots);
}

void CoopPlayerDrawReport(bool drawn) {
    static int frames = 0;
    static int shown = 0;

    if (!CoopDebug()) {
        return;
    }

    // coop-joinvisuals: this frame's view (the draw's) put the two where (CoopScreenSpots). The player where it is drawn (a
    // scene's node carries it in the cave's entrance, CharaMain.pos then (0 0 0)).
    float player_at[4];
    float axis_x[2];
    float axis_z[2];

    coop::PlayerDrawnPlace(player_at, axis_x, axis_z);

    const float *const at[2] = {player_at, g_partner.pos};

    for (int who = 0; who < 2; who++) {
        for (int part = 0; part < 2; part++) {
            float point[4] = {at[who][0], at[who][1] + (part == 0 ? 0.0f : 16.0f), at[who][2], 1.0f};
            int   screen[4];
            int  *spot = &g_screen_spots[who * 4 + part * 2];

            spot[0] = spot[1] = -1;
            if (MGRotTransPers(screen, point, 0) != 0) {
                spot[0] = (screen[0] - 0x6C00) >> 4;
                spot[1] = ((screen[1] - GS_Y_OFFSET) >> 4) * 2;
            }
        }
    }

    shown += drawn ? 1 : 0;
    if (++frames >= 60) {
        // Where on screen (640x480) this frame drew him, for the test's frame check.
        std::fprintf(stderr, "coop-debug: floor player drawn %d/60 frames, first person %d, copy of p%d, at (%d %d)\n", shown,
                     CoopEyePlay() ? 1 : 0, NetplayLocalPlayer() == 2 ? 2 : 1, drawn ? g_player_screen[0] : -1,
                     drawn ? g_player_screen[1] : -1);
        frames = shown = 0;
    }
}

// coop-choosing (a desync's hunt, DC_NETPLAY_TICK_TRACE): the partner camera's state each traced tick.
void CoopPartnerCameraTrace(long long tick) {
    if (g_camera.camera == nullptr) {
        return;
    }
    float pos[4];

    g_camera.camera->GetPos(pos);
    std::printf("netplay-tick %lld: camera2 ready %d intro %d idle %d last (%a %a) pos (%a %a %a) follow (%a %a) partner (%a %a)\n", tick, (int) g_camera.ready,
                (int) g_camera.intro, g_camera.idle, g_camera.last[0], g_camera.last[2], pos[0], pos[1], pos[2], g_camera.camera->follow[0],
                g_camera.camera->follow[2], g_partner.pos[0], g_partner.pos[2]);
}

bool CoopPartnerCamera(float *angle, float *pos) {
    if (!IndependentCameras() || !g_camera.ready) {
        return false;
    }

    *angle = g_camera.camera->angle;
    g_camera.camera->GetPos(pos);
    return true;
}
