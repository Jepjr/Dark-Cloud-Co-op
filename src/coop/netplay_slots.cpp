#include "coop/game_ns.hpp"
#include "coop/netplay_slots.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <string>

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

std::vector<unsigned char> NetplaySlotsTake() {
    std::vector<unsigned char> out;
    std::vector<fs::path>      files;
    std::error_code            error;
    fs::path                   root = dc::save::SaveSlotsRoot();

    for (fs::recursive_directory_iterator it(root, error), end; !error && it != end; it.increment(error)) {
        if (it->is_regular_file(error)) {
            files.push_back(it->path());
        }
    }

    std::ranges::sort(files);
    Put32(out, (std::uint32_t) files.size());

    for (const fs::path &file : files) {
        std::string       name = fs::relative(file, root, error).generic_string();
        std::ifstream     in(file, std::ios::binary);
        std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

        Put32(out, (std::uint32_t) name.size());
        out.insert(out.end(), name.begin(), name.end());
        Put32(out, (std::uint32_t) bytes.size());
        out.insert(out.end(), bytes.begin(), bytes.end());
    }

    return out;
}

bool NetplaySlotsPut(const unsigned char *data, std::size_t size) {
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
