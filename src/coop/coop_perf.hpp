#pragma once

#include "coop/game_ns.hpp"

#include <cstdint>

// coop: perf: where each game tick's wall time goes. A tick runs from one MGFlip's end to the next:
// the game's own work (sim and drawing), the linked copies' wait for the other copy's input
// (NetplayTick), and MGFlip's wait for the next tick boundary with the presents between ticks.
//
// The log line "tick-stats: ..." reports ticks per second over wall time, the milliseconds per tick
// in each part, the clock ticks the game dropped for good and those it made up (see
// CoopPerfCatchingUp), the worst tick, and the p50 and p95 tick times. It prints every 3600 ticks on a paced clock, and every N
// ticks with DC_TICK_STATS=N (any clock).

// NetplayTick, timed. pad_button_read calls it in place of NetplayTick.
void CoopPerfNetplayTick();

// MGFlip calls these around its wait for the next tick boundary.
void CoopPerfFlipBegin();
// jointake: this tick's time on the game thread before its flip (the take and the tick's sim and draw), said at its flip as
// "tick-stats: <what>: this tick held the game thread N ms before its flip".
void CoopPerfSayTick(const char *what, double less_ms = 0.0); // (less_ms: a test's own work on that tick, left out)
void CoopPerfFlipEnd();

// Whether this tick ends without waiting for the next boundary because the game is behind the clock.
// MGFlip used to wait for the boundary after the one a late tick missed, so each late tick (a slow
// Present, the other copy's input, a busy PC) cost a tick for good, and both linked copies ran slow.
// Now a late tick is owed: the ticks after it start at once until the game is back on the clock.
// More than kMaxOwed owed (a load, a long stall) are dropped instead of run in a burst.
// DC_PERF_TWIN=1 (the tick-rate test's mutation twin) puts back the old wait.
bool CoopPerfCatchingUp();

// coop-p2tick: the boundaries owed now, and every tick made up so far (the join's stamps).
std::int64_t CoopPerfOwed();
std::int64_t CoopPerfMadeUp();

// The co-op code's own share of each tick, for the tick-stats line ("coop ... ms per tick: step,
// draw, hud, checksum"): a CoopPerfScope at the top of a co-op hook adds its wall time to its part.
// Step: CoopBeforeMotion and CoopStep. Draw: the partner, its shadow and shots. Hud: its status
// lines and panels (CoopDrawHud). Checksum: the linked copies' state checksum (once a second).
enum CoopPerfPart { kCoopPerfStep, kCoopPerfDraw, kCoopPerfHud, kCoopPerfChecksum, kCoopPerfParts };

class CoopPerfScope {
public:
    explicit CoopPerfScope(CoopPerfPart part);
    ~CoopPerfScope();
    CoopPerfScope(const CoopPerfScope &) = delete;
    CoopPerfScope &operator=(const CoopPerfScope &) = delete;

private:
    CoopPerfPart  part_;
    std::int64_t  start_;
};

// coop-simslow (Jep's 0.1.5 playtest, 11-15: two windows linked on one PC, the second's tick-stats "sim" 19.5 ms, both at 47 tps):
// since the 11-10 re-pin the display frames are presented by the render thread (video.render_thread "async"), and each tick's
// MGEndFrame takes the device back from it (gfx::RenderLease), waiting for the display frame it is drawing to end. Under FIFO
// (video.present_mode "fifo", the default) that frame's vkQueuePresentKHR waits for the display's next vblank while a frame
// is still queued: drawn back to back, the render thread spent 13.4 of every 16.7 ms there (PC-02, --perf-log "present"),
// and the game thread waited for it in its tick (MGEndFrame's lease, DC_PERF_END_FRAME "lease" 12-14.5 ms), which tick-stats
// counts as sim. Now the render thread paces its display frames to the vblank: once a present is seen to wait (its frame took
// over kPaceBlockMs more than drawing one costs), its return marks a vblank, and the next display frame starts just early
// enough (its cost and kPaceLeadMs) to be presented right before the next vblank, sleeping until then, where a lease is let in
// at once (gfx WorkerMain). A present that never waits (mailbox, immediate) never paces. DC_PERF_MUTATE=unpaced_present (the
// twin, x-twocopy-sim~twin): drawn back to back, as before.
// CoopPresentPaceDue: before drawing a display frame at now, how long to sleep first (zero: draw now).
// CoopPresentPaceNote: a display frame drawn and presented from start to end (gfx::RenderList with present).
std::int64_t CoopPresentPaceDue(std::int64_t now_ns);
void         CoopPresentPaceNote(std::int64_t start_ns, std::int64_t end_ns);
// The display frames the render thread presented since the last call (tick-stats' display line).
double       CoopPresentDisplayFrames();
// coop-indoorfps: the period the display frames are paced at (ms; 0 while learning) and the times it was cut since the start.
double       CoopPresentPacePeriod(int *cuts);
// coop-indoorfps: reads the window's screen rate for the pace's cap (game thread; CoopPerfFlipEnd calls it once a second).
void         CoopPresentNoteDisplayRate();
// Test seams (x-twocopy-sim, headless): DC_QA_RENDER_ASYNC=1 keeps the async render thread in a headless copy (headless runs
// turn it off), and DC_QA_FAKE_VSYNC_HZ=N makes its present wait as FIFO does at N Hz (one frame queued; a present while one
// is queued waits for the vblank that shows it).
bool CoopTestRenderAsync();
bool CoopTestFakeVsyncPresent(); // (true: the seam is on and the frame counts as presented)

// coop-wepmenu: a load step on the game thread, timed with Chronicle's load profile (platform/load_work_profile.hpp) and said as
// "coop-wepmenu: <what> held the game thread N ms (reads R, metadata M, png P, textures T, visual V, mesh S); files read F, of them
// copied from the prefetch C" at its end.
class CoopLoadStepTimer {
public:
    explicit CoopLoadStepTimer(const char *what);
    ~CoopLoadStepTimer();
    CoopLoadStepTimer(const CoopLoadStepTimer &) = delete;
    CoopLoadStepTimer &operator=(const CoopLoadStepTimer &) = delete;

private:
    const char  *what_;
    bool         was_active_;
    std::int64_t  start_;
    double        before_[6];
    std::uint64_t loads_ = 0;      // (DataReadLoadCounts at the start: the step's own files read,
    std::uint64_t prefetched_ = 0; //  and how many came from DataReadPrefetch)
};
