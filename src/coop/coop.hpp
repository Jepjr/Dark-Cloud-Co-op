#pragma once

#include "coop/game_ns.hpp"

#include <string> // coop-warmreset: CoopJobLeftovers

// Couch co-op (game.coop, off by default): on a dungeon floor, the second controller plays a second
// party member beside the player, on the same screen. The partner is the first party member other
// than the one in control (Xiao when she has joined), with its own CCharacter, model arena and
// texture block, so the game's CharaMain and CharaModelBuffer are left alone.
//
// Pad 2's left stick walks and runs it, Cross swings its weapon at the monsters. L3 swaps pad 2 to the
// next party member (Select+L3 the one before), never the player's; a downed partner gets up after
// 10 s, or after 2 s of the player standing still beside it (coop_party.cpp, StepDown). // coop-party
// The monsters go after
// whichever of the two is nearer (coop_target.cpp) and their hits take the partner's life, which is
// its character's own in the party status. The camera looks between the two and draws back as they
// part.

// Turns the camera to take in both before motionDrive steps it. DunMoveChara calls it each tick.
// Linked copies with game.coop_independent_cameras give each player a camera instead: the player's
// is the game's, the partner's its own (coop_camera.cpp), and each copy shows its own player's.
void CoopBeforeMotion();

namespace dc::game {
class CFrameVu1;
} // namespace dc::game

// Around DunMainDraw: while the cameras are independent, the partner's copy draws from the partner's
// camera, round the partner, and drawing leaves nothing behind that the next tick reads (the random
// generator, the map cells' distances from the camera). Both copies call them.
void CoopViewBegin();
void CoopViewEnd();

// The character the floor is drawn round (the chests, water, fires and lights near it): the partner
// on its own copy while the cameras are independent, else the player.
CFrameVu1 *CoopViewFrame();

// coop-fpvisible: whether this copy draws the game's player (Toan, or whoever he plays) on the floor and its map
// arrow. As the game has it (BtActStatus.player_visible, shared state), except on player 2's linked copy while player 1
// looks in first person: her screen is hers, so he stays in it. Draw-only. DC_COOP_MUTATE=fp_hidden hides him (twin).
bool CoopPlayerShown();
// Draws him (Draw_MainUnit, which tests player_visible as well) when CoopPlayerShown, and notes where on screen.
void CoopDrawPlayer();
// coop-fpvisible (DC_COOP_DEBUG): once a second, how many frames this copy drew its player on the floor.
void CoopPlayerDrawReport(bool drawn);
// coop-joinvisuals (DC_COOP_DEBUG): where on screen (640x480) the last floor frame put the player's and the partner's feet
// and heads (-1 off screen): x y of feet then head, player first. For the tests' frame checks.
void CoopScreenSpots(int out[8]);

// The partner's camera, for the linked copies' checksum: false while the cameras are shared.
bool CoopPartnerCamera(float *angle, float *pos);

// Moves, swings and animates the partner, and takes the monsters' hits on it. DunMoveChara calls it
// each tick after motionDrive; playing is whether the player is under control on the floor (no
// pause, menu or event).
void CoopStep(bool playing);

// Draws the partner's shadow and model. DunMainDraw calls them beside the player's.
void CoopDrawShadow();
void CoopDraw();

// Draws the partner's life bar and name under the player's status; y is the status panel's slide-in.
void CoopDrawHud(int y);

// coop: gaps2
// The mini-map's second player: false unless the partner is on the floor; else where it stands, the
// way it faces, and whether it is this copy's own player (a linked copy B). Draw only.
bool CoopMiniMapPartner(float *pos, float *yaw, bool *own);

