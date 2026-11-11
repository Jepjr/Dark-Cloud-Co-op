// coop-scriptmenus (option 3 of debug\notes\shared-state-review.md, 2026-11-09): the menus a running event's script opens (Use Item,
// the bait's Use Item, names, the world map, the fishing exchange and records) run on the opener's copy alone.
//
// Retail opens all of them through one block in the event's step (editloop3.cpp EdEventMode: the script sets menu_mode and waits on
// it, EdInitMenu, four frames of EdInitModeFinish, then EdMenuMode each tick till it finishes, its result written into the script's
// stack) and one switch (edit.cpp EdInitMenu, EdInitModeFinish, EdMenuMode). Linked, that block ran on every copy in the lockstep: the
// menu stepped on the copy of a player who never opened it (out of sight since coop-pt1108f), its pad mode and textures on both.
//
// Now, linked with local menus on, the copy of the player whose pad the event reads (the opener: a room or town event of a player
// reads that player's pad, anything else the game's, player 1's) makes the menu (EdInitModeFinish) and steps it (EdMenuMode) on a pad of its own
// fed from that player's agreed pad, under the menu's write scope (its party status, stock, flag, name and fishing writes as pokes,
// FullMenuWritesBegin), its random numbers its copy's own (DrawRng) and its sounds its player's. Every other copy counts the same four
// opening frames and then waits in the script's same wait. As the menu finishes on the opener's copy, its result goes out as one
// command, kCmdScriptMenuEnd (a = the menu kind, c = the result: the item for Use Item, the map for the world map), applied on every
// copy on one tick, after the menu's pokes: there the script gets its result and goes on, as retail's close. Nobody else's play
// waits on it (the others walk on through another's own event as before; in an event they share they are in its wait anyway).
// Apart, unlinked or with menus run in the lockstep (the class's fix-off), retail's block as it was.
// DC_COOP_MUTATE=scriptmenu_lockstep (twin): retail's block on every copy, as before coop-pt1108f.

#include "coop/game_ns.hpp"
#include <cstdint>
#include <cstdio>
#include <optional>

#include "battlemenu.hpp"
#include "editloop.hpp"
#include "edit.hpp"
#include "mglib.hpp"
#include "savedata.hpp"
#include "gamepad.hpp"
#include "gamepad.hpp"
#include "platform/input.hpp"
#include "texture.hpp"

#include "coop/coop.hpp"
#include "coop/coop_audio.hpp"
#include "coop/coop_camera.hpp" // DrawRng
#include "coop/coop_core.hpp"
#include "coop/coop_fullmenu.hpp"
#include "coop/coop_town.hpp"
#include "coop/coop_townmenu.hpp"
#include "coop/coop_worlds.hpp"
#include "coop/coop_writeguard.hpp"
#include "coop/netplay.hpp"
#include "coop/netplay_cmd.hpp"

using namespace coop;

namespace dc::game {
extern int MenuMapJumpMode;
int        EdInitModeFinish(CCamera *camera, CTexture *texture); // (ps2/src/edit.cpp)
int        EdMenuMode();
void       EdExitMenu();
void       EdDrawOffAll(); // (ps2/src/editloop.cpp)
void       EdDrawOnAll();
int        EdGetUseItem();
void       MGFlipWaitVSync(int wait);
} // namespace dc::game

