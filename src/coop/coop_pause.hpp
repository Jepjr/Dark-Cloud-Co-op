#pragma once

#include "coop/game_ns.hpp"

// Each player's own pause on a dungeon floor (Jep's 2026-10-20 rule): "both pause and the main menu are
// considered a form of pause, and if every player in the zone has a form of pause active, the world pauses.
// if only one has either form of pause, the world keeps moving". And no shelter in it: a player in a pause or
// a menu stands where they are and the world can still hit them ("they become able to 'cheese' fight
// mechanics").
//
// With the menus on and the overlay off (game.coop_menus, not game.coop_menu_panel), on a floor under control:
//   Start toggles that player's pause. Their character stands (their pad reaches nothing), their copy draws
//   the pause over the running world (couch: a line per paused player).
//   The zone is the floor; player 2 counts while her character is on it (coop's partner visible).
//   Every player of the zone in a pause or the game's menu: the world holds (retail pause's holds; with the
//   game's menu open beside it, its world step is left out). Otherwise it runs.
//   A player who goes down leaves the pause; the menu beside the world closes (coop_fullmenu).
// The pauses move on the pads both linked copies agree on, at the tick's pad read, and are in the checksum.

#include <cstdint>

#include "platform/input.hpp"

namespace dc::game {
struct PAD_STATUS;
} // namespace dc::game

namespace coop {

// The pauses are this module's (game.coop_menus on, game.coop_menu_panel off).
bool PauseOn();

// From MenuTick (the agreed pads): Start's toggles, the downed leaving, the zone's hold.
void PauseTick(const InputPadState &one, const InputPadState &two);

// Whether player 1 or 2 is in their pause.
bool PausePlayer(int player);

// Every player of the zone in a pause or the game's menu: the world holds.
bool PauseZoneHeld();

// Port 1 (the game's pad) on a floor: Start is the pause's, never retail's; player 1's pad reaches nothing
// while he is paused and the port is his.
void PauseFilterPort(PAD_STATUS *status);

// Over the HUD on this copy: its player's pause (couch: each paused player's line).
void PauseDraw();

// Around the floor's DrawAtraBoll: while the world holds, the Atlas' bobs (stepped in their draw) are put back after it.
void PauseAtlaDrawBegin();
void PauseAtlaDrawEnd(const float *pos);

// coop-chestworld: from motionDrive, after the task's step: player 1's chest, Atla or gate key presentation leaves the monsters and
// effects running (and drawn) while player 2 is on the floor; retail held them all.
void PresentationWorldRuns();

// coop-atlamenu: whether the world's task is one of player 1's presentations above (his chest, Atla or gate key), which run
// beside play for player 2.
bool PresentationTask();

// For the link's checksum; session reset (both out of their pause, the world's holds let go).
std::uint32_t PauseHash();
void PauseResetForSession();

} // namespace coop
