#pragma once

#include "coop/game_ns.hpp"

#include <cstdint>

#include "platform/input.hpp"

// The co-op link's test hooks (port/src/coop/netplay.cpp), each off unless its test variable is set:
//
//   DC_COOP_FAKE_MOUSE       ticks 700 to 1500 push real mouse and keyboard events, as a hand would
//   DC_NETPLAY_PERTURB=N     at tick N this copy alone draws a random number and loses a point of life
//   DC_NETPLAY_NEXT_FLOOR=N,...  from each tick, once on the floor under control, the party takes the
//                            floor's key and exit gate as the player's Cross at them does
//   DC_NETPLAY_LEAVE=N,...   from each tick, once on the floor under control, the party leaves for town
//                            (these two: an entry "loaded" waits for a state file's load instead, and a last entry
//                            "@REGEX >> ..." for this copy's own log lines, apart only; coop-gatekey52)
//   DC_NETPLAY_LIST_EVENTS=N,... logs the floor's events
//   DC_NETPLAY_RUN_EVENTS=N,...  from each tick, once on the floor under control, runs the floor's next
//                            event (not the exit gate or its key) as the player's Cross at it does
//
//
// Mutation twins (DC_NETPLAY_MUTATE=name, or several names separated by commas; on one copy only unless a test says), each breaking one feature so its test
// must fail: menu_claim (player 2's Start never takes the menu), floor_tick (the floor change starts a
// tick late), event_slot (the events hook runs a different event), no_resync (the joiner does not take
// the host's state), lang_ignored (the handshake does not compare the language), no_adopt (the joiner
// does not take the host's settings), no_resend (from tick 600 inputs go out once, never again), build_ignored (the
// handshake does not compare the builds), fixed_delay (the input delay never adapts),
// no_session_reset (a resync leaves the co-op modules' own state as it was), no_bag (a resync does
// not take the host's copy of pad 2's own bag), own_slots (the joiner keeps its own save slots),
// link_at_title (the link starts at the title, before the game is in a town or a dungeon), no_filter
// (the keyboard and the mouse still reach the game while linked), own_view (the host's resync state
// leaves out its last view, so each copy keeps its own for the first tick's clip tests), own_stops (it
// leaves out the town's stop flags), own_town_draw (the host keeps a town's draw-time state through a
// resync); all three set on the host. no_join (player 2's menu Join is never picked), join_any_pad (a
// copy waiting to Join reads the game's usual pads), no_keepalive (a copy that stops ticking, as in a
// slow load, sends no Busy, so the other drops the link after five seconds), no_shot_sum and no_save_sum
// (the checksum leaves out pad 2's shots, or the SaveData; set on both copies), no_leave (a quitting copy
// does not say so, so the other waits out its timeout), own_load (on the joiner: a slot the host loaded
// (DC_NETPLAY_LOAD_AT) leaves the joiner's SaveData as it was), stale_floor_sum (the floor checksum hashes free
// chest and circle slots too; on both copies), keep_map_guard (a resync leaves the floor builder's guards as they
// were; on both copies), keep_weapon_fx (a resync leaves the weapon element slot rotation as it was), no_floor_dump (a desync writes no
// floor-slot dump), keep_effect_count (a resync leaves the towns' effect clock as it was), protocol_unbumped (its Hello says
// protocol 14, train 50's bump for kocatchup's state set undone).
//
// Set alike on both copies but for DC_NETPLAY_PERTURB and DC_NETPLAY_MUTATE. NetplayTick calls this
// as each tick starts.
void NetplayTestTick(std::int64_t tick);
// coop-fx51b: this copy's catch-up reached its lockstep on that link tick, and a compare of that tick was in sync
// (DC_NETPLAY_PERTURB_FLOORSLOTS=lockstep).
void NetplayTestLockstep(std::int64_t tick);
void NetplayTestInSync(std::int64_t tick);

// coop-worlds: whether DC_NETPLAY_NEXT_FLOOR's hook is driving the party through a floor's key and gate now (the door
// vote leaves a test's gate alone unless the test asks for a vote).
bool NetplayTestDrivingGate();

// coop-worlds: an apart copy's own floor hooks (DC_NETPLAY_NEXT_FLOOR, DC_NETPLAY_LEAVE), its tick counted on from the
// split by its own floor play, since an apart copy has no link tick.
void NetplayTestFloorApart(std::int64_t tick);

// coop-worlds: the copies merged. The floor hooks an apart copy ran alone (only that copy has them) stop driving its
// pad 1 (NetplayTestPad), which would now press Cross on that copy alone: both then step alike from the merge.
void NetplayTestFloorMerged();

// As a resync's state is taken (TakeState), the leftovers DC_NETPLAY_PERTURB_STOPS / _VIEW ask for, which the state must
// then replace.
void NetplayTestResyncLeftovers();

// While a floor change DC_NETPLAY_NEXT_FLOOR started is under way, the game's first pad reads Cross
// tapped with the sticks at rest, so the gate's questions get their first answer (on, not back to
// town) whatever the script input holds. Route calls it on the pad both copies agreed on.
void NetplayTestPad(std::int64_t tick, InputPadState &pad);

// Whether DC_NETPLAY_MUTATE names this mutation.
bool NetplayMutated(const char *name);

// coop-pilot: the unit layer's in-process cases (DC_UNIT=checksum), run once a floor plays; the copy then leaves.
void NetplayUnitTick();

// coop-pilot: a test seam (DC_QA_NO_DRAW=1): the game's ticks are not rendered (GameRenderTick, as a catching-up joiner's),
// the draw code still runs; said once on stderr. Off in play.
bool NetplayTestNoDraw();
bool NetplayTestNoDrawUploads(); // (false under the twin DC_QA_DRAW_MUTATE=skip_nodraw_uploads)