namespace {

// The menu kinds (ED_MENU_*, edit.hpp) whose result the script reads.
constexpr int kKindUseItem = ED_MENU_USE_ITEM;
constexpr int kKindBait = ED_MENU_CHANGE_ESA;
constexpr int kKindWorldMap = ED_MENU_WORLD_MAP;

struct ScriptMenu {
    bool          on = false;     // a script's menu is open (every copy: from its opening to the tick its end applies)
    bool          local = false;  // it runs on the opener's copy alone (decided at its opening, every copy alike)
    int           owner = 0;      // the opener (1 up)
    int           kind = 0;       // ED_MENU_*
    int           init_ticks = 0; // the opening's frames counted (a copy that does not make it)
    bool          sent = false;   // the opener's copy: finished, its end command sent
    bool          landed = false; // every copy: its end applied this tick
    int           result = -1;    // its result as the command carried it
    long long     opened = -1;    // the link tick it opened on
    int           steps = 0;      // the opener's copy: ticks it stepped
    bool          made = false;   // its opening's frames done (retail's EdInitModeFinish answered, or the four counted)
    bool          stray_said = false; // coop-small2: a retail step on a copy that never opened it, said once
    bool          carried = false;    // coop-small2: brought by a merge's state set, before the link it came with is up
    CGamePad      pad{};          // the opener's copy: the menu's own pad (its menu mode and repeats never in the game's)
    InputPadState before{};
} g_menu;

bool OwnerHere() {
    return g_menu.local && NetplayLocalPlayer() == g_menu.owner;
}

// The opener's agreed pad into the menu's own pad.
void FeedPad() {
    InputPadState now{};

    if (NetplayMenuPad(g_menu.owner, now)) {
        PadUpdateFrom(g_menu.pad, now);
        g_menu.before = now;
    }
}

struct PadIn {
    PadIn() { std::swap(GamePad, g_menu.pad); }
    ~PadIn() { std::swap(GamePad, g_menu.pad); }
    PadIn(const PadIn &) = delete;
    PadIn &operator=(const PadIn &) = delete;
};

const char *KindName(int kind) {
    switch (kind) {
        case ED_MENU_USE_ITEM:
            return "Use Item";
        case ED_MENU_CHANGE_ESA:
            return "the bait's Use Item";
        case ED_MENU_NAME_REGISTRY:
            return "names";
        case ED_MENU_WORLD_MAP:
            return "the world map";
        case ED_MENU_FISHING_EXCHANGE:
            return "the fishing exchange";
        case ED_MENU_FISH_RANKING:
            return "the fish records";
    }
    return "a menu";
}

// Every copy, the command's tick: the opener's menu ended with its result; the script takes it on this tick's step.
void ApplyEnd(int player, const NetCommand &command, std::int64_t tick) {
    if (!g_menu.on || !g_menu.local || g_menu.landed || player != g_menu.owner || command.a != g_menu.kind) {
        std::fprintf(stderr, "coop-scriptmenu: player %d's script menu end (kind %d) at link tick %lld finds none waiting for it: ignored\n", player, (int) command.a,
                     (long long) tick);
        return;
    }
    g_menu.landed = true;
    g_menu.result = (int) (std::int32_t) command.c;
    std::fprintf(stderr, "coop-scriptmenu: player %d's %s ends with %d on every copy at link tick %lld (%lld ticks open)\n", player, KindName(g_menu.kind),
                 g_menu.result, (long long) tick, (long long) (tick - g_menu.opened));
    if (g_menu.kind == ED_MENU_FISHING_EXCHANGE && SaveData != nullptr) {
        std::fprintf(stderr, "coop-scriptmenu: the fishing points now %d\n", (int) SaveData->GetFishingPoint()); // (the rows' line, every copy)
    }
    std::fflush(stderr);
}

} // namespace

void CoopScriptMenuCommandHandler() {
    NetplaySetCommandHandler(kCmdScriptMenuEnd, ApplyEnd);
}

// Every copy, as the script asks for its menu (before EdInitMenu).
void CoopScriptMenuOpens(int kind) {
    g_menu = ScriptMenu{};
    g_menu.on = true;
    g_menu.kind = kind;
    g_menu.owner = CoopTownEventPadPlayer();
    // (linked: together, or a catch-up's ticks both copies step alike; a room's scene too, where the town's menu test wants her pose)
    g_menu.local = (CoopWorldsLinkedTogether() || CoopTownMenuLinked()) && FullMenuLocalOn() && NetplayHasPlayer() && !Mutated("scriptmenu_lockstep");
    g_menu.opened = NetplayLinkTick();
    if (OwnerHere()) {
        FeedPad();
        FeedPad(); // (its first step reads no press as new: the one that answered the script is held, not pressed)
    }
    std::fprintf(stderr, "coop-scriptmenu: the event's script opens %s for player %d at link tick %lld: %s\n", KindName(kind), g_menu.owner,
                 (long long) g_menu.opened, !g_menu.local ? "retail's, on every copy" : OwnerHere() ? "made and stepped on this copy alone" : "this copy waits for its end");
    std::fflush(stderr);
}

// Every copy, the opening's frames: true on the fourth (retail's EdInitModeFinish). The opener's copy makes it.
int CoopScriptMenuInit(CCamera *camera) {
    if (!g_menu.local) {
        const int finished = EdInitModeFinish(camera, TexManager.GetTexture(const_cast<char *>("frame_image"), -1));

        g_menu.made = g_menu.made || finished != 0;
        return finished;
    }
    if (!OwnerHere()) {
        g_menu.made = ++g_menu.init_ticks >= 4;
        return g_menu.made ? 1 : 0;
    }
    FeedPad();
    WriteGuardMenu guard("script menu open"); // coop-guard (a test's: DC_QA_WRITE_GUARD)
    SoundOwner     sound(g_menu.owner);
    DrawRng        rng;
    PadIn          pad;
    FullMenuWritesBegin(true); // (its opening's writes too)
    const int finished = EdInitModeFinish(camera, TexManager.GetTexture(const_cast<char *>("frame_image"), -1));
    FullMenuWritesEnd();
    g_menu.made = g_menu.made || finished != 0;
    return finished;
}

