// coop-steamhelper (Jep, 2026-10-30: "Spacewar" stayed on his Steam profile for as long as the game ran): the game's side of
// Steam. Every NetSteam call goes to the Steam helper (dcsteam.exe beside the game, src\helper\dcsteam.cpp), which this file
// starts at NetSteamStart and tells to quit at NetSteamStop; the game itself never loads steam_api64.dll. The helper's state
// comes each 50 ms ('T', coop/netplay_steam_wire.hpp) and the calls answer from it; Steam's datagrams come as 'D'.
// DC_STEAM_IN_PROCESS=1, DC_STEAM_INPUT_API=1 (Steam Input reads this process's pads) or DC_NETPLAY_MUTATE=steam_in_process
// (the twin): Steam in this process (netplay_steam_core.cpp), as before.
#include "coop/game_ns.hpp"
#include "coop/netplay_steam.hpp"

#include "coop/coop.hpp"
#include "coop/netplay_steam_core.hpp"
#include "coop/netplay_steam_wire.hpp"
#include "coop/netplay_test.hpp"
#include "platform/config.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <windows.h>
#endif

namespace {

bool InProcess() {
    static const bool in_process = TestEnvOn("DC_STEAM_IN_PROCESS") || TestEnvOn("DC_STEAM_INPUT_API") || NetplayMutated("steam_in_process");
    return in_process;
}

// coop-lobbysteam: what outlives a Steam session (a stop, a new helper): the host's lobby keys, set again on every lobby it
// opens; whether it listens; when a Steam datagram last came in (a friend's join still trading over Steam); the friend's
// lobby the joiner picked.
std::map<std::string, std::string>    g_lobby_keys;
bool                                  g_listen = false;
std::chrono::steady_clock::time_point g_heard{};
std::uint64_t                         g_pick = 0;
constexpr auto                        kSteamInUse = std::chrono::seconds(5);

constexpr char kBase64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string Encode(const std::string &bytes) {
    std::string out;

    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        const std::uint32_t n = ((std::uint32_t) (unsigned char) bytes[i] << 16) |
                                (i + 1 < bytes.size() ? (std::uint32_t) (unsigned char) bytes[i + 1] << 8 : 0) |
                                (i + 2 < bytes.size() ? (std::uint32_t) (unsigned char) bytes[i + 2] : 0);

        out += kBase64[(n >> 18) & 63];
        out += kBase64[(n >> 12) & 63];
        out += i + 1 < bytes.size() ? kBase64[(n >> 6) & 63] : '=';
        out += i + 2 < bytes.size() ? kBase64[n & 63] : '=';
    }
    return out;
}

std::string Decode(const std::string &text) {
    std::string   out;
    std::uint32_t n = 0;
    int           bits = 0;

    for (char c : text) {
        const char *at = c != '\0' ? std::strchr(kBase64, c) : nullptr;

        if (at == nullptr) {
            continue; // ('=' and anything else)
        }
        n = (n << 6) | (std::uint32_t) (at - kBase64);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out += (char) ((n >> bits) & 0xFF);
        }
    }
    return out;
}

#if defined(_WIN32)
struct Helper {
    SOCKET              socket = INVALID_SOCKET;
    sockaddr_in         from{}; // the helper's own port, once it has said anything
    bool                from_set = false;
    PROCESS_INFORMATION process{};
    bool                running = false;
    std::chrono::steady_clock::time_point last_keepalive{};

    // The helper's last state.
    std::map<std::string, std::string> state;
    bool                               ready = false;
    std::deque<std::pair<std::uint64_t, std::vector<char>>> datagrams;

    // What this side asked for (each said once, again to a new helper).
    std::map<std::string, bool> keys;
    std::map<std::uint64_t, bool> names;
    std::uint64_t               session = 0;
    std::string                 hosting; // the build tag of NetSteamHost, "" when not hosting
    // coop-lobbysteam: the pick told to this helper (each new helper is told again).
    std::uint64_t pick_told = 0;
    bool          listen_told = false;
} g_helper;

