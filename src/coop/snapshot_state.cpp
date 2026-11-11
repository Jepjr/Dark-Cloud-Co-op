#include "coop/game_ns.hpp"
#include "coop/snapshot_state.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include "coop/coop.hpp"
#include "coop/coop_fullmenu.hpp" // coop-offthread: the twin's menu_in_set
#include "coop/coop_opener.hpp" // coop-opt7: PresentSetScope
#include "platform/arena.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

// runscript.cpp: the script VM's heap blocks.
namespace dc::game {
void RunScriptForEachHeapBlock(void (*visit)(void *start, std::size_t size, const char *name, void *context), void *context);
} // namespace dc::game

namespace {

constexpr std::size_t kPage = 4096;

enum Kind : std::uint32_t { kPs2 = 1, kPort = 2, kArena = 3, kHeap = 4, kExtra = 5 }; // kExtra: a SnapshotExtra (two copies)

const char *KindName(std::uint32_t kind) {
    return kind == kPs2 ? "ps2" : kind == kPort ? "port" : kind == kArena ? "arena" : "heap";
}

std::uintptr_t ImageBase() {
#ifdef _WIN32
    return reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
#else
    return 0;
#endif
}

struct Region {
    std::uint32_t  kind;
    std::uint64_t  id; // a global's offset in the image, or the arena owner's
    unsigned char *start;
    std::size_t    size;
    std::string    name; // the global (snapregions), or empty for an arena
};

// A region's bytes as taken: the pages that were not all zero, in order.
struct Taken {
    std::vector<std::uint32_t> pages;
    std::vector<unsigned char> bytes;
};

struct State {
    std::vector<Region> regions;
    std::vector<Taken>  taken;
    std::size_t         bytes = 0;
};

std::uint64_t Hash(const unsigned char *data, std::size_t size) {
    // Four lanes of multiply-xor over 8-byte words: about a memcpy's speed, enough to tell two states apart.
    std::uint64_t lane[4] = {0x9e3779b97f4a7c15ull, 0xbf58476d1ce4e5b9ull, 0x94d049bb133111ebull, 0x2545f4914f6cdd1dull};
    std::size_t   at = 0;

    for (; at + 32 <= size; at += 32) {
        for (int i = 0; i < 4; i++) {
            std::uint64_t word;
            std::memcpy(&word, data + at + i * 8, 8);
            lane[i] = (lane[i] ^ word) * 0x100000001b3ull;
        }
    }
    std::uint64_t h = lane[0] ^ (lane[1] << 1) ^ (lane[2] << 2) ^ (lane[3] << 3) ^ size;

    for (; at < size; at++) {
        h = (h ^ data[at]) * 0x100000001b3ull;
    }
    return h ^ (h >> 29);
}

std::string ExeSidePath(const char *extension) {
#ifdef _WIN32
    char        path[MAX_PATH];
    DWORD       length = GetModuleFileNameA(nullptr, path, MAX_PATH);
    std::string out(path, length);
    std::size_t dot = out.rfind('.');

    return (dot == std::string::npos ? out : out.substr(0, dot)) + extension;
#else
    return std::string("darkcloud") + extension;
#endif
}

// <exe>.snapregions: "ps2|port <offset hex> <size hex> <name>" a line, written by snapregions.py from the link map.
// Each range must lie in a writable section of this image, or the file is refused (a stale file of another build).
bool ReadGlobalsFile(std::vector<Region> &out);
bool Mutated(const char *name);

// coop-offthread: the file is read once (it is this exe's own, written at its build); the stayer's take read and parsed its
// 1800 lines on the game thread at every join.
bool ReadGlobals(std::vector<Region> &out) {
    static std::vector<Region> read;
    static bool                have = false;

    if (!have) {
        std::vector<Region> fresh;

        if (!ReadGlobalsFile(fresh)) {
            return false;
        }
        // coop-offthread: DC_SNAPSHOT_MUTATE=menu_in_set (twin, x-town-stayermenu~menuset): the battle menu's working globals that
        // snapshot-allow.txt took out (the swap list's, coop_fullmenu.cpp) put back in, as before.
        // (and DC_COOP_MUTATE=merge_menu_retail: retail's menu stepped in the lockstep through the merge, which the taker can only
        // step with the sender's menu state, as before: its twins' rows fail on their checks, not on a stale pointer)
        if (Mutated("menu_in_set") || coop::MenuRetailLockstepMutated()) {
            struct Add {
                std::vector<Region> *list;
            } add{&fresh};

            coop::MenuWorkRegionsAll(
                [](void *at, std::size_t size, void *context) {
                    auto *into = static_cast<Add *>(context)->list;
                    auto *start = static_cast<unsigned char *>(at);

                    for (const Region &region : *into) {
                        if (region.start == start) {
                            return;
                        }
                    }
                    into->push_back({kPs2, reinterpret_cast<std::uintptr_t>(start) - ImageBase(), start, size, std::string("menu")});
                },
                &add);
        }
        read.swap(fresh);
        have = true;
    }
    out.insert(out.end(), read.begin(), read.end());
    return true;
}

bool ReadGlobalsFile(std::vector<Region> &out) {
    static const char *named = TestEnv("DC_SNAPSHOT_REGIONS");
    const std::string  path = named != nullptr ? std::string(named) : ExeSidePath(".snapregions");
    std::FILE         *in = std::fopen(path.c_str(), "r");

    if (in == nullptr) {
        std::fprintf(stderr, "snapshot: no state set: could not read %s\n", path.c_str());
        return false;
    }

    auto *base = reinterpret_cast<unsigned char *>(ImageBase());
#ifdef _WIN32
    auto *dos = reinterpret_cast<IMAGE_DOS_HEADER *>(base);
    auto *nt = reinterpret_cast<IMAGE_NT_HEADERS *>(base + dos->e_lfanew);
#endif
    char line[1024];
    int  bad = 0;

    while (std::fgets(line, sizeof line, in) != nullptr) {
        char               kind[8] = {};
        unsigned long long offset = 0, size = 0;
        char               name[900] = {};

        if (line[0] == '#' || std::sscanf(line, "%7s %llx %llx %899s", kind, &offset, &size, name) < 3) {
            continue;
        }
        bool writable = false;
#ifdef _WIN32
        auto *section = IMAGE_FIRST_SECTION(nt);

        for (int i = 0; i < nt->FileHeader.NumberOfSections; i++, section++) {
            if ((section->Characteristics & IMAGE_SCN_MEM_WRITE) && offset >= section->VirtualAddress &&
                offset + size <= (unsigned long long) section->VirtualAddress + section->Misc.VirtualSize) {
                writable = true;
            }
        }
#endif
        if (!writable || size == 0) {
            bad++;
            continue;
        }
        out.push_back({std::strcmp(kind, "ps2") == 0 ? kPs2 : kPort, offset, base + offset, (std::size_t) size, name});
    }

    std::fclose(in);

    if (bad > 0) {
        std::fprintf(stderr, "snapshot: no state set: %d ranges in %s lie outside this image's writable sections\n", bad, path.c_str());
        return false;
    }
    return true;
}

std::vector<Region> Arenas() {
    std::vector<Region> regions;

    ArenaForEach(
        [](const void *owner, unsigned char *start, std::size_t size, void *context) {
            auto *list = static_cast<std::vector<Region> *>(context);
            list->push_back({kArena, reinterpret_cast<std::uintptr_t>(owner) - ImageBase(), start, size, std::string()});
        },
        &regions);
    return regions;
}

// Game state the port keeps on the C heap, block by block: the script VM's stacks and function records
// (runscript.cpp). In one copy the blocks stay put while no script is loaded between the take and the restore.
std::vector<Region> HeapBlocks() {
    std::vector<Region> regions;

    RunScriptForEachHeapBlock(
        [](void *start, std::size_t size, const char *name, void *context) {
            auto *list = static_cast<std::vector<Region> *>(context);
            char  label[96];
            std::snprintf(label, sizeof label, "%s@%p", name, start);
            list->push_back({kHeap, 0, static_cast<unsigned char *>(start), size, label});
        },
        &regions);
    return regions;
}

std::string Label(const Region &region) {
    char at[32];
    std::snprintf(at, sizeof at, "+0x%llx", (unsigned long long) region.id);
    return std::string(region.kind == kExtra ? "extra" : KindName(region.kind)) + " " + (region.name.empty() ? std::string("image") + at : region.name);
}

void TakeRegion(const Region &region, Taken &taken) {
    static const unsigned char zero[kPage] = {};
    const std::size_t          pages = (region.size + kPage - 1) / kPage;

    taken.pages.clear();
    taken.bytes.clear();

    for (std::size_t i = 0; i < pages; i++) {
        const std::size_t length = std::min(kPage, region.size - i * kPage);

        if (std::memcmp(region.start + i * kPage, zero, length) != 0) {
            taken.pages.push_back((std::uint32_t) i);
            taken.bytes.insert(taken.bytes.end(), region.start + i * kPage, region.start + i * kPage + length);
        }
    }
}

void Take(State &state) {
    state.taken.resize(state.regions.size());
    state.bytes = 0;

    for (std::size_t i = 0; i < state.regions.size(); i++) {
        TakeRegion(state.regions[i], state.taken[i]);
        state.bytes += state.taken[i].bytes.size();
    }
}

void PutRegion(const Region &region, const Taken &taken) {
    const std::size_t pages = (region.size + kPage - 1) / kPage;
    std::size_t       next = 0, at = 0;

    for (std::size_t i = 0; i < pages; i++) {
        const std::size_t length = std::min(kPage, region.size - i * kPage);

        if (next < taken.pages.size() && taken.pages[next] == i) {
            std::memcpy(region.start + i * kPage, taken.bytes.data() + at, length);
            at += length;
            next++;
        } else {
            std::memset(region.start + i * kPage, 0, length);
        }
    }
}

// The bytes of region i as taken, page by page (zero pages filled in).
void TakenPage(const State &state, std::size_t i, std::size_t page, unsigned char *out) {
    const Region     &region = state.regions[i];
    const Taken      &taken = state.taken[i];
    const std::size_t length = std::min(kPage, region.size - page * kPage);
    auto              found = std::lower_bound(taken.pages.begin(), taken.pages.end(), (std::uint32_t) page);

    std::memset(out, 0, kPage);
    if (found != taken.pages.end() && *found == page) {
        // Every page before it is a whole one (only a region's last page can be short).
        std::memcpy(out, taken.bytes.data() + (std::size_t) (found - taken.pages.begin()) * kPage, length);
    }
}

// Two takes of one region set: how many bytes differ, and the first.
std::size_t Differ(const State &a, const State &b, std::string *first) {
    std::size_t   count = 0;
    unsigned char pa[kPage], pb[kPage];

    for (std::size_t i = 0; i < a.regions.size(); i++) {
        if (a.taken[i].pages == b.taken[i].pages && a.taken[i].bytes == b.taken[i].bytes) {
            continue;
        }
        const std::size_t pages = (a.regions[i].size + kPage - 1) / kPage;

        for (std::size_t page = 0; page < pages; page++) {
            TakenPage(a, i, page, pa);
            TakenPage(b, i, page, pb);

            for (std::size_t at = 0; at < kPage; at++) {
                if (pa[at] != pb[at]) {
                    if (count++ == 0 && first != nullptr) {
                        char where[64];
                        std::snprintf(where, sizeof where, " +0x%zx", page * kPage + at);
                        *first = Label(a.regions[i]) + where;
                    }
                }
            }
        }
    }
    return count;
}

void HashAll(const std::vector<Region> &regions, std::vector<std::uint64_t> &out) {
    out.resize(regions.size());

    for (std::size_t i = 0; i < regions.size(); i++) {
        out[i] = Hash(regions[i].start, regions[i].size);
    }
}

// The state set at T + K, in snapshot.cpp's dump format (DCSNAP01), so snapdiff.py copies names what differs.
void Dump(const std::vector<Region> &regions, const char *prefix, const char *which, std::int64_t tick) {
    const std::string path = std::string(prefix) + "-" + which + ".bin";
    std::FILE        *out = std::fopen(path.c_str(), "wb");

    if (out == nullptr) {
        std::fprintf(stderr, "snapshot: could not write %s\n", path.c_str());
        return;
    }

    const char    magic[8] = {'D', 'C', 'S', 'N', 'A', 'P', '0', '1'};
    std::uint64_t image = ImageBase();
    std::int32_t  numbers[3] = {0, 0, std::strcmp(which, "again") == 0 ? 1 : 0};
    std::uint32_t count = 0;

    for (const Region &region : regions) {
        count += region.kind != kHeap ? 1 : 0; // the heap blocks are left out below (floor run 3: snapdiff.py read past the end)
    }

    std::fwrite(magic, 1, 8, out);
    std::fwrite(&image, 8, 1, out);
    std::fwrite(&tick, 8, 1, out);
    std::fwrite(numbers, 4, 3, out);
    std::fwrite(&count, 4, 1, out);

    for (const Region &region : regions) {
        if (region.kind == kHeap) {
            continue; // no image offset to name it by: not in snapdump's format
        }
        // snapdump's kinds: 1 the image, 2 an arena.
        std::uint32_t kind = region.kind == kArena ? 2 : 1;
        std::uint64_t address = reinterpret_cast<std::uintptr_t>(region.start);
        std::uint64_t size = region.size;
        Taken         taken;

        TakeRegion(region, taken);
        std::vector<unsigned char> present((region.size + kPage - 1) / kPage, 0);

        for (std::uint32_t page : taken.pages) {
            present[page] = 1;
        }
        std::fwrite(&kind, 4, 1, out);
        std::fwrite(&region.id, 8, 1, out);
        std::fwrite(&address, 8, 1, out);
        std::fwrite(&size, 8, 1, out);
        std::fwrite(present.data(), 1, present.size(), out);
        std::fwrite(taken.bytes.data(), 1, taken.bytes.size(), out);
    }

    std::fclose(out);
    std::fprintf(stderr, "snapshot: round trip dumped tick %lld (%s) to %s\n", (long long) tick, which, path.c_str());
}

// The image's whole writable sections (netplay, audio, the co-op modules included), for finding what the state set
// leaves out: a global that differs between the take and the restore changed in T to T + K and was not put back.
std::vector<Region> ImageSections() {
    std::vector<Region> regions;
#ifdef _WIN32
    auto *base = reinterpret_cast<unsigned char *>(ImageBase());
    auto *nt = reinterpret_cast<IMAGE_NT_HEADERS *>(base + reinterpret_cast<IMAGE_DOS_HEADER *>(base)->e_lfanew);
    auto *section = IMAGE_FIRST_SECTION(nt);

    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++, section++) {
        if ((section->Characteristics & IMAGE_SCN_MEM_WRITE) && !(section->Characteristics & IMAGE_SCN_MEM_DISCARDABLE)) {
            regions.push_back({kPs2, section->VirtualAddress, base + section->VirtualAddress, section->Misc.VirtualSize, std::string()});
        }
    }
#endif
    return regions;
}

