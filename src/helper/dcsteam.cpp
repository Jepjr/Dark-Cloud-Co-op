// coop-steamhelper (Jep, 2026-10-30: "Spacewar" stayed on his Steam profile after the punch, for as long as the game ran; Steam
// counts a process as the game from its SteamAPI_Init until it exits, shutdown or not): the Steam helper. The game starts it
// for NAT > Steam (netplay_steam.cpp) and it alone loads steam_api64.dll and runs Steam (netplay_steam_core.cpp, built in here
// too): the lobby, the presence, the NAT codes and Steam's datagrams. Once the punch has made a direct link the game says quit
// and the helper exits, so Steam shows nothing; when player 2 drops, the game starts a new one for the lobby again.
// The messages: coop/netplay_steam_wire.hpp.
//
//   dcsteam.exe --port P --parent PID [--rate KB]
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <fcntl.h>
#include <io.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "coop/netplay_steam_core.hpp"
#include "coop/netplay_steam_wire.hpp"
#include "platform/config.hpp"

// The game's test helpers, as the shims declare them (src\helper\shim): the same environment, read here.
const char *TestEnv(const char *name) {
    const char *value = std::getenv(name);
    return value != nullptr && *value != '\0' ? value : nullptr;
}
int TestEnvInt(const char *name, int fallback) {
    const char *value = TestEnv(name);
    return value != nullptr ? std::atoi(value) : fallback;
}
bool TestEnvOn(const char *name) {
    const char *value = TestEnv(name);
    return value != nullptr && *value != '0';
}
bool TestEnvIs(const char *name, const char *value) {
    const char *set = TestEnv(name);
    return set != nullptr && std::strcmp(set, value) == 0;
}
bool NetplayMutated(const char *name) {
    const char *mutation = TestEnv("DC_NETPLAY_MUTATE");

    if (mutation == nullptr) {
        return false;
    }
    std::size_t length = std::strlen(name);

    for (const char *at = mutation; *at != '\0';) {
        const char *end = std::strchr(at, ',');
        std::size_t size = end != nullptr ? (std::size_t) (end - at) : std::strlen(at);

        if (size == length && std::strncmp(at, name, length) == 0) {
            return true;
        }
        if (end == nullptr) {
            break;
        }
        at = end + 1;
    }
    return false;
}

namespace {

dc::platform::HelperConfig g_config;

SOCKET      g_socket = INVALID_SOCKET;
sockaddr_in g_game{};

void SendToGame(char kind, const void *data, std::size_t size) {
    std::vector<char> out(size + 1);

    out[0] = kind;
    if (size > 0) {
        std::memcpy(out.data() + 1, data, size);
    }
    static bool said = false;

    if (sendto(g_socket, out.data(), (int) out.size(), 0, reinterpret_cast<const sockaddr *>(&g_game), sizeof g_game) < 0 && !said) {
        said = true;
        std::fprintf(stderr, "netplay: steam helper: cannot tell the game (port %d): error %d\n", ntohs(g_game.sin_port), WSAGetLastError());
    }
}

std::uint64_t Id(const char *at) {
    std::uint64_t id;

    std::memcpy(&id, at, sizeof id);
    return id;
}

} // namespace

const dc::platform::HelperConfig &dc::platform::ConfigGet() {
    return g_config;
}

