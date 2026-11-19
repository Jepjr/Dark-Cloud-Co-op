#pragma once

#include "coop/game_ns.hpp"

// coop-rumble (Jep's pack 8 playtest, 2026-11-03: P1's hits shook P2's controller): rumble is presentation
// (coop_present.hpp), so each copy shakes its own player's controller and only for that player's own hits. The game's
// motors (CGamePad::SetVibration on the game pad's port 0: the walker's hits, his fishing line) are the game's walker's.
// Every other player's motors are kept here, one pair per player, set as CGamePad::SetVibration sets the game's.
// Neither is game state: every copy steps every player's alike, and only the send reads which player this copy plays.
//
// coop-p2rumble (the developer's rc5b playtest, 11-19: "P2s controller isnt vibrating during fishing"): a player's own step that runs
// retail's code on that player's own pad swapped into GamePad (her own fishing, coop_eventslots' FishStep; her parked event,
// her actor's step) has retail's SetVibration (ebattle's EdFishingStepIn: the nibble 1/80/2, the hooked tug 1/100/10, the
// reel 1/80-119/10) write that swapped-in pad's motors, which nobody steps or sends: swapped back out, they were lost, and
// only the game pad's own (the walker's) ever reached a controller. Now SetVibration on GamePad while a player's own step
// runs (a SoundOwner names them: CoopRumbleNote) is that player's too, here, and goes to their own controller alone.

// Player's (1 up) motor (0 small, on or off; 1 big, its strength) for duration ticks, as CGamePad::SetVibration.
void CoopRumbleFor(int player, int motor, int intensity, int duration);

// CGamePad::SetVibration's call on the game pad (game: this == &GamePad), before its own checks: inside a player's own step
// (coop::SoundOwnerPlayer) the motor is that player's own too. The pad's own fields are written as before (game state
// unchanged). DC_COOP_MUTATE=rumble_owner_off (the twin): nothing here, as before (the swapped-in pad's motors lost).
void CoopRumbleNote(bool game, int motor, int intensity, int duration);

// CGamePad::StopVibration on the game pad (a load's start): every player's motors stop too.
void CoopRumbleStopAll();

// CGamePad::Step's send, after its timers: game is the game pad's port 0 motors (its walker's), enabled this copy's
// vibration option (its own config word, kept per copy through every state take). Sends this copy's player's motors (the
// game's when it plays the walker, and its own player's from here) to this copy's controller, and nothing to any other.
// Logged when what is sent changes, with or without a controller:
//   coop-rumble: copy of pN: player P's motors (S B) to controller C at link tick T      (C 0: none here)
// DC_COOP_MUTATE=rumble_retail (the twin): the game's motors to the first controller on every copy, the others' none,
// as before coop-rumble.
void CoopRumbleSend(const unsigned char *game, int enabled);