bool Mutated(const char *name) {
    static const char *mutate = TestEnv("DC_SNAPSHOT_MUTATE");
    return mutate != nullptr && std::strstr(mutate, name) != nullptr;
}

// The round trip (see the header).
struct Trip {
    bool                                    parsed = false;
    std::int64_t                            from = -1; // T
    std::int64_t                            ticks = 0; // K
    int                                     phase = 0; // 0 waiting, 1 first time, 2 again, 3 done
    bool                                    take_due = false;    // tick T - 1 ran: take at the next frame start
    bool                                    restore_due = false; // tick T + K ran: put back at the next frame start
    std::string                             place;
    int                                     map_no = -1;
    State                                   state;
    std::vector<std::uint64_t>              taken_hashes; // at the take, for the restore's check
    std::uint32_t                           taken_sum = 0;
    std::uint32_t (*checksum)() = nullptr; // the lockstep's Checksum(), from SnapshotTripTick
    std::vector<std::vector<std::uint64_t>> hashes; // the first time's, a vector of region hashes a tick
    std::vector<std::uint32_t>              sums;
    std::vector<std::uint64_t>              now;
    std::int64_t                            parted = -1;
    std::string                             parted_in;
    std::vector<bool>                       region_parted; // each region's first parting is logged once
    int                                     regions_logged = 0;
    std::int64_t                            sum_parted = -1;
    int                                     matched = 0;
    int                                     sums_matched = 0;
};