std::string g_status = "Steam not started";
std::string g_values; // the string a lobby key or name was last given back in (the caller copies it at once)

void Tell(char kind, const void *data, std::size_t size) {
    if (!g_helper.from_set) {
        return;
    }
    std::vector<char> out(size + 1);

    out[0] = kind;
    if (size > 0) {
        std::memcpy(out.data() + 1, data, size);
    }
    sendto(g_helper.socket, out.data(), (int) out.size(), 0, reinterpret_cast<const sockaddr *>(&g_helper.from), sizeof g_helper.from);
}

void TellText(char kind, const std::string &text) {
    Tell(kind, text.data(), text.size());
}

void TellId(char kind, std::uint64_t id) {
    Tell(kind, &id, sizeof id);
}

const std::string &Get(const char *key) {
    static const std::string none;
    const auto               it = g_helper.state.find(key);

    return it != g_helper.state.end() ? it->second : none;
}

bool On(const char *key) {
    return Get(key) == "1";
}

// The helper's messages so far.
void Drain() {
    if (g_helper.socket == INVALID_SOCKET) {
        return;
    }
    static std::vector<char> buffer(kSteamHelperMaxDatagram + 16);

    for (;;) {
        // (Only what is waiting: a blocking read here held the game's frame for good once the helper went quiet.)
        fd_set  ready;
        timeval none{};

        FD_ZERO(&ready);
        FD_SET(g_helper.socket, &ready);
        if (select(0, &ready, nullptr, nullptr, &none) <= 0) {
            break;
        }

        sockaddr_in from{};
        int         from_size = sizeof from;
        int         got = recvfrom(g_helper.socket, buffer.data(), (int) buffer.size(), 0, reinterpret_cast<sockaddr *>(&from), &from_size);

        if (got <= 0) {
            break;
        }
        if (!g_helper.from_set) {
            g_helper.from = from;
            g_helper.from_set = true;
        }
        if (buffer[0] == 'T') {
            const std::string text(buffer.data() + 1, (std::size_t) got - 1);
            std::size_t       at = 0;

            while (at < text.size()) {
                std::size_t end = text.find('\n', at);
                std::size_t eq = text.find('=', at);

                if (end == std::string::npos) {
                    end = text.size();
                }
                if (eq != std::string::npos && eq < end) {
                    g_helper.state[text.substr(at, eq - at)] = text.substr(eq + 1, end - eq - 1);
                }
                at = end + 1;
            }
            g_helper.ready = On("ready");
            g_status = Get("status");
        } else if (buffer[0] == 'G') {
            std::fprintf(stderr, "%.*s\n", got - 1, buffer.data() + 1); // the helper's log line, whole
        } else if (buffer[0] == 'D' && got >= 9) {
            std::uint64_t id;

            std::memcpy(&id, buffer.data() + 1, 8);
            g_helper.datagrams.emplace_back(id, std::vector<char>(buffer.data() + 9, buffer.data() + got));
        }
    }

    // Each second something, so the helper knows the game is there (it exits after kSteamHelperSilentMs of nothing).
    const auto now = std::chrono::steady_clock::now();

    if (g_helper.running && now - g_helper.last_keepalive > std::chrono::seconds(1)) {
        g_helper.last_keepalive = now;
        Tell('K', nullptr, 0);
    }
}

bool Alive() {
    return g_helper.running && WaitForSingleObject(g_helper.process.hProcess, 0) == WAIT_TIMEOUT;
}

void Close(const char *how) {
    if (g_helper.running) {
        CloseHandle(g_helper.process.hThread);
        CloseHandle(g_helper.process.hProcess);
        std::fprintf(stderr, "netplay: steam: helper (pid %lu) %s\n", g_helper.process.dwProcessId, how);
    }
    if (g_helper.socket != INVALID_SOCKET) {
        closesocket(g_helper.socket);
    }
    const std::string hosting = g_helper.hosting;

    g_helper = Helper{};
    (void) hosting;
}

