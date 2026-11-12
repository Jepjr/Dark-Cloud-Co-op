#include "coop/game_ns.hpp"
#include "coop/coop_opener.hpp" // coop-opt7
#include "coop/coop_fullmenu.hpp"
#include "coop/coop_scriptmenu.hpp" // coop-scriptmenus
#include "coop/coop_storage.hpp"    // coop-small2
#include "coop/coop_writeguard.hpp" // coop-guard
#include "coop/coop_townmenu.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "battlemenu.hpp"
#include "btitem.hpp" // coop-menuleak: the item select beside the menu
#include "coop/coop_worlds.hpp" // coop-dungeonp2: CoopWorldsExitBy
#include "btactstatus.hpp"
#include "dun/gameloop.hpp"
#include "camerafollow.hpp"
#include "gamepad.hpp"
#include "gamepad.hpp"
#include "gfx/gfx.hpp"

#include "coop/coop.hpp"
#include "coop/coop_actor.hpp" // coop-localmenu: the in-menu bits
#include "coop/coop_camera.hpp" // coop-localmenu: DrawRng
#include "coop/coop_audio.hpp"
#include "coop/coop_core.hpp"
#include "coop/coop_inventory.hpp" // coop-bagsendlocal: InventoryApplyGive
#include "coop/coop_menu.hpp"
#include "coop/coop_pause.hpp"
#include "coop/coop_party.hpp"
#include "itemdata.hpp" // coop-localmenu: the equip hook's Baselard
#include "coop/coop_prompt.hpp"
#include "coop/coop_wheel.hpp" // coop-wheelcrash
#include "coop/snapshot_state.hpp" // coop-offthread: SnapshotStateHolds
#include "savedata.hpp" // coop-stage4: the town's party status
#include "editloop.hpp" // coop-pt1109a: EditSave
#include "userstatus.hpp"
#include "mglib.hpp"
#include "camera.hpp"  // coop-stabfix2: MenuCamera
#include "menuetc.hpp" // coop-stabfix2: MenuCamera
#include "rect.hpp"
#include "snd.hpp"
#include "coop/netplay.hpp"
#include "coop/netplay_cmd.hpp" // coop-localmenu 3c: the menu's writes as commands
#include "coop/coop_georama.hpp" // coop-stage4: her parts menu close command
#include "coop/coop_town.hpp"    // coop-p1parts: her town member (ApplyPoke)

#include <cstring>
#include <optional>
#include <vector>

#include "clsmes.hpp"
#include "dataread.hpp"
#include "memcard.hpp"
#include "memorycardaccess.hpp"
#include "menu_save.hpp"
#include "menu_draw.hpp"
#include "menu_misc.hpp"
#include "menu_save.hpp" // coop-menuleak: InitEventItemSelect, EventItemSelectLoop
#include "menuiconautoget.hpp"
#include "menuitemstep.hpp"
#include "texture.hpp"
#include "weaponlevelup.hpp"
#include "platform/arena.hpp"

namespace dc::game {
extern s8              MenuTrushMark[100];
extern CUserStatus    *PerBoardStatusPt;
extern PERSONAL_BOARD *PerBoardPt;

extern s32 gameTask;
} // namespace dc::game
namespace dc::battlemenu {
// coop-saveload: the ring's choosers and its load under way (port/src/battlemenu/main_ring.cpp).
void BattleMenuRingRegions(void (*add)(void *at, std::size_t size));
void BattleMenuRingChoosersOff(); // coop-pt1109d
} // namespace dc::battlemenu
using dc::battlemenu::BattleMenuRingChoosersOff;
using dc::battlemenu::BattleMenuRingRegions;
namespace dc::game {
// coop-invcrash: the weapons' .chr addresses (port/src/menu_misc.cpp) and the file read slots (port/src/dataread.cpp).
void MenuWeaponFileRegions(void (*add)(void *at, std::size_t size));
void ReadBGRegions(void (*add)(void *at, std::size_t size));
extern s32 oldUnitNow; // coop-localmenu: the member when the menu opened (dun/gameloop.cpp)
extern s32 rogoY3; // the dungeon HUD's slide (dun/gameloop.cpp draws it from this)
extern s32 oldRogoY3; // coop-hudblink: and where it was before the menu's opening slid it away
// The dungeon's MoveChara and MainDraw (port/include/stubs/dun/gameloop.hpp: editloop.cpp has its own).
void DunMoveChara();
void DunMainDraw();

extern s32 mode; // coop-quitown: a town's (2, GAME_MODE_EDIT) own menu is player 1's
extern s32 infoMap;    // coop-menuflash: the dungeon's mini-map switch (dun/gameloop.cpp)
extern s32 infoMapOld; // and what it was before SetMIniMapStatus(0) hid it
// repin: menu_misc.cpp's menu globals, out of its anonymous namespace for the regions below (recipe 0032).
extern float           MenuCharaOldAmbient[4];
extern int             MenuItemUseVolume;
extern u_long128      *WeaponRead_Buf;
extern s16             MenuCharaEffectReadFlag;
extern s16             MenuCharaOldEffect;
extern BT_SHOT_EFFECT *WepEffectMenuPt;
extern int             CharaFileBGReadNo;
extern u_long128      *CharaChangeBaseBuf;
extern u_long128      *menucharReadbuf;
extern u_long128      *menud0wepReadBuf;
extern u_long128      *menud1wepReadBuf;
extern u_long128      *menud2wepReadBuf;
extern u_long128      *MenuWepIconCharaChangePtr;
extern u_long128      *MenuVoiceLoadPtr;
extern s16             CharaNameDrawFlag;
extern s16             CharaNameDrawCase;
} // namespace dc::game

