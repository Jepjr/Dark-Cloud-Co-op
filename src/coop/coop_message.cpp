#include "coop/game_ns.hpp"
#include "coop/coop_interact.hpp" // coop-pt1109c: PartnerPresenting
#include "coop/coop_menu.hpp"     // coop-pt1109c: MenuTaskNow
#include "coop/coop_fullmenu.hpp" // coop-pt1111f: FullMenuOpenFor
#include "coop/coop_message.hpp"
#include "coop/coop_town.hpp" // coop-pt1109e: CoopTownSceneWatchedTogether

#include <cstdint>
#include <cstdio>

#include "dngmessageman.hpp"
#include "itemdata.hpp" // coop-pt1111b: ITEM_WEAPON_START
#include "sysmes.hpp"
#include "dun/gameloop.hpp" // coop-pt1111h: UserStatus
#include "userstatus.hpp" // coop-pt1111h: the floor's zone

#include "coop/coop.hpp"
#include "coop/coop_core.hpp"
#include "coop/coop_inventory.hpp"
#include "coop/coop_worlds.hpp"
#include "coop/netplay.hpp"

// sysmes.cpp's system message state (repin: out of its anonymous namespace for coop).
namespace dc::game {
extern int SystemMesNo;
extern int SystemMesCount;
extern int SystemMesInputKey;
} // namespace dc::game

