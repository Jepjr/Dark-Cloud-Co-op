#pragma once

#include "coop/game_ns.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "coop/netplay_steam_core.hpp"

// coop-steamhelper: every call below goes to the Steam helper process (dcsteam.exe beside the game, src\helper\dcsteam.cpp),
// started by NetSteamStart and gone after NetSteamStop, so the game never loads steam_api64.dll and Steam shows it as Spacewar
// only while the helper runs. With DC_STEAM_IN_PROCESS=1, DC_STEAM_INPUT_API=1 or DC_NETPLAY_MUTATE=steam_in_process (the
// twin) the calls run in this process (netplay_steam_core.cpp), as before the helper.
//
// The co-op link over Steam (netplay's game.netplay 3, one client): traffic between two SteamIDs through
// Valve's relays, so neither player forwards a port or types an address. It runs as Valve's free sample
// app, AppID 480 "Spacewar" (coop-anything playbook/networking-steam-spacewar.md): steam_api64.dll is
// loaded at run time through Steam's flat C API (no SDK headers), from DC_STEAM_API or beside the exe.
// The host opens a friends-only lobby marked as Dark Cloud co-op; a guest finds a friend hosting one
// through the friends list and joins it. Windows only; elsewhere every call says no.

// Loads the DLL and starts Steam once (later calls answer from the first). False when the DLL, Steam
// or its login is missing; NetSteamStatus then says why.
bool NetSteamStart();

// Ends this copy's Steam session (Jep, 2026-10-21: the game shows as Spacewar only while Steam is in use): leaves its
// lobby, clears its rich presence and shuts Steam down; the next NetSteamStart starts it again. why goes in the log.
void NetSteamStop(const char *why);
const char *NetSteamStatus();

// This copy hosts: a friends-only lobby for two, marked with the build tag, and rich presence a friend's
// Join Game can use. Once; true when Steam is up (the lobby itself arrives a few frames later).
bool NetSteamHost(const char *build_tag);

// A guest looking for a friend hosting Dark Cloud co-op: call once a frame. 0 while it looks, 1 when it
// is in that friend's lobby (host and name filled in), -1 when no friend hosts (or Steam is out).
int NetSteamFindHost(const char *build_tag, std::uint64_t &host, char *name, std::size_t name_size);
void NetSteamFindReset();

// Runs Steam's callbacks (lobby results, session requests). The receive below runs it too.
void NetSteamPump();

// A datagram to a SteamID (unreliable, no Nagle delay: the link resends itself), and the next one
// waiting from anyone (its sender in from); 0 when none is. coop-steamrate: the send says 1 sent, -1 refused because
// Steam's send buffer is full (k_EResultLimitExceeded: send it again later), 0 any other failure.
int  NetSteamSend(std::uint64_t to, const void *data, std::size_t size);
int  NetSteamReceive(void *data, std::size_t size, std::uint64_t &from);

// coop-steamjoin: the session's send queue to a SteamID: bytes Steam holds unsent (its unreliable ones) and the rate it
// sends at (bytes a second). False when Steam is out, the DLL lacks the call, or there is no session yet.
bool NetSteamQueue(std::uint64_t to, int &pending_bytes, int &rate);

// coop-steamrate: whether the host paces a join's pieces itself by what reaches the other copy (game.netplay_steam_rate
// 0, the default), Steam's rate its ceiling; false with a fixed rate (Steam's pace alone) or DC_NETPLAY_MUTATE=steam_pace_off.
bool NetSteamPaced();
// coop-steamceiling: Steam's own view of the session to a SteamID as one log line (packets arrived each way, bytes a
// second, relayed or direct). False when there is no session.
bool NetSteamLine(std::uint64_t to, char *line, std::size_t size);

// This copy's own SteamID (0 before Steam is up).
std::uint64_t NetSteamSelf();

// A SteamID's persona name (a friend's, as Steam shows it); "" when Steam is out or does not know it.
const char *NetSteamName(std::uint64_t id);

// Whether a Steam invite to a lobby was accepted and waits (NetSteamFindHost then joins it first).
bool NetSteamInvited();

// The pad as Steam Input reads it (steam_input\dc_actions.vdf), while Steam runs this copy as Spacewar and owns the controllers:
// false when Steam (or Steam Input, or a controller) is not there, and SDL's pads are the ones to read.
using NetSteamPadState = SteamPadState; // (netplay_steam_core.hpp)
bool NetSteamPad(NetSteamPadState &out);