namespace coop {

namespace {

void MenuPackTrace(); // coop-chestmenu's test hook (below), from the local menu's loop too

struct FullMenu {
    bool live = false;
    int  owner = 0; // the player whose pad steps the menu (1 for now)
    int  ticks = 0;
    int  task_logged = -1;
    // Player 2's menu: the menu steps and draws with her member and bag in the party status.
    int  player_chara = -1;
    bool bag_swapped = false;
    InputPadState p1_before{};
    bool view_logged = false;
    bool opening = false; // coop-menuleak: the opening tick draws the world (FullMenuBegin)
};

FullMenu g_full;

gfx::TextureHandle g_hidden = gfx::kNullTexture;

// ---- the second menu (Jep, 2026-10-24: both players in the game menu at once) ----------------------------------------
// The game has one menu, kept in globals. While one player's is open (the first, which the game opened its own way),
// the other's Triangle opens a second: every global the menu keeps (the battle menu's, its windows, its model and
// weapon pictures, menu_misc's and menu_draw's) is copied aside per menu and put back around each menu's step and
// draw, as the Georama editor is. The second menu has texture blocks and a work buffer of its own (its pictures and
// models never land on the first's), its own game pad (repeat and menu mode), and its owner's pad on port 1 for its
// step. Both copies step both menus from the agreed pads; each copy shows only its own player's. The link sends only
// pads, so nothing more goes over the wire.
// DC_COOP_MUTATE=menuboth_shared: the second menu runs on the first's globals, not its own (twin of t-triangle-both).
struct Region {
    void       *at;
    std::size_t size;
};

#define COOP_MENU_REGION(v) Region{(void *) &(v), sizeof(v)}

const std::vector<Region> &MenuRegions() {
    static const std::vector<Region> regions = {
        // battlemenu.cpp
        COOP_MENU_REGION(MenuMes), COOP_MENU_REGION(MenuExCashBuffer), COOP_MENU_REGION(NorMenuIcon), COOP_MENU_REGION(SysCur),
        COOP_MENU_REGION(MenuChara), COOP_MENU_REGION(MenuCharaFrame), Region{(void *) DngWeaponFrm, sizeof(CCharacter) * 12},
        COOP_MENU_REGION(WepMenu), COOP_MENU_REGION(ItemMenuMode), COOP_MENU_REGION(IconAutoGet), COOP_MENU_REGION(MenuMove),
        COOP_MENU_REGION(SysChara), COOP_MENU_REGION(BattleMenuFlag), COOP_MENU_REGION(chara_r_long), COOP_MENU_REGION(BtlEffectFlag),
        COOP_MENU_REGION(MenuMapJumpMode), COOP_MENU_REGION(BtlMenuSaveDataPt), COOP_MENU_REGION(BtlMenuStatusPt),
        COOP_MENU_REGION(BtlMenuMode), COOP_MENU_REGION(BtlMenuBGMvol), COOP_MENU_REGION(BtlMenuReadBlock), COOP_MENU_REGION(BtlMenuReadBuf),
        COOP_MENU_REGION(BtlMenuReadEndFlag), COOP_MENU_REGION(ItemMenuWeaponIconReadBlock), COOP_MENU_REGION(ItemMenuAlreadyReadWepIconTexFlag),
        COOP_MENU_REGION(MenuShadowReadBlock),
#ifdef PAL
        COOP_MENU_REGION(BtlMenuExtBlock), COOP_MENU_REGION(BtlEuro),
#endif
        COOP_MENU_REGION(BtlMenuExReadBlock), COOP_MENU_REGION(BtlMenuExReadFlag), COOP_MENU_REGION(MenuExtendReadBlock),
        COOP_MENU_REGION(MenuExTextureReadFlag), COOP_MENU_REGION(ItemMenuCharaReadBuf), COOP_MENU_REGION(BtlMDSBuildCnt),
        COOP_MENU_REGION(BtlMenuManualReadBlock), COOP_MENU_REGION(DngWepHavePt), COOP_MENU_REGION(WepMenuEffectReadBuf),
        COOP_MENU_REGION(CharaStatus), COOP_MENU_REGION(MenuCharaFace), COOP_MENU_REGION(NonCharaFace), COOP_MENU_REGION(BtStatus),
        COOP_MENU_REGION(WepStatus), COOP_MENU_REGION(BtlAlpha), COOP_MENU_REGION(BtlHira), COOP_MENU_REGION(BtlKata),
        COOP_MENU_REGION(HelpWinHead), COOP_MENU_REGION(BtlWakuMake2), COOP_MENU_REGION(BtlHelpWinAlpha), COOP_MENU_REGION(BtlHelpWinW),
        COOP_MENU_REGION(BtlHelpWinH), COOP_MENU_REGION(MenuSelect), COOP_MENU_REGION(MenuItemPackPt), COOP_MENU_REGION(EscapeDngFlg),
        COOP_MENU_REGION(RoomOutFlag), COOP_MENU_REGION(MenuCharaMove), COOP_MENU_REGION(PosAngle), COOP_MENU_REGION(BtlHaveItemPt),
        COOP_MENU_REGION(WepFrameRate), COOP_MENU_REGION(WeaponPos), COOP_MENU_REGION(WepPolyPos), COOP_MENU_REGION(BtlEffectCt),
        COOP_MENU_REGION(MenuWarningMsgFlag), COOP_MENU_REGION(BtlMenuCharaChangeBuf), COOP_MENU_REGION(WeaponMenuSelectKeyLockFlag),
        COOP_MENU_REGION(mapo), COOP_MENU_REGION(mapmovev), COOP_MENU_REGION(NextWorldPos), COOP_MENU_REGION(MapMoveCursor),
        COOP_MENU_REGION(MenuMoveTex),
        // the menu's windows, icons and effects (clsmes, memcard, weaponlevelup, gameloop)
        COOP_MENU_REGION(CommonMenuMes1), COOP_MENU_REGION(CommonMenuMes2), COOP_MENU_REGION(CommonMenuMes3), COOP_MENU_REGION(WepIcon),
        COOP_MENU_REGION(ItemIcon), COOP_MENU_REGION(ItemIcon2), COOP_MENU_REGION(AttachIcon), COOP_MENU_REGION(StayTex),
        COOP_MENU_REGION(CursorVibeCnt), COOP_MENU_REGION(MenuWepLevelUp),
        // menu_misc.cpp and menu_draw.cpp
        COOP_MENU_REGION(MenuWeaponModelData), COOP_MENU_REGION(MenuWeaponModelInfo), COOP_MENU_REGION(MenuCharaOldAmbient),
        /* repin: MenureadFile is gone (9418a550 builds the path in a local) */ COOP_MENU_REGION(MenuItemUseVolume), COOP_MENU_REGION(WeaponRead_Buf),
        COOP_MENU_REGION(MenuCharaEffectReadFlag), COOP_MENU_REGION(MenuCharaOldEffect), COOP_MENU_REGION(WepEffectMenuPt),
        COOP_MENU_REGION(WepEffectMenuReadBuf), COOP_MENU_REGION(MenuWeaponModelBuildBuffer), COOP_MENU_REGION(CharaFileBGReadNo),
        COOP_MENU_REGION(charachangeid), COOP_MENU_REGION(CharaChangeBaseBuf), COOP_MENU_REGION(menucharReadbuf),
        COOP_MENU_REGION(menud0wepReadBuf), COOP_MENU_REGION(menud1wepReadBuf), COOP_MENU_REGION(menud2wepReadBuf),
        COOP_MENU_REGION(MenuWepIconCharaChangePtr), COOP_MENU_REGION(MenuVoiceLoadPtr), COOP_MENU_REGION(CharaNameDrawFlag),
        COOP_MENU_REGION(CharaNameDrawCase), COOP_MENU_REGION(PerBoardStatusPt), COOP_MENU_REGION(PerBoardTex),
        COOP_MENU_REGION(PerBoardPt), COOP_MENU_REGION(MenuTrushMark),
    };
    // coop-guard: the bag's melt timer is the world's, not a menu's (MeltTimer below). DC_COOP_MUTATE=melttimer_menu (twin): a menu's own, as before.
    static const bool timer_added = [] {
        if (Mutated("melttimer_menu")) {
            const_cast<std::vector<Region> &>(regions).push_back(COOP_MENU_REGION(ItemVolumeStep));
        }
        return true;
    }();
    (void) timer_added;
    return regions;
}

#undef COOP_MENU_REGION

// coop-saveload (Jep's note 14): the save screen's own state (its step, the card access it drives) and the ring's
// choosers and load under way, per menu as the rest, so her menu beside his neither answers his save screen with her pad
// nor draws it, nor ends his load. DC_COOP_MUTATE=saveload_shared (twin): one set for both menus, as before.
const std::vector<Region> &SaveRegions() {
    static const std::vector<Region> regions = [] {
        static std::vector<Region> ring;

        if (!Mutated("saveload_shared")) {
            ring.push_back(Region{(void *) &::SaveMenu, sizeof ::SaveMenu});
            ring.push_back(Region{(void *) &::McAccess, sizeof ::McAccess});
            BattleMenuRingRegions([](void *at, std::size_t size) { ring.push_back(Region{at, size}); });
        }
        return ring;
    }();
    return regions;
}

// coop-invcrash (Jep's 2026-10-27 crash, both players in their menus): the port's own table of the weapons' .chr addresses
// (menu_misc keeps them there, not in MenuWeaponModelData) and the file read slots, per menu as the rest. With one table,
// her weapons page left it pointing into her work buffer, and his weapons page's way out (DngWeaponEquipModelBuild) built
// his weapon from her files, which her later reads had overwritten: both copies crashed in GetPackFile. With one set of read
// slots, a menu's StartReadBG cleared the other's read and its file stood in for the other's.
// DC_COOP_MUTATE=menufiles_shared (twin): one table and one set of slots for both menus, as before.
const std::vector<Region> &FileRegions() {
    static const std::vector<Region> regions = [] {
        static std::vector<Region> files;

        if (!Mutated("menufiles_shared")) {
            MenuWeaponFileRegions([](void *at, std::size_t size) { files.push_back(Region{at, size}); });
            ReadBGRegions([](void *at, std::size_t size) { files.push_back(Region{at, size}); });
        }
        return files;
    }();
    return regions;
}

} // namespace

// coop-offthread: the battle menu's working globals (MenuRegions, SaveRegions) against the state set: how many the set holds,
// and a hash of the bytes of the ones it does not (a copy's own menu, which a join or a state file must leave as it was).
void MenuWorkCensus(std::size_t *held, std::size_t *total, std::uint32_t *own_hash) {
    std::uint32_t hash = 2166136261u;

    *held = *total = 0;
    for (const std::vector<Region> *list : {&MenuRegions(), &SaveRegions()}) {
        for (const Region &region : *list) {
            ++*total;
            if (SnapshotStateHolds(region.at)) {
                ++*held;
                continue;
            }
            for (std::size_t i = 0; i < region.size; i++) {
                hash = (hash ^ static_cast<const unsigned char *>(region.at)[i]) * 16777619u;
            }
        }
    }
    *own_hash = hash;
}

// coop-pt1111l: and DC_COOP_MUTATE=scriptmenu_merge_retail (x-town-stayermenu-hisscene~mergeretail): a script's retail menu
// stepped in the lockstep through the merge, on the taker too, which never opened it. coop-small2 made that twin on a base without
// e57647e7; at their merge (3e1009de) the set no longer carried the menu's working globals, so the taker's world map step read
// its own BtlMenuStatusPt, null in a copy that never opened a battle menu (the dump: SearchItemIndexNo, this null, from
// EscapeDungeonMode in MenuMoveKey), and the copy crashed before the twin's stop. Its twin carries them too, as before.
bool MenuRetailLockstepMutated() {
    return Mutated("merge_menu_retail") || Mutated("scriptmenu_merge_retail");
}

// coop-pt1111m (found by pt1111l): with game.coop_menus off (a player's setting) every menu is retail's, stepped in the lockstep on
// every copy, so its working globals are simulated state that every copy must hold alike, as the world's. e57647e7 took them out of
// the state set on the rule "menu state is not world state", which holds only for local menus. A merge into a stayer in a menu
// (his scene's world map, EscapeDungeonMode's) then had the taker step it on its own globals: BtlMenuStatusPt null in a copy that
// never opened a battle menu, SearchItemIndexNo(this=nullptr) <- EscapeDungeonMode <- MenuMoveKey <- EdMenuMode <-
// CoopScriptMenuStep: a crash. With the menus in the lockstep the set carries them again (as before e57647e7); with local menus
// it does not (each copy's menu is its own). Every copy of a session plays with one setting (the checksum's MenuHash already
// differs on every tick otherwise), so sender and taker list the same regions.
bool MenuWorkInSet() {
    return Enabled() && !MenusOn();
}

// DC_COOP_MUTATE=menusoff_menu_out (twin, x-town-stayermenu-*-menusoff~menuout): a link's apply leaves this copy's own menu working
// globals as they were, as since e57647e7 (a state file's load, the row's fixture, takes them as the fix does).
bool MenuWorkKeptMutated() {
    return Mutated("menusoff_menu_out");
}

// coop-pt1111m: every menu working global's bytes (MenuRegions, SaveRegions): saved and put back (the twin), and their hash (the rows'
// check: the taker's after the apply is the sender's at the take).
void MenuWorkSave(std::vector<unsigned char> &out) {
    out.clear();
    for (const std::vector<Region> *list : {&MenuRegions(), &SaveRegions()}) {
        for (const Region &region : *list) {
            const auto *at = static_cast<const unsigned char *>(region.at);
            out.insert(out.end(), at, at + region.size);
        }
    }
}

void MenuWorkRestore(const std::vector<unsigned char> &in) {
    std::size_t at = 0;

    for (const std::vector<Region> *list : {&MenuRegions(), &SaveRegions()}) {
        for (const Region &region : *list) {
            if (at + region.size > in.size()) {
                return;
            }
            std::memcpy(region.at, in.data() + at, region.size);
            at += region.size;
        }
    }
}

std::uint32_t MenuWorkHash() {
    std::vector<unsigned char> bytes;
    std::uint32_t              hash = 2166136261u;

    MenuWorkSave(bytes);
    for (unsigned char byte : bytes) {
        hash = (hash ^ byte) * 16777619u;
    }
    return hash;
}

// coop-offthread (the twin's DC_SNAPSHOT_MUTATE=menu_in_set): every one of them, as at, size.
void MenuWorkRegionsAll(void (*add)(void *at, std::size_t size, void *context), void *context) {
    for (const std::vector<Region> *list : {&MenuRegions(), &SaveRegions()}) {
        for (const Region &region : *list) {
            add(region.at, region.size, context);
        }
    }
}

namespace {

std::size_t MenuStateSize() {
    std::size_t size = 0;
    for (const std::vector<Region> *list : {&MenuRegions(), &SaveRegions(), &FileRegions()}) {
        for (const Region &region : *list) {
            size += region.size;
        }
    }
    return size;
}

void SaveMenu(std::vector<unsigned char> &to) {
    to.resize(MenuStateSize());
    unsigned char *at = to.data();
    for (const std::vector<Region> *list : {&MenuRegions(), &SaveRegions(), &FileRegions()}) {
        for (const Region &region : *list) {
            std::memcpy(at, region.at, region.size);
            at += region.size;
        }
    }
}

void LoadMenu(const std::vector<unsigned char> &from) {
    const unsigned char *at = from.data();
    for (const std::vector<Region> *list : {&MenuRegions(), &SaveRegions(), &FileRegions()}) {
        for (const Region &region : *list) {
            std::memcpy(region.at, at, region.size);
            at += region.size;
        }
    }
}

// The second menu's own texture blocks (blocks no floor was seen to enter; coop-texdup) and its work buffer, the size
// of the dungeon's read_buffer that the first one works in.
// coop-pt1108 (Jep's 11-08 shots: her Georama Parts page's cards, pictures and chips gone once the time of day's event started): 0x2D-0x31 are the town event's: every event's start and end
// deletes 45-49 and its sprites load into 45 and 46, under her open menu. One answer with coop-pt1108f's proof below (cand56): blocks past retail's 72, which no script can name.
#ifdef PAL
constexpr int kSecondBlocks[6] = {74, 75, 76, 77, 78, 79}; // (coop-pt1108f: past retail's blocks; 45 to 49 are a town script's)
#else
constexpr int kSecondBlocks[5] = {74, 75, 76, 77, 78};
#endif
// DC_COOP_MUTATE=menu_blocks_scripts (twin): the old set, in which a town script's texture load takes her menu's pictures.
const int *SecondBlocks() {
    static const int kOld[6] = {0x2C, 0x2D, 0x2E, 0x2F, 0x30, 0x31};

    return Mutated("menu_blocks_scripts") ? kOld : kSecondBlocks;
}
constexpr int kSecondBlockCount = (int) (sizeof kSecondBlocks / sizeof kSecondBlocks[0]);
constexpr std::size_t kSecondBufferQuads = 0x200000; // 32 MB, past the dungeon read_buffer with headroom: the menu reads its pak,
                                                     // models and pictures into it (6 MB was short of read_buffer)

struct SecondMenu {
    bool                       live = false;
    int                        owner = 0;
    int                        ticks = 0;
    std::vector<unsigned char> state;  // its globals while the first's are in place
    std::vector<unsigned char> first;  // the first's while its are in place
    u_long128                 *buffer = nullptr; // its work buffer: an arena block (zeroed, guard page after it), as read_buffer is
    CGamePad                   pad;
    InputPadState              before{};
    std::uint16_t              prev_other = 0; // the other player's buttons last tick (the Triangle that opens it)
    bool                       first_closed = false; // the first menu shut while this one stays open
    int                        escape = 0;           // coop-localfix: its MenuMapJumpMode as it closed ("escape this dungeon")
};

SecondMenu g_second;

int g_stepping = 0; // coop-quitown: the player whose menu BattleMenuCursor steps now (0: none named)

u_long128 *g_second_arena = nullptr; // coop-invcrash: the second menu's work buffer, for MenuWeaponBuildTrace

// GamePad's port 1 (pad[0] and previous_pad[0]) released: the owner's character, stepped by MoveChara
// on the same pad the menu just read, stands. pad[1] (pad 2, couch) is left as it is.
void ReleasePortOne(CGamePad &pad) {
    static_assert(sizeof(PAD_DATA) == 0x4C);
    PAD_DATA *data = reinterpret_cast<PAD_DATA *>(&pad); // pad[0], pad[1], previous_pad[0], previous_pad[1]

    for (PAD_DATA *port : {&data[0], &data[2]}) {
        port->input.status.button = 0;
        port->input.status.left_x = port->input.status.left_y = 0x80;
        port->input.status.right_x = port->input.status.right_y = 0x80;
    }
}

// Player 1's pad, as the copies agreed on it, on port 1 for the world's step while player 2 has the menu.
// Less Triangle and Start: the game has one menu, so his Triangle must not open it again under hers (the world
// went through the menu's tasks and her menu opened a second time) nor his Start pause the world. The lock Jep
// asked for on 2026-10-20 ("if one player has the menu open, the other cant also open the menu"); her side is
// netplay's Route, which gives her no claim while his menu is open. DC_COOP_MUTATE=fullmenulock (its twin)
// leaves them in.
// In his own pause (coop_pause) his pad reaches nothing.
void SetPortOne(CGamePad &pad, const InputPadState &now, const InputPadState &before) {
    const std::uint32_t keep = PausePlayer(1)            ? 0u
                               : Mutated("fullmenulock") ? 0xFFFFu
                                                         : 0xFFFFu & ~(std::uint32_t) (kInputTriangle | kInputStart);
    const bool          still = PausePlayer(1);

    PAD_DATA *data = reinterpret_cast<PAD_DATA *>(&pad);

    for (int i : {0, 2}) {
        const InputPadState &from = i == 0 ? now : before;
        PAD_STATUS          &to = data[i].input.status;

        to.button = from.buttons & keep;
        to.left_x = still ? 0x80 : from.left_x;
        to.left_y = still ? 0x80 : from.left_y;
        to.right_x = still ? 0x80 : from.right_x;
        to.right_y = still ? 0x80 : from.right_y;
    }
}

// coop-localmenu (below, stage 3a): this copy's own local menu.
struct LocalMenu {
    bool          live = false;
    int           owner = 0;
    int           ticks = 0;
    int           jump = 0; // the map jump the closed engine left, sent with the close
    int           stills = 0; // coop-menucursor: ticks the world behind it was kept as its still
    CGamePad      pad{};
    InputPadState before{};
    MenuCameraOwn camera{}; // coop-stabfix2: its own menu camera (LocalPad)
};

LocalMenu g_local;

// Whether that player's menu steps on a view of their own (OwnerViewBegin): every player but the world's first.
bool OwnerHasView(int owner) {
    return owner != 1;
}

// Around the menu's step and draw for player 2: her member is the party's current one and her bag the
// party's. A member she picks there (Allies) is hers to take: the partner's pick, or a swap asked of
// player 1 for his.
void OwnerViewBegin(int owner) {
    if (owner != 2 || UserStatus == nullptr || g_partner.loaded_chara < 0 || Mutated("fullmenuview")) {
        g_full.player_chara = -1;
        return;
    }

    g_full.player_chara = UserStatus->cur_chara;
    UserStatus->cur_chara = (s8) g_partner.loaded_chara;
    g_full.bag_swapped = MenuSwapP2Bag();
    if (!g_full.view_logged) {
        g_full.view_logged = true;
        std::printf("coop-fullmenu: player 2's menu shows member %d (her bag %s)\n", g_partner.loaded_chara, g_full.bag_swapped ? "swapped in" : "shared");
    }
}

void OwnerViewEnd() {
    if (g_full.player_chara < 0) {
        return;
    }

    int picked = UserStatus->cur_chara;

    UserStatus->cur_chara = (s8) g_full.player_chara;
    if (g_full.bag_swapped) {
        MenuSwapP2Bag();
        g_full.bag_swapped = false;
    }
    if (picked != g_partner.loaded_chara) {
        // coop-localmenu 3c: her local menu's pick is a command (kCmdPick), so every copy takes it on one tick.
        // DC_COOP_MUTATE=localmenu_pick_here (twin): taken on her copy alone, as the lockstep menu did on both.
        if (g_local.live && g_local.owner == 2 && !Mutated("localmenu_pick_here")) {
            NetCommand command;

            command.kind = kCmdPick;
            command.a = (std::uint8_t) picked;
            NetplayQueueCommand(command);
        } else if (picked == g_full.player_chara) {
            AskSwap(1, picked);
        } else if (Pickable(picked)) {
            g_partner.choice = picked;
        }
        std::printf("coop-fullmenu: player 2 picks member %d in her menu\n", picked);
    }
    g_full.player_chara = -1;
}

// kCmdPick, every copy: a pick from a local menu, as MenuCharaChange and OwnerViewEnd took it from the lockstep one: hers
// changes her member (or asks him for his), his asks her for hers.
void ApplyPick(int player, const NetCommand &command, std::int64_t tick) {
    const int picked = command.a;

    if (UserStatus == nullptr) {
        return;
    }
    // (inside her menu's view, the twin's, his member is the one OwnerViewBegin kept)
    const int his = g_full.player_chara >= 0 ? g_full.player_chara : UserStatus->cur_chara;

    if (player == 2 && picked != g_partner.loaded_chara) {
        if (picked == his) {
            AskSwap(1, picked);
        } else if (Pickable(picked)) {
            g_partner.choice = picked;
        }
    } else if (player == 1 && picked == g_partner.loaded_chara) {
        AskSwap(0, picked);
    } else {
        std::printf("coop-localmenu: player %d's pick of member %d: not carried by a command yet\n", player, picked);
        return;
    }
    std::printf("coop-localmenu: player %d's pick of member %d applied at link tick %lld\n", player, picked, (long long) tick);
}

bool LocalPick(int player, int wanted) {
    if ((!g_local.live || g_local.owner != player) && !CoopTownMenuRunsLocalHere(player)) { // coop-stage4: or her town menu's
        return false;
    }

    NetCommand command;

    command.kind = kCmdPick;
    command.a = (std::uint8_t) wanted;
    if (Mutated("localmenu_pick_here")) {
        ApplyPick(player, command, NetplayLinkTick()); // (the twin: taken on this copy alone)
        return true;
    }
    NetplayQueueCommand(command);
    std::printf("coop-localmenu: player %d's menu picks member %d (a command)\n", player, wanted);
    return true;
}

// Whether this copy shows the menu: the owner's copy, or the one screen of couch co-op.
bool ShownHere() {
    int local = NetplayLocalPlayer();

    return local == 0 || local == g_full.owner;
}

// One menu's draw (its draw steps its state too, so both copies run it): on its player's copy over the dimmed world,
// on the other into a target nobody sees.
void DrawOne(int owner, bool shown) {
    SoundOwner sound(owner); // coop-menuse: what its draw plays is its player's

    if (shown) {
        OwnerViewBegin(owner);
        MenuOverWorldDepth(owner);
        BattleMenuDraw();
        OwnerViewEnd();
        return;
    }

    if (g_hidden == gfx::kNullTexture) {
        g_hidden = gfx::CreateRenderTarget(640, 480, true);
    }

    gfx::TextureHandle previous = gfx::CurrentRenderTarget();

    gfx::SetRenderTarget(g_hidden);
    OwnerViewBegin(owner);
    BattleMenuDraw();
    OwnerViewEnd();
    gfx::SetRenderTarget(previous);
}

bool SecondShared() {
    return Mutated("menuboth_shared");
}

// The second menu's globals in place (the first's aside) and its pad as the game pad; and back.
void SecondIn() {
    if (!SecondShared()) {
        SaveMenu(g_second.first);
        LoadMenu(g_second.state);
    }
    std::swap(GamePad, g_second.pad);
}

void SecondOut() {
    std::swap(GamePad, g_second.pad);
    if (!SecondShared()) {
        SaveMenu(g_second.state);
        LoadMenu(g_second.first);
    }
}

// A player's pad on a game pad's port 1, all of it.
void PlayerPadIn(CGamePad &pad, int player, InputPadState &before) {
    InputPadState now{};

    NetplayPlayerPad(player, now);

    PAD_DATA *data = reinterpret_cast<PAD_DATA *>(&pad);

    for (int i : {0, 2}) {
        const InputPadState &from = i == 0 ? now : before;
        PAD_STATUS          &to = data[i].input.status;

        to.button = from.buttons;
        to.left_x = from.left_x;
        to.left_y = from.left_y;
        to.right_x = from.right_x;
        to.right_y = from.right_y;
    }
    before = now;
}

// The second menu's owner's pad on its game pad's port 1 (Triangle and Start are its menu's).
void SecondPadIn() {
    PlayerPadIn(g_second.pad, g_second.owner, g_second.before);
}

void DeleteSecondBlocks() {
    for (int i = 0; i < kSecondBlockCount; i++) {
        TexManager.DeleteTextureBlock(SecondBlocks()[i]);
    }
}

bool FirstOpen() {
    return g_full.live && !g_second.first_closed;
}

// ---- coop-localmenu stage 3a: the game menu on its player's copy alone -----------------
// Linked, a Triangle that opens the menu (the lockstep's pad: every copy sees it on one tick) sets the opener's in-menu bit
// on every copy and starts the menu's engine on the opener's copy only (FullMenuOpenLocal); the world never enters the
// menu's tasks. The engine reads its player's agreed pad on a game pad of its own, and its step and draw take this copy's own
// random numbers (DrawRng). Its close is one command (kCmdMenu, kinds 0, c = the map jump): on its tick every copy runs
// retail's close task (the escape, the minimap, a member change). Browse only for now: the engine's writes to the party status
// stay on this copy (3c makes them commands). DC_COOP_MUTATE=fullmenu_lockstep: the menu in the lockstep, as before (twin).
// DC_COOP_MUTATE=fullmenufreeze (x-chest-world-menu's twin, coop-stalechecks): retail's menu, which stops the world. A local menu
// never reaches FullMenuBegin, where that mutation acts, so it takes the lockstep's menu too, as when the twin was made.

bool LocalMenusOn() {
    return Enabled() && MenusOn() && NetplayPartnerLinked() && !Mutated("fullmenu_lockstep") && !Mutated("fullmenufreeze");
}

// The engine's own pad as the game pad, around its step and draw; and the view the world's draw left (the next tick's
// clip tests read it, and the checksum sums it), which the menu's draw sets for its own models; and the engine's own menu camera
// (coop-stabfix2, MenuCameraSwap), which its pages set.
struct LocalPad {
    LocalPad() {
        std::swap(GamePad, g_local.pad);
        MenuCameraSwap(g_local.camera);
    }
    ~LocalPad() {
        MenuCameraSwap(g_local.camera);
        std::swap(GamePad, g_local.pad);
        mgRenderInfo = view;
    }
    RenderInfo view = mgRenderInfo;
};

// ---- coop-localmenu 3c: the local menu's writes to the party status, as commands ----------------------------------------------
// The engine runs over the live status with this copy's own writes not yet applied laid on top (the overlay, so the menu sees
// its own edits at once). After its step, every 4-byte word it changed becomes a poke command (kCmdPoke: b = the word, c = its
// value; the player's view, her member and bag, is the command's player's); then the live status is put back, so the world
// steps the same on every copy until the poke applies there, on one tick. A poke applied takes its word off the overlay.
// The current member's byte stays out (a member picked in her menu is OwnerViewEnd's). DC_COOP_MUTATE=localmenu_writes_here
// (twin): the writes stay on the menu's copy at once, as a lockstep menu's would.
void ApplyPoke(int player, const NetCommand &command, std::int64_t tick);

struct Poke {
    std::uint32_t word;
    std::uint32_t value;
};

std::vector<Poke> g_pokes; // this copy's own, sent and not yet applied

std::uint32_t *StatusWords() {
    return reinterpret_cast<std::uint32_t *>(UserStatus);
}

constexpr std::size_t kStatusWords = sizeof(CUserStatus) / 4;

// coop-stage4 r6: a shop writes outside the status too: the stock (the storage board) and the game flags right after it in the
// save (CSaveData: stock_item, game_flags[256]; the flags private there, so reached by the stock's end). Their words come after
// the status's in a poke's number. DC_COOP_MUTATE=localmenu_stock_here (twin): those stay on the menu's copy.
static_assert(sizeof(CStockItem) % 4 == 0);
// coop-scriptmenus: and a script's menus write the save before the status (CSaveData): the names screen chara_name[6][32] (at 0x48, 96 words)
// and the fishing exchange fishing_point and fish_data[64] (at 0x1dc, 257 words: the points and the records), after the stock's and flags'
// in a poke's number. Not the words between (map_no, now_time, play_time, day: the world's own). DC_COOP_MUTATE=localmenu_save_here (twin):
// those stay on the menu's copy.
constexpr std::size_t kStockWords = sizeof(CStockItem) / 4 + 256;
constexpr std::size_t kNamesAt = 0x48, kNamesWords = 6 * 32 * 2 / 4;
constexpr std::size_t kFishAt = 0x1dc, kFishWords = 1 + 64 * 16 / 4;
constexpr std::size_t kExtraWords = kStockWords + kNamesWords + kFishWords;
static_assert(kStatusWords + kExtraWords < 0x10000);

std::uint32_t *StockWords() {
    return SaveData != nullptr ? reinterpret_cast<std::uint32_t *>(SaveData->GetStockItem()) : nullptr;
}

bool SaveWordsOn() {
    // (the names' place checked against the save's own accessor once: a moved layout turns these off rather than poke the wrong words)
    static const bool placed = SaveData == nullptr ||
                               reinterpret_cast<unsigned char *>(SaveData->GetCharaName(0)) - reinterpret_cast<unsigned char *>(SaveData) == (std::ptrdiff_t) kNamesAt;

    return placed && !Mutated("localmenu_save_here");
}

// The extra word i (0 up to kExtraWords): the stock and flags, then the names, then the fishing words; null when off.
std::uint32_t *ExtraWord(std::size_t i) {
    if (SaveData == nullptr) {
        return nullptr;
    }
    if (i < kStockWords) {
        return StockWords() + i;
    }
    if (!SaveWordsOn()) {
        return nullptr;
    }
    unsigned char *save = reinterpret_cast<unsigned char *>(SaveData);

    i -= kStockWords;
    return i < kNamesWords ? reinterpret_cast<std::uint32_t *>(save + kNamesAt) + i : reinterpret_cast<std::uint32_t *>(save + kFishAt) + (i - kNamesWords);
}

bool ExtraWritesOn() {
    return StockWords() != nullptr && !Mutated("localmenu_stock_here");
}

void ExtraRead(std::vector<std::uint32_t> &to) {
    to.assign(kExtraWords, 0);
    for (std::size_t i = 0; i < kExtraWords; i++) {
        if (const std::uint32_t *w = ExtraWord(i)) {
            to[i] = *w;
        }
    }
}

void ExtraWrite(const std::vector<std::uint32_t> &from) {
    for (std::size_t i = 0; i < kExtraWords && i < from.size(); i++) {
        if (std::uint32_t *w = ExtraWord(i)) {
            *w = from[i];
        }
    }
}

// ---- coop-guard: the bag's melt timer (ItemVolumeStep), the world's on every copy -----------------------------------------------
// Retail's world stands still under its menu, so the menu ticks the timer (battlemenu.cpp BattleMenuCursor: LoopStep, then
// CheckItemVolume melts the bag by what it counted) and resets it as it closes (ExitBattleMenu: Initialize). Beside a local menu the
// world runs on: the floor's step ticks it on every copy (motiondrive.cpp) and the town melts the bag from it on every copy
// (editloop.cpp). The write guard's rows found the menu's ticks and its close on its owner's copy alone: the timer, then the melt,
// parted. So any player's local menu runs on the world's timer with its ticking off (the world ticks it beside the menu), sees its
// own sent and unapplied changes laid over, and what it did to it goes to every copy on one tick as its bag writes do: the counts it
// melted and its frame's reset as a melt command, its other fields as pokes. DC_COOP_MUTATE=melttimer_menu (twin): as before.
constexpr std::size_t kTimerWords = sizeof(CMenuItemStep) / 4;
static_assert(sizeof(CMenuItemStep) % 4 == 0);
constexpr std::size_t   kTimerBase = kStatusWords + kExtraWords; // a poke of a timer field (set, as the status's)
constexpr std::size_t   kMeltWord = kTimerBase + kTimerWords;    // the melt command: c = counts melted | kMeltReset if the menu reset its frame
static_assert(kMeltWord < 0x10000);
constexpr std::size_t   kFrameWord = offsetof(CMenuItemStep, frame) / 4;
constexpr std::size_t   kPendingWord = offsetof(CMenuItemStep, pending_volume) / 4;
constexpr std::uint32_t kMeltReset = 0x80000000u;

struct Melt {
    int           player;
    std::uint32_t value;
};
std::vector<Melt> g_melts; // this copy's own menus' melts, sent and not yet applied

bool MeltTimerLocal() {
    return !Mutated("melttimer_menu");
}

std::uint32_t *TimerWords() {
    return reinterpret_cast<std::uint32_t *>(&ItemVolumeStep);
}

// Every copy, the command's tick: what the menu melted comes off the world's count, and its reset resets the world's frame.
void ApplyMelt(std::uint32_t value) {
    const int counts = (int) (value & ~kMeltReset);

    ItemVolumeStep.pending_volume = ItemVolumeStep.pending_volume > counts ? ItemVolumeStep.pending_volume - counts : 0;
    if ((value & kMeltReset) != 0) {
        ItemVolumeStep.frame = 0;
    }
}

// Around a local menu's engine (inside LocalWrites): the world's timer kept, the menu's view of it laid out, its changes collected.
class MeltTimer {
  public:
    MeltTimer(bool collect, int owner) : collect_(collect), owner_(owner) {
        if (!MeltTimerLocal()) {
            return;
        }
        on_ = true;
        std::copy(TimerWords(), TimerWords() + kTimerWords, live_);
        for (const Poke &poke : g_pokes) {
            if (poke.word >= kTimerBase && poke.word < kTimerBase + kTimerWords) {
                TimerWords()[poke.word - kTimerBase] = poke.value;
            }
        }
        for (const Melt &melt : g_melts) {
            ApplyMelt(melt.value);
        }
        std::copy(TimerWords(), TimerWords() + kTimerWords, before_);
        enabled_ = ItemVolumeStep.enabled;
        ItemVolumeStep.enabled = 0; // (the world ticks it beside the menu: the menu's LoopStep counts nothing)
    }