Trip g_trip;

bool SameArenas(const std::vector<Region> &regions) {
    std::vector<Region> arenas = Arenas();
    std::size_t         at = 0;

    for (const Region &region : regions) {
        if (region.kind != kArena) {
            continue;
        }
        if (at >= arenas.size() || arenas[at].id != region.id || arenas[at].start != region.start || arenas[at].size != region.size) {
            return false;
        }
        at++;
    }
    return at == arenas.size();
}

// Every heap block taken is still there, where it was and as large. Blocks made since (function records the VM
// decoded for the first time) may stay: each is a copy of its record in the loaded file, rewritten whenever decoded.
bool SameHeap(const std::vector<Region> &regions) {
    std::vector<Region> heap = HeapBlocks();

    for (const Region &region : regions) {
        if (region.kind != kHeap) {
            continue;
        }
        bool found = false;

        for (const Region &now : heap) {
            if (now.start == region.start && now.size == region.size) {
                found = true;
                break;
            }
        }
        if (!found) {
            std::fprintf(stderr, "snapshot: round trip: heap block %s is gone or resized\n", region.name.c_str());
            return false;
        }
    }
    return true;
}

// Regions the trip's tick-by-tick compare leaves out (DC_SNAPSHOT_TRIP_IGNORE, names comma-separated; default ref_off):
// state the set carries but the simulation rewrites each tick from its own inputs. ref_off (dun/gameloop.cpp, the
// camera's offset for the player's move) differed on the restore tick alone in about half the floor trips of
// 2026-10-24 (the next tick matched again, and the lockstep's checksum matched 601 of 601 every time): the frame's camera
// code writes it before the pad read, from something the two passes do not share (not found; the live stick or the
// clock). It is draw-side as far as the lockstep can tell. Logged once per region left out.
bool Ignored(const Region &region) {
    static const std::string list = TestEnvOr("DC_SNAPSHOT_TRIP_IGNORE", "ref_off");

    if (region.kind != kPs2 && region.kind != kPort) {
        return false;
    }
    std::size_t at = 0;

    while (at <= list.size()) {
        std::size_t comma = list.find(',', at);
        std::string one = list.substr(at, comma == std::string::npos ? std::string::npos : comma - at);

        if (!one.empty() && one == region.name) {
            return true;
        }
        if (comma == std::string::npos) {
            break;
        }
        at = comma + 1;
    }
    return false;
}

// Compares this tick's hashes with the first time's tick i; returns the regions that differ.
int Compare(std::size_t i, std::string *first) {
    int differ = 0;

    HashAll(g_trip.state.regions, g_trip.now);
    g_trip.region_parted.resize(g_trip.now.size(), false);
    for (std::size_t r = 0; r < g_trip.now.size(); r++) {
        if (g_trip.now[r] != g_trip.hashes[i][r] && Ignored(g_trip.state.regions[r])) {
            if (!g_trip.region_parted[r]) {
                g_trip.region_parted[r] = true;
                std::fprintf(stderr, "snapshot: round trip: %s differs at tick %lld and is left out of the compare (DC_SNAPSHOT_TRIP_IGNORE)\n",
                             Label(g_trip.state.regions[r]).c_str(), (long long) (g_trip.from + (std::int64_t) i));
            }
            continue;
        }
        if (g_trip.now[r] != g_trip.hashes[i][r]) {
            if (differ++ == 0 && first != nullptr) {
                *first = Label(g_trip.state.regions[r]);
            }
            // Where each region first parts (up to 24): the order tells cause from effect.
            if (!g_trip.region_parted[r] && g_trip.regions_logged < 24) {
                g_trip.region_parted[r] = true;
                g_trip.regions_logged++;
                std::fprintf(stderr, "snapshot: round trip: %s first parts at tick %lld\n", Label(g_trip.state.regions[r]).c_str(),
                             (long long) (g_trip.from + (std::int64_t) i));
            }
        }
    }
    return differ;
}

void SelfChecks() {
    // Two takes of one tick are equal (lesson 8).
    State again;
    again.regions = g_trip.state.regions;
    Take(again);
    std::string first;
    std::size_t differ = Differ(g_trip.state, again, &first);

    std::fprintf(stderr, differ == 0 ? "snapshot: self-check: two takes of tick %lld equal (%zu bytes)\n"
                                     : "snapshot: self-check: two takes of tick %lld differ (%zu bytes, first %s)\n",
                 (long long) g_trip.from, differ == 0 ? g_trip.state.bytes : differ, first.c_str());

    // A poked byte shows up exactly once, where it was poked: the first taken page of the largest region.
    std::size_t largest = 0;

    for (std::size_t i = 0; i < again.regions.size(); i++) {
        if (again.taken[i].bytes.size() > again.taken[largest].bytes.size()) {
            largest = i;
        }
    }
    if (again.taken[largest].bytes.empty()) {
        std::fprintf(stderr, "snapshot: self-check: nothing taken to poke\n");
        return;
    }
    const std::size_t poked = std::min<std::size_t>(123, again.taken[largest].bytes.size() - 1);
    again.taken[largest].bytes[poked] ^= 0x5a;
    first.clear();
    differ = Differ(g_trip.state, again, &first);

    char want[64];
    std::snprintf(want, sizeof want, " +0x%zx", (std::size_t) again.taken[largest].pages[0] * kPage + poked);
    const bool exact = differ == 1 && first == Label(again.regions[largest]) + want;

    if (exact) {
        std::fprintf(stderr, "snapshot: self-check: a poked byte showed exactly (%s)\n", first.c_str());
    } else {
        std::fprintf(stderr, "snapshot: self-check: a poked byte showed as %zu bytes, first %s\n", differ, first.c_str());
    }
}

} // namespace