// The host's open lobby: a key's value (NetSteamLobbySet; false before the lobby is open). A guest in a friend's
// lobby: a key's value there ("" when none). Host > NAT > Steam trades the NAT codes so.
bool        NetSteamLobbySet(const char *key, const char *value);
const char *NetSteamLobbyGet(const char *key);

// Whether Steam Input reads the pads now (NetSteamPad): Steam must stay up while it does, or the pads go quiet.
bool NetSteamInputOn();

// coop-steamhelper: whether a steam_api64.dll (DC_STEAM_API's file, else Steam's own) is loaded in this process now (the
// tests' check that the game itself never loads it); and the helper's process id while it runs, else 0.
bool          NetSteamLoadedHere();
unsigned long NetSteamHelperPid();

// coop-lobbysteam (the lobby plan; Jep, 2026-10-30): Steam in the lobby.
//
// The host. Select in the lobby, and in the game's main menu, listens: on opens a lobby for six (kSteamListenMembers) that
// friends find by Join Game, an invite or JOIN > STEAM's list, with the host's code ("nat", NetSteamLobbySet as today) and
// the seat table (NetSteamLobbySetData) on it; off closes the lobby and, unless a friend's Steam datagrams came in the last
// few seconds (a join still trading the codes: the direct link's NetSteamStop ends it), Steam with it, so the game does not
// show as Spacewar. The lobby starting the game says NetSteamStop("lobby started"). The keys set are kept here and set
// again on every lobby opened later, through a stop too (the main menu's listen after the start has the code and table).
// The lobby is friends-only (Jep, 2026-10-30), so no stranger reads its seat table. Steam's lobby search lists no friends-only
// lobby (Jep's 2026-10-21 retest), so a friend who hides their game activity is not in the joiner's list: the host invites
// them from Steam's friends list (Invite to game), and their accepted invite joins.
bool NetSteamLobbyListen(bool on);
// The tag the lobby and the list carry and look for: netplay.cpp's kSteamTag, the same words (one of them to go).
constexpr const char *kNetSteamTag = "darkcloud-coop-1";
// Whether the host's lobby is open (friends can find it): the lobby's "Steam: listening".
bool NetSteamListening();
// coop-pt1108s: asked for, the helper up, Steam not yet (the helper tries again by itself; the lobby waits for NetSteamListening).
bool NetSteamListenWaits();
// The seat table, opaque here: carried as the lobby's "seats" key (base64, at most kSteamSeatsBytes; past it not set, and
// said in the log), on the lobby open now and every one later.
constexpr std::size_t kSteamSeatsBytes = 6000;
void NetSteamLobbySetData(const std::string &seats);

// The joiner. JOIN > STEAM lists every friend hosting and the player picks one before the join goes in, even when one is
// listed (Jep: a safety measure); an invite accepted in Steam is a pick of its own.
struct NetSteamFriendHost {
    std::uint64_t lobby = 0, host = 0;
    std::string   name;                // the friend's Steam name
    std::string   seats;               // the lobby's seat table as the host set it ("" while Steam has not said)
    bool          invited = false;     // the lobby of an invite accepted in Steam
    bool          other_build = false; // another build of Dark Cloud co-op: a join would be refused
};
// Each frame while the list shows (it looks again every few seconds): 1 a list stands (empty or not), 0 the first look
// still out, -1 Steam is out (NetSteamStatus says why; coop-lobbysteam-retry: Steam's start is tried again every 5 s
// while it is, so a Steam started after the list opened fills it).
int  NetSteamFriendsHosting(std::vector<NetSteamFriendHost> &out);
// The friend's lobby picked from the list; 0 forgets it (so does NetSteamStop). NetSteamFindHost joins that lobby and no
// other: with none picked (and no invite) it says -1 at once, its status saying so. DC_NETPLAY_MUTATE=steam_autopick (the
// twin): the first friend listed, as before the list.
void NetSteamJoinPick(std::uint64_t lobby);
std::uint64_t NetSteamJoinPicked();
// coop-lobbysteam-deadpick: why the picked friend's lobby would not take this copy ("" while it has not): their game closed
// after the list showed it, or it runs another build. NetSteamFindHost then says -1 and joins it no more; the join goes
// back to JOIN > STEAM's list with this as its note (netplay.cpp). Cleared by the next NetSteamJoinPick.
std::string NetSteamPickRefused();
// A guest in the friend's lobby: the host's seat table ("" when none).
std::string NetSteamLobbyData();
