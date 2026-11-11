#include "coop/game_ns.hpp"
#include "coop/coop_perf.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include "coop/coop.hpp"
#include "coop/netplay.hpp"
#include "platform/clock.hpp"

namespace {

using Clock = std::chrono::steady_clock;

// A stall longer than this (a load, a window drag) is dropped rather than run as a burst of ticks.
constexpr std::int64_t kMaxOwed = 15;

// A NetplayTick longer than this waited for the other copy's input: the clock moves to it (Rephase).
constexpr double kRephaseMs = 1.0;
constexpr int    kRephaseCalls = 30;   // of the last this many calls (half a second),
constexpr int    kRephaseWaits = 20;   // this many waited: the copies are out of phase, not one hitch
constexpr int    kRephaseMarginMs = 8; // half a tick into the slack
constexpr int    kRephaseSomeWaits = 8;   // coop-netphase: or this many waited, kRephaseSumMs together: a gap under a tick
constexpr double kRephaseSumMs = 30.0;

// The ticks' wall times in 0.1 ms buckets up to 250 ms (the last holds the rest; a headless test's
// load ticks run past 50 ms), for the window's p50 and p95 (the coop-qa history's tick.p50_ms/p95_ms).
constexpr int    kHistBuckets = 2500;
constexpr double kHistBucketMs = 0.1;

struct Window {
    std::int64_t hist[kHistBuckets] = {};
    std::int64_t ticks = 0;
    std::int64_t dropped = 0;
    std::int64_t made_up = 0;
    double       net = 0.0;
    double       flip = 0.0;
    double       worst = 0.0;
    double       worst_net = 0.0;
    double       worst_flip = 0.0;
    std::int64_t rephased = 0;
    double       rephase_ms = 0.0;
    std::int64_t rephase_calls = 0; // the calls that waited over kRephaseMs
    double       coop[kCoopPerfParts] = {};
};

struct Perf {
    bool              read_period = false;
    std::int64_t      period = 0;
    bool              read_twin = false;
    bool              twin = false;
    std::int64_t      total = 0;
    bool              started = false;
    Clock::time_point window_start;
    Clock::time_point tick_start;
    Clock::time_point flip_start;
    std::int64_t      flip_count = 0; // the clock's tick count as this tick's flip began
    std::int64_t      owed = 0;       // boundaries late ticks passed that the game has not run yet
    double            tick_net = 0.0;
    Window            window;
};

Perf g_perf;

double Ms(Clock::duration d) {
    return std::chrono::duration<double, std::milli>(d).count();
}

std::int64_t Period() {
    if (!g_perf.read_period) {
        g_perf.read_period = true;
        g_perf.period = TestEnvInt("DC_TICK_STATS", 0);
    }
    if (g_perf.period > 0) {
        return g_perf.period;
    }
    return ClockUnbounded() ? 0 : 3600;
}

bool Twin() {
    if (!g_perf.read_twin) {
        g_perf.read_twin = true;
        g_perf.twin = TestEnvOn("DC_PERF_TWIN");
        if (g_perf.twin) {
            std::fprintf(stderr, "tick-stats: DC_PERF_TWIN: late ticks are dropped (the old MGFlip wait)\n");
        }
    }
    return g_perf.twin;
}

// The tick time at or under which a share p of the window's ticks fall: the upper edge of its bucket.
double Percentile(const Window &w, double p) {
    std::int64_t want = static_cast<std::int64_t>(std::ceil(p * static_cast<double>(w.ticks)));
    std::int64_t seen = 0;
    for (int i = 0; i < kHistBuckets; i++) {
        seen += w.hist[i];
        if (seen >= want) {
            return (i + 1) * kHistBucketMs;
        }
    }
    return kHistBuckets * kHistBucketMs;
}

void Report(Clock::time_point now) {
    const Window &w = g_perf.window;
    double        seconds = std::chrono::duration<double>(now - g_perf.window_start).count();
    double        ticks = static_cast<double>(w.ticks);
    double        tick_ms = seconds * 1000.0 / ticks;
    double        coop = 0.0;
    for (double part : w.coop) {
        coop += part;
    }
    std::fprintf(stderr,
                 "tick-stats: tick %lld, %lld ticks in %.2f s = %.2f tps; per tick %.2f ms: sim %.2f, netplay wait %.2f, "
                 "flip %.2f; clock ticks dropped %lld, made up %lld; worst %.1f ms (netplay %.1f, flip %.1f); "
                 "coop %.3f ms per tick: step %.3f, draw %.3f, hud %.3f, checksum %.3f; tick p50 %.2f, p95 %.2f ms; "
                 "clock moved to the other copy %lld times (%.1f ms in waits over 1 ms, on %lld calls)\n",
                 static_cast<long long>(g_perf.total), static_cast<long long>(w.ticks), seconds, ticks / seconds,
                 tick_ms, tick_ms - (w.net + w.flip) / ticks, w.net / ticks, w.flip / ticks,
                 static_cast<long long>(w.dropped), static_cast<long long>(w.made_up), w.worst, w.worst_net,
                 w.worst_flip, coop / ticks, w.coop[kCoopPerfStep] / ticks, w.coop[kCoopPerfDraw] / ticks,
                 w.coop[kCoopPerfHud] / ticks, w.coop[kCoopPerfChecksum] / ticks, Percentile(w, 0.50), Percentile(w, 0.95),
                 static_cast<long long>(w.rephased), w.rephase_ms, static_cast<long long>(w.rephase_calls));
}

} // namespace

