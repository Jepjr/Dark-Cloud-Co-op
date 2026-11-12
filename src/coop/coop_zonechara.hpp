#pragma once

#include "coop/game_ns.hpp"

namespace dc::game {
struct RS_STACKDATA;
struct PAD_STATUS;
} // namespace dc::game

// coop-pt1111k (Jep's 2026-11-11 playtest: a character-limited floor forces player 1 into its member even when another player
// already plays it). Retail: on each such floor the floor's event script (dun/script/d0N/event.stb) calls its helper F(z),
// whose _SET_MAIN_CHR2(z) runs selectChrUnit(z) on the player, and then _SET_LIMMIT_ZONE(z) fences the floor to member z
// (res_limit_zone_current 0 to 5; 15 floors, DBC B11 to Demon Shaft B23). The script runs in the lockstep on every copy and
// names only UserStatus->cur_chara, player 1's member, so co-op switched him whoever played z, and the partner's pick
// (PickPartner never gives player 2 player 1's member) moved her off z.
//
// The rule (Jep 11-11), on every copy alike from the tick's state, for any number of players:
//   - the floor's start: z is met when any player on the floor plays it; only when none does is player 1 switched (retail);
//   - a member pick (the menu's Characters page, the radial, pad 2's L3) is checked when it is applied, never when it was
//     offered: a pick that leaves nobody on the floor playing z is refused (the refuse sound and the zone's notice on the
//     picker's screen alone; nothing changes). A trade keeps the count of z's players, so it always goes;
//   - nobody on the floor plays z any more (its player left the floor or dropped): the lowest-numbered player still on it is
//     switched to z, as the floor's start does (Jep's default A, 11-11).

namespace coop {

// The member the floor is fenced to (res_limit_zone_current 0 to 5), else -1; -1 unlinked/solo-less (co-op off).
int ZoneForcedMember();
// Whether player index (0 up) is on this copy's floor now (index 0 always; another when present, not apart, not away).
bool ZonePlayerOnFloor(int index);
// The member player index plays (player 2 before her body loads: the one she loads).
int ZoneMemberOf(int index);
// How many players on the floor play member chara, player except left out (-1: none left out).
int ZoneHolders(int chara, int except = -1);
// Whether player index may take member chara as a pick of their own (not a trade): false when it would leave z unplayed.
// apply: the check at the pick's apply (DC_COOP_MUTATE=zonepick_open turns that one off: the offer's check alone, the twin).
bool ZonePickLegal(int index, int chara, bool apply = true);
// A refused pick: the refuse sound and the zone's notice ("only <z>"), on that player's screen alone; logged.
void ZonePickRefused(int index, int chara, const char *how);

// The floor script's _SET_MAIN_CHR2(chara) (btsysscript recipe): true when this is the floor start's zone force (the script's
// next statement is _SET_LIMMIT_ZONE(chara)) and another player on the floor already plays chara: player 1 is not switched.
// DC_COOP_MUTATE=zoneforce_p1 (twin): always switched, as before.
bool ZoneForceMetByOther(dc::game::RS_STACKDATA *stack, int chara);

// Each tick in the step, on every copy (coop_core, before PickPartner): the leave-floor switch and the test hooks.
void ZoneStep();

// The menu's Characters page on a character-limited floor (main_ring recipe): open with another player on the floor (each
// pick checked as it is taken); alone, retail's refusal.
bool ZoneMenuPicksOpen();

// The sit-out of a player not on z, one switch, OFF by Jep's 11-11 decision (everyone fights); with it on, ZoneSitsOut holds for every non-holder alike.
bool ZoneNonHoldersSitOut();

// Whether player index (0 up) sits out the floor's fights: the floor is character-limited to z, the switch is on and they play
// another member (player 1 as much as player 2: pt1111n; the member player 2 loads is her loaded_chara, as SitsOutFightZone had it).
bool ZoneSitsOut(int index);
// DunMoveChara's attack start (recipe 0031): the game's walker is a non-holder, so its swing is refused (refuse sound on its
// screen, a log line); false when it may swing. DC_COOP_MUTATE=fightzone_p1 (twin): never refused.
bool ZoneRefusesPortSwing(const char *what);
// A swing taken by player index (0 up) on a floor it is not on z for, the sit-out off: a log line (the rows' evidence).
void ZoneNoteSwing(int index);
// MenuFilterPort(0): the same walker's Square (item, boost) and R2 (aim) held off the game's pad.
void ZoneFilterPort(PAD_STATUS *status);
// The Characters page (battlemenu recipe 0020): whether a pick of member chara by this page's owner would be refused now (it
// would leave z unplayed), so the page greys that entry as it does a downed one, from the state of every draw, live.
// pick: the page's Cross on it: refused as a pick of the picker's own (the refuse sound, the zone notice, a log line).
// DC_COOP_MUTATE=zonemenu_nogrey (twin): never greyed.
bool ZoneMenuGrey(int chara, bool pick = false);

// DC_COOP_TEST_ZONE_ANSWER=yes: the one asked answers a swap question yes (coop_prompt's StepSwapQuestion).
bool ZoneTestAnswersYes();

} // namespace coop
