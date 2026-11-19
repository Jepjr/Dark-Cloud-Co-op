// coop-memberowner: the one owner of each player's member (coop_members.hpp).
#include "coop/game_ns.hpp"
#include "coop/coop.hpp"
#include "coop/coop_audio.hpp"
#include "coop/coop_members.hpp"

#include <cstdio>
#include <cstring>

#include "dun/gameloop.hpp"
#include "main.hpp"
#include "menu_draw.hpp"
#include "snd.hpp"
#include "userstatus.hpp"

#include "coop/coop_actor.hpp"
#include "coop/coop_core.hpp"
#include "coop/coop_prompt.hpp"
#include "coop/coop_town.hpp"
#include "coop/coop_worlds.hpp"
#include "coop/netplay.hpp"

namespace coop {

// Each player's member (per player, every copy alike while linked: the state set's by name, build\snapshot-allow.txt; the
// checksum's; a resync's carried part "members"). Written by SetMember alone; a state load puts the whole set back as saved.
int g_member_of[kMaxPlayers] = {-1, -1, -1, -1, -1, -1};

namespace {

// This copy's player (apart the link's number is none: the worlds' own).
int Copy() {
    return CoopWorldsApart() ? CoopWorldsSelf() : NetplayLocalPlayer();
}

const char *const kMemberNames[6] = {"Toan", "Xiao", "Goro", "Ruby", "Ungaga", "Osmond"};

const char *Name(int chara) {
    return chara >= 0 && chara < 6 ? kMemberNames[chara] : "none";
}

const char *WhyName(MemberWhy why) {
    switch (why) {
        case MemberWhy::kChange:
            return "change";
        case MemberWhy::kPage:
            return "page";
        case MemberWhy::kPick:
            return "pick";
        case MemberWhy::kBump:
            return "bump";
        case MemberWhy::kKeep:
            return "keep";
        case MemberWhy::kTrade:
            return "trade";
        case MemberWhy::kApartTrade:
            return "apart trade";
        case MemberWhy::kZone:
            return "zone";
        case MemberWhy::kLoad:
            return "load";
        case MemberWhy::kNet:
            return "net";
        case MemberWhy::kTest:
            return "test";
    }
    return "?";
}

// A plain pick: the one refusal rule holds for it.
bool Plain(MemberWhy why) {
    return why == MemberWhy::kChange || why == MemberWhy::kPage || why == MemberWhy::kPick || why == MemberWhy::kBump;
}

// The members change hands: whoever held it moves off it (none kept), so the two never hold one member.
bool Displaces(MemberWhy why) {
    // DC_COOP_MUTATE=member_zone_keeps (t-memberowner's twin): a floor's rule moves nobody off the member it gives.
    return why == MemberWhy::kTrade || why == MemberWhy::kApartTrade || (why == MemberWhy::kZone && !Mutated("member_zone_keeps"));
}

// May say none (-1): another copy's word, a load, a test hook.
bool NoneOk(MemberWhy why) {
    return why == MemberWhy::kNet || why == MemberWhy::kLoad || why == MemberWhy::kTest;
}

int Holder(int player, int chara) {
    for (int other = 0; chara >= 0 && other < kMaxPlayers; other++) {
        if (other != player && g_member_of[other] == chara) {
            return other;
        }
    }
    return -1;
}

void Say(int player, int was, int chara, const char *why) {
    std::fprintf(stderr, "coop-member: player %d's member %d (%s) -> %d (%s), why %s, on player %d's copy%s at link tick %lld\n", player + 1, was,
                 Name(was), chara, Name(chara), why, Copy(), CoopWorldsApart() ? " (apart)" : "", (long long) NetplayLinkTick());
}

} // namespace

int Member(int player) {
    return player >= 0 && player < kMaxPlayers ? g_member_of[player] : -1;
}

bool MemberFree(int player, int chara, MemberWhy why) {
    // DC_COOP_MUTATE=member_unrefused (t-memberowner's twin): no refusal, as before the rule.
    return !Plain(why) || Holder(player, chara) < 0 || Mutated("member_unrefused");
}

bool SetMember(int player, int chara, MemberWhy why) {
    if (player < 0 || player >= kMaxPlayers || chara >= 6 || (NoneOk(why) ? chara < -1 : chara < 0)) {
        return false;
    }
    const int was = g_member_of[player];
    const int holder = Holder(player, chara);

    if (holder >= 0 && !MemberFree(player, chara, why)) {
        std::fprintf(stderr, "coop-member: player %d's %s of member %d (%s) refused: player %d holds it; player %d keeps %d (%s), on player %d's copy%s at link tick %lld\n",
                     player + 1, WhyName(why), chara, Name(chara), holder + 1, player + 1, was, Name(was), Copy(),
                     CoopWorldsApart() ? " (apart)" : "", (long long) NetplayLinkTick());
        return false;
    }
    if (holder >= 0 && Displaces(why)) {
        g_member_of[holder] = -1;
        Say(holder, chara, -1, "moved off it");
    }
    if (was == chara) {
        return true; // (nothing changes: nothing said)
    }
    g_member_of[player] = chara;
    // DC_COOP_MUTATE=member_unsaid (t-memberowner's twin): a trade's write (together or across zones) with no line, as a writer
    // beside SetMember would leave it.
    if ((why == MemberWhy::kTrade || why == MemberWhy::kApartTrade) && Mutated("member_unsaid")) {
        return true;
    }
    Say(player, was, chara, WhyName(why));
    return true;
}

void MemberSeed(int chara) {
    if (Mutated("member_unseeded")) {
        return;
    }
    SetMember(0, chara, MemberWhy::kLoad);
}

bool SetPick(int player, int chara, MemberWhy why) {
    if (!SetMember(player, chara, why)) {
        return false;
    }
    // (players past 2 have no body module of their own yet: their member alone)
    if (player == 1) {
        g_partner.choice = chara;
    }
    return true;
}

void SetPartnerBody(int chara) {
    g_partner.choice = chara;
}

// Player 2's copy apart with its own roles: its game player hers, the partner body player 1's. Not once its merge's state is in
// (her floor merge's roles swapped, coop_worlds' g_merge_state_in: player 1's member the game's player until the merge closes),
// except while her opening scene still plays on before its hand-over (the scene stays hers).
// DC_COOP_MUTATE=merge_roles_apart (t-memberowner's twin): apart is enough, as stage 3 first had it.
static bool HerOwnRoles() {
    if (!CoopWorldsApart() || CoopWorldsSelf() != 2) {
        return false;
    }
    return !CoopWorldsMergeStateInHere() || g_scene_handover.due != 0 || Mutated("merge_roles_apart");
}

int GamePlayer() {
    if (!HerOwnRoles()) {
        return 0;
    }
    return mode == GAME_MODE_DUNGEON ? 1 : -1;
}

int PartnerPlayer() {
    return HerOwnRoles() ? 0 : 1;
}

int OwnBodyChara(int game_chara) {
    const int self = Copy() - 1;
    int       chara = -1;

    if (!Enabled() || self < 0 || self >= kMaxPlayers) {
        return game_chara;
    }
    if (self == GamePlayer() || Mutated("presence_game_player")) {
        chara = game_chara;
    } else if (self == PartnerPlayer()) {
        chara = g_partner.loaded_chara;
    } else if (GamePlayer() < 0 && self == 1) {
        chara = TownPlayerMember(); // (her copy apart off a floor: the game's Toan stands in, her town body is co-op's)
    } else {
        chara = ActorChara(self);
    }
    if (chara < 0 || chara > 5) {
        chara = g_member_of[self] >= 0 ? g_member_of[self] : game_chara;
    }
    if (TestEnvOn("DC_COOP_TEST_MEMBERS")) {
        static int said = -2;
        if (said != chara * 8 + (game_chara & 7)) {
            said = chara * 8 + (game_chara & 7);
            std::fprintf(stderr, "coop-member: presence: player %d is drawn as %d (%s), the game's player %d, at link tick %lld\n", self + 1, chara, Name(chara),
                         game_chara, (long long) NetplayLinkTick());
        }
    }
    return chara;
}

bool SetPartnerPick(int chara, MemberWhy why) {
    // DC_COOP_MUTATE=partner_pick_hers (t-memberowner's twin): her member written for the body on her copy apart, as stage 2 had
    // it (her bump refused every tick through her floor merge's opening, x-entrwin-p2late-traded's OUT OF SYNC).
    if (PartnerPlayer() == 1 || Mutated("partner_pick_hers")) {
        return SetPick(1, chara, why);
    }
    SetPartnerBody(chara);
    return true;
}

void MembersHeard(int player, int member) {
    const int p = player - 1;
    const int self = Copy() - 1;

    if (p < 0 || p >= kMaxPlayers || p == self || member < 0 || member > 5 || g_member_of[p] == member) {
        return;
    }
    static int said = -2;
    const bool say = TestEnvOn("DC_COOP_TEST_MEMBERS") && said != p * 8 + member;

    if (say) {
        said = p * 8 + member;
        std::fprintf(stderr, "coop-member: heard player %d's member %d (%s) (recorded %d) on player %d's copy at link tick %lld\n", player, member, Name(member),
                     g_member_of[p], self + 1, (long long) NetplayLinkTick());
    }
    if (self >= 0 && self < kMaxPlayers && g_member_of[self] == member) {
        if (say) {
            std::fprintf(stderr, "coop-member: heard word kept out: this copy's player %d holds %d (%s)\n", self + 1, member, Name(member));
        }
        return;
    }
    if (Mutated("heard_member_unwritten")) {
        return;
    }
    // (the player this copy's partner body plays: their pick too, the body's wish, as a trade across zones writes it: the merge's
    // state set then carries a pick that is their member, and the partner body loads it)
    if (p == PartnerPlayer()) {
        SetPick(p, member, MemberWhy::kNet);
    } else {
        SetMember(p, member, MemberWhy::kNet);
    }
    if (say) {
        std::fprintf(stderr, "coop-member: heard word written: player %d's member %d, the partner body's pick %d on player %d's copy\n", player, g_member_of[p],
                     g_partner.choice, self + 1);
    }
}

void MemberRefused(int player, int chara, MemberWhy why) {
    (void) chara;
    if (why == MemberWhy::kBump) {
        return; // (nobody's press)
    }
    SoundOwner sound(player + 1);

    SndSePlay(MENU_SOUND_REFUSE, -1, 0);
}

void MemberBreakForTest(int player, int chara) {
    if (player >= 0 && player < kMaxPlayers) {
        g_member_of[player] = chara;
    }
}

namespace {
int g_said_state[kMaxPlayers + 3] = {-2, -2, -2, -2, -2, -2, -2, -2, -2}; // (the last state said: DC_COOP_TEST_MEMBERS only)
}

void MembersStateSetApplied() {
    if (!TestEnvOn("DC_COOP_TEST_MEMBERS")) {
        return;
    }
    g_said_state[0] = -2;
    std::fprintf(stderr, "coop-member: state set applied: members %d %d %d %d %d %d on player %d's copy at link tick %lld\n", g_member_of[0], g_member_of[1],
                 g_member_of[2], g_member_of[3], g_member_of[4], g_member_of[5], Copy(), (long long) NetplayLinkTick());
}

void MembersSelfTest() {
    static int        said_broken[kMaxPlayers + 1] = {-2, -2, -2, -2, -2, -2, -2};
    int *const        said_state = g_said_state;
    static const bool say_states = TestEnvOn("DC_COOP_TEST_MEMBERS");

    int first = -1, second = -1;

    for (int a = 0; a < kMaxPlayers && first < 0; a++) {
        for (int b = a + 1; b < kMaxPlayers; b++) {
            if (g_member_of[a] >= 0 && g_member_of[a] == g_member_of[b]) {
                first = a;
                second = b;
                break;
            }
        }
    }
    // Player 2's pick is her member: together (apart, her copy's player is the game's own and its pick waits for the merge) and
    // outside a role swap (the partner body is then another's for the scene).
    const bool roles = CoopWorldsRolesSwapping() || g_scene_handover.due != 0;
    const bool pick_off = !CoopWorldsApart() && !roles && g_member_of[1] >= 0 && g_partner.choice != g_member_of[1];

    // Outside a scoped window, the game's player on a floor in play is its member (MemberBodyExpected). Said once a body and member.
    const char *scope = nullptr;
    const int   expected = UserStatus != nullptr ? MemberBodyExpected(&scope) : -1;
    static int  said_body = -2;
    const int   body = UserStatus != nullptr ? (int) UserStatus->cur_chara : -1;

    if (expected >= 0 && scope == nullptr && body != expected) {
        if (said_body != body * 8 + expected) {
            said_body = body * 8 + expected;
            std::fprintf(stderr, "coop-member: SELF-TEST FAIL the game's player is %d (%s), its member %d (%s) (player %d's), outside every window, on player %d's copy%s at link tick %lld\n",
                         body, Name(body), expected, Name(expected), GamePlayer() + 1, Copy(), CoopWorldsApart() ? " (apart)" : "", (long long) NetplayLinkTick());
        }
    } else {
        said_body = -2;
    }
    // The review's shadows (11-13): retail's nowUnitNow follows the game's player (selectChrUnit, the floor's start, the menu's
    // close compares it); players past 2 keep their body's member in the actor's entry (coop_actor's g_players[i].chara, no
    // loader of its own yet). Outside every window each is its member. Said once a state.
    static int said_shadow = -2;
    int        shadow = -2;

    if (expected >= 0 && scope == nullptr && nowUnitNow != body) {
        shadow = 100 + nowUnitNow;
    }
    for (int p = 2; p < kMaxPlayers && shadow == -2; p++) {
        if (g_member_of[p] >= 0 && ActorChara(p) >= 0 && ActorChara(p) != g_member_of[p]) {
            shadow = p * 10 + ActorChara(p);
        }
    }
    if (shadow != -2 && shadow != said_shadow) {
        if (shadow >= 100) {
            std::fprintf(stderr, "coop-member: SELF-TEST FAIL retail's nowUnitNow %d is not the game's player %d (%s), outside every window, on player %d's copy at link tick %lld\n",
                         shadow - 100, body, Name(body), Copy(), (long long) NetplayLinkTick());
        } else {
            std::fprintf(stderr, "coop-member: SELF-TEST FAIL player %d's body plays %d, not their member %d, on player %d's copy at link tick %lld\n", shadow / 10 + 1,
                         shadow % 10, g_member_of[shadow / 10], Copy(), (long long) NetplayLinkTick());
        }
    }
    said_shadow = shadow;

    int broken[kMaxPlayers + 1];

    std::memcpy(broken, g_member_of, sizeof g_member_of);
    broken[kMaxPlayers] = g_partner.choice;
    if ((first >= 0 || pick_off) && std::memcmp(said_broken, broken, sizeof broken) != 0) {
        std::memcpy(said_broken, broken, sizeof broken);
        if (first >= 0) {
            std::fprintf(stderr, "coop-member: SELF-TEST FAIL players %d and %d both hold member %d (%s): members %d %d %d %d %d %d on player %d's copy%s at link tick %lld\n",
                         first + 1, second + 1, g_member_of[first], Name(g_member_of[first]), g_member_of[0], g_member_of[1], g_member_of[2],
                         g_member_of[3], g_member_of[4], g_member_of[5], Copy(), CoopWorldsApart() ? " (apart)" : "",
                         (long long) NetplayLinkTick());
        }
        if (pick_off) {
            std::fprintf(stderr, "coop-member: SELF-TEST FAIL player 2's pick %d (%s) is not her member %d (%s) on player %d's copy at link tick %lld\n",
                         g_partner.choice, Name(g_partner.choice), g_member_of[1], Name(g_member_of[1]), Copy(), (long long) NetplayLinkTick());
        }
    }
    if (say_states && UserStatus != nullptr) {
        int state[kMaxPlayers + 3];

        std::memcpy(state, g_member_of, sizeof g_member_of);
        state[kMaxPlayers] = UserStatus->cur_chara;
        state[kMaxPlayers + 1] = g_partner.choice;
        state[kMaxPlayers + 2] = CoopWorldsApart() ? 1 : 0;
        if (std::memcmp(said_state, state, sizeof state) != 0) {
            std::memcpy(said_state, state, sizeof state);
            std::fprintf(stderr, "coop-member: state: members %d %d %d %d %d %d, the game's player %d, her pick %d (loaded %d), %s, %s, on player %d's copy at link tick %lld\n",
                         g_member_of[0], g_member_of[1], g_member_of[2], g_member_of[3], g_member_of[4], g_member_of[5], (int) UserStatus->cur_chara,
                         g_partner.choice, g_partner.loaded_chara, CoopWorldsApart() ? "apart" : "together", first >= 0 || pick_off ? "BROKEN" : "distinct",
                         Copy(), (long long) NetplayLinkTick());
        }
    }
}

} // namespace coop
