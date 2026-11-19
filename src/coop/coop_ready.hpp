#pragma once

// coop-readyscreen (Jep 11-13: "one who's-coming flow for story starts and item scenes"): the one ready check behind every story
// start that wants the other players along (a door into a room whose arrival story is still to play, a room's story event point,
// the Georama menu's Event pick) and the item-scene vote (Wise Owl flute B9, Shipwreck music box key B17).
//
// Every other linked player is asked at once, each on their own screen ("<P1> wants to start a story scene. Come along?"); the
// player who started it waits in a box with a row per other player (their face and a status: accepted / not answered yet /
// declined, live) and two buttons: CANCEL (nothing starts, nobody moves, an item is kept) and START NOW (it starts with the
// players who accepted; the others are left where they are). Every other player accepted: it starts by itself.
//
// This file is the decision alone: who was asked, what each said, and what the initiator's pick or the answers make of it. It
// holds no game state of its own beyond that table and reads nothing of the game, so the callers (coop_worlds: the door, the
// item scene) step it on every linked copy alike from the agreed pads, and a test runs it with any number of players
// (DC_COOP_TEST_READY_TABLE). A table by player number, never "the other player" (Jep 11-09: design for three).

#include "coop/coop_actor.hpp" // kMaxPlayers

namespace coop::ready {

enum Status : int {
    kNotAsked = 0, // the initiator, or a player not linked here
    kPending = 1,  // asked, not answered yet (its question may have timed out: still not answered)
    kAccepted = 2,
    kDeclined = 3, // said no, or left the game while asked
};

enum Kind : int {
    kNone = 0,
    kStoryDoor = 1, // a story door, a room's story point or the Georama's Event pick (coop_worlds doorgate)
    kItemScene = 2, // an item used at its spot starts a story scene (coop_worlds CoopWorldsItemSceneAsk)
    kStoryArrival = 3, // coop-firstvisit: a town's story arrival, apart: its starter held on black (coop_worlds arrive gather)
};

enum Outcome : int {
    kWaiting = 0,  // still open
    kStartAll = 1, // every other player accepted: it starts by itself, with everyone
    kStartNow = 2, // the initiator's START NOW: it starts with the players who accepted
    kCancel = 3,   // the initiator's CANCEL (or Circle): nothing starts
};

// The initiator's box: its two buttons, in this order (the hand starts on CANCEL: a Cross held from the start never starts it).
enum Pick : int { kPickNone = 0, kPickCancel = 1, kPickStartNow = 2 };
constexpr int kButtonStartNow = 0;
constexpr int kButtonCancel = 1;

struct Session {
    Kind   kind = kNone;
    int    initiator = 0; // 1 up
    Status status[kMaxPlayers + 1] = {};
    bool   question[kMaxPlayers + 1] = {}; // that player's question still up on its screen
    bool   waits[kMaxPlayers + 1] = {};    // coop-askwait: asked, its question waiting (not drawn) for that player to be free
    int    cursor = kButtonCancel;         // the initiator's hand
    int    changes = 0;                    // status changes so far (the box lays out again on each)
    bool   cancel = true;                  // coop-firstvisit: the box has CANCEL (a story arrival's has none: it cannot be undone)
    bool   start_now = true;               // coop-firstvisit: START NOW is offered (a story arrival's may wait for player 1)
};

// Opens a session: `linked[p]` (1 up) says which players are linked here with the initiator (the initiator's own entry is
// ignored). False, and nothing open, when no other player is linked (the start is the initiator's alone) or one is open.
bool Open(Kind kind, int initiator, const bool linked[kMaxPlayers + 1]);
// coop-firstvisit: the open session's buttons (CANCEL offered, START NOW offered); the hand goes to START NOW when CANCEL is gone.
void Buttons(bool cancel, bool start_now);
bool IsOpen();
const Session &Get();

// A player's question is up on their screen (pending, not timed out, not waiting for the player to be free).
bool Asking(int player);
// coop-askwait (Jep 11-15: a request with a Cancel never expires, a busy player is never auto-rejected): a busy asked player's
// question waits, not drawn, its row "Not answered yet", and comes up once the player is free. Waits: it is waiting so.
void QuestionWaits(int player, const char *why);
void QuestionComesUp(int player);
bool Waits(int player);
// The first player whose question is up, 0 none (a single question window per copy: the copies' own screens show theirs).
int FirstAsked();

// The answers. Each status change is logged ("ready: player N: accepted", ...), so a test reads every step.
void Answer(int player, bool accepted);
void QuestionTimedOut(int player); // its question closes; it stays "not answered yet" (the initiator decides)
void Left(int player);             // gone while asked (the link dropped, apart): declined

// The initiator's box: its hand (Up/Down) and pick. Decide takes the pick (kPickNone while it only waits) and says what the
// session comes to; anything but kWaiting closes it. Accepted(p) stays readable until the next Open, for the caller's start.
void   MoveCursor();
Outcome Decide(Pick pick);
bool   Accepted(int player);
int    CountAccepted();
void   Close(const char *why);

// The other players in the box's order (player number order), and what a row says.
int         Rows(int players[kMaxPlayers]);
const char *StatusText(Status status); // "Accepted", "Not answered yet", "Declined"

// DC_COOP_TEST_READY_TABLE=1 (the 3-player table row, coop_core's test hooks): the decision for three and four players run
// through every path, one line each, "ready: table: done" at the end.
void TableTest();

} // namespace coop::ready
