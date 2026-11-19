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
// coop-seed324: the rings' stepped state goes in the state set (build/snapshot-allow.txt): the twin (DC_COOP_MUTATE=ringset_own)
// keeps this copy's own across a state set's apply, as before.
void WheelApplyBefore();
void WheelApplyAfter();

// coop-wheelcrash: player 1's own ring. Linked and together on a floor, his Select opens it in place of retail's wheel
// (which stops the game for both, draws on both screens, and beside her menu took its texture block and crashed both
// copies); drawn on his copy alone; the world goes on. Pad 2's Select opens hers (StepWheel), drawn on her copy alone.
// DC_COOP_MUTATE=gamewheel (twin of t-wheel): retail's wheel, as before.
bool OwnWheels();
// coop-p2controls3: each player's element ring (their D-pad Up, the game's element picker as a menu of their own and a pause state of
// theirs), stepped from the menu tick on the agreed pads; whether player's is open.
void StepElementRings(const InputPadState &one, const InputPadState &two);
bool ElementRingOpen(int player);
unsigned char *CoopWheelBank(); // coop-p2controls: retail's quick change bank (quickchr.pac's quickchr.img: faces, element icons) in the ring's buffer
void CoopWheelSheetNote(const char *what); // coop-ringlog: the element sheet was just read, loaded or re-keyed by `what` (the ring's open line names the last)

// Steps his ring from his agreed pad. Walk calls it each tick of play; true while open.
bool StepPlayerWheel(const InputPadState &pad);

// Whether his ring is open, and the buttons of his the game must not see now (the ring's while open, then those still
// held as it closed): the link's routing and the menu's world step take them off his pad.
bool          PlayerWheelOpen();
std::uint16_t PlayerWheelMask();
// coop-georamaring: the buttons a player's ring holds off their own windows (his: PlayerWheelMask; hers: all while one of her rings is
// open, and after it closes what was still down till let go).
std::uint16_t RingHold(int player);

// coop-pausewheel: a player's own pause closes that player's ring as it starts (coop_pause calls it; the rings' steps do not
// run while the world holds, so they cannot close it themselves).
void CloseWheelForPause(int player);

// coop-pt1111k: a ring's Cross on chosen for player index (0 up), as it is taken: a trade asked, or that player's own pick checked
// against a character-limited floor's rule (refused on their screen when it would leave the floor's member unplayed).
void WheelConfirm(int index, int chosen);
// Test hooks (DC_COOP_TEST_ZONE_PICKS): whether the ring offers chara now; open it with the hand on chara (false: not offered).
bool WheelOffersForTest(int index, int chara);
bool WheelOpenForTest(int index, int chara);

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
// coop-areanotice: the same face at alpha (0x80 whole), for a notice that fades.
bool CoopDrawHudFaceFaded(int chara, int x, int y, int size, int alpha);
