// Play Dark Cloud Co-op.exe: starts game\darkcloud.exe with the co-op settings, with no console window.
// Built by build\build.ps1 (a Windows program, -mwindows). This folder is <Chronicle checkout>\mods\Dark Cloud Co-op
// (release3c, Jep 11-10: the mod lives only there): the game data is the checkout's own port\run\data (the setup extracts
// it there when the checkout has none; the mod keeps no copy), the saves and settings are in this folder's save\, and the
// game's log in save\log.txt (local only, never sent; the last three starts kept; "write_log": false under "game" in
// save\config.json turns it off). NTSC (US 1.02) data only for now. DCO_DATA and DCO_SAVE point elsewhere; DCO_ARGS adds
// arguments. Test switches: DCO_FAIL_FILE (a failure is written there instead of shown), DCO_LAUNCHER_MUTATE
// (l-launcher-release's twins).
#include <windows.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

namespace {

fs::path FromEnv(const wchar_t *name, const fs::path &fallback) {
    wchar_t value[2048];
    DWORD   n = GetEnvironmentVariableW(name, value, 2048);
    return n > 0 && n < 2048 ? fs::path(value) : fallback;
}

void Fail(const std::wstring &text) {
    fs::path file = FromEnv(L"DCO_FAIL_FILE", {});
    if (!file.empty()) {
        int         n = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int) text.size(), nullptr, 0, nullptr, nullptr);
        std::string utf8(n, '\0');
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int) text.size(), utf8.data(), n, nullptr, nullptr);
        std::ofstream(file, std::ios::binary) << utf8;
        return;
    }
    MessageBoxW(nullptr, text.c_str(), L"Dark Cloud Co-op", MB_OK | MB_ICONERROR);
}

bool Mutated(const wchar_t *name) {
    wchar_t value[256];
    DWORD   n = GetEnvironmentVariableW(L"DCO_LAUNCHER_MUTATE", value, 256);
    return n > 0 && n < 256 && std::wstring(value, n) == name;
}

std::string ReadText(const fs::path &file) {
    std::ifstream     in(file, std::ios::binary);
    std::stringstream all;
    all << in.rdbuf();
    return all.str();
}

// The disc release dcdata found ("release" in the data's languages.json, e.g. "NTSC 1.02"), or empty when there is none.
std::string DataRelease(const fs::path &data) {
    std::string text = ReadText(data / "languages.json");
    size_t      at = text.find("\"release\"");
    at = at == std::string::npos ? at : text.find('"', text.find(':', at));
    size_t end = at == std::string::npos ? at : text.find('"', at + 1);
    return end == std::string::npos ? std::string() : text.substr(at + 1, end - at - 1);
}

// NTSC 1.02 only until PAL is tested (Jep, 2026-11-10); the link refuses a PAL player the same way. Empty when it plays.
std::wstring RegionRefusal(const fs::path &data) {
    std::string release = DataRelease(data);
    if (release == "NTSC 1.02" || Mutated(L"region_ignored")) {
        return {};
    }
    std::wstring name(release.begin(), release.end());
    return L"This game data is from a " + (name.empty() ? std::wstring(L"disc of an unknown release") : name + L" disc") +
           L".\n\nDark Cloud Co-op plays the NTSC (US, version 1.02) disc only for now: PAL is not tested yet.";
}

// save\config.json's "write_log" (under "game"): false turns the log off.
bool LogWanted(const fs::path &save) {
    std::string text = ReadText(save / "config.json");
    size_t      at = text.find("\"write_log\"");
    if (at == std::string::npos || Mutated(L"log_toggle_ignored")) {
        return true;
    }
    size_t value = text.find_first_not_of(" \t\r\n:", at + 11);
    return value == std::string::npos || text.compare(value, 5, "false") != 0;
}

// config.json with co-op on: a new one also starts windowed; an existing one keeps every other setting. The game writes
// the file itself (nlohmann's dump), so "coop" is a plain key under "game".
void CoopOn(const fs::path &save) {
    fs::path config = save / "config.json";
    if (!fs::exists(config)) {
        std::ofstream(config, std::ios::binary) << "{\"game\": {\"coop\": true}, \"video\": {\"fullscreen\": false}}";
        return;
    }
    std::ifstream     in(config, std::ios::binary);
    std::stringstream all;
    all << in.rdbuf();
    in.close();
    std::string text = all.str();
    std::string was = text;
    size_t      at = text.find("\"coop\"");
    if (at != std::string::npos) {
        size_t value = text.find_first_not_of(" \t\r\n:", at + 6);
        if (value != std::string::npos && text.compare(value, 5, "false") == 0) {
            text.replace(value, 5, "true");
        }
    } else if ((at = text.find("\"game\"")) != std::string::npos && (at = text.find('{', at)) != std::string::npos) {
        text.insert(at + 1, "\"coop\": true,");
    } else if ((at = text.find('{')) != std::string::npos) {
        text.insert(at + 1, "\"game\": {\"coop\": true},");
    }
    if (text != was) {
        std::ofstream(config, std::ios::binary | std::ios::trunc) << text;
    }
}

