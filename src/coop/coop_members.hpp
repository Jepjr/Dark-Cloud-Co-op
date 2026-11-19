// coop-memberowner (the swap root cause analysis, 11-13: every swap and trade bug since rc1 was one of several holders of "who
// plays which member" disagreeing): one owner of each player's member. Members holds who plays what, per player (kMaxPlayers),
// in the state set, the checksum and a resync's carried parts; SetMember is its only writer, one log line a change (who, why).
// The game's own cur_chara keeps retail's meaning, the body this copy steps and draws now; it is put back to the game's
// player's member at one point (coop_prompt's MemberRestoreTick). Player 2's pick (g_partner.choice) is the partner body's
// wish, written through SetPick alone: her member first, then the body's wish; a role swap sets the body alone (SetPartnerBody).
#pragma once

namespace coop {

// Why a member is written: the log's word, and the refusal rule's exemptions.
enum class MemberWhy {
    kChange,     // the game's player's own change of member, model and all (ChangePlayer: his ring, a test hook, the restore)
    kPage,       // the Characters page's own pick, as retail loads it
    kPick,       // a player's own pick for their body (her ring, menu, L3)
    kBump,       // her pick cannot play (the player took it, it lies down): the next member who can
    kKeep,       // a pick refused by a floor's rule: she keeps the member she plays
    kTrade,      // a trade: the two players' members change hands
    kApartTrade, // a trade across zones, this copy's side of it
    kZone,       // a character-limited floor's rule gives the floor's member to a player on it; who held it moves on
    kLoad,       // a save's pick, a new game's, the lobby's
    kNet,        // another copy's word: a resync's carried part, the host's state, a joiner's pick
    kTest,       // a test hook's
};

// Player index's member (0 player 1), -1 none kept yet.
int Member(int player);

// Whether player may take chara for why: no other player holds it, or why is one the rule exempts (a trade, a floor's rule,
// another copy's word, a load, a test hook).
bool MemberFree(int player, int chara, MemberWhy why);

// The only writer. False when nothing is written: no such player or member, or the one refusal rule: a member another player
// holds is not taken by a plain pick (kChange, kPage, kPick, kBump), said in one line. A trade or a floor's rule moves the
// other holder off it (none kept, one line), so no two players hold one member afterwards.
bool SetMember(int player, int chara, MemberWhy why);

// Player 1's member as a game starts (a save's load, a new game, the lobby's pick: Jep's rule 11-14, the member he starts as). Not
// in the save: each start's lobby seeds it again. DC_COOP_MUTATE=member_unseeded (twin): none kept until his first change, as
// before (his lobby pick lost at the dungeon's load).
void MemberSeed(int chara);

// A player's pick for their own body: their member (SetMember), then the body's wish (player 2's partner: g_partner.choice,
// loaded by PickPartner). chara -1: no pick (a load, a new game, a session reset). False when SetMember refuses.
bool SetPick(int player, int chara, MemberWhy why);

// The player whose member this copy's game player (cur_chara, the body it steps and draws) is, by mode: together player 1's on
// every copy; apart this copy's own on a floor; -1 on player 2's copy apart off a floor (a town, a building: the game's Toan
// stands in, her member is co-op's town body).
int GamePlayer();

// coop-memberstage4 (stage 4's per-read audit): the member this copy's own player is drawn as now (their body: the game's player,
// the partner body, a third player's actor entry; her copy apart off a floor: her town body). For reads that mean "the person at
// this screen" rather than "the body the game steps": Discord's presence. game_chara: the game's own answer (cur_chara), kept
// when nothing better is known (no co-op, no player). DC_COOP_MUTATE=presence_game_player (t-memberowner's twin): the game's.
int OwnBodyChara(int game_chara);

// The player whose member this copy's partner body plays: player 2's, except on player 2's copy apart, where the game's player
// is hers and the partner body stands for player 1 (her floor merge's opening scene, before its hand-over).
int PartnerPlayer();

// The partner body's own pick (PickPartner: her pick cannot play, the next who can; a floor's rule keeps the one loaded): her
// member and the body's wish (SetPick) where the body is player 2's; elsewhere (PartnerPlayer() is another's) the body's wish
// alone, no player's member changes (this copy is not that player's).
bool SetPartnerPick(int chara, MemberWhy why);

// A role swap's partner body (coop_worlds' FloorRolesSwap: her floor merge, a scene's handover, a Toan-only story event): the
// member the partner body loads while the roles are swapped. No player's member changes.
void SetPartnerBody(int chara);

// coop-memberstage4 (item 3): apart, another copy's presence names its player's member (player 1-based, member -1 none): written
// to Members as its word (why net) when it differs, so a member changed apart reaches every copy (its asks, the floor fences'
// holder, the merge's state set). This copy's own player is never moved by it: a word naming his member is an older one (a
// presence sent before a trade's apply), kept out, said once. DC_COOP_MUTATE=heard_member_unwritten (twin): heard, not written.
void MembersHeard(int player, int member);

// The picker's side of a refusal: the refuse sound on their screen.
void MemberRefused(int player, int chara, MemberWhy why);

// The invariants, read at each checksum and each tick of every mode: no two players hold one member; together and outside a
// role swap, player 2's pick is her member. A break is said once ("coop-member: SELF-TEST FAIL ...") for each new state that
// breaks one. DC_COOP_TEST_MEMBERS=1: every new state is said.
void MembersSelfTest();
// A state set was put over this copy's memory (a state file, a merge, a snapshot join): the members are the set's, whatever
// they were. With DC_COOP_TEST_MEMBERS it is said, so a log's play-back starts again from them.
void MembersStateSetApplied();

// DC_COOP_MUTATE=member_dup (t-memberowner's twin): the invariant broken on purpose, a member written beside the rule.
void MemberBreakForTest(int player, int chara);

} // namespace coop
