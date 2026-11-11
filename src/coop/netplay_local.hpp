#pragma once

#include "coop/game_ns.hpp"

#include <cstdint>
#include <string>

// coop: the title's Multi-player > Local (title/title_mpmenu.hpp): one PC, two copies, as "Play Dark Cloud Online (this
// PC test).cmd" (tools/coop-qa/netplay/online-this-pc.ps1) runs them, but started from the game. This copy hosts on
// 127.0.0.1 with controller 1; once its game starts (a town or a dungeon) it starts player 2's copy itself: the same
// exe and data, a save folder of its own (local-p2 under this one's, its config seeded once from this one's: windowed
// on the right, coop on), joining 127.0.0.1 at once with no logos or title (--jump edit:0, DC_NETPLAY=3, DC_NETPLAY_TEST_JOIN); its controller is the first one
// pressed that is not player1_pad (DC_NETPLAY_LOCAL_CLAIM: it asks "Player 2: press any button").
// Player 2's copy closes with this one (a job object). DC_NETPLAY_LOCAL_ARGS adds arguments to it (a test's --headless
// --frames N). Windows only. Returns the new copy's process id, 0 when it could not start (why says why).
unsigned long NetplayLocalSpawn(int player1_pad, char *why, unsigned long why_size);
// coop-lobby: the same copy for a lobby's seat (multi screen): to its title, where its join opens the host's lobby on its
// own window, its Hello asking for that seat (DC_LOBBY_SEAT).
unsigned long NetplayLocalSpawnToLobby(int player1_pad, int seat, char *why, unsigned long why_size);
// coop-lobby: the copy for a lobby's Multi screen seat at the lobby's start (Jep, 2026-11-01): straight into the game as
// Local's player 2's copy, but on controller pad from the first frame (DC_NETPLAY_LOCAL_PAD: the lobby's hand-off, no claim)
// and as the seat's identity (DC_NETPLAY_PLAYER_ID: its profile and member, the lobby's LateJoin).
unsigned long NetplayLocalSpawnHanded(int player1_pad, int pad, std::uint64_t identity, char *why, unsigned long why_size);

// coop-lobbyslots (Jep's pack 7 playtest, 2026-11-02: P1's pad played both): a controller named by its device, which every
// copy on this PC agrees on (netplay.cpp): pad's device ("" none known), and this copy's number for a device (-1 not open).
// Player 2's copy is handed both (DC_NETPLAY_LOCAL_PAD_DEVICE, DC_NETPLAY_LOCAL_CLAIM_DEVICE) beside the numbers.
std::string NetplayPadDevice(int pad);
int         NetplayPadOfDevice(const std::string &device);