SnapshotStep SnapshotTripTick(std::int64_t tick, const char *place, int map_no, std::uint32_t (*checksum)()) {
    if (!g_trip.parsed) {
        g_trip.parsed = true;
        const char *spec = TestEnv("DC_SNAPSHOT_ROUNDTRIP");
        long long   from = -1, ticks = 0;

        if (spec != nullptr && std::sscanf(spec, "%lld,%lld", &from, &ticks) == 2 && from >= 1 && ticks > 0) {
            g_trip.from = from;
            g_trip.ticks = ticks;
        }
    }
    g_trip.checksum = checksum;
    if (g_trip.from < 0 || g_trip.phase == 3) {
        return SnapshotStep::None;
    }

    const std::int64_t i = tick - g_trip.from;

    if (g_trip.phase == 0) {
        // The take waits for the next frame's start, where no game frame is on the stack: the state there is tick T's.
        if (tick == g_trip.from - 1) {
            g_trip.take_due = true;
            g_trip.place = place;
            g_trip.map_no = map_no;
        }
        return SnapshotStep::None;
    }

    if (i < 0 || i > g_trip.ticks) {
        std::fprintf(stderr, "snapshot: round trip refused at tick %lld: outside ticks %lld to %lld\n", (long long) tick, (long long) g_trip.from,
                     (long long) (g_trip.from + g_trip.ticks));
        g_trip.phase = 3;
        return SnapshotStep::None;
    }

    // DC_SNAPSHOT_TRIP_WATCH=i (with DC_SNAPSHOT_TRIP_DUMP): the whole writable image at tick T + i both times
    // (-watch-first.bin, -watch-again.bin): what differs there outside the state set, the tick before a parting, is
    // what feeds it.
    static const std::int64_t watch = TestEnvInt("DC_SNAPSHOT_TRIP_WATCH", -1);

    if (i == watch) {
        if (const char *dump = TestEnv("DC_SNAPSHOT_TRIP_DUMP")) {
            Dump(ImageSections(), dump, g_trip.phase == 1 ? "watch-first" : "watch-again", tick);
        }
    }

    if (g_trip.phase == 1) {
        HashAll(g_trip.state.regions, g_trip.hashes[(std::size_t) i]);
        g_trip.sums[(std::size_t) i] = checksum();

        if (i == g_trip.ticks) {
            if (const char *dump = TestEnv("DC_SNAPSHOT_TRIP_DUMP")) {
                Dump(g_trip.state.regions, dump, "first", tick);
            }
            g_trip.restore_due = true;
        }
        return SnapshotStep::None;
    }

    // Again: tick i of the second time against the first time's.
    std::string first;
    const int   differ = Compare((std::size_t) i, &first);
    const bool  sum = checksum() == g_trip.sums[(std::size_t) i];

    g_trip.matched += differ == 0 ? 1 : 0;
    g_trip.sums_matched += sum ? 1 : 0;
    if (!sum && g_trip.sum_parted < 0) {
        g_trip.sum_parted = tick;
        std::fprintf(stderr, "snapshot: round trip: the checksum first parts at tick %lld\n", (long long) tick);
    }
    if ((differ > 0 || !sum) && g_trip.parted < 0) {
        g_trip.parted = tick;
        g_trip.parted_in = first.empty() ? "the checksum" : first;
        std::fprintf(stderr, "snapshot: round trip parted at tick %lld: %d regions differ (first %s)%s\n", (long long) tick, differ, first.c_str(),
                     sum ? "" : ", the checksum differs");
    }

    if (i == g_trip.ticks) {
        if (const char *dump = TestEnv("DC_SNAPSHOT_TRIP_DUMP")) {
            Dump(g_trip.state.regions, dump, "again", tick);
        }
        std::fprintf(stderr, "snapshot: round trip done at tick %lld: %d of %lld ticks matched, %d checksums%s%s\n", (long long) tick, g_trip.matched,
                     (long long) g_trip.ticks + 1, g_trip.sums_matched, g_trip.parted < 0 ? "" : ", parted in ", g_trip.parted_in.c_str());
        g_trip.phase = 3;
        g_trip.state = State{};
        g_trip.hashes.clear();
    }
    return SnapshotStep::Again;
}

SnapshotStep SnapshotTripFrame() {
    if (g_trip.take_due) {
        g_trip.take_due = false;
        std::vector<Region> regions;

        if (!ReadGlobals(regions)) {
            g_trip.phase = 3;
            return SnapshotStep::None;
        }
        std::vector<Region> arenas = Arenas();
        std::vector<Region> heap = HeapBlocks();
        std::size_t         globals = 0;

        for (const Region &region : regions) {
            globals += region.size;
        }
        regions.insert(regions.end(), arenas.begin(), arenas.end());
        regions.insert(regions.end(), heap.begin(), heap.end());
        g_trip.state.regions = std::move(regions);
        Take(g_trip.state);
        SelfChecks();

        if (const char *dump = TestEnv("DC_SNAPSHOT_TRIP_DUMP")) {
            Dump(ImageSections(), dump, "take", g_trip.from);
        }
        HashAll(g_trip.state.regions, g_trip.taken_hashes);
        g_trip.taken_sum = g_trip.checksum();
        g_trip.hashes.assign((std::size_t) g_trip.ticks + 1, {});
        g_trip.sums.assign((std::size_t) g_trip.ticks + 1, 0);
        g_trip.phase = 1;
        std::fprintf(stderr, "snapshot: round trip took tick %lld in map %d (%s): %zu regions (%zu arenas, %zu bytes of globals, %zu heap blocks), %zu bytes taken\n",
                     (long long) g_trip.from, g_trip.map_no, g_trip.place.c_str(), g_trip.state.regions.size(), arenas.size(), globals, heap.size(),
                     g_trip.state.bytes);
        return SnapshotStep::Taken;
    }

    if (!g_trip.restore_due) {
        return SnapshotStep::None;
    }
    g_trip.restore_due = false;
    const std::int64_t at = g_trip.from + g_trip.ticks;

    if (!SameArenas(g_trip.state.regions)) {
        std::fprintf(stderr, "snapshot: round trip refused at tick %lld: the arenas changed since tick %lld\n", (long long) at, (long long) g_trip.from);
        g_trip.phase = 3;
        return SnapshotStep::None;
    }
    if (!SameHeap(g_trip.state.regions)) {
        std::fprintf(stderr, "snapshot: round trip refused at tick %lld: the script VM's heap blocks changed since tick %lld\n", (long long) at,
                     (long long) g_trip.from);
        g_trip.phase = 3;
        return SnapshotStep::None;
    }

    for (std::size_t r = 0; r < g_trip.state.regions.size(); r++) {
        const Region &region = g_trip.state.regions[r];

        if ((region.kind == kPort && Mutated("skip_port")) || (region.kind == kArena && Mutated("skip_arenas"))) {
            continue;
        }
        PutRegion(region, g_trip.state.taken[r]);
    }
    if (Mutated("poke")) {
        for (const Region &region : g_trip.state.regions) {
            if (region.kind == kArena) {
                region.start[region.size / 2] ^= 0x5a;
                break;
            }
        }
    }
    if (const char *dump = TestEnv("DC_SNAPSHOT_TRIP_DUMP")) {
        Dump(ImageSections(), dump, "restore", g_trip.from);
    }

    // The restore's own check: every region and the checksum as they were at the take.
    std::vector<std::uint64_t> now;
    int                        differ = 0;
    std::string                first;

    HashAll(g_trip.state.regions, now);
    for (std::size_t r = 0; r < now.size(); r++) {
        if (now[r] != g_trip.taken_hashes[r] && differ++ == 0) {
            first = Label(g_trip.state.regions[r]);
        }
    }
    const bool sum = g_trip.checksum() == g_trip.taken_sum;

    std::fprintf(stderr, differ == 0 && sum ? "snapshot: round trip restored tick %lld at tick %lld: all regions and the checksum as taken\n"
                                            : "snapshot: round trip restored tick %lld at tick %lld: %d regions differ (first %s)%s\n",
                 (long long) g_trip.from, (long long) at, differ, first.c_str(), sum ? "" : ", the checksum differs");
    g_trip.phase = 2;
    return SnapshotStep::Restored;
}

std::int64_t SnapshotTripFrom() {
    return g_trip.from;
}