    // The menu's changes as commands (queued after the put-back, as LocalWrites' pokes); the world's timer back.
    void Collect(std::vector<NetCommand> &commands, std::vector<Poke> &pokes) {
        if (!on_) {
            return;
        }
        // retail's ExitBattleMenu (Initialize) is the menu's only write of enabled: on, as it left it; else as it was
        if (ItemVolumeStep.enabled == 0) {
            ItemVolumeStep.enabled = enabled_;
        }
        std::uint32_t now[kTimerWords];

        std::copy(TimerWords(), TimerWords() + kTimerWords, now);
        std::copy(live_, live_ + kTimerWords, TimerWords());
        on_ = false;
        if (!collect_) {
            return;
        }
        const int  was_pending = (int) before_[kPendingWord];
        const int  now_pending = (int) now[kPendingWord];
        const int  melted = was_pending > now_pending ? was_pending - now_pending : 0;
        const bool reset = now[kFrameWord] != before_[kFrameWord];

        if (melted > 0 || reset) {
            NetCommand command;

            command.kind = kCmdPoke;
            command.b = (std::uint16_t) kMeltWord;
            command.c = (std::uint32_t) melted | (reset ? kMeltReset : 0u);
            commands.push_back(command);
            g_melts.push_back(Melt{owner_, command.c});
        }
        for (std::size_t i = 0; i < kTimerWords; i++) {
            if (i == kFrameWord || i == kPendingWord || now[i] == before_[i]) {
                continue;
            }
            NetCommand command;

            command.kind = kCmdPoke;
            command.b = (std::uint16_t) (kTimerBase + i);
            command.c = now[i];
            commands.push_back(command);
            pokes.push_back(Poke{(std::uint32_t) (kTimerBase + i), now[i]});
        }
    }

    ~MeltTimer() {
        if (on_) {
            std::copy(live_, live_ + kTimerWords, TimerWords());
        }
    }

    MeltTimer(const MeltTimer &) = delete;
    MeltTimer &operator=(const MeltTimer &) = delete;

  private:
    bool          collect_;
    int           owner_;
    bool          on_ = false;
    s16           enabled_ = 0;
    std::uint32_t live_[kTimerWords] = {};
    std::uint32_t before_[kTimerWords] = {};
};

// coop-soakfix2 (the soak's rooms seed 26111233: OUT OF SYNC at tick 900, part "p2 bag"): the party's key ring (coop-worldkeys,
// RingOn) is the world's too, outside the status and the save's stock, so a local menu's write to it stayed on the menu's
// copy: her debug item list (a --jump's DebugMode, Select on the Items page) took the Moustache Key with her Cross, GetItem
// put it in the ring on her copy alone and no poke went. Its words come after the melt timer's (coop-guard) in a poke's number;
// the ring is 0x1AA bytes (s8 and s16 fields), so its words are 2 bytes each, the value in a poke's low half.
// DC_COOP_MUTATE=localmenu_ring_here (twin): the ring's writes stay on the menu's copy, as before.
static_assert(sizeof(ITEM_PACK) % 2 == 0);
constexpr std::size_t kRingWords = sizeof(ITEM_PACK) / 2;
constexpr std::size_t kRingFirst = kMeltWord + 1;
// cand57: the poke regions in order and apart (status, the save's words, the melt timer and its command, the ring): a branch that
// numbers a new region from an old one's start fails here, not as two copies applying each other's pokes.
static_assert(kStatusWords + kExtraWords <= kTimerBase && kTimerBase + kTimerWords <= kMeltWord && kMeltWord < kRingFirst, "poke regions overlap"); // (train 56: after coop-guard's timer words and melt command, which took kStatusWords + kExtraWords on)
static_assert(kRingFirst + kRingWords < 0x10000);

std::uint16_t *RingWords() {
    return RingOn() && !Mutated("localmenu_ring_here") ? reinterpret_cast<std::uint16_t *>(&SharedRing()) : nullptr;
}

std::size_t CharaWord() {
    return (std::size_t) (reinterpret_cast<unsigned char *>(&UserStatus->cur_chara) - reinterpret_cast<unsigned char *>(UserStatus)) / 4;
}

class LocalWrites {
  public:
    explicit LocalWrites(bool collect) : collect_(collect) {
        if (UserStatus == nullptr || Mutated("localmenu_writes_here")) {
            return;
        }
        on_ = true;
        timer_.emplace(collect_, g_local.live ? g_local.owner : NetplayLocalPlayer()); // coop-guard
        std::uint32_t *words = StatusWords();

        live_.assign(words, words + kStatusWords);
        extra_ = ExtraWritesOn();
        if (extra_) {
            ExtraRead(extra_live_);
        }
        ring_ = RingWords();
        if (ring_ != nullptr) {
            ring_live_.assign(ring_, ring_ + kRingWords);
        }
        for (const Poke &poke : g_pokes) {
            if (poke.word < kStatusWords) {
                words[poke.word] = poke.value;
            } else if (poke.word >= kRingFirst) {
                if (ring_ != nullptr) {
                    ring_[poke.word - kRingFirst] = (std::uint16_t) poke.value;
                }
            } else if (extra_ && poke.word < kStatusWords + kExtraWords) { // (the melt timer's words are MeltTimer's)
                if (std::uint32_t *w = ExtraWord(poke.word - kStatusWords)) {
                    *w = poke.value;
                }
            }
        }
        if (collect_) {
            before_.assign(words, words + kStatusWords);
            if (extra_) {
                ExtraRead(extra_before_);
            }
            if (ring_ != nullptr) {
                ring_before_.assign(ring_, ring_ + kRingWords);
            }
        }
    }

    ~LocalWrites() {
        if (!on_) {
            return;
        }

        std::uint32_t *words = StatusWords();
        const std::size_t chara = CharaWord();
        std::vector<NetCommand> commands;

        for (std::size_t i = 0; collect_ && i < kStatusWords; i++) {
            std::uint32_t now = words[i];

            if (i == chara) {
                // the member's byte as it was: only the other bytes of its word are the menu's to send
                const std::size_t at = (std::size_t) (reinterpret_cast<unsigned char *>(&UserStatus->cur_chara) - reinterpret_cast<unsigned char *>(UserStatus)) % 4;
                reinterpret_cast<unsigned char *>(&now)[at] = reinterpret_cast<const unsigned char *>(&before_[i])[at];
            }
            if (now == before_[i]) {
                continue;
            }

            NetCommand command;

            command.kind = kCmdPoke;
            command.b = (std::uint16_t) i;
            command.c = now;
            // coop-pt1106-lockup: a command can apply at once (a catch-up's sender records it, NetplayQueueCommand), so it goes
            // out only after the live status is back and its overlay word is listed: applied before, the put-back undid it
            // and the overlay kept it for good, a menu's write lost on every copy. DC_COOP_MUTATE=poke_before_putback (twin): as before.
            if (Mutated("poke_before_putback")) {
                NetplayQueueCommand(command);
            } else {
                commands.push_back(command);
            }
            g_pokes.push_back(Poke{(std::uint32_t) i, now});
        }
        for (std::size_t i = 0; collect_ && extra_ && i < kExtraWords; i++) {
            const std::uint32_t *word = ExtraWord(i);
            const std::uint32_t  now = word != nullptr ? *word : extra_before_[i];

            if (now != extra_before_[i]) {
                NetCommand command;

                command.kind = kCmdPoke;
                command.b = (std::uint16_t) (kStatusWords + i);
                command.c = now;
                commands.push_back(command);
                g_pokes.push_back(Poke{(std::uint32_t) (kStatusWords + i), now});
            }
        }
        for (std::size_t i = 0; collect_ && ring_ != nullptr && i < kRingWords; i++) {
            if (ring_[i] != ring_before_[i]) {
                NetCommand command;

                command.kind = kCmdPoke;
                command.b = (std::uint16_t) (kRingFirst + i);
                command.c = ring_[i];
                commands.push_back(command);
                g_pokes.push_back(Poke{(std::uint32_t) (kRingFirst + i), ring_[i]});
            }
        }
        std::copy(live_.begin(), live_.end(), words);
        if (extra_) {
            ExtraWrite(extra_live_);
        }
        if (timer_) { // coop-guard: the melt timer back to the world's, the menu's changes to it as commands too
            std::vector<Poke> timer_pokes;

            timer_->Collect(commands, timer_pokes);
            g_pokes.insert(g_pokes.end(), timer_pokes.begin(), timer_pokes.end());
        }
        if (ring_ != nullptr) {
            std::copy(ring_live_.begin(), ring_live_.end(), ring_);
        }
        for (const NetCommand &command : commands) {
            NetplayQueueCommand(command);
            // coop-pt1109a test hook (DC_COOP_TEST_POKE_AT_ONCE=1, t-triangle-menu -PokeAtOnce): as a catch-up's sender does, the poke applied
            // here inside the menu's view too (it lands again on its tick, on both copies).
            COOP_TEST_SWITCH(bool, at_once, TestEnvOn("DC_COOP_TEST_POKE_AT_ONCE"));
            if (at_once) {
                ApplyPoke(g_local.owner, command, NetplayLinkTick());
            }
        }
    }

    LocalWrites(const LocalWrites &) = delete;
    LocalWrites &operator=(const LocalWrites &) = delete;

  private:
    bool                       collect_;
    bool                       on_ = false;
    std::vector<std::uint32_t> live_;
    std::vector<std::uint32_t> before_;
    bool                       extra_ = false; // coop-stage4 r6
    std::vector<std::uint32_t> extra_live_;
    std::vector<std::uint32_t> extra_before_;
    std::optional<MeltTimer>   timer_; // coop-guard
    std::uint16_t             *ring_ = nullptr; // coop-soakfix2
    std::vector<std::uint16_t> ring_live_;
    std::vector<std::uint16_t> ring_before_;
};

// coop-pt1108: the chip boards and the parts' records, outside LocalWrites (coop_georama_own.cpp).
struct PartsWritesScope {
    explicit PartsWritesScope(bool collect) { CoopPartsWritesBegin(collect); }
    ~PartsWritesScope() { CoopPartsWritesEnd(); }
    PartsWritesScope(const PartsWritesScope &) = delete;
    PartsWritesScope &operator=(const PartsWritesScope &) = delete;
};

// coop-stage4: in a town the party status is the save's dungeon status (UserStatus is a floor's): a town menu's writes and their
// pokes go there, with UserStatus pointed at it meanwhile (as coop_worlds' Atla layout does).
class TownStatus {
  public:
    TownStatus() {
        if (UserStatus == nullptr && mode == 2 && SaveData != nullptr) {
            UserStatus = reinterpret_cast<CUserStatus *>(SaveData->GetDngStatus());
            set_ = true;
        }
    }
    ~TownStatus() {
        if (set_) {
            UserStatus = nullptr;
        }
    }
    TownStatus(const TownStatus &) = delete;
    TownStatus &operator=(const TownStatus &) = delete;

