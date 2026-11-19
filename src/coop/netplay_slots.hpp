#pragma once

#include "coop/game_ns.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// The save slots of a linked session (port/src/coop/netplay.cpp). Both copies must list and load the
// same saves, so the host's slots travel with its state, and the joiner keeps them in a folder of its
// own (netplay-host-saves in its save folder) that its save and load menus then use for the rest of
// the run: it never lists, loads or writes its own slots while it plays the host's game. What the game
// saves while linked lands alike in the host's slots and in the joiner's copy of them.

// The host's slots: every file under the slots folder, with its path. coop-joinstuck: for_state, packed (SnapshotPack) for a
// state the link carries (TakeState): a slot is 80-95 KB raw and 1.5-4.4 KB packed, and Jep's 24 slots raw (2,110,398 bytes,
// 32,826 packed) made his checkpoint 2.19 MB, past one blob's 2 MB (the join stuck at "Joining player 1..."). The joiner reads
// them only for the save and load screens' list (each save.dat for its 56-byte SAVEDATA_INFO) and in player 1's load stepped on
// its copy (which the load's resync then replaces). NetplaySlotsPut takes either. DC_NETPLAY_MUTATE=slots_raw (twin): raw.
std::vector<unsigned char> NetplaySlotsTake(bool for_state = false);

// The joiner: replaces netplay-host-saves with these slots and uses it from now on. False when the
// bytes are not slots.
bool NetplaySlotsPut(const unsigned char *data, std::size_t size);

// The folder the save menus use instead of the copy's own (save_slots.cpp), or empty for its own.
std::filesystem::path NetplaySlotsRoot();

// A hash of the slots, for the handshake's digest: copies whose saves differ start from the host's.
std::uint64_t NetplaySlotsHash();

// The slots' numbers as text ("1 3"), for the log.
std::string NetplaySlotsList();
