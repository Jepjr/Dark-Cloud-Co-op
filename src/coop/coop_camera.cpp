#include "coop/game_ns.hpp"
#include "coop/coop_opener.hpp" // coop-opt7
#include "coop/coop.hpp"
#include "coop/coop_perf.hpp"
#include "coop/coop_writeguard.hpp"
#include "coop/netplay.hpp"

#include <SDL3/SDL.h>
#include <libvu0.h>

#include <algorithm>
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
#include "clothread.hpp"
#include "collision.hpp"
#include "collisiondata.hpp"
#include "dataalloc.hpp"
#include "dataread.hpp"
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
#include "gamepad.hpp"
#include "gameutil.hpp"
#include "hitvalue.hpp"
#include "itemdata.hpp"
#include "menu_draw.hpp"
#include "mglib.hpp"
#include "monstorunit.hpp"
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
void         PortRandSetState(unsigned int state);
} // namespace dc::game

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
CCameraFollow *g_view_arrival = nullptr; // coop-arrivalcam: the arrival's own camera, for the arriver's walk (ArrivalWalkView)
bool           g_view_own_frame = false; // the floor drawn round this copy's player all the same

// Whether the floor's opening ran last tick (StandBesideArrival).
bool g_opening = false;

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

namespace coop {

bool IndependentCamerasSetting(const dc::platform::Config &config) {
    return config.coop_independent_cameras.value_or(!CoopMutate("cameras_default_off"));
}

bool IndependentCameras() {
    return Enabled() && IndependentCamerasSetting(dc::platform::ConfigGet()) && NetplayLocalPlayer() != 0;
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

void StandBesideArrival() {
    bool          was = g_opening;
    sceVu0FVECTOR rotation;

    g_opening = FloorIntro();
    bool opening = g_opening;

    if (!opening && !was) {
        return;
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
    constexpr int   kTrailTicks = 20;
    constexpr int   kTrailSize = 32;
    constexpr float kTrailSide = 7.0f;
    constexpr float kTrailStep = 0.67f; // the cave's node, units a tick
    struct TrailSample {
        float place[3];
        float axis_x[2];
        float axis_z[2];
        int   motion;
        int   flags;
    };
    static TrailSample trail[kTrailSize];
    static int         trail_n = 0;
    static bool        trailing = false;

    if (opening && !was) {
        trailing = false;
        trail_n = 0;
    }

    if (carried && !was_carried && !CoopMutate("opening_trail")) {
        trailing = true;
    }

    was_carried = carried;

    const TrailSample *follow = nullptr;
    float              back = 0.0f;

    if (trailing) {
        TrailSample &now = trail[trail_n % kTrailSize];

        for (int i = 0; i < 3; i++) {
            now.place[i] = player[i];
        }

        for (int i = 0; i < 2; i++) {
            now.axis_x[i] = axis_x[i];
            now.axis_z[i] = axis_z[i];
        }

        now.motion = CharaMain.motion_no;
        now.flags = CharaMain.motion_flags;
        trail_n++;

        if (trail_n > kTrailTicks) {
            follow = &trail[(trail_n - 1 - kTrailTicks) % kTrailSize];
        } else {
            follow = &trail[0];
            back = kTrailStep * (kTrailTicks - (trail_n - 1));
        }
    }

    if (follow != nullptr) {
        g_partner.pos[0] = follow->place[0] - kTrailSide * follow->axis_x[0] - back * follow->axis_z[0];
        g_partner.pos[1] = follow->place[1];
        g_partner.pos[2] = follow->place[2] - kTrailSide * follow->axis_x[1] - back * follow->axis_z[1];
    } else {
        for (int i = pick ? 0 : held; i < 4; i++) {
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

    const float *facing = follow != nullptr ? follow->axis_z : axis_z;

    g_partner.pos[3] = 1.0f;
    g_partner.velocity[0] = g_partner.velocity[1] = g_partner.velocity[2] = 0.0f;
    g_partner.chara->frame->SetPosition(g_partner.pos);
    g_partner.chara->frame->SetRotation(0.0f, atan2f(facing[0], facing[1]), 0.0f);

    // The partner arrives as the player does: the motions the opening plays on the player (100, its
    // arrival, held on its last frame), from their first frame, where the partner's model has them.
    if (opening && !was && !CoopMutate("no_arrival")) {
        LoadArrival();
    }

    // Following his way, what he played when he was where she is.
    int index = -1;
    int motion = follow != nullptr ? follow->motion : CharaMain.motion_no;
    int flags = follow != nullptr ? follow->flags : CharaMain.motion_flags;

    if (opening && motion != g_partner.motion && g_partner.chara->GetMotionParam(motion, &index, nullptr, nullptr, nullptr) != nullptr && index >= 0) {
        PlayMotion(motion, flags | 4);
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

float PartnerCameraAngleH() {
    if (!IndependentCameras() || !g_camera.ready || CoopMutate("shared_cameras")) {
        return NowCamera__3->GetAngleH();
    }

    return g_camera.camera->GetAngleH();
}

void PartnerCameraTick() {
    g_camera.idle++;
}

void PartnerCameraFromPlayer() {
    if (g_camera.camera == nullptr) {
        g_camera.camera = new CCameraFollow(20.0f, 15.0f, 0.0f, 8.0f);
    }
    CopyCamera(g_camera.camera, &MainCamera__4);
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
        InputPadState        linked;
        const InputPadState &pad = NetplayPad(1, linked) ? linked : InputGetPad(1);
        float                rx = Stick(pad.right_x);
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
};

constexpr int kArrivalSamples = 900; // the arrival is some 350 ticks (Divine Beast Cave B1); a longer one is cut here

struct ArrivalTrail {
    int           key = -1000;       // the floor (selectMapNo * 256 + floor) of the arrival recorded, -1000 none
    int           recording = -1000; // the floor of the arrival under way, -1000 none
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
    bool  door = false; // coop-joinatla: the stairwell door replayed, and its height before the walk (put back at its end)
    float door_before = 0.0f;
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

bool SampleMoves(int tick) {
    const ArrivalTrail &t = g_arrival_trail;

    if (tick + 1 >= t.count) {
        return false;
    }
    const ArrivalSample &a = t.samples[tick];
    const ArrivalSample &b = t.samples[tick + 1];

    return std::hypot(b.place[0] - a.place[0], b.place[2] - a.place[2]) > 0.05f;
}

// Where the walk puts the arriver at a sample (the twin arrival_path_side: 12 units off the way, to the record's right).
void SamplePlace(const ArrivalSample &s, float *place) {
    place[0] = s.place[0];
    place[1] = s.place[1];
    place[2] = s.place[2];
    place[3] = 1.0f;

    if (NetplayMutated("arrival_path_side")) {
        place[0] += 12.0f * cosf(s.yaw);
        place[2] -= 12.0f * sinf(s.yaw);
    }
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

void ArrivalTrailTick() {
    ArrivalTrail &t = g_arrival_trail;
    const bool    intro = BtEventMode != 0 && BtEventInfo.running_script_no == 100 && mode == GAME_MODE_DUNGEON && UserStatus != nullptr &&
                       CharaFrame != nullptr && CharaMain.frame != nullptr && g_arrival_walk.player == 0;

    if (intro) {
        // (a new floor under the same script, as from the entrance's menu: the record starts again there)
        if (t.recording != FloorKey()) {
            t.recording = FloorKey();
            t.count = 0;
            t.key = -1000;
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
            if (const CFrame *door = StairDoor()) {
                s.door = door->position[1];
            } else {
                s.door = 0.0f;
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

void ArrivalWalkStart(int player) {
    const ArrivalTrail &t = g_arrival_trail;

    if (NetplayMutated("arrival_skipped")) {
        std::fprintf(stderr, "coop: arrival walk: none for player %d (arrival_skipped)\n", player);
        return;
    }
    if (mode != GAME_MODE_DUNGEON || t.key != FloorKey() || t.count <= 0 || (player != 1 && player != 2)) {
        std::fprintf(stderr, "coop: arrival walk: none for player %d (no arrival of this floor recorded: here %d, recorded %d)\n", player, FloorKey(), t.key);
        return;
    }
    // coop-joinlate (Jep, 2026-11-02, on the walk coop-arrivalonce hid: "i wanted it not to happen, xiao already walked down the
    // stairs, she shouldn't do it a second time"): the arriver has had its arrival on its own copy (the game's floor entrance), so
    // at the lockstep it stands where that arrival ended, on both copies, and plays on from there: no walk again (it held her
    // screen black some 7 s more at the game's pace). DC_NETPLAY_MUTATE=arrival_walk_twice (twin): the walk again, as before.
    if (!NetplayMutated("arrival_walk_twice")) {
        sceVu0FVECTOR end;

        ArrivalStand(player, end);
        std::fprintf(stderr, "coop: arrival walk: none again for player %d (its own copy had its arrival): it stands at (%.0f %.0f %.0f)\n", player,
                     end[0], end[1], end[2]);
        return;
    }
    g_arrival_walk.player = player;
    g_arrival_walk.tick = 0;
    if (player == 2 && g_partner.chara != nullptr) {
        LoadArrival(); // her own stairs motion, where her member has one
    }
    // coop-joinatla: the stairwell door as the record has it, tick by tick (on both copies: the door is game state), and back to
    // where it stood at the end. DC_NETPLAY_MUTATE=arrival_door_shut (twin): left as it stands.
    CFrame *door = StairDoor();

    g_arrival_walk.door = door != nullptr && !NetplayMutated("arrival_door_shut");
    if (g_arrival_walk.door) {
        float lowest = t.samples[0].door;

        for (int i = 0; i < t.count; i++) {
            lowest = std::min(lowest, t.samples[i].door);
        }
        g_arrival_walk.door_before = door->position[1];
        door->SetPosition(door->position[0], t.samples[0].door, door->position[2]);
        std::fprintf(stderr, "coop: arrival walk: the stairwell door as recorded, from %.1f (at %.1f till now) down to %.1f\n", t.samples[0].door,
                     g_arrival_walk.door_before, lowest);
    }
    std::fprintf(stderr, "coop: arrival walk: player %d arrives from the stairs on both copies, %d ticks, from (%.0f %.0f %.0f)\n", player, t.count,
                 t.samples[0].place[0], t.samples[0].place[1], t.samples[0].place[2]);
}

int ArrivalWalkPlayer() {
    return g_arrival_walk.player;
}

// coop-latefall (Jep's 2026-10-27 run: P1 joined B1 where P2 already played; Xiao fell through the floor, then the copies parted):
// every floor's arrival, script 100, loads its stairwell as an event actor (B1: dun/d01/stair_mos/in01.chr into NPC 1), so
// coop-b2join's "an actor loaded" took each plain walk-in for a scene like B2's cat (dun/d01/event/e614c04cat.chr). A scene's own
// pack is what tells them apart.
void ArrivalChrFile(int index, const char *path) {
    if (index < 0 || index >= 16) {
        return;
    }
    const bool scene = path != nullptr && path[0] != 0 && std::strstr(path, "/stair_mos/") == nullptr;

    g_scene_pack_bits = scene ? g_scene_pack_bits | (1u << index) : g_scene_pack_bits & ~(1u << index);
}

bool ArrivalScenePackLoaded() {
    return g_scene_pack_bits != 0;
}

// Player 1's arrival: the game's player put on the record's way before its motion steps (CoopBeforeMotion).
void ArrivalWalkPlayerOne() {
    const ArrivalTrail &t = g_arrival_trail;
    const ArrivalWalk  &w = g_arrival_walk;

    if (w.player != 1 || CharaFrame == nullptr || t.count <= 0) {
        return;
    }
    const ArrivalSample &s = t.samples[std::min(w.tick, t.count - 1)];
    sceVu0FVECTOR        place;

    SamplePlace(s, place);
    CharaFrame->SetPosition(place);
    CharaFrame->SetRotation(0.0f, s.yaw, 0.0f);
    CharaMain.SetPosition(place);
    BtActStatus.motion_no = WalkMotion(&CharaMain, s.motion, SampleMoves(w.tick));
}

// Player 2's arrival: the partner on the record's way, playing it (CoopStep, in Walk's place). False when hers is not playing.
bool ArrivalWalkPartner() {
    const ArrivalTrail &t = g_arrival_trail;
    const ArrivalWalk  &w = g_arrival_walk;

    if (w.player != 2 || g_partner.chara == nullptr || t.count <= 0) {
        return false;
    }
    const ArrivalSample &s = t.samples[std::min(w.tick, t.count - 1)];
    const int            motion = WalkMotion(g_partner.chara, s.motion, SampleMoves(w.tick));

    SamplePlace(s, g_partner.pos);
    g_partner.velocity[0] = g_partner.velocity[1] = g_partner.velocity[2] = 0.0f;
    g_partner.chara->frame->SetPosition(g_partner.pos);
    g_partner.chara->frame->SetRotation(0.0f, s.yaw, 0.0f);
    if (motion != g_partner.motion) {
        PlayMotion(motion, 4);
    }
    return true;
}

// Once a tick (CoopStep, both copies): the walk goes on, and at its end the arriver stands where play started.
void ArrivalWalkTick() {
    const ArrivalTrail &t = g_arrival_trail;
    ArrivalWalk        &w = g_arrival_walk;

    if (w.player == 0) {
        return;
    }
    if (mode != GAME_MODE_DUNGEON || t.key != FloorKey()) {
        std::fprintf(stderr, "coop: arrival walk: player %d's ended early (left the floor)\n", w.player);
        w.player = 0;
        return;
    }
    CFrame *door = w.door ? StairDoor() : nullptr;

    if (++w.tick < t.count) {
        if (door != nullptr) {
            door->SetPosition(door->position[0], t.samples[w.tick].door, door->position[2]);
        }
        if (CoopDebug() && w.tick % 30 == 0) {
            const ArrivalSample &s = t.samples[w.tick];

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
    std::fprintf(stderr, "coop: arrival walk: player %d's arrival played out (%d ticks): at (%.0f %.0f %.0f)\n", w.player, w.tick, end[0], end[1], end[2]);
    w.player = 0;
}

// The arriver where its arrival ends (end: where): where play started on the floor, beside the other player when that one stands
// there, with its camera reset behind it.
static void ArrivalStand(int player, float *end) {
    const ArrivalTrail &t = g_arrival_trail;

    end[0] = t.end[0];
    end[1] = t.end[1];
    end[2] = t.end[2];
    end[3] = 1.0f;

    // The other player may stand at or near where play starts (the one there first, not moved since its own arrival): the arriver ends
    // beside it, at its right hand or else its left (the way clear), not inside or behind it (x-floor-join -JoinVisuals: Toan hidden
    // under Xiao, then behind her camera, on her screen).
    const float *other = player == 1 ? g_partner.pos : CharaMain.pos;

    if (std::hypot(other[0] - end[0], other[2] - end[2]) < 2.5f * kSpawnDistance) {
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
        CharaFrame->SetRotation(0.0f, t.end_yaw, 0.0f);
        CharaMain.SetPosition(end);
        BtActStatus.motion_no = 0;
        ResetArrivalCamera(&MainCamera__4, end, t.end_yaw); // coop-arrivalcam: on both copies (his camera is game state)
    } else if (player == 2 && g_partner.chara != nullptr) {
        sceVu0CopyVector(g_partner.pos, end);
        g_partner.chara->frame->SetPosition(g_partner.pos);
        g_partner.chara->frame->SetRotation(0.0f, t.end_yaw, 0.0f);
        PlayMotion(0, 0);
        ResetArrivalCamera(g_camera.ready ? g_camera.camera : nullptr, end, t.end_yaw); // coop-arrivalcam: hers, stepped on both
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
    const ArrivalSample &s = t.samples[std::min(w.tick, t.count - 1)];

    for (int i = 0; i < 3; i++) {
        eye[i] = s.eye[i];
        ref[i] = s.ref[i];
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

} // namespace coop

static void CoopViewChoose() {
    // coop-cutsceneview: this copy's floor start held for the other's opening scene of it: that scene's camera, as the
    // other copy draws it, on this copy's build of the same floor (its player stands in for the other's: ScenePoseTick).
    // Apart, the link down: not only with a camera each.
    sceVu0FVECTOR eye;
    sceVu0FVECTOR ref;
    float         roll = 0.0f;
    const bool    scene = CoopWorldsSceneView(eye, ref, &roll);

    sceVu0FVECTOR arrival_eye;
    sceVu0FVECTOR arrival_ref;
    const bool    arrival = !scene && ArrivalWalkView(arrival_eye, arrival_ref);

    g_view_active = IndependentCameras() || scene || arrival;
    g_view_swapped = false;

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

    // coop-opt7: this copy's own presentation engine (its player's chest, run on this copy alone): its camera, draw only.
    if (CCameraFollow *own = PresentEngineCamera(); own != nullptr) {
        NowCamera__3 = own;
        g_view_swapped = true;
        CamTestView(NowCamera__3, g_view_shown);
        return;
    }

    // coop-openercam: her chest opening, on her copy: its own camera, as his presentation has his (OpeningCamera).
    if (NetplayLocalPlayer() == 2 && g_partner.visible && OpeningCamera() != nullptr) {
        NowCamera__3 = OpeningCamera();
        g_view_swapped = true;
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
}

void CoopViewBegin() {
    DrawRngEnter(); // coop-drawrng: every copy's floor draw, linked or not, cameras or not
    PartnerBoxDrawBegin(); // coop-chestfix
    CoopViewChoose();
    ArrivalViewLog(NowCamera__3); // coop-arrivalcam: test hook

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

        KeepPlayProjection(two ? g_other_pres.on : PartnerPresenting(), two ? OpeningCamera() != nullptr : PresentationTask(gameTask) || PresentEngineCamera() != nullptr); // (coop-opt7: his engine's)
    }
    if (g_view_active || PresentationTask(gameTask)) {
        LogOpenerView();
    }
}

static void ViewEnd();
static void DrawTwiceCheck();

void CoopViewEnd() {
    PartnerBoxDrawEnd(); // coop-chestfix
    ViewEnd();
    DrawRngLeave();
    DrawTwiceCheck();
    NetplayUnitTick(); // coop-pilot: DC_UNIT's cases, after a whole floor frame
}

static void ViewEnd() {
    if (!g_view_active) {
        return;
    }

    OtherPresentationEnd();
    PutPresentationProjection(); // coop-hischest

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
}

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

bool CoopPlayerShown() {
    if (BtActStatus.player_visible != 0) {
        return true;
    }

    // coop-cutsceneview: the other's player in the other's scene, which this copy's stands in for.
    if (CoopWorldsSceneShowsOther()) {
        return true;
    }

    // His first-person look (InitEyeCamera hides him on both copies) on her copy, which shows her camera.
    return gameTask == GAME_TASK_EYE_CAMERA && IndependentCameras() && NetplayLocalPlayer() == 2 && g_partner.visible && !CoopWorldsApart() &&
           !CoopMutate("fp_hidden");
}

static int g_player_screen[2] = {-1, -1};

void CoopDrawPlayer() {
    // coop-pt1109b: her copy draws him only inside the distance retail draws a monster at (OtherBeyondClip)
    if (ActorLocalIndex() == 1 && coop::OtherBeyondClip()) {
        return;
    }

    // Draw-only: the shared flag is put back before anything else reads it.
    const auto visible = BtActStatus.player_visible;
    float      waist[4] = {CharaMain.pos[0], CharaMain.pos[1] + 6.0f, CharaMain.pos[2], 1.0f};
    int        screen[4];

    g_player_screen[0] = g_player_screen[1] = -1;
    if (MGRotTransPers(screen, waist, 0) != 0) {
        g_player_screen[0] = (screen[0] - 0x6C00) >> 4;
        g_player_screen[1] = ((screen[1] - GS_Y_OFFSET) >> 4) * 2;
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
    Draw_MainUnit();
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
                     gameTask == GAME_TASK_EYE_CAMERA ? 1 : 0, NetplayLocalPlayer() == 2 ? 2 : 1, drawn ? g_player_screen[0] : -1,
                     drawn ? g_player_screen[1] : -1);
        frames = shown = 0;
    }
}

bool CoopPartnerCamera(float *angle, float *pos) {
    if (!IndependentCameras() || !g_camera.ready) {
        return false;
    }

    *angle = g_camera.camera->angle;
    g_camera.camera->GetPos(pos);
    return true;
}
