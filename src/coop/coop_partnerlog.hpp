#pragma once

#include "coop/game_ns.hpp"

#include <cstdint>

// coop-partnerlog (Jep, 2026-11-01; Jep's 2026-10-30 playtest ask, lan-pack6 item 10): each copy's log reaches the other
// PC. A copy reads its own log file as the game writes it (stdout and stderr, whatever file the launcher sent them to:
// save\online-log.txt in a pack, save\log.txt from Playtest.cmd) and sends the new lines over the link's socket; the
// other copy writes them to partner-log.txt in its save folder. What the other copy has not acknowledged is kept (the
// last kKeepBytes) while the link is down or apart and goes on once it hears again, so a rejoin brings the lines in
// between. Nothing here touches game state: datagrams and two files only.
//
// Log lines: "partnerlog: sending this copy's log (<files>)" once; "partnerlog: player N's log (run R) to <path>" on the
// first lines of each of the other copy's runs. DC_NETPLAY_MUTATE=partnerlog_off (twin): nothing is sent.

// Once a tick from NetplayTick: reads new log bytes and sends what the window and pace allow.
void PartnerLogTick();

// Netplay's Receive, for a datagram from the other player: true if it was this module's (data or acknowledgement).
bool PartnerLogReceive(std::uint32_t magic, const void *data, int size);

// coop-catchupdrop: the bytes of the other copy's log written to partner-log.txt in this game so far.
std::uint64_t PartnerLogTakenBytes();