// The helper started and its first state in, or false (the status says why).
bool Launch() {
    if (Alive() && g_helper.ready) {
        return true;
    }
    if (g_helper.running) {
        Close("gone");
    }

    WSADATA wsa;

    WSAStartup(MAKEWORD(2, 2), &wsa);
    g_helper.socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

    sockaddr_in local{};

    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    const int bound = ::bind(g_helper.socket, reinterpret_cast<sockaddr *>(&local), sizeof local);

    int size = sizeof local;

    getsockname(g_helper.socket, reinterpret_cast<sockaddr *>(&local), &size);

    u_long    nonblocking = 1;
    const int unblocked = ::ioctlsocket(g_helper.socket, FIONBIO, &nonblocking);

    // (The socket is this process's own: the helper must not inherit it.)
    SetHandleInformation(reinterpret_cast<HANDLE>(g_helper.socket), HANDLE_FLAG_INHERIT, 0);
    if (bound != 0) {
        std::fprintf(stderr, "netplay: steam: no port for the Steam helper here (error %d)\n", WSAGetLastError());
    }
    (void) unblocked; // (it fails on some PCs, error 10045: Drain reads only what select says is waiting)

    wchar_t exe[1024];
    DWORD   length = GetModuleFileNameW(nullptr, exe, 1024);

    while (length > 0 && exe[length - 1] != L'\\' && exe[length - 1] != L'/') {
        length--;
    }
    std::wstring path = std::wstring(exe, length) + L"dcsteam.exe";

    if (const char *given = TestEnv("DC_STEAM_HELPER")) {
        wchar_t wide[1024];

        if (MultiByteToWideChar(CP_UTF8, 0, given, -1, wide, 1024) > 0) {
            path = wide;
        }
    }

    std::wstring line = L"\"" + path + L"\" --port " + std::to_wstring(ntohs(local.sin_port)) + L" --parent " +
                        std::to_wstring(GetCurrentProcessId()) + L" --rate " + std::to_wstring(dc::platform::ConfigGet().netplay_steam_rate);
    STARTUPINFOW start{};

    start.cb = sizeof start;
    start.dwFlags = STARTF_USESTDHANDLES;
    start.hStdInput = nullptr;
    start.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    start.hStdError = GetStdHandle(STD_ERROR_HANDLE); // its "netplay: steam:" lines in this copy's log
    std::fflush(stderr);

    if (!CreateProcessW(path.c_str(), line.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &start, &g_helper.process)) {
        g_status = "Steam: the Steam helper (dcsteam.exe) is missing beside the game";
        std::fprintf(stderr, "netplay: steam: %s (error %lu)\n", g_status.c_str(), GetLastError());
        closesocket(g_helper.socket);
        g_helper = Helper{};
        return false;
    }
    g_helper.running = true;
    std::fprintf(stderr, "netplay: steam: helper started (pid %lu)\n", g_helper.process.dwProcessId);

    // Its first state (Steam started or not), within 10 s.
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);

    while (!g_helper.ready && std::chrono::steady_clock::now() < until && Alive()) {
        Drain();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if (!g_helper.ready) {
        g_status = "Steam: the Steam helper did not answer";
        std::fprintf(stderr, "netplay: steam: %s\n", g_status.c_str());
        if (Alive()) {
            TerminateProcess(g_helper.process.hProcess, 1);
        }
        Close("given up");
        return false;
    }
    return true;
}
#endif

} // namespace

bool NetSteamStart() {
    if (InProcess()) {
        return SteamCoreStart();
    }
#if defined(_WIN32)
    return Launch() && On("up");
#else
    return false;
#endif
}

