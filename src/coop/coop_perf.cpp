#include "coop/game_ns.hpp"
#include "coop/coop_perf.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <deque> // coop-pacefix: the fake FIFO queue (a test seam)
#include <SDL3/SDL.h> // coop-indoorfps: the window's screen rate
#include "coop/coop.hpp"
#include "coop/netplay.hpp"
#include "platform/clock.hpp"
#include "platform/load_work_profile.hpp" // coop-wepmenu
#include "data/data_read.hpp"               // coop-wepmenu: DataReadLoadCounts

namespace {

using Clock = std::chrono::steady_clock;

// A stall longer than this (a load, a window drag) is dropped rather than run as a burst of ticks.
constexpr std::int64_t kMaxOwed = 15;
constexpr double       kSlowTickMs = 250.0; // coop-wepmenu: a tick this long or longer is said on its own ("slowtick:")

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
    std::int64_t      made_up = 0;    // coop-p2tick: every tick made up so far (the stamps' count), never reset
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
    // coop-apartfps: where the netplay tick's time went, when it took 1 ms a tick or more (its own line: tests read the one
    // above whole; a window under that says nothing, so a row's log is not one line longer each 600 ticks)
    // coop-simslow: the display frames the render thread presented over the window (async only; its own line, as below)
    if (const double frames = CoopPresentDisplayFrames(); frames > 0.0) {
        std::fprintf(stderr, "tick-stats: display frames %.1f a second\n", frames / seconds);
        int          cuts = 0;
        const double pace = CoopPresentPacePeriod(&cuts);
        if (pace > 0.0) { // coop-indoorfps: the period the display frames keep (none: drawn back to back)
            std::fprintf(stderr, "tick-stats: display pace %.3f ms a frame, %d cuts\n", pace, cuts);
        }
    }
    const std::string parts = NetplayTickPartsWindow(static_cast<long long>(w.ticks));
    if (w.net / ticks >= 1.0) {
        std::fprintf(stderr, "tick-stats: netplay tick's parts, ms a tick: %s\n", parts.c_str());
    }
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

// Test hook (x-apart-fps; DC_PERF_TEST_WINDOW_APART=N): N ticks after the link went down (it was up first), the tick-stats window starts
// again, once, at that tick's end, so the window a test reads is the copy's play apart alone, not the fixture's load and the split
// before it (a whole window sooner: about 10 s a row).
bool g_window_restart = false;

static void TestWindowApart() {
    static const int n = TestEnvInt("DC_PERF_TEST_WINDOW_APART", 0);
    static bool      was_up = false;
    static bool      done = false;
    static int       apart = 0;

    if (n <= 0 || done) {
        return;
    }
    if (NetplayLinkUp()) {
        was_up = true;
        apart = 0;
    } else if (was_up && ++apart >= n) {
        done = true;
        g_window_restart = true;
    }
}

void CoopPerfNetplayTick() {
    TestRephaseRule();
    TestPhaseHit();
    TestWindowApart();
    Clock::time_point start = Clock::now();
    NetplayTick();
    double waited = Ms(Clock::now() - start);
    g_perf.tick_net += waited;
    Rephase(waited);
}

// jointake: a tick a hook asked about (CoopPerfSayTick): its time on the game thread before its flip, said at the flip.
const char *g_say_tick = nullptr;
double      g_say_less = 0.0;

void CoopPerfSayTick(const char *what, double less_ms) {
    g_say_tick = what;
    g_say_less = less_ms;
}

void CoopPerfFlipBegin() {
    g_perf.flip_start = Clock::now();
    g_perf.flip_count = ClockTickCount();
    if (g_say_tick != nullptr) {
        std::fprintf(stderr, "tick-stats: %s: this tick held the game thread %.2f ms before its flip (netplay wait %.2f ms%s)\n", g_say_tick,
                     g_perf.started ? Ms(g_perf.flip_start - g_perf.tick_start) - g_say_less : -1.0, g_perf.tick_net,
                     g_say_less > 0.0 ? "; a test's check on it left out" : "");
        g_say_tick = nullptr;
    }
}

// coop-p2tick: the clock's owed boundaries now and every tick made up so far, for the join's stamps.
std::int64_t CoopPerfOwed() {
    return g_perf.owed;
}

std::int64_t CoopPerfMadeUp() {
    return g_perf.made_up;
}

bool CoopPerfCatchingUp() {
    if (NetplayStateSetDue()) { // coop-firsttick: the frame that took a state set's last part skips its wait (netplay.cpp)
        return true;
    }
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
    if (static int every = 0; every++ % 60 == 0) {
        CoopPresentNoteDisplayRate(); // coop-indoorfps: the window's screen rate, for the display frames' pace (once a second)
    }
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
            g_perf.made_up += std::max<std::int64_t>(0, -extra);
            g_perf.owed = std::max<std::int64_t>(0, g_perf.owed + extra);
        }
    }
    // coop-wepmenu: a tick that held the game a quarter second or more, said on its own (the developer's rc4 freeze could only be
    // placed within a minute's tick-stats window, which also held a merge and the partner's chest scene); its log neighbours say what ran
    if (tick >= kSlowTickMs) {
        std::fprintf(stderr, "slowtick: tick %lld (link tick %lld) took %.1f ms (netplay wait %.1f, flip %.1f)\n",
                     static_cast<long long>(g_perf.total), static_cast<long long>(NetplayLinkTick()), tick, g_perf.tick_net, flip);
    }
    if (tick > w.worst) {
        w.worst = tick;
        w.worst_net = g_perf.tick_net;
        w.worst_flip = flip;
    }
    g_perf.total++;
    g_perf.tick_start = now;
    g_perf.tick_net = 0.0;

    if (g_window_restart) {
        g_window_restart = false;
        g_perf.window = {};
        g_perf.window_start = now;
        NetplayTickPartsWindow(0); // (its parts from here too)
        std::fprintf(stderr, "tick-stats: test: the window starts again at tick %lld, %d ticks after the link went down\n",
                     static_cast<long long>(g_perf.total), TestEnvInt("DC_PERF_TEST_WINDOW_APART", 0));
        return;
    }
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

