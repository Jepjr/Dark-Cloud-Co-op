// No game header here, as in netplay_socket.cpp: the few Win32 calls are declared by hand, and every Steam
// call goes through the flat C API's exports, found with GetProcAddress (playbook/networking-steam-spacewar.md:
// export names, callback ids and the x64 struct layouts used below).
#include "coop/netplay_steam_core.hpp"

#include "coop/coop.hpp"
#include "coop/netplay_test.hpp"
#include "platform/config.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>

#if defined(_WIN32)
extern "C" {
__declspec(dllimport) void *__stdcall LoadLibraryW(const wchar_t *name);
__declspec(dllimport) void *__stdcall GetProcAddress(void *module, const char *name);
__declspec(dllimport) unsigned long __stdcall GetModuleFileNameW(void *module, wchar_t *name, unsigned long size);
__declspec(dllimport) int __stdcall SetEnvironmentVariableA(const char *name, const char *value);
__declspec(dllimport) unsigned long __stdcall GetModuleFileNameA(void *module, char *name, unsigned long size);
__declspec(dllimport) unsigned long __stdcall GetFileAttributesA(const char *name);
__declspec(dllimport) int __stdcall MultiByteToWideChar(unsigned int page, unsigned long flags, const char *text, int size, wchar_t *out,
                                                        int out_size);
}
#endif

