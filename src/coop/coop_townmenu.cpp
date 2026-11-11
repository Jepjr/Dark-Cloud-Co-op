// coop-town: the game's menu in a linked town, each player's own (Jep, 2026-10-21, two-PC test: player 2's Esc
// "immediately makes P2 control P1 (Toan) and then opens P1's menu"; player 1's Triangle menu froze player 2 on
// "Toan is in the menu"). His rule: the world pauses only when every player in the zone is paused or in a menu.
//
// Player 1's menu (the town's walk opens it on Triangle, editloop): player 2 walks on through it (coop_town's
// CoopTownStepAside, CoopTownDrawnMode); her copy draws the town walking and steps his menu into a target nobody
// sees, so both copies step the same menu state (its draw steps some) and stay alike.
//
// Player 2's own menu: her Triangle (Esc on a keyboard) while the town walks opens the game's menu for her member
// and, with separate bags, her bag. Toan stays player 1's and walks on. Each tick both copies step it from the pad
// they agreed on, with the game pad (its menu mode and repeats kept in a pad of its own) and the party's member and
// bag hers for the step and back after (as coop_fullmenu steps her menu on a floor); only her copy draws it, over
// the live town dimmed as his menu dims it. It closes as the menu closes itself, or when the town changes.
//
// coop-townmenus (Jep, 2026-10-24: "the menu screen doesn't pause P2's game. P2 can still move around with the analogue
// stick and can't use the menu with the analogue. It also doesn't dim the screen"; "they can't be in the triangle menu
// at the same time"): her menu is her pause: she stands while it is open (coop_town's step). Her game pad is a whole
// one of her own, updated as the game's is (PadUpdateFrom: the menu mode turns her stick into the d-pad, the repeats
// run). Her menu keeps every global of the game's menu, its texture blocks and its work buffer to itself (coop_fullmenu's
// second menu's means), put in place around her step and draw only, so his menu opens beside hers and hers beside his,
// the second half a second after the first (a tick count, the same on both copies, as on a floor).
// Her Start is her own pause in the linked town (Jep, 2026-10-24: Start is each player's pause): she stands, her copy
// shows the town's own pause (its dim and picture) over the live town; Start again, or the town changing, ends it.
// The town goes on (Georama is no pause either; the town has no foes to hold). Her Triangle waits while she is paused.
// DC_COOP_MUTATE=townmenu_shared: his menu freezes her as before (twin); p2townmenu_off: her Triangle opens nothing;
// townmenu_rawpad: her pad copied in as before (no stick mode, no repeats); townmenu_nodim: no dim under hers;
// townmenuboth_off: one menu at a time, as before; townmenu_oneset: hers on the globals in place (his, if open).

#include "coop/game_ns.hpp"
#include <algorithm>
#include <cstdint>
#include <optional>
#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>

#include "battlemenu.hpp"
#include "dun/gameloop.hpp"
#include "editloop.hpp"
#include "edit.hpp"
#include "editloop3.hpp"
#include "editmapscript.hpp"
#include "npcharacter.hpp"
#include "gamepad.hpp"
#include "gamepad.hpp"
#include "menu_draw.hpp"
#include "mglib.hpp"
#include "rect.hpp"
#include "texture.hpp"
#include "gfx/gfx.hpp"
#include "platform/input.hpp"
#include "memcard.hpp"
#include "menu_save.hpp"
#include "savedata.hpp"
#include "shop.hpp"
#include "snd.hpp"
#include "userstatus.hpp"

#include "coop/coop.hpp"
#include "coop/coop_actor.hpp"
#include "coop/coop_camera.hpp" // coop-stage4: DrawRng
#include "coop/coop_audio.hpp"
#include "coop/coop_core.hpp"
#include "coop/coop_fullmenu.hpp"
#include "coop/coop_georama.hpp"
#include "coop/coop_interact.hpp" // coop-gatepopup3: DrawCallQuestion
#include "coop/coop_menu.hpp"
#include "coop/coop_town.hpp"
#include "coop/coop_townmenu.hpp"
#include "coop/coop_writeguard.hpp" // coop-guard
#include "coop/coop_worlds.hpp"
#include "coop/netplay.hpp"

using namespace coop;

// The ring's chooser (save or load, quit) and its choice (port/src/battlemenu/main_ring.cpp).
namespace dc::battlemenu {
bool BattleMenuQuitConfirmActive();
int *BattleMenuQuitConfirmChoicePtr();
} // namespace dc::battlemenu
namespace dc::game {
extern int AtoraTextureEnterFlag;
extern int AtoraTextureReadBlock;
void EdSaveFrameImage(CTexture texture); // coop-menubackdrop: the menu's still (ps2/src/edit.cpp)

extern s32 mode;
extern s32 MapNo;
int EdGetShopNo(); // coop-p1parts (edit.cpp)
void EdInMenuToWalk(); // coop-stage4 4c (edit_in.cpp)
} // namespace dc::game
namespace dc::quest {
void CustomShopEnd();   // coop-stage4 r6: a parked shop puts retail's prices back (quest/custom_shop)
} // namespace dc::quest

