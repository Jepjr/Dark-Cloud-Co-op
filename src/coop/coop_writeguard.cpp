// coop-review1109 (tests only; nothing here runs without its switch): the shared-state review's two instruments.
//
// DC_QA_WRITE_GUARD=<regions>[|<census dir>[|<every N ticks>]], where <regions> is the bin's darkcloud.snapregions.
//
// 1. The guard. Code inside DrawRngEnter/Leave runs on one copy's own terms (a local menu's step and draw, a copy's own view,
//    a label's layout). Around the outermost pair it keeps the state set's globals (all but those over 1 MB) and compares them
//    after; and the same around a local menu's own scope (CoopPartsWritesBegin/End: every local menu's step, open and draw, its
//    pokes back by then, on its owner's copy alone). A byte that changed in a menu's scope is a write to the shared world from
//    one copy, the class behind most menu desyncs. A draw's run on every copy, so its lines are a census, not a fault. One line
//    per scope kind, caller and global: "writeguard: menu|draw +<caller's RVA> wrote <global>+0x<offset> ...". The row checks the
//    menu lines against qa\local-writes-allow.txt (t-triangle-menu -WriteGuard, lib\Waste.ps1 Get-LocalWriteStrays).
// 2. The census (with a dir). Every N link ticks (default 240) both copies write every state-set global's and every outside
//    global's hash, plus the bytes of those of 512 bytes or fewer, to <dir>\census-p<player>-<tick>.txt: what differs between two
//    copies in lockstep on one tick.
#include "coop/game_ns.hpp"
#include "coop/coop_writeguard.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "coop/coop.hpp"
#include "coop/coop_camera.hpp" // coop-guard: DrawRngHeld
#include "platform/paths.hpp"

#ifdef _WIN32
namespace dc::game {
extern "C" char __ImageBase; // (lld's, at the exe's base: the region file's offsets are from it)
} // namespace dc::game
#endif

