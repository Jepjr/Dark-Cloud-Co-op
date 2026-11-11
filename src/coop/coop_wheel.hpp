#pragma once

#include "coop/game_ns.hpp"

#include <cstdint>

#include "platform/input.hpp"

// Pad 2's character wheel: R3 opens a ring of the members pad 2 may play (its own at the top), the
// D-pad turns it, Cross takes the one at the top, Circle (or R3 again) closes it. Only on a dungeon
// floor in normal play (Walk calls it), never while the partner lies down; the game and the player go
// on as usual meanwhile. Its state is simulation state, stepped from the shared pad alone, so linked
// copies agree. L3 / Select+L3 still swap without it.

namespace coop {

// While open the wheel owns pad 2's D-pad, Cross and Circle; combat leaves them alone.
bool WheelOpen();

// Steps the wheel from pad 2. Walk calls it each tick of play; true while open.
bool StepWheel(const InputPadState &pad);

// Closes it without a choice.
void CloseWheel(const char *how = "closed"); // how: for its log line (coop-wheelcrash)

// Pad 2's wheel in town: coop_town calls it each tick of walking with the town partner's member.
bool StepTownWheel(const InputPadState &pad, int loaded);
// coop-menucross: her town wheel has her pad (open, or closed on this link tick: its Cross or Circle is the wheel's).
bool TownWheelHoldsPad();

// Session reset (CoopResetForSession): closed, as at the start of a session.
void WheelResetForSession();

// coop-wheelcrash: player 1's own ring. Linked and together on a floor, his Select opens it in place of retail's wheel
// (which stops the game for both, draws on both screens, and beside her menu took its texture block and crashed both
// copies); drawn on his copy alone; the world goes on. Pad 2's Select opens hers (StepWheel), drawn on her copy alone.
// DC_COOP_MUTATE=gamewheel (twin of t-wheel): retail's wheel, as before.
bool OwnWheels();

// Steps his ring from his agreed pad. Walk calls it each tick of play; true while open.
bool StepPlayerWheel(const InputPadState &pad);

// Whether his ring is open, and the buttons of his the game must not see now (the ring's while open, then those still
// held as it closed): the link's routing and the menu's world step take them off his pad.
bool          PlayerWheelOpen();
std::uint16_t PlayerWheelMask();

// coop-pausewheel: a player's own pause closes that player's ring as it starts (coop_pause calls it; the rings' steps do not
// run while the world holds, so they cannot close it themselves).
void CloseWheelForPause(int player);

} // namespace coop

// coop-wheelcrash: for the dungeon's own Select (port/src/dun/movechara.cpp): its wheel stays shut while each player has
// their own (coop::OwnWheels), and shut beside a menu open next to the world (its texture block is the menu's).
bool CoopOwnWheels();
bool CoopGameWheelBlocked();

// Draws the ring around the partner. CoopDrawHud calls it.
void CoopDrawWheel();

// coop-hudicon: draws one member's wheel face, size by size with its top left at x, y (the HUD's own 2D
// coordinates); false when the faces could not be loaded.
bool CoopDrawHudFace(int chara, int x, int y, int size);
