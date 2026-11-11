#pragma once

#include "coop/game_ns.hpp"

#include <cstdint>

// coop-snapshot: a host's map state as a memory image.
//
// Stage 0, the layout: built with DC_FIXED_LAYOUT (cmake -DDC_FIXED_LAYOUT=ON) the executable keeps its preferred
// address and every arena is mapped at an address fixed by its owner (ArenaPinLayout), so two copies of one build hold
// the game's memory at the same addresses. DC_SNAPSHOT_MUTATE=unpinned (tests: the twin) maps the arenas anywhere.
//
// DC_SNAPSHOT_DUMP=<prefix> (tests): on the catch-up's ticks in DC_SNAPSHOT_DUMP_TICKS (default "0,600"), recording or
// replaying, the writable image sections and every arena block go to <prefix>-t<tick>.bin, for
// tools\coop-qa\netplay\snapdiff.py.

// The catch-up's tick (SoloTick), before the tick's inputs: dumps it when asked.
void SnapshotDumpTick(std::int64_t tick, int at_mode, int map_no, bool replay);

// The same dump to path (coop-houseexit's DC_NETPLAY_TEST_TICK_DUMP).
#include <string>
void SnapshotDumpTo(const std::string &path, std::int64_t tick, int at_mode, int map_no, bool replay);

// Whether this copy's layout is pinned (a DC_FIXED_LAYOUT build with every arena block at its slot): only then can a
// snapshot taken on one copy be put back on another. A build without it joins as before.
bool SnapshotLayoutPinned();

// coop-localfix2 (Jep, 2026-10-26: the arriving copy's catch-up hidden in its fade, "shouldn't that speed up"): a state set
// packed for the link and back (an LZ coding of runs and repeats; no library): about a third of its 16.5 MB went 1.6 s on a
// local link alone. Packed bytes start "LZC1" and the size unpacked; Unpack takes unpacked bytes as they are.
#include <vector>
void SnapshotPack(const std::vector<unsigned char> &in, std::vector<unsigned char> &out);
bool SnapshotIsPacked(const std::vector<unsigned char> &in);
bool SnapshotUnpack(const std::vector<unsigned char> &in, std::vector<unsigned char> &out);