namespace coop {
namespace {

struct GuardRegion {
    std::uintptr_t offset;
    std::size_t    size;
    std::string    name;
    std::size_t    shadow_at;
    char           kind; // 's' state set, 'o' outside it
};

constexpr std::size_t kGuardMost = 1024 * 1024;
constexpr int         kGuardLinesMost = 600;

struct Scope {
    std::vector<unsigned char> shadow;
    void                      *caller = nullptr;
    const char                *site = nullptr;
    std::uintptr_t             skip = 0; // (coop-guard: a global the scope holds something else in, by its offset)
    bool                       rng_held = false; // (coop-guard: a draw held the random numbers as the scope began)
    int                        depth = 0; // (coop-guard: a scope inside one of its kind is the outer one's: its town menu inside FullMenuWritesBegin)
};

struct Guard {
    bool                     read = false;
    bool                     on = false;
    std::vector<GuardRegion> regions;
    Scope                    scopes[2];
    std::size_t              shadow_size = 0;
    std::string              dir;
    int                      every = 240;
    long long                last_census = -1;
    long long                tick = -1;
    int                      lines = 0;
    std::set<std::pair<std::pair<int, std::uintptr_t>, std::pair<std::size_t, std::size_t>>> said;
};
Guard g_guard;

std::uintptr_t ImageAt() {
#ifdef _WIN32
    return (std::uintptr_t) &__ImageBase;
#else
    return 0;
#endif
}

void ReadList(const std::string &path, char kind) {
    std::ifstream in(PathsFromUtf8(path.c_str()));
    std::string   line;

    while (std::getline(in, line)) {
        char               what[16] = {}, name[512] = {};
        unsigned long long offset = 0, size = 0;

        if (line.empty() || line[0] == '#') {
            continue;
        }
        if (kind == 's') {
            if (std::sscanf(line.c_str(), "%15s %llx %llx %511s", what, &offset, &size, name) != 4) {
                continue;
            }
        } else if (std::sscanf(line.c_str(), "%llx %llx %511s", &offset, &size, name) != 3) {
            continue;
        }
        g_guard.regions.push_back({(std::uintptr_t) offset, (std::size_t) size, name, 0, kind});
    }
}

void GuardRead() {
    if (g_guard.read) {
        return;
    }
    g_guard.read = true;
    const char *spec = TestEnv("DC_QA_WRITE_GUARD");

    if (spec == nullptr || ImageAt() == 0) {
        return;
    }
    std::string       s = spec;
    const std::size_t a = s.find('|');
    const std::string path = s.substr(0, a);

    if (a != std::string::npos) {
        const std::size_t b = s.find('|', a + 1);

        g_guard.dir = s.substr(a + 1, b == std::string::npos ? b : b - a - 1);
        if (b != std::string::npos) {
            g_guard.every = std::atoi(s.c_str() + b + 1);
        }
    }
    ReadList(path, 's');
    std::string outside = path;

    outside.replace(outside.rfind('.'), std::string::npos, ".outside");
    if (!g_guard.dir.empty()) {
        ReadList(outside, 'o');
    }
    std::size_t at = 0;

    for (GuardRegion &region : g_guard.regions) {
        if (region.kind == 's' && region.size <= kGuardMost) {
            region.shadow_at = at;
            at += region.size;
        } else {
            region.shadow_at = (std::size_t) -1;
        }
    }
    g_guard.shadow_size = at;
    g_guard.on = !g_guard.regions.empty();
    std::fprintf(stderr, "writeguard: on: %zu globals, %zu bytes kept round local code; census %s every %d ticks\n", g_guard.regions.size(), at,
                 g_guard.dir.empty() ? "off" : g_guard.dir.c_str(), g_guard.every);
}

// coop-guard: the random numbers, exactly. While a draw holds them (DrawRng, coop_camera.cpp), the game's generator state
// (runtime.cpp g_rand_state) is the draw stream's, per copy by design, and the lockstep's waits in g_draw_rng_step; otherwise the
// game's is the lockstep's, and g_draw_rng (the draw stream) and g_draw_rng_step (where the scope's own DrawRng parks the lockstep's)
// are this copy's own. So the guard skips the per-copy pair of the two and keeps the lockstep's: a menu that moves the lockstep's
// random numbers on one copy is named whichever way it is held.
bool IsOtherStream(const GuardRegion &region, bool held) {
    if (held) {
        return region.name == "(anonymous_namespace)::g_rand_state";
    }
    return region.name == "coop::(anonymous_namespace)::g_draw_rng" || region.name == "coop::(anonymous_namespace)::g_draw_rng_step";
}

} // namespace

void WriteGuardEnter(int kind, const char *site, void *caller, const void *skip) {
    GuardRead();
    if (!g_guard.on) {
        return;
    }
    // coop-guard: a draw's scope only for the census (it runs on every copy, so its lines are never a fault; a 3.5 MB copy per draw otherwise)
    if (kind == kGuardDraw && g_guard.dir.empty()) {
        return;
    }
    Scope &scope = g_guard.scopes[kind];

    if (scope.depth++ != 0) {
        return;
    }
    scope.caller = caller;
    scope.site = site;
    scope.skip = skip == nullptr ? 0 : (std::uintptr_t) skip - ImageAt();
    scope.rng_held = DrawRngHeld();
    scope.shadow.resize(g_guard.shadow_size);
    const std::uintptr_t base = ImageAt();

    for (const GuardRegion &region : g_guard.regions) {
        if (region.shadow_at != (std::size_t) -1) {
            std::memcpy(scope.shadow.data() + region.shadow_at, (const void *) (base + region.offset), region.size);
        }
    }
}

void WriteGuardLeave(int kind) {
    if (!g_guard.on) {
        return;
    }
    if (kind == kGuardDraw && g_guard.dir.empty()) {
        return;
    }
    Scope &scope = g_guard.scopes[kind];

    if (scope.depth == 0 || --scope.depth != 0) {
        return;
    }
    const std::uintptr_t base = ImageAt();
    const std::uintptr_t caller = scope.site != nullptr ? (std::uintptr_t) scope.site : (std::uintptr_t) scope.caller - base; // (the dedupe's key)

    for (std::size_t r = 0; r < g_guard.regions.size(); r++) {
        const GuardRegion &region = g_guard.regions[r];

        if (region.shadow_at == (std::size_t) -1 || IsOtherStream(region, scope.rng_held) || (scope.skip != 0 && region.offset == scope.skip)) {
            continue;
        }
        const auto *now = (const unsigned char *) (base + region.offset);
        const auto *was = scope.shadow.data() + region.shadow_at;

        if (std::memcmp(now, was, region.size) == 0) {
            continue;
        }
        // The changed runs of bytes, the first 8 (a big global's write is read by offset against its type).
        std::string ranges;
        std::size_t runs = 0, first = (std::size_t) -1, count = 0;

        for (std::size_t i = 0; i < region.size; i++) {
            if (now[i] == was[i]) {
                continue;
            }
            std::size_t end = i;

            while (end + 1 < region.size && now[end + 1] != was[end + 1]) {
                end++;
            }
            count += end - i + 1;
            if (first == (std::size_t) -1) {
                first = i;
            }
            if (runs++ < 8) {
                char run[48];

                std::snprintf(run, sizeof run, "%s%zx-%zx", ranges.empty() ? "" : ",", i, end);
                ranges += run;
            }
            i = end;
        }
        // Once per scope kind, caller, global and (in a big one) 256-byte stretch.
        const std::size_t stretch = region.size > 4096 ? first >> 8 : 0;

        if (!g_guard.said.insert({{kind, caller}, {r, stretch}}).second || g_guard.lines >= kGuardLinesMost) {
            continue;
        }
        g_guard.lines++;
        char where[96];

        if (scope.site != nullptr) {
            std::snprintf(where, sizeof where, "%s", scope.site);
        } else {
            std::snprintf(where, sizeof where, "+%llx", (unsigned long long) caller);
        }
        std::fprintf(stderr, "writeguard: %s %s wrote %s+0x%zx (%zu of %zu bytes at %s) at tick %lld\n", kind == kGuardMenu ? "menu" : "draw", where, region.name.c_str(), first,
                     count, region.size, ranges.c_str(), g_guard.tick);
    }
}

void WriteGuardTick(long long tick, int player, bool lockstep) {
    GuardRead();
    if (!g_guard.on) {
        return;
    }
    g_guard.tick = tick;
    if (!lockstep || g_guard.dir.empty() || tick == g_guard.last_census || g_guard.every <= 0 || tick % g_guard.every != 0) {
        return;
    }
    g_guard.last_census = tick;
    const std::string    path = g_guard.dir + "/census-p" + std::to_string(player) + "-" + std::to_string(tick) + ".txt";
    std::ofstream        out(PathsFromUtf8(path.c_str()));
    const std::uintptr_t base = ImageAt();

    for (const GuardRegion &region : g_guard.regions) {
        const auto   *at = (const unsigned char *) (base + region.offset);
        std::uint64_t hash = 1469598103934665603ull;

        for (std::size_t i = 0; i < region.size; i++) {
            hash = (hash ^ at[i]) * 1099511628211ull;
        }
        out << region.kind << ' ' << region.name << ' ' << region.size << ' ' << std::hex << hash;
        if (region.size <= 512) {
            out << ' ';
            for (std::size_t i = 0; i < region.size; i++) {
                static const char kHex[] = "0123456789abcdef";
                out << kHex[at[i] >> 4] << kHex[at[i] & 15];
            }
        }
        out << std::dec << '\n';
    }
}

} // namespace coop