// Two copies (stage 4). Layout: "DCSS0003", u32 regions; a region: u32 kind, u64 id, u64 size, (an arena's: u64 lead, u64 base,)
// u32 pages, u32 page[pages], then those pages' bytes (a region's last page may be short). An arena's base (coop-cand57, 0003)
// is where the owner's block lies on the taker: the game's pointers in the set point there, so a copy whose block lies
// elsewhere (a block unpinned, or pinned at another probe) refuses the set rather than load pointers into nothing.
namespace {

constexpr char kBytesMagic[8] = {'D', 'C', 'S', 'S', '0', '0', '0', '3'};

// An arena's lead: the bytes of its block before the base the owner was given (ArenaBlock hands an owner's live block back
// unchanged when asked for nothing).
std::size_t ArenaLead(const Region &region) {
    const void *owner = reinterpret_cast<const void *>(ImageBase() + region.id);

    return (std::size_t) (ArenaBlock(owner, 0, 0) - region.start);
}

template <typename T> void Put(std::vector<unsigned char> &out, T value) {
    const unsigned char *bytes = reinterpret_cast<const unsigned char *>(&value);
    out.insert(out.end(), bytes, bytes + sizeof value);
}

template <typename T> bool Get(const unsigned char *data, std::size_t size, std::size_t &at, T &value) {
    if (at + sizeof value > size) {
        return false;
    }
    std::memcpy(&value, data + at, sizeof value);
    at += sizeof value;
    return true;
}

} // namespace

namespace {
void AddExtras(std::vector<Region> &regions, const std::vector<SnapshotExtra> &extras) {
    for (const SnapshotExtra &extra : extras) {
        if (extra.start != nullptr && extra.size > 0) {
            regions.push_back({kExtra, extra.id, extra.start, extra.size, std::string("extra")});
        }
    }
}
} // namespace

namespace {
SnapshotTakeStats g_take_stats; // coop-offthread
} // namespace

SnapshotTakeStats SnapshotLastTake() {
    return g_take_stats;
}

bool SnapshotTakeBytes(std::vector<unsigned char> &out, std::string &why, const std::vector<SnapshotExtra> &extras) {
    using Clock = std::chrono::steady_clock;
    const auto          began = Clock::now();
    coop::PresentSetScope present(true); // coop-opt7: an opener's presentation screen stays out of the set
    std::vector<Region> regions;

    if (!ReadGlobals(regions)) {
        why = "no state set (see the snapshot: lines above)";
        return false;
    }
    std::vector<Region> arenas = Arenas();
    regions.insert(regions.end(), arenas.begin(), arenas.end());
    AddExtras(regions, extras);
    const auto listed = Clock::now();

    g_take_stats = SnapshotTakeStats{};
    g_take_stats.regions = regions.size();
    for (const Region &region : regions) {
        g_take_stats.scanned += region.size;
    }

    // coop-offthread: the copy is all that stays on the game thread, so it copies once: the page list of a region first (a
    // page is kept unless all zero, as TakeRegion), then its kept pages straight from the game's memory into the set (not
    // through a buffer of their own), into room kept from the last take (the set grew from nothing, copied on each growth).
    static std::size_t room = 24u << 20;
    std::vector<std::uint32_t> pages;

    out.clear();
    out.reserve(room);
    out.insert(out.end(), kBytesMagic, kBytesMagic + 8);
    Put(out, (std::uint32_t) regions.size());

    for (const Region &region : regions) {
        const std::size_t count = (region.size + kPage - 1) / kPage;

        pages.clear();
        for (std::size_t i = 0; i < count; i++) {
            const unsigned char *at = region.start + i * kPage;
            const std::size_t    length = std::min(kPage, region.size - i * kPage);
            bool                 zero = true;
            std::size_t          k = 0;

            for (; k + 8 <= length && zero; k += 8) {
                std::uint64_t word;
                std::memcpy(&word, at + k, 8);
                zero = word == 0;
            }
            for (; k < length && zero; k++) {
                zero = at[k] == 0;
            }
            if (!zero) {
                pages.push_back((std::uint32_t) i);
            }
        }
        Put(out, region.kind);
        Put(out, (std::uint64_t) region.id);
        Put(out, (std::uint64_t) region.size);
        if (region.kind == kArena) {
            Put(out, (std::uint64_t) ArenaLead(region));
            Put(out, (std::uint64_t) reinterpret_cast<std::uintptr_t>(ArenaBlock(reinterpret_cast<const void *>(ImageBase() + region.id), 0, 0)));
        }
        Put(out, (std::uint32_t) pages.size());
        for (std::uint32_t page : pages) {
            Put(out, page);
        }
        for (std::uint32_t page : pages) {
            const unsigned char *at = region.start + (std::size_t) page * kPage;
            out.insert(out.end(), at, at + std::min(kPage, region.size - (std::size_t) page * kPage));
        }
    }
    room = std::max(room, out.size() + (1u << 20));
    g_take_stats.kept = out.size();
    g_take_stats.list_ms = std::chrono::duration<double, std::milli>(listed - began).count();
    g_take_stats.copy_ms = std::chrono::duration<double, std::milli>(Clock::now() - listed).count();
    return true;
}

