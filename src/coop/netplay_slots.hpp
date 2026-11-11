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

// The host's slots: every file under the slots folder, with its path.
std::vector<unsigned char> NetplaySlotsTake();

// The joiner: replaces netplay-host-saves with these slots and uses it from now on. False when the
// bytes are not slots.
bool NetplaySlotsPut(const unsigned char *data, std::size_t size);

// The folder the save menus use instead of the copy's own (save_slots.cpp), or empty for its own.
std::filesystem::path NetplaySlotsRoot();

// A hash of the slots, for the handshake's digest: copies whose saves differ start from the host's.
std::uint64_t NetplaySlotsHash();

// The slots' numbers as text ("1 3"), for the log.
std::string NetplaySlotsList();
