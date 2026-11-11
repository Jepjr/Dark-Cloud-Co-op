#pragma once

#include "coop/game_ns.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// coop-snapshot stage 1: the game's state as a memory image, taken and put back in one
// copy. The state set is every arena block, the decomp's globals (dc_ps2) and an allow list of port globals, read from
// <exe>.snapregions (tools\coop-qa\netplay\snapregions.py writes it from the link map; DC_SNAPSHOT_REGIONS=<file>
// names another). Nothing else is taken: netplay, sockets, audio, the renderer and the co-op modules stay as they are.
//
// DC_SNAPSHOT_ROUNDTRIP=T,K (tests, T >= 1): recording (the catch-up's SoloTick), the state set is taken at the start
// of the frame that plays tick T; ticks T to T + K are played as usual while each region's hash and the lockstep
// Checksum() are kept; at the next frame's start the state taken is put back and ticks T to T + K are played again with
// the pads recorded the first time, each tick's hashes compared with the first time's. The take and the restore wait
// for a frame's start because the tick itself runs inside the mode's frame (the pad read), whose locals would outlive
// a restore. Logs "snapshot: round trip ..." lines for tools\coop-qa\netplay\snaptrip.ps1.
// DC_SNAPSHOT_MUTATE (tests, the twins): skip_port puts back no port global, skip_arenas no arena, poke changes one byte
// of an arena after putting it back.
// DC_SNAPSHOT_TRIP_DUMP=<prefix> (tests): the state set at T + K both times (<prefix>-first.bin, -again.bin) and the
// whole writable image at the take and right after the restore (-take.bin, -restore.bin), in snapshot.cpp's dump
// format for snapdiff.py copies.

enum class SnapshotStep {
    None,
    Taken,    // the state was taken (frame start): the caller keeps its pad routing too
    Restored, // the state taken is back (frame start): the caller puts its pad routing back and plays from T again
    Again,    // a tick of T to T + K played again: the caller plays the pads it recorded and records nothing
};

// Recording, the catch-up's tick before its inputs: hashes and compares. place: "town", "dungeon" or "other" (logged,
// for the test's mode check); checksum: the lockstep's Checksum().
SnapshotStep SnapshotTripTick(std::int64_t tick, const char *place, int map_no, std::uint32_t (*checksum)());

// The main loop's frame start: takes or puts back the state when a tick asked for it.
SnapshotStep SnapshotTripFrame();

// The tick the state was taken for (T).
std::int64_t SnapshotTripFrom();

// Stage 4, two copies: the state set as bytes, taken on the host at a frame start and put
// back on the joiner at one, both copies of one DC_FIXED_LAYOUT build at the same addresses. No heap blocks: the host takes
// it on a quiet tick (no script under way), so none are needed. Regions go by kind and id (a global's image offset, an
// arena owner's); the apply refuses (false, why) when a region is missing on this copy or sized otherwise, and then changes
// nothing. DC_SNAPSHOT_MUTATE=skip_arenas (the join test's twin) puts no arena back.
// An extra region: a heap object a co-op module owns on each copy (its own on each), carried by id; the apply writes the
// host's bytes into this copy's object of the same id and size.
struct SnapshotExtra {
    std::uint64_t  id;
    unsigned char *start;
    std::size_t    size;
};

bool SnapshotTakeBytes(std::vector<unsigned char> &out, std::string &why, const std::vector<SnapshotExtra> &extras);
// coop-offthread: what the last SnapshotTakeBytes cost on its thread: the regions and their bytes scanned, the bytes kept, the
// time to list the regions and to copy them.
struct SnapshotTakeStats {
    std::size_t regions = 0, scanned = 0, kept = 0;
    double      list_ms = 0.0, copy_ms = 0.0;
};
SnapshotTakeStats SnapshotLastTake();
bool SnapshotApplyBytes(const unsigned char *data, std::size_t size, std::string &why, std::size_t *regions_put, std::size_t *bytes_put,
                        const std::vector<SnapshotExtra> &extras);

// coop-pagediff: the state set sent as the pages the taking copy lacks (snapshot_state.cpp says how). The sender: its page
// list (pages: how many it lists), and later the bytes of the pages the taker said it needs (need: a bit a listed page,
// set = needed). The taker: which pages of the list it needs against a state set of its own (own; empty: all), and the
// sender's state set rebuilt from the list, its own and the pages sent, refused (false, why) unless the whole hash matches.
bool SnapshotPageList(const std::vector<unsigned char> &raw, std::vector<unsigned char> &list, std::size_t *pages);
bool SnapshotPageNeed(const std::vector<unsigned char> &list, const std::vector<unsigned char> &own, std::vector<std::uint32_t> &need,
                      std::size_t *pages, std::size_t *needed);
bool SnapshotPageDiff(const std::vector<unsigned char> &raw, const std::vector<std::uint32_t> &need, std::vector<unsigned char> &diff, std::size_t *pages);
bool SnapshotPageRebuild(const std::vector<unsigned char> &list, const std::vector<unsigned char> &own, const std::vector<std::uint32_t> &need,
                         const std::vector<unsigned char> &diff, std::vector<unsigned char> &raw, std::string &why);

// coop-joinfix (LAN playtest 2026-10-30, item 37b): whether the state set carries the byte at pointer: an arena block, or a
// global of <exe>.snapregions (ps2 or port). The texture rebuild after an apply decodes only such pixels.
bool SnapshotStateHolds(const void *pointer);

// coop-joinfix (tests, DC_NETPLAY_TEST_FOREIGN_CLUT): a fresh buffer of size bytes within 1 GB of close_to and outside the state
// set, as the host's heap buffer of item 37b was; nullptr when none could be had (not Windows, or no free range).
void *SnapshotTestForeignBuffer(const void *close_to, std::size_t size);