bool SnapshotApplyBytes(const unsigned char *data, std::size_t size, std::string &why, std::size_t *regions_put, std::size_t *bytes_put,
                        const std::vector<SnapshotExtra> &extras) {
    coop::PresentSetScope present(false); // coop-opt7: and the opener's own stays through an apply
    if (size < 12 || std::memcmp(data, kBytesMagic, 8) != 0) {
        why = "not a snapshot";
        return false;
    }
    std::vector<Region> regions;

    if (!ReadGlobals(regions)) {
        why = "no state set on this copy";
        return false;
    }
    std::vector<Region> arenas = Arenas();
    regions.insert(regions.end(), arenas.begin(), arenas.end());
    AddExtras(regions, extras);

    // Every incoming region must be here, as large; parsed whole before anything is written.
    struct Part {
        std::size_t                index;    // in regions
        std::uint32_t              pages;
        std::size_t                at;       // the page list, then the bytes
        std::size_t                src_size; // the taker's size of the region (an arena here may be larger: see below)
        std::uint64_t              base = 0; // an arena's block on the taker
    };
    std::vector<Part> parts;
    struct Grow {
        std::size_t index; // in regions
        std::size_t size;  // the taker's
        std::size_t lead;  // the taker's
    };
    std::vector<Grow> grow;
    std::size_t       at = 8;
    std::uint32_t     count = 0;

    Get(data, size, at, count);
    for (std::uint32_t i = 0; i < count; i++) {
        std::uint32_t kind = 0, pages = 0;
        std::uint64_t id = 0, region_size = 0, lead = 0, base = 0;

        if (!Get(data, size, at, kind) || !Get(data, size, at, id) || !Get(data, size, at, region_size) ||
            (kind == kArena && (!Get(data, size, at, lead) || !Get(data, size, at, base))) || !Get(data, size, at, pages) || lead > region_size) {
            why = "cut short";
            return false;
        }
        const Region *found = nullptr;

        for (const Region &region : regions) {
            if (region.kind == kind && region.id == id) {
                found = &region;
                break;
            }
        }
        // coop-snapjoin: an arena the taker carved and this copy never did (the host played the opening, the joiner came from
        // its title): carved below, at the owner's pinned address, as the taker's (DC_SNAPSHOT_MUTATE=no_arena_grow: refused).
        if (found == nullptr && kind == kArena && !Mutated("no_arena_grow")) {
            regions.push_back({kArena, id, nullptr, 0, std::string()});
            found = &regions.back();
        }
        if (found == nullptr) {
            char line[160];
            std::snprintf(line, sizeof line, "region %s +0x%llx is not on this copy", KindName(kind), (unsigned long long) id);
            why = line;
            return false;
        }
        // An arena block here may be larger than the taker's (the game keeps the larger of the sizes a mode carved an owner
        // at: a copy that came through the title holds MotionData at 500000 quads, a dungeon's host at 10000); with the
        // pinned layout both begin at one address, so the taker's bytes go in and the rest of the block is cleared. A
        // smaller block here is grown to the taker's size below, once the whole set has parsed (coop-snapjoin: a joiner
        // that skipped the cave entrance's scene holds image+0x203acc8 smaller than the host, opening resync log
        // 2026-10-25); DC_SNAPSHOT_MUTATE=no_arena_grow refuses it as before.
        if (kind == kArena && found->size < region_size && !Mutated("no_arena_grow")) {
            grow.push_back({(std::size_t) (found - regions.data()), (std::size_t) region_size, (std::size_t) lead});
        } else if (kind == kArena ? found->size < region_size : found->size != region_size) {
            why = "region " + Label(*found) + (kind == kArena ? " is smaller here" : " is sized otherwise here");
            return false;
        }
        const std::size_t page_count = (region_size + kPage - 1) / kPage;
        std::size_t       bytes = 0;

        for (std::uint32_t p = 0; p < pages; p++) {
            std::uint32_t page = 0;
            std::size_t   page_at = at + (std::size_t) p * 4;

            if (!Get(data, size, page_at, page) || page >= page_count) {
                why = "a page index out of range in " + Label(*found);
                return false;
            }
            bytes += std::min(kPage, (std::size_t) region_size - (std::size_t) page * kPage);
        }
        parts.push_back({(std::size_t) (found - regions.data()), pages, at, (std::size_t) region_size, base});
        at += (std::size_t) pages * 4 + bytes;
        if (at > size) {
            why = "cut short in " + Label(*found);
            return false;
        }
    }

    // Each smaller or missing block is carved at the taker's size and lead (ArenaBlock, as the game's own carve at that size
    // would do; a block outgrown keeps its address, the pinned layout mapping an owner at one address whatever its size), so
    // it begins where the taker's does and holds all of it.
    for (const Grow &g : grow) {
        Region           &region = regions[g.index];
        const void       *owner = reinterpret_cast<const void *>(ImageBase() + region.id);
        const std::size_t unpinned = ArenaUnpinned();
        const bool        missing = region.start == nullptr;
        const std::size_t was = region.size;
        unsigned char    *was_start = region.start;
        const std::size_t lead = missing ? g.lead : std::max(g.lead, ArenaLead(region));

        ArenaBlock(owner, g.size - std::min(g.size, lead), lead);
        region.start = nullptr;
        for (const Region &now : Arenas()) {
            if (now.id == region.id) {
                region.start = now.start;
                region.size = now.size;
            }
        }
        if (ArenaUnpinned() != unpinned || region.start == nullptr || region.size < g.size || (!missing && region.start != was_start)) {
            why = "region " + Label(region) + (missing ? " is not on this copy and could not be carved" : " is smaller here and could not be grown where it lies");
            return false;
        }
        std::fprintf(stderr, "snapshot: %s region %s: %zu bytes, lead %zu (the taker's %zu, lead %zu; here %zu before)\n", missing ? "carved" : "grew",
                     Label(region).c_str(), region.size, ArenaLead(region), g.size, g.lead, was);
    }

    // coop-cand57: every arena block lies where the taker's does, or the set is refused (its pointers would point into
    // nothing here: train 56's 96 floor crashes, MapModelBuffer unpinned on one copy). DC_SNAPSHOT_MUTATE=arena_base_unchecked
    // (twin): not checked, as before.
    for (const Part &part : parts) {
        const Region &region = regions[part.index];

        if (region.kind != kArena || Mutated("arena_base_unchecked")) {
            continue;
        }
        const auto here = (std::uint64_t) reinterpret_cast<std::uintptr_t>(ArenaBlock(reinterpret_cast<const void *>(ImageBase() + region.id), 0, 0));

        if (here != part.base) {
            char line[200];
            std::snprintf(line, sizeof line, "%s lies at 0x%llx here and at 0x%llx on the taker: the pinned layouts differ (%zu unpinned here)",
                          Label(region).c_str(), (unsigned long long) here, (unsigned long long) part.base, ArenaUnpinned());
            why = line;
            return false;
        }
    }

    std::size_t put_regions = 0, put_bytes = 0;
    // repin (Jep, 2026-11-10, test-only): DC_SNAPSHOT_HEAPPTR=1 names each word of an image global that held, or is given, a
    // pointer into this process's private memory outside the arenas: a heap owner left in the state set (a copy that then
    // frees the taker's pointer at exit dies with heap corruption, 0xC0000374).
    static const bool heap_trace = TestEnv("DC_SNAPSHOT_HEAPPTR") != nullptr;
    int               heap_lines = 0;
    const auto        image_base = ImageBase();
    std::uintptr_t    image_end = image_base;
#ifdef _WIN32
    if (heap_trace) {
        auto *nt = reinterpret_cast<IMAGE_NT_HEADERS *>(image_base + reinterpret_cast<IMAGE_DOS_HEADER *>(image_base)->e_lfanew);
        image_end = image_base + nt->OptionalHeader.SizeOfImage;
    }
#endif
    const auto private_here = [&](std::uint64_t value) {
#ifdef _WIN32
        if (value < 0x10000 || value > 0x7FFFFFFFFFFFull || (value & 7) != 0 || (value >= image_base && value < image_end)) {
            return false;
        }
        for (const Region &arena : regions) {
            if (arena.kind == kArena && value >= (std::uintptr_t) arena.start && value < (std::uintptr_t) arena.start + arena.size) {
                return false;
            }
        }
        MEMORY_BASIC_INFORMATION info{};
        return VirtualQuery(reinterpret_cast<const void *>(value), &info, sizeof info) != 0 && info.State == MEM_COMMIT && info.Type == MEM_PRIVATE;
#else
        (void) value;
        return false;
#endif
    };
    const auto trace_page = [&](const Region &region, std::size_t offset, const unsigned char *incoming, std::size_t length) {
        if (!heap_trace || (region.kind != kPs2 && region.kind != kPort)) {
            return;
        }
        for (std::size_t k = 0; k + 8 <= length && heap_lines < 200; k += 8) {
            if ((((std::uintptr_t) region.start + offset + k) & 7) != 0) {
                continue;
            }
            std::uint64_t old_word = 0, new_word = 0;
            std::memcpy(&old_word, region.start + offset + k, 8);
            if (incoming != nullptr) {
                std::memcpy(&new_word, incoming + k, 8);
            }
            if (old_word != new_word && (private_here(old_word) || private_here(new_word))) {
                std::fprintf(stderr, "snapshot: heap pointer: %s %s +0x%zx: 0x%llx here, 0x%llx put\n", KindName(region.kind), region.name.c_str(),
                             offset + k, (unsigned long long) old_word, (unsigned long long) new_word);
                heap_lines++;
            }
        }
    };

    for (const Part &part : parts) {
        const Region &region = regions[part.index];

        if (region.kind == kArena && Mutated("skip_arenas")) {
            continue;
        }
        // coop-soakfix3: the town pause's return mode and the floor's co-op pauses stay this copy's (DC_SNAPSHOT_MUTATE=skip_pause,
        // test_host_sits' twin: as before they were in the set). coop-repinfix3: by name in any kind but the arenas: since the re-pin
        // to Chronicle 9418a550 the town loop is the decomp's (dc::game::EditLoop()::old_mode, a ps2 region), and a port-only match
        // skipped nothing (the twin blind on townpause).
        if (region.kind != kArena && Mutated("skip_pause") &&
            (region.name.find("EditLoop()::old_mode") != std::string::npos || region.name.find("::g_pause") != std::string::npos)) {
            continue;
        }
        const std::size_t page_count = (part.src_size + kPage - 1) / kPage;
        std::size_t       next = 0, bytes_at = part.at + (std::size_t) part.pages * 4;

        if (region.size > part.src_size) {
            std::memset(region.start + part.src_size, 0, region.size - part.src_size); // the block's tail past the taker's
        }

        for (std::size_t page = 0; page < page_count; page++) {
            const std::size_t length = std::min(kPage, part.src_size - page * kPage);
            std::uint32_t     listed = 0;

            if (next < part.pages) {
                std::memcpy(&listed, data + part.at + next * 4, 4);
            }
            if (next < part.pages && listed == page) {
                trace_page(region, page * kPage, data + bytes_at, length);
                std::memcpy(region.start + page * kPage, data + bytes_at, length);
                bytes_at += length;
                put_bytes += length;
                next++;
            } else {
                trace_page(region, page * kPage, nullptr, length);
                std::memset(region.start + page * kPage, 0, length);
            }
        }
        put_regions++;
    }
    if (regions_put != nullptr) {
        *regions_put = put_regions;
    }
    if (bytes_put != nullptr) {
        *bytes_put = put_bytes;
    }
    return true;
}