// ---- coop-simslow: the render thread's display frames paced to the vblank (coop_perf.hpp) ----------------------------------
namespace {

constexpr double kPaceBlockMs = 1.0;      // a present that took this much over the frame's cost waited for a vblank
constexpr double kPaceAfterMs = 0.5;      // a display frame starts this long after a vblank (its present finds the queue empty)
constexpr double kPaceEarly = 0.003;      // the frames come this share of a period early, so a drift shows as a short wait
constexpr int    kPaceLearn = 4;          // the vblank periods seen back to back before pacing
constexpr double kPaceRelearnMs = 5000.0; // no wait seen this long: drawn back to back again until one is
constexpr double kPaceMinPeriodMs = 3.0;  // the vblank periods taken as one (333 Hz to 20 Hz)
constexpr double kPaceMaxPeriodMs = 50.0;
// coop-indoorfps (the developer's v0.2-rc2 playtest, 11-18: "FPS 123.7" on his 143 Hz screen, "sometimes lower"): the period is learnt
// from a few waiting presents' returns, which come a millisecond or two off the vblank (PC-02 at 60 Hz: 18.45, 16.14, 16.93 ms),
// so it was learnt up to ~2% long at 60 Hz (16.93-17.33 ms against 16.67) and, the same milliseconds against a 6.95 ms vblank,
// 5-15% long at 143 Hz. Paced frames then come late, never find the last one still queued, never wait, and nothing corrected
// it: a display frame lost every few vblanks for the whole session (58.2-59.1 a second at 60 Hz, 47.2 in one session; 143 /
// 1.15 = 124 at 143 Hz). A period right or short makes a wait soon (each frame comes kPaceEarly sooner until one finds the last
// still queued), so this many paced frames without one mean the period is long: it is cut by kPaceShrink and tried again.
// DC_PERF_MUTATE=pace_no_shrink (the twin): never cut, as before.
// The learner also took its first sample whole and its 10% gate then refused every true one (PC-02, i01h06: 19.64 ms learnt, every
// later sample 15.5-17.1 refused), and stray waits (a frame slow for its own reasons) kept breaking the silence. So the screen's
// own rate (SDL's mode of the window's display, read on the game thread once a second) caps the period whenever it is known:
// a period never longer than the vblank's always makes the waits that keep the phase. DC_PERF_MUTATE=pace_no_cap (the twin): no
// cap. The silence cut stays for a screen whose rate is unknown.
constexpr int    kPaceSilentFrames = 60;
constexpr double kPaceShrink = 0.02;

struct PresentPace {
    bool         read = false;
    bool         off = false;
    bool         trace = false;
    int          traced = 0;
    double       cost = -1.0;  // ms a display frame takes to draw and present without waiting
    double       period = 0.0; // ms between vblanks
    int          learnt = 0;   // the periods seen
    std::int64_t anchor = 0;   // ns: the last vblank seen (a waiting present's return)
    std::int64_t next = 0;     // ns: when the next display frame starts (0: from the anchor)
    int          silent = 0;   // coop-indoorfps: paced frames since the last that waited
    bool         no_shrink = false;
    bool         no_cap = false;
    int          skew = 0;     // coop-indoorfps test seam (DC_QA_PACE_SKEW_PCT): the learnt period made this much long, once
};
PresentPace               g_pace; // (the render thread's alone)
std::atomic<std::int64_t> g_display_frames{0};
std::atomic<double>       g_pace_period{0.0}; // coop-indoorfps: for tick-stats (the render thread writes them)
std::atomic<int>          g_pace_cuts{0};
std::atomic<double>       g_display_period{0.0}; // coop-indoorfps: ms between the screen's vblanks (0: unknown); the game thread's

double NsMs(std::int64_t ns) {
    return static_cast<double>(ns) / 1.0e6;
}

std::int64_t MsNs(double ms) {
    return static_cast<std::int64_t>(ms * 1.0e6);
}

} // namespace

