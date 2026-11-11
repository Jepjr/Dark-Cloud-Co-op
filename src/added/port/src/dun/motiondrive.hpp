#pragma once

#include "coop/game_ns.hpp"

// coop-p2absent: motionDrive's regeneration counter (port/src/dun/motiondrive.cpp), a function static on
// the PS2. The co-op link zeroes it on both copies at a resync and holds it in its checksum.
extern int MotionDriveHealCounter;
