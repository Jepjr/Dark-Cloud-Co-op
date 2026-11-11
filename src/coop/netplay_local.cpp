// Parse JSON before including game headers, which define single-letter macros.
#include "coop/game_ns.hpp"
#include <nlohmann/json.hpp>

#include "coop/netplay_local.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "coop/netplay_test.hpp" // coop-p2config: NetplayMutated
#include "platform/paths.hpp"

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#endif

namespace {

// Player 2's own keys: her window and its title, her controller and her copy's link settings (the Local spawn sets the link).
bool OwnKey(const std::string &section, const std::string &key) {
    if (section == "game") {
        return key == "window_title" || key == "coop" || key.rfind("netplay", 0) == 0;
    }
    return key == "fullscreen" || key == "width" || key == "height" || key == "window_left" || key == "window_top";
}

// Player 2's folder: local-p2 under this copy's save folder. Its config is made from this one's (the this-PC script's
// settings: a window of 960x720 on the right, coop on, its own title). coop-p2config (Jep's 2026-10-28 run: the aerial map
// on for player 1 and off for player 2): on every Local start, player 1's game and display options (config "game" and
// "video") are copied again over hers, so a choice he made since her folder was first made reaches her too; her own keys
// (OwnKey) and her other sections (input, audio) stay hers. His config is only read. DC_NETPLAY_MUTATE=local_config_once
// (twin): made once and never again, as before.
std::filesystem::path PrepareFolder() {
    const std::filesystem::path folder = PathsSaveRoot() / "local-p2";
    const std::filesystem::path config = folder / "config.json";
    const bool                  fresh = !std::filesystem::exists(config);
    std::error_code             error;

    std::filesystem::create_directories(folder, error);
    if (!fresh && NetplayMutated("local_config_once")) {
        return folder;
    }

    auto read = [](const std::filesystem::path &path) {
        std::ifstream  in(path);
        nlohmann::json root = in ? nlohmann::json::parse(in, nullptr, false) : nlohmann::json::object();

        return root.is_discarded() || !root.is_object() ? nlohmann::json::object() : root;
    };
    const nlohmann::json his = read(PathsSaveRoot() / "config.json");
    nlohmann::json       root = fresh ? his : read(config);
    int                  copied = 0;

    if (fresh) {
        root["game"]["window_title"] = "Dark Cloud - Local player 2 (controller 2)";
        root["game"]["coop"] = true;
        root["video"]["fullscreen"] = false;
        root["video"]["width"] = 960;
        root["video"]["height"] = 720;
        root["video"]["window_left"] = 960;
        root["video"]["window_top"] = 120;
    } else {
        for (const char *section : {"game", "video"}) {
            if (!his.contains(section) || !his[section].is_object()) {
                continue;
            }
            for (const auto &[key, value] : his[section].items()) {
                if (!OwnKey(section, key) && (!root[section].contains(key) || root[section][key] != value)) {
                    root[section][key] = value;
                    copied++;
                }
            }
        }
    }
    std::ofstream(config) << root.dump(2);
    std::fprintf(stderr, "netplay-local: player 2's config %s (%d of player 1's game and display options changed)\n",
                 fresh ? "made from player 1's" : "takes player 1's game and display options", copied);
    return folder;
}

// coop-lobby: the copy started for a lobby's seat (NetplayLocalSpawnToLobby), -1 the plain Local one; the controller and
// identity a lobby's start hands a copy (NetplayLocalSpawnHanded), -1 and 0 none.
int           g_lobby_seat = -1;
int           g_handed_pad = -1;
std::uint64_t g_handed_id = 0;
} // namespace

unsigned long NetplayLocalSpawnToLobby(int player1_pad, int seat, char *why, unsigned long why_size) {
    g_lobby_seat = seat;
    const unsigned long pid = NetplayLocalSpawn(player1_pad, why, why_size);

    g_lobby_seat = -1;
    return pid;
}

