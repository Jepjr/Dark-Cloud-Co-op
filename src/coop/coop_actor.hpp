#pragma once

#include "coop/game_ns.hpp"

// coop-actorswap (root cause 1, root-cause-analysis-20261030T061957-8e89.md and root-cause-review-2026-10-30.md): retail's
// actions read "the player" from the main character's globals (CharaMain, UserStatus->cur_chara, the pack, the purse, the
// game pad's first port). An actor swap puts another player's actor in those globals for one retail action and takes it
// back after, inside the step on every copy alike, so that player runs retail's own code for it (the Georama editor's
// SwapIn/SwapOut, coop_georama_own.cpp, generalised from one subsystem to the player).
//
// Built on a player table, never on "him and her": players[0..kMaxPlayers-1], entry k is player number k + 1. Entry 0's
// actor lives in the globals themselves (the resident); a swap exchanges the resident's values with entry k's, so a second
// exchange puts everything back byte for byte (the swap-roundtrip self-test, TESTING-STANDARD.md "Root fixes"). The copy's
// own entry (ActorLocalIndex, what it presents) is kept apart from the player numbers the step runs.
//
// What one swap exchanges (each read from code, 2026-10-30):
//   - the body: CharaMain's bytes with the entry's CCharacter (player 2: g_partner.chara), its cloth pointer rebased to its
//     own cloth_buf, CharaFrame (= CharaMain.frame) and nowUnitNow following;
//   - the member: UserStatus->cur_chara with the entry's character (player 2: g_partner.loaded_chara); stats per member
//     (hp, weapons, equipped slot, water) are then read for the swapped-in member, as the design wants them shared;
//   - the pack, its quick-use slots' uses and the purse, while the players have a bag each (OwnBags);
//   - the game pad (GamePad) with the entry's own CGamePad, fed from its agreed pad (ActorFeedPad);
//   - the other one: while k is swapped in, the partner slot (g_partner: pos, heading, its body and member) holds the
//     resident, so retail and co-op code that reads "the partner" sees player 1 (with more than two players: the resident,
//     until a rule for many is needed);
//   - the presenting player: a SoundOwner and a MessageBy for k's number live as long as the swap (cause 2's owners).
// Story-flag writes stay with the session's one owner whoever is swapped in (review risk 6): nothing here changes that.
//
// A retail sequence that runs over many ticks with one set of statics (a chest opening, a talk, a door script) either has
// its state in the context or serializes: ActorSequenceTake refuses a second player while one holds it and logs the wait
// ("actor: <sequence> serializes: player N waits for player M"), never silently.
//
// Test hooks: DC_COOP_TEST_SWAP_ROUNDTRIP=<numbers> (e.g. "1,2,6"): on a floor, once the partner is up, for each player
// number the state set is taken, swapped in and out with nothing run between, taken again and page-diffed ("actor: swap
// roundtrip player N ..."), then "actor: roundtrip done". DC_COOP_MUTATE=actorswap_leak: the swap out leaves the member
// unswapped (the self-test's twin).

#include <cstdint>

#include "platform/input.hpp"

