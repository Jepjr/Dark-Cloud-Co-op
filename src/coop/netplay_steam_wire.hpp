#pragma once

#include "coop/game_ns.hpp"

// coop-steamhelper: the game <-> helper messages (netplay_steam.cpp, src\helper\dcsteam.cpp), datagrams on 127.0.0.1. The game
// binds a port and starts "dcsteam.exe --port P --parent PID --rate KB" (its environment and stderr inherited, so the helper's
// "netplay: steam:" lines land in the game's log); the helper answers from a port of its own. The first byte says what follows.
//
// Game to helper:
//   'H' host, the build tag            'F' find a host, the build tag (each frame while it looks)   'R' find reset
//   'S' send: 8-byte SteamID, data     'L' lobby set: key \0 value \0     'W' watch a lobby key: key \0
//   'N' a SteamID's name: 8 bytes      'Z' watch the session to a SteamID (its queue and line): 8 bytes
//   'Q' quit: why (the helper stops Steam and exits)                       'K' keepalive (each second)
//   coop-lobbysteam: 'O' listen: '1' or '0', the build tag   'A' list the friends hosting: the build tag (each frame while
//   the list shows)   'P' the friend's lobby picked: 8 bytes (0: none)
// Helper to game:
//   'T' its state, "key=value" lines: ready (Steam's start tried), up, self, status, invited, find, host, hostname, paced,
//       inputon, qok, qpend, qrate, line, and "lk.<key>" / "nm.<id>" for watched lobby keys and asked names; listen (0 not,
//       1 opening, 2 open); while the list is asked for, fh (its answer), fn (how many) and per friend i "f<i>" =
//       lobby \t host \t flags (1 invited, 2 another build) \t name, "fs<i>" = its seats key; refused (the picked lobby \t why
//       it refused this copy, "" while it has not: coop-lobbysteam-deadpick)
//   'D' a datagram from Steam: 8-byte SteamID, data
// A helper that hears nothing from the game for kSteamHelperSilentMs, or whose game process ended, stops Steam and exits.

constexpr int kSteamHelperSilentMs = 15000;
constexpr int kSteamHelperMaxDatagram = 65536;