std::int64_t CoopPresentPaceDue(std::int64_t now_ns) {
    if (!g_pace.read) {
        g_pace.read = true;
        g_pace.off = TestEnvIs("DC_PERF_MUTATE", "unpaced_present");
        g_pace.trace = TestEnvOn("DC_PERF_PACE_TRACE");
        g_pace.no_shrink = TestEnvIs("DC_PERF_MUTATE", "pace_no_shrink") || TestEnvIs("DC_PERF_MUTATE", "pace_unbounded");
        g_pace.no_cap = TestEnvIs("DC_PERF_MUTATE", "pace_no_cap") || TestEnvIs("DC_PERF_MUTATE", "pace_unbounded");
        g_pace.skew = TestEnvInt("DC_QA_PACE_SKEW_PCT", 0);
    }
    if (g_pace.off || g_pace.anchor == 0 || g_pace.learnt < kPaceLearn) {
        return 0;
    }
    if (NsMs(now_ns - g_pace.anchor) > kPaceRelearnMs) {
        g_pace.anchor = 0; // (drawn back to back until a present waits again)
        g_pace.next = 0;
        return 0;
    }
    // A paced present finds the queue empty and does not wait, so the vblanks are counted on from the last one seen, a little
    // short (kPaceEarly): a frame that comes before its vblank waits for it, and that wait is the next anchor.
    // coop-indoorfps: never longer than the screen's own vblank period, when known (above)
    const double screen = g_pace.no_cap ? 0.0 : g_display_period.load(std::memory_order_relaxed);
    // coop-pacefix (Jep's v0.2.3 playtest, 11-22: "FPS 31.2  TICK 33.8/60" mid-combo, two windows on a 143 Hz and a 60 Hz screen): the
    // screen's own period whenever its rate is known, never one under it. The learnt period was only capped at the screen's, so a period
    // learnt or cut short stayed in use: his player 1 paced at 3.4-3.5 ms on a 6.99 ms vblank from its first second (a fast pair of
    // waiting presents taken as the vblank), his player 2 cut down to 9-12 ms on a 16.67 ms one. Frames then came about twice a vblank,
    // FIFO presents waited inside the render thread's turn, and the game thread waits for that turn (its lease, MGEndFrame) every tick:
    // 1-2.7 ms a tick on average, up to 7-24 ms (100 once) against PC-02's 0.004, and a combo's extra drawing pushed ticks past 16.7 ms
    // (33.8 a second, the lockstep dragging both copies). The learnt period paces a screen whose rate is unknown, as before.
    // DC_PERF_MUTATE=pace_under_screen (twin): the learnt period capped at the screen's, as before.
    static const bool under = TestEnvIs("DC_PERF_MUTATE", "pace_under_screen");
    const double      period = screen > 0.0 ? (under ? std::min(g_pace.period, screen) : screen) : g_pace.period;
    const std::int64_t step = MsNs(period * (1.0 - kPaceEarly));
    if (g_pace.next == 0) {
        g_pace.next = g_pace.anchor + step + MsNs(kPaceAfterMs); // (the frame that waited shows at the vblank after its own)
    }
    while (g_pace.next + step <= now_ns) {
        g_pace.next += step; // (a frame time missed: the latest one gone by)
    }
    if (now_ns < g_pace.next) {
        return g_pace.next - now_ns;
    }
    g_pace.next += step; // (drawn now)
    return 0;
}