namespace coop {

constexpr int kMaxPlayers = 6;

// The player table.
int  ActorNumber(int index);    // index + 1
bool ActorPresent(int index);   // a player plays entry index this session (0 always)
int  ActorChara(int index);     // the party member entry index plays, -1 none
int  ActorLocalIndex();         // this copy's own entry (what it presents): 0 unlinked (the one copy presents all)

// coop-players: the table as this copy sees it, once (DC_COOP_TEST_PLAYERS=1: on a floor, once the partner is up, "players:
// ..."); coop_core's step calls it.
void ActorTableTrace();

// coop-localmenu stage 2: a player's menus as simulation state. Each copy runs its own
// player's menus; what the step reads is this bit per player and menu kind, set and cleared by an open or close command
// (kCmdMenu) on its link tick on every copy, so the copies agree whichever of them runs the menu. Player is 1 up.
// DC_COOP_MUTATE=menubit_here (twin): the bit set at once on the menu's own copy, never sent.
enum ActorMenuKind : std::uint8_t {
    kActorMenuFull = 1, // the game menu (Triangle), the first or the second
    kActorMenuTown = 2, // a town's own menu
    kActorMenuLocal = 4, // stage 3a: the menu runs on its player's copy alone; its close runs retail's close task everywhere
};
bool          PlayerInMenu(int player, std::uint8_t kinds = kActorMenuFull | kActorMenuTown);
std::uint32_t ActorMenuBits(); // every player's kinds, 4 bits each (the checksum's)
// Once a tick, before the copy's input frame (NetplayTick): queues an open or close for each player this copy runs (its own
// linked, every present one alone) whose menus changed since the last it sent.
void ActorMenuSync();
// A menu opening every copy sees on the same tick (the lockstep's pad): the bits set at once on each, no command.
void ActorMenuOpenHere(int player, std::uint8_t kinds);
// coop-soakfix5: a resync's load (every copy, after the session reset has closed the menu engines): no player's bits and nothing
// sent, so an engine still running says so again by its command.
void ActorMenusResetForResync();

// The swap. Nested swaps are refused (false); a swap of the resident (0) changes nothing and returns true.
bool ActorSwapIn(int index, const char *why);
void ActorSwapOut();
int  ActorSwapped();            // the entry swapped in, -1 for none
int  ActorPresenting();         // the player number retail's presentation is for now: the swapped one, else 0 (everyone's)
int  ActorOther(int index);     // whose actor the partner slot holds while index is swapped in

// Feeds entry index's own game pad from the pad the copies agreed on this tick (now) and the tick before.
void ActorFeedPad(int index, const InputPadState &now, const InputPadState &before);

// RAII: in for a scope, out after.
class ActorSwap {
  public:
    ActorSwap(int index, const char *why) : in_(ActorSwapIn(index, why) && index != 0) {}
    ~ActorSwap() {
        if (in_) {
            ActorSwapOut();
        }
    }
    ActorSwap(const ActorSwap &) = delete;
    ActorSwap &operator=(const ActorSwap &) = delete;
    bool in() const { return in_; }

  private:
    bool in_;
};

// Whether an action ported onto the swap takes retail's path (true) or the mod's own one it replaces. Ported one at a time,
// each behind its switch: DC_COOP_ACTORSWAP=off or DC_COOP_MUTATE=actorswap_off turns every one off (the class check's fix-off), DC_COOP_MUTATE=
// actorswap_<action>_off that one (its twin). Ported: "items" (player 2's food, water and cures: checkItemUsed,
// usedActiveItem), "throw" (her throwables, gems included).
bool ActorPorted(const char *action);

// The "items" action: player index uses a food, water or cure item by retail's own rule and use (checkItemUsed, then
// usedActiveItem) with index swapped in: its member's life, water and ailments change, its sounds play on its screen. The
// rule reads a quick-use slot, so the item sits in the first one for the check and the slot is put back. Spending the item
// is the caller's (her quick-use slot first, or a menu's own). 1 used, 0 refused (the refuse sound played), -1 no swap.
// Shaped for the local menus' named "item use" command (coop-localmenu), which calls it
// with the command's player index on every copy.
// coop-playfix: `quick`, the field's Square, refuses quietly (retail's Square says nothing when it has nothing to use).
int ActorUseItem(int index, int item, bool quick = false);

// The "throw" action: player index throws an item (a bomb, stone, fruit, gem or holy water: retail's quick-use throwables)
// as retail's throw does it (BattleActionThlow's release): retail's thrown item model from the swapped-in body's "item" hand,
// lobbed to the point to, or straight at it when locked; retail's item model pool flies and lands it with retail's effect
// for that item (gems included). The item's model is shared from the pool or loaded once for the entry. No hand model:
// retail draws hand models on CharaMain's hand alone. Spending is the caller's. 1 thrown, 0 refused, -1 no swap.
int ActorThrowItem(int index, int item, const float *to, bool locked);

// coop-feathersync: the items the game's quick-use slots run (activeItem.item, what the field's Square reads) taken from the
// game's pack on every copy alike, each world step before the player moves (coop_actor.cpp).
void ActorActiveItemsAgree();

// A single-instance retail sequence: true when entry index holds it (now or already); false (logged) while another holds it.
bool ActorSequenceTake(const char *sequence, int index);
void ActorSequenceRelease(const char *sequence, int index);
void ActorSequencesReset();

// The step's hook (CoopStep, once the partner has stepped): the self-test.
void ActorTestTick();

} // namespace coop