void NetSteamStop(const char *why) {
    g_pick = 0; // coop-lobbysteam (the lobby keys are kept: the next lobby carries them)
    g_listen = false;
    if (InProcess()) {
        SteamCoreStop(why);
        std::fprintf(stderr, "netplay: steam: steam_api loaded in the game's own process: %s\n", NetSteamLoadedHere() ? "yes" : "no");
        return;
    }
#if defined(_WIN32)
    if (!g_helper.running) {
        return;
    }
    TellText('Q', why);
    // Its own lines say Steam stopped; it exits within a second or so (its last lines read before the socket closes).
    const bool exited = WaitForSingleObject(g_helper.process.hProcess, 3000) == WAIT_OBJECT_0;

    Drain();
    if (!exited) {
        TerminateProcess(g_helper.process.hProcess, 1);
        Close("ended (it did not exit by itself)");
    } else {
        Close("exited");
    }
    g_status = "Steam not started";
    std::fprintf(stderr, "netplay: steam: steam_api loaded in the game's own process: %s\n", NetSteamLoadedHere() ? "yes" : "no");
#endif
}

const char *NetSteamStatus() {
    return InProcess() ? SteamCoreStatus() : g_status.c_str();
}

bool NetSteamHost(const char *build_tag) {
    if (InProcess()) {
        return SteamCoreHost(build_tag);
    }
#if defined(_WIN32)
    if (!NetSteamStart()) {
        return false;
    }
    if (g_helper.hosting != build_tag) {
        g_helper.hosting = build_tag;
        TellText('H', build_tag);
    }
    return true;
#else
    return false;
#endif
}

int NetSteamFindHost(const char *build_tag, std::uint64_t &host, char *name, std::size_t name_size) {
    if (InProcess()) {
        SteamCorePick(g_pick);
        return SteamCoreFindHost(build_tag, host, name, name_size);
    }
#if defined(_WIN32)
    if (!NetSteamStart()) {
        return -1;
    }
    Drain();
    if (g_helper.pick_told != g_pick) {
        g_helper.pick_told = g_pick;
        TellId('P', g_pick); // (before the 'F' it decides)
    }
    TellText('F', build_tag);

    const int find = std::atoi(Get("find").c_str());

    if (find == 1) {
        host = std::strtoull(Get("host").c_str(), nullptr, 10);
        std::snprintf(name, name_size, "%s", Get("hostname").c_str());
        return 1;
    }
    return find < 0 ? -1 : 0;
#else
    return -1;
#endif
}

void NetSteamFindReset() {
    if (InProcess()) {
        SteamCoreFindReset();
        return;
    }
#if defined(_WIN32)
    Tell('R', nullptr, 0);
    g_helper.state["find"] = "0";
#endif
}

void NetSteamPump() {
    if (InProcess()) {
        SteamCorePump();
        return;
    }
#if defined(_WIN32)
    Drain();
    if (g_helper.running && !Alive()) {
        Close("ended by itself");
        g_status = "Steam not started";
    }
#endif
}

int NetSteamSend(std::uint64_t to, const void *data, std::size_t size) {
    if (InProcess()) {
        return SteamCoreSend(to, data, size);
    }
#if defined(_WIN32)
    if (!g_helper.running || size > (std::size_t) kSteamHelperMaxDatagram) {
        return 0;
    }
    std::vector<char> out(8 + size);

    std::memcpy(out.data(), &to, 8);
    std::memcpy(out.data() + 8, data, size);
    Tell('S', out.data(), out.size());
    return 1;
#else
    return 0;
#endif
}

int NetSteamReceive(void *data, std::size_t size, std::uint64_t &from) {
    if (InProcess()) {
        const int got = SteamCoreReceive(data, size, from);

        if (got > 0) {
            g_heard = std::chrono::steady_clock::now();
        }
        return got;
    }
#if defined(_WIN32)
    Drain();
    if (g_helper.datagrams.empty()) {
        return 0;
    }
    auto datagram = std::move(g_helper.datagrams.front());

    g_helper.datagrams.pop_front();
    from = datagram.first;
    g_heard = std::chrono::steady_clock::now();
    if (datagram.second.size() > size) {
        return 0;
    }
    std::memcpy(data, datagram.second.data(), datagram.second.size());
    return (int) datagram.second.size();
#else
    return 0;
#endif
}

