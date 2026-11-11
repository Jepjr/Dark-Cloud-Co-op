#include "coop/game_ns.hpp"
#include "coop/steam_check.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

#include "coop/coop.hpp"

#if defined(_WIN32)
#include <windows.h>
#endif

namespace coop::steamcheck {
namespace {

#if defined(_WIN32)
// A DWORD under HKEY_CURRENT_USER, or 0 when the key or value is not there.
DWORD UserDword(const wchar_t *key, const wchar_t *value) {
    DWORD data = 0;
    DWORD size = sizeof data;

    if (RegGetValueW(HKEY_CURRENT_USER, key, value, RRF_RT_REG_DWORD, nullptr, &data, &size) != ERROR_SUCCESS) {
        return 0;
    }
    return data;
}

bool SteamRunning() {
    const DWORD pid = UserDword(L"Software\\Valve\\Steam\\ActiveProcess", L"pid");

    if (pid == 0) {
        return false;
    }
    // (Steam leaves its last pid behind when it was killed: the process must still be there.)
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    DWORD  code = 0;
    const bool alive = process != nullptr && GetExitCodeProcess(process, &code) && code == STILL_ACTIVE;

    if (process != nullptr) {
        CloseHandle(process);
    }
    return alive;
}

bool LibThere() {
    std::error_code error;

    if (const char *path = TestEnv("DC_STEAM_API")) {
        return std::filesystem::is_regular_file(std::filesystem::u8path(path), error);
    }
    wchar_t here[1024];
    DWORD   size = GetModuleFileNameW(nullptr, here, 1024);

    if (size == 0 || size >= 1024) {
        return false;
    }
    return std::filesystem::is_regular_file(std::filesystem::path(std::wstring(here, size)).parent_path() / L"steam_api64.dll", error);
}
#endif

State Look() {
    State state;

    if (const char *fake = TestEnv("DC_STEAM_CHECK")) {
        int r = 0, l = 0, s = 0;

        std::sscanf(fake, "%d,%d,%d", &r, &l, &s);
        state.running = r != 0;
        state.lib = l != 0;
        state.spacewar = s != 0;
    } else {
        // (coop-pt1108s: the Spacewar install is no longer looked at for readiness: Jep's 11-08 Steam join worked with none, the
        // helper sets AppID 480 itself. Read only for the log, and for need_spacewar, the twin.)
#if defined(_WIN32)
        state.running = SteamRunning();
        state.lib = LibThere();
        state.spacewar = UserDword(L"Software\\Valve\\Steam\\Apps\\480", L"Installed") == 1;
#endif
    }
    if (TestEnvIs("DC_STEAM_CHECK_MUTATE", "blind")) {
        state = State{true, true, true};
    }
    // DC_STEAM_CHECK_MUTATE=need_spacewar (the twin): Spacewar's install required again, as before.
    state.need_spacewar = TestEnvIs("DC_STEAM_CHECK_MUTATE", "need_spacewar");
    return state;
}

} // namespace

const State &Get() {
    static State                                 state;
    static bool                                  looked = false;
    static std::chrono::steady_clock::time_point at;
    const auto                                   now = std::chrono::steady_clock::now();

    if (!looked || now - at >= std::chrono::seconds(2)) {
        const State was = state;

        state = Look();
        at = now;
        if (!looked || was.running != state.running || was.lib != state.lib || was.Ready() != state.Ready()) {
            std::fprintf(stderr, "steam-check: Steam running %s, steam_api64.dll %s, ready %s\n", state.running ? "yes" : "no",
                         state.lib ? "yes" : "no", state.Ready() ? "yes" : "no");
        }
        looked = true;
    }
    return state;
}

std::string Line() {
    const State &s = Get();

    if (s.Ready()) {
        return "Steam ready";
    }
    std::string missing;
    const auto  add = [&](const char *what) { missing += (missing.empty() ? "" : ", ") + std::string(what); };

    if (!s.running) {
        add("Steam not running");
    }
    if (!s.lib) {
        add("no Steam DLL");
    }
    if (s.need_spacewar && !s.spacewar) {
        add("no Spacewar");
    }
    return "Steam: " + missing + (s.lib ? "" : " (Set up Steam.bat)");
}

} // namespace coop::steamcheck