int main(int argc, char **argv) {
    int           port = 0;
    unsigned long parent_pid = 0;

    for (int i = 1; i + 1 < argc; i += 2) {
        if (std::strcmp(argv[i], "--port") == 0) {
            port = std::atoi(argv[i + 1]);
        } else if (std::strcmp(argv[i], "--parent") == 0) {
            parent_pid = std::strtoul(argv[i + 1], nullptr, 10);
        } else if (std::strcmp(argv[i], "--rate") == 0) {
            g_config.netplay_steam_rate = std::atoi(argv[i + 1]);
        }
    }
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    if (port <= 0 || port > 65535) {
        std::fprintf(stderr, "netplay: steam helper: no --port: exits\n");
        return 2;
    }

    WSADATA wsa;

    WSAStartup(MAKEWORD(2, 2), &wsa);
    g_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

    sockaddr_in local{};

    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    local.sin_port = 0;
    bind(g_socket, reinterpret_cast<sockaddr *>(&local), sizeof local);

    u_long nonblocking = 1;

    ioctlsocket(g_socket, FIONBIO, &nonblocking);
    g_game.sin_family = AF_INET;
    g_game.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    g_game.sin_port = htons((u_short) port);

    HANDLE parent = parent_pid != 0 ? OpenProcess(SYNCHRONIZE, FALSE, parent_pid) : nullptr;

    // Its log lines (its own, netplay_steam_core.cpp's, the Steam DLL's) go to the game whole, a 'G' message a line, and the
    // game writes them to its log: written by both processes to one file, the lines came out mixed letter by letter.
    int lines[2] = {-1, -1};

    if (_pipe(lines, 65536, _O_BINARY) == 0 && _dup2(lines[1], 2) == 0) {
        _close(lines[1]);
    }
    std::setvbuf(stderr, nullptr, _IONBF, 0);

    std::thread forward([fd = lines[0]] {
        std::string line;
        char        chunk[4096];

        for (;;) {
            const int got = _read(fd, chunk, sizeof chunk);

            if (got <= 0) {
                break;
            }
            for (int i = 0; i < got; i++) {
                if (chunk[i] == '\n') {
                    SendToGame('G', line.data(), line.size());
                    line.clear();
                } else if (chunk[i] != '\r') {
                    line += chunk[i];
                }
            }
        }
        if (!line.empty()) {
            SendToGame('G', line.data(), line.size());
        }
    });

    std::fprintf(stderr, "netplay: steam helper: started (pid %lu, for the game's pid %lu)\n", GetCurrentProcessId(), parent_pid);

    const bool up = SteamCoreStart();

    std::fprintf(stderr, "netplay: steam helper: Steam %s\n", up ? "up" : "not up");

    std::set<std::string>           watched_keys;
    std::map<std::string, std::string> lobby_sets; // asked for before the lobby was open: set as soon as it is
    std::map<std::uint64_t, std::string> names;
    std::uint64_t                   watched_session = 0;
    std::string                     tag, find_tag;
    std::uint64_t                   find_host = 0;
    char                            find_name[128] = "";
    int                             find = 0;
    bool                            listing = false; // coop-lobbysteam: the friends hosting asked for ('A')
    int                             list_result = 0;
    std::vector<SteamFriendHost>    friends;
    std::uint64_t                   picked = 0; // coop-lobbysteam-deadpick: the lobby a refusal is about ('P')
    bool                            want_listen = false; // coop-pt1108s: the host's lobby asked for ('O' 1)
    std::string                     listen_tag;
    bool                            quit = false;
    std::string                     why = "the game said so";
    auto                            heard = std::chrono::steady_clock::now();
    auto                            told = std::chrono::steady_clock::now() - std::chrono::seconds(1);
    std::vector<char>               buffer(kSteamHelperMaxDatagram + 16);

    while (!quit) {
        // The game's messages.
        for (;;) {
            int got = recv(g_socket, buffer.data(), (int) buffer.size(), 0);

            if (got <= 0) {
                break;
            }
            heard = std::chrono::steady_clock::now();

            const char  kind = buffer[0];
            const char *body = buffer.data() + 1;
            const int   size = got - 1;
            const std::string text(body, (std::size_t) size);

            if (kind == 'H') {
                tag = text;
                SteamCoreHost(tag.c_str());
            } else if (kind == 'F') {
                find_tag = text;
                find = SteamCoreFindHost(find_tag.c_str(), find_host, find_name, sizeof find_name);
            } else if (kind == 'R') {
                SteamCoreFindReset();
                find = 0;
                find_host = 0;
                find_name[0] = '\0';
            } else if (kind == 'S' && size >= 8) {
                SteamCoreSend(Id(body), body + 8, (std::size_t) (size - 8));
            } else if (kind == 'L') {
                const char *value = body + std::strlen(body) + 1;

                // (Steam opens the lobby a few frames after the host asks: until then the key waits here, not lost.)
                if (value < body + size && !SteamCoreLobbySet(body, value)) {
                    lobby_sets[body] = value;
                }
            } else if (kind == 'W') {
                watched_keys.insert(text.c_str());
            } else if (kind == 'N' && size >= 8) {
                names[Id(body)] = SteamCoreName(Id(body));
            } else if (kind == 'Z' && size >= 8) {
                watched_session = Id(body);
            } else if (kind == 'O' && size >= 1) {
                // coop-lobbysteam: the host listens (a lobby for six) or stops
                want_listen = body[0] == '1';
                listen_tag = text.substr(1);
                SteamCoreListen(want_listen, listen_tag.c_str());
            } else if (kind == 'A') {
                listing = true;
                list_result = SteamCoreFriendsHosting(text.c_str(), friends);
            } else if (kind == 'P' && size >= 8) {
                picked = Id(body);
                SteamCorePick(picked);
            } else if (kind == 'Q') {
                why = text.empty() ? why : text;
                quit = true;
            }
        }

        for (auto it = lobby_sets.begin(); it != lobby_sets.end();) {
            it = SteamCoreLobbySet(it->first.c_str(), it->second.c_str()) ? lobby_sets.erase(it) : std::next(it);
        }

        // Steam's datagrams, to the game.
        SteamCorePump();
        for (;;) {
            std::uint64_t from = 0;
            int           got = SteamCoreReceive(buffer.data() + 8, kSteamHelperMaxDatagram, from);

            if (got <= 0) {
                break;
            }
            std::memcpy(buffer.data(), &from, 8);
            SendToGame('D', buffer.data(), (std::size_t) got + 8);
        }

        // coop-pt1108s: the lobby asked for while Steam was not up (started after this helper): SteamCoreStart tries Steam again
        // every few seconds, and the lobby opens as it comes up. (DC_NETPLAY_MUTATE=steam_no_retry: never tried again.)
        if (want_listen && SteamCoreListening() == 0) {
            SteamCoreListen(true, listen_tag.c_str());
        }

        // The state, each 50 ms.
        const auto now = std::chrono::steady_clock::now();

        if (now - told >= std::chrono::milliseconds(50)) {
            told = now;

            int  pending = 0, rate = 0;
            char line[512] = "";
            bool qok = watched_session != 0 && SteamCoreQueue(watched_session, pending, rate);
            bool lok = watched_session != 0 && SteamCoreLine(watched_session, line, sizeof line);

            std::string state = "ready=1\nup=" + std::to_string(SteamCoreSelf() != 0 ? 1 : 0) + "\nself=" + std::to_string(SteamCoreSelf()) +
                                "\nstatus=" + SteamCoreStatus() + "\ninvited=" + std::to_string(SteamCoreInvited() ? 1 : 0) +
                                "\nfind=" + std::to_string(find) + "\nhost=" + std::to_string(find_host) + "\nhostname=" + find_name +
                                "\npaced=" + std::to_string(SteamCorePaced() ? 1 : 0) + "\ninputon=" + std::to_string(SteamCoreInputOn() ? 1 : 0) +
                                "\nqok=" + std::to_string(qok ? 1 : 0) + "\nqpend=" + std::to_string(pending) + "\nqrate=" + std::to_string(rate) +
                                "\nline=" + (lok ? line : "") + "\nlisten=" + std::to_string(SteamCoreListening()) + "\nrefused=" + (*SteamCorePickRefused() != '\0' ? std::to_string(picked) + "\t" + SteamCorePickRefused() : std::string()) + "\n";

            if (listing) {
                state += "fh=" + std::to_string(list_result) + "\nfn=" + std::to_string(friends.size()) + "\n";
                for (std::size_t i = 0; i < friends.size(); i++) {
                    std::string name = friends[i].name;

                    name.erase(std::remove_if(name.begin(), name.end(), [](char c) { return c == '\n' || c == '\r'; }), name.end());
                    state += "f" + std::to_string(i) + "=" + std::to_string(friends[i].lobby) + "\t" + std::to_string(friends[i].host) + "\t" +
                             std::to_string((friends[i].invited ? 1 : 0) | (friends[i].other_build ? 2 : 0)) + "\t" + name + "\n";
                    state += "fs" + std::to_string(i) + "=" + friends[i].seats + "\n";
                }
            }

            for (const std::string &key : watched_keys) {
                state += "lk." + key + "=" + SteamCoreLobbyGet(key.c_str()) + "\n";
            }
            for (const auto &name : names) {
                state += "nm." + std::to_string(name.first) + "=" + name.second + "\n";
            }
            SendToGame('T', state.data(), state.size());
        }

        if (parent != nullptr && WaitForSingleObject(parent, 0) == WAIT_OBJECT_0) {
            why = "the game ended";
            quit = true;
        }
        if (now - heard > std::chrono::milliseconds(kSteamHelperSilentMs)) {
            why = "nothing heard from the game";
            quit = true;
        }
        if (!quit) {
            Sleep(2);
        }
    }

    SteamCoreStop(why.c_str());
    // coop-sockinherit: test hook (mp-portheld): DC_STEAM_HELPER_TEST_HOLD_FILE=<path>: after its game has gone, it stays while
    // that file is there (the test deletes it once it has tried the game's port), as a slow Steam shutdown would, so whether it
    // holds the game's port then is seen every run. (Ten minutes at most, so a test that died never leaves it running.)
    if (const char *hold = std::getenv("DC_STEAM_HELPER_TEST_HOLD_FILE"); hold != nullptr && why == "the game ended") {
        const auto since = std::chrono::steady_clock::now();

        while (GetFileAttributesA(hold) != INVALID_FILE_ATTRIBUTES && std::chrono::steady_clock::now() - since < std::chrono::minutes(10)) {
            Sleep(20);
        }
    }
    std::fprintf(stderr, "netplay: steam helper: exits (%s)\n", why.c_str());
    // The last lines out to the game before the socket goes: the pipe closed, the forwarder ends at its end.
    std::fflush(stderr);
    _close(2);
    forward.join();
    closesocket(g_socket);
    WSACleanup();
    return 0;
}
