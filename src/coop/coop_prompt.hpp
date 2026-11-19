#pragma once

#include "coop/game_ns.hpp"
#include "coop/coop_members.hpp" // coop-memberowner: Member, SetMember

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
// coop-dpadmenus: whether the question open now (the swap question or the revive one, shown or still waiting) names this player as its
// asker or the one asked (players 1 to kMaxPlayers): its answer is theirs, and no ring of theirs opens over it.
bool PromptQuestionOf(int player);
// coop-dpadmenus: the player (0-based) other than `except` who plays member chara in town now (TownAlliesPlays, the Allies page's word),
// -1 none: the town rings offer that member as a trade, as the floor's ring and the Allies page do.
int TownMemberHolder(int chara, int except);
// coop-apartrings: the member player port (0-based) plays in town now (the Allies page's word: TownAlliesPlays).
int TownMemberOf(int port);

// trade3: game.coop_swap_same_zone (the host's, default off): trades are offered only while both players are in the same zone.
bool SwapSameZone();

// trade3 (Jep's rc3: apart, his Select opened the game's own wheel and its Cross made him the member player 2 plays): the game's
// wheel's Cross on chara (menu_dungeon.cpp's Select step, recipe), before it changes the player. 0: the game goes on as its own;
// 1: chara is another player's member, asked for as a trade (apart: across zones): the wheel closes, the member kept; 2: refused
// (the knocked-out menu may not take another player's member), the wheel stays.
int CharaWheelPick(int chara, int knocked_out);

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

// coop-townally (Jep 11-16: change character outside the dungeon, exactly as in it): the town's Allies page with a co-op session
// (TownAlliesOn; alone, retail's refusal). TownAlliesPick: its Cross on chara, the menu's player's pick: 0 nothing (that player's
// member already), 1 taken or asked (the menu closes, as the dungeon's after its change), -1 refused (the twin). A local menu's
// pick goes as a command; TownAlliesApply takes it on every copy (or this copy alone, apart): another player's member asks a
// trade (the question, in a town too), another member is the player's own (SetMember / SetPick, why page).
bool TownAlliesOn();
int  TownAlliesPick(int chara);
// coop-townally (Jep 11-16): where the Allies page's cursor starts: this menu's player's member (retail: cur_chara, the copy's game
// player, which in a town is Toan's body on every copy: apart his cursor started on Toan, not the member he plays). In town the member
// that player plays (TownAlliesPlays); on a floor cur_chara, which is the copy's player's member there. cur: the game's start.
int  AlliesCursorStart(int cur);
void TownAlliesApply(int player, int chara);

// coop-worlds: the player becomes chara at once, as the party menus' change does it.
bool PromptChangePlayer(int chara, MemberWhy why = MemberWhy::kChange);

// coop-atlaswap (tests, DC_COOP_DUNTEST_SWAP): the swap a yes to her Characters pick makes (Trade): the player takes the
// partner's member and the partner the player's, which loads on the next tick. False when the player could not change.
bool PromptTradeForTest();
// coop-swapcombo part 2: the other copy's step of a trade across zones (coop_worlds' kWhatTrade record): 0 an ask (a the asker's
// member, b the one it wants), 1 the answer (a the offer, b 1 yes / 0 no / 2 cannot / 3 no answer, c the one wanted), 3 a withdrawal.
void PromptApartTradeHeard(int from, int step, int a, int b, int c);
// Each tick (coop_worlds' tick, every mode): this copy's side of it (the change due, the asker's box, the asked player's popup).
void PromptApartTradeTick();
// coop-forcefloor: apart, this copy's player's member and the other player's as last known (the trade across zones' own words),
// and this copy's side of a trade agreed by a forced floor's call (SWAP): its player becomes own, the other other.
int  PromptApartOwnMember();
int  PromptApartOtherMember();
void PromptApartApply(int own, int other);

// coop-swapcombo (A): each player's member, kept across retail's resets to Toan (a dungeon's way out, a town, a dungeon's load):
// written where a member changes, player 1's restored at each floor's first tick of play (MemberRestoreTick, from the sim tick;
// MemberFloorStarted arms it at the floor's start). In the state set and the checksum.
extern int  g_member_of[6]; // (coop_members.cpp: written by SetMember alone)
extern bool g_member_restore_due;
bool        MemberRestorePending(); // in a dungeon, player 1 is not his kept member (retail's reset): his restore is due
void        MemberFloorStarted();
void        MemberRestoreTick();
// coop-memberowner stage 3: the member this copy's game player (cur_chara: the body it steps and draws now) should be, on a floor
// in play: Member(GamePlayer()); -1 no word (not on a floor in play, no member kept, her copy's town). *scope names the window
// it may differ in on purpose (a role swap, a scene's hand-over, retail's reset before the restore, a trade's change due, a
// scene; a floor's rule makes its member the player's, so it is none), nullptr none: outside one cur_chara must be it (MembersSelfTest).
int         MemberBodyExpected(const char **scope);

// coop-revive2 (Jep 11-18): the revive rule. The base game: a player lying down gets up only by a Revive Powder. An alive
// player near it (kReviveDistance) sees a red "!" over it (ReviveMarksDraw, its own screen alone) and with Cross is asked in the
// game's YES/NO window, "Use a Revive Powder?". YES spends one from the reviver's own bag and the one lying down gets up with
// half its life (retail's powder: movechara.cpp's max_hp >> 1); NO closes it. No powder in the reviver's bag: the refuse sound
// and "You have no Revive Powder." in the game's box on the reviver's screen, nothing else. The question never times out (the one
// lying down waits). A Revive Powder used from the bag on the one lying down works as before. DC_COOP_REVIVE_POWDER=0 (tests
// only, the host's value goes to the joiner in the hello): no question at all.
bool RevivePowderOn();
void RevivePowderAdopt(bool on);
// The free get-up, only while the session's debug mode is on (game.debug_mode: the host's, the same on every copy, as
// coop-debuggate's switches read it): one lying down gets up by itself after kDownTicks (3 s) with a quarter of its life.
// Off (the base game) it lies there until revived or both are down. DC_COOP_MUTATE=getup_always (twin): the old get-up in any mode.
bool ReviveFreeGetUp();

// Steps the question each tick player downed (1 up) lies down, for one alive player (reviver, 1 up) and whether it stands
// near (kReviveDistance, alive): its Cross (as every copy agrees it) asks. True on the tick its YES spends a powder.
bool ReviveAsk(int reviver, int downed, bool near);
// The revive test hook (DC_COOP_REVIVETEST, coop_party.cpp): player's Cross pressed on the next ReviveAsk, every copy alike.
void ReviveTestCross(int player);
// The no-powder box's frames left for player (1 up): presentation only, drawn on that player's screen (coop_useprompt.cpp).
int  ReviveNoneFrames(int player);
void ReviveNoneTick();

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
