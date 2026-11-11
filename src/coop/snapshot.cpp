#include "coop/game_ns.hpp"
#include "coop/snapshot.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "coop/coop.hpp"
#include "platform/arena.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace {

constexpr std::size_t   kPage = 4096;
constexpr std::uint32_t kImageRegion = 1;
constexpr std::uint32_t kArenaRegion = 2;

std::uintptr_t ImageBase() {
#ifdef _WIN32
    return reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
#else
    return 0;
#endif
}

// Before main, so the first arena is already pinned. DC_FIXED_LAYOUT builds only: elsewhere the image moves each run
// and owner-keyed addresses would not match another copy's anyway.
struct Pin {
    Pin() {
#ifdef DC_FIXED_LAYOUT
        const char *mutate = std::getenv("DC_SNAPSHOT_MUTATE");

        if (mutate == nullptr || std::strstr(mutate, "unpinned") == nullptr) {
            ArenaPinLayout(ImageBase());
        }
        // coop-cand57: the build's pin plan (<exe>.pinplan, build\pinplan.py from the link map: "<owner offset hex> <probe>" a
        // line), so two owners that hash to one slot get theirs whichever carves first. DC_SNAPSHOT_MUTATE=pin_noprobe (twin):
        // no plan and no probing, the old fallback (the second owner unpinned).
        if (mutate != nullptr && std::strstr(mutate, "pin_noprobe") != nullptr) {
            ArenaPinProbes(1);
            return;
        }
        char exe[MAX_PATH];
        const DWORD n = GetModuleFileNameA(nullptr, exe, MAX_PATH);
        std::string path(exe, n);
        if (const std::size_t dot = path.rfind('.'); n > 0 && dot != std::string::npos) {
            path = path.substr(0, dot) + ".pinplan";
            if (std::FILE *f = std::fopen(path.c_str(), "r")) {
                char line[256];
                while (std::fgets(line, sizeof line, f) != nullptr) {
                    unsigned long long offset = 0;
                    int                probe = 0;
                    if (line[0] != '#' && std::sscanf(line, "%llx %d", &offset, &probe) == 2) {
                        ArenaPinPlan(static_cast<std::uintptr_t>(offset), probe);
                    }
                }
                std::fclose(f);
            }
        }
#endif
    }
} g_pin;

struct Region {
    std::uint32_t  kind;
    std::uint64_t  id; // the section's offset in the image, or the arena owner's
    unsigned char *start;
    std::size_t    size;
};

void PutRegion(std::FILE *out, const Region &region) {
    std::uint64_t address = reinterpret_cast<std::uintptr_t>(region.start);
    std::uint64_t size = region.size;
    std::size_t   pages = (region.size + kPage - 1) / kPage;
    std::vector<unsigned char> present(pages, 0);
    static const unsigned char zero[kPage] = {};

    for (std::size_t i = 0; i < pages; i++) {
        std::size_t length = std::min(kPage, region.size - i * kPage);
        present[i] = std::memcmp(region.start + i * kPage, zero, length) != 0 ? 1 : 0;
    }

    std::fwrite(&region.kind, 4, 1, out);
    std::fwrite(&region.id, 8, 1, out);
    std::fwrite(&address, 8, 1, out);
    std::fwrite(&size, 8, 1, out);
    std::fwrite(present.data(), 1, pages, out);

    for (std::size_t i = 0; i < pages; i++) {
        if (present[i]) {
            std::fwrite(region.start + i * kPage, 1, std::min(kPage, region.size - i * kPage), out);
        }
    }
}

std::vector<Region> Regions() {
    std::vector<Region> regions;
#ifdef _WIN32
    auto *base = reinterpret_cast<unsigned char *>(ImageBase());
    auto *dos = reinterpret_cast<IMAGE_DOS_HEADER *>(base);
    auto *nt = reinterpret_cast<IMAGE_NT_HEADERS *>(base + dos->e_lfanew);
    auto *section = IMAGE_FIRST_SECTION(nt);

    // The writable sections: .data with the .bss after it, .tls and the like. Not the code or the constants.
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++, section++) {
        if ((section->Characteristics & IMAGE_SCN_MEM_WRITE) && !(section->Characteristics & IMAGE_SCN_MEM_DISCARDABLE)) {
            regions.push_back({kImageRegion, section->VirtualAddress, base + section->VirtualAddress, section->Misc.VirtualSize});
        }
    }