unsigned long NetplayLocalSpawnHanded(int player1_pad, int pad, std::uint64_t identity, char *why, unsigned long why_size) {
    g_handed_pad = pad;
    g_handed_id = identity;
    const unsigned long pid = NetplayLocalSpawn(player1_pad, why, why_size);

    g_handed_pad = -1;
    g_handed_id = 0;
    return pid;
}

unsigned long NetplayLocalSpawn(int player1_pad, char *why, unsigned long why_size) {
#if defined(_WIN32)
    wchar_t exe[1024];

    if (GetModuleFileNameW(nullptr, exe, 1024) == 0) {
        std::snprintf(why, why_size, "cannot find this game's exe");
        return 0;
    }

    const std::filesystem::path folder = PrepareFolder();
    std::wstring line = L"\"" + std::wstring(exe) + L"\" --data \"" + PathsDataRoot().wstring() + L"\" --save \"" + folder.wstring() + L"\"";
    // Straight into a town, no logos or title (the host's state then brings it to the host's place, its debug mode too).
    // coop-lobby: a lobby's copy to the title instead, where its join opens the host's lobby on its own window.
    // coop-p2toan (Jep's LAN pack 7 playtest, 2026-11-02: her window reached Norune before his, on an empty save of its own;
    // her menu there was Toan's with nothing in it, and once he linked, her walk there had made her the stayer: her blank
    // world went over his loaded save as the merge's state set, both bags empty): a lobby's handed copy to the title too. Its
    // join waits there, as a friend's copy does, and the host's state (his save, the lobby's picks, both profiles' bags)
    // brings it into his town as player 2. No world of her own exists before his. DC_NETPLAY_MUTATE=p2_blank_town (twin):
    // straight into a town, as before.
    const bool handed_waits = g_handed_pad >= 0 && !NetplayMutated("p2_blank_town");

    line += g_lobby_seat >= 0 || handed_waits ? L" --jump title --fast-load" : L" --jump edit:0 --fast-load";

    if (const char *extra = std::getenv("DC_NETPLAY_LOCAL_ARGS")) {
        line += L" " + std::filesystem::path(extra).wstring();
    }

    // Player 2's settings, for the child only: this copy's environment is put back right after.
    struct Var {
        const char *name;
        const char *value;
        std::string was;
        bool        had;
    } vars[] = {
        {"DC_NETPLAY",           "3",         {}, false},
        {"DC_NETPLAY_TEST_JOIN", "127.0.0.1", {}, false},
        {"DC_NETPLAY_LOCAL_CLAIM", nullptr,    {}, false},
        {"DC_NETPLAY_LOCAL_ARGS", nullptr,    {}, false},
        {"DC_LOBBY_SEAT",        nullptr,     {}, false},
        {"DC_NETPLAY_LOCAL_PAD", nullptr,     {}, false},
        {"DC_NETPLAY_PLAYER_ID", nullptr,     {}, false},
        {"DC_NETPLAY_LOCAL_PAD_DEVICE", nullptr, {}, false},
        {"DC_NETPLAY_LOCAL_CLAIM_DEVICE", nullptr, {}, false},
        // coop-fx51e: the title's test pick (front-local-seed's Local > Continue) is player 1's alone: in hers it picked Local
        // again and took her controller for a player 1.
        {"DC_TITLE_MP_TEST_HOST", nullptr, {}, false},
    };

    const std::string claim = std::to_string(player1_pad);
    const std::string seat = std::to_string(g_lobby_seat);
    const std::string handed = std::to_string(g_handed_pad);
    char              id[17] = "";

    std::snprintf(id, sizeof id, "%016llx", (unsigned long long) g_handed_id);
    vars[2].value = claim.c_str();
    vars[4].value = g_lobby_seat >= 0 ? seat.c_str() : nullptr;
    vars[5].value = g_handed_pad >= 0 ? handed.c_str() : nullptr;
    vars[6].value = g_handed_id != 0 ? id : nullptr;
    // The controllers by their devices too (this copy's numbers can be other ones in hers: two of one make).
    const std::string handed_device = g_handed_pad >= 0 ? NetplayPadDevice(g_handed_pad) : std::string();
    const std::string claim_device = NetplayPadDevice(player1_pad);

    vars[7].value = !handed_device.empty() ? handed_device.c_str() : nullptr;
    vars[8].value = !claim_device.empty() ? claim_device.c_str() : nullptr;
    std::fprintf(stderr, "netplay: local: player 2's copy gets controller %d (%s), player 1 keeps controller %d (%s)\n", g_handed_pad + 1,
                 handed_device.empty() ? "no device name" : handed_device.c_str(), player1_pad + 1, claim_device.empty() ? "no device name" : claim_device.c_str());
    // Test hook DC_NETPLAY_LOCAL_CHILD_ENV="K=V;K=V": more variables for player 2's copy alone (its virtual pads).
    std::vector<std::pair<std::string, std::string>> extra;
    if (const char *more = std::getenv("DC_NETPLAY_LOCAL_CHILD_ENV")) {
        std::string list = more;
        std::size_t at = 0;

        while (at < list.size()) {
            std::size_t end = list.find(';', at);
            std::string item = list.substr(at, end == std::string::npos ? std::string::npos : end - at);
            std::size_t eq = item.find('=');

            if (eq != std::string::npos) {
                extra.emplace_back(item.substr(0, eq), item.substr(eq + 1));
            }
            at = end == std::string::npos ? list.size() : end + 1;
        }
    }
    std::vector<std::pair<std::string, std::string>> extra_was;
    for (auto &[key, value] : extra) {
        const char *now = std::getenv(key.c_str());

        extra_was.emplace_back(key, now != nullptr ? now : std::string("\x01"));
        SetEnvironmentVariableA(key.c_str(), value.c_str());
    }

    for (Var &var : vars) {
        const char *now = std::getenv(var.name);

        var.had = now != nullptr;
        var.was = now != nullptr ? now : "";
        SetEnvironmentVariableA(var.name, var.value);
    }

    STARTUPINFOW        start{};
    PROCESS_INFORMATION process{};

    start.cb = sizeof start;
    start.dwFlags = STARTF_USESHOWWINDOW;
    start.wShowWindow = SW_SHOWNOACTIVATE; // no focus taken from player 1's window

    // The log beside its saves, as the launchers keep one.
    SECURITY_ATTRIBUTES inherit{sizeof inherit, nullptr, TRUE};
    HANDLE log = CreateFileW((folder / "log.txt").wstring().c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, nullptr);

    if (log != INVALID_HANDLE_VALUE) {
        start.dwFlags |= STARTF_USESTDHANDLES;
        start.hStdOutput = log;
        start.hStdError = log;
        start.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    }

    BOOL made = CreateProcessW(nullptr, line.data(), nullptr, nullptr, log != INVALID_HANDLE_VALUE, CREATE_SUSPENDED, nullptr,
                               PathsSaveRoot().wstring().c_str(), &start, &process);

    for (Var &var : vars) {
        SetEnvironmentVariableA(var.name, var.had ? var.was.c_str() : nullptr);
    }
    for (auto &[key, was] : extra_was) {
        SetEnvironmentVariableA(key.c_str(), was == "\x01" ? nullptr : was.c_str());
    }
    if (log != INVALID_HANDLE_VALUE) {
        CloseHandle(log);
    }
    if (!made) {
        std::snprintf(why, why_size, "CreateProcess failed (%lu)", GetLastError());
        return 0;
    }

    // Closes with this copy: a job that kills what it holds once its last handle (this process's) goes.
    static HANDLE job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};

    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (job != nullptr) {
        SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof limits);
        AssignProcessToJobObject(job, process.hProcess);
    }
    ResumeThread(process.hThread);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    std::snprintf(why, why_size, "started");
    return process.dwProcessId;
#else
    std::snprintf(why, why_size, "Windows only");
    return 0;
#endif
}
