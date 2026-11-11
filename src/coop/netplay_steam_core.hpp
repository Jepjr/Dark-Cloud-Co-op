#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// coop-steamhelper (Jep, 2026-10-30: Steam kept showing "Spacewar" after the punch, for as long as the game ran): Steam itself,
// through steam_api64.dll's flat C API (netplay_steam_core.cpp). It runs in the helper process (dcsteam.exe, src\helper), which
// the game starts for NAT > Steam and which exits once Steam is no longer needed; the game reaches it through
// netplay_steam.hpp. In the game process only with DC_STEAM_IN_PROCESS=1 (or Steam Input's test path,
// DC_STEAM_INPUT_API=1), as before the helper. Each call is netplay_steam.hpp's of the same name, NetSteam -> SteamCore.

struct SteamPadState {
    std::uint16_t buttons = 0;
    std::uint8_t  left_x = 128, left_y = 128, right_x = 128, right_y = 128;
    bool          input = false; // anything pressed or pushed
};

bool          SteamCoreStart();
void          SteamCoreStop(const char *why);
const char   *SteamCoreStatus();
bool          SteamCoreHost(const char *build_tag);
int           SteamCoreFindHost(const char *build_tag, std::uint64_t &host, char *name, std::size_t name_size);
void          SteamCoreFindReset();
void          SteamCorePump();
int           SteamCoreSend(std::uint64_t to, const void *data, std::size_t size);
int           SteamCoreReceive(void *data, std::size_t size, std::uint64_t &from);
bool          SteamCoreQueue(std::uint64_t to, int &pending_bytes, int &rate);
bool          SteamCorePaced();
bool          SteamCoreLine(std::uint64_t to, char *line, std::size_t size);
std::uint64_t SteamCoreSelf();
const char   *SteamCoreName(std::uint64_t id);
bool          SteamCoreInvited();
bool          SteamCorePad(SteamPadState &out);
bool          SteamCoreLobbySet(const char *key, const char *value);
const char   *SteamCoreLobbyGet(const char *key);
bool          SteamCoreInputOn();

// coop-lobbysteam (the lobby plan, 4.3 and stage 6): the host listens with a lobby for six; a guest lists every friend hosting
// and picks one before its join goes in (netplay_steam.hpp's NetSteamLobbyListen, NetSteamFriendsHosting, NetSteamJoinPick).
constexpr int kSteamListenMembers = 6;

struct SteamFriendHost {
    std::uint64_t lobby = 0;
    std::uint64_t host = 0;
    bool          invited = false;     // the lobby of an invite accepted in Steam
    bool          other_build = false; // its lobby says another build of Dark Cloud co-op (a join would be refused)
    std::string   name;                // the friend's persona name
    std::string   seats;               // the lobby's "seats" key as set (netplay_steam.cpp encodes and decodes it)
};

bool SteamCoreListen(bool on, const char *build_tag);
int  SteamCoreListening(); // 0 not, 1 the lobby opening, 2 open
int  SteamCoreFriendsHosting(const char *build_tag, std::vector<SteamFriendHost> &out);
void SteamCorePick(std::uint64_t lobby);
// coop-lobbysteam-deadpick: why the picked lobby refused this copy, "" while it has not (until the next pick).
const char *SteamCorePickRefused();