// coop: minimap
// The other copy's player's arrow on the map (Jep's "different color minimap icons"): true and the vertex
// colour it takes; this copy's own keeps retail's. DC_COOP_MUTATE=maparrows: false, one colour for both.
bool CoopMiniMapOtherColour(unsigned char *red, unsigned char *green, unsigned char *blue);
bool CoopMiniMapArrowColour(bool other, unsigned char *red, unsigned char *green, unsigned char *blue); // coop-uinotice
// With DC_COOP_MAPARROWS, every frame the map draws: where its arrows went (logical 640x480, -1 for
// none), this copy's own and the other's, for the minimap test's frame check.
void CoopMiniMapArrows(int own_x, int own_y, int other_x, int other_y);
// coop-minimapfill
// Retail's dungeon map (its dark-blue tiles; Jep, 2026-10-28: a solid fill). The seen cells' tiles take alpha; under
// DC_COOP_MUTATE=mapfill (v-minimap-fill's twin) a quarter of it, see-through as Chronicle's old aerial map read.
int CoopMiniMapFillAlpha(int alpha);
// With DC_COOP_MAPTILES, every frame the map draws: the alpha the seen cells' tiles take and where retail's tiles for them
// go (logical 640x480, each tile's top-left corner, x y pairs), for the minimap fill test's frame check.
void CoopMiniMapTiles(int alpha, const int *places, int count);
// coop-p2marks: the game's "!" over her head (mark: the dungeon's bicCursorFrame) while her Cross or Square would act on
// something where she stands, as his own over him. DunMainDraw calls it after his. Draw-only (coop_interact.cpp).
namespace dc::game {
class CFrame;
} // namespace dc::game
void CoopDrawPartnerMark(CFrame *mark);

// Draws the partner's shots. DunMainDraw calls it beside NowShotData->draw.
void CoopDrawShots();

// Whether the monsters may go after the partner (it is on the floor and standing), and if so where it
// stands, the way it last moved, ticks since its last swing and which character it is. Any out
// pointer but the first may be null.
bool CoopPartnerTarget(float *out_position, float *out_heading, int *out_since_attack, int *out_chara);

// WS5 (DC_COOP_TEST_FRAME_WATCH=<ms>, coop_crashlog.cpp): a frame was drawn; armed: sample the game thread when the next is late.
void CoopFrameBeat(bool armed, long long link_tick);

// WS5 (a test copy's alone, coop_core.cpp): hold this copy's log lines in memory, then write them as one.
void QaLogHold(bool hold);
double QaLogCost(long long *lines);
void   QaLogTotals(double *ms, long long *lines); // coop-small1b: the game thread's log write time (ms) and lines since the start // coop-small1b: the game thread's log write time (ms) and lines since the last call

// Whom monster monster_no goes after: 0 the player, 1 the partner.
int CoopMonsterTarget(int monster_no);

// Test, debug and launcher variables (DC_COOP_*, DC_NETPLAY_*, DC_TICK_STATS ...), all read through
// these. TestEnv: the value, or null when it is unset or empty.
const char *TestEnv(const char *name);
// coop-serve: a persistent test client's job sets its own test switches ("env NAME=VALUE" in its job file): TestEnv
// reads them before the process's environment. A switch read once into a static keeps its first value; the few the jobs
// drive (the dungeon driver, the menu presses) re-read theirs when TestJobGeneration() changes.
void TestEnvSetJob(const char *name, const char *value);
void TestEnvClearJob();
int  TestJobGeneration();
// coop-fx51a (switchreread, train 52's slice): COOP_TEST_SWITCH(type, name, expr) declares a function's static `name`
// holding expr, read again whenever TestJobGeneration() changes, so a warm pair's next job reaches a switch read once
// into a static before (its own value, or none). DC_COOP_MUTATE=switch_stale (warm-switch-selftest's twin): read once.
bool TestSwitchStale();
// coop-fx51a (shortest in ticks, qa\lib\Waste.ps1): with DC_QA_TICK_MARK=1 (a fixture test copy) every line the copy writes on
// stdout or stderr starts "f<frame> ", its own game frame at the write (the dev build wraps the CRT's formatted output,
// debug\build\reproducible.cmake DCO_QA_STAMP). NetplayFrameStart sets the frame.
void TestLogFrame(long long frame);
// coop-fx51a: a test trace's said-once (or last said) state, put back to value at each job on a warm pair, so each job says
// its lines as a fresh copy would (coop_keytab's Attach board line, said once a process, missed p1attach on a pair a row
// before it had opened that board). DC_COOP_MUTATE=trace_once_kept (warm-repeat-selftest -TraceTwin): kept, as before.
bool TestTraceKept();
#define COOP_TEST_TRACE_RESET(var, value)                                                                                   \
    do {                                                                                                                    \
        static int var##_job = TestJobGeneration();                                                                         \
        if (var##_job != TestJobGeneration()) {                                                                             \
            var##_job = TestJobGeneration();                                                                                \
            if (!TestTraceKept()) {                                                                                         \
                var = (value);                                                                                              \
            }                                                                                                               \
        }                                                                                                                   \
    } while (0)
