#pragma once

#include "coop/game_ns.hpp"

#include <cstdint>

namespace dc::game {
class CCameraFollow;
class CMotionModel;
} // namespace dc::game

// coop-opt7 (Jep 11-09; debug\notes\opt7-design.md): a chest's or an Atla's presentation is its opener's own. Two halves:
//   - the world half, on every copy alike (lockstep; the player table's g_present in the state set and the checksum): at the
//     opener's agreed Cross the box opens, the item goes into the bag (Jep: at the opening press), the opener stands at the
//     box and plays the opening pose, out of harm and still; the world stays in play (no presentation gameTask) and the
//     opener counts as paused in the pause rule (PauseZoneHeld: all players paused holds the world);
//   - the local half, on the opener's copy alone: retail's own Init and Loop (btitem.cpp) as an engine, their world writes
//     put back after each step (or kept off by the recipe's PresentEngine() guards), its camera this copy's view only, its
//     random draws on the draw stream. When it ends it sends kCmdPresent; on that command's tick every copy frees the opener.
// Players are 1 up and any number (the player table); the rows test two.
// DC_COOP_MUTATE=present_lockstep (twin): retail's presentation task on every copy, as before.
namespace coop {

enum PresentKind : std::uint8_t {
    kPresentNone = 0,
    kPresentSmall = 1, // a small chest (BtGetTreasureboxSmall_*)
    kPresentBig = 2,   // a large chest without a trap (BtGetTreasureboxBig_*)
    kPresentAtla = 3,  // an Atla (BtAtraGetShort_*)
};

// The local path is on (linked co-op, as the local menus; else retail's task, alone too).
bool PresentLocalOn();

// Every copy, at a player's agreed Cross on a chest or an Atla (player 1: DunMoveChara; player 2: her Cross, Interact): the world
// half's start. True when it took it (retail's task, or her old path, is then not entered). Any player of the table whose body
// is in the world (the resident's, the partner's); players past 2 have none yet.
bool PresentStartFor(int player, int active);

// Whether a player is in a presentation of their own (the world half's state, the same on every copy).
bool PlayerPresenting(int player);
bool AnyPresenting();

// DunMoveChara's play task, every copy: player 1 presenting steps the world half (the pose) instead of his free play, then
// on his own copy the engine. True when it took the tick.
bool PresentPlayTick();
// The same for a player who is not the game's (the partner: her walk's StepBusy, every copy): true while it holds her.
bool PresentStepFor(int player);
// A player's presentation kind (PresentKind), kPresentNone for none: the world's, alike on every copy.
int  PresentKindOf(int player);

// While retail's Init and Loop run as this copy's engine (the recipe's guards read it), and what the world decided for it.
bool PresentEngine();
int  PresentEngineItem();
int  PresentEngineVolume();
int  PresentEngineRefusal();
// coop-pt1111b (Jep's 11-11 playtest, item 10: the opener's '"" acquired.'): the Atla retail's step 0 names (BtAtraGetID). As
// this copy's engine, the one the world took at the press (PresentEngineItem): the registry's id (read) is -1 by then, the
// world's getAtraToSaveData having emptied its slot. Else read, retail's. DC_COOP_DEBUG: the name's message said
// ("coop-opener: player N's engine names Atla <id> (message <no>)"). DC_COOP_MUTATE=atlaname_reread (twin): read, as before.
int  PresentEngineAtla(int map_no, int read);
// The Atla's stance the world gave its opener (PlayerAtlaStance, run once at the press on every copy): true when the opener
// stands in it (a member who is not Toan), with the effect's turn.
bool PresentEngineStance(float *turn);

// The engine's camera while it frames this copy's view (CoopViewChoose), else null.
CCameraFollow *PresentEngineCamera();

// coop-camflash (Jep's 11-16 playtest: "when P1 goes to open a chest, it plays a frame of a different camera angle or something
// before it starts the animation"). Retail's chest step 1 (BtGetTreasureboxSmall_Loop / Big_Loop, btitem.cpp) poses the opener
// at the box and puts the presentation on SubCamera with `SubCamera = MainCamera__4`, but CCameraFollow's operator= copies only
// follow, distance and height (ps2/src/camerafollow.cpp, the retail function): SubCamera's eye and look stay where its last
// user (the floor's opening script, the last chest, a key or an Atla) left them, and step 1's tick is drawn from there. Step 2
// then reads the effect's "cam"/"int" frames (setCameraPassData) as motionDrive posed them. ChestCameraOwe (step 1, after it
// set the effect's frame 10): the effect posed at that frame, its clock put back, and retail's step-2 read, so the scene's first
// drawn frame is its own camera and the next one does not move, whatever order the opener's step and motionDrive run in (player
// 2's engine steps after motionDrive). DC_COOP_MUTATE=camflash_stale (twin): as retail, step 1's tick drawn from SubCamera's
// last eye ("coop-camflash: ... (camflash_stale)").
void ChestCameraOwe(CMotionModel *effect);

// coop-opt7e: the Atla's light follows its opener (P2+ run P1's routines). Retail's Draw_MainUnit lights the game's player's body
// (CharaMain) from the effect's "light01" frame while atraGetStatus; the opener's copy runs retail's Atla as its engine, so the
// body lit is the engine's player's, any player. PresentLitPlayer: whose body is lit on this copy (0 none). PresentLight: for a
// player's body standing at stood, retail's light into light/colour when it is that player's (Draw_MainUnit asks for the
// resident's, the partner's draw for hers); false and untouched otherwise.
// DC_COOP_MUTATE=atlalight_resident (twin): retail's choice, the resident lit whoever opened (her body unlit on her own screen).
int  PresentLitPlayer();
bool PresentLight(int player, const float *stood, float light[4][4], float colour[4][4]);

// Test hook (DC_COOP_TEST_OPENER=<the opener under test, 1 up>, x-opener-local): every 10th tick of the press clock on a floor,
// every copy, any task:
//   coop-opener: test: p<local> clock <c> task <gameTask> presenting <its kind> engine <0/1> mesoff <1: the game's message
//   window shut> motion <its body's motion: BtActStatus.motion_no, or the partner's> held <the world held> atla <atraGetStatus>
//   lit <the players whose bodies the Atla's light was last drawn on here, comma-joined, 0 none>
// (coop_pause's PauseTick calls it).
void PresentTestTick(bool held);

// The state set's take and apply on this copy (snapshot_state.cpp, around SnapshotTakeBytes / SnapshotApplyBytes). While this copy
// runs a presentation's engine its screen state (the opening box's and the Atla's flags, the game's message window, the minimap)
// is the engine's alone: a take puts the no-presentation values in the set, so a joiner never shows the opener's screen; an apply
// leaves this copy's own, so the engine goes on through a resync onto the opener. Unused anywhere else (nothing to keep).
// DC_COOP_MUTATE=present_screen_lost (twin): the apply puts the set's over it. (The take's side has no twin yet: it needs a
// real join into a presentation, left in opt7-design.md.)
// coop-tripwires3: the opener's engine's own screen state (the box's and the Atla's flags, the message window's switch, the mini-map)
// exchanged in for its scope, on the opener's copy while its engine runs; nothing elsewhere (coop_opener.cpp EngineScreenSwap).
// The floor's draw (coop_camera.cpp CoopViewBegin/End) and motionDrive's presentation effects (motiondrive.cpp) hold one.
class PresentScreenIn {
  public:
    PresentScreenIn();
    ~PresentScreenIn();
    PresentScreenIn(const PresentScreenIn &) = delete;
    PresentScreenIn &operator=(const PresentScreenIn &) = delete;

  private:
    bool on_;
    bool counted_;
};

class PresentSetScope {
  public:
    explicit PresentSetScope(bool take);
    ~PresentSetScope();
    PresentSetScope(const PresentSetScope &) = delete;
    PresentSetScope &operator=(const PresentSetScope &) = delete;

  private:
    bool take_;
    bool kept_ = false;
};

// Test hook (DC_COOP_TEST_PRESENT_RELOAD=1, x-opener-local -Case reload): 1 at the 40th tick of player 1's presentation (take the
// state set to memory), 2 at its 70th (load it again), on every copy alike; 0 otherwise (netplay.cpp's state file frame).
int PresentTestStateDue();

// The checksum's word (every player's kind, 2 bits each), the kCmdPresent handler's registration (once), session reset.
std::uint32_t PresentHash();
void          PresentInstall();
void          PresentResetForSession();

} // namespace coop
