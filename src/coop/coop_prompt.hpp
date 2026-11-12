#pragma once

#include "coop/game_ns.hpp"

// The same-character rule: the player and the partner are never the same member. When one asks for
// the member the other plays (the player from the party menus, the partner from its wheel), nobody is
// bumped; the other is asked instead, in the game's Yes/No popup (coop-townwheel; the asker waits in a
// box and may withdraw it). Yes trades the two members (both models reload), no or five seconds of
// silence leaves both as they were. The
// question is simulation state, answered from the shared pads, so linked copies agree.

namespace coop {

// Whether a question is waiting for player 2's answer (pad 2's Cross and Circle are then its).
bool PromptAsksPartner();
// coop-pt1111k: whether a swap question (not the revive one) is open now.
bool PromptSwapOpen();

// Asks the other player for the member: asker 0 the player wants the partner's, 1 the partner wants
// the player's. A question already waiting is left as it is.
// spot: the member whose ally spot the partner found (it asks the player to become that member).
void AskSwap(int asker, int wanted, int spot = -1);

// Each tick of play (CoopStep): the player's member as on the floor, the item sheet's reload after a scene.
void StepPrompt();

// coop-townwheel: steps the swap question each tick on a floor (MenuTick, from the agreed pads, so it is answered while the
// world holds for it): the answer, the asker's cancel, the trade, the timeout.
void StepSwapQuestion();

// The party menus' change of the player's member, as BtMenuLoadChara is about to load it: the member
// the player gets (wanted), or -1 to keep the one it had on the floor (put back in cur_chara, which the
// menus set already). The partner's member is asked for instead of taken, and a change made from the
// menus player 2 holds is the partner's, not the player's.
int MenuCharaChange(int wanted);

// coop-worlds: the player becomes chara at once, as the party menus' change does it.
bool PromptChangePlayer(int chara);

// coop-atlaswap (tests, DC_COOP_DUNTEST_SWAP): the swap a yes to her Characters pick makes (Trade): the player takes the
// partner's member and the partner the player's, which loads on the next tick. False when the player could not change.
bool PromptTradeForTest();

// coop-revive: the revive rule with a Revive Powder (DC_COOP_REVIVE_POWDER=1, off by default; the host's setting goes
// to the joiner in the hello). While it is on, a player standing still beside the other lying down, with life and a
// Revive Powder in its bag, is asked one question, "Use Revive Powder?  Cross yes / Circle no" (no item menu; five
// seconds to answer, the player's pad held for it as the swap question holds it). Yes spends one powder and the one
// lying down gets up with half its life, as the game's own powder gives; no or silence leaves it down, and it is not
// asked again until the reviver steps away. Off: the free stand-still revive (coop_party.cpp StepDown) as before; the
// 10 s get-up holds either way.
bool RevivePowderOn();
void RevivePowderAdopt(bool on);

// Steps the question for reviver (0 the player, 1 the partner) each tick it lies down, beside: whether the reviver
// stands where it may be asked. True on the tick it says yes and its powder is spent.
bool ReviveAsk(int reviver, bool beside);

// Session reset (CoopResetForSession): no question waiting, the player's pad unlocked.
void PromptResetForSession();

// coop-townwheel (Jep, 2026-11-02: the Triangle menu's Allies pick asks as the Select ring's does): retail's Cross on the
// Allies page writes the party's member before the change loads, so the partner's pick saw the player take her member and
// moved her to his, and the load then found nothing to ask. True when chara is the partner's member: the Cross leaves the
// party's member alone (the question is asked at the load, MenuCharaChange). DC_COOP_MUTATE=menu_takes_partner: as before.
bool MenuCharaAsks(int chara);

// coop-townwheel: both players stand while the swap question is up (Jep, 2026-11-02: the asker counts as paused while its
// waiting box is up, the one asked answers in the game's box), as a gate's question holds them (CoopWorldsQuestionHolds).
// port 0 player 1, 1 player 2. DC_COOP_MUTATE=swap_no_hold (twin): neither held, as before.
bool PromptHolds(int port);

// The question up, for its draw: players numbered 1 and 2.
struct PromptView {
    int  asked = 0;
    int  asker = 0;
    bool revive = false;
    int  wanted = -1;
    int  own = -1; // the asker's member, which the one asked gets in the trade
    int  spot = -1;
    int  cursor = 0; // the hand: 0 yes, 1 no
    int  ticks = 0;
    int  carried = 0; // revive: the powders the one asked carries
};
bool PromptQuestion(PromptView *view);

// coop_useprompt.cpp: the question in the game's Yes/No popup on the asked player's copy, the asker's waiting box on theirs
// (with its Circle to withdraw it). Draw only.
void DrawSwapQuestion();

} // namespace coop

// Draws the question for the player it asks. CoopDrawWheel calls it.
void CoopDrawPrompt();
