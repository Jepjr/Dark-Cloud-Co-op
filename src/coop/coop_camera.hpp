#pragma once

#include "coop/game_ns.hpp"

// The shared couch co-op camera's helpers, and the partner's own camera while linked copies keep one
// camera per player (game.coop_independent_cameras).

namespace dc::platform {
struct Config;
} // namespace dc::platform

namespace dc::game {
class CCameraFollow;
} // namespace dc::game

namespace coop {

bool PathClear(const float *from, const float *to);

// game.coop_independent_cameras as a linked session uses it: on unless the config says false, so the
// Linked and Online launchers get a camera each with no setting of their own. The host sends it in its
// Hello and the joiner takes it (NetplayHelloAdopt), so both copies agree. DC_COOP_MUTATE=cameras_default_off
// (a test twin) makes unset off, as before.
bool IndependentCamerasSetting(const dc::platform::Config &config);

// Whether each player has a camera of their own: co-op linked (or online) with IndependentCamerasSetting
// on. Couch co-op on one screen always keeps the shared camera.
bool IndependentCameras();

// The angle the partner's stick is read against: its own camera's while the cameras are independent.
float PartnerCameraAngleH();

// The floor's opening event (system script 100, Toan arriving) while the cameras are independent: the
// partner is put on the floor and stands through it, so the partner's copy can show it arriving.
bool FloorIntro();

// Where the player is drawn (place, with w 1) and its own x and z axes on the ground, unit long: its
// frame's world matrix while a scene's node carries it (Divine Beast Cave's opening), else CharaMain.pos
// and its yaw.
void PlayerDrawnPlace(float *place, float *axis_x, float *axis_z);

// Through the floor's opening and on the tick after it, when the opening has put the player where play
// starts, keeps the partner beside the player where it is drawn (PlayerDrawnPlace; the event moves it):
// at its right hand as PlaceBesidePlayer puts it, or another side when a wall, rock or tree is in the
// way, picked when the opening starts and held to its end. While a scene's node carries the player (and
// to the opening's end), she follows his way instead, a little behind and to his right, playing what he
// played there. CoopStep calls it every tick.
void StandBesideArrival();

// coop-arrivalfull: the floor's arrival played again for the copy that caught up (coop_camera.cpp): each copy records its
// own (ArrivalTrailTick, every tick), and both play it for the arriver from the lockstep's first tick (ArrivalWalkStart,
// EnterLockstep): player 1 before his motion steps (ArrivalWalkPlayerOne, CoopBeforeMotion), player 2 in the partner's
// walk (ArrivalWalkPartner, CoopStep). ArrivalWalkPlayer: the arriver whose arrival plays now (its pad neutral), 0 none.
void ArrivalTrailTick();
void ArrivalWalkStart(int player);
int  ArrivalWalkPlayer();
void ArrivalWalkPlayerOne();
bool ArrivalWalkPartner();
void ArrivalWalkTick();

// coop-latefall: the event script's chr packs (_LOAD_CHR_FILE's, CoopWorldsSceneChrFile) as game state: whether any of them is a
// scene's own, not the floor's stairwell (dun/d0N/stair_mos). This file's globals ride the state set, so both copies agree after a
// merge (CoopWorldsArrivalPlaysScene).
void ArrivalChrFile(int index, const char *path);
bool ArrivalScenePackLoaded();

// coop-snapshot (two copies): the partner's camera object, made if none yet (it lives on this copy's heap, so a
// snapshot carries its bytes as an extra region into this copy's own); and around an apply, the module's pointers to
// it: Before keeps this copy's, After puts them back where the host's state named the host's object.
CCameraFollow *CoopCameraObject();
void           CoopCameraSnapshotBefore();
void           CoopCameraSnapshotAfter();

// Puts the camera module back as it boots, for a linked session's reset (resync or reconnect): the
// partner's camera starts again from the player's on the next tick, the opening and the couch camera's
// look-point shift start again, and nothing drawn is held. The partner's loaded arrival stays, as it
// goes with the partner's model, which LoadArrival checks. CoopResetForSession calls it on both copies.
void CameraResetForSession();

// Counts a tick for the partner's camera; CoopStep calls it every tick, before the partner moves.
void PartnerCameraTick();
// coop-dungeonjoin: her member, the game's player until now, becomes the partner where she stands (a floor merge by her state):
// her camera goes on from the player's as it is.
void PartnerCameraFromPlayer();
// coop-floorexit: the other way, her member becoming the game's player where she stands (she stays on a floor he left): the
// player's camera goes on from hers, and a cell-built floor's cells are measured from it at once (the player's collision).
void PlayerCameraFromPartner();

// Follows the partner with its own camera and turns it with pad 2's right stick (and R1, L1, L2), as
// DunMoveChara and autoCamTrial do the player's. CoopStep calls it after the partner has moved, on
// both linked copies alike; playing is whether the players are under control.
void StepPartnerCamera(bool playing);

// coop-p2circle: turns the partner's camera round behind her facing (retail's Circle with no monster to lock on),
// on both linked copies alike, as L2 does in StepPartnerCamera.
void PartnerCameraBehind();

// coop-drawrng (root fix 5, Jep 2026-10-30): what one copy draws or lays out for its own screen takes its random numbers
// from a draw-only stream, never from the lockstep generator, so two linked copies whose screens differ never part. The
// stream carries on from frame to frame (particles and sparkles keep moving, where putting the generator back after each
// draw froze them) and is neither checksummed nor carried. Enter and Leave nest; only the outermost pair swaps.
// DC_COOP_MUTATE=draw_rng_shared (the root fix off): draws roll the lockstep generator, as retail.
void DrawRngEnter();
void DrawRngLeave();
bool DrawRngHeld(); // coop-guard: a draw holds the stream now (the write guard's: which of the two states is the lockstep's)

class DrawRng {
public:
    DrawRng() { DrawRngEnter(); }
    ~DrawRng() { DrawRngLeave(); }
    DrawRng(const DrawRng &) = delete;
    DrawRng &operator=(const DrawRng &) = delete;
};

} // namespace coop