bool NetSteamQueue(std::uint64_t to, int &pending_bytes, int &rate) {
    if (InProcess()) {
        return SteamCoreQueue(to, pending_bytes, rate);
    }
    pending_bytes = rate = 0;
#if defined(_WIN32)
    if (g_helper.session != to) {
        g_helper.session = to;
        TellId('Z', to);
        return false;
    }
    pending_bytes = std::atoi(Get("qpend").c_str());
    rate = std::atoi(Get("qrate").c_str());
    return On("qok") && rate > 0;
#else
    return false;
#endif
}

bool NetSteamPaced() {
    if (InProcess()) {
        return SteamCorePaced();
    }
#if defined(_WIN32)
    return g_helper.ready ? On("paced") : !NetplayMutated("steam_pace_off");
#else
    return false;
#endif
}

bool NetSteamLine(std::uint64_t to, char *line, std::size_t size) {
    if (InProcess()) {
        return SteamCoreLine(to, line, size);
    }
#if defined(_WIN32)
    if (g_helper.session != to) {
        g_helper.session = to;
        TellId('Z', to);
        return false;
    }
    if (Get("line").empty() || size == 0) {
        return false;
    }
    std::snprintf(line, size, "%s", Get("line").c_str());
    return true;
#else
    return false;
#endif
}

std::uint64_t NetSteamSelf() {
    if (InProcess()) {
        return SteamCoreSelf();
    }
#if defined(_WIN32)
    return g_helper.running ? std::strtoull(Get("self").c_str(), nullptr, 10) : 0;
#else
    return 0;
#endif
}

const char *NetSteamName(std::uint64_t id) {
    if (InProcess()) {
        return SteamCoreName(id);
    }
#if defined(_WIN32)
    if (!g_helper.running) {
        return "";
    }
    if (!g_helper.names[id]) {
        g_helper.names[id] = true;
        TellId('N', id);
    }
    Drain();
    g_values = Get(("nm." + std::to_string(id)).c_str());
    return g_values.c_str();
#else
    return "";
#endif
}

bool NetSteamInvited() {
    if (InProcess()) {
        return SteamCoreInvited();
    }
#if defined(_WIN32)
    Drain();
    return g_helper.running && On("invited");
#else
    return false;
#endif
}

bool NetSteamPad(NetSteamPadState &out) {
    if (InProcess()) {
        return SteamCorePad(out);
    }
    out = NetSteamPadState{}; // (Steam Input reads pads only in the game's own process: DC_STEAM_INPUT_API)
    return false;
}

bool NetSteamLobbySet(const char *key, const char *value) {
    g_lobby_keys[key] = value; // coop-lobbysteam: for every lobby opened later too
    if (InProcess()) {
        return SteamCoreLobbySet(key, value);
    }
#if defined(_WIN32)
    if (!g_helper.running) {
        return false;
    }
    std::string body = std::string(key) + '\0' + value + '\0';

    Tell('L', body.data(), body.size());
    return true;
#else
    return false;
#endif
}

const char *NetSteamLobbyGet(const char *key) {
    if (InProcess()) {
        return SteamCoreLobbyGet(key);
    }
#if defined(_WIN32)
    if (!g_helper.running) {
        return "";
    }
    if (!g_helper.keys[key]) {
        g_helper.keys[key] = true;
        std::string body = std::string(key) + '\0';

        Tell('W', body.data(), body.size());
    }
    Drain();
    g_values = Get(("lk." + std::string(key)).c_str());
    return g_values.c_str();
#else
    return "";
#endif
}

bool NetSteamInputOn() {
    return InProcess() ? SteamCoreInputOn() : false;
}

bool NetSteamLoadedHere() {
#if defined(_WIN32)
    const char *given = TestEnv("DC_STEAM_API");
    const char *name = given != nullptr ? given : "steam_api64.dll";
    const char *slash = std::strrchr(name, '\\');
    const char *slash2 = std::strrchr(name, '/');

    if (slash2 != nullptr && (slash == nullptr || slash2 > slash)) {
        slash = slash2;
    }
    return GetModuleHandleA(slash != nullptr ? slash + 1 : name) != nullptr;
#else
    return false;
#endif
}

