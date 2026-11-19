#pragma once

#include "coop/game_ns.hpp"

// coop-padlight (Jep, 2026-11-13: the DualSense light colour matches the players' assigned colours in game): every
// DualSense pad's light bar shows the colour of the player it plays, the player's colour of the map arrows (WhereColour:
// P1 pink, P2 teal, then yellow, green, orange, violet), on every copy and in the lobby once a seat is taken (idle blue
// before). Presentation only: nothing here is in the checksum or the state set, and every copy sets its own pads alone.
//
// Low health: retail's dungeon HUD turns the player's life gauge at 30% of its maximum (DngComStatus: hp < 0.3 * max_hp,
// then again below 15%); the light bar pulses the same colour (its brightness, 25% to 100% over 90 ticks) while the player
// it shows is below the first threshold, and is steady once healed. Dungeon floors only, where the gauge is.
//
// Test hooks (DC_* only): DC_COOP_TEST_LIGHTBAR=1 logs a "coop-lightbar:" line whenever a pad's player, colour or pulse
// changes (=ticks: and each pulsing tick's colour sent), and the table of every player's colour and pulse once;
// DC_COOP_MUTATE=lightbar_retail (the twin): retail's own (pad 0 by the active party member's life), as before.

namespace coop {

struct LightbarRgb {
    unsigned char r = 0, g = 0, b = 0;
};

// Whether a player's life is below retail's gauge turn (hp < 0.3 * max_hp, as DngComStatus compares it).
bool LightbarLow(int hp, int max_hp);

// The colour the pad of player (1 up) shows at tick: the player's colour, scaled by the pulse's brightness when pulse.
LightbarRgb LightbarColour(int player, bool pulse, long tick);

// DualSenseUpdate's hook, once a tick: sets this copy's pads. False when co-op is off or the twin asks for retail's: the
// caller then runs retail's own.
bool LightbarUpdate();

} // namespace coop
