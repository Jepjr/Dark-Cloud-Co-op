#pragma once

#include "coop/game_ns.hpp"

#include <cstdint>

// coop-localmenu (cause 3, stage 1): commands. A player's committed action that changes the
// shared world (a menu's equip, use, send, escape ...) goes into the lockstep as a command: it rides that player's next input
// frame (the Entry beside its pad, resent with it) and every linked copy applies it on that frame's link tick, player 1's
// before player 2's, after the tick's pads are taken. A copy alone applies its own at once, and a catch-up's recording keeps
// it (a progress delta, kWhatCommand) so the joiner's replay applies it on the same tick. Up to N players: the player is the
// link's number (1 up), never a "his" or "hers".
struct NetCommand {
    std::uint8_t  kind = 0; // NetCommandKind; 0 none
    std::uint8_t  a = 0;
    std::uint16_t b = 0;
    std::uint32_t c = 0;
};

enum NetCommandKind : std::uint8_t {
    kCmdNone = 0,
    kCmdTest = 1, // the tests' (DC_NETPLAY_TEST_COMMAND): applied, it logs who sent it and the tick
    kCmdMenu = 2, // a player's menus opened or closed: a = the ActorMenuKind bits open now, b = the player (coop_actor.hpp)
    kCmdPoke = 3, // a local menu's write to the party status: b = the 4-byte word, c = its value (coop_fullmenu.cpp)
    kCmdPick = 4, // player 2's member pick in her local menu: a = the member (coop_fullmenu.cpp)
    kCmdGive = 5, // a local menu's bag screen send: b = the item, c = its uses, into the other player's bag (coop_inventory.cpp)
    kCmdServe = 6, // the tests' (DC_QA_SERVE, a linked pair): this copy has read serve job c (netplay.cpp ServeFrameStart)
    kCmdSlot = 7,  // coop-catchuprace: player 2's live-sync record c, reached the host after its catch-up's last blob (coop_worlds.cpp)
    kCmdProgress = 8, // coop-catchuprace: player 2's progress c (a flag, floor, Atla ...), likewise (coop_worlds.cpp ApplyLateProgress)
    kCmdGeoMenu = 9, // a Georama parts menu run on its player's copy alone shut: a = EditMenuStatus.mode, b = its part + 1, c = its event
                     // (coop_georama_own.cpp, coop-stage4; 9 because train 55 gave 7 and 8 to coop-catchuprace)
    kCmdPartsPoke = 10, // coop-pt1108: a Georama parts menu's write outside the party status: b = the word (coop_georama_own.cpp), a = which of its bytes, c = its value
    kCmdScriptMenuEnd = 11, // coop-scriptmenus: a menu an event's script opened, run on its opener's copy alone, ended: a = its kind (ED_MENU_*), c = its result
                            // (the item, the map; coop_scriptmenu.cpp), applied on every copy after its pokes, where the script takes it
    kCmdStorageClaim = 12, // coop-small2 (storage C): a pick-up from the party's storage claims its cell: a = page | take<<2 | swap<<3, b = the cell, c = the item seen (coop_storage.cpp)
    kCmdStorageMove = 13,  // coop-small2: a put-down (or a sort, or a staged word of a record) into the party's storage: a = page | what<<2 | staged<<4, b = the cell, c = the value
    kCmdPresent = 14, // coop-opt7 (14 in train 56: 11 is coop-scriptmenus', 12-13 coop-small2's): a chest's presentation on its opener's copy ended: a = its kind, b = the player (coop_opener.cpp; 11 is the next free on cand56: renumber at the merge)
    kCmdQuit = 15, // coop-quittitle (15 at the repin's rebase onto train 56: 12-14 are coop-small2's and coop-opt7's): the sender's Quit Game from its in-game menu (no fields): the host's (player 1's) ends the session for
                   // every player, anyone else's takes that player alone to the title (netplay.cpp ApplyQuit)
};

// This copy's own player's command, applied on every copy on its frame's tick (see above).
void NetplayQueueCommand(const NetCommand &command);

// What applies a kind: player is 1 up, tick the link tick it is applied on (the recording's on a replay).
using NetCommandHandler = void (*)(int player, const NetCommand &command, std::int64_t tick);
void NetplaySetCommandHandler(std::uint8_t kind, NetCommandHandler handler);

// The progress delta's kind (coop_worlds' What) for a recorded command.
constexpr std::int32_t kWhatCommandDelta = 15;

// A command as a catch-up's recording carries it (coop_worlds' kWhatCommand delta: [1] player | kind << 8 | a << 16, [2] b,
// [3] c), applied on the replay's tick.
void NetplayApplyRecordedCommand(const std::int32_t delta[5]);