namespace {

constexpr int kTownMode = 2; // GAME_MODE_EDIT

int  g_p1_menu = -1; // the menu the walk opened last (ED_MENU_*), for "his battle menu"
int  g_p1_ticks = 0;  // ticks his battle menu has been open (its step's count)
bool g_drawing_hers = false;
bool g_drawing_his = false; // coop-menuanim: his menu's draw over the live town in progress
bool g_his_frame = false;   // coop-menuanim: this tick's frame draws his menu over the live town (the town drawn walking)
bool g_paused = false; // her own pause (Start) in the linked town
int  g_paused_ticks = 0;
long long g_let_go = -1; // coop-menucross: the link tick her menu or her pause last let go of her pad
bool g_hidden_on = false;
bool g_in_hers = false; // coop-saveload: her menu's globals are the ones in place (HerIn to HerOut)
gfx::TextureHandle g_hidden = gfx::kNullTexture;
gfx::TextureHandle g_previous = gfx::kNullTexture;
std::optional<SoundOwner> g_his_sound; // coop-menuse: around the town's menu step, its sounds are his

struct Own {
    bool          on = false;
    int           map = -1;
    int           ticks = 0;
    std::uint16_t prev = 0;
    InputPadState before{};
    CGamePad      pad{};
    int           chara = -1; // the party's own member, while hers is swapped in
    bool          bag_swapped = false;
    int           page = -1; // her menu's page on the ring (MenuSelect[1]), as last logged
    // Her menu's own globals (while the ones in place are aside), its work buffer and the town menu allocator over it.
    std::vector<unsigned char> state;
    std::vector<unsigned char> aside;
    u_long128                 *buffer = nullptr;
    CDataAlloc2<1>             alloc;
    int                        jump = -1; // the map her closed menu leaves town for, till the town takes it
    int                        kind = ED_MENU_BATTLE; // coop-stage4 r6: the battle menu, or a shop
    MenuCameraOwn              camera{}; // coop-stabfix2: its own menu camera (EngineIn)
} g_own;

// coop-stage4 4b (Jep, 2026-11-07: town menus local for both players, always): his battle menu from his Triangle in the linked
// town runs as hers does, on his copy alone. The press (his agreed pad) sets his in-menu bit (town + local) on every copy and the
// town stays in its walk on every copy (Toan stands: the bit routes his pad to nothing, netplay's stage 3a); his copy starts the
// engine (retail's BattleMenuInit for the town, its own globals, pad and the town menu allocator, as hers), its writes as pokes
// and its random numbers its copy's own. Its close is one command (kCmdMenu) carrying his map jump (and indoors the room's way
// out), taken on every copy on its tick, as retail's close takes TownMapJump. The menus retail opens for him by itself (the bag's
// overflow, an event's goto_menu other than the battle menu) stay retail's mode. DC_COOP_MUTATE=p1townmenu_lockstep (twin):
// his menu is retail's mode on both copies, as before.
// coop-stage4 r6: now those too. The bag's overflow is his battle menu (its close clears the flag on every copy, retail's close, by the
// command). A keeper's shop (goto_menu) is retail's own init (EdInitModeFinish) and loop (CommonShopLoop) on his copy alone; that
// loop steps and draws in one call, so it runs where his menu draws, over the town. Its writes go as pokes, the stock and the game
// flags with the status (coop_fullmenu's LocalWrites). The menus a talk's script opens inside the talk (Use Item, names, the
// world map, fishing) are the opener's copy's too, in the event's own step (coop_scriptmenu.cpp). DC_COOP_MUTATE=retailmenu_lockstep (twin): as before, retail's mode.
Own           g_his;
bool          IsShop(int kind) { return kind == ED_MENU_SHOP || kind == ED_MENU_UNK_4; }
// The player whose engine it is, so a copy asks "is it mine" by the engine, not by a player number (the n-player lint).
constexpr int kHisPlayer = 1;
bool          HisHere() { return NetplayLocalPlayer() == kHisPlayer; }
int           g_his_flag = 0;         // his engine's page state (BattleMenuFlag) as it last stepped, for the log
int           g_his_bit_ticks = 0;    // ticks his town bit has been set (every copy alike)
int           g_his_close_code = 0;   // his closed engine's close code for the command (jump + 1, 0 none; kRoomOut: the room left)
int           g_his_jump = -1;        // his close command's map jump, every copy, till the town (or the room) takes it
bool          g_his_room_out = false; // indoors: the room left with it (retail's SetInteriorOutFlag from the menu)
constexpr int kRoomOut = 0x10000;
constexpr int kClearOver = 0x20000; // coop-stage4 r6: his battle menu's close (retail's EdClearItemOverFlag)

// coop-stage4 (Jep, 2026-11-07: town menus local for both players, always): linked with local menus on, her menu's engine runs on
// her copy alone. Her Triangle (her agreed pad: every copy sees it on one tick) sets her in-menu bit on every copy (town + local)
// and starts the engine on hers; its writes to the party status go out as pokes (FullMenuWritesBegin), its random numbers are
// her copy's own (DrawRng), and its close is one command (kCmdMenu with her map jump) that lets go of her pad and takes her out
// of town on every copy on its tick. Every simulation read of "her menu open" reads the bit (HerMenuOpen). The other copy no
// longer steps or draws her menu out of sight. DC_COOP_MUTATE=townmenu_lockstep (twin): her menu stepped on every copy, as before.
bool g_local_engine = false; // g_own.on is a local engine (this copy is hers)
int  g_bit_ticks = 0;        // ticks her town bit has been set (every copy alike: his Triangle's wait reads it)
int  g_close_jump = -1;      // her closed local engine's map jump, till the close command carries it

// coop-stage4 4c (Jep, 2026-11-06/07: a merge lands at once, neither player waits, the menu stays open): apart a town menu is retail's
// mode on its copy. As a merge comes, the sender's goes on as its local engine (its pack, globals and pad are the ones in place:
// the town back in its walk, the bit in the state set), so it stays exactly where it is. The taker's cannot: the state set replaces
// the texture table and the menu's work memory, so it closes as the link starts and opens again on its page once the copy has
// landed (g_reopen). DC_COOP_MUTATE=merge_menu_retail (twin): retail's mode holds the merge, as before.
int  g_reopen[2] = {-1, -1}; // the page each player's parked menu opens again on (-1 none)
bool g_carried = false;      // a sender's menu goes on through the merge's own ticks (no session reset ends it)

bool LocalOn() {
    return FullMenuLocalOn() && !Mutated("townmenu_lockstep");
}

// The merge's own ticks: linked, still apart (Linked() is false until the two copies step alike).
bool MergeUnderWay() {
    return NetplayLinkUp() && CoopWorldsApart() && mode == kTownMode;
}

// Her menu open as the simulation knows it: the bit, local; her engine, in the lockstep.
bool HerMenuOpen() {
    return LocalOn() ? PlayerInMenu(2, kActorMenuTown) : g_own.on;
}

CUserStatus *Status() {
    return UserStatus != nullptr ? UserStatus : SaveData != nullptr ? (CUserStatus *) SaveData->GetDngStatus() : nullptr;
}

// coop-inbuild: inside a building too, once her character is placed in it.
// coop-atlamenu: or still apart in a catch-up's ticks that both copies step (his long load as he comes back to her town: her
// menu and pause were refused all through it). DC_COOP_MUTATE=catchupmenu_apart (the twin): apart is never linked, as before.
bool Linked() {
    float at[3];

    return Enabled() && NetplayLocalPlayer() != 0 && mode == kTownMode && (EdInteriorFlag == 0 || (CoopTownIndoorPose(at) && !Mutated("no_indoor_menu"))) &&
           (!CoopWorldsApart() || (NetplayCatchupStepsAlike() && !Mutated("catchupmenu_apart")));
}

// The town (or the room) walks: player 1 is in none of his own modes, so her menu may open.
bool TownWalks() {
    return EdInteriorFlag == 0 ? ::GameMode == ED_MODE_WALK : CoopTownIndoorWalks();
}

// coop-pausegeo (Jep's playtest, 2026-10-25: "P2 can't pause when P1 is in georama"): his Georama (its edit, its menu
// and his pause from it) is never a pause of hers to wait out; her Start pauses her beside it. Outdoors only (a house has
// no Georama). DC_COOP_MUTATE=p2pause_notingeorama: her Start waits for his walk, menu or pause, as before (twin).
bool HisGeorama() {
    return EdInteriorFlag == 0 && !Mutated("p2pause_notingeorama") &&
           (::GameMode == ED_MODE_GEORAMA || ::GameMode == ED_MODE_GEORAMA_MENU_INIT || ::GameMode == ED_MODE_GEORAMA_MENU ||
            ::GameMode == ED_MODE_RETURN_MENU_GEORAMA);
}

bool Allowed() {
    return Linked() && MenusOn() && !MenuPanelOn() && !Mutated("p2townmenu_off") && TownPartnerShown(nullptr, nullptr);
}

// Her pad on the game pad's first port, as it is now and as it was the tick before (Down reads the change).
void SetPortOne(CGamePad &pad, const InputPadState &now, const InputPadState &before) {
    PAD_DATA *data = reinterpret_cast<PAD_DATA *>(&pad); // pad[0], pad[1], previous_pad[0], previous_pad[1]

    for (int i : {0, 2}) {
        const InputPadState &from = i == 0 ? now : before;
        PAD_STATUS          &to = data[i].input.status;

        to.button = from.buttons & 0xFFFF;
        to.left_x = from.left_x;
        to.left_y = from.left_y;
        to.right_x = from.right_x;
        to.right_y = from.right_y;
    }
}

int PartnerMember() {
    int member = -1;

    return TownPartnerShown(nullptr, &member) ? member : -1;
}

// Around her menu's step and draw: her member is the party's current one and her bag the party's.
void ViewBegin() {
    CUserStatus *status = Status();
    int          member = PartnerMember();

    g_own.chara = -1;
    if (status == nullptr || member < 0) {
        return;
    }
    g_own.chara = status->cur_chara;
    status->cur_chara = (s8) member;
    g_own.bag_swapped = MenuSwapP2Bag();
}

void ViewEnd() {
    CUserStatus *status = Status();

    if (g_own.chara < 0 || status == nullptr) {
        return;
    }
    status->cur_chara = (s8) g_own.chara;
    if (g_own.bag_swapped) {
        MenuSwapP2Bag();
        g_own.bag_swapped = false;
    }
    g_own.chara = -1;
}

bool OwnSet() {
    return !Mutated("townmenu_oneset");
}

// Her menu in place around its init, step and draw: its globals (the ones in place aside), the town menu allocator over
// its own buffer, her pad as the game pad, her member and bag. HerOut puts all back.
// coop-stage4 4b: an engine's in and out, hers (g_own) or his (g_his): its globals, allocator and pad; her member and bag hers.
void EngineIn(Own &engine, int player) {
    if (OwnSet()) {
        MenuStateSave(engine.aside);
        MenuStateLoad(engine.state);
    }
    std::swap(EdMenuBuffer, engine.alloc);
    std::swap(GamePad, engine.pad);
    MenuCameraSwap(engine.camera); // coop-stabfix2: its pages' camera its own, the world's kept
    if (player == 2) {
        ViewBegin();
        g_in_hers = true;
    }
}

void EngineOut(Own &engine, int player) {
    if (player == 2) {
        g_in_hers = false;
        ViewEnd();
    }
    MenuCameraSwap(engine.camera);
    std::swap(GamePad, engine.pad);
    std::swap(EdMenuBuffer, engine.alloc);
    if (OwnSet()) {
        MenuStateSave(engine.state);
        MenuStateLoad(engine.aside);
    }
}

void HerIn() {
    EngineIn(g_own, 2);
}

void HerOut() {
    EngineOut(g_own, 2);
}

// An agreed pad into its engine's game pad: its whole update (the menu mode's stick to d-pad, the repeats), or (the twin)
// copied in as before.
void FeedPadTo(Own &engine, const InputPadState &now) {
    if (Mutated("townmenu_rawpad")) {
        SetPortOne(engine.pad, now, engine.before);
    } else {
        PadUpdateFrom(engine.pad, now);
    }
    engine.before = now;
}

void FeedPad(const InputPadState &now) {
    FeedPadTo(g_own, now);
}

void DeleteOwnBlocks() {
    int        count = 0;
    const int *blocks = MenuSecondBlocks(&count);

    for (int i = 0; i < count; i++) {
        TexManager.DeleteTextureBlock(blocks[i]);
    }
}

// The other menu open long enough for this one to open beside it (half a second: its pictures have loaded on both
// copies alike), or not open at all.
bool BesideOk(bool other_open, int other_ticks) {
    return !other_open || (!Mutated("townmenuboth_off") && other_ticks >= 30);
}

// DC_COOP_MENUDEBUG (coop::Debug in coop_menu): the menu draws, shown on this copy or out of sight, each second.
int g_draws[2][2] = {}; // [his, hers][shown, hidden]

void CountDraw(int whose, bool hidden) {
    COOP_TEST_SWITCH(bool, debug, TestEnv("DC_COOP_MENUDEBUG") != nullptr);
    static int        n = 0;

    g_draws[whose][hidden ? 1 : 0]++;
    if (debug && ++n % 60 == 1) {
        std::printf("coop-townmenu: draws on player %d's copy: his menu shown %d hidden %d, hers shown %d hidden %d\n", NetplayLocalPlayer(),
                    g_draws[0][0], g_draws[0][1], g_draws[1][0], g_draws[1][1]);
    }
}

void HiddenBegin() {
    if (g_hidden == gfx::kNullTexture) {
        g_hidden = gfx::CreateRenderTarget(640, 480, true);
    }
    g_previous = gfx::CurrentRenderTarget();
    gfx::SetRenderTarget(g_hidden);
    g_hidden_on = true;
}

void HiddenEnd() {
    if (g_hidden_on) {
        gfx::SetRenderTarget(g_previous);
        g_hidden_on = false;
    }
}

// coop-saveload (DC_COOP_MENUDEBUG): the save screen as a menu has it in place (its chooser and choice, its page's
// state, the save screen's mode and step), logged on a change for each menu; and where each menu's save screen is drawn.
bool SaveScreenUp() {
    return dc::battlemenu::BattleMenuQuitConfirmActive() || BattleMenuFlag == BTLMENU_STATE_SAVE || BattleMenuFlag == BTLMENU_STATE_SAVE_OPEN ||
           BattleMenuFlag == BTLMENU_STATE_SAVE_CLOSE;
}

void NoteSaveScreen(int whose) {
    COOP_TEST_SWITCH(bool, debug, TestEnv("DC_COOP_MENUDEBUG") != nullptr);
    static int        last[2][6] = {{-1, -1, -1, -1, -1, -1}, {-1, -1, -1, -1, -1, -1}};

    if (!debug) {
        return;
    }

    const bool chooser = dc::battlemenu::BattleMenuQuitConfirmActive();
    const bool up = SaveScreenUp() && !chooser;
    const int  now[6] = {chooser ? 1 : 0, chooser ? *dc::battlemenu::BattleMenuQuitConfirmChoicePtr() : -1, (int) BattleMenuFlag,
                         up ? (int) ::SaveMenu.mode : -1, up ? (int) ::SaveMenu.key_no : -1, up ? (int) ::SaveMenu.file_no : -1};

    if (std::memcmp(now, last[whose - 1], sizeof now) != 0) {
        std::memcpy(last[whose - 1], now, sizeof now);
        std::printf("coop-saveload: player %d's menu: chooser %d choice %d state %d save mode %d step %d file %d\n", whose, now[0], now[1],
                    now[2], now[3], now[4], now[5]);
    }
}

int g_save_draws[2][2] = {}; // [his, hers][shown, hidden]

void CountSaveDraw(int whose, bool hidden) {
    COOP_TEST_SWITCH(bool, debug, TestEnv("DC_COOP_MENUDEBUG") != nullptr);
    static int        n = 0;

    if (!SaveScreenUp()) {
        return;
    }
    g_save_draws[whose][hidden ? 1 : 0]++;
    if (debug && ++n % 30 == 1) {
        std::printf("coop-saveload: save screens drawn on player %d's copy: his shown %d hidden %d, hers shown %d hidden %d\n",
                    NetplayLocalPlayer(), g_save_draws[0][0], g_save_draws[0][1], g_save_draws[1][0], g_save_draws[1][1]);
    }
}

// coop-menubackdrop (Jep's 2026-10-28 online playtest: "when P2 opens the weapons menu after leaving the mayor's house,
// the menu background still shows the inside of the house"): her menu's pages draw parts of the game's still frame
// ("frame_image", the Weapons page's side bands), which only the walk's own menu kept (EdInitModeFinish), so hers showed
// whatever was kept last. Her copy keeps her screen's frame before her menu as she opens it (the town's step takes it
// from the frame before, EdSaveFrameImageTask). The still is drawing only: no lockstep state.
// DC_COOP_MUTATE=menubackdrop_stale (twin): none kept, as before.
void KeepStill(int player = 2) {
    if (NetplayLocalPlayer() != player || Mutated("menubackdrop_stale")) {
        return;
    }
    if (CTexture *still = TexManager.GetTexture("frame_image", -1)) {
        EdSaveFrameImage(*still);
        std::fprintf(stderr, "coop-townmenu: player %d's menu keeps %s screen's still (town mode %d, indoors %d)\n", player, player == 2 ? "her" : "his",
                     (int) ::GameMode, EdInteriorFlag != 0 ? 1 : 0);
    }
}

void End(const char *why) {
    g_own.on = false;
    if (!g_local_engine) {
        g_let_go = NetplayLinkTick(); // coop-menucross (a local one's: its close command, on every copy)
    }
    g_local_engine = false;
    DeleteOwnBlocks();
    std::printf("coop-townmenu: player 2's menu closes (%s) after %d ticks\n", why, g_own.ticks);
}

// coop-stage4 4b: his menu local (g_his above).
bool HisLocalOn() {
    return LocalOn() && !Mutated("p1townmenu_lockstep");
}

bool HisLocalMenu() {
    return HisLocalOn() && PlayerInMenu(1, kActorMenuTown);
}

void HisEnd(const char *why) {
    g_his.on = false;
    std::printf("coop-townmenu: player 1's menu closes (%s) after %d ticks\n", why, g_his.ticks);
}

// Every copy, each town tick: his bit's count and (DC_COOP_DEBUG) where both stand under it; his copy steps his engine.
void HisTick() {
    // coop-stage4: retail's save screen opens with EditSave (the town's live Georama and clock into the SaveData), now on the
    // saver's copy alone. That state is every copy's alike, so while a town menu runs local every copy writes it each tick
    // before the menus step: the saver's own call then changes nothing the others lack. DC_COOP_MUTATE=townsave_here (twin):
    // the saver's copy alone, as the local menu first had it.
    // (coop-p1parts: a parts menu has the Save page too)
    if (LocalOn() && Linked() && pEditGround != nullptr && (PlayerInMenu(1, kActorMenuTown | kActorMenuLocal) || PlayerInMenu(2, kActorMenuTown | kActorMenuLocal) || CoopPartsMenuLocalOpen()) &&
        !Mutated("townsave_here")) {
        EditSave();
    }
    g_his_bit_ticks = PlayerInMenu(1, kActorMenuTown) ? g_his_bit_ticks + 1 : 0;
    if (HisLocalMenu() && CoopDebug() && g_his_bit_ticks % 30 == 1) {
        float at[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        float toan[4] = {0.0f, 0.0f, 0.0f, 1.0f};

        TownPartnerShown(at, nullptr);
        if (Chara != nullptr) {
            Chara->GetPosition(toan);
        }
        std::printf("coop-townmenu: player 1's menu open, player 2 at (%.1f %.1f), Toan at (%.1f %.1f), %d ticks%s (page state %d)\n", at[0], at[2], toan[0],
                    toan[2], g_his_bit_ticks, g_his.on ? ", its engine here" : "", g_his_flag);
    }
    if (!g_his.on) {
        return;
    }
    if ((!Linked() && !MergeUnderWay()) || MapNo != g_his.map) {
        HisEnd("the town changed");
        return;
    }

    InputPadState pad;

    if (!NetplayMenuPad(1, pad)) {
        HisEnd("no player 1");
        return;
    }
    g_his.ticks++;

    WriteGuardMenu guard("his town menu step"); // coop-guard (a test's: DC_QA_WRITE_GUARD)
    SoundOwner     sound(1); // coop-menuse
    DrawRng        rng;      // its random numbers this copy's own
    const int      jump_before = MenuMapJumpMode;
    const int  out_before = GetInteriorOutFlag();

    FeedPadTo(g_his, pad);
    if (IsShop(g_his.kind)) {
        return; // (its loop steps and draws in one call: HisDraw)
    }
    EngineIn(g_his, 1);
    MenuMapJumpMode = -1; // (retail's menu step, editloop)
    FullMenuStepping(1);
    FullMenuWritesBegin(true);
    int open = BattleMenuCursor();
    FullMenuWritesEnd();
    FullMenuStepping(0);
    if (open != 0 && EdInteriorFlag != 0 && CoopTownRoomMenuTestClose()) { // (coop-roommenu's test hook, as the room's menu step)
        open = 0;
    }
    const int  page = MenuSelect[1];
    const int  jump = MenuMapJumpMode;
    const bool room_out = EdInteriorFlag != 0 && GetInteriorOutFlag() != 0;
    g_his_flag = (int) BattleMenuFlag;
    NoteSaveScreen(1);
    EngineOut(g_his, 1);
    MenuMapJumpMode = jump_before; // (the close command sets them on every copy)
    SetInteriorOutFlag(out_before);

    if (page != g_his.page) {
        std::printf("coop-townmenu: player 1's menu at page %d (was %d) after %d ticks\n", page, g_his.page, g_his.ticks);
        g_his.page = page;
    }
    if (open == 0) {
        HisEnd("closed");
        g_his_close_code = (jump >= 0 ? jump + 1 : 0) | (room_out ? kRoomOut : 0) | kClearOver;
        if (jump >= 0 || room_out) {
            std::fprintf(stderr, "coop-townmenu: player 1's menu picks map %d%s on his copy\n", jump, room_out ? " (out of the room)" : "");
        }
    }
}

// coop-stage4 r6: a shop's one call (step and draw), an engine's: its own writes as pokes. True when it closed.
bool ShopCall(Own &engine, int player) {
    WriteGuardMenu guard(player == 1 ? "his shop" : "her shop"); // coop-guard (a test's: DC_QA_WRITE_GUARD)
    const auto     render_info = mgRenderInfo; // (as HisDraw: its keeper's model sets its own view)
    DrawRng    rng;

    MenuOverWorldDepth(player);
    EngineIn(engine, player);
    FullMenuStepping(player);
    FullMenuWritesBegin(true);
    COOP_TEST_SWITCH(bool, stock_test, TestEnv("DC_COOP_TEST_SHOP_STOCKWRITE") != nullptr);
    if (stock_test && engine.ticks == 30 && SaveData != nullptr) {
        SaveData->GetStockItem()->dungeon_items[0] = 0x71; // (the row's: a stock word the shop wrote, carried to every copy)
        std::printf("coop-townmenu: test: player %d's shop writes a stock word\n", player);
    }
    const bool closed = CommonShopLoop() != 0;
    FullMenuWritesEnd();
    FullMenuStepping(0);
    EngineOut(engine, player);
    mgRenderInfo = render_info;
    return closed;
}

// coop-pt1108 (Jep's 11-08 shot, not reproduced yet: the Atla page's cards, pictures and chips gone, its labels there): a line when its picture block, entered, holds no pictures, for the next playtest's log.

static void AtlaBlockCheck() {
    static bool said = false;
    const bool  empty = AtoraTextureEnterFlag != 0 && TexManager.GetTexture(const_cast<char *>("sozai"), AtoraTextureReadBlock) == nullptr;

    if (empty != said) {
        said = empty;
        std::fprintf(stderr, "coop-townmenu: the Atla page's picture block %d %s at link tick %lld (town mode %d, clock %.2f)\n", AtoraTextureReadBlock, empty ? "holds no pictures" : "has its pictures",
                     (long long) NetplayLinkTick(), (int) ::GameMode, (double) NowTime);
    }
}

// coop-pt1108 (Jep's 11-08 playtest: the battle menu's Georama Parts page: half the screen frozen, its bands lit differently): retail's pages draw pieces of the
// still "frame_image" over themselves (the Atla page's right half and its top and bottom bands, the Weapons page's bands), which match only a backdrop that is that
// still at retail's dim (FrameImageDraw at 0x40). Over the live town the still was the one kept as the menu opened. So here it is the town as just drawn, every frame,
// before the dim, and the dim is retail's half. Drawing only. DC_COOP_MUTATE=townmenu_still_open (twin): kept at the opening alone, the old dim.
const spRGBA *KeepLiveStill() {
    AtlaBlockCheck();
    static const spRGBA kDim = {0x00, 0x00, 0x00, 0x40};
    static const spRGBA kDimOld = {0x00, 0x00, 0x00, 0x50};

    if (Mutated("townmenu_still_open")) {
        return &kDimOld;
    }
    if (CTexture *still = TexManager.GetTexture(const_cast<char *>("frame_image"), -1)) {
        MGMoveFrameBuffImage(reinterpret_cast<sceGsTex0 *>(&still->tex0), 0, 0, 0);
    }
    return &kDim;
}

// His engine's draw over the live town, his copy: the town dimmed under it, as under hers.
void HisDraw() {
    if (IsShop(g_his.kind)) {
        SoundOwner sound(1);

        CountDraw(0, false);
        g_drawing_his = true;
        const bool closed = ShopCall(g_his, 1);
        g_drawing_his = false;
        if (closed) {
            HisEnd("closed");
            g_his_close_code = 0;
        }
        return;
    }
    static int          dims = 0;
    WriteGuardMenu      guard("his town menu draw"); // coop-guard (a test's: DC_QA_WRITE_GUARD)
    SoundOwner          sound(1);
    DrawRng             rng;

    spRGBA *dim = const_cast<spRGBA *>(KeepLiveStill());
        set2DSpriteC4(Vif1Packet, CRect_i_(0, 0, 640, 512), dim, dim, dim, dim);
    if (++dims % 60 == 1) {
        std::printf("coop-townmenu: player 1's menu over the live town, dimmed (%d draws)\n", dims);
    }
    // The menu's draw sets its own projection and view (MenuWorldTrans) in the render info the next tick's clip tests read in a
    // room (netplay's view part): on his copy alone now, so the room's are put back after it. DC_COOP_MUTATE=hisdraw_view (twin):
    // left, as the first local draw had it.
    const auto render_info = mgRenderInfo;

    MenuOverWorldDepth(1); // coop-menudepth
    CountDraw(0, false);
    EngineIn(g_his, 1);
    CountSaveDraw(0, false); // (its save screen's state is the engine's own)
    FullMenuWritesBegin(false); // the draw shows its own edits too
    g_drawing_his = true;
    BattleMenuDraw();
    g_drawing_his = false;
    FullMenuWritesEnd();
    EngineOut(g_his, 1);
    if (!Mutated("hisdraw_view")) {
        mgRenderInfo = render_info;
    }
}

} // namespace

static void HisEngineStart(int page);
static void HerEngineStart(const InputPadState &pad, bool local, bool beside, int page, int kind = ED_MENU_BATTLE);
static void HerEngineStep(const InputPadState &agreed);

void CoopTownNoteMenu(int menu_no) {
    g_p1_menu = menu_no;
}

// coop-menubackdrop: his menu opening beside hers keeps no still on her copy (it would be her open menu, under his
// hidden one); hers stays. DC_COOP_MUTATE=menubackdrop_his (twin): his kept there as before.
bool CoopTownMenuStillHere() {
    if (g_own.on && NetplayLocalPlayer() == 2 && Linked() && !Mutated("menubackdrop_his")) {
        std::fprintf(stderr, "coop-townmenu: player 1's menu keeps no still on player 2's copy (her menu open)\n");
        return false;
    }
    return true;
}

bool CoopTownP1Menu() {
    return Linked() && !Mutated("townmenu_shared") && g_p1_menu == ED_MENU_BATTLE &&
           (EdInteriorFlag == 0 ? ::GameMode == ED_MODE_MENU_INIT || ::GameMode == ED_MODE_MENU : CoopTownIndoorP1Menu()); // coop-inbuild
}

bool CoopTownDrawsRoomUnderMenu() {
    return EdInteriorFlag != 0 && NetplayLocalPlayer() == 2 && CoopTownP1Menu();
}

// coop-roommenu: his menu stepped with its draw in a room (not over the live room: she is in her own menu or pause), and
// her copy draws the room under it: the menu's draw set the menu's projection (MenuWorldTrans, 800) and the room drew with
// it, zoomed in on her screen. True when the room's projection is to be set again before the room's draw.
// DC_COOP_MUTATE=roommenu_zoom (twin): left, as before.
bool CoopTownRoomMenuProjection() {
    return CoopTownDrawsRoomUnderMenu() && !CoopTownP1MenuLive() && !Mutated("roommenu_zoom");
}

bool CoopP2TownMenuBlocksP1() {
    // coop-stage4 r6: his local menu open: the walk opens nothing more (the overflow's flag stays set till its close).
    if (HisLocalMenu()) {
        return true;
    }
    if (LocalOn()) {
        return PlayerInMenu(2, kActorMenuTown) && !BesideOk(true, g_bit_ticks);
    }
    return g_own.on && !BesideOk(true, g_own.ticks);
}

bool CoopP2TownMenuDrawing() {
    return g_drawing_hers;
}

void CoopTownMenuStepBegin() {
    g_his_sound.emplace(1);
    // DC_COOP_DEBUG: where player 2 stands while his menu is open, every 30 ticks (the tests read her walk).
    static int ticks = 0;
    float      at[4] = {0.0f, 0.0f, 0.0f, 1.0f};

    if (CoopDebug() && CoopTownP1Menu() && ++ticks % 30 == 1 && TownPartnerShown(at, nullptr)) {
        std::printf("coop-townmenu: player 1's menu open, player 2 at (%.1f %.1f)\n", at[0], at[2]);
    }
    g_p1_ticks = CoopTownP1Menu() ? g_p1_ticks + 1 : 0;
    // His menu's step draws (BattleMenuDraw in EdMenuMode): on her copy into the hidden target. Over the live town it
    // steps only, and draws after the town's frame (CoopTownP1MenuDraw).
    if (CoopTownP1MenuLive()) {
        return;
    }
    if (NetplayLocalPlayer() == 2 && CoopTownP1Menu()) {
        HiddenBegin();
    }
    if (CoopTownP1Menu()) {
        CountDraw(0, g_hidden_on);
        CountSaveDraw(0, g_hidden_on);
    }
}

void CoopTownMenuStepEnd() {
    HiddenEnd();
    g_his_sound.reset();
    if (CoopTownP1Menu()) {
        NoteSaveScreen(1);
    }
}

// coop-menuanim: his battle menu over the live town (Jep, 2026-10-24), linked and with player 2 in the town; the menu's
// step leaves out its draw (editloop) and the town draws walking under it on both copies (CoopTownDrawnMode).
// DC_COOP_MUTATE=p1menu_still: retail's still frame under it and the town held, as before (twin).
// coop-roommenu (Jep's 2026-10-28 online playtest, both in the mayor's house: P1's menu "zooms P2's camera in", stops the
// room and the mayor): in a room too, the room's menu mode (ED_IN_MODE_MENU, 7). DC_COOP_MUTATE=roommenu_still (twin): in
// a room his menu steps and draws as retail's, over its still frame, as before.
bool CoopTownP1MenuLive() {
    if (!CoopTownP1Menu() || Mutated("p1menu_still")) {
        return false;
    }
    // (indoors her shown state as her step left it this tick: the room's first step puts her body away)
    return EdInteriorFlag == 0 ? ::GameMode == ED_MODE_MENU && TownPartnerShown(nullptr, nullptr)
                               : CoopTownRoomModeNow() == 7 && CoopTownRoomPartnerShown() && !Mutated("roommenu_still"); // ED_IN_MODE_MENU
}

// coop-menuflash (Jep's 2026-10-27 playtest: "when P1 closes his menu, a blue flash shows on both screens"): the tick his
// live menu closes, editloop's step returns before its draw; it draws the town instead. DC_COOP_MUTATE=townclose_flash (twin):
// that tick draws nothing, as before.
bool CoopTownMenuCloseFrame() {
    if (Mutated("townclose_flash")) {
        return false;
    }
    std::printf("coop-townmenu: player 1's menu closes over the live town: its close tick draws the town\n");
    return true;
}

// coop-clockblink (Jep's 2026-10-28 online playtest: "the HUD clock disappears for one frame on both screens, only when P1
// opens the menu"): retail's opening tick (MENU_INIT) hides the clock for the still frame it saves under the menu. That tick
// runs on both copies, and his menu now has the live town under it, so the clock came back the next tick: a one-frame
// blink on both. Her menu never enters MENU_INIT. Over the live town the clock stays; alone (the still frame) as retail.
// DC_COOP_MUTATE=clockblink (twin): hidden that tick, as before.
bool CoopTownMenuOpenKeepsClock() {
    if (!CoopTownP1Menu() || ::GameMode != ED_MODE_MENU_INIT || !TownPartnerShown(nullptr, nullptr) ||
        Mutated("p1menu_still") || Mutated("clockblink")) {
        return false;
    }
    std::printf("coop-townmenu: player 1's menu opens over the live town: the clock stays\n");
    return true;
}

bool CoopTownP1MenuDrawing() {
    return g_drawing_his;
}

void CoopTownP1MenuNoteFrame() {
    g_his_frame = CoopTownP1MenuLive() || (g_his.on && HisHere()); // coop-stage4 4b: or his local engine
}

void CoopTownP1MenuDraw() {
    if (!g_his_frame) {
        return;
    }
    if (g_his.on && HisHere()) { // coop-stage4 4b
        HisDraw();
        return;
    }

    SoundOwner sound(1); // coop-menuse

    if (!HisHere()) {
        HiddenBegin();
    } else {
        // The town under his menu dimmed, as under hers (CoopP2TownMenuDraw).
        static int          dims = 0;

        spRGBA *dim = const_cast<spRGBA *>(KeepLiveStill());
        set2DSpriteC4(Vif1Packet, CRect_i_(0, 0, 640, 512), dim, dim, dim, dim);
        if (++dims % 60 == 1) {
            std::printf("coop-townmenu: player 1's menu over the live town, dimmed (%d draws)\n", dims);
        }
        MenuOverWorldDepth(1); // coop-menudepth: the town's depth gone before his page's models
    }
    CountDraw(0, g_hidden_on);
    CountSaveDraw(0, g_hidden_on);
    g_drawing_his = true;
    BattleMenuDraw();
    g_drawing_his = false;
    HiddenEnd();
}

bool CoopP2TownMenuOn() {
    return HerMenuOpen();
}

// coop-localmenu: each player's own town menu as this copy runs it (players past 2 have none of their own yet).
bool CoopTownMenuRunsFor(int player) {
    const bool runs[2] = {CoopTownP1Menu() || g_his.on, g_own.on};

    return player >= 1 && player <= 2 && runs[player - 1];
}

bool CoopTownMenuRunsLocalHere(int player) {
    return player == 2 ? g_own.on && g_local_engine : player == 1 && g_his.on;
}

int CoopTownMenuCloseJump(int player) {
    return player == 2 ? g_close_jump + 1 : player == 1 ? g_his_close_code : 0;
}

void CoopTownMenuClosedByCommand(int player, int code, long long tick) {
    if (player == 1) {
        // coop-stage4 4b: his close on every copy, its tick: his map jump (and indoors the room's way out) for the town to take
        g_his_jump = (code & 0xFFFF) - 1;
        g_his_room_out = (code & kRoomOut) != 0;
        g_his_close_code = 0;
        if ((code & kClearOver) != 0 && EdCheckItemOver() != 0) {
            EdClearItemOverFlag();
            std::printf("coop-townmenu: his close clears the bag's overflow flag at link tick %lld\n", tick);
        }
        CoopWorldsBagDump("his shop over"); // (coop-bags: DC_WORLDS_TEST_BAGDUMP, x-town-storage reads the storage on both copies)
        std::printf("coop-townmenu: player 1's local menu close runs at link tick %lld (map jump %d%s)\n", tick, g_his_jump,
                    g_his_room_out ? ", out of the room" : "");
        return;
    }
    if (player != 2) {
        return;
    }
    g_let_go = tick; // coop-menucross: the close's press is the menu's on every copy
    CoopWorldsBagDump("her shop over"); // (DC_WORLDS_TEST_BAGDUMP; x-town-hershop reads it on both copies)
    if (code > 0) {
        g_own.jump = code - 1;
        std::fprintf(stderr, "coop-townmenu: player 2's menu leaves town for map %d (her close command at link tick %lld)\n", g_own.jump, tick);
        std::fflush(stderr);
    }
    std::printf("coop-townmenu: player 2's local menu close runs at link tick %lld (map jump %d)\n", tick, code - 1);
}

bool CoopTownP1MenuLocalOn() {
    return HisLocalOn() && Allowed();
}

bool CoopTownMenuLinked() {
    return Linked();
}

bool CoopTownP1MenuRunsLocal(int menu_no) {
    const bool retail_only = Mutated("retailmenu_lockstep");

    return CoopTownP1MenuLocalOn() && ((menu_no == ED_MENU_BATTLE && (EdCheckItemOver() == 0 || !retail_only)) || (IsShop(menu_no) && !retail_only));
}

bool CoopTownP1MenuOpensLocal(int menu_no) {
    // coop-p1parts: a keeper's talk of hers the merge carried: her shop, not his (its goto_menu runs in the town's own walk on every copy).
    if (IsShop(menu_no) && CoopTownSwapDeferred() && CoopTownHerShopOpens(menu_no)) {
        std::printf("coop-townmenu: the carried talk's shop is hers: it waits for the swap\n");
        return true;
    }
    const bool retail_only = Mutated("retailmenu_lockstep");

    if ((menu_no != ED_MENU_BATTLE && (!IsShop(menu_no) || retail_only)) || (EdCheckItemOver() != 0 && retail_only) || !HisLocalOn() || !Allowed() || PlayerInMenu(1)) {
        return false;
    }
    g_his.kind = menu_no;
    MGFlipWaitVSync(0); // (EdInitMenu's, for retail's menu mode: the town walks on)
    ActorMenuOpenHere(1, kActorMenuTown | kActorMenuLocal);
    g_his_bit_ticks = 0;
    g_his_close_code = 0;
    g_his_jump = -1;
    g_his_room_out = false;
    if (!HisHere()) {
        std::printf("coop-townmenu: player 1's menu opens on his own copy at link tick %lld\n", (long long) NetplayLinkTick());
        return true;
    }
    HisEngineStart(-1);
    return true;
}

static void HisEngineStart(int page) {
    WriteGuardMenu guard("his town menu open"); // coop-guard (a test's: DC_QA_WRITE_GUARD)
    SoundOwner    sound(1);
    DrawRng       rng;
    InputPadState pad;
#ifdef PAL
    int texture_block[6] = {16, 17, 18, 25, 32, 33}; // retail's (EdInitModeFinish)
#else
    int texture_block[5] = {16, 17, 18, 25, 32};
#endif

    NetplayPlayerPad(1, pad);
    g_his.pad = GamePad;
    g_his.pad.AutoRepeatOff();
    g_his.before = pad;
    FeedPadTo(g_his, pad); // (his Triangle no fresh press in the menu)
    FeedPadTo(g_his, pad);
    g_his.alloc = EdMenuBuffer; // the town menu allocator, as retail's (her engine's has its own buffer)
    if (OwnSet()) {
        MenuStateSave(g_his.state);
    }
    KeepStill(1); // the pages' still (retail's EdInitModeFinish keeps it)
    EngineIn(g_his, 1);
    FullMenuWritesBegin(true);
    GamePad.MenuModeOn(120);
    if (IsShop(g_his.kind)) {
        // retail's own opening, its four frames at once (the still kept above; menu_mode as EdInitMenu set it on every copy)
        for (int i = 0; i < 4; i++) {
            EdInitModeFinish(nullptr, nullptr);
        }
    } else {
        BattleMenuInit(texture_block, BATTLE_MENU_MODE_TOWN);
    }
    if (page >= 0) {
        MenuSelect[1] = page; // (coop-stage4 4c: a parked menu, on its page)
    }
    g_his.page = MenuSelect[1];
    FullMenuWritesEnd();
    EngineOut(g_his, 1);
    g_his.on = true;
    g_his.map = MapNo;
    g_his.ticks = 0;
    std::printf("coop-townmenu: player 1's menu opens at link tick %lld, its engine on this copy alone%s (menu %d, bag overflow %d)\n", (long long) NetplayLinkTick(),
                HerMenuOpen() ? ", beside player 2's" : "", g_his.kind, EdCheckItemOver() != 0 ? 1 : 0);
}

int CoopTownP1MenuTakeJump() {
    if (g_his_jump < 0 || g_his_room_out) {
        return -1;
    }
    const int map = g_his_jump;

    g_his_jump = -1;
    std::fprintf(stderr, "coop-townmenu: player 1's menu leaves town for map %d (his close command)\n", map);
    return map;
}

bool CoopTownP1MenuTakeRoomOut() {
    if (!g_his_room_out) {
        return false;
    }
    MenuMapJumpMode = g_his_jump; // (as retail's close leaves it for the town)
    SetInteriorOutFlag(1);
    std::fprintf(stderr, "coop-townmenu: player 1's menu leaves the room (map jump %d, his close command)\n", g_his_jump);
    g_his_room_out = false;
    g_his_jump = -1;
    return true;
}

bool CoopTownP1MenuLocal() {
    return HisLocalMenu();
}

// coop-stage4 4c (see g_reopen): this copy's player in retail's battle menu.
static bool RetailBattleMenu() {
    const bool open = EdInteriorFlag == 0 ? ::GameMode == ED_MODE_MENU_INIT || ::GameMode == ED_MODE_MENU : CoopTownIndoorP1Menu();

    if (Mutated("retailmenu_lockstep")) {
        return mode == kTownMode && open && g_p1_menu == ED_MENU_BATTLE && EdCheckItemOver() == 0;
    }
    return mode == kTownMode && open && (g_p1_menu == ED_MENU_BATTLE || IsShop(g_p1_menu)); // (coop-stage4 r6: the overflow's and a shop too)
}

bool CoopTownMenuCarries() {
    // (apart there is no linked partner yet, so not LocalOn(): its own conditions but that one)
    return Enabled() && MenusOn() && !Mutated("fullmenu_lockstep") && !Mutated("fullmenufreeze") && !Mutated("townmenu_lockstep") && !Mutated("p1townmenu_lockstep") &&
           !Mutated("merge_menu_retail") && RetailBattleMenu();
}

// The town (or the room) back in its walk under the menu, as retail's close leaves it.
static void RetailMenuLeave() {
    EdExitMenu();
    if (EdInteriorFlag == 0) {
        ::GameMode = ED_MODE_WALK;
        EdInitVillagerOnOff(EdVillager, EdVillagerInfo, pEditGround);
    } else {
        EdInMenuToWalk();
        CoopTownIndoorMenuLeft();
    }
}

void CoopTownMenuPark(int player) {
    if (!CoopTownMenuCarries() || player < 1 || player > 2) {
        return;
    }
    const bool built = EdInteriorFlag == 0 ? ::GameMode == ED_MODE_MENU : CoopTownRoomModeNow() == 7; // ED_IN_MODE_MENU

    if (IsShop(g_p1_menu)) {
        // coop-stage4 r6: an arriver's shop closes at the link (Jep 11-08: an arriver's menu may): its visit was in the world the set replaces.
        std::printf("coop-townmenu: player %d's shop closes for the landing (indoors %d)\n", player, EdInteriorFlag != 0 ? 1 : 0);
        if (built) {
            dc::quest::CustomShopEnd();
        }
        RetailMenuLeave();
        return;
    }
    g_reopen[player - 1] = built ? MenuSelect[1] : 0;
    std::printf("coop-townmenu: player %d's menu parks for the landing on page %d (indoors %d)\n", player, g_reopen[player - 1], EdInteriorFlag != 0 ? 1 : 0);
    RetailMenuLeave();
}

void CoopTownMenuRetailToLocal(int player) {
    if (!CoopTownMenuCarries() || player < 1 || player > 2) {
        return;
    }
    if (EdInteriorFlag == 0 ? ::GameMode != ED_MODE_MENU : CoopTownRoomModeNow() != 7) {
        CoopTownMenuPark(player); // (still building its first frames: nothing to carry yet, it opens as soon as it may)
        return;
    }

    Own &engine = player == 1 ? g_his : g_own;

    engine.kind = g_p1_menu; // (coop-stage4 r6: the battle menu or a shop)
    engine.pad = GamePad;
    NetplayMenuPad(player, engine.before);
    engine.alloc = EdMenuBuffer;
    if (OwnSet()) {
        MenuStateSave(engine.state);
    }
    engine.on = true;
    engine.map = MapNo;
    engine.ticks = 0;
    engine.page = MenuSelect[1];
    if (player == 1) {
        g_his_bit_ticks = 0;
        g_his_close_code = 0;
        g_his_jump = -1;
        g_his_room_out = false;
    } else {
        g_local_engine = true;
        g_bit_ticks = 0;
        g_close_jump = -1;
    }
    g_carried = true;
    ActorMenuOpenHere(player, kActorMenuTown | kActorMenuLocal);
    RetailMenuLeave();
    std::printf("coop-townmenu: player %d's menu goes on as a local menu through the merge, page %d, page state %d (read done %d)\n", player, engine.page, (int) BattleMenuFlag,
                BtlMenuReadEndFlag != 0 ? 1 : 0);
}

// coop-soakfix5 (the soak's apart seed 1269, copy A's crash 0xC0000005 in DrawPersonalBoard under CoopTownP1MenuDraw at link tick
// 34): his battle menu was open as her merge came, so it went on as his local engine (g_carried, "no session reset ends it": the
// sender's own checkpoint take reloads nothing). But this merge's copies were not alike and no catch-up could do: the host's
// resync (zone RESET) loaded the town again on his copy too, under the engine, and its first draw after the load read the
// menu's memory the load had replaced. A resync that loads the town is the taker's case on every copy: the carried menu parks
// and opens again on its page once landed (ReopenParked), and the session reset after this closes its engine.
// DC_COOP_MUTATE=resync_keeps_carried (twin): the carried menu goes on through the resync's load, as before.
void CoopTownMenuParkCarriedForResync() {
    if (!g_carried || Mutated("resync_keeps_carried")) {
        return;
    }
    g_carried = false;

    const int player = NetplayLocalPlayer();
    const Own &engine = player == 1 ? g_his : g_own;

    if (player >= 1 && player <= 2 && engine.on && engine.kind == ED_MENU_BATTLE) {
        g_reopen[player - 1] = engine.page >= 0 ? engine.page : 0;
        std::printf("coop-townmenu: the resync loads the town under player %d's menu carried through the merge: it parks for the landing on page %d\n",
                    player, g_reopen[player - 1]);
    } else {
        std::printf("coop-townmenu: the resync loads the town under the menu carried through the merge: it closes\n");
    }
}

bool CoopP2TownPaused() {
    return g_paused;
}

void CoopP2TownPauseForTest() {
    if (g_paused || HerMenuOpen()) {
        return;
    }
    g_paused = true;
    g_paused_ticks = 0;
    g_own.map = MapNo;
    std::fprintf(stderr, "coop-townmenu: test: player 2 paused at the split (the town goes on), town mode %d\n", (int) ::GameMode);
}

// coop-mergecam (w-town-apart-bags-new, nightly 2026-11-01: OUT OF SYNC at tick 120 after her merge, a resync at 245): her
// pause is this module's, not the game's, so a merge's state set left each copy its own. The copy that went into a building
// alone reset its session (her pause gone); hers kept her paused through the split until the merge ended it, and held her
// pad on the link tick it let go: her first step after the state set walked on his replay and stood on hers, and her place
// and camera parted from there. Her pause, its count, the tick it let go and her last buttons travel with the state set
// (an extra beside her town place): taken on the sender, put back on the taker. DC_COOP_MUTATE=town_pause_own (twin): each
// copy keeps its own, as before.
struct PauseSync {
    std::int32_t  paused;
    std::int32_t  paused_ticks;
    std::int32_t  map;
    std::int32_t  menu_on; // her own menu open as the set was taken (not carried: said, the taker's left as it is)
    std::int64_t  let_go;
    std::uint16_t prev;
};
PauseSync g_pause_sync;

unsigned char *CoopP2TownPauseBytes(std::size_t *size) {
    *size = sizeof g_pause_sync;
    return reinterpret_cast<unsigned char *>(&g_pause_sync);
}

void CoopP2TownPauseTake() {
    g_pause_sync = PauseSync{};
    g_pause_sync.paused = g_paused ? 1 : 0;
    g_pause_sync.paused_ticks = g_paused_ticks;
    g_pause_sync.map = g_own.map;
    g_pause_sync.menu_on = HerMenuOpen() ? 1 : 0;
    g_pause_sync.let_go = g_let_go;
    g_pause_sync.prev = g_own.prev;
}

void CoopP2TownPauseApply() {
    if (Mutated("town_pause_own")) {
        return;
    }
    if (g_pause_sync.paused != (g_paused ? 1 : 0) || g_pause_sync.let_go != g_let_go) {
        std::fprintf(stderr, "coop-townmenu: her pause from the state set: paused %d (here %d), let go at %lld (here %lld)\n",
                     (int) g_pause_sync.paused, g_paused ? 1 : 0, (long long) g_pause_sync.let_go, g_let_go);
    }
    if (g_pause_sync.menu_on != 0) {
        std::fprintf(stderr, "coop-townmenu: the sender's state set was taken with her menu open (not carried)\n");
    }
    g_paused = g_pause_sync.paused != 0;
    g_paused_ticks = g_pause_sync.paused_ticks;
    g_own.map = g_pause_sync.map;
    g_let_go = g_pause_sync.let_go;
    g_own.prev = g_pause_sync.prev;
}

// coop-p1parts (Jep 11-08): her talk to a keeper opens her own shop, on her copy, as his does (4b/r6): the bit on every copy on one tick, retail's ShopNoInput and
// CommonShopLoop on her copy alone in her engine (its buffer and blocks, her member, her bag and purse in), its writes pokes, its close the command; the world runs on
// for him and he may shop at the same time. Her talk ended on every copy on one tick, so the opening is due on every copy alike. A talk she started apart that a merge
// carried is retail's on every copy till the swap runs (its goto_menu: CoopTownP1MenuOpensLocal), so it waits for that (her bag home, she the partner).
// DC_COOP_MUTATE=hershop_off (twin): none, as before.
static int g_her_shop = 0;

bool CoopTownHerShopOpens(int kind) {
    if (!LocalOn() || Mutated("hershop_off") || Mutated("retailmenu_lockstep")) {
        return false;
    }
    g_her_shop = kind;
    return true;
}

static bool HerShopDue(const InputPadState &pad) {
    if (g_her_shop == 0) {
        return false;
    }
    if (!Linked()) {
        std::printf("coop-townmenu: player 2's shop is dropped (the town changed)\n");
        g_her_shop = 0;
        return false;
    }
    if (CoopTownSwapDeferred() || CoopWorldsApart() || !Allowed() || HerMenuOpen() || g_paused) {
        return false;
    }
    const int kind = g_her_shop;

    g_her_shop = 0;
    ActorMenuOpenHere(2, kActorMenuTown | kActorMenuLocal);
    g_bit_ticks = 0;
    g_own.map = MapNo;
    g_close_jump = -1;
    std::fprintf(stderr, "coop-townmenu: player 2's shop opens on her own copy at link tick %lld (menu %d)\n", (long long) NetplayLinkTick(), kind);
    if (NetplayLocalPlayer() == 2) {
        HerEngineStart(pad, true, false, -1, kind);
    }
    return true;
}

static void HerEngineStart(const InputPadState &pad, bool local, bool beside, int page, int kind) {
    WriteGuardMenu         guard("her town menu open", local); // coop-guard (a test's: DC_QA_WRITE_GUARD)
    std::optional<DrawRng> rng;

    if (local) {
        rng.emplace();
    }
    int        count = 0;
    const int *own = MenuSecondBlocks(&count);
    int        texture_block[6] = {};

    std::copy(own, own + count, texture_block);
    g_own.kind = kind;
    // Her pad: the game pad's copy, its repeats off, her pad in it as now and as before (her Triangle no fresh
    // press in the menu); the town menu allocator over her own buffer; her globals start as the ones in place.
    g_own.pad = GamePad;
    g_own.pad.AutoRepeatOff();
    g_own.before = pad;
    FeedPad(pad);
    FeedPad(pad);
    g_own.buffer = MenuSecondBuffer();
    g_own.alloc = EdMenuBuffer;
    g_own.alloc.base = reinterpret_cast<u_char *>(MenuCalcBufAlignment(g_own.buffer));
    g_own.alloc.used = 0;
    if (OwnSet()) {
        MenuStateSave(g_own.state);
    }
    HerIn();
    if (local) {
        FullMenuWritesBegin(true);
    }
    GamePad.MenuModeOn(120);
    if (IsShop(kind)) {
        ShopNoInput(texture_block, EdGetShopNo(), 1); // (coop-p1parts: retail's EdInitModeFinish's own call for a shop)
    } else {
        BattleMenuInit(texture_block, BATTLE_MENU_MODE_TOWN);
    }
    if (page >= 0) {
        MenuSelect[1] = page; // (coop-stage4 4c: a parked menu, on its page)
    }
    g_own.page = MenuSelect[1];
    if (local) {
        FullMenuWritesEnd();
    }
    HerOut();
    KeepStill();
    g_own.on = true;
    g_local_engine = local;
    g_own.map = MapNo;
    g_own.ticks = 0;
    // The open sound retail plays when Toan's Triangle opens his menu (editloop's walk); her open skipped it.
    if (!Mutated("townmenu_nosound")) {
        SndSePlay(MENU_SOUND_CONFIRM, -1, 0);
        std::printf("coop-townmenu: player 2's menu open sound played\n");
    }
    // (coop-atlamenu: and its link tick, and whether the pair was still apart in a catch-up, the tests' word of when)
    std::printf("coop-townmenu: player 2's menu opens (member %d), Toan stays player 1's%s at link tick %lld%s%s\n", PartnerMember(),
                beside ? ", beside player 1's" : "", (long long) NetplayLinkTick(), CoopWorldsApart() ? ", apart in a catch-up" : "",
                local ? ", its engine on this copy alone" : "");
}

// Her step, on her own pad and menu; all back after.
static void HerEngineStep(const InputPadState &agreed) {
    InputPadState pad = agreed;

    NetplayMenuPad(2, pad); // (coop-stage4 4c: through a merge's own ticks, her own controller)
    g_own.ticks++;

    WriteGuardMenu         guard("her town menu step", g_local_engine); // coop-guard (a test's: DC_QA_WRITE_GUARD)
    std::optional<DrawRng> rng;

    if (g_local_engine) {
        rng.emplace();
    }
    FeedPad(pad);
    if (IsShop(g_own.kind)) {
        return; // (coop-stage4 r6: carried through her merge; its one call runs in her draw)
    }
    HerIn();
    FullMenuStepping(2); // coop-quitown: her menu's Quit Game is hers
    if (g_local_engine) {
        FullMenuWritesBegin(true);
    }
    int open = BattleMenuCursor();
    if (g_local_engine) {
        FullMenuWritesEnd();
    }
    FullMenuStepping(0);
    int page = MenuSelect[1];
    int jump = MenuMapJumpMode; // her menu's own (its globals are hers till HerOut)
    NoteSaveScreen(2);
    HerOut();

    if (page != g_own.page) {
        std::printf("coop-townmenu: player 2's menu at page %d (was %d) after %d ticks\n", page, g_own.page, g_own.ticks);
        g_own.page = page;
    }

    if (CoopDebug() && g_own.ticks % 30 == 1) {
        float at[4] = {0.0f, 0.0f, 0.0f, 1.0f};

        TownPartnerShown(at, nullptr);
        std::printf("coop-townmenu: player 2's menu open %d ticks, player 2 at (%.1f %.1f)%s\n", g_own.ticks, at[0], at[2],
                    CoopTownP1Menu() || HisLocalMenu() ? ", player 1's menu open too" : "");
    }

    if (open == 0) {
        const bool local = g_local_engine;

        End("closed");
        if (local) {
            // its close command (ActorMenuSync, next tick) carries the jump to every copy
            if (jump >= 0) {
                std::fprintf(stderr, "coop-townmenu: player 2's menu picks map %d on her copy\n", jump);
            }
            g_close_jump = jump >= 0 && !Mutated("p2menu_nojump") ? jump : -1;
            return;
        }

        // Her menu's way out of town (its "go to the dungeon", or a world map's place): she takes it, as his menu's
        // takes him (editloop's TownMapJump). DC_COOP_MUTATE=p2menu_nojump (twin): dropped, as before (Jep's
        // 2026-10-24 playtest: "xiao can't enter the dungeon with the menu option").
        if (jump >= 0 && !Mutated("p2menu_nojump")) {
            g_own.jump = jump;
            std::fprintf(stderr, "coop-townmenu: player 2's menu leaves town for map %d\n", g_own.jump);
            std::fflush(stderr);
        }
    }
}

bool CoopP2TownMenuHoldsPad() {
    return HerMenuOpen() || g_paused || g_let_go == NetplayLinkTick();
}

// coop-stage4 4c: a parked menu opens again as soon as this copy has landed and its player stands in the town's walk. Only this copy
// runs it, so the bit comes by its command (ActorMenuSync), as any local engine's change does.
static void ReopenParked() {
    const int player = NetplayLocalPlayer();

    if (player < 1 || player > 2 || g_reopen[player - 1] < 0 || !Allowed() || !TownWalks() || CoopWorldsApart() || PlayerInMenu(player)) {
        return;
    }
    const int page = g_reopen[player - 1];

    g_reopen[player - 1] = -1;
    std::printf("coop-townmenu: player %d's parked menu opens again on page %d at link tick %lld, landed\n", player, page, (long long) NetplayLinkTick());
    if (player == 1) {
        g_his_bit_ticks = 0;
        g_his_close_code = 0;
        g_his_jump = -1;
        g_his_room_out = false;
        g_his.kind = ED_MENU_BATTLE;
        HisEngineStart(page);
        return;
    }

    InputPadState pad;

    g_close_jump = -1;
    if (NetplayPlayerPad(2, pad)) {
        HerEngineStart(pad, true, false, page);
    }
}

// Test hook (DC_COOP_TEST_TOWN_RETAILMENU, both copies alike): at link tick T, in the walk, what retail itself would open:
// =over:T the bag's overflow flag (an item past the bag); =shop:N:T shop N, as its keeper's talk ends with goto_menu; apart prefix
// (apartover:0, apartshop:N:0): on this copy once apart (the merge's rows).
static bool g_room_shop_test = false;

bool CoopTownRoomShopTestOpen() {
    const bool open = g_room_shop_test;

    g_room_shop_test = false;
    return open;
}

static void TestRetailMenu() {
    COOP_TEST_SWITCH(const char *, spec, TestEnv("DC_COOP_TEST_TOWN_RETAILMENU"));
    static bool done = false;

    COOP_TEST_TRACE_RESET(done, false);
    if (spec == nullptr || done || mode != kTownMode || !TownWalks()) {
        return;
    }
    const bool  apart = std::strncmp(spec, "apart", 5) == 0;
    const char *what = apart ? spec + 5 : spec;
    int         shop = 0, at = 0;
    const bool  over = std::sscanf(what, "over:%d", &at) == 1;
    const bool  beside = std::strncmp(what, "beside", 6) == 0; // (coop-p1parts: besideshop:N:T: his shop once her menu is open, x-town-hershop)

    if (beside) {
        what += 6;
        if (!PlayerInMenu(2, kActorMenuTown)) {
            return;
        }
    }

    if (!over && std::sscanf(what, "shop:%d:%d", &shop, &at) != 2) {
        return;
    }
    if (apart ? !CoopWorldsApart() : !Linked() || CoopWorldsApart() || NetplayLinkTick() < at) {
        return;
    }
    done = true;
    if (over) {
        SaveData->GetDngStatus()->overflow_flag = true;
    } else {
        EdSetShopNo(shop);
        if (EdInteriorFlag != 0) {
            g_room_shop_test = true; // (the room's own goto_menu, its step: CoopTownRoomShopTestOpen)
        } else {
            goto_menu = ED_MENU_SHOP;
        }
    }
    std::printf("coop-townmenu: test: retail opens %s at link tick %lld (%s)\n", over ? "the bag's overflow" : "a shop", (long long) NetplayLinkTick(), apart ? "apart" : "linked");
}

void CoopP2TownMenuTick() {
    if (g_carried && !MergeUnderWay()) {
        g_carried = false;
    }
    TestRetailMenu();
    CoopTownSwapDeferTick(); // coop-stage4 4e part 2
    ReopenParked();
    HisTick(); // coop-stage4 4b: his local menu (every copy its bit, his its engine)

    SoundOwner    sound(2); // coop-menuse: her menu's and her pause's sounds are hers
    InputPadState pad;

    if (!NetplayPlayerPad(2, pad)) {
        if (g_own.on) {
            End("no player 2");
        }
        return;
    }
    if (HerShopDue(pad)) { // coop-p1parts
        g_own.before = pad;
        return;
    }

    std::uint16_t pressed = pad.buttons & ~g_own.prev;

    g_own.prev = pad.buttons;
    g_bit_ticks = PlayerInMenu(2, kActorMenuTown) ? g_bit_ticks + 1 : 0;

    if (g_own.on && ((!Allowed() && !(g_local_engine && MergeUnderWay())) || MapNo != g_own.map)) {
        End("the town changed");
        return;
    }

    // Her pause: Start in her walk (not in her menu or her Georama), Start again to end it.
    if (g_paused) {
        g_paused_ticks++;
        if (!Allowed() || MapNo != g_own.map || CoopP2Building() || (pressed & kInputStart) != 0) {
            g_paused = false;
            g_let_go = NetplayLinkTick(); // coop-menucross
            std::printf("coop-townmenu: player 2 resumes after %d ticks%s\n", g_paused_ticks, (pressed & kInputStart) != 0 ? "" : " (the town changed)");
        } else if (CoopDebug() && g_paused_ticks % 30 == 1) {
            float at[4] = {0.0f, 0.0f, 0.0f, 1.0f};

            TownPartnerShown(at, nullptr);
            std::printf("coop-townmenu: player 2 paused %d ticks, player 2 at (%.1f %.1f)\n", g_paused_ticks, at[0], at[2]);
        }
        g_own.before = pad;
        return;
    }
    if (!HerMenuOpen() && (pressed & kInputStart) != 0 && Allowed() && !Mutated("townstart_handover") && !CoopP2Building() && change_time_event == 0 &&
        (TownWalks() || CoopTownP1Menu() || ::GameMode == ED_MODE_RETURN_MENU_WALK || HisGeorama())) { // coop-inbuild: TownWalks
        g_paused = true;
        g_paused_ticks = 0;
        g_own.map = MapNo;
        SndSePlay(MENU_SOUND_CONFIRM, -1, 0);
        std::printf("coop-townmenu: player 2 paused (the town goes on), town mode %d\n", (int) ::GameMode);
        g_own.before = pad;
        return;
    }

    if (!HerMenuOpen()) {
        // In the walk, or beside his menu once it has been open half a second.
        // coop-inbuild: indoors, the room's walk and its menu mode.
        const bool walk = TownWalks();
        const bool his_menu_runs = EdInteriorFlag == 0 ? ::GameMode == ED_MODE_MENU : CoopTownIndoorP1Menu();
        const bool beside = (CoopTownP1Menu() && his_menu_runs && BesideOk(true, g_p1_ticks)) || HisLocalMenu(); // (coop-stage4 4b: or his local one)

        if ((pressed & kInputTriangle) != 0 && Allowed() && (walk || beside) && !CoopP2Building() && change_time_event == 0 &&
            !(HisLocalMenu() && Mutated("townmenuboth_off"))) { // (coop-stage4 4b: the twin's one at a time beside his local menu too)
            // coop-stage4: local, the bit on every copy now and the engine on hers alone.
            const bool local = LocalOn();

            if (local) {
                ActorMenuOpenHere(2, kActorMenuTown | kActorMenuLocal);
                g_bit_ticks = 0;
                g_own.map = MapNo;
                g_close_jump = -1;
                if (NetplayLocalPlayer() != 2) {
                    std::printf("coop-townmenu: player 2's menu opens on her own copy (member %d)%s at link tick %lld%s\n", PartnerMember(),
                                beside ? ", beside player 1's" : "", (long long) NetplayLinkTick(), CoopWorldsApart() ? ", apart in a catch-up" : "");
                    g_own.before = pad;
                    return;
                }
            }
            HerEngineStart(pad, local, beside, -1);
        }
        g_own.before = pad;
        return;
    }

    if (!g_own.on) {
        g_own.before = pad; // (local: her bit set on a copy with no engine of hers, or her close not applied yet)
        if (CoopDebug() && g_bit_ticks % 30 == 1) { // (her stand as this copy sees it, the line her engine's copy logs)
            float at[4] = {0.0f, 0.0f, 0.0f, 1.0f};

            TownPartnerShown(at, nullptr);
            float toan[4] = {0.0f, 0.0f, 0.0f, 1.0f};

            if (EdInteriorFlag != 0 ? EdExchangeInfo.player != nullptr : Chara != nullptr) {
                (EdInteriorFlag != 0 ? EdExchangeInfo.player : Chara)->GetPosition(toan); // (coop-p1parts: the world runs on under hers)
            }
            std::printf("coop-townmenu: player 2's menu open %d ticks, player 2 at (%.1f %.1f)%s, Toan at (%.1f %.1f)\n", g_bit_ticks, at[0], at[2],
                        CoopTownP1Menu() || HisLocalMenu() ? ", player 1's menu open too" : "", toan[0], toan[2]);
        }
        return;
    }

    HerEngineStep(pad);
}

int CoopP2TownMenuTakeJump() {
    const int map = MapNo == g_own.map ? g_own.jump : -1; // a town left meanwhile drops it

    g_own.jump = -1;
    return map;
}

// Her pause on her copy: the town's own (editloop's: the screen's dim and the "pause" picture where retail puts it).
static void DrawPause() {
    if (!g_paused || NetplayLocalPlayer() != 2) {
        return;
    }

    char      name[] = "pause";
    CTexture *pause = TexManager.GetTexture(name, -1);

    if (pause == nullptr) {
        return;
    }
    MGFillBox(CRect_i_(0, 0, 0x2800, SCREEN_HALF_HEIGHT << 4), 0, 0, 0, 0x40);
    setbilinear(0);
    set2DSprite(GetVif1Packet(), pause, CRect_i_(0x100, 0xdc, 0x80, 0x28), CRect_i_(0, 0, 0x80, 0x28), 0x80);

    static int draws = 0;

    if (++draws % 60 == 1) {
        std::printf("coop-townmenu: player 2's copy draws her pause (%d draws)\n", draws);
    }
}

void CoopP2TownMenuDraw() {
    CoopTownCaveDraw(); // coop-doorfix: the user's own screen fades as they walk into the cave
    // coop-gatepopup3: an apart call's question and the asker's waiting box, with the town's late 2D (over the characters).
    if (!Mutated("call_under_world")) {
        DrawCallQuestion();

        // (once a call: the test's line that it went with the late 2D, after the town's characters)
        static bool said = false;
        COOP_TEST_TRACE_RESET(said, false);
        int         from = 0, map = 0, to = 0, floor = 0, cursor = 0;
        const bool  asked = CoopWorldsCallQuestion(&from, &map, &to, &floor, &cursor);

        if (asked && !said) {
            std::fprintf(stderr, "coop: the call's question drawn with the town's late 2D, over the characters\n");
            std::fflush(stderr);
        }
        said = asked;
    }

    SoundOwner sound(2); // coop-menuse

    DrawPause();
    if (!g_own.on) {
        return;
    }

    bool hers = NetplayLocalPlayer() == 2;

    if (IsShop(g_own.kind)) { // coop-stage4 r6: her shop carried through her merge
        g_drawing_hers = true;
        const bool closed = ShopCall(g_own, 2);
        g_drawing_hers = false;
        if (closed) {
            End("closed");
            g_close_jump = -1;
        }
        return;
    }
    if (!hers) {
        HiddenBegin();
    } else if (!Mutated("townmenu_nodim")) {
        // The town under her menu dimmed, as the floor's under a menu beside the world (coop_fullmenu's Draw).
        static int          dims = 0;

        spRGBA *dim = const_cast<spRGBA *>(KeepLiveStill());
        set2DSpriteC4(Vif1Packet, CRect_i_(0, 0, 640, 512), dim, dim, dim, dim);
        if (++dims % 60 == 1) {
            std::printf("coop-townmenu: the town dimmed under player 2's menu (%d draws)\n", dims);
        }
    }
    CountDraw(1, g_hidden_on);
    g_drawing_hers = true;
    WriteGuardMenu         guard("her town menu draw", g_local_engine); // coop-guard (a test's: DC_QA_WRITE_GUARD)
    std::optional<DrawRng> rng;

    if (g_local_engine) {
        rng.emplace();
    }
    // coop-guard (the write guard's town row, her menu: mgRenderInfo+0x10 on her copy alone): her menu's draw sets its own projection
    // and view (MenuWorldTrans) in the render info the next tick's clip tests read in a room, as his did before HisDraw put the
    // room's back (hisdraw_view). Hers is put back the same way. DC_COOP_MUTATE=herdraw_view (twin): left, as before.
    const auto render_info = mgRenderInfo;

    HerIn();
    if (g_local_engine) {
        FullMenuWritesBegin(false); // the draw shows her menu's own edits too
    }
    if (hers) {
        MenuOverWorldDepth(2); // coop-menudepth (as his, CoopTownP1MenuDraw)
    }
    CountSaveDraw(1, g_hidden_on);
    BattleMenuDraw();
    if (g_local_engine) {
        FullMenuWritesEnd();
    }
    HerOut();
    if (!Mutated("herdraw_view")) {
        mgRenderInfo = render_info;
    }
    g_drawing_hers = false;
    HiddenEnd();
}

bool CoopMenuLoadAllowed() {
    if (!g_in_hers || !Linked() || Mutated("saveload_p2load")) {
        return true;
    }

    std::printf("coop-saveload: player 2's Load refused in the linked town\n");
    if (NetplayLocalPlayer() == 2) {
        NetplayShowNotice("Only the host can load a save");
    }
    return false;
}

void CoopMenuLoaded() {
    // Apart (her copy in its own world, no lockstep): she hears it; the merge takes both copies to his.
    if (Enabled() && CoopWorldsApart()) {
        std::fprintf(stderr, "coop-saveload: player 1's load done while apart (file %d)\n", (int) ::SaveMenu.file_no);
        CoopWorldsLoaded(::SaveMenu.file_no);
        return;
    }

    if (!Linked()) {
        return;
    }

    std::printf("coop-saveload: player %d's load done%s\n", g_in_hers ? 2 : 1, g_own.on ? ", player 2's menu open" : "");
    if (!g_in_hers) {
        NetplayMenuLoadResync();
    }
}

void CoopTownMenuClosedByLoad() {
    // What she was doing as the load's resync came (the resync then resets every module: her Georama, her pause).
    const char *was = g_own.on ? "in her menu" : CoopP2Building() ? "in her Georama" : g_paused ? "paused" : mode == kTownMode ? "in town" : "elsewhere";

    std::fprintf(stderr, "coop-saveload: player 1's load reaches player 2 %s (mode %d)\n", was, (int) mode);
    if (g_own.on) {
        End("player 1 loaded a save");
    }
    if (g_his.on) {
        HisEnd("player 1 loaded a save"); // coop-stage4 4b
    }
    if (NetplayLocalPlayer() == 2 && !Mutated("saveload_nonotice")) {
        NetplayShowNotice("Player 1 loaded a save");
        std::printf("coop-saveload: notice on player 2's copy: player 1 loaded a save\n");
    }
}

void CoopTownAfterLoad() {
    std::fprintf(stderr, "coop-saveload: after the load player 2's menu %d Georama %d paused %d\n", g_own.on ? 1 : 0, CoopP2Building() ? 1 : 0,
                 g_paused ? 1 : 0);
}

void CoopP2TownMenuResetForSession() {
    // coop-stage4 4c: a menu carried through a merge is its copy's own, not the session's (g_reopen likewise lives through the landing).
    if (g_carried) {
        std::fprintf(stderr, "coop-townmenu: the session reset leaves the menu carried through the merge\n");
        return;
    }
    g_own.on = false;
    g_local_engine = false;
    g_bit_ticks = 0;
    g_close_jump = -1;
    g_own.ticks = 0;
    g_own.prev = 0;
    g_paused = false;
    g_let_go = -1;
    g_own.state.clear();
    g_own.aside.clear();
    g_p1_menu = -1;
    g_p1_ticks = 0;
    g_his.on = false; // coop-stage4 4b
    g_his.state.clear();
    g_his.aside.clear();
    g_his_bit_ticks = 0;
    g_his_close_code = 0;
    g_his_jump = -1;
    g_his_room_out = false;
}