// coop-warmreset (test-time plan WS7 step 1): a warm pair's job begins with its own generation (ServeResetForJob, just
// before its fixture goes back), so a counter reset at a generation change starts from the fixture's moment, not from the
// last job's end (the pair idles between jobs).
void TestJobBegin();
// Whether DC_COOP_MUTATE (the process's own) names mutation: a per-job reset's twin keeps that leftover (warm-repeat-selftest).
bool TestJobResetKept(const char *mutation);
// A test counter or said-once state that a check line rests on, put back to value at each job (as COOP_TEST_TRACE_RESET), its
// twin its own mutation (DC_COOP_MUTATE=<mutation>: kept, as before) rather than trace_once_kept.
#define COOP_TEST_JOB_RESET(var, value, mutation)                                                                           \
    do {                                                                                                                    \
        static int var##_job = TestJobGeneration();                                                                         \
        if (var##_job != TestJobGeneration()) {                                                                             \
            var##_job = TestJobGeneration();                                                                                \
            if (!TestJobResetKept(mutation)) {                                                                              \
                var = (value);                                                                                              \
            }                                                                                                               \
        }                                                                                                                   \
    } while (0)
#define COOP_TEST_SWITCH(type, name, expr)                                                                                  \
    static type name{};                                                                                                     \
    static int  name##_generation = -1;                                                                                     \
    if (name##_generation != TestJobGeneration() && (name##_generation < 0 || !TestSwitchStale())) {                         \
        name##_generation = TestJobGeneration();                                                                            \
        name = (expr);                                                                                                      \
    }
// The value, or fallback when it is unset or empty.
const char *TestEnvOr(const char *name, const char *fallback);
// The value as a number (atoi), or fallback when it is unset or empty.
int TestEnvInt(const char *name, int fallback);
// Whether a switch is on: set, not empty and not starting with '0' (DC_PERF_TWIN=1, DC_COOP_MOUSE_TWIN=1).
bool TestEnvOn(const char *name);
// Whether the variable is exactly value (DC_COOP_OWN_BAGS=1).
bool TestEnvIs(const char *name, const char *value);
// DC_COOP_DEBUG: the headless tests' extra log lines. Read once.
bool CoopDebug();

// Session reset: puts every co-op module's own state back to what a fresh session starts with, so two
// copies that restart from the host's save after a resync or reconnect go on alike. Netplay calls it
// on both copies at the resync tick. Each module resets its own part (coop_core.cpp lists them).
void CoopResetForSession();

// coop-warmreset (test-time plan WS7 step 1): a warm pair's job start, on both copies on the job's tick, just before its fixture
// goes back (netplay.cpp ServeResetForJob): CoopResetForSession (what a resync resets: the wheels, the monsters' targets, the
// partner's shot ring, the key picker, the prompts, the menus, the cameras; whatever of it the state set holds, the fixture puts
// back after), then the per-job scratch the session reset leaves (the door vote, the worlds' said-once lines, the presence clock,
// the partner voice, the message tags, the camera tests). None of it is in the state set or the checksum. Twins (DC_COOP_MUTATE):
// job_session_kept (no session reset), job_gate_kept (the door vote and the gate's shown build kept), job_scratch_kept (the rest).
void CoopResetForJob();
// The job's leftovers line (qa-job: N leftovers: ...): what a job finds outside the state set once its fixture is back, for
// warm-repeat-selftest to compare with the pair's first job.
std::string CoopJobLeftovers();

// coop-lockhud: DunMainDraw's lock-on marker, name and bar per copy (LockHudBegin, LockHudEnd, LockHudPlayerPreview).
#include "coop/coop_lockhud.hpp"
