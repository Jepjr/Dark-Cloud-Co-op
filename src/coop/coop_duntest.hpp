#pragma once

#include "coop/game_ns.hpp"

// coop-dungeonsuite: the dungeon interaction suite's driver (qa\tests\x-dungeon-suite.ps1), a test hook only
// (DC_COOP_DUNTEST). Each link tick, set alike on both copies (NetplayTestTick, and NetplayTestFloorApart apart).

#include <cstdint>

namespace coop {

void DunTestTick(std::int64_t tick);
void DunTestAloneFrame(); // coop-soakfix3: DC_COOP_DUNTEST_ALONE (a host alone, before a join)
// coop-fixplace: test hook, right before a floor is built for the first time (the dungeon's floor start): with
// DC_DUNGEON_TEST_SEED=N the random number state is N + the floor, so the floor's seed and layout are the same in every run
// (a state fixture's recipe sets it; the floor is built at --jump before the link, from as many rand() calls as the boot
// made, which varied). Nothing without it.
void DungeonTestSeed();

} // namespace coop