namespace {

using u64 = std::uint64_t;

constexpr int kLobbyCreated = 513, kLobbyEnter = 504, kLobbyMatchList = 510, kCallCompleted = 703, kSessionRequest = 1251, kLobbyJoinRequested = 333;
constexpr int kSendUnreliableNoNagle = 1, kSendAutoRestart = 32;
// coop-steamjoin: Steam's send rate. Without it Steam paces every session at its default 256 KB/s (SendRateMin = SendRateMax
// = 256K, steamnetworkingtypes.h: no bandwidth estimate yet, the two set the rate), and a join's 8 MB state set took
// 2 minutes behind Norune's splash (Jep's 2026-10-27 Steam join: 2 MB parts in 27 s each, the wire at 250 KB/s). 40 MB/s
// (Jep, 2026-10-28): a ceiling, not a pace, for now; past the upload's own speed the window halves on the losses.
// DC_STEAM_SEND_RATE (bytes a second) sets another; DC_NETPLAY_MUTATE=steam_rate_default (the twin) leaves Steam's.
// coop-steamrate: 40 MB/s is now the ceiling of the host's own pacing (netplay.cpp SendPieces, by what reaches the joiner);
// game.netplay_steam_rate (KB/s, config.hpp) or DC_STEAM_SEND_RATE sets a fixed rate instead, with no pacing.
constexpr int kSendRateMin = 10, kSendRateMax = 11; // ESteamNetworkingConfigValue
constexpr int kSteamSendRate = 40 * 1024 * 1024;
// coop-steamrate: Steam's own buffers. Steam's defaults: a 512 KB send buffer (past
// it SendMessageToUser says k_EResultLimitExceeded and the piece is not sent), and a receive queue of 1 MB / 1000
// messages a session (past either Steam drops what arrives), some 25 ms of a join at 40 MB/s: any hitch of the joiner's
// (its zone load, the state set's apply) lost the pieces in flight. KeeperFX raises them to 32 MB (coop-anything playbook
// networking-steam-spacewar.md, "Large messages"); so do we, with 32768 messages (32 MB of our 1 KB pieces).
// DC_NETPLAY_MUTATE=steam_buffers_default (twin): Steam's own.
constexpr int kSendBufferSize = 9, kRecvBufferSize = 47, kRecvBufferMessages = 48;
constexpr int kSteamBuffer = 32 * 1024 * 1024, kSteamBufferMessages = 32768;
constexpr int kLimitExceeded = 25; // EResult
constexpr unsigned kSpacewar = 480;

#pragma pack(push, 8)
struct CallbackMsg {
    std::int32_t  user;
    std::int32_t  callback;
    std::uint8_t *param;
    std::int32_t  param_size;
};
struct CallCompleted {
    u64           call;
    std::int32_t  callback;
    std::uint32_t param_size;
};
struct LobbyCreated {
    std::int32_t result;
    u64          lobby;
};
struct LobbyEnter {
    u64           lobby;
    std::uint32_t perms;
    bool          locked;
    std::uint32_t response;
};
struct FriendGame {
    u64           game;
    std::uint32_t ip;
    std::uint16_t port;
    std::uint16_t query_port;
    u64           lobby;
};
// Steam Input's action data (by value from the flat API, as the MS x64 ABI returns them).
struct DigitalData {
    bool state;
    bool active;
};
struct AnalogData {
    std::int32_t mode;
    float        x, y;
    bool         active;
};
#pragma pack(pop)

// SteamNetworkingIdentity, opaque (136 bytes; KeeperFX keeps 160), read and written only through its two calls.
struct Identity {
    unsigned char bytes[160];
};

struct Api {
    int (*InitFlat)(char *error);
    std::int32_t (*GetPipe)();
    void (*DispatchInit)();
    void (*DispatchRun)(std::int32_t);
    bool (*NextCallback)(std::int32_t, CallbackMsg *);
    void (*FreeCallback)(std::int32_t);
    bool (*CallResult)(std::int32_t, u64, void *, int, int, bool *);
    void *(*User)();
    u64 (*UserId)(void *);
    void *(*Matchmaking)();
    u64 (*CreateLobby)(void *, int, int);
    u64 (*JoinLobby)(void *, u64);
    bool (*SetLobbyData)(void *, u64, const char *, const char *);
    const char *(*GetLobbyData)(void *, u64, const char *);
    u64 (*LobbyOwner)(void *, u64);
    void *(*Friends)();
    int (*FriendCount)(void *, int);
    u64 (*FriendAt)(void *, int, int);
    bool (*FriendGamePlayed)(void *, u64, FriendGame *);
    const char *(*FriendName)(void *, u64);
    bool (*SetPresence)(void *, const char *, const char *);
    void *(*Messages)();
    int (*SendTo)(void *, const Identity *, const void *, std::uint32_t, int, int);
    int (*ReceiveOn)(void *, int, void **, int);
    bool (*Accept)(void *, const Identity *);
    void (*SetId)(Identity *, u64);
    u64 (*GetId)(Identity *);
    void (*Release)(void *);
    // A guest whose friend hides their game activity: the lobby list, by Dark Cloud's key, owners who are friends.
    void (*StringFilter)(void *, const char *, const char *, int);
    void (*DistanceFilter)(void *, int);
    u64 (*RequestLobbies)(void *);
    u64 (*LobbyAt)(void *, int);
    bool (*HasFriend)(void *, u64, int);
    const char *(*OwnName)(void *);
    // Steam Input (the controllers Steam owns while it runs this as Spacewar).
    void *(*Input)();
    bool (*InputInit)(void *, bool);
    bool (*InputManifest)(void *, const char *);
    void (*InputRunFrame)(void *, bool);
    int (*InputControllers)(void *, u64 *);
    u64 (*InputSet)(void *, const char *);
    void (*InputActivate)(void *, u64, u64);
    u64 (*InputDigital)(void *, const char *);
    DigitalData (*InputDigitalData)(void *, u64, u64);
    u64 (*InputAnalog)(void *, const char *);
    AnalogData (*InputAnalogData)(void *, u64, u64);
    int (*InputType)(void *, u64);
    // SteamCoreStop's (optional: a DLL without them still starts).
    void (*Shutdown)();
    void (*LeaveLobby)(void *, u64);
    void (*ClearPresence)(void *);
    // coop-steamjoin's (optional too): Steam's send rate, and a session's send queue.
    void *(*Utils)();
    bool (*SetGlobalInt)(void *, int, std::int32_t);
    int (*SessionInfo)(void *, const Identity *, void *, void *);
    // coop-lobbysteam's (optional): a friend's lobby's data before joining it (real Steam has it only once asked for).
    bool (*RequestLobbyData)(void *, u64);
};

Api         g_api{};
bool        g_tried = false;
// coop-lobbysteam-retry: when Steam's start was last tried; the friend list tries again every kRetryStart while it is not up
// (Steam started, or logged in, after the list opened).
std::chrono::steady_clock::time_point g_tried_at{};
constexpr auto                        kRetryStart = std::chrono::seconds(5);
bool        g_up = false;
std::string g_status = "Steam not started";
void       *g_user = nullptr, *g_match = nullptr, *g_friends = nullptr, *g_messages = nullptr;
std::int32_t g_pipe = 0;
u64         g_self = 0;

// Hosting: the lobby (0 until Steam made it) and the call that makes it.
bool        g_hosting = false;
u64         g_lobby = 0;
u64         g_create_call = 0;
std::string g_tag;

// Looking for a host: the lobby being joined and its call, the friend who owns it.
int         g_find = 0; // 0 not yet, 1 joining, 2 in it, -1 none
u64         g_join_call = 0;
u64         g_join_lobby = 0;
u64         g_join_owner = 0;
std::string g_join_name;

// A Steam invite (the host's friends list "Invite to game", or the overlay's): its lobby and who sent it.
u64 g_invite_lobby = 0;
u64 g_invite_from = 0;

// coop-lobbysteam: the host's lobby keys (its code, the seat table), set on each lobby it opens; the lobby's size; the
// creation calls of lobbies closed while Steam was still making them (left once made); the call whose result Handle has
// now (0 for a broadcast); whether this Steam session hosted at all (its friends' sessions are still taken after the lobby
// closes: a friend who just joined it still trades the codes).
std::map<std::string, std::string> g_lobby_keys;
int                                g_members = 2;
std::set<u64>                      g_stale_calls;
u64                                g_result_call = 0;
bool                               g_hosted = false;

// The guest's list of friends hosting: the last whole one and the one being gathered (its lobby search asked for), looked
// for again every few seconds while the list is shown; the friend's lobby picked from it (0: none yet).
std::vector<SteamFriendHost>          g_hosts, g_scan;
int                                   g_list = 0; // 0 not gathered, 1 the lobby search asked for, 2 gathered
bool                                  g_listed = false;
std::chrono::steady_clock::time_point g_list_at{};
std::string                           g_list_said;
u64                                   g_pick = 0;
bool                                  g_pick_said = false;
// coop-lobbysteam-deadpick: why the picked lobby would not take this copy ("" while it may): Steam refused it (the friend's
// game closed meanwhile) or it says another build. Kept until the next pick, so no retry joins it again.
std::string                           g_pick_refused;
constexpr auto                        kListAgain = std::chrono::seconds(3);
constexpr auto                        kListGiveUp = std::chrono::seconds(10);

void JoinLobby(u64 lobby, const std::string &who) {
    g_join_name = who;
    g_join_lobby = lobby;
    g_join_call = g_api.JoinLobby(g_match, lobby);
    g_find = 1;
    g_status = "Joining " + g_join_name + "'s game on Steam...";
}

std::string LobbyData(u64 lobby, const char *key) {
    const char *value = g_api.GetLobbyData(g_match, lobby, key);

    return value != nullptr ? value : "";
}

// A friend's lobby into the list being gathered (once a lobby: an invite's mark kept); its data as far as Steam has it.
void AddHost(u64 lobby, u64 owner, bool invited) {
    if (lobby == 0 || owner == g_self) {
        return;
    }
    auto it = std::find_if(g_scan.begin(), g_scan.end(), [lobby](const SteamFriendHost &host) { return host.lobby == lobby; });

    if (it == g_scan.end()) {
        SteamFriendHost host;
        const char     *name = owner != 0 ? g_api.FriendName(g_friends, owner) : nullptr;

        host.lobby = lobby;
        host.host = owner;
        host.name = name != nullptr && *name != '\0' ? name : LobbyData(lobby, "ownername");
        if (host.name.empty()) {
            host.name = "a friend";
        }
        g_scan.push_back(host);
        it = g_scan.end() - 1;
        if (g_api.RequestLobbyData != nullptr) {
            g_api.RequestLobbyData(g_match, lobby); // (its data by the next look, on real Steam)
        }
    }
    it->invited = it->invited || invited;
}

// The gathered list stands: each lobby's data read again, and the list logged when it changed.
void FinishList(const char *how) {
    for (SteamFriendHost &host : g_scan) {
        const std::string tag = LobbyData(host.lobby, "dccoop");

        host.other_build = !tag.empty() && tag != g_tag;
        host.seats = LobbyData(host.lobby, "seats");
    }
    g_hosts = g_scan;
    g_list = 2;
    g_listed = true;
    g_list_at = std::chrono::steady_clock::now();

    std::string said;

    for (const SteamFriendHost &host : g_hosts) {
        said += (said.empty() ? "" : ", ") + host.name + (host.invited ? " (invited)" : "") + (host.other_build ? " (another build)" : "");
    }
    said = std::to_string(g_hosts.size()) + (said.empty() ? "" : " (" + said + ")");
    if (said != g_list_said) {
        g_list_said = said;
        std::fprintf(stderr, "netplay: steam: friends hosting: %s [%s]\n", said.c_str(), how);
    }
    g_status = g_hosts.empty() ? "No Steam friend is hosting Dark Cloud co-op yet (looking again every few seconds)" : "Choose the friend to join";
}

// The host's lobby: opened for members, or closed (left, presence cleared; Steam stays up).
void OpenLobby(const char *build_tag, int members) {
    g_hosting = g_hosted = true;
    g_tag = build_tag;
    g_members = members;
    // Host > Steam's lobby for two: public, so a friend whose Steam shows no game of ours (activity hidden) still finds it
    // by the lobby search: Steam lists no friends-only lobby (Jep's 2026-10-21 retest). Only a friend gets in (the session
    // accept below). DC_NETPLAY_MUTATE=friends_lobby (the test's twin): friends-only, as before.
    // coop-lobbysteam (Jep, 2026-10-30: "locked down to friends only for team lobby"): the listen lobby is friends-only, so
    // no stranger reads its seat table; a friend hiding their activity is not listed and joins by the host's Steam invite
    // (the friends list's Invite to game). DC_NETPLAY_MUTATE=public_listen (its twin): public, as Host > Steam's.
    bool friends = members == kSteamListenMembers ? !NetplayMutated("public_listen") : TestEnvIs("DC_NETPLAY_MUTATE", "friends_lobby");

    g_create_call = g_api.CreateLobby(g_match, friends ? 1 : 2 /* public */, members);
    g_status = "Opening a Steam lobby...";
    std::fprintf(stderr, "netplay: steam: hosting: opening a %s lobby for %d\n", friends ? "friends-only" : "public (friends only get in)", members);
    if (friends && members == kSteamListenMembers) {
        // (said once a lobby, for a later test and for why a friend set Invisible does not see it)
        std::fprintf(stderr, "netplay: steam: listen lobby friends-only: a friend hiding their game activity joins by this host's invite\n");
    }
}

void CloseLobby(const char *why) {
    if (g_lobby != 0 && g_api.LeaveLobby != nullptr) {
        g_api.LeaveLobby(g_match, g_lobby);
    } else if (g_lobby == 0 && g_create_call != 0) {
        g_stale_calls.insert(g_create_call);
    }
    if (g_api.ClearPresence != nullptr) {
        g_api.ClearPresence(g_friends);
    }
    std::fprintf(stderr, "netplay: steam: lobby %llu closed (%s)%s\n", (unsigned long long) g_lobby, why,
                 g_lobby == 0 ? ": Steam was still opening it, left once made" : "");
    g_hosting = false;
    g_lobby = g_create_call = 0;
    g_status = "Steam up, not listening";
}

#if defined(_WIN32)
template <typename T>
bool Bind(void *dll, T &slot, const char *name) {
    slot = reinterpret_cast<T>(GetProcAddress(dll, name));
    return slot != nullptr;
}

void *Load() {
    if (const char *path = TestEnv("DC_STEAM_API")) {
        wchar_t wide[1024];

        if (MultiByteToWideChar(65001, 0, path, -1, wide, 1024) > 0) {
            return LoadLibraryW(wide);
        }
        return nullptr;
    }

    wchar_t here[1024];
    unsigned long size = GetModuleFileNameW(nullptr, here, 1024);

    while (size > 0 && here[size - 1] != L'\\' && here[size - 1] != L'/') {
        size--;
    }
    if (size == 0 || size + 20 >= 1024) {
        return nullptr;
    }
    std::wcscpy(here + size, L"steam_api64.dll");
    return LoadLibraryW(here);
}
#endif

void Handle(int callback, const void *param, int size) {
    if (callback == kLobbyCreated && size >= (int) sizeof(LobbyCreated)) {
        // (Real Steam sends it twice: as the call's result and as a broadcast. coop-lobbysteam: only the result of the call
        // that asked for this lobby is taken, as listening can close and open again before Steam answers.)
        const auto *made = static_cast<const LobbyCreated *>(param);

        if (g_result_call != 0 && g_stale_calls.erase(g_result_call) != 0) {
            // A lobby no longer wanted (listening ended while Steam made it): left.
            if (made->result == 1 && g_api.LeaveLobby != nullptr) {
                g_api.LeaveLobby(g_match, made->lobby);
                std::fprintf(stderr, "netplay: steam: lobby %llu made after it was no longer wanted: left\n", (unsigned long long) made->lobby);
            }
        } else if (!g_hosting || g_lobby != 0 || g_result_call == 0 || g_result_call != g_create_call) {
            // (the broadcast, or the second word of the lobby already open)
        } else if (made->result == 1) {
            char connect[64];

            g_lobby = made->lobby;
            g_api.SetLobbyData(g_match, g_lobby, "dccoop", g_tag.c_str());
            // Who hosts, for a guest that finds the lobby by searching (a friend hiding their game activity).
            g_api.SetLobbyData(g_match, g_lobby, "owner", std::to_string(g_self).c_str());
            g_api.SetLobbyData(g_match, g_lobby, "ownername", g_api.OwnName(g_friends) != nullptr ? g_api.OwnName(g_friends) : "");
            // coop-lobbysteam: the lobby's size, and the keys set before it opened (or on a lobby closed before it).
            g_api.SetLobbyData(g_match, g_lobby, "members", std::to_string(g_members).c_str());
            for (const auto &key : g_lobby_keys) {
                g_api.SetLobbyData(g_match, g_lobby, key.first.c_str(), key.second.c_str());
            }
            std::snprintf(connect, sizeof connect, "+connect_lobby %llu", (unsigned long long) g_lobby);
            g_api.SetPresence(g_friends, "connect", connect);
            g_api.SetPresence(g_friends, "status", "Hosting Dark Cloud co-op");
            g_status = g_members == kSteamListenMembers ? "Steam: listening" : "Steam lobby open: friends can join";
            std::fprintf(stderr, "netplay: steam: lobby %llu open as %llu for %d (%d keys set again)\n", (unsigned long long) g_lobby,
                         (unsigned long long) g_self, g_members, (int) g_lobby_keys.size());
        } else {
            g_status = "Steam would not open a lobby (result " + std::to_string(made->result) + ")";
            std::fprintf(stderr, "netplay: steam: %s\n", g_status.c_str());
        }
    } else if (callback == kLobbyEnter && size >= (int) sizeof(LobbyEnter) && g_find == 1) {
        const auto *entered = static_cast<const LobbyEnter *>(param);

        if (entered->lobby == g_join_lobby && entered->response == 1) {
            const char *tag = g_api.GetLobbyData(g_match, g_join_lobby, "dccoop");

            if (tag != nullptr && g_tag == tag) {
                g_join_owner = g_api.LobbyOwner(g_match, g_join_lobby);
                g_find = 2;
                std::fprintf(stderr, "netplay: steam: in %s's lobby %llu (host %llu)\n", g_join_name.c_str(), (unsigned long long) g_join_lobby,
                             (unsigned long long) g_join_owner);
            } else {
                g_find = -1;
                g_status = tag != nullptr && *tag != '\0' ? "That friend runs a different build of Dark Cloud co-op" : "That lobby is not Dark Cloud co-op";
                std::fprintf(stderr, "netplay: steam: %s (lobby %llu, tag \"%s\")\n", g_status.c_str(), (unsigned long long) g_join_lobby,
                             tag != nullptr ? tag : "");
                if (g_join_lobby == g_pick && !NetplayMutated("steam_dead_pick_retry")) {
                    g_pick_refused = g_join_name + (tag != nullptr && *tag != '\0' ? " runs another build of Dark Cloud co-op" : "'s lobby is not Dark Cloud co-op");
                }
            }
        } else if (entered->lobby == g_join_lobby) {
            g_find = -1;
            g_status = "Steam would not let this copy into the friend's lobby";
            std::fprintf(stderr, "netplay: steam: %s (response %u)\n", g_status.c_str(), entered->response);
            // coop-lobbysteam-deadpick: the friend picked closed their game (or their lobby filled) after the list showed it.
            if (g_join_lobby == g_pick && !NetplayMutated("steam_dead_pick_retry")) {
                g_pick_refused = g_join_name + "'s game is no longer open on Steam";
            }
        }
        if (!g_pick_refused.empty() && entered->lobby == g_pick) {
            std::fprintf(stderr, "netplay: steam: picked lobby %llu refused: %s (no retry; pick again)\n", (unsigned long long) g_pick, g_pick_refused.c_str());
        }
    } else if (callback == kLobbyMatchList && size >= 4 && g_list == 1) {
        // coop-lobbysteam: the lobbies carrying Dark Cloud's key whose hosts are friends, into the list (a friend hiding their
        // game activity is found only so).
        std::uint32_t count = *static_cast<const std::uint32_t *>(param);
        int           friends = 0;

        for (std::uint32_t i = 0; i < count; i++) {
            u64 lobby = g_api.LobbyAt(g_match, (int) i);
            u64 id = std::strtoull(LobbyData(lobby, "owner").c_str(), nullptr, 10);

            if (id != 0 && id != g_self && g_api.HasFriend(g_friends, id, 4)) {
                AddHost(lobby, id, false);
                friends++;
            }
        }
        FinishList(("the search found " + std::to_string(count) + ", " + std::to_string(friends) + " a friend's").c_str());
    } else if (callback == kLobbyJoinRequested && size >= 16) {
        // An invite accepted in Steam: join it from the main menu (netplay.cpp opens the join for it).
        g_invite_lobby = static_cast<const u64 *>(param)[0];
        g_invite_from = static_cast<const u64 *>(param)[1];
        std::fprintf(stderr, "netplay: steam: invited to lobby %llu by %llu\n", (unsigned long long) g_invite_lobby, (unsigned long long) g_invite_from);
    } else if (callback == kSessionRequest && size >= 136) {
        // A message from someone new waits until the session is accepted: the host takes anyone (the
        // link's Hello then decides), a guest only its host.
        Identity who{};

        std::memcpy(who.bytes, param, 136);
        u64 id = g_api.GetId(&who);

        if (((g_hosting || g_hosted) && g_api.HasFriend(g_friends, id, 4)) || id == g_join_owner) {
            g_api.Accept(g_messages, &who);
            std::fprintf(stderr, "netplay: steam: session with %llu accepted\n", (unsigned long long) id);
        } else {
            std::fprintf(stderr, "netplay: steam: session with %llu refused (not a Steam friend)\n", (unsigned long long) id);
        }
    }
}

// coop-steamrate: a fixed send rate (bytes a second) from DC_STEAM_SEND_RATE or game.netplay_steam_rate; 0: auto.
int FixedRate() {
    int env = TestEnvInt("DC_STEAM_SEND_RATE", 0);

    if (env > 0) {
        return std::max(64 * 1024, env);
    }
    return dc::platform::ConfigGet().netplay_steam_rate > 0 ? std::max(64, dc::platform::ConfigGet().netplay_steam_rate) * 1024 : 0;
}

// coop-steamrate: Steam's send buffer and receive queue (kSteamBuffer), before any session opens.
void SetBuffers(void *utils) {
    if (NetplayMutated("steam_buffers_default")) {
        std::fprintf(stderr, "netplay: steam: buffers left at Steam's own (test: steam_buffers_default)\n");
        return;
    }

    bool send = g_api.SetGlobalInt(utils, kSendBufferSize, kSteamBuffer);
    bool recv = g_api.SetGlobalInt(utils, kRecvBufferSize, kSteamBuffer);
    bool messages = g_api.SetGlobalInt(utils, kRecvBufferMessages, kSteamBufferMessages);

    std::fprintf(stderr, "netplay: steam: buffers: send %d KB%s, receive %d KB%s and %d messages%s\n", kSteamBuffer / 1024, send ? "" : " NOT taken",
                 kSteamBuffer / 1024, recv ? "" : " NOT taken", kSteamBufferMessages, messages ? "" : " NOT taken");
}

// coop-steamjoin: Steam's send rate for every session from here on (kSteamSendRate, or the fixed rate).
void SetSendRate() {
    void *utils = g_api.Utils != nullptr ? g_api.Utils() : nullptr;

    if (NetplayMutated("steam_rate_default")) {
        std::fprintf(stderr, "netplay: steam: send rate left at Steam's own (test: steam_rate_default)\n");
        return;
    }
    if (utils == nullptr || g_api.SetGlobalInt == nullptr) {
        std::fprintf(stderr, "netplay: steam: send rate left at Steam's own (no SteamNetworkingUtils in this steam_api64.dll)\n");
        return;
    }

    SetBuffers(utils);

    int  fixed = FixedRate();
    int  rate = fixed > 0 ? fixed : kSteamSendRate;
    bool ok = g_api.SetGlobalInt(utils, kSendRateMin, rate) && g_api.SetGlobalInt(utils, kSendRateMax, rate);

    std::fprintf(stderr, "netplay: steam: send rate %d KB/s%s\n", rate / 1024, ok ? "" : " NOT taken (Steam refused it)");
    std::fprintf(stderr, "netplay: steam: pacing %s\n",
                 fixed > 0 ? "off: a fixed rate (game.netplay_steam_rate or DC_STEAM_SEND_RATE)"
                 : NetplayMutated("steam_pace_off") ? "off (test: steam_pace_off)"
                                                    : "by what reaches the other copy, up to the send rate");
}

} // namespace