void CoopPresentPaceNote(std::int64_t start_ns, std::int64_t end_ns) {
    g_display_frames.fetch_add(1, std::memory_order_relaxed);
    double took = NsMs(end_ns - start_ns);
    bool   waited = g_pace.cost >= 0.0 && took >= g_pace.cost + kPaceBlockMs;
    if (g_pace.trace && g_pace.traced < 600) {
        g_pace.traced++;
        std::fprintf(stderr, "pace: took %.2f cost %.2f%s, %.2f ms after the anchor, period %.3f\n", took, g_pace.cost,
                     waited ? " (waited)" : "", g_pace.anchor != 0 ? NsMs(end_ns - g_pace.anchor) : -1.0, g_pace.period);
    }
    if (!waited) {
        // (drawing costs: down at once, up slowly; a wait never counts)
        g_pace.cost = g_pace.cost < 0.0 || took < g_pace.cost ? took : g_pace.cost + 0.05 * (took - g_pace.cost);
        // coop-indoorfps: paced this long without a wait, the period is long (kPaceSilentFrames)
        if (g_pace.anchor != 0 && g_pace.learnt >= kPaceLearn && ++g_pace.silent >= kPaceSilentFrames && !g_pace.no_shrink) {
            g_pace.silent = 0;
            g_pace.period *= 1.0 - kPaceShrink;
            g_pace_cuts.fetch_add(1, std::memory_order_relaxed);
            g_pace_period.store(g_pace.period, std::memory_order_relaxed);
        }
        return;
    }
    g_pace.silent = 0;
    // It waited: its return is a vblank.
    if (g_pace.anchor != 0) {
        double between = NsMs(end_ns - g_pace.anchor);
        double n = g_pace.period > 0.0 ? std::round(between / g_pace.period) : 1.0;
        double each = n >= 1.0 ? between / n : 0.0;
        if (g_pace.period <= 0.0 || (n < 1.0 && between >= kPaceMinPeriodMs)) {
            if (between >= kPaceMinPeriodMs && between <= kPaceMaxPeriodMs) {
                g_pace.period = between; // (the first, or a faster display than learnt)
                g_pace.learnt = 1;
            }
        } else if (n >= 1.0 && n <= 8.0 && std::fabs(each - g_pace.period) < 0.1 * g_pace.period) {
            g_pace.period += (g_pace.learnt < kPaceLearn ? 0.5 : 0.1) * (each - g_pace.period);
            g_pace.learnt++;
        }
    }
    // coop-indoorfps test seam: the period as learnt, made DC_QA_PACE_SKEW_PCT long once (a noisy learn, as the developer's at 143 Hz)
    if (g_pace.skew != 0 && g_pace.learnt >= kPaceLearn) {
        g_pace.period *= 1.0 + g_pace.skew / 100.0;
        g_pace.skew = 0;
    }
    g_pace_period.store(g_pace.learnt >= kPaceLearn ? g_pace.period : 0.0, std::memory_order_relaxed);
    g_pace.anchor = end_ns;
    g_pace.next = 0;
}

double CoopPresentDisplayFrames() {
    return static_cast<double>(g_display_frames.exchange(0, std::memory_order_relaxed));
}

double CoopPresentPacePeriod(int *cuts) {
    *cuts = g_pace_cuts.load(std::memory_order_relaxed);
    const double learnt = g_pace_period.load(std::memory_order_relaxed);
    const double screen = g_pace.no_cap ? 0.0 : g_display_period.load(std::memory_order_relaxed);
    static const bool under = TestEnvIs("DC_PERF_MUTATE", "pace_under_screen");
    if (learnt > 0.0 && screen > 0.0 && !under) {
        return screen; // coop-pacefix: the pace in use (CoopPresentPaceDue)
    }
    return learnt > 0.0 && screen > 0.0 ? std::min(learnt, screen) : learnt;
}

void CoopPresentNoteDisplayRate() {
    // (DC_QA_DISPLAY_HZ: a headless test's stand-in for the window's screen, which it has none of)
    static const int test_hz = TestEnvInt("DC_QA_DISPLAY_HZ", 0);
    double           hz = test_hz;
    if (hz <= 0.0) {
        int          count = 0;
        SDL_Window **windows = SDL_GetWindows(&count);
        if (windows != nullptr && count > 0) {
            const SDL_DisplayMode *mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(windows[0]));
            if (mode != nullptr && mode->refresh_rate_numerator > 0 && mode->refresh_rate_denominator > 0) {
                hz = static_cast<double>(mode->refresh_rate_numerator) / mode->refresh_rate_denominator;
            } else if (mode != nullptr) {
                hz = mode->refresh_rate;
            }
        }
        SDL_free(windows);
    }
    const double ms = hz >= 1000.0 / kPaceMaxPeriodMs && hz <= 1000.0 / kPaceMinPeriodMs ? 1000.0 / hz : 0.0;
    g_display_period.store(ms, std::memory_order_relaxed);
}