unsigned long NetSteamHelperPid() {
#if defined(_WIN32)
    return Alive() ? g_helper.process.dwProcessId : 0;
#else
    return 0;
#endif
}

// coop-lobbysteam: the lobby's Steam (netplay_steam.hpp).
bool NetSteamLobbyListen(bool on) {
    if (on) {
        const bool was = g_listen;
        bool       ok = false;

        g_listen = true;
        if (InProcess()) {
            ok = SteamCoreListen(true, kNetSteamTag);
            for (const auto &key : g_lobby_keys) {
                SteamCoreLobbySet(key.first.c_str(), key.second.c_str()); // (kept by Steam's side for the lobby it opens)
            }
        }
#if defined(_WIN32)
        else if (NetSteamStart() || (g_helper.running && g_helper.ready && !NetplayMutated("steam_no_retry"))) {
            // (coop-pt1108s: the helper up but Steam not yet is told too: it tries Steam again by itself and opens the lobby
            // when Steam comes; ok stays false until then, and the lobby waits on NetSteamListenWaits)
            ok = On("up");
            g_helper.listen_told = true;
            TellText('O', std::string("1") + kNetSteamTag);
            for (const auto &key : g_lobby_keys) {
                const std::string body = key.first + '\0' + key.second + '\0';

                Tell('L', body.data(), body.size()); // (the helper holds them until the lobby is open)
            }
        }
#endif
        if (!was || !ok) {
            std::fprintf(stderr, "netplay: steam: listen on: %s (%d lobby keys)\n", ok ? "a lobby for friends opening" : NetSteamStatus(),
                         (int) g_lobby_keys.size());
        }
        if (!ok && !NetSteamListenWaits()) {
            g_listen = false;
        }
        return ok;
    }

    if (!g_listen) {
        return true;
    }
    g_listen = false;
    if (InProcess()) {
        SteamCoreListen(false, kNetSteamTag);
    }
#if defined(_WIN32)
    else if (g_helper.running) {
        TellText('O', std::string("0") + kNetSteamTag);
        g_helper.listen_told = false;
    }
#endif
    // Steam goes with the lobby (no Spacewar while playing), unless a friend's join still trades over it.
    const auto since = std::chrono::steady_clock::now() - g_heard;

    if (g_heard != std::chrono::steady_clock::time_point{} && since < kSteamInUse) {
        std::fprintf(stderr, "netplay: steam: listen off: lobby closed; Steam kept (a friend's datagram %lld ms ago)\n",
                     (long long) std::chrono::duration_cast<std::chrono::milliseconds>(since).count());
    } else if (NetplayMutated("steam_kept")) {
        // (mp-title-menu host-steam-back's twin, DC_NETPLAY_MUTATE=steam_kept: Steam left running, here as in netplay.cpp's
        // close: this stop came first and made that twin pass on nothing)
        std::fprintf(stderr, "netplay: steam: listen off: lobby closed; Steam kept (steam_kept)\n");
    } else {
        std::fprintf(stderr, "netplay: steam: listen off: lobby closed\n");
        NetSteamStop("listening ended");
    }
    return true;
}

// coop-pt1108s: the lobby's Steam asked for and the helper up, but Steam itself not yet (it tries again by itself).
bool NetSteamListenWaits() {
#if defined(_WIN32)
    if (g_listen && !InProcess() && g_helper.running && g_helper.ready) {
        Drain();
        return !On("up");
    }
#endif
    return false;
}

bool NetSteamListening() {
    if (!g_listen) {
        return false;
    }
    if (InProcess()) {
        return SteamCoreListening() == 2;
    }
#if defined(_WIN32)
    Drain();
    if (g_helper.running && !g_helper.listen_told) {
        // (a new helper, started meanwhile by another call: told again)
        g_helper.listen_told = true;
        TellText('O', std::string("1") + kNetSteamTag);
    }
    return g_helper.running && Get("listen") == "2";
#else
    return false;
#endif
}