namespace coop {

namespace {

// The player the system message on show is for (0: the party's, drawn on both), and the item it names (-1: none).
struct Tagged {
    int          message = -1;
    int          owner = 1;
    int          item = -1;
    bool         scene = false; // coop-pt1109e: set in a scene both copies watch: its text is on both screens, whoever's Cross closes it
    bool         fades = false; // coop-closetitem: an Atla's name, shown to the other player for its time (kOthersTicks)
    std::int64_t set = 0;       //   the link tick it was set on
};

// coop-closetitem (Jep, 2026-10-31: "For the players who didn't open the atla, it should just fade away like all of the key
// items"): an Atla's name (AtraGetMes's message) waits for its opener's Cross; the other player's screen shows it too, for the
// time a key item's shows (the 0x96 ticks AtraGetMes is given), then not. Drawing only. DC_COOP_MUTATE=atlaname_others
// (twin): the other screen never shows it, as coop-itemprompt had it.
constexpr int kAtlaNameMes = 40;
constexpr int kOthersTicks = 0x96;
// coop-pt1111b: how long an item drop's "acquired" stays (BtGetAttach_Init: ItemGetMes(..., 0x78, 0)), the others' notices' time.
constexpr int kNoticeTicks = 0x78;

Tagged g_sys;
Tagged g_dng;              // DngMessMan's, as coop code last set it (message and its first inserted text)
int    g_dng_insert = -1;  //   that text: the game's own next message differs in one or the other
int    g_next_by = 0;      // MessageBy's player for the next SetSystemMes (0: none)
int    g_next_item = -1;   // ItemGetMes's item for it
int    g_pickups[8] = {-1, -1, -1, -1, -1, -1, -1, -1}; // her raised pickup events not yet shown
int    g_pickup_next = 0;

// A message the game sets by itself: player 1's, or hers where everything said is said to her (her town event, her own
// world apart).
int GameOwner() {
    return CoopWorldsPartnerSpeaks() ? 2 : 1;
}

// coop-pt1111b (Jep 11-11, item 20: "the other player sees a pickup notice ONLY for important items; ordinary drops/consumables
// show on the picker's screen only"): the kinds of thing whose "acquired" the other players see too. One list, to change as Jep
// decides (his default, 11-11: Atla finds, key items with the party's Keys tab, story items, weapons). An Atla find is not an item:
// its notice is OthersAtlaNotice's, asked by its kind alone.
enum class Notice { AtlaFind, KeyItem, StoryItem, Weapon, Ordinary };
constexpr Notice kOthersSee[] = {Notice::AtlaFind, Notice::KeyItem, Notice::StoryItem, Notice::Weapon};

bool OthersSee(Notice kind) {
    for (Notice seen : kOthersSee) {
        if (seen == kind) {
            return true;
        }
    }
    return false;
}

Notice NoticeOf(int item) {
    if (item >= ITEM_WEAPON_START) {
        return Notice::Weapon;
    }
    if (FloorKeyItem(item) || RingItem(item)) {
        return Notice::KeyItem; // a floor's key, a gate key, the party's Keys tab
    }
    if (SharedItem(item)) {
        return Notice::StoryItem; // the quest and story items, the map and crystal
    }
    return Notice::Ordinary;
}

// DC_COOP_MUTATE=meskeyown: a key item's message is its getter's alone (the shared check's twin).
bool KeyMessage(int item) {
    return item > 0 && OthersSee(NoticeOf(item)) && !Mutated("meskeyown");
}

bool Shown(const Tagged &tag) {
    const int local = NetplayLocalPlayer();

    const bool others = tag.fades && NetplayLinkTick() - tag.set < kOthersTicks && !Mutated("atlaname_others");

    return local == 0 || tag.owner == 0 || tag.owner == local || others || (tag.scene && !Mutated("scene_mes_owner")) || CoopWorldsApart() || Mutated("mesboth");
}

// DC_COOP_DEBUG: each message this copy starts drawing or hiding, once (the itemprompt test's lines).
void Trace(const char *kind, const Tagged &tag, bool shown, int *was_message, bool *was_shown) {
    static const bool debug = CoopDebug();

    if (!debug || (tag.message == *was_message && shown == *was_shown)) {
        return;
    }

    *was_message = tag.message;
    *was_shown = shown;

    if (tag.message > 0) {
        std::fprintf(stderr, "coop-mes: p%d %s %s message %d item %d for %s\n", NetplayLocalPlayer(), shown ? "draws" : "hides", kind, tag.message,
                     tag.item, tag.owner == 0 ? "both" : tag.owner == 1 ? "p1" : "p2");
    }
}

} // namespace

MessageBy::MessageBy(int player) : was_(g_next_by) {
    g_next_by = player;
}

MessageBy::~MessageBy() {
    g_next_by = was_;
}

void SystemMesItem(int item) {
    g_next_item = item;
}

static int MesOwner(); // (below)

void OthersKeyItemMes(int player, int item, int volume) {
    if (!KeyMessage(item) || Mutated("keymes_opener_only")) {
        return;
    }
    {
        MessageBy by(player);
        ItemGetMes(item, volume != 0 ? volume : -1, kNoticeTicks, 0); // (coop-pt1111b: an item drop's time, as an Atla's notice)
    }
    g_sys.fades = true;
    std::fprintf(stderr, "coop-mes: p%d shows player %d's key item %d from his presentation, fading\n", NetplayLocalPlayer(), player, item);
}

// coop-pt1111b (Jep's 11-11 playtest, items 10 and 10b; his choice B): the opener's Atla name is retail's, on the opener's screen
// until the opener's own Cross; every other player gets a notice of their own, on their own screen, for as long as an item drop's
// notice stays (BtGetAttach_Init: ItemGetMes(..., 0x78, 0)), and it goes by itself. It was the opener's message set on the other
// copies too (MessageBy(opener)), waiting for the opener's Cross there and drawn there only for its first 0x96 ticks (fades), so
// it held the others' message slot until the opener confirmed. A message this copy's own player must answer (one waiting for a
// key) is never taken for it. DC_COOP_MUTATE=atlanotice_waits (twin): the opener's waiting message, as before.
void OthersAtlaNotice(int opener, int map_no, int atla) {
    const int local = NetplayLocalPlayer();

    if (!OthersSee(Notice::AtlaFind)) {
        return;
    }
    if (Mutated("atlanotice_waits")) {
        MessageBy by(opener);
        AtraGetMes(map_no, atla, 0x28);
        return;
    }
    if (SystemMesNo > 0 && SystemMesInputKey != 0 && MesOwner() == local) {
        std::fprintf(stderr, "coop-mes: p%d keeps its own message %d up: player %d's Atla %d not noticed here\n", local, SystemMesNo, opener, atla);
        return;
    }
    {
        MessageBy by(local);
        AtraGetMes(map_no, atla, kNoticeTicks);
    }
    SystemMesInputKey = 0; // (AtraGetMes always waits for a key: this one goes in its time)
    g_sys.fades = false;
    std::fprintf(stderr, "coop-mes: p%d notices player %d's Atla %d on its own screen for %d ticks from link tick %lld\n", local, opener, atla, kNoticeTicks,
                 (long long) NetplayLinkTick());
}

void SystemMesSet(int message_no) {
    int owner = g_next_by != 0 ? g_next_by : GameOwner();

    // Her pickup of an attachment or story item: the game's tick shows it as the player's.
    for (int &pending : g_pickups) {
        if (g_next_by == 0 && g_next_item > 0 && pending == g_next_item) {
            pending = -1;
            owner = 2;
            break;
        }
    }

    g_sys.message = message_no;
    g_sys.item = g_next_item;
    g_sys.owner = KeyMessage(g_next_item) ? 0 : owner;
    g_sys.fades = message_no == kAtlaNameMes;
    // coop-pt1109e (Jep's 11-09 playtest, 1l: the Stone Breaker's "acquired" on one screen of a scene both watched): a scene is one show, so what it says is said on both.
    // DC_COOP_MUTATE=scene_mes_owner (twin): its owner's screen alone.
    g_sys.scene = message_no > 0 && CoopTownSceneWatchedTogether();
    g_sys.set = NetplayLinkTick();
    g_next_item = -1;

    if (message_no > 0 && SystemMesInputKey != 0 && (g_sys.owner == 0 ? GameOwner() : g_sys.owner) == 2) {
        NetplayMesPressArm(); // coop-closetitem test hook
    }

    static const bool debug = CoopDebug();

    if (debug && message_no > 0) {
        std::fprintf(stderr, "coop-mes: system message %d item %d for %s scene %d\n", message_no, g_sys.item,
                     g_sys.owner == 0 ? "both" : g_sys.owner == 1 ? "p1" : "p2", g_sys.scene ? 1 : 0); // (coop-pt1111b: scene)
    }
}

// coop-closetitem (Jep's 2026-10-30 playtest, both in the mayor's house: while her closet's '"Prickly" acquired.' box was up,
// P1's Cross closed it on her screen and her event stayed waiting): a system message that waits for a button
// (SystemMesStep's input_key) waited for the game pad's Cross, which the room's step reads as player 1's, while her event's
// own wait reads her pad (her room event's pad, swapped in around its step alone). The message waits for its owner's Cross:
// the agreed pad of the player it is for (a message for both, a key item's: the game's owner now, hers through her event),
// pressed this tick and not the tick before. Unlinked or apart, the game pad's, as before. Both copies read the same agreed
// pads, so the message closes on one tick on both. DC_COOP_MUTATE=sysmes_gamepad (twin): the game pad's Cross, as before.
// coop-atlacross: the press is the player's own on the agreed pads (NetplayPlayerPressed: the link's one record of what each
// player pressed this tick, whoever holds the menus), where this read the game's routed ports through a tracker of its own.
// The player a system message is for (its own tag, else the game's owner now), or 0 unlinked or apart (the game pad's).
static int MesOwner() {
    if (NetplayLocalPlayer() == 0 || CoopWorldsApart()) {
        return 0;
    }
    return g_sys.owner != 0 && SystemMesNo == g_sys.message ? g_sys.owner : GameOwner();
}

bool SystemMesCross(bool game_pad) {
    const std::int64_t now = NetplayLinkTick();
    const int          owner = MesOwner();

    if (owner == 0) {
        return game_pad;
    }

    const bool pressed = NetplayPlayerPressed(owner == 2 ? 2 : 1, kInputCross);
    const bool closes = Mutated("sysmes_gamepad") ? game_pad : pressed;

    // (DC_COOP_DEBUG: each Cross of either kind on a message that waits for one, and what it did; the closetmes test's lines)
    static const bool debug = CoopDebug();

    if (debug && SystemMesNo > 0 && SystemMesInputKey != 0 && SystemMesCount == 2 && (pressed || game_pad)) {
        std::fprintf(stderr, "coop-mes: p%d system message %d for p%d: %s's Cross at link tick %lld %s\n", NetplayLocalPlayer(), SystemMesNo, owner,
                     pressed ? "its owner" : "the game pad", (long long) now, closes ? "closes it" : "leaves it up");
    }

    // (and when a message that waited for a button goes, by this step or by its script (_INIT_SYS_MES on the event's own Cross),
    // whether its owner pressed Cross on that tick)
    static int up_was = -1, owner_was = 0;
    const int  up = SystemMesNo > 0 && SystemMesInputKey != 0 ? SystemMesNo : -1;

    if (debug && up_was > 0 && up != up_was) {
        std::fprintf(stderr, "coop-mes: p%d system message %d for p%d gone at link tick %lld, its owner's Cross %s\n", NetplayLocalPlayer(), up_was, owner_was,
                     (long long) now, NetplayPlayerPressed(owner_was == 2 ? 2 : 1, kInputCross) ? "pressed on it" : "not pressed");
    }
    up_was = up;
    owner_was = owner;

    // coop-pt1111b (DC_COOP_DEBUG): any message's end on this copy, a timed one's too, and whether it waited for a key (the rows'
    // line: an Atla's notice on the others' screens goes by itself, the opener's on the opener's Cross)
    static int any_was = -1, key_was = 0, for_was = 0;
    COOP_TEST_TRACE_RESET(any_was, -1); // (put back at each warm-pair job, as a fresh copy has it)
    if (debug && any_was > 0 && SystemMesNo != any_was) {
        std::fprintf(stderr, "coop-mes: p%d message %d for p%d ended at link tick %lld, it waited for a key %d\n", NetplayLocalPlayer(), any_was, for_was,
                     (long long) now, key_was);
    }
    any_was = SystemMesNo;
    key_was = SystemMesInputKey != 0 ? 1 : 0;
    for_was = SystemMesNo > 0 ? (g_sys.owner != 0 && SystemMesNo == g_sys.message ? g_sys.owner : 0) : 0;

    return closes;
}

int SystemMesFor() {
    return SystemMesNo > 0 ? MesOwner() : 0;
}

bool SystemMesShown() {
    static int  was_message = -1;
    static bool was_shown = true;
    // (coop-warmreset, WS7: put back at each warm-pair job as a fresh copy has it; trace_once_kept twin: kept)
    COOP_TEST_TRACE_RESET(was_message, -1);
    COOP_TEST_TRACE_RESET(was_shown, true);

    if (SystemMesNo <= 0) {
        was_message = -1;
        return true;
    }

    if (SystemMesNo != g_sys.message) { // set by a path that does not tag (ClearSystemMes and the like): the game's
        g_sys.message = SystemMesNo;
        g_sys.item = -1;
        g_sys.owner = GameOwner();
        g_sys.fades = false;
    }

    const bool shown = Shown(g_sys);

    Trace("system", g_sys, shown, &was_message, &was_shown);
    return shown;
}

void DngMesBy(int player) {
    g_dng.message = DngMessMan.message;
    g_dng.owner = player;
    g_dng.item = -1;
    g_dng_insert = DngMessMan.insert_mes_1;

    static const bool debug = CoopDebug();

    if (debug) {
        std::fprintf(stderr, "coop-mes: dungeon message %d for p%d\n", g_dng.message, player);
    }
}

namespace {
bool HerDryShown();
bool HerCopyTogether();
bool DryMessage(int message);

// coop-pt1111h (Jep's 2026-11-11 playtest, choice A: "Limited zone / Cannot disarm weapon / Abs decreased." on every
// player's screen): the floor's rule notices are CDngMessageMan::LimmitZone's, raised as the floor starts (movechara.cpp,
// BT_REQUEST_GO_DUNGEON) and the only setter of these numbers in the port and the mod: 0xB the one member who may fight (the
// fighter's name inserted, the same text for every player), 0xD no weapon change, 0xE thirst five times as fast. The rule is
// the floor's, so every player on it: the game's own messages otherwise went to GameOwner (player 1's), so player 2's copy
// never drew it. Apart, each world's messages are its own already (Shown). DC_COOP_MUTATE=floormes_p1 (twin): player 1's,
// as before.
bool FloorRuleMessage(int message) {
    return (message == 0xB || message == 0xD || message == 0xE) && !Mutated("floormes_p1");
}
} // namespace

// DngMessMan's message as tagged: whether this copy's player may see it.
bool DngMesOwnShown() {
    static int  was_message = -1;
    static bool was_shown = true;
    // (coop-warmreset, WS7: put back at each warm-pair job as a fresh copy has it; trace_once_kept twin: kept)
    COOP_TEST_TRACE_RESET(was_message, -1);
    COOP_TEST_TRACE_RESET(was_shown, true);

    if (DngMessMan.message < 0) {
        g_dng.message = -1;
        was_message = -1;
        return true;
    }

    Tagged now = g_dng;

    if (DngMessMan.message != g_dng.message || DngMessMan.insert_mes_1 != g_dng_insert) { // the game's own
        now.message = DngMessMan.message;
        now.owner = FloorRuleMessage(now.message) ? 0 : GameOwner(); // coop-pt1111h: the floor's rule is every player's
    }

    const bool shown = Shown(now);

    Trace("dungeon", now, shown, &was_message, &was_shown);

    // coop-p2hurt: his thirst's messages are not hers.
    return shown && !(HerCopyTogether() && now.owner == 1 && DryMessage(now.message));
}

// coop-pt1111f (Jep's 2026-11-11 playtest item 11: the "Limited zone / Cannot disarm weapon" box drawn under the open main
// menu): retail's menu opening (movechara.cpp, GAME_TASK_MENU_INIT's task before it) drops the dungeon's message
// (DngMessMan.enabled false, message -1, timer 0, ClearSystemMes) and captures its background three frames on, so no box is
// under its menu; the thirst messages come back after it (SetStatus_Dry raises them again while the water stays out). Local
// menus leave the world and its message as they are, on both copies: so on the copy whose own menu is open the box is not
// drawn, and the message it opened over (not a thirst one) stays undrawn there after the close, as retail's was dropped.
// Draw only: the message is the world's, the other copy's screen keeps it. DC_COOP_MUTATE=dngmes_under_menu (twin): drawn
// under the menu, as before.
struct UnderMenu {
    int message = -1; // the message this copy's menu opened over
    int timer = 0;    // its timer then (the same instance while it counts down from there)
    bool closed = false; // its menu closed (said once)
};

UnderMenu g_under_menu;

bool HiddenByOwnMenu() {
    const int local = NetplayLocalPlayer();

    if (local <= 0 || Mutated("dngmes_under_menu")) {
        return false;
    }
    if (FullMenuOpenFor(local)) {
        if (g_under_menu.message < 0 && DngMessMan.message >= 0 && !DryMessage(DngMessMan.message)) {
            g_under_menu = UnderMenu{DngMessMan.message, DngMessMan.timer, false};
            std::fprintf(stderr, "coop-dngmes: player %d's menu opens over the dungeon's message %d: not drawn on this copy (timer %d)\n", local,
                         DngMessMan.message, DngMessMan.timer);
            std::fflush(stderr);
        }
        return true;
    }
    if (g_under_menu.message >= 0 &&
        (DngMessMan.message != g_under_menu.message || DngMessMan.timer <= 0 || DngMessMan.timer > g_under_menu.timer)) {
        g_under_menu = UnderMenu{}; // another message (or this one raised again): drawn
    }
    if (g_under_menu.message >= 0 && !g_under_menu.closed) {
        g_under_menu.closed = true;
        std::fprintf(stderr, "coop-dngmes: player %d's menu closed: the dungeon's message %d stays undrawn on this copy (timer %d), as retail drops it\n", local,
                     DngMessMan.message, DngMessMan.timer);
        std::fflush(stderr);
    }
    return g_under_menu.message >= 0;
}

// coop-pt1111h (Jep's 11-11 choice A, its arrival part: the floor's rule told to a player who comes onto the floor any way but
// its walk-in): retail tells the floor's rule as the floor starts (BT_REQUEST_GO_DUNGEON: CDngMessageMan::LimmitZone, its
// notice up 0xF0 frames). A player who arrives by a merge or a join comes in behind the arrival's black (NetplayArrivalBlack:
// its own walk-in's notice, if it had one, was drawn under it) and the state set lands the stayer's message slot, so the
// arriver was never told. Each copy keeps whether its own player has seen the rule of the floor it stands on (the game's notice
// drawn here outside the black); a player on a zone floor not told yet gets the notice the floor's start gives, from P1's
// routine itself (LimmitZone run on a copy of the message state: the same message and fighter's name), on its own screen alone
// for the routine's time. Draw only: the shared message slot (the stayer's message) is left as it is, so the stayer's screen
// changes in nothing (merge-no-wait), and each copy does it for its own player (any number of players). Its own menu drops it,
// as retail's menu drops the floor's message. DC_COOP_MUTATE=arrival_no_floornotice (twin): never, as before.
namespace {

struct ArrivalNotice {
    int  key = -1;      // the floor this copy's player stands on (dungeon << 8 | floor), -1 none
    bool told = false;  // that floor's rule has been on this copy's screen
    int  message = -1;  // this copy's own notice up now, -1 none
    int  insert = 0;    //   the name it inserts
    int  frames = 0;    //   its frames left
};

ArrivalNotice g_arrival;

bool ZoneMessage(int message) {
    return message == 0xB || message == 0xD || message == 0xE;
}

void ArrivalNoticeStep() {
    const int local = NetplayLocalPlayer();
    const int floor = CoopWorldsHereFloor();
    const int key = UserStatus != nullptr && floor >= 0 ? (UserStatus->cur_georama << 8 | floor) : -1;

    if (key != g_arrival.key) { // another floor (or none): nothing told on it yet
        g_arrival = ArrivalNotice{};
        g_arrival.key = key;
    }
    if (g_arrival.message >= 0 && (--g_arrival.frames <= 0 || (local > 0 && FullMenuOpenFor(local)))) {
        std::fprintf(stderr, "coop-dngmes: player %d's own floor notice %d %s\n", local, g_arrival.message, g_arrival.frames <= 0 ? "ran its time" : "dropped by the player's menu");
        g_arrival.message = -1;
    }
    if (key < 0 || g_arrival.told || UserStatus->res_limit_zone_current == RES_LIMIT_ZONE_NONE || !DngMessMan.enabled || NetplayArrivalBlack() ||
        (NetplayCatchingUp() && !Mutated("arrival_replay_told"))) { // (a catch-up's replayed frames and the arrival's black: nothing
        // shown to the player; DC_COOP_MUTATE=arrival_replay_told (twin): the replayed frames count, so a joiner is never told)
        return;
    }
    if (DngMessMan.timer > 0 && ZoneMessage(DngMessMan.message) && DngMesOwnShown()) { // the floor's walk-in told it, here
        g_arrival.told = true;
        std::fprintf(stderr, "coop-dngmes: player %d sees the floor's notice %d (the floor's start, zone %d)\n", local, (int) DngMessMan.message,
                     (int) UserStatus->res_limit_zone_current);
        return;
    }
    if (Mutated("arrival_no_floornotice")) {
        return;
    }
    CDngMessageMan routine = DngMessMan; // P1's routine, on a copy: the game's slot stays the stayer's
    routine.LimmitZone();
    g_arrival.told = true;
    g_arrival.message = routine.message;
    g_arrival.insert = routine.insert_mes_1;
    g_arrival.frames = routine.timer;
    std::fprintf(stderr, "coop-dngmes: player %d is on the floor's rule untold (zone %d): its notice %d on this copy alone, %d frames\n", local,
                 (int) UserStatus->res_limit_zone_current, g_arrival.message, g_arrival.frames);
}

} // namespace

bool DngMesShown() {
    return !HiddenByOwnMenu() && (g_arrival.message >= 0 || HerDryShown() || DngMesOwnShown());
}

int DngMesInsert1() {
    return g_arrival.message >= 0 ? g_arrival.insert : DngMessMan.insert_mes_1;
}

void PartnerPickupRaised(int item) {
    g_pickups[g_pickup_next] = item;
    g_pickup_next = (g_pickup_next + 1) % 8;
}

namespace {

// coop-p2hurt (Jep's 2026-10-28 playtest: "P2 doesnt have the thirst has reached its limit message"): the game's thirst
// messages (CDngMessageMan::SetStatus_Dry: 0xAA water low, 0xAB thirst at its limit) live in DngMessMan's one slot and
// follow the controlled character's water, player 1's. Hers follow her own member's water, as SetStatus_Dry runs for
// his, in a slot of their own; stepped each tick (StepThirst, both copies alike), drawn on her copy only, where his
// thirst messages are not drawn. DC_COOP_MUTATE=drymes (twin): none of hers, his on both, as before.
constexpr int kDryLow = 0xAA;
constexpr int kDryLimit = 0xAB;

struct Dry {
    int   chara = -1;
    int   message = -1;
    int   timer = 0;
    float before = 0.0f;
};

Dry g_dry;

bool DryMessage(int message) {
    return message == kDryLow || message == kDryLimit;
}

// Her copy, linked and together (apart, her world's own messages are hers already).
bool HerCopyTogether() {
    return NetplayLocalPlayer() == 2 && !CoopWorldsApart() && !Mutated("drymes");
}

// coop-pt1109c (Jep's 11-09 playtest 1i: "thirst limit" over the big chest's screen): retail shuts his message window through his own presentation
// (DngMessMan.enabled = false in its Init); hers had no such close through hers, and came back when it ends. DC_COOP_MUTATE=dry_over_her_chest (twin): over it, as before.
bool HerDryShown() {
    return HerCopyTogether() && g_dry.message != -1 && (!PartnerPresenting() || Mutated("dry_over_her_chest"));
}

} // namespace

bool PresentationTask(); // coop_pause.cpp

// coop-pt1109c (Jep's 11-09 playtest 1j: P1's Atla took her thirst message off her screen): DngMessMan.enabled is the game's, and the presentation's Init
// shuts it on both copies. That is for the opener's screen: on hers, her own thirst's message stays through his presentation. Anything else that shuts it (a
// scene both watch) shuts hers too. DC_COOP_MUTATE=dry_his_shuts (twin): his shuts hers, as before.
bool DngMesEnabledHere() {
    ArrivalNoticeStep(); // coop-pt1111h: once a drawn frame, before this frame's draw asks
    const bool on = DngMessMan.enabled || (HerDryShown() && PresentationTask() && !Mutated("dry_his_shuts"));

    // Test hook (x-atla-beside): what this copy's draw would do, every 20th draw.
    if (static const bool test = TestEnv("DC_COOP_TEST_DRY") != nullptr; test) {
        static int wait = 0;

        if (--wait <= 0) {
            wait = 20;
            const int  drawn = on && DngMesShown() ? DngMesDrawn() : -1;
            std::fprintf(stderr, "coop-drymes: p%d clock %lld task %d presenting %d shown %d message %d\n", NetplayLocalPlayer(), (long long) NetplayTestPressClock(), MenuTaskNow(),
                         PartnerPresenting() ? 1 : 0, drawn == kDryLimit || drawn == kDryLow ? 1 : 0, drawn);
        }
    }
    return on;
}

void PartnerDryStep(int chara, float water_max, float water_now) {
    if (chara != g_dry.chara) {
        g_dry = Dry{};
        g_dry.chara = chara;
        g_dry.before = water_now;
    }

    float before = g_dry.before;
    float threshold = 0.1f * water_max;

    g_dry.before = water_now;

    if (g_dry.timer > 0 && --g_dry.timer == 0) {
        g_dry.message = -1;
    }

    if (threshold < water_now && g_dry.message == kDryLow) {
        g_dry.message = -1;
    }

    if (0.0f < water_now && g_dry.message == kDryLimit) {
        g_dry.message = -1;
    }

    if (threshold <= before + 0.5f && !(threshold <= water_now - 0.5f) && g_dry.message == -1) {
        g_dry.message = kDryLow;
        g_dry.timer = 0xF0;
    }

    if (water_now <= 0.0f && (g_dry.message == -1 || g_dry.message == kDryLimit)) {
        if (g_dry.message != kDryLimit) {
            std::fprintf(stderr, "coop-drymes: member %d's thirst at its limit (her message %#x)\n", chara, kDryLimit);
        }

        g_dry.message = kDryLimit;
        g_dry.timer = 0x9FFF6;
    }
}

int DngMesDrawn() {
    int drawn = g_arrival.message >= 0 ? g_arrival.message // coop-pt1111h: this copy's own floor notice
                : HerDryShown() && (DngMessMan.message < 0 || DryMessage(DngMessMan.message) || !DngMesOwnShown()) ? g_dry.message
                                                                                                             : DngMessMan.message;

    // Test hook (x-p2-hurt -Case thirst): what the dungeon's message draw shows, every 30th draw.
    if (static int wait = 0; TestEnv("DC_COOP_TEST_P2HURT") != nullptr && --wait <= 0) {
        wait = 30;
        std::fprintf(stderr, "coop-drymes: p%d draws dungeon message %d (the game's %d, hers %d)\n", NetplayLocalPlayer(), drawn,
                     DngMessMan.message, g_dry.message);
    }

    return drawn;
}

int PartnerDryMessage() {
    return g_dry.message;
}

// coop-warmreset (WS7): the system message's owner tag and her thirst slot as a fresh copy has them (a job's thirst at its limit said
// "thirst at its limit" only on entering it, so a job after one at the limit never said it; both copies reset alike).
void MessageResetForJob() {
    g_sys = Tagged{};
    g_dry = Dry{};
    g_arrival = ArrivalNotice{}; // coop-pt1111h
}

int MessageJobDry() {
    return g_dry.message;
}

} // namespace coop