#endif
    ArenaForEach(
        [](const void *owner, unsigned char *start, std::size_t size, void *context) {
            auto *list = static_cast<std::vector<Region> *>(context);
            list->push_back({kArenaRegion, reinterpret_cast<std::uintptr_t>(owner) - ImageBase(), start, size});
        },
        &regions);
    return regions;
}

bool Wanted(std::int64_t tick) {
    static const std::string ticks = TestEnvOr("DC_SNAPSHOT_DUMP_TICKS", "0,600");
    std::size_t              at = 0;

    while (at <= ticks.size()) {
        std::size_t comma = ticks.find(',', at);
        std::string one = ticks.substr(at, comma == std::string::npos ? std::string::npos : comma - at);

        if (!one.empty() && std::atoll(one.c_str()) == tick) {
            return true;
        }
        if (comma == std::string::npos) {
            break;
        }
        at = comma + 1;
    }
    return false;
}

} // namespace

bool SnapshotLayoutPinned() {
#ifdef DC_FIXED_LAYOUT
    static const bool unpinned_twin = std::getenv("DC_SNAPSHOT_MUTATE") != nullptr && std::strstr(std::getenv("DC_SNAPSHOT_MUTATE"), "unpinned") != nullptr;
    return !unpinned_twin && ArenaUnpinned() == 0;
#else
    return false;
#endif
}

void SnapshotDumpTick(std::int64_t tick, int at_mode, int map_no, bool replay) {
    static const char *prefix = TestEnv("DC_SNAPSHOT_DUMP");

    if (prefix == nullptr || !Wanted(tick)) {
        return;
    }
    SnapshotDumpTo(std::string(prefix) + "-t" + std::to_string((long long) tick) + ".bin", tick, at_mode, map_no, replay);
}

void SnapshotDumpTo(const std::string &path, std::int64_t tick, int at_mode, int map_no, bool replay) {
    std::FILE          *out = std::fopen(path.c_str(), "wb");
    std::vector<Region> regions = Regions();

    if (out == nullptr) {
        std::fprintf(stderr, "snapshot: could not write %s\n", path.c_str());
        return;
    }

    const char    magic[8] = {'D', 'C', 'S', 'N', 'A', 'P', '0', '1'};
    std::uint64_t image = ImageBase();
    std::int64_t  at_tick = tick;
    std::int32_t  numbers[3] = {map_no, at_mode, replay ? 1 : 0};
    std::uint32_t count = (std::uint32_t) regions.size();
    std::size_t   bytes = 0;

    std::fwrite(magic, 1, 8, out);
    std::fwrite(&image, 8, 1, out);
    std::fwrite(&at_tick, 8, 1, out);
    std::fwrite(numbers, 4, 3, out);
    std::fwrite(&count, 4, 1, out);

    for (const Region &region : regions) {
        PutRegion(out, region);
        bytes += region.size;
    }

    std::fclose(out);
    std::fprintf(stderr, "snapshot: dumped tick %lld in map %d (%s): image 0x%llx, %zu regions, %zu bytes, %zu arena blocks unpinned, to %s\n",
                 (long long) tick, map_no, replay ? "replay" : "recording", (unsigned long long) image, regions.size(), bytes, ArenaUnpinned(),
                 path.c_str());
}

