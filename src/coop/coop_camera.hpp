#pragma once

#include "coop/game_ns.hpp"

#include <cstdint>

// The shared couch co-op camera's helpers, and the partner's own camera while linked copies keep one
// camera per player (always, linked).

namespace dc::platform {
struct Config;
} // namespace dc::platform

namespace dc::game {
class CCameraFollow;
} // namespace dc::game

namespace coop {

bool PathClear(const float *from, const float *to);
bool CameraLineClear(const float *from, const float *to); // candfix2: a camera line past the floor's camera collision (camtest)

// Whether each player has a camera of their own: co-op linked (or online). Couch co-op on one screen keeps the shared camera.
// (coopoptions, Jep 11-17: game.coop_independent_cameras retired; a linked game always gives each player a camera.)
bool IndependentCameras();

// coop-gapmark: whether this floor draw is its copy's player's own scene (the Xiao gap's cutscene, its own chest or Atla engine,
// her gate key): set as the view is chosen (CoopViewChoose), off after the draw (CoopViewEnd).
bool ViewPrivateScene();

// The angle the partner's stick is read against: its own camera's while the cameras are independent.
float PartnerCameraAngleH();

// coop-wave3 (local-first Wave 3): the steer row. Each pad carries the yaw its left stick is read against (InputPadState::steer),
// read on its player's own copy as the pad is read, so the step on every copy turns the stick by the yaw that player saw.
// SteerQuantize: a yaw in 65536ths of a turn. SteerOwnAngle: the yaw this copy's own player's stick is read against now, for the
// port its pad goes to (0 the game's player, 1 the partner): the camera the step read before the row. SteerAngle: the yaw the step
// turns a port's stick by this tick: its pad's row, else `camera` (not linked, or no row: the game's own camera, as retail).
std::int32_t SteerQuantize(float yaw);
float        SteerOwnAngle(int port);
float        SteerAngle(int port, float camera);
// coop-wave3: whether DunMoveChara's collision takes cell (x, z) for a walker at pos: within 240 of it each way (as the partner's own
// pass), 400 for Gallery of Time's wide parts (wide: retail's own 160 more for them); not near the camera this copy draws, which is
// its own player's. DC_COOP_MUTATE=collision_cells_camera (twin): retail's, within 240 of the camera (camera_dist).
bool         CollisionCellNear(int x, int z, const float *pos, float camera_dist, bool wide);
// coop-wave3 part 2: a camera's input (port: whose pad its player plays on, 0 the game's player, 1 the partner): its player's own
// controller as read now on its own copy (NetplayCameraPad), else `game`, the agreed pad's reading as the game's code has it.
enum CameraButton { kCameraR1, kCameraL1, kCameraL2 };
float        CameraRX(int port, float game);
float        CameraRY(int port, float game);
int          CameraOn(int port, int game, CameraButton button);
void         CameraScrambleTick(long long tick); // the test hook DC_COOP_TEST_CAMERA_SCRAMBLE (netplay's tick, after the pads)

// The floor's opening event (system script 100, Toan arriving) while the cameras are independent: the
// partner is put on the floor and stands through it, so the partner's copy can show it arriving.
bool FloorIntro();
// coop-entrance: a floor entrance's own spot for a walker after the lead (2, 3), in the lead's frame: right of him, along his
// way (+ before him), her facing turned from his (radians). nullptr: none for that dungeon (the side table, or the cave's trail).
// coop-entrance2 (Jep 11-12): own, the walker's entrance starts at the lead's own (his place, its own arrival motion: as retail
// plays it for that character as the lead), and right/along/turn are where it ends, beside him.
struct EntranceSpot {
    float right;
    float along;
    float turn;
    bool  own;
    float way_right; // own: the walk's offset to his right along his way (0: in his steps)
};
const EntranceSpot *EntranceSpotFor(int map, int walker);
// coop-entrance2: the per-character entrance rules (coop_camera.cpp, at kEntranceDelayStep): a character's delay in ticks for
// players arriving together (EntranceTogether: this floor's opening began with the partner beside the lead), Moon Sea's pad
// spots and beams (MoonEntrance, MoonSpot; the retail beam's clock and its reveal tick, -1 none), the walkers' own characters,
// and the beams' draw (CoopDraw). EntranceHidden: the character is not drawn yet (before its beam). EntranceTableTest: the
// 6-character table row's hook (DC_COOP_TEST_ENTRANCE_PLAN).
int  EntranceCharaDelay(int chara);
void EntranceOrderOffsets(const int *charas, int n, int *out);
int  EntranceTogetherOffset(int chara);
bool ForestTailOn(); // coop-entrance2: the forest's partner walk-in playing on past the opening to its curved end
// Option C's stand-in of the game's player on his lagged way (draw: coop_party's StandInPosed), false when none.
bool EntranceSwapStandIn(int *chara, float *place, float *yaw, int *motion, int *flags);
// coop-entrance2: the stand-in still waits its delay at his way's start (not drawn).
bool EntranceSwapStandInWaiting();
// coop-entrancereds: the together walk-in draws both players (past its cut, neither waiting for its start or beam).
bool EntranceWalkInShowsBoth();
const char *EntranceCharaName(int chara);
int  EntranceLeadChara();
int  EntranceWalkChara();
int  EntranceWalkDelay();
bool EntranceTogether();
bool MoonEntrance();
bool MoonSpot(int chara, float *place, float *yaw);
void MoonSpotAt(int chara, const float *centre, float pad_yaw, float *place);
int  MoonEntranceClock();
int  MoonEntranceReveal();
void MoonEntranceTick();
void MoonShownTick();
bool EntranceLeadHidden();
bool EntrancePartnerHidden();
void EntranceBeamsDraw();

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
// coop-b4arrive: cut: the merge landed over the arriver's own arrival as it played (a pick join: NetplayHello::cut_walk): its walk
// goes on from record tick `from` (what its screen had shown, ArrivalOwnShown, the host's word), not the stand coop-joinlate gives an
// arriver that had its arrival, shown on its own screen (ArrivalWalkCut), the floor's title at its end on that screen.
// ArrivalOwnShown: this copy's own arrival on its floor as far as it has played (record ticks), 0 none.
void ArrivalWalkStart(int player, bool cut, int from);
bool ArrivalWalkCut();
int  ArrivalOwnShown();
int  ArrivalWalkRecordTick(); // the record's tick the walk stands at, -1 none
int  ArrivalWalkPlayer();
void ArrivalWalkEndAtResync(); // coop-b9arrive: a resync leaves the floor: no walk goes on into the entrance's floor menu
void ArrivalWalkPlayerOne();
bool ArrivalWalkPartner();
void ArrivalWalkTick();
// coop-entrwin (Jep 11-11, item 23 option A): a join in a floor's entrance window. The walker (player, its walker number 2 or 3)
// plays its own entrance from its start on both copies at the lockstep: the lead's walk-in record (still being recorded while the
// lead's walk-in runs, ahead of the walker) played from its first tick at the walker's entrance spot (EntranceSpotFor, in the
// lead's frame at each tick), its screen drawing it from the walk-in's own camera moved by that spot; at its end the walker stands
// where the lead's walk-in ended moved by the spot. EntranceWalkOn: such a walk plays now.
// gap: the ticks the walker's walk-in began after the lead's (the walker's estimate, said in its Hello; -1 not known): arrivals
// within the arrival window go by arrival order (EntranceArrivalOffsets).
void EntranceWalkStart(int player, int walker, int gap);
// coop-entrwin: player 1's entrance drawn by the partner while player 2's walk-in is still hers (coop-sceneowner), and the member
// the partner then is (his), -1 otherwise.
bool EntranceWalkByPartner();
int  EntranceWalkMember();
// coop-entrwin: the entrance window's plan for any number of players: per arrival (player, the tick its walk-in began on one clock,
// -1 none) 1 the lead, 2.. its walker number, 0 a scene-arriver; lead_length the lead's walk-in in ticks. EntrancePlanTest: the
// table row's hook (DC_COOP_TEST_ENTRANCE_PLAN).
struct EntranceArrival {
    int player;
    int start;
};
void EntrancePlan(const EntranceArrival *in, int n, int lead_length, int *out);
// coop-entrance2 rule 3 (Jep 11-12, arrivals near together): each arrival's entrance start in ticks after the first arrival's, -1 none.
void EntranceArrivalOffsets(const EntranceArrival *in, int n, int *out);
void EntrancePlanTest();
bool EntranceWalkOn();
void EntranceWalkDrawnTrace(); // candfix2b: once a walk, the partner drawn at the walker's start (CoopStep)

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
// coop-dungeonreds (tests: x-dungeon-mark-lock, her frame a boulder's face): a test's put of her far from where her camera looks
// (coop_duntest PutAt) cuts it there keeping its angle, which may run into the floor's rocks at her new spot (the fixture's angle,
// set clear where she started). Her next cut starts it as a load's does (PartnerCameraClearStart), on both copies alike.
void PartnerCameraClearAtCut();
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
