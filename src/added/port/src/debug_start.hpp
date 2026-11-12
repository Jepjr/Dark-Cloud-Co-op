#pragma once

#include "coop/game_ns.hpp"

// Debug start: a start in any town or on any floor of any dungeon, for testing. The picker opens at any time
// with pad 1's L3+R3 (gameloop.cpp; the lockstep's pad 1 when linked, so both co-op copies open it on the same
// tick and its choice, made with pad 1, takes both there), or at boot with --jump pick (or DC_JUMP); --jump
// also takes "town:<0-4>" and "dungeon:<0-6>:<floor>" (floor 1 up). A debug start turns DebugMode on.

// The picker's tick, while the game mode is the menu: draws the list and, once a choice is made, applies
// it (GameApplyDebugStart) and returns 1; -1 for O (back where it was opened); 0 while picking.
int DebugStartModeLoop();

// Floors in dungeon 0-6 (retail's maxFloorTbl), 0 for any other number.
int DebugStartFloors(int dungeon);

// The tests' mutation twins (DC_DEBUG_START_MUTATE, comma-separated): "local" reads the L3+R3 chord from the
// copy's own pad 1, not the lockstep's; "floor" enters the floor next to the
// one asked for, "town" starts the town next to the one picked, "scene" leaves the story scenes unseen,
// "menu" leaves a join's resync at the floor menu, "save" lets saves through after a debug start.
bool DebugStartMutated(const char *what);
// pt1111c: DC_DEBUG_START_SEEN's game flags, set by a test start (GameTakeJumpFloor) beside its scenes; and the floor (0 up)
// that start goes to, kept for a join's resync that comes before the floor is entered (DebugStartOnResync's read).
void DebugStartSeenFlags(int floor);

// Online: a join's resync sends both copies back to the dungeon's entrance, whose floor menu would follow. In
// debug mode (a debug start, which the joiner takes from the host's state) both instead enter the floor the
// host was on directly: DebugStartOnResync marks the resync, DebugStartBeforeModeInit reads the host's floor
// from the state both now hold, and the entrance window takes it (DebugStartTakeResyncFloor, once).
void DebugStartOnResync();
void DebugStartBeforeModeInit();
int  DebugStartTakeResyncFloor();

// Saving is off once a debug start has been used, until the game restarts, so a test run never reaches a real
// save: DebugStartMark (every debug start) sets a game flag in the save data as well, which a joiner takes with
// the host's state, so both copies refuse alike; DebugStartSavesOff(what) is true (and logs) for a save then;
// DebugStartAfterLoad marks a save just loaded again. DC_DEBUG_START_MUTATE=save lets the saves through.
void DebugStartMark();
bool DebugStartSavesOff(const char *what);
void DebugStartAfterLoad();

// app/gameloop.cpp: once, the floor a dungeon:<n>:<floor> jump enters (0 up), marked reached; -1 otherwise.
namespace dc::app {
int GameTakeJumpFloor();
// gameloop.cpp: a choice applied as the same --jump would have applied it at boot (floor 0: the floor select).
void GameApplyDebugStart(int mode, int map, int floor);
} // namespace dc::app