// coop-localfix2: the state set's packing (snapshot.hpp). Tokens: a literal count, the literals, a match length (0 at the
// end) and, for a match, its distance back; counts and lengths as 7-bit varints, the distance 3 bytes. Matches of 4 bytes
// or more found through a hash of the next 4 bytes, the scan stepping faster through bytes that do not repeat.
namespace {
constexpr unsigned char kPackMagic[4] = {'L', 'Z', 'C', '1'};
constexpr std::size_t   kPackWindow = (1u << 24) - 1;

void PutVar(std::vector<unsigned char> &out, std::size_t value) {
    while (value >= 0x80) {
        out.push_back((unsigned char) (value | 0x80));
        value >>= 7;
    }
    out.push_back((unsigned char) value);
}

bool GetVar(const unsigned char *&at, const unsigned char *end, std::size_t &value) {
    value = 0;
    for (int shift = 0; shift < 64; shift += 7) {
        if (at >= end) {
            return false;
        }
        const unsigned char byte = *at++;
        value |= (std::size_t) (byte & 0x7F) << shift;
        if ((byte & 0x80) == 0) {
            return true;
        }
    }
    return false;
}

std::uint32_t Read32(const unsigned char *p) {
    std::uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}
} // namespace

void SnapshotPack(const std::vector<unsigned char> &in, std::vector<unsigned char> &out) {
    const std::size_t n = in.size();
    const unsigned char *src = in.data();
    std::vector<std::uint32_t> table(1u << 16, 0xFFFFFFFFu);

    out.clear();
    out.reserve(n / 2 + 64);
    out.insert(out.end(), kPackMagic, kPackMagic + 4);
    PutVar(out, n);

    std::size_t anchor = 0;
    std::size_t i = 0;
    std::size_t misses = 0;

    auto emit = [&](std::size_t literal_end, std::size_t length, std::size_t distance) {
        PutVar(out, literal_end - anchor);
        out.insert(out.end(), src + anchor, src + literal_end);
        PutVar(out, length);
        if (length != 0) {
            out.push_back((unsigned char) distance);
            out.push_back((unsigned char) (distance >> 8));
            out.push_back((unsigned char) (distance >> 16));
        }
    };

    while (n >= 4 && i + 4 <= n) {
        const std::uint32_t word = Read32(src + i);
        const std::uint32_t slot = (word * 2654435761u) >> 16;
        const std::uint32_t cand = table[slot];

        table[slot] = (std::uint32_t) i;
        if (cand != 0xFFFFFFFFu && i - cand <= kPackWindow && Read32(src + cand) == word) {
            std::size_t length = 4;

            while (i + length < n && src[cand + length] == src[i + length]) {
                length++;
            }
            emit(i, length, i - cand);
            i += length;
            anchor = i;
            misses = 0;
        } else {
            i += 1 + (misses++ >> 6);
        }
    }
    emit(n, 0, 0);
}

bool SnapshotIsPacked(const std::vector<unsigned char> &in) {
    return in.size() >= 4 && std::memcmp(in.data(), kPackMagic, 4) == 0;
}

bool SnapshotUnpack(const std::vector<unsigned char> &in, std::vector<unsigned char> &out) {
    if (!SnapshotIsPacked(in)) {
        out = in;
        return true;
    }

    const unsigned char *at = in.data() + 4;
    const unsigned char *end = in.data() + in.size();
    std::size_t          size = 0;

    if (!GetVar(at, end, size) || size > (std::size_t) 1 << 31) {
        return false;
    }
    out.assign(size, 0);

    std::size_t pos = 0;

    for (;;) {
        std::size_t literals = 0;
        std::size_t length = 0;

        if (!GetVar(at, end, literals) || literals > (std::size_t) (end - at) || pos + literals > size) {
            return false;
        }
        std::memcpy(out.data() + pos, at, literals);
        at += literals;
        pos += literals;
        if (!GetVar(at, end, length)) {
            return false;
        }
        if (length == 0) {
            return pos == size && at == end;
        }
        if (end - at < 3) {
            return false;
        }
        const std::size_t distance = (std::size_t) at[0] | (std::size_t) at[1] << 8 | (std::size_t) at[2] << 16;

        at += 3;
        if (distance == 0 || distance > pos || pos + length > size) {
            return false;
        }
        for (std::size_t k = 0; k < length; k++, pos++) { // (a distance under the length repeats what it copies)
            out[pos] = out[pos - distance];
        }
    }
}