bool SteamCoreStart() {
    // coop-pt1108s (Jep's 11-08 playtest: a helper started before Steam stayed "Steam is not running" for good): not up (Steam not
    // running or not logged in at the first try) is tried again every kRetryStart by whoever asks again (the friend list, the lobby's
    // listen, the helper's loop), so Steam started later is taken up. DC_NETPLAY_MUTATE=steam_no_retry (the twin): once.
    if (g_tried && !g_up && std::chrono::steady_clock::now() - g_tried_at >= kRetryStart && !NetplayMutated("steam_no_retry")) {
        std::fprintf(stderr, "netplay: steam: trying Steam's start again (last: %s)\n", g_status.c_str());
        g_tried = false;
    }
    if (g_tried) {
        return g_up;
    }

    g_tried = true;
    g_tried_at = std::chrono::steady_clock::now();
#if defined(_WIN32)
    void *dll = Load();

    if (dll == nullptr) {
        g_status = "Steam: no steam_api64.dll beside the game (see README)";
        std::fprintf(stderr, "netplay: steam: %s\n", g_status.c_str());
        return false;
    }

    bool ok = Bind(dll, g_api.InitFlat, "SteamAPI_InitFlat") && Bind(dll, g_api.GetPipe, "SteamAPI_GetHSteamPipe") &&
              Bind(dll, g_api.DispatchInit, "SteamAPI_ManualDispatch_Init") && Bind(dll, g_api.DispatchRun, "SteamAPI_ManualDispatch_RunFrame") &&
              Bind(dll, g_api.NextCallback, "SteamAPI_ManualDispatch_GetNextCallback") &&
              Bind(dll, g_api.FreeCallback, "SteamAPI_ManualDispatch_FreeLastCallback") &&
              Bind(dll, g_api.CallResult, "SteamAPI_ManualDispatch_GetAPICallResult") && Bind(dll, g_api.User, "SteamAPI_SteamUser_v023") &&
              Bind(dll, g_api.UserId, "SteamAPI_ISteamUser_GetSteamID") && Bind(dll, g_api.Matchmaking, "SteamAPI_SteamMatchmaking_v009") &&
              Bind(dll, g_api.CreateLobby, "SteamAPI_ISteamMatchmaking_CreateLobby") &&
              Bind(dll, g_api.JoinLobby, "SteamAPI_ISteamMatchmaking_JoinLobby") &&
              Bind(dll, g_api.SetLobbyData, "SteamAPI_ISteamMatchmaking_SetLobbyData") &&
              Bind(dll, g_api.GetLobbyData, "SteamAPI_ISteamMatchmaking_GetLobbyData") &&
              Bind(dll, g_api.LobbyOwner, "SteamAPI_ISteamMatchmaking_GetLobbyOwner") && Bind(dll, g_api.Friends, "SteamAPI_SteamFriends_v018") &&
              Bind(dll, g_api.FriendCount, "SteamAPI_ISteamFriends_GetFriendCount") &&
              Bind(dll, g_api.FriendAt, "SteamAPI_ISteamFriends_GetFriendByIndex") &&
              Bind(dll, g_api.FriendGamePlayed, "SteamAPI_ISteamFriends_GetFriendGamePlayed") &&
              Bind(dll, g_api.FriendName, "SteamAPI_ISteamFriends_GetFriendPersonaName") &&
              Bind(dll, g_api.SetPresence, "SteamAPI_ISteamFriends_SetRichPresence") &&
              Bind(dll, g_api.Messages, "SteamAPI_SteamNetworkingMessages_SteamAPI_v002") &&
              Bind(dll, g_api.SendTo, "SteamAPI_ISteamNetworkingMessages_SendMessageToUser") &&
              Bind(dll, g_api.ReceiveOn, "SteamAPI_ISteamNetworkingMessages_ReceiveMessagesOnChannel") &&
              Bind(dll, g_api.Accept, "SteamAPI_ISteamNetworkingMessages_AcceptSessionWithUser") &&
              Bind(dll, g_api.SetId, "SteamAPI_SteamNetworkingIdentity_SetSteamID64") &&
              Bind(dll, g_api.GetId, "SteamAPI_SteamNetworkingIdentity_GetSteamID64") &&
              Bind(dll, g_api.Release, "SteamAPI_SteamNetworkingMessage_t_Release") &&
              Bind(dll, g_api.StringFilter, "SteamAPI_ISteamMatchmaking_AddRequestLobbyListStringFilter") &&
              Bind(dll, g_api.DistanceFilter, "SteamAPI_ISteamMatchmaking_AddRequestLobbyListDistanceFilter") &&
              Bind(dll, g_api.RequestLobbies, "SteamAPI_ISteamMatchmaking_RequestLobbyList") &&
              Bind(dll, g_api.LobbyAt, "SteamAPI_ISteamMatchmaking_GetLobbyByIndex") && Bind(dll, g_api.HasFriend, "SteamAPI_ISteamFriends_HasFriend") &&
              Bind(dll, g_api.OwnName, "SteamAPI_ISteamFriends_GetPersonaName") && Bind(dll, g_api.Input, "SteamAPI_SteamInput_v006") &&
              Bind(dll, g_api.InputInit, "SteamAPI_ISteamInput_Init") &&
              Bind(dll, g_api.InputManifest, "SteamAPI_ISteamInput_SetInputActionManifestFilePath") &&
              Bind(dll, g_api.InputRunFrame, "SteamAPI_ISteamInput_RunFrame") &&
              Bind(dll, g_api.InputControllers, "SteamAPI_ISteamInput_GetConnectedControllers") &&
              Bind(dll, g_api.InputSet, "SteamAPI_ISteamInput_GetActionSetHandle") &&
              Bind(dll, g_api.InputActivate, "SteamAPI_ISteamInput_ActivateActionSet") &&
              Bind(dll, g_api.InputDigital, "SteamAPI_ISteamInput_GetDigitalActionHandle") &&
              Bind(dll, g_api.InputDigitalData, "SteamAPI_ISteamInput_GetDigitalActionData") &&
              Bind(dll, g_api.InputAnalog, "SteamAPI_ISteamInput_GetAnalogActionHandle") &&
              Bind(dll, g_api.InputAnalogData, "SteamAPI_ISteamInput_GetAnalogActionData") &&
              Bind(dll, g_api.InputType, "SteamAPI_ISteamInput_GetInputTypeForHandle");

    Bind(dll, g_api.Shutdown, "SteamAPI_Shutdown");
    Bind(dll, g_api.LeaveLobby, "SteamAPI_ISteamMatchmaking_LeaveLobby");
    Bind(dll, g_api.ClearPresence, "SteamAPI_ISteamFriends_ClearRichPresence");
    Bind(dll, g_api.Utils, "SteamAPI_SteamNetworkingUtils_SteamAPI_v004");
    Bind(dll, g_api.SetGlobalInt, "SteamAPI_ISteamNetworkingUtils_SetGlobalConfigValueInt32");
    Bind(dll, g_api.SessionInfo, "SteamAPI_ISteamNetworkingMessages_GetSessionConnectionInfo");
    Bind(dll, g_api.RequestLobbyData, "SteamAPI_ISteamMatchmaking_RequestLobbyData");

    if (!ok) {
        g_status = "Steam: steam_api64.dll is too old (no flat API)";
        std::fprintf(stderr, "netplay: steam: %s\n", g_status.c_str());
        return false;
    }

    // Spacewar's AppID, for this process (Steam reads it from here as from steam_appid.txt).
    SetEnvironmentVariableA("SteamAppId", "480");
    SetEnvironmentVariableA("SteamGameId", "480");

    char error[1024] = "";
    int  init = g_api.InitFlat(error);

    if (init != 0) {
        g_status = init == 2 ? "Steam is not running: start Steam and log in" : std::string("Steam would not start: ") + error;
        std::fprintf(stderr, "netplay: steam: init %d: %s\n", init, error);
        return false;
    }

    g_api.DispatchInit();
    g_pipe = g_api.GetPipe();
    g_user = g_api.User();
    g_match = g_api.Matchmaking();
    g_friends = g_api.Friends();
    g_messages = g_api.Messages();
    g_self = g_user != nullptr ? g_api.UserId(g_user) : 0;
    g_up = g_match != nullptr && g_friends != nullptr && g_messages != nullptr && g_self != 0;
    g_status = g_up ? "Steam ready" : "Steam started without its interfaces";

    if (g_up) {
        SetSendRate();
    }

    // The session ends with the game: out of the lobby and Spacewar gone at once, not when Steam notices (once a process).
    static bool at_exit = false;

    if (g_up && !at_exit) {
        at_exit = true;
        std::atexit([] { SteamCoreStop("the game quit"); });
    }
    std::fprintf(stderr, "netplay: steam: %s (AppID %u, SteamID %llu)\n", g_status.c_str(), kSpacewar, (unsigned long long) g_self);
    return g_up;
#else
    g_status = "Steam: Windows only";
    return false;
#endif
}

