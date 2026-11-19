#include "coop/game_ns.hpp"
#include "coop/coop.hpp"
#include "coop/coop_audio.hpp"
#include "coop/coop_zonechara.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "btactstatus.hpp"
#include "dngstatusdata.hpp" // coop-deadswap: the down op's CheckActItemSlot
#include "itemdata.hpp"      // coop-deadswap: ITEM_REVIVAL_POWDER
#include "dngmessageman.hpp"
#include "dun/gameloop.hpp"
#include "gamepad.hpp"
#include "menu_draw.hpp"
#include "runscript.hpp"
#include "snd.hpp"
#include "userstatus.hpp"

namespace dc::game {
extern s32        gameTask;
extern s32        mode; // GAME_MODE_DUNGEON is 3 (main.hpp)
extern s32        selectMapNo; // the dungeon (coop-swapcombo's nextfloor op)
extern CRunScript EdEventScript; // the floor's event interpreter (editloop3.cpp)
void              ToanKey_On(); // charakey.cpp: Toan's Cross (coop-swapcombo's test op)
} // namespace dc::game

#include "coop/coop_actor.hpp"
#include "coop/coop_combat.hpp" // coop-swapcombo: StartAttack (the swing op)
#include "coop/coop_core.hpp"
#include "coop/coop_fullmenu.hpp"
#include "coop/coop_menu.hpp"
#include "coop/coop_message.hpp"
#include "coop/coop_party.hpp"
#include "coop/coop_prompt.hpp"
#include "coop/coop_town.hpp"   // towncrash: TownBodyOnFloorForTest (the townbody op)
#include "coop/coop_wheel.hpp"
#include "coop/coop_worlds.hpp"
#include "coop/netplay.hpp"

