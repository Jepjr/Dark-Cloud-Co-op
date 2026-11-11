#pragma once

#include "coop/game_ns.hpp"

// coop-lobby (Jep, 2026-10-30): the multiplayer lobby between the title's hosting pick
// and the game. MULTIPLAYER > HOST > START or CONTINUE picks the save first (a new game, or a slot loaded by the load
// screen), then the lobby opens over the title's backdrop: a header board, six seat cards, six character boards in party
// order and two message windows (layout C, lobby-mock-C2-host.png). Each player chooses a member with Left and Right, locks
// it with Cross (a locked member is nobody else's), unlocks with Circle; Circle while choosing asks "Leave? Yes / No" on
// that player's own seat card. When every seat present is locked the host's START lights and Start begins the game.
//
// The seat table is the host's and the only shape: six seats, no player 1 / player 2 branches. Seats past the link's
// capacity (kLinkPlayers) are drawn dimmed "coming later". Every change logs one "lobby: ..." line.
//
// Local players (game.lobby_local, DC_LOBBY_LOCAL overrides; Jep, 2026-10-30 21:42Z: both shapes, a switch to flip):
// "combined" (the default) has no LOCAL row in the title menu and any lobby takes a local player by Start on a free
// controller; "separate" keeps MULTIPLAYER > LOCAL, whose lobby is local only, and the HOST's lobby refuses local pads.

#include <cstdint>
#include <string>

namespace coop {
namespace lobby {

constexpr int kSeats = 6;
// The link's capacity: seats past it are dimmed "coming later" until the N-player link exists (plan 4.6).
constexpr int kLinkPlayers = 2;

// Where local players come in (plan 4.1).
enum class LocalShape { Combined, Separate };
LocalShape Local();
const char *LocalName(LocalShape shape);

struct Seat {
    bool          present = false;
    bool          local = false;   // a player on this PC (the host's own seat too)
    std::uint64_t identity = 0;    // SteamID or player_id (stage 5); 0 not known
    std::string   name;            // the profile name (stage 5); "Player N" until then
    int           hover = -1;      // the member the seat's hand is on while choosing
    int           pick = -1;       // the member locked, -1 none
    bool          leaving = false; // the "Leave?" question is up on the seat's card
    bool          leave_yes = false;
    bool          naming = false;  // the card asks for the seat's profile (plan 4.10)
    bool          active = false;  // lit: its player pressed Start on their controller (Jep, 2026-10-31)
    // This copy's own profile ask: the profile shown (past the last: "New"), and a new name being typed.
    int           profile = 0;
    bool          typing = false;
    std::string   entry;
    int           letter = 0;
    // A player on this PC (plan 4.6): its controller, the split / multi screen question, its own window's copy started and
    // not yet joined; its locking Cross let go (a swing only after it, as the host's own seat's attack_armed).
    int           pad = -1;
    bool          screen_ask = false;
    bool          screen_multi = true;
    bool          awaiting_copy = false;
    bool          armed = true;
    // coop-lobbyleave (Jep's pack 8 playtest, 2026-11-04: neither saw the other's card or locked model move): its locked
    // model's stick and swing (a seat played here: as its player drives it; another copy's: as that copy sent it), and
    // another copy's naming card as that copy shows it (its profile, or the name being typed when shown_typing).
    float         drive_x = 0.0f;
    float         drive_y = 0.0f;
    bool          drive_attack = false;
    std::string   shown;
    bool          shown_typing = false;
};

// What the lobby hands the title (Step).
enum class Result { Running, Start, Closed };

// The title's side. Pend: the hosting pick (START or CONTINUE) was made through the lobby's way, so the save's pick opens
// the lobby instead of starting the game; local_only: MULTIPLAYER > LOCAL under the separate shape.
void Pend(bool pend, bool local_only = false);
bool Pending();
// Opens the lobby once the save is known: new_game (the opening book follows the start), else the slot just loaded.
void Open(bool new_game);
bool Active();
// One frame of input; Start once the host has started (the screen has faded), Closed once it was left.
Result Step();
// Draws the lobby over the title's backdrop (after the 3D pass and the fog, before the feedback).
void Draw();

// The link's side (netplay.cpp): the host lobby listens (its socket read from the link's tick while no link runs); a
// "BLCD" datagram for the lobby (true when it was one, consumed); each frame's sends and timeouts; whether the lobby last
// shown was a guest's (the title's menu then stays at JOIN's level).
bool Listening();
bool Receive(std::uint32_t magic, const void *data, int size);
void NetTick();
bool WasGuest();
// coop-lobbyleave: a guest's lobby is up on this copy (its join waits there for the host's start; only its Leave? gives the
// join up, never the title's Circle cancel).
bool GuestIn();

// The host, as a player links after the lobby (or with none): the member it plays by plan 4.5 (the one it had if free, else
// the first free usable one in party order), logged "lobby: late join ...". Its apply goes with coop-localmenu's command
// channel (a change after the host's checkpoint stays in lockstep only as a command); until then the game's own choice holds.
int LateJoin(std::uint64_t identity);

// coop-joinlobby: a joiner's link Hellos wait while its card is open (the host's game has not accepted its pick yet, and no
// lobby of the host's started); its card closes as the host's table accepts it. The host: the pick LateJoin took for the
// joiner (its member, its profile's name), once, for the catch-up's last blob (false: none to carry).
bool JoinHoldsHello();
bool JoinPickTake(int &member, std::string &name);

// The table, for the game after the start and for a later test: seat k's member, -1 none.
int  PickOf(int seat);
bool NewGame();

} // namespace lobby
} // namespace coop