// ps2/src/sysmes.cpp's (recipe 0068), which reaches no port header.
void CoopSystemMesItem(int item) {
    coop::SystemMesItem(item);
}

void CoopSystemMesSet(int message_no) {
    coop::SystemMesSet(message_no);
}

bool CoopSystemMesShown() {
    return coop::SystemMesShown();
}

bool CoopSystemMesCross(bool game_pad) {
    return coop::SystemMesCross(game_pad);
}

namespace coop {
// coop-eventslots: the system message's tag (whose it is) as a range of each player's own message slot (coop_eventslots.cpp),
// its reset for a fresh slot, and the owner the next SetSystemMes will be tagged with (0: the party's, both screens).
void MessageTagRanges(void (*add)(void *at, std::size_t bytes, const char *name, void *context), void *context) {
    add(&g_sys, sizeof g_sys, "coop g_sys", context);
}

void MessageTagClear() {
    g_sys = Tagged{};
}

int MessageOwnerNext() {
    int owner = g_next_by != 0 ? g_next_by : GameOwner();

    for (const int pending : g_pickups) {
        if (g_next_by == 0 && g_next_item > 0 && pending == g_next_item) {
            owner = 2;
            break;
        }
    }
    return KeyMessage(g_next_item) ? 0 : owner;
}
} // namespace coop