const char *SteamCoreStatus() {
    return g_status.c_str();
}

std::uint64_t SteamCoreSelf() {
    return g_self;
}

void SteamCorePump() {
    if (!g_up) {
        return;
    }

    g_api.DispatchRun(g_pipe);

    CallbackMsg msg{};

    while (g_api.NextCallback(g_pipe, &msg)) {
        if (msg.callback == kCallCompleted && msg.param_size >= (int) sizeof(CallCompleted)) {
            const auto *done = reinterpret_cast<const CallCompleted *>(msg.param);
            unsigned char result[512];
            bool          failed = false;

            if (done->param_size <= sizeof result &&
                g_api.CallResult(g_pipe, done->call, result, (int) done->param_size, done->callback, &failed) && !failed) {
                g_result_call = done->call;
                Handle(done->callback, result, (int) done->param_size);
                g_result_call = 0;
            }
        } else {
            Handle(msg.callback, msg.param, msg.param_size);
        }
        g_api.FreeCallback(g_pipe);
    }
}

bool SteamCoreHost(const char *build_tag) {
    if (!SteamCoreStart()) {
        return false;
    }

    if (!g_hosting) {
        OpenLobby(build_tag, 2);
    }

    return true;
}

// coop-lobbysteam (the lobby plan 4.3: Jep, 2026-10-30): the host's Select listens, a lobby for six friends find by Join Game
// or the list; it closes when the lobby starts the game or the in-game menu closes. DC_NETPLAY_MUTATE=steam_listen_two (a
// twin): a lobby for two, as Host > Steam's.
bool SteamCoreListen(bool on, const char *build_tag) {
    if (!on) {
        if (g_up && g_hosting) {
            CloseLobby("listening ended");
        }
        return true;
    }
    if (!SteamCoreStart()) {
        return false;
    }
    const int members = NetplayMutated("steam_listen_two") ? 2 : kSteamListenMembers;

    if (g_hosting && g_members == members) {
        return true;
    }
    if (g_hosting) {
        CloseLobby("listening opens a lobby of its own");
    }
    OpenLobby(build_tag, members);
    return true;
}