// Every copy, each tick it is open: true on the tick it ends (retail: its own finish; local: its command applied).
int CoopScriptMenuStep() {
    if (!g_menu.local) {
        // coop-small2: retail's step on a copy whose script never opened it here (a merge brought it, its record not carried: the twin)
        if (!g_menu.on && !g_menu.stray_said) {
            g_menu.stray_said = true;
            std::fprintf(stderr, "coop-scriptmenu: a script's menu this copy never opened steps here in the lockstep (retail's) at link tick %lld\n", (long long) NetplayLinkTick());
            std::fflush(stderr);
        }
        EdDrawOffAll();
        return EdMenuMode();
    }
    if (g_menu.landed) {
        return 1;
    }
    if (!OwnerHere()) {
        if (CoopWorldsLinkedTogether()) {
            g_menu.carried = false; // (merged: from here a link down is the opener gone)
            return 0;
        }
        if (CoopTownMenuLinked()) {
            return 0; // (the catch-up's replay steps alike: linked for the town's menus, the merge not done yet)
        }
        // coop-small2: a merge's state set brought it before the link is up (the join lands first): the opener is coming, not gone
        if (g_menu.carried) {
            return 0;
        }
        // The opener gone (the link down, this copy alone): nothing will come; it ends here as cancelled.
        std::fprintf(stderr, "coop-scriptmenu: player %d's %s ends without them (the link is down): cancelled here\n", g_menu.owner, KindName(g_menu.kind));
        g_menu.landed = true;
        g_menu.result = -1;
        return 1;
    }
    if (g_menu.sent) {
        return 0;
    }
    FeedPad();
    int finished = 0;
    int result = -1;
    {
        WriteGuardMenu guard("script menu step"); // coop-guard (a test's: DC_QA_WRITE_GUARD)
        SoundOwner     sound(g_menu.owner);
        DrawRng        rng;
        PadIn          pad;
        const int      jump_before = MenuMapJumpMode;

        EdDrawOffAll(); // (the menu covers this copy's screen alone: the flag only MainDraw reads)
        FullMenuWritesBegin(true);
        finished = EdMenuMode();
        FullMenuWritesEnd();
        if (finished != 0) {
            result = g_menu.kind == kKindUseItem || g_menu.kind == kKindBait ? EdGetUseItem() : g_menu.kind == kKindWorldMap ? MenuMapJumpMode : 0;
            GamePad.AutoRepeatOff(); // (retail's EdExitMenu, on its own pad)
            GamePad.MenuModeOff();
        }
        MenuMapJumpMode = jump_before; // (its end command sets it on every copy)
    }
    if (++g_menu.steps % 120 == 1) { // (the rows' line)
        std::fprintf(stderr, "coop-scriptmenu: player %d's %s steps on this copy alone (link tick %lld)\n", g_menu.owner, KindName(g_menu.kind), (long long) NetplayLinkTick());
    }
    if (finished == 0) {
        return 0;
    }
    g_menu.sent = true;
    EdDrawOnAll(); // (the world on this screen again at once, as retail's close)

    NetCommand command;

    command.kind = kCmdScriptMenuEnd;
    command.a = (std::uint8_t) g_menu.kind;
    command.c = (std::uint32_t) (std::int32_t) result;
    std::fprintf(stderr, "coop-scriptmenu: player %d's %s finished on this copy with %d at link tick %lld: its end goes to every copy\n", g_menu.owner,
                 KindName(g_menu.kind), result, (long long) NetplayLinkTick());
    NetplayQueueCommand(command);
    return g_menu.landed ? 1 : 0; // (alone, a command applies at once)
}

// The result the script gets: retail's own, or the end command's.
int CoopScriptMenuResult(int here) {
    return g_menu.local ? g_menu.result : here;
}

// Every copy, after the script took its result (retail's EdExitMenu).
void CoopScriptMenuExit() {
    if (!g_menu.local) {
        EdExitMenu();
    } else {
        MGFlipWaitVSync(0); // (EdInitMenu's, on every copy; the pad's menu mode was only ever the menu's own pad)
    }
    g_menu.on = false;
}