namespace coop {

namespace {

const char *const kZoneNames[6] = {"Toan", "Xiao", "Goro", "Ruby", "Ungaga", "Osmond"};

const char *Name(int chara) {
    return chara >= 0 && chara < 6 ? kZoneNames[chara] : "none";
}

// The floor script's own statements (the interpreter's vmcode_t): op, arg1, arg2.
constexpr int kOpLoad = 1;
constexpr int kOpPush = 3;
constexpr int kOpPop = 4;
constexpr int kOpRet = 15;
constexpr int kOpCall = 19;
constexpr int kOpExt = 21;
constexpr int kExtSetMainChr2 = 1067;
constexpr int kExtSetLimitZone = 1068;

bool Is(const vmcode_t &code, int op, int arg1, int arg2) {
    return code.op == op && code.arg1 == arg1 && code.arg2 == arg2;
}

// The test's own interpreter (DC_COOP_TEST_ZONE_FORCE), beside the floor's.
CRunScript *g_test_script = nullptr;

// The interpreter whose operand stack holds stack (an external function's arguments lie on its caller's stack).
CRunScript *ScriptOf(RS_STACKDATA *stack) {
    for (CRunScript *script : {&EdEventScript, g_test_script}) {
        if (script != nullptr && script->stack != nullptr && stack >= script->stack && stack < script->stack_end) {
            return script;
        }
    }
    return nullptr;
}

} // namespace

int ZoneForcedMember() {
    if (!Enabled() || UserStatus == nullptr) {
        return -1;
    }
    const int zone = UserStatus->res_limit_zone_current;

    return zone >= RES_LIMIT_ZONE_TOAN && zone <= RES_LIMIT_ZONE_OSMOND ? zone : -1;
}

bool ZonePlayerOnFloor(int index) {
    if (index == 0) {
        return true;
    }
    if (CoopWorldsApart()) {
        return false;
    }
    if (index == 1) { // (at a floor's start her body is not loaded yet: she counts by her presence, StepPresence's)
        return (ActorPresent(1) || PartnerCounted()) && !MenuPartnerAway();
    }
    return ActorPresent(index);
}

int ZoneMemberOf(int index) {
    if (index == 0 && FullMenuViewPlayerChara() >= 0) {
        return FullMenuViewPlayerChara(); // (inside her menu's view the globals hold her member)
    }
    if (index == 1 && g_partner.loaded_chara < 0) {
        return g_partner.choice >= 0 && g_partner.choice < 6 ? g_partner.choice : -1; // the member she loads (PickPartner)
    }
    return ActorChara(index);
}

int ZoneHolders(int chara, int except) {
    int count = 0;

    for (int i = 0; i < kMaxPlayers; i++) {
        if (i != except && ZonePlayerOnFloor(i) && ZoneMemberOf(i) == chara) {
            count++;
        }
    }
    // coop-forcefloor, coop-golapse (Jep 11-14: never two players on one member): apart, the other copy's player plays its member
    // elsewhere (on its way here after its go, or a go that lapsed: still that member there), so it counts as played: the floor's
    // force and the leave-floor switch leave this copy's player on its own. DC_COOP_MUTATE=forcefloor_elsewhere_ignored (twin): not
    // counted (this player switched, two players on one member).
    if (chara >= 0 && CoopWorldsApart() && PromptApartOtherMember() == chara && !Mutated("forcefloor_elsewhere_ignored")) {
        count++;
    }
    return count;
}

bool ZonePickLegal(int index, int chara, bool apply) {
    const int zone = ZoneForcedMember();

    if (zone < 0 || (apply && Mutated("zonepick_open"))) {
        return true;
    }
    // Only a player on z can leave it unplayed, and only when nobody else on the floor plays it.
    if (ZoneMemberOf(index) != zone || chara == zone) {
        return true;
    }
    return ZoneHolders(zone, index) > 0;
}

void ZonePickRefused(int index, int chara, const char *how) {
    const int zone = ZoneForcedMember();

    {
        SoundOwner sound(index + 1);

        SndSePlay(MENU_SOUND_REFUSE, -1, 0);
    }
    // The zone's notice (CDngMessageMan::LimmitZone: "Limited zone", only <z>), tagged for the picker's screen alone.
    DngMessMan.LimmitZone();
    DngMessMan.insert_mes_1 = (s16) (zone + 0x32);
    DngMesBy(index + 1);
    std::fprintf(stderr, "coop-zone: player %d's pick of %d (%s) refused (%s): nobody else on the floor plays %d (%s); notice %d on their screen at link tick %lld\n",
                 index + 1, chara, Name(chara), how, zone, Name(zone), (int) DngMessMan.message, (long long) NetplayLinkTick());
}

bool ZoneForceMetByOther(RS_STACKDATA *stack, int chara) {
    if (!Enabled() || UserStatus == nullptr || chara < 0 || chara > 5) {
        return false;
    }

    // The floor start's zone force: F(z) returns into "POP; PUSH 1068; PUSH z; EXT 2", _SET_LIMMIT_ZONE(z), in every one of
    // the 15 floors' scripts. A story scene's F(0) (a Toan scene) is followed by anything else and stays retail.
    CRunScript *script = ScriptOf(stack);

    if (script == nullptr || script->call_sp == nullptr || script->call_sp <= script->call) {
        return false;
    }
    const vmcode_t *ret = (script->call_sp - 1)->ret;

    if (ret == nullptr || !Is(ret[1], kOpPop, 0, 0) || !Is(ret[2], kOpPush, 1, kExtSetLimitZone) || !Is(ret[3], kOpPush, 1, chara) ||
        !Is(ret[4], kOpExt, 2, 0)) {
        return false;
    }

    const int others = ZoneHolders(chara, 0);

    // coop-forcefloor: the floor's own force against the table the gates read before the floor (coop_worlds' ZoneFloorChara).
    if (script == &EdEventScript) {
        const int table = CoopWorldsZoneFloorChara(selectMapNo, UserStatus->cur_floor);

        std::fprintf(stderr, "coop-zone: forcefloor: table: map %d floor %d fenced to %d (%s) by its script, the table says %d: %s\n", selectMapNo,
                     (int) UserStatus->cur_floor, chara, Name(chara), table, table == chara ? "agree" : "DIFFER");
    }
    if (others > 0 && CoopWorldsApart() && PromptApartOtherMember() == chara) {
        std::fprintf(stderr, "coop-zone: forcefloor: player %d plays %d (%s) elsewhere%s: it counts as playing it here\n", 3 - CoopWorldsSelf(), chara,
                     Name(chara), CoopWorldsForceComing() == chara ? " (on its way onto this floor)" : "");
    }

    if (Mutated("zoneforce_skipall")) { // (twin of the nobody case: player 1 kept whoever plays z)
        std::fprintf(stderr, "coop-zone: the floor's zone %d (%s): player 1 keeps %d (zoneforce_skipall)\n", chara, Name(chara), (int) UserStatus->cur_chara);
        return true;
    }

    if (others == 0 || Mutated("zoneforce_p1")) {
        std::fprintf(stderr, "coop-zone: the floor's zone %d (%s): no other player plays it, player 1 (%d) is switched as retail at link tick %lld\n", chara,
                     Name(chara), (int) UserStatus->cur_chara, (long long) NetplayLinkTick());
        // coop-memberowner (Jep's rule 11-14: a forced character becomes the player's member; no going back once the floor's
        // restriction lifts, a swap the normal way brings another): the game's player on this copy keeps it as its member (who
        // held it moves off it). DC_COOP_MUTATE=zone_force_unkept (twin): not kept, the member before comes back at the next
        // floor's restore, as before.
        if (GamePlayer() >= 0 && !Mutated("zone_force_unkept")) {
            SetMember(GamePlayer(), chara, MemberWhy::kZone);
        }
        return false;
    }
    std::fprintf(stderr, "coop-zone: the floor's zone %d (%s): %d other player(s) already play it, player 1 keeps %d (%s) at link tick %lld\n", chara,
                 Name(chara), others, (int) UserStatus->cur_chara, Name(UserStatus->cur_chara), (long long) NetplayLinkTick());
    return true;
}

bool ZoneMenuPicksOpen() {
    if (!Enabled()) {
        return false;
    }
    for (int i = 1; i < kMaxPlayers; i++) {
        if (ZonePlayerOnFloor(i)) {
            return true;
        }
    }
    return false;
}

bool ZoneNonHoldersSitOut() {
    // Jep's decision (11-11): on a character-limited floor every player fights; the sit-out of a player not on z stays as a switch,
    // OFF. On, it holds for every non-holder alike, player 1 too (ZoneSitsOut). DC_COOP_ZONE_SITOUT=1 (the rows' "on", both copies)
    // and DC_COOP_MUTATE=zone_sitout (the off rows' twin) turn it on.
    constexpr bool kNonHoldersSitOut = false;
    static const bool test_on = TestEnvOn("DC_COOP_ZONE_SITOUT");

    return kNonHoldersSitOut || test_on || Mutated("zone_sitout");
}

bool ZoneSitsOut(int index) {
    const int zone = ZoneForcedMember();

    if (zone < 0 || !ZoneNonHoldersSitOut() || index < 0 || index >= kMaxPlayers) {
        return false;
    }
    // (Player 2 before her body loads is no holder, as SitsOutFightZone always had it; the others by the member they play.)
    const int member = index == 1 ? g_partner.loaded_chara : ActorChara(index);

    return member != zone;
}

namespace {

// The player whose pad is the game's (port 0): whose swing, item and aim the floor's retail code runs on this tick.
int GamePadIndex() {
    const int player = NetplayGamePadPlayer();

    return player >= 1 ? player - 1 : 0;
}

bool PortPlayerSitsOut() {
    const int index = GamePadIndex();

    // (Player 2's own fighting is the partner's, SitsOutFightZone; DC_COOP_MUTATE=fightzone_p1 is the twin: the port's player fights.)
    return mode == 3 && index != 1 && ZoneSitsOut(index) && !Mutated("fightzone_p1");
}

} // namespace

void ZoneNoteSwing(int index) {
    const int zone = ZoneForcedMember();

    // (The evidence the swing was taken where the rule is asked: a non-holder of the floor's member swinging, the switch off.)
    if (zone >= 0 && !ZoneSitsOut(index) && (index == 1 ? g_partner.loaded_chara : ActorChara(index)) != zone) {
        std::fprintf(stderr, "coop-zone: player %d (not on zone %d, %s) swings: nobody sits out here at link tick %lld\n", index + 1, zone, Name(zone),
                     (long long) NetplayLinkTick());
    }
}

bool ZoneRefusesPortSwing(const char *what) {
    if (!PortPlayerSitsOut()) {
        if (mode == 3 && GamePadIndex() != 1) {
            ZoneNoteSwing(GamePadIndex());
        }
        return false;
    }
    const int index = GamePadIndex();

    {
        SoundOwner sound(index + 1);

        SndSePlay(MENU_SOUND_REFUSE, -1, 0);
    }
    std::fprintf(stderr, "coop-zone: player %d sits out zone %d (%s): %s refused at link tick %lld\n", index + 1, ZoneForcedMember(),
                 Name(ZoneForcedMember()), what, (long long) NetplayLinkTick());
    return true;
}

void ZoneFilterPort(PAD_STATUS *status) {
    static std::uint16_t before = 0;

    if (!PortPlayerSitsOut()) {
        before = 0;
        return;
    }
    // Square (item, boost) and R2 (aim) are held off the game's pad; the swing is refused where it starts (DunMoveChara's attack
    // start, ZoneRefusesPortSwing), so Cross still talks and opens things.
    const std::uint16_t fenced = status->button & (kInputSquare | kInputR2);

    status->button &= ~(kInputSquare | kInputR2) & 0xFFFF;
    if ((fenced & ~before) != 0) {
        ZoneRefusesPortSwing("Square or R2");
    }
    before = fenced;
}

bool ZoneMenuGrey(int chara, bool pick) {
    static signed char shown[6] = {-1, -1, -1, -1, -1, -1};
    const int          zone = ZoneForcedMember();

    if (zone < 0 || chara < 0 || chara > 5 || UserStatus == nullptr) {
        std::fill(shown, shown + 6, (signed char) -1);
        return false;
    }
    // The page's owner: the player whose menu engine runs on this copy (her local menu), else the entry swapped in, else the menus' player.
    const int picker = FullMenuRunsFor(2) ? 1 : ActorSwapped() > 0 ? ActorSwapped() : 0;
    // (Their own member; another player's is a trade, which always goes; any other only when it would leave z unplayed.)
    const bool grey = !Mutated("zonemenu_nogrey") && chara != ZoneMemberOf(picker) && ZoneHolders(chara) == 0 && !ZonePickLegal(picker, chara, false);

    if (pick) {
        if (grey) {
            ZonePickRefused(picker, chara, "page");
        }
        return grey;
    }
    if (shown[chara] != (signed char) grey) { // (the page draws each frame: the line is its change)
        shown[chara] = (signed char) grey;
        std::fprintf(stderr, "coop-zone: player %d's Characters page: %d (%s) %s at link tick %lld\n", picker + 1, chara, Name(chara),
                     grey ? "greyed (it would leave the floor's member unplayed)" : "pickable", (long long) NetplayLinkTick());
    }
    return grey;
}

namespace {

// Nobody on the floor plays z (its player left the floor or dropped): the lowest-numbered player on it is switched to z, by the
// routine its own pick uses (player 1: PromptChangePlayer, the floor start's selectChrUnit's co-op form; another: its pick).
// DC_COOP_MUTATE=zone_noswitch (twin): nobody is.
void LeaveFloorSwitch() {
    const int zone = ZoneForcedMember();

    // (Not while a pick is loading: a trade gives her z by her pick, which PickPartner loads after this step.)
    const bool loading = g_partner.loaded_chara >= 0 && g_partner.choice >= 0 && g_partner.choice != g_partner.loaded_chara;

    if (zone < 0 || !CoopPlainPlay() || BtEventMode != 0 || zone >= UserStatus->party_size || ZoneHolders(zone) > 0 ||
        PromptSwapOpen() || loading) {
        return;
    }
    for (int i = 0; i < kMaxPlayers; i++) {
        if (!ZonePlayerOnFloor(i)) {
            continue;
        }
        if (i == 1 && g_partner.choice == zone) {
            return; // already asked for, loading
        }
        if (Mutated("zone_noswitch")) {
            static bool said = false;

            if (!said) {
                said = true;
                std::fprintf(stderr, "coop-zone: nobody on the floor plays %d (%s): nobody switched (zone_noswitch)\n", zone, Name(zone));
            }
            return;
        }
        std::fprintf(stderr, "coop-zone: nobody on the floor plays %d (%s): player %d (%d) is switched to it at link tick %lld\n", zone, Name(zone), i + 1,
                     ZoneMemberOf(i), (long long) NetplayLinkTick());
        // (coop-memberowner: why zone: the floor's rule gives the member whoever held it, who moves on)
        if (i == 0) {
            PromptChangePlayer(zone, MemberWhy::kZone);
        } else if (i == 1) {
            SetPick(1, zone, MemberWhy::kZone); // (loaded by PickPartner this tick)
        }
        return;
    }
}

// ---- Test hooks -------------------------------------------------------------------------------------------------------
// DC_COOP_TEST_ZONE_FORCE=z: once the state file is back and the partner is up, the floor script's zone sequence runs in an
//   interpreter of its own on the floor's external functions, as the 15 floors' scripts have it: F(z) = _SET_MAIN_CHR2(z), then
//   POP, PUSH 1068, PUSH z, EXT 2 (_SET_LIMMIT_ZONE(z)). (F's draw flag and waits are left out: no frame is drawn between.)
// DC_COOP_TEST_ZONE_PICKS=op;op;...: then, one at a time, each once the last has settled (no question open, the partner's pick
//   loaded): p<N>:radial:<c> (the ring's Cross on c), p<N>:menu:<c> (the Characters page's pick of c), race:<y> (player 1's ring
//   open on player 2's member x; player 2 becomes y; then the ring's Cross on the x it offered), both (each player asks the
//   other for their member on one tick), leave (player 2 leaves the floor, her overlay's Leave floor).
//   force:<z> (the floor script's zone sequence for z, now). lobby:<c> (the lobby's pick c for player 1, as its start applies it).
//   at:<tick> (wait until that link tick: the pad presses of DC_COOP_MENUPRESS come on the same clock).
//   down:<N> (coop-deadswap: player N goes down now: player 1 by his co-op down, player 2 by her fall). downasked:<N>:<T> (player N
//   goes down T ticks after the next swap question is up, while it waits for its answer).
// DC_COOP_TEST_ZONE_ANSWER=yes: a swap question is answered yes by the one asked.

struct ZoneTest {
    bool             armed = false;
    bool             forced = false;
    std::vector<std::string> ops;
    size_t           next = 0;
    bool             waiting = false;
    int              race_stage = 0;
    int              race_x = -1;
    int              race_y = -1;
    bool             done = false;
    int              down_who = 0;   // coop-deadswap: downasked's player
    int              down_after = 0; // its ticks after the question is up
    int              down_up = -1;   // the ticks the question has been up (-1 not yet)
};

ZoneTest g_test;

// coop-deadswap: player who goes down, every copy on this tick. Player 1: his life to 0, and the game's own check of it in his step
// (movechara: CheckLife() <= 0, out of his hit-proof frames) takes him down the co-op way (PlayerGoesDown), as a monster's blow
// does; a Revival Powder among his active items is taken out first (the game's death would use it). Player 2: her fall.
void TestDown(int who, const char *op) {
    if (who == 1) {
        auto *status = (CDngStatusData *) UserStatus;

        for (int slot; (slot = status->CheckActItemSlot(ITEM_REVIVAL_POWDER)) != -1;) {
            std::fprintf(stderr, "coop-test: zone op %s: player 1's Revival Powder in active slot %d taken out first\n", op, slot);
            status->item_pack.quick_item_slot[slot] = -1;
        }
        UserStatus->hp[UserStatus->cur_chara] = 0;
        BtActStatus.invincible_frames = 0;
    } else if (who == 2) {
        PartnerFalls("zone test");
    }
    std::fprintf(stderr, "coop-test: zone op %s: player %d goes down at link tick %lld\n", op, who, (long long) NetplayLinkTick());
}

void Members(char *out, size_t size) {
    std::snprintf(out, size, "p1 %d p2 %d", ActorChara(0), ActorChara(1));
}

void RunForce(int zone) {
    // Header, program table, then the code: F's record, main's record, F's body, main's body (16-byte disc records).
    static std::int32_t image[64];
    std::memset(image, 0, sizeof image);
    constexpr int kCode = 32;                // bytes
    std::int32_t *code = image + kCode / 4;
    const int     f_record = 0;              // code-relative
    const int     main_record = 16;
    const int     f_body = 32;
    std::int32_t *w = code + f_body / 4;
    auto put = [&](int op, int a, int b) { *w++ = op; *w++ = a; *w++ = b; };

    put(kOpPush, 1, kExtSetMainChr2);
    put(kOpLoad, 0, 1);
    put(kOpExt, 2, 0);
    put(kOpPush, 1, 0);
    put(kOpRet, 0, 0);
    const int main_body = (int) ((w - code) * 4);
    put(kOpPush, 1, zone);
    put(kOpCall, 0, f_record);
    put(kOpPop, 0, 0);
    put(kOpPush, 1, kExtSetLimitZone);
    put(kOpPush, 1, zone);
    put(kOpExt, 2, 0);
    put(kOpPush, 1, 0);
    put(kOpRet, 0, 0);
    const int prog_table = (int) ((w - image) * 4);
    code[f_record / 4 + 0] = f_body;
    code[f_record / 4 + 2] = 1; // local
    code[f_record / 4 + 3] = 1; // arg
    code[main_record / 4 + 0] = main_body;
    *w++ = 0;                       // no
    *w++ = kCode + main_record;     // func (base-relative)
    image[0] = 0;
    image[1] = kCode + main_record; // main
    image[2] = kCode;               // code
    image[3] = prog_table;          // prog
    image[4] = 1;                   // prog_num

    static CRunScript script;
    char              before[32];

    Members(before, sizeof before);
    g_test_script = &script;
    script.load(reinterpret_cast<RS_PROG_HEADER *>(image), nullptr, 128, nullptr, 512);
    script.ext_func(EdEventScript.ext_func_table, EdEventScript.ext_func_num);
    const int result = EdEventScript.ext_func_table != nullptr ? script.run(0) : -2;
    g_test_script = nullptr;

    char after[32];

    Members(after, sizeof after);
    std::fprintf(stderr, "coop-test: zone force %d (%s): ran %d, members %s -> %s, zone now %d, holders %d at link tick %lld\n", zone, Name(zone), result,
                 before, after, (int) UserStatus->res_limit_zone_current, ZoneHolders(zone), (long long) NetplayLinkTick());
}

bool Settled() {
    return CoopPlainPlay() && !PromptSwapOpen() && (g_partner.choice < 0 || g_partner.choice == g_partner.loaded_chara);
}

void Report(const char *what) {
    char now[32];
    const int zone = ZoneForcedMember();

    Members(now, sizeof now);
    std::fprintf(stderr, "coop-test: zone %s: members %s, zone %d holders %d at link tick %lld\n", what, now, zone, zone >= 0 ? ZoneHolders(zone) : -1,
                 (long long) NetplayLinkTick());
}

// One op; true when it is done (its effect then settles before the next).
bool RunOp(const std::string &op) {
    int  player = 0;
    char path[16] = {};
    int  chara = -1;

    if (std::sscanf(op.c_str(), "p%d:%15[a-z]:%d", &player, path, &chara) == 3) {
        std::fprintf(stderr, "coop-test: zone op %s: player %d's %s pick of %d (%s) at link tick %lld\n", op.c_str(), player, path, chara, Name(chara),
                     (long long) NetplayLinkTick());
        if (std::strcmp(path, "radial") == 0) {
            WheelConfirm(player - 1, chara);
        } else if (std::strcmp(path, "plain") == 0) {
            // coop-memberowner: a plain pick straight to the members' writer, past every screen's own offer (as a screen whose view
            // of the members lags would send it): the one refusal rule's own row.
            if (player == 1) {
                PromptChangePlayer(chara, MemberWhy::kPick);
            } else if (!SetPick(player - 1, chara, MemberWhy::kPick)) {
                MemberRefused(player - 1, chara, MemberWhy::kPick);
            }
        } else if (player == 1) {
            // The Characters page's pick, as BtMenuLoadChara takes it: asked of the co-op first, then the change.
            if (MenuCharaChange(chara) >= 0) {
                PromptChangePlayer(chara);
            }
        } else {
            FullMenuApplyPickForTest(player, chara);
        }
        return true;
    }
    // coop-swapcombo (A): nextfloor takes both players onto the dungeon's next floor (as a floor-start checkpoint's copy goes: the
    // dungeon's load, retail's reset of the game's player to Toan among it), every copy on this tick.
    // coop-memberowner: force:<z> runs the floor script's zone sequence for z now (as DC_COOP_TEST_ZONE_FORCE does at the load),
    // every copy on this tick: after a trade, so the game's player is on the floor's member while his own is another.
    if (std::sscanf(op.c_str(), "force:%d", &chara) == 1) {
        RunForce(chara);
        return true;
    }
    // coop-memberowner: lobby:<host> applies the lobby's pick for player 1 as its start does (CoopPartyLobbyPicks), every copy on
    // this tick (the question whether a lobby pick survives the dungeon's load). The real start applies it in a town, where the
    // game's body stays retail's until the dungeon's load; on this floor the body the floor loaded is put back at once (no model
    // of the pick is loaded here), so only the members' record is the lobby's: what the next floor's load then meets.
    if (std::sscanf(op.c_str(), "lobby:%d", &chara) == 1) {
        std::fprintf(stderr, "coop-test: zone op lobby: player 1's lobby pick %d at link tick %lld\n", chara, (long long) NetplayLinkTick());
        const int body = UserStatus->cur_chara;

        CoopPartyLobbyPicks(chara, -1, false);
        UserStatus->cur_chara = (s8) body;
        return true;
    }
    if (op == "nextfloor") {
        std::fprintf(stderr, "coop-test: zone op nextfloor: onto floor %d at link tick %lld\n", (int) UserStatus->cur_floor + 2, (long long) NetplayLinkTick());
        NetplayGoToFloor(selectMapNo, UserStatus->cur_floor + 1);
        return true;
    }
    // towncrash: townbody loads the floor partner's member as the town body (as a Norune visit leaves it), every copy on this tick;
    // the floor partner loads again over it on the next tick (coop_town.cpp TownBodyOnFloorForTest).
    if (op == "townbody") {
        const int chara = g_partner.loaded_chara;

        std::fprintf(stderr, "coop-test: zone op townbody: the town body as %d at link tick %lld\n", chara, (long long) NetplayLinkTick());
        TownBodyOnFloorForTest(chara);
        return true;
    }
    // towncrash: leave takes player 1 out of the dungeon by his floor menu's way out (Jep's rc3 run; as DC_WORLDS_TEST_FLOOR_LEAVE
    // does), every linked copy on this tick: the copies split, he to the dungeon's town, she stays on the floor.
    if (op == "leave1") {
        std::fprintf(stderr, "coop-test: zone op leave1: player 1 takes the floor's way out as %d at link tick %lld\n", (int) UserStatus->cur_chara,
                     (long long) NetplayLinkTick());
        CoopWorldsExitBy(1);
        gameTask = GAME_TASK_EXIT_FADE_START;
        return true;
    }
    // coop-swapcombo part 3: swing:<N> starts player N's attack on every copy on this tick (player 1's as his Cross does,
    // ToanKey_On for Toan; player 2's as her Cross does, StartAttack), so the next op asks mid-swing.
    if (std::sscanf(op.c_str(), "swing:%d", &player) == 1) {
        if (player == 1 && UserStatus->cur_chara == CHARA_TOAN) {
            ToanKey_On();
        } else if (player == 2) {
            StartAttack();
        }
        std::fprintf(stderr, "coop-test: zone op %s: player %d swings (action %d, hers %d) at link tick %lld\n", op.c_str(), player,
                     (int) BtActStatus.action_on, g_partner.attacking ? 1 : 0, (long long) NetplayLinkTick());
        return true;
    }
    long long at = 0;

    if (std::sscanf(op.c_str(), "at:%lld", &at) == 1) { // (wait until link tick at: the pads' presses and the menu come on that clock)
        return (long long) NetplayLinkTick() >= at;
    }
    if (std::sscanf(op.c_str(), "race:%d", &chara) == 1) {
        switch (g_test.race_stage) {
        case 0:
            g_test.race_x = g_partner.loaded_chara;
            g_test.race_y = chara;
            if (!WheelOpenForTest(0, g_test.race_x)) {
                std::fprintf(stderr, "coop-test: zone race: player 1's ring does not offer %d\n", g_test.race_x);
                return true;
            }
            std::fprintf(stderr, "coop-test: zone race: player 1's ring open on player 2's member %d at link tick %lld\n", g_test.race_x,
                         (long long) NetplayLinkTick());
            g_test.race_stage = 1;
            return false;
        case 1:
            SetPick(1, g_test.race_y, MemberWhy::kTest); // player 2 changes member while his ring is open
            g_test.race_stage = 2;
            return false;
        case 2:
            if (g_partner.loaded_chara != g_test.race_y) {
                return false;
            }
            std::fprintf(stderr, "coop-test: zone race: player 2 is now %d; player 1's ring offers %s; he confirms the %d it offered at link tick %lld\n",
                         g_test.race_y, WheelOffersForTest(0, g_test.race_x) ? "it still" : "it no more", g_test.race_x, (long long) NetplayLinkTick());
            WheelConfirm(0, g_test.race_x);
            g_test.race_stage = 0;
            return true;
        }
        return true;
    }
    if (std::sscanf(op.c_str(), "down:%d", &player) == 1) {
        TestDown(player, op.c_str());
        return true;
    }
    if (std::sscanf(op.c_str(), "downasked:%d:%d", &player, &chara) == 2) {
        g_test.down_who = player;
        g_test.down_after = chara;
        g_test.down_up = -1;
        return true;
    }
    if (op == "both") {
        std::fprintf(stderr, "coop-test: zone both: each player asks the other for their member on one tick at link tick %lld\n", (long long) NetplayLinkTick());
        AskSwap(0, g_partner.loaded_chara);
        AskSwap(1, UserStatus->cur_chara);
        return true;
    }
    if (op == "leave") {
        std::fprintf(stderr, "coop-test: zone leave: player 2 leaves the floor at link tick %lld\n", (long long) NetplayLinkTick());
        MenuLeaveFloor(true);
        return true;
    }
    std::fprintf(stderr, "coop-test: zone op %s not known\n", op.c_str());
    return true;
}

void StepTest() {
    static const int   force = TestEnvInt("DC_COOP_TEST_ZONE_FORCE", -1);
    static const char *picks = TestEnv("DC_COOP_TEST_ZONE_PICKS");

    if (g_test.done || (force < 0 && picks == nullptr) || UserStatus == nullptr || !NetplayStateFileLoaded() || g_partner.loaded_chara < 0 ||
        !CoopPlainPlay() || BtEventMode != 0) {
        return;
    }
    if (!g_test.armed) {
        g_test.armed = true;
        for (const char *at = picks != nullptr ? picks : ""; *at != 0;) {
            const char *end = std::strchr(at, ';');
            const size_t n = end != nullptr ? (size_t) (end - at) : std::strlen(at);

            if (n > 0) {
                g_test.ops.emplace_back(at, n);
            }
            at += n + (end != nullptr ? 1 : 0);
        }
    }
    // (coop-deadswap: downasked's down, counted from the question's first tick up, past the ops' settling)
    if (g_test.down_who != 0 && PromptSwapOpen() && ++g_test.down_up >= g_test.down_after) {
        const int who = g_test.down_who;

        g_test.down_who = 0;
        TestDown(who, "downasked");
    }
    if (!g_test.forced) {
        g_test.forced = true;
        if (force >= 0) {
            RunForce(force);
            g_test.waiting = true;
        }
        return;
    }
    if (g_test.waiting) {
        if (!Settled()) {
            return;
        }
        g_test.waiting = false;
        Report(g_test.next == 0 ? "after the force" : ("after " + g_test.ops[g_test.next - 1]).c_str());
    }
    if (g_test.next >= g_test.ops.size()) {
        g_test.done = true;
        Report("done");
        return;
    }
    if (RunOp(g_test.ops[g_test.next])) {
        g_test.next++;
        g_test.waiting = true;
    }
}

} // namespace

// coop-entrance: the zone test hook still has picks to make (DC_COOP_TEST_ZONE_FORCE / _PICKS set and not done): the descend
// driver's DC_COOP_DUNTEST_WAIT=ready waits for it, so a floor's gate comes after a trade (x-entrance -Case p1xiao).
bool ZoneTestPending() {
    static const bool armed = TestEnvInt("DC_COOP_TEST_ZONE_FORCE", -1) >= 0 || TestEnv("DC_COOP_TEST_ZONE_PICKS") != nullptr;

    return armed && !g_test.done;
}

bool ZoneTestAnswersYes() {
    static const bool yes = TestEnv("DC_COOP_TEST_ZONE_ANSWER") != nullptr && std::strcmp(TestEnv("DC_COOP_TEST_ZONE_ANSWER"), "yes") == 0;

    return yes;
}

// coop-forcefloor test hook (DC_WORLDS_TEST_ZONE_FLOOR=map:floor:z, coop_worlds' table reads it too): on this copy's first tick
// of play on that floor, the floor script's zone sequence for z runs (RunForce), as a fenced floor's start runs it.
static void TestFloorForce() {
    static int  map = -1, floor = -1, zone = -1;
    static bool read = false, done = false;

    if (!read) {
        read = true;
        if (const char *const spec = TestEnv("DC_WORLDS_TEST_ZONE_FLOOR"); spec != nullptr) {
            std::sscanf(spec, "%d:%d:%d", &map, &floor, &zone);
        }
    }
    if (done || map < 0 || selectMapNo != map || UserStatus->cur_floor != floor || mode != 3) {
        return;
    }
    static bool forced = false;
    // (retail's force is in the floor's start script, script 100, as it begins: F(z) then _SET_LIMMIT_ZONE(z), so the test's runs
    // there too, while a follower is still on its way (at first play it ran after her merge); at the first tick of play only
    // when the start script was missed)
    const bool start_script = BtEventMode != 0 && BtEventInfo.running_script_no == 100;

    if (!forced && !start_script && (!CoopPlainPlay() || BtEventMode != 0)) {
        return;
    }
    if (!forced) {
        forced = true;
        std::fprintf(stderr, "coop-test: forcefloor: on the test's fenced floor (map %d floor %d): its zone force runs (%s), player %d plays %d\n", map, floor,
                     start_script ? "the start script" : "first play", CoopWorldsApart() ? CoopWorldsSelf() : 1, (int) UserStatus->cur_chara);
        RunForce(zone);
        std::fprintf(stderr, "coop-test: forcefloor: after the force: this copy's player plays %d; the members' record p1 %d p2 %d at link tick %lld\n",
                     (int) UserStatus->cur_chara, Member(0), Member(1), (long long) NetplayLinkTick());
        return;
    }
    // (then once, linked together on it with the partner's body loaded: who plays what, both copies alike)
    if (!CoopWorldsApart() && g_partner.loaded_chara >= 0 && (g_partner.choice < 0 || g_partner.choice == g_partner.loaded_chara)) {
        done = true;
        std::fprintf(stderr, "coop-test: forcefloor: together on the fenced floor: p1 plays %d, p2 plays %d; the members' record p1 %d p2 %d at link tick %lld\n",
                     ActorChara(0), ActorChara(1), Member(0), Member(1), (long long) NetplayLinkTick());
    }
}

void ZoneStep() {
    if (!Enabled() || UserStatus == nullptr) {
        return;
    }
    LeaveFloorSwitch();
    StepTest();
}

// coop-forcefloor: the fenced test floor's force (DC_WORLDS_TEST_ZONE_FLOOR), each floor tick, apart too (retail's start script
// runs on a copy apart as well; ZoneStep is together's only).
void ZoneTestFloorStep() {
    if (Enabled() && UserStatus != nullptr) {
        TestFloorForce();
    }
}

} // namespace coop