void NetSteamLobbySetData(const std::string &seats) {
    if (seats.size() > kSteamSeatsBytes) {
        std::fprintf(stderr, "netplay: steam: seat table of %zu bytes not set (at most %zu)\n", seats.size(), kSteamSeatsBytes);
        return;
    }
    const std::string text = Encode(seats);

    if (g_lobby_keys["seats"] != text) {
        NetSteamLobbySet("seats", text.c_str());
    }
}

int NetSteamFriendsHosting(std::vector<NetSteamFriendHost> &out) {
    out.clear();
    if (InProcess()) {
        std::vector<SteamFriendHost> hosts;
        const int                    result = SteamCoreFriendsHosting(kNetSteamTag, hosts);

        for (const SteamFriendHost &host : hosts) {
            out.push_back(NetSteamFriendHost{host.lobby, host.host, host.name, Decode(host.seats), host.invited, host.other_build});
        }
        return result;
    }
#if defined(_WIN32)
    // (coop-lobbysteam-retry: told even while the helper's Steam is down, as its list is what tries Steam's start again)
    if (!Launch()) {
        return -1;
    }
    Drain();
    TellText('A', kNetSteamTag);
    if (!On("up")) {
        return -1;
    }
    if (Get("fh").empty()) {
        return 0;
    }

    const int count = std::atoi(Get("fn").c_str());

    for (int i = 0; i < count; i++) {
        const std::string &line = Get(("f" + std::to_string(i)).c_str());
        std::size_t        tab1 = line.find('\t');
        std::size_t        tab2 = tab1 != std::string::npos ? line.find('\t', tab1 + 1) : std::string::npos;
        std::size_t        tab3 = tab2 != std::string::npos ? line.find('\t', tab2 + 1) : std::string::npos;

        if (tab3 == std::string::npos) {
            continue;
        }
        NetSteamFriendHost host;
        const int          flags = std::atoi(line.substr(tab2 + 1, tab3 - tab2 - 1).c_str());

        host.lobby = std::strtoull(line.substr(0, tab1).c_str(), nullptr, 10);
        host.host = std::strtoull(line.substr(tab1 + 1, tab2 - tab1 - 1).c_str(), nullptr, 10);
        host.invited = (flags & 1) != 0;
        host.other_build = (flags & 2) != 0;
        host.name = line.substr(tab3 + 1);
        host.seats = Decode(Get(("fs" + std::to_string(i)).c_str()));
        out.push_back(host);
    }
    return std::atoi(Get("fh").c_str());
#else
    return -1;
#endif
}

void NetSteamJoinPick(std::uint64_t lobby) {
    if (g_pick == lobby) {
        return;
    }
    g_pick = lobby;
    std::fprintf(stderr, "netplay: steam: join pick: lobby %llu\n", (unsigned long long) lobby);
    if (InProcess()) {
        SteamCorePick(lobby);
    }
#if defined(_WIN32)
    else if (g_helper.running) {
        g_helper.pick_told = lobby;
        TellId('P', lobby);
    }
#endif
}

std::uint64_t NetSteamJoinPicked() {
    return g_pick;
}

std::string NetSteamLobbyData() {
    return Decode(NetSteamLobbyGet("seats"));
}

std::string NetSteamPickRefused() {
    if (g_pick == 0) {
        return "";
    }
    if (InProcess()) {
        return SteamCorePickRefused();
    }
#if defined(_WIN32)
    Drain();
    // (its lobby first: a refusal of the last pick, said before this pick reached the helper, is not this one's)
    const std::string &refused = Get("refused");
    const std::size_t  tab = refused.find('\t');

    if (!g_helper.running || tab == std::string::npos || std::strtoull(refused.substr(0, tab).c_str(), nullptr, 10) != g_pick) {
        return "";
    }
    return refused.substr(tab + 1);
#else
    return "";
#endif
}