// The menu's opening refused (EdInitMenu), or the event cleared under it.
void CoopScriptMenuOver() {
    g_menu.on = false;
}

bool CoopScriptMenuOpenHere() {
    return g_menu.on && (!g_menu.local || OwnerHere());
}

// coop-small2 (Jep 11-09: a script's menu open at the merge is its opener's): a menu a script opened apart is retail's on its
// copy (its block on every copy of one, decided at its opening). A merge took that copy's state set with it open, so on the
// taker's copy, which never opened it, the lockstep stepped it too (x-town-stayermenu-hisscene: his scene's world map, opened
// apart, stepped on her copy after she joined). Now, as the sender's set is taken, its retail menu goes on as a local one on
// the opener's copy (the menu's pad mode moves to its own pad, the game's pad back to the walk's), and its record (open, kind,
// whose, local) goes with the set: the taker waits in the script's wait for the end command, as every other copy of a local
// menu does. A menu still in its opening frames stays retail's (said). DC_COOP_MUTATE=scriptmenu_merge_retail (twin): nothing
// changes at the merge, retail's on every copy after it, as before.
namespace {

struct ScriptMenuSync {
    std::int32_t present; // 1: the sender had a script's menu open, carried local
    std::int32_t kind;
    std::int32_t owner;
    std::int64_t opened;
} g_menu_sync;

} // namespace

void CoopScriptMenuRetailToLocal() {
    if (!g_menu.on || g_menu.local || g_menu.landed || Mutated("scriptmenu_merge_retail")) {
        return;
    }
    const int here = NetplayLocalPlayer() != 0 ? NetplayLocalPlayer() : NetplayRole(); // (the merge's link not up yet: its role)

    if (!g_menu.made || g_menu.owner != here || !FullMenuCarryOn() || Mutated("scriptmenu_lockstep")) {
        std::fprintf(stderr, "coop-scriptmenu: player %d's %s stays retail's through the merge (%s)\n", g_menu.owner, KindName(g_menu.kind),
                     !g_menu.made ? "still opening" : g_menu.owner != here ? "not this copy's player's" : "menus in the lockstep");
        return;
    }
    g_menu.local = true;
    g_menu.pad = GamePad; // (its menu mode and repeats, as retail's step left them)
    g_menu.before = InputPadState{};
    NetplayMenuPad(g_menu.owner, g_menu.before);
    GamePad.AutoRepeatOff(); // (the game's pad as retail's EdExitMenu leaves it, on this copy and so in the set)
    GamePad.MenuModeOff();
    std::fprintf(stderr, "coop-scriptmenu: player %d's %s (opened apart at link tick %lld) goes on as a local menu through the merge at link tick %lld\n",
                 g_menu.owner, KindName(g_menu.kind), (long long) g_menu.opened, (long long) NetplayLinkTick());
    std::fflush(stderr);
}

unsigned char *CoopScriptMenuSyncBytes(std::size_t *size) {
    *size = sizeof g_menu_sync;
    return reinterpret_cast<unsigned char *>(&g_menu_sync);
}

void CoopScriptMenuSyncTake() {
    g_menu_sync = ScriptMenuSync{};
    if (g_menu.on && g_menu.local && !g_menu.landed) {
        g_menu_sync.present = 1;
        g_menu_sync.kind = g_menu.kind;
        g_menu_sync.owner = g_menu.owner;
        g_menu_sync.opened = g_menu.opened;
    }
}

void CoopScriptMenuSyncClear() {
    g_menu_sync = ScriptMenuSync{};
}

void CoopScriptMenuSyncApply() {
    if (g_menu_sync.present == 0) {
        return;
    }
    g_menu = ScriptMenu{};
    g_menu.on = true;
    g_menu.local = true;
    g_menu.made = true;
    g_menu.init_ticks = 4;
    g_menu.kind = g_menu_sync.kind;
    g_menu.owner = g_menu_sync.owner;
    g_menu.opened = g_menu_sync.opened;
    g_menu.carried = true;
    g_menu_sync = ScriptMenuSync{};
    if (OwnerHere()) {
        std::fprintf(stderr, "coop-scriptmenu: the state set brings this copy's own %s open: not this copy's to make (said, left waiting)\n", KindName(g_menu.kind));
    }
    EdDrawOnAll(); // (the world on this screen: the menu is the opener's)
    std::fprintf(stderr, "coop-scriptmenu: the state set brings player %d's %s open (local): this copy waits for its end\n", g_menu.owner, KindName(g_menu.kind));
    std::fflush(stderr);
}