bool CoopTestRenderAsync() {
    static const bool on = TestEnvOn("DC_QA_RENDER_ASYNC");
    return on;
}

bool CoopTestFakeVsyncPresent() {
    static const int hz = TestEnvInt("DC_QA_FAKE_VSYNC_HZ", 0);
    static const Clock::time_point origin = Clock::now();
    static Clock::time_point       shown_at; // the vblank that shows the frame queued last
    // coop-pacefix test seams: DC_QA_FAKE_DRAW_US=X: each display frame takes X microseconds more to draw (a heavier screen's GPU, as a bigger
    // window's); DC_QA_FAKE_VSYNC_QUEUE=N: the swapchain holds N frames, so a present returns at once until N are waiting for their
    // vblanks and only then waits for the oldest to show (Windows' flip-model FIFO; 1, the default: each waits, as before).
    static const int draw_us = TestEnvInt("DC_QA_FAKE_DRAW_US", 0);
    static const int depth = std::max(1, TestEnvInt("DC_QA_FAKE_VSYNC_QUEUE", 1));
    static std::deque<Clock::time_point> queued; // the vblanks the frames still queued show at
    if (hz <= 0) {
        return false;
    }
    if (draw_us > 0) { // (spun, as drawing works: a sleep this short can oversleep by a timer quantum)
        const Clock::time_point until = Clock::now() + std::chrono::microseconds(draw_us);
        while (Clock::now() < until) {
        }
    }
    const Clock::duration period = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0 / hz));
    Clock::time_point     now = Clock::now();
    if (depth > 1) {
        while (!queued.empty() && queued.front() <= now) {
            queued.pop_front();
        }
        if ((int) queued.size() >= depth) {
            std::this_thread::sleep_until(queued.front()); // (the queue full: the oldest frame's vblank frees an image)
            now = queued.front();
            queued.pop_front();
        }
        const Clock::time_point next = origin + ((now - origin) / period + 1) * period;
        queued.push_back(queued.empty() ? next : std::max(next, queued.back() + period));
        return true;
    }
    if (shown_at > now) {
        std::this_thread::sleep_until(shown_at); // (a frame still queued: wait for its vblank, as a FIFO present does)
        now = shown_at;
    }
    shown_at = origin + ((now - origin) / period + 1) * period;
    return true;
}

// ---- coop-wepmenu: a load step on the game thread, timed by Chronicle's load profile (coop_perf.hpp) --------------------------
namespace {
namespace profile = dc::platform::load_profile;
constexpr profile::Kind kStepKinds[6] = {profile::Kind::BackgroundRead, profile::Kind::Metadata, profile::Kind::Png,
                                         profile::Kind::Texture, profile::Kind::Visual, profile::Kind::Mesh};
double StepKindMs(profile::Kind kind) {
    return static_cast<double>(profile::counters[static_cast<unsigned>(kind)].nanoseconds) / 1'000'000.0;
}
} // namespace

CoopLoadStepTimer::CoopLoadStepTimer(const char *what) : what_(what), was_active_(profile::active) {
    profile::active = true;
    for (int i = 0; i < 6; i++) {
        before_[i] = StepKindMs(kStepKinds[i]);
    }
    dc::data::DataReadLoadCounts(&loads_, &prefetched_);
    start_ = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count();
}

CoopLoadStepTimer::~CoopLoadStepTimer() {
    const std::int64_t now = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count();
    double             part[6];
    for (int i = 0; i < 6; i++) {
        part[i] = StepKindMs(kStepKinds[i]) - before_[i];
    }
    profile::active = was_active_;
    std::uint64_t loads = 0;
    std::uint64_t prefetched = 0;
    dc::data::DataReadLoadCounts(&loads, &prefetched);
    std::fprintf(stderr,
                 "coop-wepmenu: %s held the game thread %.2f ms (reads %.2f, metadata %.2f, png %.2f, textures %.2f, visual %.2f, mesh %.2f); "
                 "files read %llu, of them copied from the prefetch %llu\n",
                 what_, static_cast<double>(now - start_) / 1'000'000.0, part[0], part[1], part[2], part[3], part[4], part[5],
                 static_cast<unsigned long long>(loads - loads_), static_cast<unsigned long long>(prefetched - prefetched_));
}
