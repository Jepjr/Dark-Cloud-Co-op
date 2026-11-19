// Play Dark Cloud Co-op.exe: starts game\darkcloud.exe with the co-op settings, with no console window.
// Built by build\build.ps1 (a Windows program, -mwindows). This folder is <Chronicle checkout>\mods\Dark Cloud Co-op
// (release3c, Jep 11-10: the mod lives only there): the game data is the checkout's own port\run\data (the setup extracts
// it there when the checkout has none; the mod keeps no copy), the saves and settings are in this folder's save\, and the
// game's log in save\log.txt (local only, never sent; the last three starts kept; "write_log": false under "game" in
// save\config.json turns it off). NTSC (US 1.02) data only for now. DCO_DATA and DCO_SAVE point elsewhere; DCO_ARGS adds
// arguments. Test switches: DCO_FAIL_FILE (a failure is written there instead of shown), DCO_LAUNCHER_MUTATE
// (l-launcher-release's twins), DCO_ASK_ANSWER (yes/no: the answer to the extraction question instead of asking) and
// DCO_ASK_ISO (the disc image instead of the file picker).
//
// dropin (Jep 11-12: the ready-built mod folder is the main way to install): a player unzips this folder into their
// Chronicle folder and runs this. The game is ours (game\), only the data is Chronicle's, so the data is what is checked:
// the disc's release and models.json exactly as Chronicle's dcdata at our pin writes it (launcher\data-pin.h), never
// Chronicle's commit. With no data yet, it asks, then extracts the player's own disc with game\dcdata.exe (the pin's
// dcdata) into <Chronicle>\port\run\data. It writes nowhere else outside this folder and never changes Chronicle's files:
// data from another Chronicle is left as it is (that Chronicle uses it) and the message says what to do.
#include <windows.h>
#include <commdlg.h>
#include <wincrypt.h>

#include "data-pin.h"

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

std::wstring Wide(const std::string &text) {
    int          n = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int) text.size(), nullptr, 0);
    std::wstring out(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int) text.size(), out.data(), n);
    return out;
}

std::wstring EnvText(const wchar_t *name) {
    wchar_t value[2048];
    DWORD   n = GetEnvironmentVariableW(name, value, 2048);
    return n > 0 && n < 2048 ? std::wstring(value, n) : std::wstring();
}

