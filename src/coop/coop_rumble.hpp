#pragma once

#include "coop/game_ns.hpp"

// coop-rumble (Jep's pack 8 playtest, 2026-11-03: P1's hits shook P2's controller): rumble is presentation
// (coop_present.hpp), so each copy shakes its own player's controller and only for that player's own hits. The game's
// motors (CGamePad::SetVibration: the player's hits, the fishing line) are the game's walker's, and the partner's hits
// (coop_combat's TakeHits) set motors of their own here, as BtCheckDamageProc sets the player's. Neither is game state:
// both copies step the partner's alike, and only the send reads which player this copy plays.

// The partner's motor (0 small, on or off; 1 big, its strength) for duration ticks, as CGamePad::SetVibration.
void CoopRumblePartner(int motor, int intensity, int duration);

// CGamePad::Step's send, after its timers: game is the game pad's port 0 motors (its walker's), enabled the game's
// vibration option. Sends this copy's player's motors to this copy's controller, and nothing to any other.
// DC_COOP_MUTATE=rumble_retail (the twin): the game's motors to the first controller on every copy, the partner's none,
// as before.
void CoopRumbleSend(const unsigned char *game, int enabled);