int SteamCoreListening() {
    return !g_up || !g_hosting ? 0 : g_lobby != 0 ? 2 : 1;
}

// The friends hosting, gathered from their Steam games (Spacewar with a lobby), an accepted invite and the lobby search by
// Dark Cloud's key; again every kListAgain while the caller asks. 1 when a list stands (empty or not), 0 until the first,
// -1 when Steam is out.
int SteamCoreFriendsHosting(const char *build_tag, std::vector<SteamFriendHost> &out) {
    // coop-lobbysteam-retry (2026-11-01): Steam not up when JOIN > STEAM opened was tried once; SteamCoreStart now tries again
    // every kRetryStart while the list shows (steam_no_retry, the twin: once).
    if (!SteamCoreStart()) {
        out.clear();
        return -1;
    }
    SteamCorePump();

    const auto now = std::chrono::steady_clock::now();

    if (g_list == 1 && now - g_list_at > kListGiveUp) {
        FinishList("the lobby search went unanswered: the friends' games alone");
    }
    if (g_list == 0 || (g_list == 2 && now - g_list_at >= kListAgain)) {
        g_tag = build_tag;
        g_scan.clear();
        AddHost(g_invite_lobby, g_invite_from, true);

        const int count = g_api.FriendCount(g_friends, 4 /* immediate friends */);
        int       spacewar = 0; // coop-natsplit: friends whose Steam shows Spacewar, lobby or not (in the log)

        for (int i = 0; i < count; i++) {
            u64        id = g_api.FriendAt(g_friends, i, 4);
            FriendGame game{};

            if (g_api.FriendGamePlayed(g_friends, id, &game) && (game.game & 0xFFFFFF) == kSpacewar) {
                spacewar++;
                AddHost(game.lobby, id, false);
            }
        }
        g_list_at = now;
        if (!TestEnvIs("DC_NETPLAY_MUTATE", "no_lobby_search")) {
            // (a friend whose activity is hidden shows no game: the search finds their lobby by its key)
            g_api.StringFilter(g_match, "dccoop", g_tag.c_str(), 0 /* equal */);
            g_api.DistanceFilter(g_match, 3 /* worldwide */);
            g_api.RequestLobbies(g_match);
            g_list = 1;
        } else {
            FinishList((std::to_string(count) + " friends, " + std::to_string(spacewar) + " in Spacewar, no search").c_str());
        }
    }
    out = g_hosts;
    return g_listed ? 1 : 0;
}