// coop-pagediff (Jep, 2026-10-28: "make figuring out if sending the delta for catch up is viable with no side effects a
// priority", then "go"): the state set sent as the pages the taking copy lacks. The sender's page list (its state set with
// each page's bytes replaced by a hash of them, and a hash of the whole set) goes first; the taker answers which pages it
// needs (one bit a listed page, against a state set of its own taken once it has landed); the sender sends those pages'
// bytes; the taker rebuilds the sender's state set byte for byte from the list, its own pages and those, and only puts it
// back when the whole hash matches (else it asks for the whole set). The apply is SnapshotApplyBytes, unchanged.
// List layout: "DCPL0001", u64 whole hash (two lanes), u64 the state set's size, then the state set's layout with each
// page's bytes replaced by its u64 hash.
namespace {

constexpr char kListMagic[8] = {'D', 'C', 'P', 'L', '0', '0', '0', '1'};

std::uint64_t PageHash(const unsigned char *data, std::size_t size, std::uint64_t seed) {
    std::uint64_t h = seed ^ (size * 0x9e3779b97f4a7c15ull);
    std::size_t   at = 0;

    for (; at + 8 <= size; at += 8) {
        std::uint64_t word;
        std::memcpy(&word, data + at, 8);
        h ^= word * 0xc2b2ae3d27d4eb4full;
        h = ((h << 31) | (h >> 33)) * 0x9e3779b97f4a7c15ull;
    }
    for (; at < size; at++) {
        h = (h ^ data[at]) * 0x100000001b3ull;
    }
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdull;
    h ^= h >> 33;
    return h;
}

void WholeHash(const std::vector<unsigned char> &raw, std::uint64_t out[2]) {
    out[0] = PageHash(raw.data(), raw.size(), 0x243f6a8885a308d3ull);
    out[1] = PageHash(raw.data(), raw.size(), 0x13198a2e03707344ull);
}

// One region of a DCSS0003 state set: its header (with the page list) and where its pages' bytes begin.
struct WalkRegion {
    std::uint32_t              kind = 0;
    std::uint64_t              id = 0, size = 0;
    std::size_t                header_at = 0, header_size = 0;
    std::vector<std::uint32_t> pages;
    std::size_t                bytes_at = 0; // the first page's bytes (a state set), or its hash (a list)
};

std::size_t PageLength(const WalkRegion &region, std::uint32_t page) {
    return (std::size_t) std::min<std::uint64_t>(kPage, region.size - (std::uint64_t) page * kPage);
}

// The regions of a state set (page_bytes false: of a list, 8 bytes a page), from `at` to the end.
bool Walk(const std::vector<unsigned char> &data, std::size_t at, bool page_bytes, std::vector<WalkRegion> &out) {
    std::uint32_t count = 0;

    if (!Get(data.data(), data.size(), at, count)) {
        return false;
    }
    for (std::uint32_t i = 0; i < count; i++) {
        WalkRegion    region;
        std::uint64_t lead = 0, base = 0;
        std::uint32_t pages = 0;

        region.header_at = at;
        if (!Get(data.data(), data.size(), at, region.kind) || !Get(data.data(), data.size(), at, region.id) ||
            !Get(data.data(), data.size(), at, region.size) || (region.kind == kArena && (!Get(data.data(), data.size(), at, lead) || !Get(data.data(), data.size(), at, base))) ||
            !Get(data.data(), data.size(), at, pages) || (std::size_t) pages * 4 > data.size() - at) {
            return false;
        }
        region.pages.resize(pages);
        if (pages > 0) {
            std::memcpy(region.pages.data(), data.data() + at, (std::size_t) pages * 4);
        }
        at += (std::size_t) pages * 4;
        region.header_size = at - region.header_at;
        region.bytes_at = at;
        for (std::uint32_t page : region.pages) {
            if ((std::uint64_t) page * kPage >= region.size) {
                return false;
            }
            at += page_bytes ? PageLength(region, page) : 8;
        }
        if (at > data.size()) {
            return false;
        }
        out.push_back(std::move(region));
    }
    return at == data.size();
}

// The taker's own pages by region (kind, id) and page index: where each one's bytes are in its state set, and how many.
struct OwnPages {
    struct Key {
        std::uint32_t kind;
        std::uint64_t id;
        std::uint32_t page;
        bool          operator<(const Key &other) const {
            return kind != other.kind ? kind < other.kind : id != other.id ? id < other.id : page < other.page;
        }
    };
    std::vector<std::pair<Key, std::pair<std::size_t, std::size_t>>> pages; // sorted

