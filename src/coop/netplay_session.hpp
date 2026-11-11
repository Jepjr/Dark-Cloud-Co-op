#pragma once

#include "coop/game_ns.hpp"

// coop-session (root-cause analysis 2026-10-30, cause 4; Jep approved): where this copy's link is, as one state with named
// transitions, instead of loose flags (g_join_picked, listening, started, up, g_links_up, the worlds' apart state) that each
// call site combined its own way. Items 9 (the title's join cancel fired after a link) and 28 (a fresh join from the title
// held back as an apart peer) were both a check against the wrong state.
//
//   Title     not linking (no Join picked, not hosting yet)
//   Hosting   player 1 listening, never linked yet
//   Joining   player 2 picked Join (address, NAT or Steam), never linked yet
//   Linked    in lockstep
//   Apart     the pair split on purpose (a door, an exit): each copy plays its own world, the merge links them again
//   Dropped   the link went down while together (a network cut, the other copy gone): apart in place
//   Rejoining the other copy came back new from its title (its Hello says first link) while this one had it Apart/Dropped
//
// Every change logs "netplay: session: <from> -> <to> (<why>)"; a change the table does not allow is refused and logged
// "netplay: session: refused <from> -> <to> (<why>)" (the state stays).

enum class NetSession { Title, Hosting, Joining, Linked, Apart, Dropped, Rejoining };

NetSession  NetSessionNow();
const char *NetSessionName(NetSession state);
bool        NetSessionTo(NetSession next, const char *why); // false (and logged) when the table refuses it

// DC_NETPLAY_MUTATE=session_off (the class check's fix-off, debug/qa/root-fixes.txt): the decisions the session makes go back to
// the loose flags' (the join cancel on any listening join, a new copy taken only after a drop).
bool NetSessionOff();