  private:
    bool set_ = false;
};

// Every copy, the poke's tick: the word in its player's view (player 2's member and bag swapped in, as her menu saw them).
void ApplyPoke(int player, const NetCommand &command, std::int64_t tick) {
    TownStatus town;

    if (command.b >= kTimerBase && command.b <= kMeltWord) { // coop-guard: the melt timer, the world's: no view of a player's
        const bool own = player == g_local.owner || player == NetplayLocalPlayer();

        if (command.b == kMeltWord) {
            ApplyMelt(command.c);
            auto it = std::find_if(g_melts.begin(), g_melts.end(), [&](const Melt &m) { return m.player == player && m.value == command.c; });

            if (own && it != g_melts.end()) {
                g_melts.erase(it);
            }
        } else {
            TimerWords()[command.b - kTimerBase] = command.c;
            auto it = std::find_if(g_pokes.begin(), g_pokes.end(), [&](const Poke &p) { return p.word == command.b && p.value == command.c; });

            if (own && it != g_pokes.end()) {
                g_pokes.erase(it);
            }
        }
        std::fprintf(stderr, "coop-localmenu: player %d's menu write: melt timer %s %08x at link tick %lld (frame %d, counts %d)\n", player,
                     command.b == kMeltWord ? "melt" : "word", (unsigned) command.c, (long long) tick, (int) ItemVolumeStep.frame,
                     (int) ItemVolumeStep.pending_volume);
        return;
    }
    if (UserStatus == nullptr || command.b >= kRingFirst + kRingWords || (command.b >= kStatusWords + kExtraWords && command.b < kRingFirst)) {
        return;
    }
    if (command.b >= kStatusWords) { // coop-stage4 r6: the stock and the flags (coop-soakfix2: and the ring): no view of a player's
        const bool ring = command.b >= kRingFirst;

        if (ring) {
            if (RingOn()) { // (the twin's copy writes it too: only the sender keeps it from the lockstep)
                reinterpret_cast<std::uint16_t *>(&SharedRing())[command.b - kRingFirst] = (std::uint16_t) command.c;
            }
        } else if (std::uint32_t *word = ExtraWord(command.b - kStatusWords)) {
            // coop-eventslots: a save word of another player's menu (their fishing exchange) lands in their own fishing record when the
            // records are each player's (the save's own being the game's player's). DC_COOP_MUTATE=fishpoke_into_save (twin): the save's.
            const bool own_record = player > 1 && FishingPerPlayer() && !Mutated("fishpoke_into_save");

            if (own_record) {
                FishRecordSwap(player - 1);
            }
            *word = command.c;
            if (own_record) {
                FishRecordSwap(player - 1);
            }
        }
        if (player == g_local.owner || player == NetplayLocalPlayer()) {
            auto it = std::find_if(g_pokes.begin(), g_pokes.end(), [&](const Poke &p) { return p.word == command.b && p.value == command.c; });

            if (it != g_pokes.end()) {
                g_pokes.erase(it);
            }
        }
        std::fprintf(stderr, "coop-localmenu: player %d's menu write: %s word %d = %08x at link tick %lld\n", player, ring ? "ring" : "save",
                     (int) (command.b - (ring ? kRingFirst : kStatusWords)), (unsigned) command.c, (long long) tick);
        return;
    }

    // coop-p1parts: in a town her member is the town's, not the floor's partner (no floor partner there: what her town menu or shop bought landed in his bag).
    // DC_COOP_MUTATE=townpoke_noview (twin): the floor's alone, as before.
    int view_member = g_partner.loaded_chara;

    if (mode == 2 && !Mutated("townpoke_noview")) {
        view_member = -1;
        TownPartnerShown(nullptr, &view_member);
    }
    const bool view_swapped = player == 2 && view_member >= 0;
    const s8   chara = UserStatus->cur_chara;
    bool       bag = false;

    if (view_swapped) {
        UserStatus->cur_chara = (s8) view_member;
        // coop-pt1109a (Jep's 11-09 playtest: her Organize as he came back to town, OUT OF SYNC at tick 120, her copy's save holding her sorted bag
        // in his pack): a catch-up's sender applies a poke at once (NetplayQueueCommand), inside her menu's step, her bag already in the
        // pack; swapping again put his there and wrote her word over it, on her copy alone (his replay applied it outside any view).
        // DC_COOP_MUTATE=poke_view_swaps_again (twin): swapped whatever is in the pack, as before.
        if (MenuP2BagInPack() == CoopWorldsBagsSwapped() || Mutated("poke_view_swaps_again")) {
            bag = MenuSwapP2Bag();
        }
    }

    // The view's member's equipped weapon before and after: an equip reloads that member's hand on every copy, now.
    const int member = UserStatus->cur_chara;
    const auto equipped = [&]() { return (int) UserStatus->chara_weapons[member][UserStatus->equipped_weapon_slot[member]].item_no; };
    const int was = equipped();

    StatusWords()[command.b] = command.c;

    const int now = equipped();

    if (view_swapped) {
        if (bag) {
            MenuSwapP2Bag();
        }
        UserStatus->cur_chara = chara;
    }
    // (an equip's pokes come one a frame: the slot can point at a weapon whose own word has not landed yet; its own poke
    // reloads then)
    if (now != was && now > 0 && mode != 2 && !Mutated("localmenu_weapon_here")) { // (coop-stage4: a town's hands are the town's own)
        if (view_swapped) {
            LoadWeapon(member); // her hand (the partner's body)
        } else {
            MenuReloadPlayerWeapon(member);
        }
        std::fprintf(stderr, "coop-localmenu: player %d's equip: member %d's weapon %d -> %d reloaded at link tick %lld\n", player, member,
                     was, now, (long long) tick);
    }

    if (player == g_local.owner || player == NetplayLocalPlayer()) { // this copy's own (its engine ran that player's menu; coop-stage4: or its town menu)
        auto it = std::find_if(g_pokes.begin(), g_pokes.end(), [&](const Poke &p) { return p.word == command.b && p.value == command.c; });

        if (it != g_pokes.end()) {
            g_pokes.erase(it);
        }
    }
    std::fprintf(stderr, "coop-localmenu: player %d's menu write: status word %d = %08x at link tick %lld\n", player, (int) command.b,
                 (unsigned) command.c, (long long) tick);
}

void TestMenuEscape(int owner, int ticks); // (below)

// The world's tick beside local menus, on every copy alike: every player of the zone in a pause or a menu (the bits) holds it
// (coop_pause's rule, which the lockstep menu's own step applied), so it only draws; otherwise the game's own tick.
int WorldStep() {
    const bool  local = PlayerInMenu(1, kActorMenuLocal) || PlayerInMenu(2, kActorMenuLocal);
    // coop-opt7: with a player in a presentation of their own among the paused, the world steps under coop_pause's hold
    // (monsters and effects held, as retail's presentation holds them) so the opener's pose and engine go on.
    // DC_COOP_MUTATE=present_heldstill (twin): the world only draws, as for menus alone.
    const bool  held = local && PauseZoneHeld() && (!AnyPresenting() || Mutated("present_heldstill"));
    static bool logged = false;

    COOP_TEST_TRACE_RESET(logged, false);
    if (local && held != logged) {
        logged = held;
        std::printf("coop-fullmenu: the world %s\n", held ? "holds (everyone paused or in a menu)" : "runs");
    }
    ActorActiveItemsAgree(); // coop-feathersync: a local menu's Items page set them on its own copy alone

    if (!held) {
        return GameLoop();
    }

    DunMainDraw();
    BtGameModeFlag = 0; // GameLoop's tail (a floor left, the fade) alone
    const int result = GameLoop();

    if (BtGameModeFlag == 0) {
        BtGameModeFlag = BT_GAME_MODE_PLAY;
    }
    return result;
}

// coop-menucursor (Jep's 11-01 LAN pack 7: "when I hit back from the inventory as P1, the background flashed to an image of
// toan in the town"): the menu's pages draw the still "frame_image" (the Items page's Back, the Weapons page's bands), which
// retail fills from the frame before the menu (GAME_TASK_MENU_WAIT_FRAME's frameCaputer). A local menu never enters those
// tasks, so the still was whatever was kept last, a town menu's. Here the world drawn behind it is kept each tick, before the
// dim and the menu, on the menu's own copy (drawing only, no lockstep state). DC_COOP_MUTATE=localmenu_still_none: none (twin).
void KeepWorldStill() {
    CTexture *still = TexManager.GetTexture(const_cast<char *>("frame_image"), -1);

    if (still == nullptr || Mutated("localmenu_still_none")) {
        return;
    }
    MGMoveFrameBuffImage(reinterpret_cast<sceGsTex0 *>(&still->tex0), 0, 0, 0);
    if (g_local.stills++ == 0) {
        std::printf("coop-localmenu: player %d's menu keeps the world behind it as its still (link tick %lld)\n", g_local.owner,
                    (long long) NetplayLinkTick());
    }
}

int LocalGameLoop() {
    int open = 1;

    {
        WriteGuardMenu guard("LocalGameLoop step"); // coop-guard (a test's: DC_QA_WRITE_GUARD)
        SoundOwner     sound(g_local.owner);
        DrawRng        rng;

        PlayerPadIn(g_local.pad, g_local.owner, g_local.before);
        LocalPad pad;
        g_stepping = g_local.owner;
        OwnerViewBegin(g_local.owner);
        {
            PartsWritesScope parts_writes(true); // coop-pt1108
            LocalWrites writes(true);
            TestMenuEscape(g_local.owner, g_local.ticks); // coop-localmenu 3d: the escape rows' hook, on the menu's own copy
            // coop-localmenu 3c test hook (DC_COOP_TEST_MENU_WRITE=<player>): at that player's menu's 30th tick its step adds 7
            // gilda, as a commit in the menu would write the party status.
            COOP_TEST_SWITCH(int, writer, TestEnvInt("DC_COOP_TEST_MENU_WRITE", 0));
            // coop-pt1106-lockup: DC_COOP_TEST_MENU_WRITE_LOCKSTEP=N: N ticks after the lockstep, not its 30th tick.
            COOP_TEST_SWITCH(int, write_after, TestEnvInt("DC_COOP_TEST_MENU_WRITE_LOCKSTEP", -1));
            const bool write_due = write_after >= 0 ? NetplayTestLockstepFrom() >= 0 && NetplayLinkTick() == NetplayTestLockstepFrom() + write_after : g_local.ticks == 30;

            // DC_COOP_TEST_MENU_EQUIP=<player>: at its 30th tick that player's menu equips the member's next weapon slot (a
            // Baselard put there first when the slot is empty), as the Weapons page's equip writes it.
            COOP_TEST_SWITCH(int, equipper, TestEnvInt("DC_COOP_TEST_MENU_EQUIP", 0));

            if (equipper == g_local.owner && g_local.ticks == 30 && UserStatus != nullptr) {
                const int member = UserStatus->cur_chara;
                const int next = (UserStatus->equipped_weapon_slot[member] + 1) % 2;

                if (UserStatus->chara_weapons[member][next].item_no <= 0) {
                    UserStatus->chara_weapons[member][next] = UserStatus->chara_weapons[member][UserStatus->equipped_weapon_slot[member]];
                    UserStatus->chara_weapons[member][next].item_no = (s16) (member == 0 ? ITEM_WEAPON_BASELARD : UserStatus->chara_weapons[member][next].item_no);
                }
                std::printf("coop-localmenu: test: player %d's menu equips slot %d (weapon %d)\n", g_local.owner, next,
                            (int) UserStatus->chara_weapons[member][next].item_no);
                UserStatus->equipped_weapon_slot[member] = (decltype(UserStatus->equipped_weapon_slot[member])) next;
            }
            if (writer == g_local.owner && write_due && UserStatus != nullptr) {
                std::printf("coop-localmenu: test: player %d's menu adds 7 gilda (%d)\n", g_local.owner, (int) UserStatus->money);
                UserStatus->money = (u16) (UserStatus->money + 7);
            }
            open = BattleMenuCursor();
            MenuPackTrace(); // coop-servecrash: the chestmenu hook on a local menu too
        }
        OwnerViewEnd();
        g_stepping = 0;
    }
    g_local.ticks++;

    if (open == 0) {
        g_local.live = false;
        g_local.jump = MenuMapJumpMode;
        MenuMapJumpMode = 0; // every copy takes it from the close command
        std::printf("coop-localmenu: player %d's menu closes on their copy after %d ticks at page %d (map jump %d, still kept %d ticks)\n",
                    g_local.owner, g_local.ticks, MenuSelect[1], g_local.jump, g_local.stills);
        // coop-stabfix2: the camera its pages left (its own; under the twin the world's) beside the world's menu camera.
        const float *eye = g_local.camera.set ? g_local.camera.pos : MenuCamera.pos;

        std::printf("coop-localmenu: player %d's menu camera eye (%.0f %.0f %.0f), the world's (%.0f %.0f %.0f)\n", g_local.owner, eye[0], eye[1],
                    eye[2], MenuCamera.pos[0], MenuCamera.pos[1], MenuCamera.pos[2]);
        // coop-bigchest: a close inside the world's own task (his chest's presentation) leaves that task to run out its way.
        if (gameTask != GAME_TASK_PLAY) {
            std::printf("coop-localmenu: player %d's menu closed beside the world's task %#x\n", g_local.owner, (unsigned) gameTask);
        }
    }

    // The world, as every copy steps it (BtGameModeFlag stays play).
    const int result = WorldStep();

    // The owner went down in the world's step: the menu ends the game's own way (coop-deadstick: ExitBattleMenu, so the pad
    // leaves the menu's stick mode); its close command follows. DC_COOP_MUTATE=menudown: it stays (the lockstep menu's twin too).
    const bool down = g_local.owner == 1 ? UserStatus != nullptr && UserStatus->hp[UserStatus->cur_chara] <= 0 : g_partner.down > 0;

    if (g_local.live && down && !Mutated("menudown")) {
        {
            LocalPad pad;
            // coop-famA: the way out inside the menu's writes, as its step's close is: ExitBattleMenu resets the bag's melt timer
            // (ItemVolumeStep.Initialize) and tidies the bag, the world's since coop-guard; outside them they landed on the owner's
            // copy alone (OUT OF SYNC from the next checksum). Now they go to every copy by command, on one tick.
            // DC_COOP_MUTATE=menudown_writes_here (twin): outside, as before.
            std::optional<LocalWrites> writes;
            if (!Mutated("menudown_writes_here")) {
                writes.emplace(true);
            }
            if (!Mutated("deadstick")) {
                ExitBattleMenu(1);
            }
        }
        g_local.live = false;
        g_local.jump = 0;
        std::printf("coop-fullmenu: player %d's menu closes: down (local, after %d ticks)\n", g_local.owner, g_local.ticks);
    }

    if (g_local.live) {
        WriteGuardMenu guard("LocalGameLoop draw"); // coop-guard (a test's: DC_QA_WRITE_GUARD)
        SoundOwner     sound(g_local.owner);
        DrawRng        rng;
        LocalPad       pad;

        static const spRGBA kDim = {0x00, 0x00, 0x00, 0x50};

        KeepWorldStill();
        set2DSpriteC4(Vif1Packet, CRect_i_(0, 0, 640, 512), (spRGBA *) &kDim, (spRGBA *) &kDim, (spRGBA *) &kDim, (spRGBA *) &kDim);
        OwnerViewBegin(g_local.owner);
        MenuOverWorldDepth(g_local.owner);
        {
            PartsWritesScope parts_writes(false); // coop-pt1108
            LocalWrites writes(false); // the draw shows the menu's own edits too
            BattleMenuDraw();
        }
        OwnerViewEnd();
    }
    if (g_local.ticks % 60 == 1) {
        std::printf("coop-localmenu: player %d's menu open %d ticks on their copy\n", g_local.owner, g_local.ticks);
        std::fflush(stdout);
    }
    return result;
}

void Draw() {
    int  local = NetplayLocalPlayer();
    bool first_here = FirstOpen() && ShownHere();
    bool second_here = g_second.live && local == g_second.owner;

    if (first_here || second_here) {
        // The menu's player sees the world without the HUD (slid away, as retail's menu opening does) and
        // dimmed under the menu. Only the drawing reads rogoY3 here; the step that moves it ran already.
        int hud = rogoY3;

        rogoY3 = -0x60;
        DunMainDraw();
        rogoY3 = hud;

        static const spRGBA kDim = {0x00, 0x00, 0x00, 0x50};

        set2DSpriteC4(Vif1Packet, CRect_i_(0, 0, 640, 512), (spRGBA *) &kDim, (spRGBA *) &kDim, (spRGBA *) &kDim, (spRGBA *) &kDim);
    } else {
        DunMainDraw();
    }

    // The same order on both copies, the first then the second (a menu's draw steps its state and the game's random
    // numbers, so an order that followed whose copy it is parted the copies; 2026-10-24). The hidden one goes to a
    // target nobody sees, so the order does not change what the screen shows.
    if (FirstOpen()) {
        DrawOne(g_full.owner, first_here);
    }
    if (g_second.live) {
        SecondIn();
        DrawOne(g_second.owner, second_here);
        SecondOut();
    }
}

// The other player's Triangle, half a second after the first menu opened (a tick count, the same on both copies;
// the first's pictures loading is not, 2026-10-24 desync): their own menu beside it. Linked
// play only (couch co-op has one screen); not while they are down, paused, away from the floor, or in the first menu's
// map jump (leaving the floor ends both). DC_COOP_MUTATE=menuboth_off: no second menu (the lock as before).
void TrySecondOpen() {
    int           other = g_full.owner == 1 ? 2 : 1;
    InputPadState pad{};

    if (!NetplayPlayerPad(other, pad)) {
        return;
    }

    std::uint16_t pressed = pad.buttons & ~g_second.prev_other;

    g_second.prev_other = pad.buttons;

    bool down = other == 1 ? UserStatus != nullptr && UserStatus->hp[UserStatus->cur_chara] <= 0 : g_partner.down > 0;

    if (g_second.live || g_second.first_closed || (pressed & kInputTriangle) == 0 || NetplayLocalPlayer() == 0 || Mutated("menuboth_off") ||
        down || PausePlayer(other) || (other == 2 && !g_partner.visible) || g_full.ticks < 30 || MenuMapJumpMode > 0) {
        return;
    }

    SoundOwner sound(other); // coop-menuse: the opening's sounds are hers (or his)

    g_second = SecondMenu{.prev_other = pad.buttons};
    g_second.owner = other;
    g_second.buffer = MenuSecondBuffer();
    g_second.pad = GamePad;
    g_second.pad.AutoRepeatOff();
    g_second.pad.MenuModeOff();
    g_second.before = pad;

    // Its globals start as the first's (the shared pictures' handles among them), then the game's own opening runs on
    // them with the second's blocks; its work buffer then takes the read_buffer part the opening put its pak in.
    if (!SecondShared()) {
        SaveMenu(g_second.first);
    }
    std::swap(GamePad, g_second.pad);
    OwnerViewBegin(other);

    s32 blocks[6] = {};

    std::copy(SecondBlocks(), SecondBlocks() + kSecondBlockCount, blocks);
    BattleMenuInit(blocks, BATTLE_MENU_MODE_DUNGEON);

    u_long128 *start = BtlMenuBufferSet(BATTLE_MENU_MODE_DUNGEON);
    u_long128 *base = MenuCalcBufAlignment(g_second.buffer);
    std::size_t used = (std::size_t) (BtlMenuReadBuf - start);

    std::memcpy(base, start, used * sizeof(u_long128));
    MenuMes.work_area = base + ((u_long128 *) MenuMes.work_area - start);
    BtlMenuReadBuf = base + used;
    OwnerViewEnd();
    std::swap(GamePad, g_second.pad);
    g_second.live = true;

    if (!SecondShared()) {
        SaveMenu(g_second.state);
        LoadMenu(g_second.first);
    }

    std::printf("coop-fullmenu: player %d's menu opens beside player %d's (%zu bytes of menu state each) at its tick %d\n", other, g_full.owner,
                MenuStateSize(), g_full.ticks);
}

// The second menu's tick: its step on its own pad and globals. Its close drops its blocks; the first's close (or the
// world's) waits for it.
// Test hook (coop-localfix, DC_COOP_TEST_MENU_ESCAPE=P): player P's menu, first or second, 60 ticks open, takes the map page's
// "escape this dungeon" (its paid way out: MenuMapJumpMode 1, as the page sets it); the test's Circle then closes it.
void TestMenuEscape(int owner, int ticks) {
    static const int who = TestEnvInt("DC_COOP_TEST_MENU_ESCAPE", 0);

    if (who == owner && ticks == 60) {
        MenuMapJumpMode = 1;
        std::printf("coop-fullmenu: test: player %d's menu takes the dungeon's escape\n", owner);
    }
}

void StepSecond() {
    if (!g_second.live) {
        return;
    }

    SoundOwner sound(g_second.owner); // coop-menuse

    SecondPadIn();
    SecondIn();
    TestMenuEscape(g_second.owner, g_second.ticks);
    OwnerViewBegin(g_second.owner);
    g_stepping = g_second.owner;
    int open = BattleMenuCursor();
    g_stepping = 0;
    OwnerViewEnd();
    int page = MenuSelect[1];
    const int jump = MenuMapJumpMode; // coop-localfix: its own "escape this dungeon", in its globals
    SecondOut();
    g_second.ticks++;

    if (open == 0) {
        g_second.live = false;
        g_second.escape = Mutated("second_escape_lost") ? 0 : jump;
        DeleteSecondBlocks();
        std::printf("coop-fullmenu: player %d's second menu closes after %d ticks at page %d\n", g_second.owner, g_second.ticks, page);
    }
}

// ---- the item select beside the menu (Jep's note 34, 2026-10-25: "If P1 opens the use item while P2 has the menu
// open, it locks the use item menu over P2s menu") ---------------------------------------------------------------------
// A script's item window (a door's "use an item", BtMiniItemSelect) asked for by the player not in the menu. Retail's
// opening (BtMiniItemSelect_Loop's step 2) takes texture block 0x18 and read_buffer, both the open menu's, and its
// game mode (BT_GAME_MODE_ITEM_SELECT) ends the menu. Here it opens on a texture block, work buffer and game pad of its
// own, its owner's pad steps it, and it draws on its owner's copy only (the other's into the hidden target) while the
// menu steps and draws as before. Its window shows the live world behind it (the frame is captured each tick). The
// script waits on step 4 (BtMiniItemSelect_Loop does nothing there) and finishes with step 3 as retail's does. When
// the menu closes first, the window goes on as retail's (BtGameModeFlag ITEM_SELECT, its pad's modes on GamePad).
// DC_COOP_MUTATE=menuselect: retail's opening, as before (twin).
struct ItemSelect {
    bool          live = false;
    int           owner = 0;
    int           ticks = 0;
    CGamePad      pad;
    InputPadState before{};
};

ItemSelect g_select;

constexpr int         kSelectBlock = kSecondBlocks[0]; // free: no second menu while it is open
constexpr std::size_t kSelectBufferBytes = 4u << 20;   // eventmnu2.pak is far smaller

void TrySelectOpen() {
    if (g_select.live || gameTask != GAME_TASK_SCRIPT_ITEM_SELECT || BtMiniItemSelect_Sled != 2 || g_second.live || Mutated("menuselect")) {
        return;
    }

    static const char kSelectArena = 0;
    u_int            *read = read_buffer;

    g_select = ItemSelect{.live = true, .owner = g_full.owner == 1 ? 2 : 1};
    g_select.pad = GamePad;
    g_select.pad.AutoRepeatOff();
    g_select.pad.MenuModeOff();
    NetplayPlayerPad(g_select.owner, g_select.before);
    SoundOwner sound(g_select.owner); // coop-menuse: the item window's sounds are its player's

    // Retail's step 2, on the select's own block, buffer and pad.
    frameCaputer = 0;
    driveStepHold = 0;
    miniItemSelNo = -1;
    std::swap(GamePad, g_select.pad);
    read_buffer = reinterpret_cast<u_int *>(ArenaBlock(&kSelectArena, kSelectBufferBytes));
    InitEventItemSelect(kSelectBlock, BtEventInfo.item_select_list, &UserStatus->item_pack, 0xB4, 0xD2, BtEventInfo.item_select_filtered, 0);
    read_buffer = read;
    std::swap(GamePad, g_select.pad);
    BtMiniItemSelect_Sled = 4;
    std::printf("coop-fullmenu: player %d's item select opens beside player %d's menu\n", g_select.owner, g_full.owner);
}

// Its tick, after the menus' draw: its step and draw (the draw on its owner's copy), the world behind it captured.
void StepSelect() {
    if (!g_select.live) {
        return;
    }

    const int local = NetplayLocalPlayer();
    const bool shown = local == 0 || local == g_select.owner;
    gfx::TextureHandle previous = gfx::CurrentRenderTarget();

    if (!shown) {
        if (g_hidden == gfx::kNullTexture) {
            g_hidden = gfx::CreateRenderTarget(640, 480, true);
        }
        gfx::SetRenderTarget(g_hidden);
    }

    PlayerPadIn(g_select.pad, g_select.owner, g_select.before);
    std::swap(GamePad, g_select.pad);
    int done;
    {
        SoundOwner sound(g_select.owner); // coop-menuse
        done = EventItemSelectLoop(&miniItemSelNo);
    }
    std::swap(GamePad, g_select.pad);

    if (!shown) {
        gfx::SetRenderTarget(previous);
    }

    if (g_select.ticks++ % 60 == 0) {
        std::printf("coop-fullmenu: player %d's item select open %d ticks, drawn %s\n", g_select.owner, g_select.ticks - 1, shown ? "here" : "hidden");
    }

    frameCaputer = done == 0 ? 1 : 0;
    if (done != 0) {
        g_select.live = false;
        BtMiniItemSelect_Sled = 3;
        std::printf("coop-fullmenu: player %d's item select closes after %d ticks (item %d)\n", g_select.owner, g_select.ticks, miniItemSelNo);
    }
}

// The menu closes (or ends) with the select open: the select goes on as retail's.
void SelectToRetail() {
    if (!g_select.live) {
        return;
    }

    g_select.live = false;
    GamePad = g_select.pad;
    frameCaputer = 0;
    BtMiniItemSelect_Sled = 3;
    BtGameModeFlag = BT_GAME_MODE_ITEM_SELECT;
    std::printf("coop-fullmenu: the menu closed with player %d's item select open: it goes on alone\n", g_select.owner);
}

// Test hook (DC_COOP_ITEMSELECT=T): at T ticks of the menu presses' clock, a script's item window as a door's would open
// it (the whole pack offered, no result written back); its end goes back to play, as no script follows it.
void TestItemSelect() {
    static const char *spec = TestEnv("DC_COOP_ITEMSELECT");
    static bool        running = false;

    if (spec == nullptr) {
        return;
    }

    if (running && gameTask == GAME_TASK_SCRIPT_RUN) {
        running = false;
        gameTask = GAME_TASK_PLAY;
        std::printf("coop-fullmenu: test: item select done (item %d)\n", miniItemSelNo);
    }

    if (running || NetplayTestPressClock() != std::atoll(spec) || gameTask != GAME_TASK_PLAY) {
        return;
    }

    running = true;
    BtEventInfo.item_select_list[0] = -1;
    BtEventInfo.item_select_filtered = 0;
    BtEventInfo.item_select_result = 0;
    BtMiniItemSelect();
    gameTask = GAME_TASK_SCRIPT_ITEM_SELECT;
    std::printf("coop-fullmenu: test: item select starts (menu %s)\n",
                g_full.live || PlayerInMenu(1, kActorMenuLocal) || PlayerInMenu(2, kActorMenuLocal) ? "open" : "shut");
}

} // namespace

// coop-menudepth (Jep's pack 8 playtest, 11-04: Toan drawn on his Weapons page where the weapon spins, seen from behind):
// retail's menu frames draw the menu alone, over a depth buffer the frame start cleared. A menu beside the world draws over
// the world's frame, its depth still there, so the page's 3D (the weapon models, ~60 out from the menu camera) was tested
// against the world's: with the game's camera close behind Toan his body hid them, and the dimmed still showed him in their
// place. Before such a menu draws the depth is cleared, on the copy that shows it. DC_COOP_MUTATE=menudepth_kept: kept (twin).
// DC_COOP_TEST_MENU_DEPTH=1: on the Weapons page, its models ready, the depth at (320, 400) after the frame (a spot no page
// model reaches, the world's floor or Toan under it) is read back each draw (mglib's slot 15) and logged.
void MenuOverWorldDepth(int owner) {
    COOP_TEST_SWITCH(bool, probe, TestEnvInt("DC_COOP_TEST_MENU_DEPTH", 0) != 0);
    static int logged = 0;
    static int ready = 0;
    static int counted = -1; // (a warm pair's next job logs its own three)

    if (counted != TestJobGeneration()) {
        counted = TestJobGeneration();
        logged = 0;
        ready = 0;
    }

    if (!Mutated("menudepth_kept")) {
        MGClearZBuffer(0);
    }
    if (!probe) {
        return;
    }
    const bool page = BattleMenuFlag == BTLMENU_STATE_WEAPON && MenuExTextureReadFlag == MENU_EX_TEX_READY;

    ready = page ? ready + 1 : 0;
    // (the read is queued for this frame's end and comes back with a later one: the first few readings are the page's opening)
    if (ready > 10 && mgPickZBuff[15].enable != 0 && logged < 3) {
        logged++;
        std::printf("coop-menudepth: test: player %d's Weapons page: world depth under it %d (%s) at link tick %lld\n", owner,
                    (int) mgPickZBuff[15].z, mgPickZBuff[15].z > 0 ? "kept" : "cleared", (long long) NetplayLinkTick());
    }
    mgPickZBuff[15].enable = page ? 1 : 0;
    mgPickZBuff[15].x = 320;
    mgPickZBuff[15].y = 400;
}

void MenuCameraSwap(MenuCameraOwn &own) {
    if (Mutated("localmenu_camera_here")) {
        return;
    }

    CCamera &world = MenuCamera;

    if (!own.set) {
        own.set = true;
        std::memcpy(own.pos, world.pos, sizeof own.pos);
        std::memcpy(own.ref, world.ref, sizeof own.ref);
        std::memcpy(own.next_pos, world.next_pos, sizeof own.next_pos);
        std::memcpy(own.next_ref, world.next_ref, sizeof own.next_ref);
        own.roll = world.roll;
        own.limit_move = world.limit_move;
        own.speed = world.speed;
        own.angle_h = world.angle_h;
        own.angle_v = world.angle_v;
        own.snap_range = world.snap_range;
    }
    std::swap(own.pos, world.pos);
    std::swap(own.ref, world.ref);
    std::swap(own.next_pos, world.next_pos);
    std::swap(own.next_ref, world.next_ref);
    std::swap(own.roll, world.roll);
    std::swap(own.limit_move, world.limit_move);
    std::swap(own.speed, world.speed);
    std::swap(own.angle_h, world.angle_h);
    std::swap(own.angle_v, world.angle_v);
    std::swap(own.snap_range, world.snap_range);
}

// coop-menuleakfix (train 49, t-menuleak-select: with Local menus 3a her menu is her own copy's, so his script item window
// ("use an item" at a door: the world's GAME_TASK_SCRIPT_ITEM_SELECT, BT_GAME_MODE_ITEM_SELECT) drew on every copy, under her
// open menu on hers, and that tick drew nothing else there, so her menu kept a black still): the window's loop (its step reads
// port 1, the agreed pad, on every copy alike) draws on the copy of the player it is for, NetplayMenuOwner (port 1's), and into
// a target nobody sees on the others, which draw the world instead (DunMainDraw, its draw-only randoms aside, as a menu's
// copy draws the world behind it). DC_COOP_MUTATE=itemselect_everywhere is the twin (on every copy, as train 49 brought in).
int ItemSelectLoop() {
    const int local = NetplayLocalPlayer();
    const int owner = NetplayMenuOwner() != 0 ? NetplayMenuOwner() : 1;

    if (local == 0 || local == owner || Mutated("itemselect_everywhere")) {
        return EventItemSelectLoop(&miniItemSelNo);
    }

    static int hidden_ticks = 0;
    gfx::TextureHandle previous = gfx::CurrentRenderTarget();

    if (g_hidden == gfx::kNullTexture) {
        g_hidden = gfx::CreateRenderTarget(640, 480, true);
    }
    gfx::SetRenderTarget(g_hidden);
    const int done = EventItemSelectLoop(&miniItemSelNo);
    gfx::SetRenderTarget(previous);
    {
        // (the view the world's draw leaves is summed and read by the next tick's clip tests: his copy's tick drew only the
        // window, so this draw's view goes back, as a local menu's own copy keeps it, LocalPad)
        const RenderInfo view = mgRenderInfo;
        DrawRng          rng;
        DunMainDraw();
        mgRenderInfo = view;
    }
    if (hidden_ticks++ % 60 == 0) {
        std::printf("coop-fullmenu: player %d's script item window drawn hidden on player %d's copy, the world in its place\n", owner, local);
    }
    if (done != 0) {
        hidden_ticks = 0;
    }
    return done;
}

// coop-pt1109d (Jep's 11-09 playtest: OUT OF SYNC at tick 4500 on a floor, the save's byte 0x1d2, now_time 4.15 on his copy, 5.15 on hers). The ring's save-or-load and
// quit choosers are the port's, and BattleMenuInit, which starts everything else a menu has, left them as they were. A menu carried through a merge with its chooser up
// (FullMenuRetailToLocal, CoopTownMenuRetailToLocal) goes on from a copy of the globals, so the flag stayed set in them; the next menu to start from them, of any kind on
// any ring, opened with that chooser up and its first Cross took Save: InitMenuSave, whose EditSave writes the town's NowTime over the save's clock on that copy alone.
// Every menu's start now shuts them. DC_COOP_MUTATE=ring_chooser_stays (twin): as before.
void FullMenuStartsClean() {
    if (!Mutated("ring_chooser_stays")) {
        BattleMenuRingChoosersOff();
    }
}

bool FullMenuLive() {
    return g_full.live;
}

// coop-dungeonsuite (x-dungeon-suite small -Who 1 -Other menu: both copies hung for good): a menu beside the world reads its pack
// (dunmenu5.pak) into the background read list's first file and takes its textures from there once the read is done
// (BattleMenuAppear). Player 1's chest or Atla presentation starting meanwhile restarts that list with its own files
// (StartReadBG), so the menu then walked the item's model as a pack and never came back (GetPackFile on a non-pack). The
// presentation waits the few ticks until the menu's read is in. DC_COOP_MUTATE=menuload starts it at once (the twin).
bool FullMenuLoading() {
    static int waited = 0;

    if (!g_full.live || BtlMenuReadEndFlag != 0 || Mutated("menuload")) {
        waited = 0;
        return false;
    }

    // At most two seconds: a read that never ends must not hold player 1 for good.
    if (++waited > 120) {
        return false;
    }

    if (waited == 1) {
        std::printf("coop-fullmenu: a presentation waits for player %d's menu to finish reading\n", g_full.owner);
    }

    return true;
}

// coop-chestmenu (Jep's 2026-10-28 Steam run: "P2 was in a menu when P1 opened a chest", then the chest's animation broke and
// both crashed in DrawDungeon's MGDraw(itemBoxModel)): retail's dungeon menu works in read_buffer (BtlMenuBufferSet) and
// so do the chest, gate key and escape presentations (BtCashBuffer), which retail never runs together. Beside her menu his
// chest's files went over her menu's pack, and her pages' reads over his item model. The presentations take a block of
// their own (the same size, pinned, in the state set), and keep the open menu's read list (their StartReadBG cleared it:
// the menu finds its files by slot 0). DC_COOP_MUTATE=chestbuffer: read_buffer and the cleared list, as before (twin).
u_char *PresentationBuffer(u_char *retail) {
    constexpr std::size_t kBytes = 0x445C0u * 16; // BtCashBuffer.limit's quads
    static const char     kPresentationArena = 0;
    static u_char        *block = nullptr;

    if (Mutated("chestbuffer")) {
        return retail;
    }

    if (block == nullptr) {
        block = ArenaBlock(&kPresentationArena, kBytes);
    }

    return block;
}

namespace {

std::vector<unsigned char> g_kept_reads;
bool                       g_reads_kept = false;

} // namespace

// coop-servecrash: the menu beside a presentation, a local one (g_local) too: its player, 0 for none.
static int PresentationMenuOwner() {
    return FullMenuOwner() != 0 ? FullMenuOwner() : g_local.live ? g_local.owner : 0;
}

void PresentationReadsBegin() {
    // coop-servecrash: a local menu (g_local, on its owner's copy alone) keeps its reads too; since localmenu3d her menu
    // beside his chest or Atla lost its pack (serve run: her copy hung at his chest, crashed at his Atla).
    g_reads_kept = (FirstOpen() || g_second.live || g_local.live) && !Mutated("chestbuffer");

    if (g_reads_kept) {
        g_kept_reads.clear();
        ReadBGRegions([](void *at, std::size_t size) {
            const unsigned char *bytes = static_cast<const unsigned char *>(at);
            g_kept_reads.insert(g_kept_reads.end(), bytes, bytes + size);
        });
    }
}

void PresentationReadsEnd() {
    if (!g_reads_kept) {
        return;
    }

    g_reads_kept = false;
    ReadBGRegions([](void *at, std::size_t size) { std::memcpy(at, g_kept_reads.data(), std::min(size, g_kept_reads.size())); });
    std::printf("coop-fullmenu: a presentation beside player %d's menu read into its own block and kept the menu's reads\n", PresentationMenuOwner());
}

// Test hook (DC_COOP_TEST_CHESTMENU=1, x-chest-menu): whether the presentation's files and the open menu's pack still hold the
// bytes they were read with. A line the first time either changes, on stderr. Read only.
namespace {

std::uint64_t Fnv(const unsigned char *at, std::size_t size) {
    std::uint64_t hash = 1469598103934665603ull;

    for (std::size_t i = 0; i < size; i++) {
        hash = (hash ^ at[i]) * 1099511628211ull;
    }

    return hash;
}

struct Watched {
    const unsigned char *at = nullptr;
    std::size_t          size = 0;
    std::uint64_t        hash = 0;
    bool                 told = false;