CoopPerfScope::CoopPerfScope(CoopPerfPart part) : part_(part), start_(Clock::now().time_since_epoch().count()) {}

CoopPerfScope::~CoopPerfScope() {
    g_perf.window.coop[part_] += Ms(Clock::now() - Clock::time_point(Clock::duration(start_)));
}

// coop-rephase (Jep's pack 8 playtest, 2026-11-04: FPS well under the monitor's in co-op, the partner's running choppy): a
// linked copy whose ticks start ahead of the other copy's waits in NetplayTick for that copy's input every tick (his log:
// 12 ms of each 16.7, after a catch-up join left the joiner about 60 ms behind), then MGFlip's wait for the boundary, which
// presents the frames between ticks, had 2 ms left: about one frame a tick, its interpolation squeezed into those 2 ms. The
// clock now starts this tick where the input came (ClockSetTickRate re-anchors it here, the tick's sim so far counted too),
// so the next input comes in time and MGFlip's wait is a whole tick again. Only the copy that waits moves, and only later,
// so the pair settles inside the input delay's slack and neither waits; no boundary is owed or dropped for the wait. Only a
// wait on most of the last kRephaseCalls calls moves it: one the other copy's hitch caused (it makes its late ticks up after)
// would move the waiter later for good each time, and the pair with it (net-phase's first run: 21 moves in 10 s, both at
// 57.5 tps). It moves kRephaseMarginMs past the input too (one short hold of the picture): moved only to the input, it sat on
// the edge of the delay's slack and waited 1-2.4 ms on half its ticks, never 30 in a row (net-phase's second run).
// DC_PERF_MUTATE=no_rephase (net-phase~twin): the clock stays where it was, as before.
// coop-netphase (train 51's net-phase flake, 2026-11-04): after the other copy hitched and dropped its late boundaries, the
// gap it left was under a tick, so the input came late on some ticks only: A waited 4.5 ms a tick on average for 600 clean
// ticks (1-2.5 ms on a loaded PC with no hitch at all), on fewer than kRephaseWaits of each 30 calls, and never moved. Now
// kRephaseSomeWaits of them that waited kRephaseSumMs together move it too; one hitch is one call and still moves nothing.
// RephaseDue is the rule alone (net-rephase-rule feeds it planted waits). DC_PERF_MUTATE=count_only is its twin
// (net-rephase-rule~twin): the count rule alone, as before.
struct RephaseState {
    double waits[kRephaseCalls] = {};
    int    next = 0;
};
static bool RephaseDue(RephaseState &st, double waited) {
    st.waits[st.next] = waited;
    st.next = (st.next + 1) % kRephaseCalls;
    int    count = 0;
    double sum = 0.0;
    for (double w : st.waits) {
        if (w > kRephaseMs) {
            count++;
            sum += w;
        }
    }
    bool some = count >= kRephaseSomeWaits && sum >= kRephaseSumMs && !TestEnvIs("DC_PERF_MUTATE", "count_only");
    if (count < kRephaseWaits && !some) {
        return false;
    }
    st = RephaseState{};
    return true;
}

static void Rephase(double waited) {
    static RephaseState st;

    if (ClockUnbounded() || !g_perf.started || !NetplayLinkUp() || NetplayCatchingUp() || NetplayReplaying() ||
        TestEnvIs("DC_PERF_MUTATE", "no_rephase")) {
        st = RephaseState{};
        return;
    }
    if (waited > kRephaseMs) {
        g_perf.window.rephase_ms += waited;
        g_perf.window.rephase_calls++;
    }
    if (!RephaseDue(st, waited)) {
        return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(kRephaseMarginMs));
    ClockSetTickRate(ClockTickRate());
    g_perf.window.rephased++;
}