// A file's SHA-256 in capitals (Windows' CryptoAPI: advapi32 is linked by default), or empty when it cannot be read.
std::string Sha256(const fs::path &file) {
    std::string   result;
    HCRYPTPROV    prov = 0;
    HCRYPTHASH    hash = 0;
    std::ifstream in(file, std::ios::binary);
    if (!in || !CryptAcquireContextW(&prov, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
        return result;
    }
    if (CryptCreateHash(prov, CALG_SHA_256, 0, 0, &hash)) {
        std::string chunk(1 << 16, '\0');
        bool        ok = true;
        while (ok && in) {
            in.read(chunk.data(), (std::streamsize) chunk.size());
            ok = in.gcount() == 0 || CryptHashData(hash, (const BYTE *) chunk.data(), (DWORD) in.gcount(), 0);
        }
        BYTE  bytes[32];
        DWORD n = sizeof bytes;
        if (ok && CryptGetHashParam(hash, HP_HASHVAL, bytes, &n, 0)) {
            static const char hex[] = "0123456789ABCDEF";
            for (DWORD i = 0; i < n; i++) {
                result += hex[bytes[i] >> 4];
                result += hex[bytes[i] & 15];
            }
        }
        CryptDestroyHash(hash);
    }
    CryptReleaseContext(prov, 0);
    return result;
}

// Why this data is not the data the mod plays (empty when it is): the disc's release first (NTSC 1.02 only until PAL is
// tested, Jep 2026-11-10; the link refuses a PAL player the same way), then models.json exactly as our pin's dcdata
// writes it (the exe reads Chronicle's data formats as they were at our pin, nothing of Chronicle's code).
std::wstring DataProblem(const fs::path &data) {
    std::string release = DataRelease(data);
    if (release != DCO_DATA_RELEASE && !Mutated(L"region_ignored")) {
        std::wstring name = Wide(release);
        return L"This game data is from a " + (name.empty() ? std::wstring(L"disc of an unknown release") : name + L" disc") +
               L".\n\nDark Cloud Co-op plays the NTSC (US, version 1.02) disc only for now: PAL is not tested yet.";
    }
    if (Mutated(L"models_ignored")) {
        return {};
    }
    fs::path models = data / "models.json";
    if (!fs::exists(models)) {
        return L"This game data was extracted by an older version of Chronicle (it has no models.json). Dark Cloud Co-op "
               L"needs it the way the Chronicle version it was made with extracts it.";
    }
    if (Sha256(models) != DCO_DATA_MODELS_SHA256) {
        return L"This game data was extracted by a different version of Chronicle than the one Dark Cloud Co-op was made "
               L"with (its models.json differs). Dark Cloud Co-op needs it the way that version extracts it.";
    }
    return {};
}

// Removes a folder this launcher made (the extraction's own temporary folder), refusing one that holds a junction or
// link anywhere: nothing is ever deleted through one.
bool RemoveOwnFolder(const fs::path &dir) {
    std::error_code error;
    if (!fs::exists(dir, error)) {
        return true;
    }
    DWORD top = GetFileAttributesW(dir.c_str());
    if (top == INVALID_FILE_ATTRIBUTES || (top & FILE_ATTRIBUTE_REPARSE_POINT)) {
        return false;
    }
    for (auto it = fs::recursive_directory_iterator(dir, error); !error && it != fs::recursive_directory_iterator(); it.increment(error)) {
        DWORD attributes = GetFileAttributesW(it->path().c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
            return false;
        }
    }
    if (error) {
        return false;
    }
    fs::remove_all(dir, error);
    return !error;
}

unsigned long long FolderBytes(const fs::path &dir) {
    unsigned long long total = 0;
    std::error_code    error;
    for (auto it = fs::recursive_directory_iterator(dir, error); !error && it != fs::recursive_directory_iterator(); it.increment(error)) {
        if (it->is_regular_file(error)) {
            total += it->file_size(error);
        }
    }
    return total;
}

// No game data yet: asks the player, then extracts their own disc with the bundled dcdata (Chronicle's, built at our pin)
// into this mod's data-extracting\ first and moves it to <Chronicle>\port\run\data once it is checked, so a stopped or
// wrong extraction never leaves anything in Chronicle. True when the data is there and right.
bool OfferExtract(const fs::path &mod, const fs::path &data) {
    fs::path dcdata = mod / "game" / "dcdata.exe";
    if (!fs::exists(dcdata)) {
        Fail(L"No game data in\n" + data.wstring() +
             L"\n\nRun the Dark Cloud Co-op setup again with your Dark Cloud disc image (NTSC, US 1.02): it extracts the game "
             L"data there, in your Chronicle folder.");
        return false;
    }
    std::wstring question =
        L"Your Chronicle folder has no Dark Cloud game data yet:\n" + data.wstring() +
        L"\n\nDark Cloud Co-op can make it from your own Dark Cloud disc image (an .iso of the NTSC US disc, version 1.02). "
        L"It writes about 5.4 GB there and takes about 5 minutes. Nothing else in your Chronicle folder changes, and "
        L"Chronicle can use the same data.\n\nPick your disc image now?";
    std::wstring answer = EnvText(L"DCO_ASK_ANSWER");
    bool yes = answer.empty() ? MessageBoxW(nullptr, question.c_str(), L"Dark Cloud Co-op", MB_YESNO | MB_ICONQUESTION) == IDYES : answer == L"yes";
    std::wstring iso = EnvText(L"DCO_ASK_ISO");
    if (yes && iso.empty()) {
        wchar_t       file[MAX_PATH] = L"";
        OPENFILENAMEW pick{};
        pick.lStructSize = sizeof pick;
        pick.lpstrFilter = L"Disc images (*.iso)\0*.iso\0All files\0*.*\0";
        pick.lpstrFile = file;
        pick.nMaxFile = MAX_PATH;
        pick.lpstrTitle = L"Your Dark Cloud disc image (NTSC, US 1.02)";
        pick.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
        yes = GetOpenFileNameW(&pick) != 0;
        iso = file;
    }
    if (!yes) {
        Fail(L"No game data, so the game cannot start yet. Run Play Dark Cloud Co-op.exe again when you have your Dark Cloud "
             L"disc image (NTSC, US 1.02) at hand.");
        return false;
    }
    std::error_code error;
    fs::path        part = mod / "data-extracting";
    ULARGE_INTEGER  space{};
    if (GetDiskFreeSpaceExW(mod.c_str(), &space, nullptr, nullptr) && space.QuadPart < 6ull * 1024 * 1024 * 1024) {
        Fail(L"The game data needs about 5.4 GB, and the drive of\n" + mod.wstring() + L"\nhas " + std::to_wstring(space.QuadPart / (1024 * 1024)) +
             L" MB free. Free some space, then run Play Dark Cloud Co-op.exe again.");
        return false;
    }
    if (!RemoveOwnFolder(part)) {
        Fail(L"Could not clear " + part.wstring() + L" (left from an earlier extraction). Delete it, then run Play Dark Cloud Co-op.exe again.");
        return false;
    }
    fs::create_directories(part, error);
    fs::path logFile = mod / "save" / "extract-log.txt";
    fs::create_directories(logFile.parent_path(), error);

    // A console window shows how far it is (dcdata prints nothing while it works).
    bool console = !Mutated(L"no_console") && EnvText(L"DCO_FAIL_FILE").empty() && AllocConsole() != 0;
    auto say = [&](const std::wstring &text) {
        DWORD n;
        if (console) {
            WriteConsoleW(GetStdHandle(STD_OUTPUT_HANDLE), text.c_str(), (DWORD) text.size(), &n, nullptr);
        }
    };
    if (console) {
        SetConsoleTitleW(L"Dark Cloud Co-op: extracting the game data");
    }
    say(L"Extracting the game data from your disc image\n  " + iso + L"\nfor\n  " + data.wstring() + L"\n\n");

    SECURITY_ATTRIBUTES inherit{sizeof inherit, nullptr, TRUE};
    HANDLE log = CreateFileW(logFile.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &inherit, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    HANDLE nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, OPEN_EXISTING, 0, nullptr);
    STARTUPINFOW start{};
    start.cb = sizeof start;
    start.dwFlags = STARTF_USESTDHANDLES;
    start.hStdInput = nul;
    start.hStdOutput = log;
    start.hStdError = log;
    PROCESS_INFORMATION process{};
    std::wstring        line = L"\"" + dcdata.wstring() + L"\" extract \"" + iso + L"\" \"" + part.wstring() + L"\"";
    BOOL started = CreateProcessW(nullptr, line.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, (mod / "game").wstring().c_str(), &start, &process);
    DWORD startError = GetLastError();
    CloseHandle(nul);
    if (!started) {
        CloseHandle(log);
        RemoveOwnFolder(part);
        Fail(L"Could not start " + dcdata.wstring() + L" (error " + std::to_wstring(startError) + L").");
        return false;
    }
    CloseHandle(process.hThread);
    while (WaitForSingleObject(process.hProcess, 2000) == WAIT_TIMEOUT) {
        say(L"\r  " + std::to_wstring(FolderBytes(part) / (1024 * 1024)) + L" / about 5400 MB   ");
    }
    DWORD code = 1;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hProcess);
    CloseHandle(log);
    say(L"\r  " + std::to_wstring(FolderBytes(part) / (1024 * 1024)) + L" MB: checking it.        \n");
    std::wstring problem = code != 0                         ? L"The extraction stopped (dcdata's exit code " + std::to_wstring(code) + L"): is the disc image whole?"
                           : !fs::exists(part / "data.hd2") ? std::wstring(L"The extraction made no data.hd2: is it a Dark Cloud disc image?")
                                                             : DataProblem(part);
    if (console) {
        FreeConsole();
    }
    if (!problem.empty()) {
        RemoveOwnFolder(part);
        Fail(L"Your disc image did not give the game data Dark Cloud Co-op plays.\n\n" + problem + L"\n\nDetails: " + logFile.wstring() +
             L"\nNothing was written to your Chronicle folder.");
        return false;
    }
    fs::create_directories(data.parent_path(), error);
    fs::rename(part, data, error);   // (the same drive: a move, not a copy)
    if (error) {
        Fail(L"Could not move the extracted data to\n" + data.wstring() + L"\n(" + Wide(error.message()) + L"). It is in\n" + part.wstring());
        return false;
    }
    return true;
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
    // dropin: the folder sits in a Chronicle folder (unless DCO_DATA names the data), whose port\run\data the game plays.
    if (EnvText(L"DCO_DATA").empty() && !fs::is_directory(mod.parent_path().parent_path() / "port")) {
        // (Windows' Extract All makes a folder named after the zip: one level too deep)
        fs::path extra = mod.parent_path().parent_path();
        if (fs::is_directory(extra.parent_path() / "port")) {
            Fail(L"The zip went into a folder of its own:\n" + extra.wstring() + L"\n\nMove the mods folder in it up into your Chronicle "
                 L"folder,\n" + extra.parent_path().wstring() + L"\nso that you have " + (extra.parent_path() / "mods" / "Dark Cloud Co-op").wstring() +
                 L", then run Play Dark Cloud Co-op.exe from there.");
            return 1;
        }
        Fail(L"This folder is not inside a Chronicle folder:\n" + mod.wstring() +
             L"\n\nUnzip Dark Cloud Co-op into your Chronicle folder (the one with port and run.bat in it), so that this folder "
             L"is <Chronicle>\\mods\\Dark Cloud Co-op. No Chronicle yet? The setup zip on the Releases page sets one up.");
        return 1;
    }
    if (!fs::exists(data / "data.hd2")) {
        if (!OfferExtract(mod, data)) {
            return 1;
        }
    } else if (std::wstring problem = DataProblem(data); !problem.empty()) {
        Fail(problem + L"\n\nThe game data is your Chronicle's, in\n" + data.wstring() +
             L"\nDark Cloud Co-op leaves it as it is (your Chronicle uses it). To play co-op, rename that data folder (to "
             L"data-old, say) and run Play Dark Cloud Co-op.exe again: it offers to extract your NTSC (US 1.02) disc.");
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
    // lands on the host's floor as it is now; UDP 47201 (host) and 47202 (joiner).
    const wchar_t *settings[][2] = {
        {L"SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS", L"1"}, {L"SDL_JOYSTICK_HIDAPI_XBOX", L"1"}, {L"SDL_JOYSTICK_HIDAPI_XBOX_ONE", L"1"},
        {L"SDL_JOYSTICK_RAWINPUT", L"1"}, {L"SDL_GAMECONTROLLER_ALLOW_STEAM_VIRTUAL_GAMEPAD", L"1"},
        {L"DC_NETPLAY", L"3"}, {L"DC_NETPLAY_CATCHUP", L"1"}, {L"DC_NETPLAY_SNAPSHOT", L"1"},
        {L"DC_NETPLAY_PORT", L"47200"},
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
