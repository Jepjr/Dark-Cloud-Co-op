#include "coop/game_ns.hpp"
#include "coop/netplay_slots.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <utility>

#include "coop/coop.hpp"
#include "coop/netplay_test.hpp"
#include "coop/snapshot.hpp"
#include "platform/paths.hpp"
#include "save/save_slots.hpp"

namespace fs = std::filesystem;

namespace {

fs::path g_root;

void Put32(std::vector<unsigned char> &out, std::uint32_t value) {
    for (int i = 0; i < 4; i++) {
        out.push_back((unsigned char) (value >> (8 * i)));
    }
}

bool Get32(const unsigned char *&at, const unsigned char *end, std::uint32_t &value) {
    if (end - at < 4) {
        return false;
    }

    value = (std::uint32_t) at[0] | (std::uint32_t) at[1] << 8 | (std::uint32_t) at[2] << 16 | (std::uint32_t) at[3] << 24;
    at += 4;
    return true;
}

} // namespace

std::vector<unsigned char> NetplaySlotsTake(bool for_state) {
    std::vector<unsigned char>                                 out;
    std::vector<fs::path>                                      files;
    std::vector<std::pair<std::string, std::vector<char>>>     entries;
    std::error_code                                            error;
    fs::path                                                   root = dc::save::SaveSlotsRoot();

    for (fs::recursive_directory_iterator it(root, error), end; !error && it != end; it.increment(error)) {
        if (it->is_regular_file(error)) {
            files.push_back(it->path());
        }
    }

    std::ranges::sort(files);
    for (const fs::path &file : files) {
        std::ifstream in(file, std::ios::binary);

        entries.emplace_back(fs::relative(file, root, error).generic_string(), std::vector<char>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()));
    }

    // coop-joinstuck (tests), the state's slots alone (for_state), never the slots' hash (NetplaySlotsHash, which would part the copies'
    // saves): DC_NETPLAY_TEST_SLOTS_COPIES=N carries the save's slots N times (copies under joinstuck-test-copyK/, no slot of the
    // menus), as a host with N times the slots (Jep's 24); DC_NETPLAY_TEST_SLOTS_PAD_KB=N adds a file of N KB that packs to nothing
    // (xorshift bytes, the same each time), so a test's checkpoint passes one blob's 2 MB as Jep's 24 slots raw did.
    if (for_state) {
        const std::size_t own = entries.size();

        for (int k = 1; k < TestEnvInt("DC_NETPLAY_TEST_SLOTS_COPIES", 1); k++) {
            for (std::size_t i = 0; i < own; i++) {
                entries.emplace_back("joinstuck-test-copy" + std::to_string(k) + "/" + entries[i].first, entries[i].second);
            }
        }
        if (const int pad_kb = TestEnvInt("DC_NETPLAY_TEST_SLOTS_PAD_KB", 0); pad_kb > 0) {
            std::vector<char> pad((std::size_t) pad_kb * 1024);
            std::uint32_t     x = 0x2545F491u;

            for (char &byte : pad) {
                x ^= x << 13;
                x ^= x >> 17;
                x ^= x << 5;
                byte = (char) (x >> 24);
            }
            entries.emplace_back("joinstuck-test-pad.bin", std::move(pad));
        }
    }

    Put32(out, (std::uint32_t) entries.size());
    for (const auto &[name, bytes] : entries) {
        Put32(out, (std::uint32_t) name.size());
        out.insert(out.end(), name.begin(), name.end());
        Put32(out, (std::uint32_t) bytes.size());
        out.insert(out.end(), bytes.begin(), bytes.end());
    }

    if (for_state && !NetplayMutated("slots_raw")) {
        std::vector<unsigned char> small;

        SnapshotPack(out, small);
        std::fprintf(stderr, "netplay: the state's save slots: %zu files, %zu bytes, packed to %zu\n", entries.size(), out.size(), small.size());
        return small;
    }
    if (for_state) {
        std::fprintf(stderr, "netplay: the state's save slots: %zu files, %zu bytes, packed to %zu (the twin: not packed)\n", entries.size(), out.size(),
                     out.size());
    }
    return out;
}

bool NetplaySlotsPut(const unsigned char *data, std::size_t size) {
    std::vector<unsigned char> unpacked; // coop-joinstuck: packed slots (NetplaySlotsTake(true)), or raw

    if (const std::vector<unsigned char> in(data, data + size); SnapshotIsPacked(in)) {
        if (!SnapshotUnpack(in, unpacked)) {
            return false;
        }
        data = unpacked.data();
        size = unpacked.size();
    }

    const unsigned char *at = data;
    const unsigned char *end = data + size;
    std::uint32_t        count;
    fs::path             root = PathsSaveRoot() / "netplay-host-saves";
    std::error_code      error;

    if (!Get32(at, end, count)) {
        return false;
    }

    fs::remove_all(root, error);
    fs::create_directories(root, error);

    for (std::uint32_t i = 0; i < count; i++) {
        std::uint32_t length;
        std::uint32_t bytes;

        if (!Get32(at, end, length) || (std::size_t) (end - at) < length) {
            return false;
        }

        std::string name((const char *) at, length);
        at += length;

        if (!Get32(at, end, bytes) || (std::size_t) (end - at) < bytes || name.find("..") != std::string::npos) {
            return false;
        }

        fs::path path = root / fs::path(name);
        fs::create_directories(path.parent_path(), error);
        std::ofstream out(path, std::ios::binary);
        out.write((const char *) at, bytes);
        at += bytes;
    }

    g_root = root;
    return true;
}

fs::path NetplaySlotsRoot() {
    return g_root;
}

std::uint64_t NetplaySlotsHash() {
    std::uint64_t              hash = 14695981039346656037ull;
    std::vector<unsigned char> slots = NetplaySlotsTake();

    for (unsigned char byte : slots) {
        hash = (hash ^ byte) * 1099511628211ull;
    }

    return hash;
}

std::string NetplaySlotsList() {
    std::string out;

    for (int file_no : dc::save::SaveSlotFiles()) {
        out += (out.empty() ? "" : " ") + std::to_string(file_no + 1);
    }

    return out.empty() ? "none" : out;
}