// The guest's choice from the list: NetSteamFindHost joins that lobby and no other (an accepted invite aside).
void SteamCorePick(std::uint64_t lobby) {
    if (g_pick != lobby) {
        g_pick = lobby;
        g_pick_said = false;
        g_pick_refused.clear();
        std::fprintf(stderr, "netplay: steam: friend's lobby picked: %llu\n", (unsigned long long) lobby);
    }
}

void SteamCoreFindReset() {
    g_find = 0;
    g_join_lobby = g_join_owner = 0;
    g_join_name.clear();
}

int SteamCoreFindHost(const char *build_tag, std::uint64_t &host, char *name, std::size_t name_size) {
    if (!SteamCoreStart()) {
        return -1;
    }

    SteamCorePump();

    // An invite comes first: that lobby, whoever sent it (accepting it in Steam is the pick).
    if (g_invite_lobby != 0 && (g_find == 0 || g_find == -1)) {
        const char *who = g_api.FriendName(g_friends, g_invite_from);

        g_tag = build_tag;
        JoinLobby(g_invite_lobby, who != nullptr && *who != '\0' ? who : "a friend");
        std::fprintf(stderr, "netplay: steam: joining %s's invite (lobby %llu)\n", g_join_name.c_str(), (unsigned long long) g_invite_lobby);
        g_invite_lobby = 0;
    }

    // coop-lobbysteam (Jep, 2026-10-30: a Steam join lists every friend hosting and takes a pick, as a safety measure, even
    // with one): the lobby picked, no other. DC_NETPLAY_MUTATE=steam_autopick (the twin): the first friend listed, as before.
    if (g_find == 0 && g_pick == 0 && NetplayMutated("steam_autopick")) {
        std::vector<SteamFriendHost> hosts;

        if (SteamCoreFriendsHosting(build_tag, hosts) > 0 && !hosts.empty()) {
            g_pick = hosts.front().lobby;
            std::fprintf(stderr, "netplay: steam: no pick: the first friend listed (%s), as before the list\n", hosts.front().name.c_str());
        }
    }
    // coop-lobbysteam-deadpick (2026-11-01): a picked lobby that refused this copy is not joined again (the NAT search's 2 s
    // retries did, for 20 s, then fell to the Code entry saying no friend hosts). DC_NETPLAY_MUTATE=steam_dead_pick_retry
    // (the twin): no refusal kept, retried as before.
    if (!g_pick_refused.empty()) {
        g_find = -1;
        g_status = g_pick_refused;
    }
    if (g_find == 0 && g_pick != 0) {
        auto        it = std::find_if(g_hosts.begin(), g_hosts.end(), [](const SteamFriendHost &host) { return host.lobby == g_pick; });
        std::string who = it != g_hosts.end() ? it->name : "a friend";

        g_tag = build_tag;
        JoinLobby(g_pick, who);
        std::fprintf(stderr, "netplay: steam: joining %s's lobby %llu (picked)\n", who.c_str(), (unsigned long long) g_pick);
    } else if (g_find == 0 && !NetplayMutated("steam_autopick")) {
        g_find = -1;
        g_status = "Choose the friend to join in MULTIPLAYER > JOIN > STEAM";
        if (!g_pick_said) {
            g_pick_said = true;
            std::fprintf(stderr, "netplay: steam: no friend picked: no join (%s)\n", g_status.c_str());
        }
    }

    if (g_find == 2) {
        host = g_join_owner;
        std::snprintf(name, name_size, "%s", g_join_name.c_str());
        return 1;
    }

    return g_find < 0 ? -1 : 0;
}