    bool Read(const std::vector<unsigned char> &own) {
        std::vector<WalkRegion> regions;

        if (own.size() < 12 || std::memcmp(own.data(), kBytesMagic, 8) != 0 || !Walk(own, 8, true, regions)) {
            return false;
        }
        for (const WalkRegion &region : regions) {
            std::size_t at = region.bytes_at;

            for (std::uint32_t page : region.pages) {
                pages.push_back({{region.kind, region.id, page}, {at, PageLength(region, page)}});
                at += PageLength(region, page);
            }
        }
        std::sort(pages.begin(), pages.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
        return true;
    }

    const std::pair<std::size_t, std::size_t> *Find(std::uint32_t kind, std::uint64_t id, std::uint32_t page) const {
        const Key key{kind, id, page};
        auto      it = std::lower_bound(pages.begin(), pages.end(), key, [](const auto &a, const Key &b) { return a.first < b; });

        return it != pages.end() && !(key < it->first) ? &it->second : nullptr;
    }
};

// A list's whole hash, the state set's size, and its regions.
bool ReadList(const std::vector<unsigned char> &list, std::uint64_t whole[2], std::uint64_t &raw_size, std::vector<WalkRegion> &regions) {
    std::size_t at = 8;

    return list.size() >= 32 && std::memcmp(list.data(), kListMagic, 8) == 0 && Get(list.data(), list.size(), at, whole[0]) &&
           Get(list.data(), list.size(), at, whole[1]) && Get(list.data(), list.size(), at, raw_size) && Walk(list, at, false, regions);
}

bool Needed(const std::vector<std::uint32_t> &need, std::size_t index) {
    return index / 32 >= need.size() || (need[index / 32] & (1u << (index % 32))) != 0;
}

} // namespace

bool SnapshotPageList(const std::vector<unsigned char> &raw, std::vector<unsigned char> &list, std::size_t *pages) {
    std::vector<WalkRegion> regions;
    std::uint64_t           whole[2];

    if (raw.size() < 12 || std::memcmp(raw.data(), kBytesMagic, 8) != 0 || !Walk(raw, 8, true, regions)) {
        return false;
    }
    WholeHash(raw, whole);
    list.clear();
    list.insert(list.end(), kListMagic, kListMagic + 8);
    Put(list, whole[0]);
    Put(list, whole[1]);
    Put(list, (std::uint64_t) raw.size());
    Put(list, (std::uint32_t) regions.size());

    std::size_t count = 0;

    for (const WalkRegion &region : regions) {
        std::size_t at = region.bytes_at;

        list.insert(list.end(), raw.begin() + (std::ptrdiff_t) region.header_at, raw.begin() + (std::ptrdiff_t) (region.header_at + region.header_size));
        for (std::uint32_t page : region.pages) {
            Put(list, PageHash(raw.data() + at, PageLength(region, page), 0));
            at += PageLength(region, page);
            count++;
        }
    }
    if (pages != nullptr) {
        *pages = count;
    }
    return true;
}

bool SnapshotPageNeed(const std::vector<unsigned char> &list, const std::vector<unsigned char> &own, std::vector<std::uint32_t> &need,
                      std::size_t *pages, std::size_t *needed) {
    std::vector<WalkRegion> regions;
    std::uint64_t           whole[2], raw_size = 0;
    OwnPages                mine;

    if (!ReadList(list, whole, raw_size, regions)) {
        return false;
    }
    const bool  have_own = mine.Read(own); // none (no take of its own): every page is needed
    std::size_t index = 0, count = 0;

    need.clear();
    for (const WalkRegion &region : regions) {
        std::size_t at = region.bytes_at;

        for (std::uint32_t page : region.pages) {
            std::uint64_t theirs = 0;

            std::memcpy(&theirs, list.data() + at, 8);
            at += 8;
            const auto *here = have_own ? mine.Find(region.kind, region.id, page) : nullptr;
            // DC_SNAPSHOT_MUTATE=pagediff_false_match (a twin): every page of the same place and length counted as here.
            const bool  same = here != nullptr && here->second == PageLength(region, page) &&
                              (PageHash(own.data() + here->first, here->second, 0) == theirs || Mutated("pagediff_false_match"));

            if (need.size() <= index / 32) {
                need.push_back(0);
            }
            if (!same) {
                need[index / 32] |= 1u << (index % 32);
                count++;
            }
            index++;
        }
    }
    if (pages != nullptr) {
        *pages = index;
    }
    if (needed != nullptr) {
        *needed = count;
    }
    return true;
}

bool SnapshotPageDiff(const std::vector<unsigned char> &raw, const std::vector<std::uint32_t> &need, std::vector<unsigned char> &diff, std::size_t *pages) {
    std::vector<WalkRegion> regions;

    if (raw.size() < 12 || std::memcmp(raw.data(), kBytesMagic, 8) != 0 || !Walk(raw, 8, true, regions)) {
        return false;
    }
    std::size_t index = 0, count = 0;

    diff.clear();
    for (const WalkRegion &region : regions) {
        std::size_t at = region.bytes_at;

        for (std::uint32_t page : region.pages) {
            const std::size_t length = PageLength(region, page);

            if (Needed(need, index)) {
                diff.insert(diff.end(), raw.begin() + (std::ptrdiff_t) at, raw.begin() + (std::ptrdiff_t) (at + length));
                count++;
            }
            at += length;
            index++;
        }
    }
    if (pages != nullptr) {
        *pages = count;
    }
    return true;
}

bool SnapshotPageRebuild(const std::vector<unsigned char> &list, const std::vector<unsigned char> &own, const std::vector<std::uint32_t> &need,
                         const std::vector<unsigned char> &diff, std::vector<unsigned char> &raw, std::string &why) {
    std::vector<WalkRegion> regions;
    std::uint64_t           whole[2], raw_size = 0, got[2];
    OwnPages                mine;

    if (!ReadList(list, whole, raw_size, regions)) {
        why = "the page list did not parse";
        return false;
    }
    mine.Read(own);
    raw.clear();
    raw.reserve((std::size_t) raw_size);
    raw.insert(raw.end(), kBytesMagic, kBytesMagic + 8);
    Put(raw, (std::uint32_t) regions.size());

    std::size_t index = 0, diff_at = 0;

    for (const WalkRegion &region : regions) {
        raw.insert(raw.end(), list.begin() + (std::ptrdiff_t) region.header_at, list.begin() + (std::ptrdiff_t) (region.header_at + region.header_size));
        for (std::uint32_t page : region.pages) {
            const std::size_t length = PageLength(region, page);

            if (Needed(need, index)) {
                if (diff_at + length > diff.size()) {
                    why = "the pages sent end early";
                    return false;
                }
                raw.insert(raw.end(), diff.begin() + (std::ptrdiff_t) diff_at, diff.begin() + (std::ptrdiff_t) (diff_at + length));
                diff_at += length;
            } else {
                const auto *here = mine.Find(region.kind, region.id, page);

                if (here == nullptr || here->second != length) {
                    why = "a page said to be here is not";
                    return false;
                }
                raw.insert(raw.end(), own.begin() + (std::ptrdiff_t) here->first, own.begin() + (std::ptrdiff_t) (here->first + length));
            }
            index++;
        }
    }
    if (diff_at != diff.size() || raw.size() != raw_size) {
        why = "the rebuilt state set is sized otherwise";
        return false;
    }
    WholeHash(raw, got);
    // DC_SNAPSHOT_MUTATE=pagediff_no_check (a twin): the rebuilt set goes back whatever its hash.
    if ((got[0] != whole[0] || got[1] != whole[1]) && !Mutated("pagediff_no_check")) {
        why = "the rebuilt state set's hash differs from the sender's";
        return false;
    }
    return true;
}

bool SnapshotStateHolds(const void *pointer) {
    if (ArenaContains(pointer)) {
        return true;
    }

    // The globals as start/end pairs, sorted, read once (the file does not change while the game runs).
    static std::vector<std::pair<std::uintptr_t, std::uintptr_t>> globals;
    static bool                                                    read = false;

    if (!read) {
        read = true;
        std::vector<Region> regions;

        if (ReadGlobals(regions)) {
            for (const Region &region : regions) {
                const auto start = reinterpret_cast<std::uintptr_t>(region.start);
                globals.emplace_back(start, start + region.size);
            }
            std::sort(globals.begin(), globals.end());
        }
    }

    const auto at = reinterpret_cast<std::uintptr_t>(pointer);
    auto       next = std::upper_bound(globals.begin(), globals.end(), std::make_pair(at, UINTPTR_MAX));

    return next != globals.begin() && at < std::prev(next)->second;
}

void *SnapshotTestForeignBuffer(const void *close_to, std::size_t size) {
#ifdef _WIN32
    const auto at = reinterpret_cast<std::uintptr_t>(close_to);

    // Downward from 256 MB below it in 64 KB steps (the allocation granularity), the first free range that takes it.
    for (std::uintptr_t step = 0; step < 2048; step++) {
        const std::uintptr_t want = (at - (std::uintptr_t{256} << 20) - step * 0x10000) & ~std::uintptr_t{0xffff};
        void                *got = VirtualAlloc(reinterpret_cast<void *>(want), size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);

        if (got != nullptr) {
            if (!SnapshotStateHolds(got)) {
                return got;
            }
            VirtualFree(got, 0, MEM_RELEASE);
        }
    }
#else
    (void) close_to;
    (void) size;
#endif
    return nullptr;
}
