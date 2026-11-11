#pragma once

#include "coop/game_ns.hpp"
// coop-small2 (Jep 11-09, storage option C): the house's storage, the party's one board, used through the check-in counter by any
// number of players at once, together or apart (coop_storage.cpp). Called from the counter's code (ps2/src/shop.cpp, its recipe).
#include <cstdint>

bool CoopStorageOn(); // the counter's storage goes by C here (linked with local menus, or apart with a peer); else retail's
// A put-down of the held record (an item's {item, uses}; a weapon's WEAPON_HAVE; an attachment's ATTACH_LIST) into page's slot (or, taken
// by then, the page's first free slot): sent at once as one finished move. The caller empties its hand.
void CoopStoragePut(int page, int slot, const void *data, int size);
// A pick-up of page's slot, as the slot holds `expected` on this copy: claimed at the press. purpose 0: into the hand; 1: the take
// into the bag. swap: the held record goes into the slot as the claim lands (the hand keeps it till then).
void CoopStorageClaim(int page, int slot, int expected, int purpose, const void *swap, int swap_size);
void CoopStorageSort(int page); // the page sorted (retail's Seiton), as one move
bool CoopStorageWaiting();      // a claim of this copy's player is out: the counter takes no other storage press till it lands
// The counter's step on the presser's copy: a claim landed for this copy's player (true; its slot's record in data), or refused.
bool CoopStorageGranted(int *page, int *slot, int *purpose, void *data, int size);
bool CoopStorageRefused();
void CoopStorageTick(); // each frame (apart: the asks resent till answered)
void CoopStorageCommandHandlers();
bool CoopStorageReceive(const void *data, int size); // apart: an ask (the host) or an answer (the asker)
void CoopStorageReset();