    void Take(const void *from, std::size_t bytes) {
        at = static_cast<const unsigned char *>(from);
        size = bytes;
        hash = at != nullptr ? Fnv(at, size) : 0;
        told = false;
    }

    void Check(const char *what) {
        if (at == nullptr || told || Fnv(at, size) == hash) {
            return;
        }

        told = true;
        std::fprintf(stderr, "coop-test: %s changed (%zu bytes at %p), gameTask %#x, player %d's menu %s\n", what, size,
                     static_cast<const void *>(at), (unsigned) gameTask, PresentationMenuOwner(), PresentationMenuOwner() != 0 ? "open" : "shut");
    }
};

Watched              g_pres_files;
const unsigned char *g_pres_pending = nullptr;
Watched              g_menu_pack;

bool ChestMenuTest() {
    static const bool on = TestEnvOn("DC_COOP_TEST_CHESTMENU");
    return on;
}

} // namespace

void PresentationFilesTrace(const void *at, std::size_t size) {
    if (!ChestMenuTest()) {
        return;
    }

    // (hashed once the presentation's loop has built its models from them: LoadMDSFile fixes them up)
    g_pres_files = Watched{.size = size};
    g_pres_pending = static_cast<const unsigned char *>(at);
    std::fprintf(stderr, "coop-test: presentation files read (%zu bytes at %p, %s), player %d's menu %s\n", size, at,
                 at == static_cast<const void *>(read_buffer) ? "read_buffer" : "own block", PresentationMenuOwner(),
                 PresentationMenuOwner() != 0 ? "open" : "shut");
}

void PresentationFilesCheck(bool built) {
    if (!ChestMenuTest()) {
        return;
    }

    if (g_pres_pending != nullptr && built) {
        g_pres_files.Take(g_pres_pending, g_pres_files.size);
        g_pres_pending = nullptr;
        return;
    }

    g_pres_files.Check("the presentation's files");
}

namespace {

void MenuPackTrace() {
    if (!ChestMenuTest()) {
        return;
    }

    if (!g_full.live && !g_local.live) {
        g_menu_pack.at = nullptr;
        return;
    }

    if (g_menu_pack.at == nullptr && BtlMenuReadEndFlag != 0) {
        if (BG_READ_INFO *file = GetReadBGFile(0)) {
            g_menu_pack.Take(file->buffer, (std::size_t) file->size);
            std::fprintf(stderr, "coop-test: player %d's menu pack read (%zu bytes at %p)\n", g_full.live ? g_full.owner : g_local.owner, g_menu_pack.size,
                         static_cast<const void *>(g_menu_pack.at));
        }
        return;
    }

    g_menu_pack.Check("the menu's pack");
}

} // namespace

int FullMenuOwner() {
    return FirstOpen() ? g_full.owner : g_second.live ? g_second.owner : 0;
}

int FullMenuViewPlayerChara() {
    return FullMenuRunsFor(2) && g_full.player_chara >= 0 ? g_full.player_chara : -1;
}

bool FullMenuRunsFor(int player) {
    return (FirstOpen() && g_full.owner == player) || (g_full.live && g_second.live && g_second.owner == player) ||
           FullMenuRunsLocalFor(player);
}

bool FullMenuRunsLocalFor(int player) {
    return g_local.live && g_local.owner == player;
}

bool FullMenuLocalPick(int player, int wanted) {
    return LocalPick(player, wanted);
}

// coop-pt1111k test hook: a local menu's pick of wanted taken on this tick, as its kCmdPick is on every copy (ApplyPick).
void FullMenuApplyPickForTest(int player, int wanted) {
    NetCommand command;

    command.kind = kCmdPick;
    command.a = (std::uint8_t) wanted;
    ApplyPick(player, command, NetplayLinkTick());
}

// DC_COOP_MUTATE=second_escape_lost (the escape rows' twin; with local menus): the close command carries no map jump.
int FullMenuCloseJump(int player) {
    return g_local.owner == player && !Mutated("second_escape_lost") ? g_local.jump : 0;
}

bool FullMenuLocalOn() {
    return LocalMenusOn();
}

// coop-small2: local menus as a merge carries one (no linked partner yet: LocalMenusOn's conditions but that one, as CoopTownMenuCarries)
bool FullMenuCarryOn() {
    return Enabled() && MenusOn() && !Mutated("fullmenu_lockstep") && !Mutated("fullmenufreeze");
}

// coop-soakfix4: local menus on but for the partner being linked (a catch-up under way counts: the link is up). The same rule as
// coop-small2's FullMenuCarryOn (merged in train 56: both branches added it under their own names).
bool FullMenuLocalOnLinkUp() {
    return FullMenuCarryOn();
}

namespace {
std::optional<TownStatus>  g_town_status; // coop-stage4: a town menu's step or draw in progress
std::optional<LocalWrites> g_town_writes;
} // namespace

void FullMenuWritesBegin(bool collect) {
    CoopPartsWritesBegin(collect); // coop-pt1108
    g_town_status.emplace();
    g_town_writes.emplace(collect);
}

void FullMenuWritesEnd() {
    g_town_writes.reset();
    g_town_status.reset();
    CoopPartsWritesEnd(); // coop-pt1108
}

// Every copy, on the tick the opening Triangle is agreed: the bit everywhere, the engine on the owner's copy.
// A local menu's commands, 3c on (kCmdPoke), coop-bagsendlocal (kCmdGive). coop-pt1106-lockup: a copy that joined beside a menu already
// open never ran its opening, so these are set once on every copy (ActorMenuSync): set at the opening alone, its writes were dropped there.
void FullMenuCommandHandlers() {
    NetplaySetCommandHandler(kCmdPoke, ApplyPoke);
    NetplaySetCommandHandler(kCmdPick, ApplyPick);
    NetplaySetCommandHandler(kCmdGive, InventoryApplyGive);
    CoopP2GeoramaCommandHandler(); // coop-stage4: her Georama parts menu's close (kCmdGeoMenu)
    CoopScriptMenuCommandHandler(); // coop-scriptmenus: a script's menu's end (kCmdScriptMenuEnd)
    CoopStorageCommandHandlers();   // coop-small2: the party's storage, its claims and moves (kCmdStorageClaim, kCmdStorageMove)
}

static void OpenLocalFor(int owner) {
    const bool here = NetplayLocalPlayer() == owner;

    if (owner == 1) {
        ReleasePortOne(GamePad); // the press is the menu's: none of the tick's later checks (the pause, a standing Triangle) see it
        oldUnitNow = nowUnitNow; // retail's menu start keeps it for its close's member check
    }
    ActorMenuOpenHere(owner, kActorMenuFull | kActorMenuLocal);
    FullMenuCommandHandlers();
    std::printf("coop-localmenu: player %d's menu opens at link tick %lld, %s\n", owner, (long long) NetplayLinkTick(),
                here ? "its engine on this copy" : "on their own copy");
    if (!here) {
        return;
    }

    WriteGuardMenu guard("local menu open"); // coop-guard (a test's: DC_QA_WRITE_GUARD)
    SoundOwner     sound(owner);
    DrawRng        rng;
#ifdef PAL
    s32 menu[6] = {0x17, 0x18, 0x19, 0x28, 0x29};
#else
    s32 menu[5] = {0x17, 0x18, 0x19, 0x28, 0x29};
#endif

    SndSePlay(MENU_SOUND_CONFIRM, -1, 0);
    g_local = LocalMenu{};
    g_local.live = true;
    g_local.owner = owner;
    g_local.pad = GamePad;
    NetplayPlayerPad(owner, g_local.before);
    {
        LocalPad pad;
        OwnerViewBegin(owner);
        {
            PartsWritesScope parts_writes(true); // coop-pt1108
            LocalWrites writes(true); // the menu's start writes too (a debug start's party grant): commands, as its step's are
            BattleMenuInit(menu, BATTLE_MENU_MODE_DUNGEON);
        }
        OwnerViewEnd();
    }
}

// coop-pt1106-lockup (Jep, 2026-11-06: a player joining is not made to wait; x-fixture-join-hostmenu-floor: the state set "waits for
// a quiet frame" with task 0x22 for as long as the menu stays open, her screen black, and a place checkpoint has no fallback): the
// sending copy's retail menu on a floor, opened alone, is the world's task and stops the world, so no state set can be taken
// under it. As the take comes due it carries on as this copy's local menu: its pack, globals and pad are the ones in place, the
// world's task goes back to play and the in-menu bit goes in the set. Its close is a local menu's: the close command runs
// retail's close on every copy. Only a menu on the world's own view (OwnerHasView), or a stayer's at its floor merge (stayer, the player its copy is, the link not up yet: her copy's roles swap
// as it links, CoopWorldsFloorBecomePartner, and the swap's character change takes the task from under the menu, so it goes on as a local one first,
// her member and bag from then on OwnerViewBegin's). DC_COOP_MUTATE=retail_menu_holds_join (twin): the take waits, as before.
bool FullMenuRetailToLocal(int stayer) {
    const int owner = stayer != 0 ? stayer : NetplayLocalPlayer();

    if ((OwnerHasView(owner) && stayer == 0) || BtGameModeFlag != BT_GAME_MODE_BATTLE_MENU || gameTask != GAME_TASK_MENU_CLOSE || g_full.live ||
        g_local.live || BtlMenuReadEndFlag == 0 || MenuMapJumpMode != 0 || Mutated("retail_menu_holds_join")) {
        return false;
    }

    g_local = LocalMenu{};
    g_local.live = true;
    g_local.owner = owner;
    g_local.pad = GamePad;
    NetplayPlayerPad(owner, g_local.before);
    GamePad.MenuModeOff();
    GamePad.AutoRepeatOff();
    ActorMenuOpenHere(owner, kActorMenuFull | kActorMenuLocal);
    FullMenuCommandHandlers();
    infoMap = infoMapOld; // (retail's opening hid it on the world; the joiner's map is not this menu's)
    BtGameModeFlag = BT_GAME_MODE_PLAY;
    gameTask = GAME_TASK_PLAY;
    std::printf("coop-localmenu: player %d's menu opened alone goes on as a local menu at link tick %lld (a join's state set is due, page state %d)\n", owner, (long long) NetplayLinkTick(), (int) BattleMenuFlag);
    return true;
}

// coop-soakfix4 (soak seeds 1079, 1191, 1271, 1303: OUT OF SYNC on "view" some 40 ticks into a join's lockstep): his menu
// opened during a catch-up, the link not yet its players' (LocalMenusOn false), is the menu beside the running world (g_full),
// on both copies alike: the host's own, the joiner's in her replay. Linked play has no such menu, only local ones, and it
// went on into the lockstep: her Triangle opened her local menu on both copies and, on his alone (her copy's tick runs her
// local menu's engine instead), a second menu of his g_full; his copy's g_full drew his menu last (its view the menu's,
// summed by the checksum and read by the next tick's clip tests), hers drew the world. As the lockstep starts, on every
// copy at its first tick, his menu goes on as a local menu (as a retail menu opened alone does as a take comes due,
// FullMenuRetailToLocal): its globals and pad in place on his copy, the world's task and mode back to play everywhere.
// DC_COOP_MUTATE=full_menu_kept (twin): it stays the menu beside the world, as before.
namespace {
void FullToLocal(int owner, const char *when);
}

bool FullMenuFullToLocal() {
    // coop-soakfix4 (soak seed 1006: OUT OF SYNC at 1620, B's world held for good): or retail's own menu, opened during the
    // catch-up while her body was on no copy yet (FullMenuBegin wants it, so no menu beside the world: the world stopped, on
    // both copies, his and hers in the replay). In the lockstep local menus are on, so Route moves no port for it: each copy's
    // retail menu read its own player's pad, his Circle closed it on his copy alone and hers stayed in it (task 0x22, the
    // world held). As the lockstep starts it goes on as his local menu on every copy, as at a take (FullMenuRetailToLocal).
    // DC_COOP_MUTATE=retail_kept_lockstep (twin): it stays retail's, as before.
    const bool retail = !g_full.live && !g_local.live && mode == 3 /* GAME_MODE_DUNGEON */ && BtGameModeFlag == BT_GAME_MODE_BATTLE_MENU &&
                        gameTask == GAME_TASK_MENU_CLOSE && BtlMenuReadEndFlag != 0 && MenuMapJumpMode == 0 && !Mutated("retail_kept_lockstep");

    if (!g_full.live && !retail) {
        return false;
    }
    const int owner = g_full.live ? g_full.owner : NetplayMenuOwner();

    if (retail) {
        if (!Enabled() || !MenusOn() || OwnerHasView(owner) || Mutated("fullmenu_lockstep") || Mutated("fullmenufreeze")) {
            std::printf("coop-localmenu: player %d's retail menu stays so into the lockstep\n", owner);
            return false;
        }
        const bool here = NetplayLocalPlayer() == owner;

        if (here) {
            g_local = LocalMenu{};
            g_local.live = true;
            g_local.owner = owner;
            g_local.pad = GamePad;
            NetplayPlayerPad(owner, g_local.before);
        }
        GamePad.MenuModeOff();
        GamePad.AutoRepeatOff();
        ActorMenuOpenHere(owner, kActorMenuFull | kActorMenuLocal);
        FullMenuCommandHandlers();
        infoMap = infoMapOld; // (retail's opening hid it on the world)
        BtGameModeFlag = BT_GAME_MODE_PLAY;
        gameTask = GAME_TASK_PLAY;
        std::printf("coop-localmenu: player %d's retail menu goes on as a local menu at link tick %lld (the lockstep starts, %s, page state %d)\n", owner,
                    (long long) NetplayLinkTick(), here ? "its engine on this copy" : "on their own copy", (int) BattleMenuFlag);
        return true;
    }

    if (!Enabled() || !MenusOn() || OwnerHasView(owner) || g_second.live || g_select.live || Mutated("fullmenu_lockstep") ||
        Mutated("fullmenufreeze") || Mutated("full_menu_kept")) {
        std::printf("coop-localmenu: player %d's menu beside the world stays so into the lockstep (second %d, item select %d)\n", owner,
                    g_second.live ? 1 : 0, g_select.live ? 1 : 0);
        return false;
    }
    FullToLocal(owner, "the lockstep starts");
    return true;
}

// coop-repinfix2 (soak seed leave 2239 on the re-pin, PC-02 job 17: B2 0xC0000005 in CUserStatus::Step under BattleMenuCursor,
// GameLoop, WorldStep, right after it put back the host's state set): her dropped copy's new copy rejoined at link tick 6; her
// body still on his copy, his Triangle at tick 10 opened the menu beside the running world (g_full), not retail's, and the take
// waited out kSnapMenuWaitMs for a frame without it, then went with it open ("the state set is taken with the menu open").
// FullMenuFullToLocal is for a g_full opened after the take (the joiner opens it too, in her replay); this one was opened before
// it, so the set gave B2 the world's mode BT_GAME_MODE_BATTLE_MENU and no menu (its globals are not in the set, and her own
// BtlMenuStatusPt was null): her first GameLoop stepped a menu that was not there. (Train 56's build passed the seed by timing:
// there the rejoin linked at tick 40, after his menu had opened alone as retail's, which a take carries on as a local menu.)
// Now, as a take comes due on a floor, his menu beside the world goes on as a local menu as retail's does
// (FullMenuRetailToLocal): its globals and pad in place on its owner's copy, the world's mode and task back to play, the
// in-menu bit in the set. DC_COOP_MUTATE=full_menu_at_take (twin): it stays the menu beside the world, as before.
bool FullMenuFullAtTake() {
    if (!g_full.live || g_full.opening || BtlMenuReadEndFlag == 0 || MenuMapJumpMode != 0 || Mutated("full_menu_at_take")) {
        return false;
    }
    const int owner = g_full.owner;

    if (!Enabled() || !MenusOn() || OwnerHasView(owner) || g_second.live || g_select.live || Mutated("fullmenu_lockstep") ||
        Mutated("fullmenufreeze")) {
        if (static bool said = false; !said) {
            said = true;
            std::printf("coop-localmenu: player %d's menu beside the world stays so as the take comes (second %d, item select %d)\n", owner,
                        g_second.live ? 1 : 0, g_select.live ? 1 : 0);
        }
        return false;
    }
    FullToLocal(owner, "a join's state set is due");
    return true;
}

namespace {

// The menu beside the world (g_full) on as owner's local menu (FullMenuFullToLocal, FullMenuFullAtTake).
void FullToLocal(int owner, const char *when) {
    const bool here = NetplayLocalPlayer() == owner;

    g_full.live = false;
    g_full.opening = false;
    if (here) {
        g_local = LocalMenu{};
        g_local.live = true;
        g_local.owner = owner;
        g_local.pad = GamePad;
        NetplayPlayerPad(owner, g_local.before);
    }
    GamePad.MenuModeOff();
    GamePad.AutoRepeatOff();
    ActorMenuOpenHere(owner, kActorMenuFull | kActorMenuLocal);
    FullMenuCommandHandlers();
    BtGameModeFlag = BT_GAME_MODE_PLAY;
    if (gameTask == GAME_TASK_MENU_CLOSE) {
        gameTask = GAME_TASK_PLAY;
    }
    std::printf("coop-localmenu: player %d's menu beside the world goes on as a local menu at link tick %lld (%s, %s, page state %d)\n",
                owner, (long long) NetplayLinkTick(), when, here ? "its engine on this copy" : "on their own copy", (int) BattleMenuFlag);
}

} // namespace

bool FullMenuOpenLocal() {
    // Player 1's, from retail's opening on port 1 (local menus move no port: NetplayMenuOwner stays 1).
    if (!LocalMenusOn() || NetplayMenuOwner() != 1 || !g_partner.visible) {
        return false;
    }

    OpenLocalFor(1);
    return true;
}

// coop-menulocal: the button that opens a player's local menu: Triangle, or Start with game.coop_menu_panel on (Triangle is
// the overlay's then, as player 1's Start opens his: MenuOpensGameMenu). DC_COOP_MUTATE=panel_triangle_menu (the twin):
// Triangle whatever the overlay and her open overlay no bar, as before, so her Triangle opened her overlay and her menu at once.
std::uint16_t FullMenuLocalButton() {
    return MenuPanelOn() && !Mutated("panel_triangle_menu") ? kInputStart : kInputTriangle;
}

// coop-localmenu 3b: player 2's opening, from her agreed Triangle in the world's free play (the same test on every copy).
// Players past 2 take the same path once they have a body on the floor. (coop-menulocal: Start with the overlay on, and not
// while her overlay is open, which takes her pad as his takes his: MenuFilterPort.)
// coop-atlamenu (Jep's 2026-11-04 pack 8 playtest: "while P1 is collecting an Atla, P2 can't open their own menu"): his chest,
// Atla or gate key presentation is the world's task, not play, but the world runs on for her (coop-chestworld), so her menu
// opens then too; its reads stay off his files (coop-chestmenu's own block) and its close leaves his task alone (coop-bigchest).
// DC_COOP_MUTATE=atlamenu_play_only (the twin): in play only, as before.
// coop-soakfix1 (the soak's reds, seeds 26111202 floor and 26111220 menus, OUT OF SYNC at ticks 600 and 120, view and stages
// apart, random too): his Triangle in the first-person view (GAME_TASK_EYE_CAMERA) opens retail's menu, which every copy steps
// in the lockstep (FullMenuBegin, g_full), not a local one. Her Triangle then opened a local menu on her copy alone: its
// BattleMenuInit ran over his menu's globals there and her copy went to LocalGameLoop, so it stopped stepping his menu while
// his copy stepped it. Beside a lockstep menu hers is its second menu (TrySecondOpen), on every copy, as his own Triangle
// waits for no menu to be live (MenuOpensGameMenu). DC_COOP_MUTATE=local_beside_lockstep (twin): hers opens local, as before.
static void OpenOthersLocal() {
    const bool world_task_ok = gameTask == GAME_TASK_PLAY || (PresentationTask() && !Mutated("atlamenu_play_only"));

    if (g_full.live && !Mutated("local_beside_lockstep")) {
        return;
    }

    if (!LocalMenusOn() || !world_task_ok || BtEventMode != 0 || !g_partner.visible || g_partner.loaded_chara < 0 ||
        g_partner.down > 0 || PausePlayer(2) || PlayerInMenu(2) || (MenuOpen(2) && !Mutated("panel_triangle_menu")) ||
        !NetplayPlayerPressed(2, FullMenuLocalButton())) {
        return;
    }

    OpenLocalFor(2);
}

void FullMenuClosedByCommand(int player, int jump) {
    if (FullMenuRunsLocalFor(player)) {
        return; // its engine still runs: an old close (a state load's resend)
    }

    // coop-guard: the bag's melt timer as the close runs, every copy (t-triangle-menu's local rows: alike on all of them)
    std::printf("coop-localmenu: melt timer as player %d's close runs at link tick %lld: frame %d, counts %d, on %d\n", player, (long long) NetplayLinkTick(),
                (int) ItemVolumeStep.frame, (int) ItemVolumeStep.pending_volume, (int) ItemVolumeStep.enabled);
    if (player != 1 && jump == 0) {
        std::printf("coop-localmenu: player %d's menu close runs at link tick %lld (no close task: the world's is player 1's)\n", player,
                    (long long) NetplayLinkTick());
        return;
    }

    MenuMapJumpMode = jump;
    if (gameTask == GAME_TASK_PLAY) {
        gameTask = GAME_TASK_MENU_CLOSE;
    }
    if (jump != 0) {
        CoopWorldsExitBy(player);
    }
    std::printf("coop-localmenu: player %d's menu close runs at link tick %lld (map jump %d, task %#x)\n", player,
                (long long) NetplayLinkTick(), jump, gameTask);
}

bool FullMenuOpenFor(int player) {
    return PlayerInMenu(player, kActorMenuFull);
}

void FullMenuStepping(int player) {
    g_stepping = player;
}

bool FullMenuQuitHere() {
    const int local = NetplayLocalPlayer();
    const int owner = g_stepping != 0 ? g_stepping : mode == 2 ? 1 : MenuGameMenuOwner();

    if (local == 0) {
        return true;
    }
    if (owner != local && !Mutated("quitboth")) {
        std::printf("coop-quit: player %d quits on their own copy; this copy (player %d) plays on\n", owner, local);
        std::fflush(stdout);
        return false;
    }
    std::printf("coop-quit: player %d quits this copy (player %d's menu)\n", local, owner);
    std::fflush(stdout);
    return true;
}

bool FullMenuQuitSent() {
    if (!NetplaySessionOpen()) {
        return false;
    }

    NetCommand command;

    command.kind = kCmdQuit;
    std::printf("coop-quit: player %d's Quit Game goes to every copy as a command at link tick %lld\n", NetplayLinkPlayer() > 0 ? NetplayLinkPlayer() : 1,
                (long long) NetplayLinkTick());
    std::fflush(stdout);
    NetplayQueueCommand(command);
    return true;
}

bool FullMenuFirstClosed() {
    return g_full.live && g_second.first_closed;
}

bool FullMenuHoldsPartner() {
    return FullMenuOpenFor(2);
}

// coop-menuflash (Jep's 2026-10-27 playtest: "when either player opens the Triangle menu, the other player loses their
// minimap"): retail's opening hides the map (SetMIniMapStatus(0)) until its close (GAME_TASK_MENU_CLOSE), and the switch
// is the world's, so both copies lost it. A linked copy whose own player has no menu open draws the map as it was before
// the opening (infoMapOld: a hide since then, an event's, still hides it), from the opening's first tick to the close.
// DC_COOP_MUTATE=menumap (twin): hidden on both, as before.
bool FullMenuMapKept() {
    const int local = NetplayLocalPlayer();

    if (local == 0 || !Enabled() || infoMap != 0 || infoMapOld == 0 || Mutated("menumap")) {
        return false;
    }

    const bool opening = gameTask >= GAME_TASK_MENU_OPEN && gameTask <= GAME_TASK_MENU_INIT && NetplayMenuOwner() != local;

    if (!opening && !(g_full.live && !FullMenuRunsFor(local))) {
        return false;
    }

    static int kept = 0;

    if (CoopDebug() && kept++ % 60 == 0) {
        std::printf("coop-fullmenu: the map stays on player %d's copy beside the other's menu (%d frames)\n", local, kept);
    }
    return true;
}

// coop-hudblink (Jep's 2026-10-27 playtest: "when either player opens the menu in the dungeon, both players' entire HUDs
// disappear for a frame or two, except the map"): retail's opening (GAME_TASK_MENU_OPEN) slides the HUD away (rogoY3
// -0x60) for its wait ticks and puts it back at GAME_TASK_MENU_INIT, and rogoY3 is the world's, so the other copy lost
// its HUD too, and the opener's came back for the menu's first tick (FullMenuBegin's world draw). A linked copy's HUD is
// hidden by its own player's menu only: beside the other's opening it draws where it was before (oldRogoY3), and the
// opener's stays away from the opening until the menu's own draw (Draw) takes over. DC_COOP_MUTATE=hudblink (twin):
// rogoY3 as it is on both.
int FullMenuHudY(int y) {
    const int local = NetplayLocalPlayer();

    if (local == 0 || !Enabled() || Mutated("hudblink")) {
        return y;
    }

    // The draw comes after the tick's step, so the opening's own tick already shows the next task.
    const bool opening = gameTask > GAME_TASK_MENU_OPEN && gameTask <= GAME_TASK_MENU_INIT;

    if (opening && NetplayMenuOwner() != local) {
        return oldRogoY3;
    }
    if (!opening && FullMenuRunsFor(local)) { // coop-localmenu: a local menu's too
        return -0x60;
    }
    return y;
}

bool FullMenuLiveScene() {
    // coop-town: player 2's own menu over the live town; coop-menuanim: and player 1's.
    return g_full.live || g_local.live || CoopP2TownMenuDrawing() || CoopTownP1MenuDrawing(); // coop-localmenu: a local one
}

void FullMenuBegin() {
    g_full.live = false;

    int owner = NetplayMenuOwner();

    // Player 2's needs the link (her own copy shows it); on one screen her Start still opens the pausing one.
    if (!Enabled() || !MenusOn() || !g_partner.visible || (owner == 2 && NetplayLocalPlayer() == 0) || Mutated("fullmenufreeze")) {
        return;
    }

    g_full.live = true;
    g_full.owner = owner;
    NetplayPlayerPad(1, g_full.p1_before);
    g_full.ticks = 0;
    g_full.task_logged = -1;
    std::printf("coop-fullmenu: player %d's menu opens beside the running world\n", g_full.owner);

    // Jep's note 12 (2026-10-25: "If P1 opens the menu, P2s screen flashes blue"): this tick's GameLoop would draw
    // only the menu (BattleMenuDraw, without its still frame, as the world is live), which has nothing on screen yet,
    // so both copies showed one bare frame of the clear colour. It draws the world (MainDraw) instead; the menu's
    // mode comes back after it (FullMenuGameLoop). DC_COOP_MUTATE=menuflash: the bare frame (twin).
    if (!Mutated("menuflash")) {
        g_full.opening = true;
        BtGameModeFlag = BT_GAME_MODE_PLAY;
    }
}

// Diagnostic (DC_COOP_MENU_TRACE=1): the 40 ticks after the menu closes, the player's motion and the camera, a line each.
void CloseTrace(bool closed_now, bool closing = false) {
    static const bool on = TestEnvOn("DC_COOP_MENU_TRACE");
    static int        left = 0;

    if (!on) {
        return;
    }

    if (closed_now) {
        left = 40;
    }

    if ((left <= 0 && !closing) || UserStatus == nullptr || NowCamera__3 == nullptr) {
        return;
    }

    sceVu0FVECTOR eye;
    sceVu0FVECTOR ref;

    NowCamera__3->GetPos(eye);
    NowCamera__3->GetRef(ref);
    std::printf("coop-fullmenu: %s %d: task %#x motion %d/%d time %.2f action %d eye (%.1f %.1f %.1f) ref (%.1f %.1f %.1f)\n",
                closing ? "closing" : "after close", closing ? g_full.ticks : 40 - left, gameTask, (int) BtActStatus.motion_no, (int) CharaMain.motion_no,
                CharaMain.motion_type.state.time, (int) BtActStatus.action_on, eye[0], eye[1], eye[2], ref[0], ref[1], ref[2]);
    if (!closing) {
        left--;
    }
}

// coop-keycrash (train 49: herweapons' twin crashed both copies at his swing, and a crash scores red even in a twin): the
// test looks at the world's weapon itself. Each change in whether it has the "dcol1" frame (ToanKey_Play's hit point, which
// Xiao's slingshot lacks) is a line, so a wrong build shows before his swing and the twin stops on it (its stop: tag).
void WeaponFrameTrace() {
    COOP_TEST_SWITCH(bool, on, TestEnvOn("DC_COOP_TEST_SWING_LOG"));
    static int        last = -1;
    static int        generation = -1;

    // (coop-fx51a: said again at each job on a running pair, DC_QA_SERVE: its slice reads the frame the job starts with)
    if (generation != TestJobGeneration()) {
        generation = TestJobGeneration();
        last = -1;
    }

    if (!on || UserStatus == nullptr || NowWeapon == nullptr || NowWeapon->frame == nullptr) {
        return;
    }

    const int has = NowWeapon->frame->SearchFrame((char *) "dcol1") != nullptr ? 1 : 0;

    if (has != last) {
        std::fprintf(stderr, "coop-test: player 1's weapon (member %d) %s\n", (int) UserStatus->cur_chara,
                     has ? "has its dcol1 frame" : "has no dcol1 frame: his swing would read a null frame");
        last = has;
    }
}

int FullMenuGameLoop() {
    WeaponFrameTrace();

    // coop-localmenu: the tests' shelter (DC_COOP_TEST_MENU_SHELTER, below) for a player in a local menu, from the bit, so on
    // every copy alike.
    const bool local_shelter = TestEnvOn("DC_COOP_TEST_MENU_SHELTER") || Mutated("menuguard");

    if (local_shelter && PlayerInMenu(1, kActorMenuLocal)) {
        BtActStatus.invincible_frames = std::max<int>(BtActStatus.invincible_frames, 2);
    }
    if (local_shelter && PlayerInMenu(2, kActorMenuLocal)) {
        g_partner.invincible = std::max(g_partner.invincible, 2);
    }

    OpenOthersLocal(); // coop-localmenu 3b

    // coop-menuleakfix (train 49, t-menuleak-select: OUT OF SYNC at tick 180, then a zone reset): the item-select test hook ran
    // below the local branch, so the copy whose own player had a local menu open returned before it and only the other copy
    // started the window. It starts the world's task, so it runs on every copy alike, before the branch.
    TestItemSelect();

    if (g_local.live) {
        return LocalGameLoop(); // coop-localmenu: this copy's own player's menu, the world as every copy steps it
    }

    // coop-menuse: the game's port 1 is the menu owner's pad (the link's routing), so the menu sounds the dungeon's own
    // step plays (its Triangle and Start, its lock-on) are that player's; the menus' steps below say theirs.
    SoundOwner port_one(NetplayMenuOwner(), true);

    MenuPackTrace(); // coop-chestmenu: test hook

    if (!g_full.live) {
        COOP_TEST_SWITCH(bool, swing_log, TestEnvOn("DC_COOP_TEST_SWING_LOG")); // coop-herweapon's test: the player's swings
        const int         action_before = BtActStatus.action_on;
        int               result = WorldStep(); // coop-localmenu: held beside local menus as the lockstep menu's was

        if (swing_log && action_before == 0 && BtActStatus.action_on != 0 && UserStatus != nullptr) {
            std::fprintf(stderr, "coop-test: player 1 swings (member %d, action %d)\n", (int) UserStatus->cur_chara, (int) BtActStatus.action_no);
        }

        if (g_full.opening) {
            g_full.opening = false;
            BtGameModeFlag = BT_GAME_MODE_BATTLE_MENU;
            std::printf("coop-fullmenu: the opening tick drew the world\n");
        }
        CloseTrace(false);
        return result;
    }

    if (BtGameModeFlag != BT_GAME_MODE_BATTLE_MENU) {
        g_full.live = false;
        SelectToRetail();
        return GameLoop();
    }

    // DC_COOP_MUTATE=fullmenulocal (the soak's twin): only the menu's own copy steps it, so the copies part.
    int open = 1;

    if (FirstOpen()) {
        SoundOwner sound(g_full.owner); // coop-menuse: its cursor's sounds are its player's

        OwnerViewBegin(g_full.owner);
        g_stepping = g_full.owner;
        TestMenuEscape(g_full.owner, g_full.ticks);
        open = !ShownHere() && Mutated("fullmenulocal") ? 1 : BattleMenuCursor();
        MenuPackTrace(); // coop-chestmenu: test hook (the pack, once its read is in, before the world steps)
        g_stepping = 0;
        OwnerViewEnd();
    }

    // coop-localfix (Jep's 2026-10-26 Local playtest: "P2 tried to escape and I think P1 being in the menu prevented it"):
    // the first menu shut on "escape this dungeon" while the second is open: the Escape is the world's, so the second
    // menu ends here (its player goes on in the floor that stays, or leaves with the pair) and the close below starts it,
    // instead of waiting for the second to close. DC_COOP_MUTATE=second_escape_lost (twin): it waits, as before.
    if (open == 0 && g_second.live && MenuMapJumpMode != 0 && !Mutated("second_escape_lost")) {
        SecondIn();
        ExitBattleMenu(1);
        SecondOut();
        g_second.live = false;
        DeleteSecondBlocks();
        std::printf("coop-fullmenu: player %d's second menu ends: player %d escapes\n", g_second.owner, g_full.owner);
    }

    if (open == 0 && g_second.live) {
        // The first shuts while the second stays: the menu scene goes on for the second (its owner walks again).
        g_second.first_closed = true;
        std::printf("coop-fullmenu: player %d's menu closes after %d ticks; player %d's stays open\n", g_full.owner, g_full.ticks,
                    g_second.owner);
    }

    if (FirstOpen()) {
        TrySecondOpen();
    }
    StepSecond();

    // coop-localfix: the second menu shut on "escape this dungeon": its player escapes now. The first menu (still open,
    // or shut already) ends and retail's close starts the Escape with the second's choice, as the first's own would.
    if (g_second.escape != 0) {
        const int who = g_second.owner;

        MenuMapJumpMode = g_second.escape;
        if (!g_second.first_closed && open != 0) {
            ExitBattleMenu(1);
            std::printf("coop-fullmenu: player %d's menu ends: player %d escapes from the second menu\n", g_full.owner, who);
        }
        g_full.live = false;
        g_second = SecondMenu{};
        BtGameModeFlag = BT_GAME_MODE_PLAY;
        gameTask = GAME_TASK_MENU_CLOSE;
        CoopWorldsExitBy(who);
        std::printf("coop-fullmenu: player %d's second menu closed on the dungeon's escape\n", who);
        int result = GameLoop();

        CloseTrace(true);
        return result;
    }

    if ((open == 0 && !g_second.first_closed) || (g_second.first_closed && !g_second.live)) {
        // Closed: retail's close (GAME_TASK_MENU_CLOSE) runs in this tick's MoveChara.
        g_full.live = false;
        g_second = SecondMenu{};
        BtGameModeFlag = BT_GAME_MODE_PLAY;
        if (g_select.live) {
            SelectToRetail(); // the script's task stays (its item window goes on)
        } else {
            gameTask = GAME_TASK_MENU_CLOSE;
        }

        // coop-dungeonp2: her menu's "escape this dungeon" (the close then starts the Escape): she is the one who leaves.
        if (MenuMapJumpMode != 0) {
            CoopWorldsExitBy(g_full.owner);
        }
        std::printf("coop-fullmenu: player %d's menu closes after %d ticks at page %d\n", g_full.owner, g_full.ticks, MenuSelect[1]);
        int result = GameLoop();

        CloseTrace(true);
        return result;
    }

    g_full.ticks++;

    // The world's step, with each player in a menu holding still and unhurt.
    CGamePad menu_pad = GamePad;

    InputPadState p1_now{};
    const bool    p1_in_menu = (FirstOpen() && g_full.owner == 1) || (g_second.live && g_second.owner == 1);

    if (!p1_in_menu && NetplayPlayerPad(1, p1_now)) {
        p1_now.buttons &= ~PlayerWheelMask(); // coop-wheelcrash: his ring's buttons are the ring's
        SetPortOne(GamePad, p1_now, g_full.p1_before);
        g_full.p1_before = p1_now;
    } else {
        ReleasePortOne(GamePad);
    }
    BtGameModeFlag = BT_GAME_MODE_PLAY;
    if (gameTask == GAME_TASK_MENU_CLOSE) {
        gameTask = GAME_TASK_PLAY;
    }
    // The owner stands (player 2: the link gives her pad 2 nothing) and the world can still hit them (Jep's
    // 2026-10-20: a shelter in the menu would let players cheese fights). DC_COOP_MUTATE=menuguard (t-pause's
    // twin): unhurt, as before. Every player of the zone in a pause or a menu (coop_pause): the world holds.
    const bool guard = Mutated("menuguard") || TestEnvOn("DC_COOP_TEST_MENU_SHELTER");
    // DC_COOP_TEST_MENU_SHELTER=1 (t-triangle-menu -Case p2, coop-menuflake): the same shelter as the twin, but only for a test that
    // checks the menu itself, so a floor monster reaching her (3 of 20 runs killed Xiao in her menu) cannot close it.
    // coop-menudown: the shelter is each player standing in a menu now, the first's or the second's, not g_full.owner: with
    // her menu shut and his second still open, a monster downed Toan in his menu while she, walking again, kept the shelter
    // (t-triangle-menu -Case both2). DC_COOP_MUTATE=shelter_owner (twin): the first menu's owner, as before.
    const bool p2_in_menu = (FirstOpen() && g_full.owner == 2) || (g_second.live && g_second.owner == 2);
    const bool shelter_p1 = guard && (Mutated("shelter_owner") ? g_full.owner == 1 : p1_in_menu);
    const bool shelter_p2 = guard && (Mutated("shelter_owner") ? g_full.owner == 2 : p2_in_menu);

    if (shelter_p1) {
        BtActStatus.invincible_frames = std::max<int>(BtActStatus.invincible_frames, 2);
    }
    if (shelter_p2) {
        g_partner.invincible = std::max(g_partner.invincible, 2);
    }
    // coop-menudown: player 1's life lost in the world's step while he stands in a menu (the partner's hits log already).
    const int p1_life = UserStatus != nullptr ? UserStatus->hp[UserStatus->cur_chara] : 0;
    static bool held_logged = false;

    TrySelectOpen();
    if (!PauseZoneHeld()) {
        SoundOwner world(1, true); // coop-menuse: the world's step reads his pad (SetPortOne)

        DunMoveChara();
    }
    if (PauseZoneHeld() != held_logged) {
        held_logged = PauseZoneHeld();
        std::printf("coop-fullmenu: the world %s\n", held_logged ? "holds (everyone paused or in a menu)" : "runs");
    }
    if (shelter_p1) {
        BtActStatus.invincible_frames = std::max<int>(BtActStatus.invincible_frames, 2);
    }
    if (p1_in_menu && UserStatus != nullptr && UserStatus->hp[UserStatus->cur_chara] < p1_life) {
        std::printf("coop-fullmenu: player 1 hurt in his menu: life %d -> %d%s\n", p1_life, UserStatus->hp[UserStatus->cur_chara],
                    shelter_p1 ? " (sheltered)" : "");
    }
    GamePad = menu_pad;

    // The world's step split the pair and ended the menu here (FullMenuEndForSplit): the menu's pad, put back
    // just now, was still in its stick mode, so it leaves it again, and the tick goes on as play.
    if (!g_full.live) {
        if (!Mutated("staymenu")) {
            GamePad.MenuModeOff();
            GamePad.AutoRepeatOff();
        }
        DunMainDraw();
        BtGameModeFlag = 0;
        int result = GameLoop();
        if (BtGameModeFlag == 0) {
            BtGameModeFlag = BT_GAME_MODE_PLAY;
        }
        SelectToRetail();
        return result;
    }

    // The owner went down: the menu closes (the game's own downing goes on). DC_COOP_MUTATE=menudown: it stays.
    auto is_down = [](int player) {
        return player == 1 ? UserStatus != nullptr && UserStatus->hp[UserStatus->cur_chara] <= 0 : g_partner.down > 0;
    };

    if (g_second.live && is_down(g_second.owner) && !Mutated("menudown")) {
        g_second.live = false;
        DeleteSecondBlocks();
        std::printf("coop-fullmenu: player %d's second menu closes: down\n", g_second.owner);
    }

    bool down = (FirstOpen() && is_down(g_full.owner)) || (g_second.first_closed && !g_second.live);

    if (down && !Mutated("menudown")) {
        // coop-deadstick (Jep's 2026-10-27 run: after P1 died in his menu, his stick moved his quick-use slots): the
        // first menu still open ends the game's own way, as its close and the escape's do. Without ExitBattleMenu the
        // pad stayed in the menu's stick mode (MenuModeOn 0x78), so his stick pressed the d-pad and D-pad Left/Right
        // moved the slots. DC_COOP_MUTATE=deadstick (twin): no exit, as before.
        if (FirstOpen() && !Mutated("deadstick")) {
            OwnerViewBegin(g_full.owner);
            ExitBattleMenu(1);
            OwnerViewEnd();
        }
        if (g_second.live) {
            g_second.live = false;
            DeleteSecondBlocks();
        }
        g_second = SecondMenu{};
        g_full.live = false;
        if (gameTask == GAME_TASK_PLAY) {
            gameTask = GAME_TASK_MENU_CLOSE;
        }
        std::printf("coop-fullmenu: player %d's menu closes: down (task %#x)\n", g_full.owner, gameTask);
        DunMainDraw();
        BtGameModeFlag = 0;
        int result = GameLoop();
        if (BtGameModeFlag == 0) {
            BtGameModeFlag = BT_GAME_MODE_PLAY;
        }
        SelectToRetail();
        return result;
    }

    if (gameTask != GAME_TASK_PLAY && gameTask != g_full.task_logged) {
        g_full.task_logged = gameTask;
        std::printf("coop-fullmenu: the world went to task %#x with the menu open\n", gameTask);
    }

    if (BattleMenuFlag == BTLMENU_STATE_EXIT) {
        CloseTrace(false, true);
    }
    BtGameModeFlag = BT_GAME_MODE_BATTLE_MENU;
    Draw();
    StepSelect();

    if (g_full.ticks % 60 == 1) {
        std::printf("coop-fullmenu: open %d ticks, task %#x, drawn %s\n", g_full.ticks, gameTask, ShownHere() ? "here" : "hidden");
        std::fflush(stdout); // coop-menuflake: a test stops a copy by its PID, and the log's stdout part must hold the menu's lines by then
    }

    // GameLoop's tail (a floor left, the fade), with neither of its mode switches taking the tick.
    BtGameModeFlag = 0;
    int result = GameLoop();
    if (BtGameModeFlag == 0) {
        BtGameModeFlag = BT_GAME_MODE_BATTLE_MENU;
    }
    return result;
}

void MenuStateSave(std::vector<unsigned char> &to) {
    SaveMenu(to);
}

void MenuStateLoad(const std::vector<unsigned char> &from) {
    LoadMenu(from);
}

std::size_t MenuStateBytes() {
    return MenuStateSize();
}

const int *MenuSecondBlocks(int *count) {
    *count = kSecondBlockCount;
    return SecondBlocks();
}

u_long128 *MenuSecondBuffer() {
    // One arena block for whichever second menu is open (a floor's or a town's: never both), zeroed at each opening.
    static const char kSecondArena = 0;
    u_long128        *buffer = reinterpret_cast<u_long128 *>(ArenaBlock(&kSecondArena, kSecondBufferQuads * 16));

    std::memset(buffer, 0, kSecondBufferQuads * 16);
    g_second_arena = buffer;
    return buffer;
}

bool MenuBuildsWorldWeapon(int chara) {
    // coop-localmenu 3c: a local menu's equip builds the world's weapon when its poke applies, on every copy on one tick
    // (ApplyPoke), never here alone: the weapon's frames are the swing's hit points. DC_COOP_MUTATE=localmenu_weapon_here
    // (twin): built here at once, as the lockstep menu did on both.
    if (g_local.live && !Mutated("localmenu_weapon_here")) {
        std::fprintf(stderr, "coop-localmenu: player %d's weapons page builds no weapon here: the equip's command does\n", g_local.owner);
        return false;
    }
    if (g_full.player_chara < 0 || chara == g_full.player_chara || Mutated("herweapon_world")) {
        return true;
    }
    std::fprintf(stderr, "coop-fullmenu: player 2's weapons page leaves the world's weapon (member %d) alone: member %d is hers\n",
                 g_full.player_chara, chara);
    return false;
}

void MenuWeaponBuildTrace(const void *file) {
    // stderr: a crash right after it loses stdout's buffered lines (lesson: harness traps).
    COOP_TEST_SWITCH(bool, debug, TestEnv("DC_COOP_MENUDEBUG") != nullptr);

    if (!debug) {
        return;
    }

    const auto *at = static_cast<const u_long128 *>(file);
    const bool  second = g_second_arena != nullptr && at >= g_second_arena && at < g_second_arena + kSecondBufferQuads;

    std::fprintf(stderr, "coop-fullmenu: player %d's weapons page rebuilds the weapon from the %s menu's files (second menu %s)\n", g_stepping,
                second ? "second" : "first", g_second.live ? (g_second.owner == 1 ? "player 1's" : "player 2's") : "closed");
}

void FullMenuResetForSession() {
    g_full = FullMenu{};
    g_second = SecondMenu{};
    g_select = ItemSelect{};
    g_local = LocalMenu{};
}

void FullMenuEndForJob() {
    if (Mutated("job_menu_kept")) {
        return;
    }
    if (g_full.live || g_local.live || g_second.live) {
        std::printf("coop-fullmenu: a job starts with a menu open (full %d, local %d, second %d): it ends first\n", g_full.live ? 1 : 0,
                    g_local.live ? 1 : 0, g_second.live ? 1 : 0);
    }
    FullMenuEndForSplit();
    FullMenuResetForSession();
}

void FullMenuEndForSplit() {
    if (Mutated("staymenu")) {
        return;
    }

    if (g_full.live) {
        g_full.live = false;
        ExitBattleMenu(1);
        BtGameModeFlag = BT_GAME_MODE_PLAY;
        std::printf("coop-fullmenu: player %d's menu ends at the split after %d ticks\n", g_full.owner, g_full.ticks);
    }
    // coop-localmenu: this copy's own local menu too; its close command follows (ActorMenuSync).
    if (g_local.live) {
        {
            LocalPad pad;
            // coop-basereds (x-dungeon-descent-menu-p1, OUT OF SYNC at tick 180, part melt): the gate question closes the asked
            // player's local menu here (CoopWorldsGateVote), on the menu's copy alone. ExitBattleMenu resets the bag's melt timer
            // (ItemVolumeStep.Initialize: frame 0, ticking on) and tidies the bag, the world's since coop-guard; outside the
            // menu's writes they stayed on her copy (melt 15 0 1 against 0 0 0 at the next checksum). Inside them, as the down
            // path's close (coop-famA), they go to every copy by command on one tick. DC_COOP_MUTATE=splitmenu_writes_here (twin):
            // outside, as before.
            std::optional<LocalWrites> writes;
            if (!Mutated("splitmenu_writes_here")) {
                writes.emplace(true);
            }
            ExitBattleMenu(1);
        }
        g_local.live = false;
        g_local.jump = 0;
        std::printf("coop-localmenu: player %d's menu ends at the split after %d ticks\n", g_local.owner, g_local.ticks);
    }

    if (PadMenuThreshold() != 0) {
        std::printf("coop-fullmenu: the pad leaves the menu's stick mode (%d) at the split\n", PadMenuThreshold());
        GamePad.MenuModeOff();
        GamePad.AutoRepeatOff();
    }
}

} // namespace coop

bool FullMenuSaveHere() {
    const int owner = coop::g_stepping != 0 ? coop::g_stepping : mode == 2 ? 1 : coop::MenuGameMenuOwner();

    if (coop::Mutated("save_both")) {
        std::printf("coop-save: player %d's save written on this copy (player %d)\n", NetplayPartnerLinked() ? owner : NetplayLocalPlayer(),
                    NetplayLocalPlayer());
        std::fflush(stdout);
        return true;
    }
    if (NetplayRole() == 2 && NetplayCatchUnderWay()) {
        std::printf("coop-save: the host's save in the catch-up's replay: not written on this copy\n");
        std::fflush(stdout);
        return false;
    }
    if (!NetplayPartnerLinked() || owner == NetplayLocalPlayer()) {
        std::printf("coop-save: player %d's save written on this copy (player %d)\n", NetplayPartnerLinked() ? owner : NetplayLocalPlayer(),
                    NetplayLocalPlayer());
        std::fflush(stdout);
        return true;
    }
    std::printf("coop-save: player %d's save is written on their own copy; this copy (player %d) writes none\n", owner, NetplayLocalPlayer());
    std::fflush(stdout);
    return false;
}
