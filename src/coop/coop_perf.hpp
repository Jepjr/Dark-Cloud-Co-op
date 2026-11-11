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
void CoopPerfFlipEnd();

// Whether this tick ends without waiting for the next boundary because the game is behind the clock.
// MGFlip used to wait for the boundary after the one a late tick missed, so each late tick (a slow
// Present, the other copy's input, a busy PC) cost a tick for good, and both linked copies ran slow.
// Now a late tick is owed: the ticks after it start at once until the game is back on the clock.
// More than kMaxOwed owed (a load, a long stall) are dropped instead of run in a burst.
// DC_PERF_TWIN=1 (the tick-rate test's mutation twin) puts back the old wait.
bool CoopPerfCatchingUp();

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
