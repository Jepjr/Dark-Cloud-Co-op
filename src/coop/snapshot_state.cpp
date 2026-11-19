#include "coop/game_ns.hpp"
#include "coop/snapshot_state.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
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
bool NetplayMutated(const char *name); // netplay_test.cpp (coop-p2tick: apply_one_thread)

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
        // (and DC_COOP_MUTATE=merge_menu_retail or scriptmenu_merge_retail: retail's menu stepped in the lockstep through the merge, which the taker can only
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
        // coop-soundset: DC_SNAPSHOT_MUTATE=sound_in_set (twin): the special sound set (coop_ownlook.cpp), which snapshot-allow.txt took
        // out, put back in, as before.
        if (Mutated("sound_in_set")) {
            struct Add {
                std::vector<Region> *list;
            } add{&fresh};

            coop::OwnSoundRegionsAll(
                [](void *at, std::size_t size, void *context) {
                    auto *into = static_cast<Add *>(context)->list;
                    auto *start = static_cast<unsigned char *>(at);

                    into->push_back({kPs2, reinterpret_cast<std::uintptr_t>(start) - ImageBase(), start, size, std::string("sound")});
                },
                &add);
        }
        // coop-wave4: DC_SNAPSHOT_MUTATE=look_in_set (twin): the message look (coop_ownlook.cpp), which snapshot-allow.txt took out,
        // put back in, as before.
        if (Mutated("look_in_set")) {
            struct Add {
                std::vector<Region> *list;
            } add{&fresh};

            coop::OwnLookRegionsAll(
                [](void *at, std::size_t size, void *context) {
                    auto *into = static_cast<Add *>(context)->list;
                    auto *start = static_cast<unsigned char *>(at);

                    into->push_back({kPs2, reinterpret_cast<std::uintptr_t>(start) - ImageBase(), start, size, std::string("look")});
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

// jointake: a page of game memory all zero (64 bytes a branch).
bool PageZero(const unsigned char *at, std::size_t length) {
    bool        zero = true;
    std::size_t k = 0;

    for (; k + 64 <= length && zero; k += 64) {
        std::uint64_t w[8];
        std::memcpy(w, at + k, 64);
        zero = (w[0] | w[1] | w[2] | w[3] | w[4] | w[5] | w[6] | w[7]) == 0;
    }
    for (; k < length && zero; k++) {
        zero = at[k] == 0;
    }
    return zero;
}

#ifdef _WIN32
std::uintptr_t OsPage() {
    static const std::uintptr_t page = [] {
        SYSTEM_INFO info;
        GetSystemInfo(&info);
        return (std::uintptr_t) info.dwPageSize;
    }();
    return page;
}
#endif

// jointake: the arena blocks are mapped write-watched (recipe K008). Reset: the watch of the OS pages under [start, start + size)
// starts again (true when it could). Else: marks (1) in written the range's pages (kPage each, from start) that hold an OS
// page the watch names (written since its last reset), 0 the rest; false (written left as it was) where it cannot say.
bool ArenaWatch(const unsigned char *start_at, std::size_t size, unsigned char *written, std::size_t count, bool reset) {
#ifdef _WIN32
    const std::uintptr_t os_page = OsPage();
    const auto           start = reinterpret_cast<std::uintptr_t>(start_at);
    const std::uintptr_t from = start & ~(os_page - 1);
    const std::uintptr_t to = (start + size + os_page - 1) & ~(os_page - 1);

    if (to <= from) {
        return false;
    }
    if (reset) {
        return ResetWriteWatch(reinterpret_cast<void *>(from), to - from) == 0;
    }
    thread_local std::vector<void *> named;
    ULONG_PTR                        got = (to - from) / os_page;
    DWORD                            granularity = 0;

    named.resize(got);
    if (GetWriteWatch(0, reinterpret_cast<void *>(from), to - from, named.data(), &got, &granularity) != 0 || granularity == 0) {
        return false;
    }
    std::memset(written, 0, count);
    for (ULONG_PTR k = 0; k < got; k++) {
        const auto at = reinterpret_cast<std::uintptr_t>(named[k]);
        const auto lo = std::max(at, start) - start;
        const auto hi = std::min<std::uintptr_t>(at + granularity, start + size) - start;
        if (hi <= lo) {
            continue;
        }
        for (std::size_t i = lo / kPage; i <= (hi - 1) / kPage && i < count; i++) {
            written[i] = 1;
        }
    }
    return true;
#else
    (void) start_at;
    (void) size;
    (void) written;
    (void) count;
    (void) reset;
    return false;
#endif
}

// jointake: the take's preparation, made on threads of its own from the take's checkpoint on (SnapshotTakePrepare): each
// arena's watch reset, then its pages tested; and the room for the set, its pages touched. A take after it tests an arena's
// page only when the preparation found it not zero or the watch names it written since (a write after the reset is always
// named, so a page untested is still zero: the same bytes as testing every page).
struct TakePrep {
    struct Arena {
        const unsigned char       *start = nullptr;
        std::size_t                size = 0;
        bool                       watched = false;
        std::vector<unsigned char> nonzero; // a kPage each
    };
    std::vector<Arena>         arenas;
    std::vector<unsigned char> room;
    std::size_t                room_size = 0;
    std::atomic<bool>          done{false};
    double                     ms = 0.0;
    double                     touch_ms = 0.0;    // coop-firsttick: the reads before the reset
    unsigned                   generation = 0;    // coop-firsttick: its watch resets' number (PrepCurrent)
    std::atomic<unsigned>      touch_sum{0};
};
std::shared_ptr<TakePrep> g_prep;
std::size_t               g_room = 24u << 20; // the set's room (the largest set yet and 1 MB)
constexpr unsigned        kScanThreads = 4;

unsigned TakeHelpers() {
    return std::min(kScanThreads, std::max(1u, std::thread::hardware_concurrency()) - 1);
}
} // namespace

namespace {
// The preparation's work (SnapshotTakePrepare, SnapshotApplyPrepare), on threads of its own: each arena's pages read first, then
// its watch reset, then its pages tested.
// coop-firsttick (P2's first play tick 254-296 ms after P1's, l-lobby-handed's join as played): every take said "prepared ahead"
// yet tested every arena page (311 MB, 14-16 ms of the taker's game thread; 396 MB, 11-18 ms at every stayer's merge in the
// rows' logs): the watch named 99% of the arenas' pages written since its reset, though they were still zero. An arena page
// never touched since the block was mapped (or zeroed: ArenaMemoryZero decommits and commits again) is a demand-zero page, and
// Windows' write watch names a demand-zero page written once it is first brought in, even by a read: the preparation's own
// zero test, after the reset, marked every untouched page it read. Each page is now read once before the reset (brought in
// then, so the reset clears that mark; the test after it reads pages already in). The watch's meaning is unchanged (a write
// after the reset is still named), so the set is the same bytes. DC_SNAPSHOT_MUTATE=prep_touch_after (twin): no read before
// the reset, as before (the take tests every arena page again).
// coop-firsttick: the arenas' watch has one reset for all its users (the take's preparation and the apply's): a preparation counts
// only while no other has reset the watch since its own (a write between the two resets is named by neither). Its user holds
// g_watch_reset_lock from that check through its own reads of the watch, so no reset comes between them.
std::mutex            g_watch_reset_lock;
std::atomic<unsigned> g_watch_resets{0};

bool PrepCurrent(const std::shared_ptr<TakePrep> &prep) {
    return prep && prep->done.load(std::memory_order_acquire) && prep->generation == g_watch_resets.load();
}

void PrepRun(const std::shared_ptr<TakePrep> &prep, std::int64_t delay_ms) {
    if (delay_ms > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
    }
    const auto start = std::chrono::steady_clock::now();
    std::vector<std::pair<std::size_t, std::size_t>> chunks; // (arena, its first page), 1 MB each
    for (std::size_t a = 0; a < prep->arenas.size(); a++) {
        TakePrep::Arena &arena = prep->arenas[a];
        arena.nonzero.assign((arena.size + kPage - 1) / kPage, 1);
        for (std::size_t i = 0; i < arena.nonzero.size(); i += 256) {
            chunks.emplace_back(a, i);
        }
    }
    auto on_two = [](auto &fn) {
        std::thread other([&fn] { fn(); }); // (two threads: the stayer's game goes on beside them)
        fn();
        other.join();
    };
    if (!Mutated("prep_touch_after")) {
        std::atomic<std::size_t> next{0};
        auto                     touch = [&]() {
            unsigned char sum = 0;
            for (std::size_t c = next.fetch_add(1); c < chunks.size(); c = next.fetch_add(1)) {
                const TakePrep::Arena &arena = prep->arenas[chunks[c].first];
                for (std::size_t i = chunks[c].second; i < std::min(arena.nonzero.size(), chunks[c].second + 256); i++) {
                    sum = (unsigned char) (sum + *(volatile const unsigned char *) (arena.start + i * kPage));
                }
            }
            prep->touch_sum.fetch_add(sum, std::memory_order_relaxed); // (kept, so the reads stay)
        };
        on_two(touch);
    }
    prep->touch_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    {
        std::lock_guard<std::mutex> lock(g_watch_reset_lock);
        for (TakePrep::Arena &arena : prep->arenas) {
            arena.watched = ArenaWatch(arena.start, arena.size, nullptr, 0, true);
        }
        prep->generation = ++g_watch_resets;
    }
    // (after every reset: a page written after its arena's reset is named at the take, whatever this read of it saw)
    std::atomic<std::size_t> next{0};
    auto                     scan = [&]() {
        for (std::size_t c = next.fetch_add(1); c < chunks.size(); c = next.fetch_add(1)) {
            TakePrep::Arena &arena = prep->arenas[chunks[c].first];
            if (!arena.watched) {
                continue;
            }
            for (std::size_t i = chunks[c].second; i < std::min(arena.nonzero.size(), chunks[c].second + 256); i++) {
                arena.nonzero[i] = PageZero(arena.start + i * kPage, std::min(kPage, arena.size - i * kPage)) ? 0 : 1;
            }
        }
    };
    on_two(scan);
    prep->room.resize(prep->room_size); // (its pages touched here, not at the take)
    prep->ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    prep->done.store(true, std::memory_order_release);
}

std::shared_ptr<TakePrep> PrepStart(std::size_t room_size, std::int64_t delay_ms) {
    auto prep = std::make_shared<TakePrep>();

    for (const Region &region : Arenas()) {
        TakePrep::Arena arena;
        arena.start = region.start;
        arena.size = region.size;
        prep->arenas.push_back(std::move(arena));
    }
    prep->room_size = room_size;
    std::thread([prep, delay_ms] { PrepRun(prep, delay_ms); }).detach();
    return prep;
}

std::shared_ptr<TakePrep> g_apply_prep; // coop-firsttick: SnapshotApplyPrepare's, for the next apply
} // namespace

static SnapshotApplyStats g_apply_stats;

SnapshotApplyStats SnapshotLastApply() {
    return g_apply_stats;
}

void SnapshotTakePrepare() {
    g_prep.reset(); // (a preparation not taken up is let go: its thread ends on its own copy)
    if (Mutated("no_write_watch") || Mutated("scan_one_thread")) {
        return;
    }
#ifdef _WIN32
    // coop-takeprep (tests): DC_SNAPSHOT_TEST_PREP_DELAY_MS=N: the preparation starts N ms late, as on a loaded PC (x-gate-flow
    // -Returns -PrepDelay): the take must wait for it, not go on unprepared.
    static const std::int64_t delay_ms = TestEnvInt("DC_SNAPSHOT_TEST_PREP_DELAY_MS", 0);
    g_prep = PrepStart(g_room, delay_ms);
#endif
}

// coop-firsttick: the taker's side. Her apply put every page of the set's 365 MB, its bytes or zeros (35-51 ms, all but 15 MB
// zeros), on a copy black till it lands. The arenas are prepared as a take's are, while the set's parts come: an arena page
// zero then and not written since is zero still, so the apply leaves it as it is (the same bytes as putting zeros).
// DC_NETPLAY_MUTATE=apply_zeros_all (twin, netplay's): no preparation, every zero put, as before.
void SnapshotApplyPrepare() {
    g_apply_prep.reset();
    if (Mutated("no_write_watch")) {
        return;
    }
#ifdef _WIN32
    g_apply_prep = PrepStart(0, 0);
#endif
}

bool SnapshotTakePrepHeld() {
    return g_prep != nullptr;
}

bool SnapshotTakePrepDone() {
    return !g_prep || g_prep->done.load(std::memory_order_acquire);
}

SnapshotTakeStats SnapshotLastTake() {
    return g_take_stats;
}

namespace {
template <typename T> void PutAt(std::vector<unsigned char> &out, std::size_t &at, T value) {
    std::memcpy(out.data() + at, &value, sizeof value);
    at += sizeof value;
}
} // namespace

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
    // through a buffer of their own).
    // cand1112c (j-joinload-rejoin-town, Jep's rule: a join never affects the player there): the zero test read every byte of
    // the set's regions (283 MB, 15 MB kept) on the game thread, 23-31 ms of one tick, so the stayer's frame took two ticks at a
    // merge (worst tick 34.4 ms). The pages are tested on helper threads (kScanThreads) while the game thread waits for them, so
    // the set is still the one tick's state, 64 bytes a branch. DC_SNAPSHOT_MUTATE=scan_one_thread (twin): the game thread
    // alone, 8 bytes a branch, as before.
    // jointake (Jep 11-12, j-joinload-town: the take still held the stayer's game thread 16-17 ms, over a tick under load: the
    // zero test 8.5 ms reading 283 MB, of which the arenas' 226 MB; the set's layout 2 ms, most of it zeroing fresh room; the
    // kept pages' copy 1.5 ms): the arenas are tested ahead, on threads of their own, from the take's checkpoint on
    // (SnapshotTakePrepare: each arena's write watch reset, then its pages tested), so the take tests an arena page only when
    // that found it not zero or the watch names it written since; the globals, the heap and the extras are tested as before.
    // The set goes into room touched ahead too (no zeroing), its layout written in place, the kept pages copied on the helper
    // threads. The set is the same bytes. DC_SNAPSHOT_MUTATE=no_write_watch (twin): no preparation, every page tested at the
    // take and the copy on the game thread alone, as before. DC_SNAPSHOT_TEST_WATCH_CHECK=1 (rows): the untested pages are
    // tested too, after, and any not zero is said (and kept); that check's time is said apart and left out of the copy's.
    const bool                one_thread = Mutated("scan_one_thread");
    const bool                watch = !one_thread && !Mutated("no_write_watch");
    std::unique_lock<std::mutex> watch_lock(g_watch_reset_lock); // (coop-firsttick: no reset till the watch is read, below)
    std::shared_ptr<TakePrep>    prep = watch && PrepCurrent(g_prep) ? g_prep : nullptr;
    g_take_stats.prepared = prep != nullptr;
    g_take_stats.prep_ms = prep ? prep->ms : 0.0;
    g_prep.reset(); // (once: the next take's checkpoint prepares again)

    std::vector<std::size_t> first(regions.size() + 1, 0); // each region's first page in nonzero
    for (std::size_t r = 0; r < regions.size(); r++) {
        first[r + 1] = first[r] + (regions[r].size + kPage - 1) / kPage;
    }
    std::vector<unsigned char> nonzero(first.back(), 0);
    std::vector<unsigned char> test(first.back(), 1); // jointake: 0 for an arena page found zero ahead and not written since
    if (prep) {
        const auto                 watch_start = Clock::now();
        std::vector<unsigned char> written;
        for (std::size_t r = 0; r < regions.size(); r++) {
            if (regions[r].kind != kArena) {
                continue;
            }
            const std::size_t count = first[r + 1] - first[r];
            for (const TakePrep::Arena &arena : prep->arenas) {
                if (arena.start != regions[r].start || arena.size != regions[r].size || !arena.watched || arena.nonzero.size() != count) {
                    continue;
                }
                written.assign(count, 1);
                if (ArenaWatch(regions[r].start, regions[r].size, written.data(), count, false)) {
                    for (std::size_t i = 0; i < count; i++) {
                        test[first[r] + i] = (written[i] | arena.nonzero[i]) != 0 ? 1 : 0;
                    }
                    g_take_stats.watched += regions[r].size;
                }
                break;
            }
        }
        g_take_stats.watch_ms = std::chrono::duration<double, std::milli>(Clock::now() - watch_start).count();
    }
    watch_lock.unlock();
    const unsigned helpers = one_thread ? 0 : TakeHelpers();
    // fn on the helper threads and on the game thread, until each has run out of work.
    auto on_helpers = [helpers](auto &fn, bool alone) {
        std::vector<std::thread> threads;
        for (unsigned h = 0; !alone && h < helpers; h++) {
            threads.emplace_back([&fn] { fn(); });
        }
        fn(); // (the game thread works too)
        for (std::thread &thread : threads) {
            thread.join();
        }
    };
    auto page_zero = [&](std::size_t r, std::size_t i) {
        const Region        &region = regions[r];
        const unsigned char *at = region.start + i * kPage;
        const std::size_t    length = std::min(kPage, region.size - i * kPage);

        if (!one_thread) {
            return PageZero(at, length);
        }
        bool        zero = true; // (the twin's: 8 bytes a branch, as before)
        std::size_t k = 0;
        for (; k + 8 <= length && zero; k += 8) {
            std::uint64_t word;
            std::memcpy(&word, at + k, 8);
            zero = word == 0;
        }
        for (; k < length && zero; k++) {
            zero = at[k] == 0;
        }
        return zero;
    };
    const auto scan_start = Clock::now();
    {
        constexpr std::size_t                            kChunkPages = 256; // 1 MB a piece of work
        std::vector<std::pair<std::size_t, std::size_t>> chunks;            // (region, its first page)
        for (std::size_t r = 0; r < regions.size(); r++) {
            for (std::size_t i = 0; i < first[r + 1] - first[r]; i += kChunkPages) {
                chunks.emplace_back(r, i);
            }
        }
        std::atomic<std::size_t> next{0};
        auto                     scan = [&]() {
            for (std::size_t c = next.fetch_add(1); c < chunks.size(); c = next.fetch_add(1)) {
                const std::size_t r = chunks[c].first;
                const std::size_t count = first[r + 1] - first[r];
                for (std::size_t i = chunks[c].second; i < std::min(count, chunks[c].second + kChunkPages); i++) {
                    if (test[first[r] + i] != 0) {
                        nonzero[first[r] + i] = page_zero(r, i) ? 0 : 1;
                    }
                }
            }
        };
        on_helpers(scan, one_thread);
        g_take_stats.scan_threads = helpers + 1;
    }
    for (std::size_t r = 0; r < regions.size(); r++) {
        for (std::size_t i = 0; i < first[r + 1] - first[r]; i++) {
            if (test[first[r] + i] != 0) {
                g_take_stats.tested += std::min(kPage, regions[r].size - i * kPage);
            }
        }
    }
    g_take_stats.scan_ms = std::chrono::duration<double, std::milli>(Clock::now() - scan_start).count();
    static const bool check = TestEnv("DC_SNAPSHOT_TEST_WATCH_CHECK") != nullptr;
    double            check_ms = 0.0;
    if (check && prep) {
        const auto check_start = Clock::now();
        long long  missed = 0;
        for (std::size_t r = 0; r < regions.size(); r++) {
            for (std::size_t i = 0; i < first[r + 1] - first[r]; i++) {
                if (test[first[r] + i] == 0 && !PageZero(regions[r].start + i * kPage, std::min(kPage, regions[r].size - i * kPage))) {
                    nonzero[first[r] + i] = 1; // (kept: the set stays right)
                    if (missed++ == 0) {
                        std::fprintf(stderr, "snapshot: write watch check: %s page %zu untested, yet not zero\n", Label(regions[r]).c_str(), i);
                    }
                }
            }
        }
        g_take_stats.check_missed = missed;
        check_ms = std::chrono::duration<double, std::milli>(Clock::now() - check_start).count();
        g_take_stats.check_ms = check_ms;
    }

    // The set's layout: its size first, then the headers and page lists written in place (small, on the game thread), then the
    // kept pages copied into their places.
    const auto emit_start = Clock::now();
    struct Copy {
        std::size_t          to;
        const unsigned char *from;
        std::size_t          length;
    };
    std::vector<Copy>          copies;
    std::vector<std::uint32_t> kept_count(regions.size(), 0);
    std::size_t                total = 8 + 4;
    for (std::size_t r = 0; r < regions.size(); r++) {
        for (std::size_t i = 0; i < first[r + 1] - first[r]; i++) {
            kept_count[r] += nonzero[first[r] + i];
        }
        total += 4 + 8 + 8 + (regions[r].kind == kArena ? 16 : 0) + 4 + 4 * (std::size_t) kept_count[r];
        for (std::size_t i = 0; i < first[r + 1] - first[r]; i++) {
            if (nonzero[first[r] + i] != 0) {
                const std::size_t length = std::min(kPage, regions[r].size - i * kPage);
                copies.push_back({total, regions[r].start + i * kPage, length});
                total += length;
            }
        }
    }
    if (prep && prep->room.size() >= total) {
        out.swap(prep->room); // (touched ahead: shrinking it writes nothing)
        out.resize(total);
    } else {
        out.clear();
        out.resize(total);
    }
    std::size_t at = 0;
    std::memcpy(out.data(), kBytesMagic, 8);
    at = 8;
    PutAt(out, at, (std::uint32_t) regions.size());
    std::size_t copy_index = 0;
    for (std::size_t r = 0; r < regions.size(); r++) {
        const Region &region = regions[r];

        PutAt(out, at, region.kind);
        PutAt(out, at, (std::uint64_t) region.id);
        PutAt(out, at, (std::uint64_t) region.size);
        if (region.kind == kArena) {
            PutAt(out, at, (std::uint64_t) ArenaLead(region));
            PutAt(out, at, (std::uint64_t) reinterpret_cast<std::uintptr_t>(ArenaBlock(reinterpret_cast<const void *>(ImageBase() + region.id), 0, 0)));
        }
        PutAt(out, at, kept_count[r]);
        for (std::size_t i = 0; i < first[r + 1] - first[r]; i++) {
            if (nonzero[first[r] + i] != 0) {
                PutAt(out, at, (std::uint32_t) i);
            }
        }
        for (std::uint32_t k = 0; k < kept_count[r]; k++) {
            at += copies[copy_index++].length;
        }
    }
    const double layout_ms = std::chrono::duration<double, std::milli>(Clock::now() - emit_start).count();
    {
        constexpr std::size_t    kCopyPages = 64;
        std::atomic<std::size_t> next{0};
        auto                     copy = [&]() {
            for (std::size_t c = next.fetch_add(kCopyPages); c < copies.size(); c = next.fetch_add(kCopyPages)) {
                for (std::size_t k = c; k < std::min(copies.size(), c + kCopyPages); k++) {
                    std::memcpy(out.data() + copies[k].to, copies[k].from, copies[k].length);
                }
            }
        };
        on_helpers(copy, !watch); // (the twins': on the game thread alone, as before)
    }
    g_room = std::max(g_room, total + (1u << 20));
    g_take_stats.layout_ms = layout_ms;
    g_take_stats.emit_ms = std::chrono::duration<double, std::milli>(Clock::now() - emit_start).count();
    g_take_stats.kept = out.size();
    g_take_stats.list_ms = std::chrono::duration<double, std::milli>(listed - began).count();
    g_take_stats.copy_ms = std::chrono::duration<double, std::milli>(Clock::now() - listed).count() - check_ms;
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

    // coop-p2tick (l-lobby-handed: her apply 141-158 ms of the 357-474 ms from his first play tick to hers, 113 ms of it this
    // loop): every page of the 365 MB the set covers is put, its bytes or zeros, and all but 15 MB are zeros, one page at a time
    // on the game thread. The pages go now in runs of 1 MB on the take's helper threads and the game thread (each run its own
    // pages and its own bytes, found from the part's page list: the same bytes put). Not with DC_SNAPSHOT_HEAPPTR (its trace
    // counts its lines). DC_NETPLAY_MUTATE=apply_one_thread (twin): the game thread alone, as before.
    if (!heap_trace && !NetplayMutated("apply_one_thread")) {
        // coop-firsttick (SnapshotApplyPrepare): per region, an arena's pages known zero (zero at the preparation, not written
        // since). Only a preparation that is done and of this very block (its start and size) counts; none, and every zero is put.
        std::vector<std::vector<unsigned char>> known_zero(regions.size());
        std::unique_lock<std::mutex>            watch_lock(g_watch_reset_lock); // (no reset till the watch is read, below)
        std::shared_ptr<TakePrep>               prep = PrepCurrent(g_apply_prep) ? g_apply_prep : nullptr;
        std::atomic<std::size_t>                left_zero{0};
        std::atomic<long long>                  left_missed{0};
        static const bool                       watch_check = TestEnv("DC_SNAPSHOT_TEST_WATCH_CHECK") != nullptr;

        g_apply_prep.reset(); // (once: the next apply prepares again)
        g_apply_stats = SnapshotApplyStats{};
        g_apply_stats.prepared = prep != nullptr;
        for (std::size_t r = 0; prep && r < regions.size(); r++) {
            const Region &region = regions[r];
            const std::size_t count = (region.size + kPage - 1) / kPage;

            for (const TakePrep::Arena &arena : prep->arenas) {
                if (region.kind != kArena || arena.start != region.start || arena.size != region.size || !arena.watched || arena.nonzero.size() != count) {
                    continue;
                }
                std::vector<unsigned char> written(count, 1);
                if (ArenaWatch(region.start, region.size, written.data(), count, false)) {
                    known_zero[r].assign(count, 0);
                    for (std::size_t i = 0; i < count; i++) {
                        known_zero[r][i] = (written[i] | arena.nonzero[i]) == 0 ? 1 : 0;
                    }
                }
                break;
            }
        }
        watch_lock.unlock();
        struct Run {
            std::size_t part;        // in parts
            std::size_t first, end;  // pages
        };
        std::vector<Run> runs;
        constexpr std::size_t kRunPages = 256; // (1 MB)

        for (std::size_t q = 0; q < parts.size(); q++) {
            const Part   &part = parts[q];
            const Region &region = regions[part.index];

            if (region.kind == kArena && Mutated("skip_arenas")) {
                continue;
            }
            if (region.kind != kArena && Mutated("skip_pause") &&
                (region.name.find("EditLoop()::old_mode") != std::string::npos || region.name.find("::g_pause") != std::string::npos)) {
                continue;
            }
            const std::size_t page_count = (part.src_size + kPage - 1) / kPage;

            for (std::size_t first = 0; first < page_count; first += kRunPages) {
                runs.push_back(Run{q, first, std::min(page_count, first + kRunPages)});
            }
            if (page_count == 0) {
                runs.push_back(Run{q, 0, 0}); // (the tail alone)
            }
            put_regions++;
        }
        std::atomic<std::size_t> next_run{0}, bytes_total{0};
        auto                     work = [&]() {
            std::size_t mine = 0;

            for (std::size_t r = next_run.fetch_add(1); r < runs.size(); r = next_run.fetch_add(1)) {
                const Run    &run = runs[r];
                const Part   &part = parts[run.part];
                const Region &region = regions[part.index];
                const unsigned char *list = data + part.at;
                auto                 listed_at = [&](std::size_t k) {
                    std::uint32_t v = 0;
                    std::memcpy(&v, list + k * 4, 4);
                    return (std::size_t) v;
                };
                // (the first listed page at or past the run's first: the list is in page order)
                std::size_t lo = 0, hi = part.pages;
                while (lo < hi) {
                    const std::size_t mid = (lo + hi) / 2;
                    if (listed_at(mid) < run.first) {
                        lo = mid + 1;
                    } else {
                        hi = mid;
                    }
                }
                std::size_t next = lo;
                std::size_t bytes_at = part.at + (std::size_t) part.pages * 4 + next * kPage; // (only a region's last page is short)

                if (run.first == 0 && region.size > part.src_size) {
                    std::memset(region.start + part.src_size, 0, region.size - part.src_size); // the block's tail past the taker's
                }
                for (std::size_t page = run.first; page < run.end; page++) {
                    const std::size_t length = std::min(kPage, part.src_size - page * kPage);

                    if (next < part.pages && listed_at(next) == page) {
                        std::memcpy(region.start + page * kPage, data + bytes_at, length);
                        bytes_at += length;
                        mine += length;
                        next++;
                    } else if (!known_zero[part.index].empty() && known_zero[part.index][page] != 0) {
                        left_zero.fetch_add(length, std::memory_order_relaxed); // (zero already: left as it is)
                        // (rows, DC_SNAPSHOT_TEST_WATCH_CHECK: each page left tested after all; one not zero is said, and put)
                        if (watch_check && !PageZero(region.start + page * kPage, length)) {
                            if (left_missed.fetch_add(1) == 0) {
                                std::fprintf(stderr, "snapshot: write watch check: %s page %zu left as zero, yet not zero\n", Label(region).c_str(), page);
                            }
                            std::memset(region.start + page * kPage, 0, length);
                        }
                    } else {
                        std::memset(region.start + page * kPage, 0, length);
                    }
                }
            }
            bytes_total.fetch_add(mine);
        };
        std::vector<std::thread> threads;

        for (unsigned h = 0; h < TakeHelpers(); h++) {
            threads.emplace_back(work);
        }
        work(); // (the game thread works too)
        for (std::thread &thread : threads) {
            thread.join();
        }
        put_bytes = bytes_total.load();
        g_apply_stats.left_zero = left_zero.load();
        if (watch_check && prep) {
            std::fprintf(stderr, "snapshot: write watch check at the apply: %lld pages left as zero not zero\n", left_missed.load());
        }
        if (regions_put != nullptr) {
            *regions_put = put_regions;
        }
        if (bytes_put != nullptr) {
            *bytes_put = put_bytes;
        }
        return true;
    }

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