// Test hook (net-rephase-rule): DC_PERF_TEST_REPHASE_RULE=1 feeds RephaseDue planted waits once, at the first NetplayTick, and
// says on which call each moved: a steady wait (5 ms every call), a gap under a tick (5 ms on every third call), one hitch
// (one 270 ms call among quiet ones) and quiet (0.2 ms every call).
static void TestRephaseRule() {
    static bool done = false;
    if (done || !TestEnvOn("DC_PERF_TEST_REPHASE_RULE")) {
        return;
    }
    done = true;
    auto run = [](auto wait_of) {
        RephaseState st;
        for (int i = 0; i < 90; i++) {
            if (RephaseDue(st, wait_of(i))) {
                return i + 1;
            }
        }
        return 0;
    };
    int steady = run([](int) { return 5.0; });
    int third = run([](int i) { return i % 3 == 0 ? 5.0 : 0.2; });
    int hitch = run([](int i) { return i == 10 ? 270.0 : 0.2; });
    int quiet = run([](int) { return 0.2; });
    std::fprintf(stderr, "tick-stats: test: rephase rule: steady %d, third %d, hitch %d, quiet %d (the call that moved, 0 none)\n",
                 steady, third, hitch, quiet);
}

// Test hook (net-phase): DC_NETPLAY_TEST_PHASE_MS=N moves this copy's clock N ms later once, on its 300th linked tick, as a
// catch-up join's end leaves a joiner behind the host.
static void TestPhaseHit() {
    static const int ms = TestEnvInt("DC_NETPLAY_TEST_PHASE_MS", 0);
    static int       linked = 0;

    if (ms <= 0 || !NetplayLinkUp() || ++linked != 300) {
        return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    ClockSetTickRate(ClockTickRate());
    std::fprintf(stderr, "tick-stats: test: this copy's clock moved %d ms later\n", ms);
}

void CoopPerfNetplayTick() {
    TestRephaseRule();
    TestPhaseHit();
    Clock::time_point start = Clock::now();
    NetplayTick();
    double waited = Ms(Clock::now() - start);
    g_perf.tick_net += waited;
    Rephase(waited);
}

void CoopPerfFlipBegin() {
    g_perf.flip_start = Clock::now();
    g_perf.flip_count = ClockTickCount();
}

bool CoopPerfCatchingUp() {
    if (ClockUnbounded() || !g_perf.started || Twin()) {
        return false;
    }
    // The boundaries passed since the last tick ended: from one on, this tick is late.
    std::int64_t count = ClockPump();
    std::int64_t passed = count - g_perf.flip_count;
    if (g_perf.owed + passed > kMaxOwed) {
        // This tick takes the next boundary; the rest are gone.
        g_perf.window.dropped += g_perf.owed + passed;
        g_perf.owed = 0;
        g_perf.flip_count = count;
        return false;
    }
    return g_perf.owed > 0 || passed > 0;
}

void CoopPerfFlipEnd() {
    Clock::time_point now = Clock::now();
    std::int64_t      count = ClockTickCount();
    if (!g_perf.started) {
        g_perf.started = true;
        g_perf.window_start = now;
        g_perf.tick_start = now;
        g_perf.tick_net = 0.0;
        return;
    }
    Window &w = g_perf.window;
    double  tick = Ms(now - g_perf.tick_start);
    double  flip = Ms(now - g_perf.flip_start);
    w.ticks++;
    w.hist[std::min(kHistBuckets - 1, static_cast<int>(tick / kHistBucketMs))]++;
    w.net += g_perf.tick_net;
    w.flip += flip;
    if (!ClockUnbounded()) {
        // This tick takes one boundary; each more is owed, each fewer pays one back. The twin owes
        // nothing: the boundaries are gone.
        std::int64_t extra = count - g_perf.flip_count - 1;
        if (Twin()) {
            w.dropped += std::max<std::int64_t>(0, extra);
        } else if (g_perf.owed + extra > kMaxOwed) {
            w.dropped += g_perf.owed + extra;
            g_perf.owed = 0;
        } else {
            w.made_up += std::max<std::int64_t>(0, -extra);
            g_perf.owed = std::max<std::int64_t>(0, g_perf.owed + extra);
        }
    }
    if (tick > w.worst) {
        w.worst = tick;
        w.worst_net = g_perf.tick_net;
        w.worst_flip = flip;
    }
    g_perf.total++;
    g_perf.tick_start = now;
    g_perf.tick_net = 0.0;

    // Not while a joiner replays the host's pads: the line's write to a redirected stderr stalls the next tick by
    // about a timer quantum (~15 ms against ~0.3 ms replay ticks), so a 5 minute join's replay took 11.0 s with
    // DC_TICK_STATS=60 and 5.0 s without (the zone-join thread, 2026-10-24). The window runs on and reports after.
    // NetplayReplaying, not NetplayCatchingUp: that one is off on the tick that waits for the next blob.
    // DC_PERF_MUTATE=report_in_catchup (n-debug-start-pick-town-late~stats) puts it back.
    std::int64_t period = Period();
    bool         replaying = NetplayReplaying() && !TestEnvIs("DC_PERF_MUTATE", "report_in_catchup");
    if (period > 0 && w.ticks >= period && !replaying) {
        Report(now);
        g_perf.window = {};
        g_perf.window_start = now;
    }
}