int SteamCoreSend(std::uint64_t to, const void *data, std::size_t size) {
    if (!g_up) {
        return 0;
    }

    Identity who{};

    g_api.SetId(&who, to);

    int result = g_api.SendTo(g_messages, &who, data, (std::uint32_t) size, kSendUnreliableNoNagle | kSendAutoRestart, 0);

    return result == 1 ? 1 : result == kLimitExceeded ? -1 : 0;
}

bool SteamCorePaced() {
    return FixedRate() == 0 && !NetplayMutated("steam_pace_off");
}

bool SteamCoreQueue(std::uint64_t to, int &pending_bytes, int &rate) {
    pending_bytes = rate = 0;

    if (!g_up || g_api.SessionInfo == nullptr) {
        return false;
    }

    Identity who{};
    // SteamNetConnectionInfo_t (some 700 bytes) and SteamNetConnectionRealTimeStatus_t (120), with room to spare.
    alignas(8) unsigned char info[2048] = {};
    alignas(8) unsigned char status[512] = {};

    g_api.SetId(&who, to);

    int state = g_api.SessionInfo(g_messages, &who, info, status);

    // k_ESteamNetworkingConnectionState_Connecting 1, FindingRoute 2, Connected 3. The status: the state at 0, the ping at
    // 4, m_nSendRateBytesPerSecond at 32, m_cbPendingUnreliable at 36.
    if (state < 1 || state > 3) {
        return false;
    }
    std::memcpy(&rate, status + 32, 4);
    std::memcpy(&pending_bytes, status + 36, 4);
    return rate > 0;
}

// coop-steamceiling: what Steam itself says of the session, for the catch-up's log: the share of packets that arrived each
// way, the bytes a second each way, and whether it goes through a Valve relay (and which).
bool SteamCoreLine(std::uint64_t to, char *line, std::size_t size) {
    if (!g_up || g_api.SessionInfo == nullptr || size == 0) {
        return false;
    }

    Identity who{};
    alignas(8) unsigned char info[2048] = {};
    alignas(8) unsigned char status[512] = {};

    g_api.SetId(&who, to);

    int state = g_api.SessionInfo(g_messages, &who, info, status);

    if (state < 1 || state > 3) {
        return false;
    }

    // SteamNetConnectionRealTimeStatus_t: ping at 4, quality local at 8 and remote at 12 (the share of packets that
    // arrived, 0..1, -1 unknown), out packets/bytes a second at 16/20, in at 24/28. SteamNetConnectionInfo_t: the relay's
    // POP id at 172 (three letters, packed as in SteamNetworkingPOPID), the flags at 440 (16: relayed).
    int   ping = 0, flags = 0;
    float local = 0, remote = 0, out_bps = 0, in_bps = 0;
    std::uint32_t pop = 0;

    std::memcpy(&ping, status + 4, 4);
    std::memcpy(&local, status + 8, 4);
    std::memcpy(&remote, status + 12, 4);
    std::memcpy(&out_bps, status + 20, 4);
    std::memcpy(&in_bps, status + 28, 4);
    std::memcpy(&pop, info + 172, 4);
    std::memcpy(&flags, info + 440, 4);

    char relay[5] = {};
    int  letters = 0;

    // (coop-punchlog: a three-letter POP, as most are, has a zero top byte: skipped, not taken for the end.)
    for (int i = 0; i < 4; i++) {
        char c = (char) ((pop >> (8 * (3 - i))) & 0xFF);

        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            relay[letters++] = c;
        }
    }

    std::snprintf(line, size, "ping %d ms, packets arrived %.0f%% here and %.0f%% there, out %.0f KB/s, in %.0f KB/s, flags %d (%s%s%s)", ping,
                  local * 100.0f, remote * 100.0f, out_bps / 1024.0f, in_bps / 1024.0f, flags, (flags & 16) != 0 ? "relayed" : "direct",
                  relay[0] != '\0' ? " by " : "", relay);
    return true;
}

int SteamCoreReceive(void *data, std::size_t size, std::uint64_t &from) {
    if (!g_up) {
        return 0;
    }

    SteamCorePump();

    void *message = nullptr;

    if (g_api.ReceiveOn(g_messages, 0, &message, 1) <= 0 || message == nullptr) {
        return 0;
    }

    // SteamNetworkingMessage_t on x64: the data pointer at 0, its size at 8, the sender's identity at 16.
    const auto *bytes = static_cast<const unsigned char *>(message);
    const void *payload = *reinterpret_cast<void *const *>(bytes);
    int         length = *reinterpret_cast<const std::int32_t *>(bytes + 8);
    Identity    who{};

    std::memcpy(who.bytes, bytes + 16, 136);
    from = g_api.GetId(&who);

    int got = length > 0 && (std::size_t) length <= size ? length : 0;

    if (got > 0) {
        std::memcpy(data, payload, (std::size_t) got);
    }

    g_api.Release(message);
    return got;
}

void SteamCoreStop(const char *why) {
    if (!g_up) {
        // Not up (or a failed try): the next use tries again.
        g_tried = false;
        return;
    }

    // As the coop-anything networking playbook asks of a Spacewar session: leave the lobby, clear the presence, shut down.
    for (u64 lobby : {g_lobby, g_join_lobby}) {
        if (lobby != 0 && g_api.LeaveLobby != nullptr) {
            g_api.LeaveLobby(g_match, lobby);
        }
    }
    if (g_api.ClearPresence != nullptr) {
        g_api.ClearPresence(g_friends);
    }
    if (g_api.Shutdown != nullptr) {
        g_api.Shutdown();
    }

    g_tried = g_up = false;
    g_user = g_match = g_friends = g_messages = nullptr;
    g_self = 0;
    g_hosting = false;
    g_lobby = g_create_call = 0;
    g_find = 0;
    g_join_call = g_join_lobby = g_join_owner = 0;
    g_join_name.clear();
    g_invite_lobby = g_invite_from = 0;
    g_lobby_keys.clear(); // (netplay_steam.cpp keeps them for the next Steam session's lobby)
    g_members = 2;
    g_stale_calls.clear();
    g_hosted = false;
    g_hosts.clear();
    g_scan.clear();
    g_list = 0;
    g_listed = false;
    g_list_said.clear();
    g_pick = 0;
    g_pick_said = false;
    g_pick_refused.clear();
    g_status = "Steam not started"; // (Steam Input, off unless DC_STEAM_INPUT_API, is not started again)
    std::fprintf(stderr, "netplay: steam: stopped (%s): lobby left, presence cleared, Steam shut down\n", why);
}

bool SteamCoreLobbySet(const char *key, const char *value) {
    if (g_up) {
        g_lobby_keys[key] = value; // coop-lobbysteam: set again on the next lobby this session opens
    }
    if (!g_up || g_lobby == 0) {
        return false;
    }
    return g_api.SetLobbyData(g_match, g_lobby, key, value);
}