// The log, with the last two starts' logs kept as log-1.txt and log-2.txt; NUL when it is off or cannot be opened.
HANDLE OpenLog(const fs::path &save, SECURITY_ATTRIBUTES *inherit) {
    if (LogWanted(save)) {
        std::error_code error;
        fs::remove(save / "log-2.txt", error);
        fs::rename(save / "log-1.txt", save / "log-2.txt", error);
        fs::rename(save / "log.txt", save / "log-1.txt", error);
        HANDLE log = CreateFileW((save / "log.txt").c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, inherit, CREATE_ALWAYS,
                                 FILE_ATTRIBUTE_NORMAL, nullptr);
        if (log != INVALID_HANDLE_VALUE) {
            return log;
        }
    }
    return CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, inherit, OPEN_EXISTING, 0, nullptr);
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    fs::path mod = fs::path(self).parent_path();
    fs::path game = mod / "game" / "darkcloud.exe";
    // The game data is the Chronicle checkout's own (<checkout>\port\run\data, where its run.bat and the setup extract it):
    // this folder is <checkout>\mods\Dark Cloud Co-op, and the mod keeps no copy of its own.
    // (twin data_mod_folder: the old layout's own data\ in this folder)
    fs::path data = FromEnv(L"DCO_DATA", Mutated(L"data_mod_folder") ? mod / "data" : mod.parent_path().parent_path() / "port" / "run" / "data");
    fs::path save = FromEnv(L"DCO_SAVE", mod / "save");
    std::error_code error;
    if (!fs::exists(game)) {
        Fail(L"No game in " + game.parent_path().wstring() + L".\n\nRun the Dark Cloud Co-op setup again (README.md).");
        return 1;
    }
    if (!fs::exists(data / "data.hd2")) {
        Fail(L"No game data in\n" + data.wstring() +
             L"\n\nRun the Dark Cloud Co-op setup again with your Dark Cloud disc image (NTSC, US 1.02): it extracts the game "
             L"data there, in your Chronicle folder.");
        return 1;
    }
    if (std::wstring refusal = RegionRefusal(data); !refusal.empty()) {
        Fail(refusal + L"\n\nThe game data is your Chronicle's, in\n" + data.wstring() +
             L"\nRun the Dark Cloud Co-op setup again: it says how to use your NTSC disc's data.");
        return 1;
    }
    fs::create_directories(save, error);
    CoopOn(save);

    // Steam joins (optional): Valve's steam_api64.dll beside the game, which the player downloads (README.md).
    fs::path steam = mod / "game" / "steam_api64.dll";
    if (GetEnvironmentVariableW(L"DC_STEAM_API", nullptr, 0) == 0 && fs::exists(steam)) {
        SetEnvironmentVariableW(L"DC_STEAM_API", steam.wstring().c_str());
    }
    // Pads: raw HID, so Steam running alongside never hides an Xbox pad; Steam's virtual pads allowed. Co-op: each player
    // has their own bag; one client hosts or joins as picked on the title; a joiner catches up with the host's play and
    // lands on the host's floor as it is now; the keyboard plays too while linked; UDP 47201 (host) and 47202 (joiner).
    const wchar_t *settings[][2] = {
        {L"SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS", L"1"}, {L"SDL_JOYSTICK_HIDAPI_XBOX", L"1"}, {L"SDL_JOYSTICK_HIDAPI_XBOX_ONE", L"1"},
        {L"SDL_JOYSTICK_RAWINPUT", L"1"}, {L"SDL_GAMECONTROLLER_ALLOW_STEAM_VIRTUAL_GAMEPAD", L"1"}, {L"DC_COOP_OWN_BAGS", L"1"},
        {L"DC_COOP_REVIVE_POWDER", L"0"}, {L"DC_NETPLAY", L"3"}, {L"DC_NETPLAY_CATCHUP", L"1"}, {L"DC_NETPLAY_SNAPSHOT", L"1"},
        {L"DC_NETPLAY_KEYBOARD", L"1"}, {L"DC_NETPLAY_PORT", L"47200"},
    };
    for (auto &s : settings) {
        SetEnvironmentVariableW(s[0], s[1]);
    }

    // The game's output goes to save\log.txt (or nowhere when it is off), and its console (the game is a console program)
    // gets no window. Player 2's copy in Local mode shares that hidden console and log.
    SECURITY_ATTRIBUTES inherit{sizeof inherit, nullptr, TRUE};
    HANDLE              nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, OPEN_EXISTING, 0, nullptr);
    HANDLE              log = OpenLog(save, &inherit);
    STARTUPINFOW        start{};
    start.cb = sizeof start;
    start.dwFlags = STARTF_USESTDHANDLES;
    start.hStdInput = nul;
    start.hStdOutput = log;
    start.hStdError = log;
    PROCESS_INFORMATION process{};
    std::wstring        line = L"\"" + game.wstring() + L"\" --data \"" + data.wstring() + L"\" --save \"" + save.wstring() + L"\"";
    wchar_t             more[1024];
    DWORD               n = GetEnvironmentVariableW(L"DCO_ARGS", more, 1024); // more arguments (e.g. --headless --frames N)
    if (n > 0 && n < 1024) {
        line += L" " + std::wstring(more, n);
    }
    if (!CreateProcessW(nullptr, line.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, game.parent_path().wstring().c_str(),
                        &start, &process)) {
        Fail(L"Could not start " + game.wstring() + L" (error " + std::to_wstring(GetLastError()) + L").");
        return 1;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    CloseHandle(nul);
    CloseHandle(log);
    return 0;
}