const char *SteamCoreLobbyGet(const char *key) {
    const char *value = g_up && g_join_lobby != 0 ? g_api.GetLobbyData(g_match, g_join_lobby, key) : nullptr;

    return value != nullptr ? value : "";
}

bool SteamCoreInvited() {
    SteamCorePump();
    return g_invite_lobby != 0;
}

const char *SteamCoreName(std::uint64_t id) {
    const char *name = g_up ? g_api.FriendName(g_friends, id) : nullptr;

    return name != nullptr ? name : "";
}
namespace {

// Steam Input (Jep, 2026-10-21: with Steam Input on, Steam owns the pad once this copy runs as Spacewar, and SDL sees the
// pad but gets nothing from it). This copy becomes a Steam Input game: it hands Steam its action manifest
// (steam_input\dc_actions.vdf beside the exe, with a gamepad layout per controller type) and reads the actions back.
struct PadAction {
    const char   *name;
    std::uint16_t bit;
};
constexpr PadAction kPadActions[] = {{"cross", 0x0040}, {"circle", 0x0020}, {"square", 0x0080}, {"triangle", 0x0010}, {"l1", 0x0004}, {"r1", 0x0008},
                                     {"l2", 0x0001},    {"r2", 0x0002},     {"l3", 0x0200},     {"r3", 0x0400},       {"start", 0x0800}, {"select", 0x0100},
                                     {"up", 0x1000},    {"right", 0x2000},  {"down", 0x4000},   {"left", 0x8000}};
constexpr int kPadActionCount = (int) (sizeof kPadActions / sizeof kPadActions[0]);

bool  g_input_tried = false;
void *g_input = nullptr;
u64   g_input_set = 0;
u64   g_input_digital[kPadActionCount] = {};
u64   g_input_move = 0, g_input_camera = 0;
int   g_input_count = -1;

std::uint8_t Axis(float value, bool flip) {
    float v = 128.0f + (flip ? -value : value) * 127.0f;

    return (std::uint8_t) (v < 0.0f ? 0.0f : v > 255.0f ? 255.0f : v);
}

void StartInput() {
    g_input_tried = true;

    // Off unless DC_STEAM_INPUT_API=1: on real Steam, app 480 ignores our manifest (no action set ever appears, Steam's
    // controller log: "No cached sticky mapping"), and the pads play over raw HID instead (NetplayPadHints).
    if (!TestEnvOn("DC_STEAM_INPUT_API") || TestEnvIs("DC_NETPLAY_MUTATE", "no_steam_input")) {
        return;
    }

    char        path[1024] = "";
    const char *given = TestEnv("DC_STEAM_INPUT_MANIFEST");

#if defined(_WIN32)
    if (given != nullptr) {
        std::snprintf(path, sizeof path, "%s", given);
    } else {
        unsigned long size = GetModuleFileNameA(nullptr, path, sizeof path);

        while (size > 0 && path[size - 1] != '\\' && path[size - 1] != '/') {
            size--;
        }
        std::snprintf(path + size, sizeof path - size, "steam_input\\dc_actions.vdf");
    }
    bool have = GetFileAttributesA(path) != 0xFFFFFFFFul;
#else
    bool have = false;
#endif

    g_input = g_api.Input();
    // The manifest before Init as well as after (Steam's documentation is unclear which it needs).
    bool early = g_input != nullptr && have && g_api.InputManifest(g_input, path);
    bool init = g_input != nullptr && g_api.InputInit(g_input, true);
    bool manifest = init && have && g_api.InputManifest(g_input, path);

    std::fprintf(stderr, "netplay: steam input: manifest before Init %s\n", early ? "taken" : "refused");

    std::fprintf(stderr, "netplay: steam input: %s, manifest %s (%s)\n", init ? "on" : "would not start", manifest ? "given" : "NOT given", path);

    if (!init) {
        g_input = nullptr;
    }
}

} // namespace

bool SteamCorePad(SteamPadState &out) {
    out = SteamPadState{};

    if (!g_up) {
        return false;
    }

    if (!g_input_tried) {
        StartInput();
    }

    if (g_input == nullptr) {
        return false;
    }

    g_api.InputRunFrame(g_input, false);

    u64 handles[16] = {};
    int count = g_api.InputControllers(g_input, handles);

    if (count != g_input_count) {
        g_input_count = count;
        std::fprintf(stderr, "netplay: steam input: %d controller%s\n", count, count == 1 ? "" : "s");
    }

    if (count <= 0) {
        return false;
    }

    // Every two seconds while our action set is not there: which sets Steam knows (ours, and Spacewar's own).
    static int probe = 0;

    if (g_input_set == 0 && probe-- <= 0) {
        probe = 120;
        static const char *kSets[] = {"InGame", "ship_controls", "menu_controls", "InGameControls", "MenuControls"};
        std::string        seen;

        for (const char *name : kSets) {
            seen += std::string(" ") + name + "=" + std::to_string((unsigned long long) g_api.InputSet(g_input, name));
        }
        std::string types;

        for (int c = 0; c < count && c < 16; c++) {
            types += " " + std::to_string(g_api.InputType(g_input, handles[c]));
        }
        std::fprintf(stderr, "netplay: steam input: action sets%s; cross=%llu move=%llu; controller types%s\n", seen.c_str(),
                     (unsigned long long) g_api.InputDigital(g_input, "cross"), (unsigned long long) g_api.InputAnalog(g_input, "move"), types.c_str());
    }

    if (g_input_set == 0) {
        g_input_set = g_api.InputSet(g_input, "InGame");

        for (int i = 0; i < kPadActionCount; i++) {
            g_input_digital[i] = g_api.InputDigital(g_input, kPadActions[i].name);
        }
        g_input_move = g_api.InputAnalog(g_input, "move");
        g_input_camera = g_api.InputAnalog(g_input, "camera");

        if (g_input_set != 0) {
            std::fprintf(stderr, "netplay: steam input: actions ready (set %llu)\n", (unsigned long long) g_input_set);
        }
    }

    if (g_input_set == 0) {
        return false;
    }

    // The first controller with anything pressed or pushed this frame, else the first.
    for (int c = 0; c < count && c < 16; c++) {
        SteamPadState pad{};

        g_api.InputActivate(g_input, handles[c], g_input_set);

        for (int i = 0; i < kPadActionCount; i++) {
            if (g_input_digital[i] != 0 && g_api.InputDigitalData(g_input, handles[c], g_input_digital[i]).state) {
                pad.buttons |= kPadActions[i].bit;
            }
        }

        AnalogData move = g_api.InputAnalogData(g_input, handles[c], g_input_move);
        AnalogData camera = g_api.InputAnalogData(g_input, handles[c], g_input_camera);

        pad.left_x = Axis(move.x, false);
        pad.left_y = Axis(move.y, true);
        pad.right_x = Axis(camera.x, false);
        pad.right_y = Axis(camera.y, true);
        pad.input = pad.buttons != 0 || move.x * move.x + move.y * move.y > 0.1f || camera.x * camera.x + camera.y * camera.y > 0.1f;

        if (c == 0 || pad.input) {
            out = pad;
        }
        if (pad.input) {
            break;
        }
    }

    return true;
}

bool SteamCoreInputOn() {
    return g_up && g_input != nullptr;
}

const char *SteamCorePickRefused() {
    return g_pick_refused.c_str();
}