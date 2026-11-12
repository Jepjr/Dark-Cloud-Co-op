// Per-player menu overlays (game.coop_menus); see coop_menu.hpp.

#include "coop/game_ns.hpp"
#include "coop/coop_opener.hpp" // coop-opt7
#include "coop/coop_menu.hpp"
#include "coop/coop_audio.hpp" // coop-menuse
#include "coop/coop_georama.hpp" // coop-town: her own Georama
#include "coop/coop_worlds.hpp" // coop-bagapart: CoopWorldsTestPadButtons
#include "coop/coop_town.hpp"  // coop-doorfix: CoopTownCaveHoldsGamePad
#include "coop/coop_zonechara.hpp" // coop-pt1111n

#include <algorithm>
#include <utility>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include "battlemenu/internal.hpp"
#include "bt_shot_effect.hpp" // coop-pt1109a
#include "btactstatus.hpp"
#include "character.hpp"
#include "dungeonmap.hpp"
#include "edit.hpp"
#include "editloop.hpp"
#include "editmapscript.hpp"
#include "savedata.hpp"
#include "mglib.hpp"
#include "btmisc.hpp"
#include "text/caption_text.hpp"
#include "dataread.hpp"
#include "dun/gameloop.hpp"
#include "gamepad.hpp"
#include "itemdata.hpp"
#include "menu_draw.hpp"
#include "menu_dungeon.hpp"
#include "menu_inventory.hpp"
#include "menu_misc.hpp"
#include "rect.hpp"
#include "snd.hpp"
#include "userstatus.hpp"
#include "weaponeffect.hpp"
#include "platform/config.hpp"

#include "coop/coop.hpp"
#include "coop/coop_combat.hpp"
#include "coop/coop_core.hpp"
#include "coop/coop_fullmenu.hpp"
#include "coop/coop_inventory.hpp"
#include "coop/coop_interact.hpp" // coop-keypick
#include "coop/coop_worlds.hpp"   // coop-p2prompt: CoopWorldsVoteAsks
#include "coop/coop_prompt.hpp"   // coop-townwheel: StepSwapQuestion
#include "coop/coop_party.hpp"
#include "coop/coop_pause.hpp"
#include "coop/copies.hpp"
#include "coop/coop_town.hpp" // coop-townmenus: TownPartnerShown
#include "coop/netplay.hpp"

namespace dc::game {
extern s32 gameTask;
extern s32 mode;
extern int EdInteriorFlag;
} // namespace dc::game

namespace coop {

namespace {

constexpr int kDungeonMode = 3; // GAME_MODE_DUNGEON (main.hpp)
constexpr int kTownMode = 2;    // GAME_MODE_EDIT
constexpr int kWalkMode = 1;    // ED_MODE_WALK (editmapscript.hpp)

const char *const kNames[6] = {"Toan", "Xiao", "Goro", "Ruby", "Ungaga", "Osmond"};

enum Page { kItems, kEquip, kStatus, kSend, kSystem };

const char *const kPageNames[5] = {"Items", "Equip", "Status", "Send", "System"};

enum SystemRow { kPauseRow, kLeaveRow };

struct Panel {
    bool          open = false;
    int           page = 0; // index into the player's page list
    int           row = 0;
    std::uint16_t prev = 0;
    int           notice_ticks = 0;
    char          notice[64] = "";
    int           opened = 0; // times opened, for the tests' logs
};

struct State {
    Panel panel[2];
    bool  away = false;   // player 2 has left the floor
    int   away_key = -1;  // the floor she left (selectMapNo * 256 + floor)
    bool  rejoin = false; // put her back beside player 1 on the next step
    bool  pause = false;  // the System page asked for the plain pause

    // A script or chest player 2 started (her Cross on a door, gate or chest): its prompts are hers.
    // held: the buttons pad 2 still held as it took port 1, kept released until let go.
    bool          prompt = false;
    std::uint16_t prompt_held = 0;

    // In a town: who walks Toan and so talks to the shopkeepers (1 or 2; player 2's Start takes it, either
    // Start gives it back, a shop closing gives it back), the buttons the new driver still held as it
    // changed hands, Start last tick on each pad, and whether a shop has player 2's purse and bag in.
    int           driver = 1;
    std::uint16_t driver_held = 0;
    bool          start_prev[2] = {false, false};
    bool          shop_swapped = false;
};

State g_state;

// The draw counts the tests read (DC_COOP_MENUDEBUG): this copy's overlay draws per player, the cover.
int g_drawn[2] = {};
int g_cover_drawn = 0;

// The party status: UserStatus on a floor; in a town (UserStatus is only set by a dungeon) the same
// object, SaveData's.
CUserStatus *Status() {
    return UserStatus != nullptr ? UserStatus : SaveData != nullptr ? (CUserStatus *) SaveData->GetDngStatus() : nullptr;
}

bool Debug() {
    COOP_TEST_SWITCH(bool, debug, TestEnv("DC_COOP_MENUDEBUG") != nullptr);
    return debug;
}

InputPadState Pad(int player) {
    InputPadState linked;

    if (NetplayPad(player, linked)) {
        return linked;
    }

    return InputGetPad(CopiesPadSource(player));
}

// coop-doorfix (Jep's 2026-10-30 Local playtest: P2 apart could not open her picker at the cave door): the port a player's
// picker steps on: apart in a town this copy's player walks as the game's own, on port 0; together each on their own.
int KeyPickPort(int player) {
    return mode == kTownMode && CoopWorldsApart() && !Mutated("cavepick_linked_only") && !Mutated("cavedoor_off") ? 0 : player - 1;
}

// Its pad (apart, with the test's presses of her copy apart, DC_WORLDS_TEST_APART_PRESS_P2: 0 otherwise).
InputPadState KeyPickPad(int player) {
    const int     port = KeyPickPort(player);
    InputPadState pad = Pad(port);

    if (port == 0 && player == 2) {
        pad.buttons |= CoopWorldsTestPadButtons();
    }
    return pad;
}

// The player the game's pad (port 0) walks: player 1, or apart in a town this copy's player.
int GamePadPlayer() {
    return KeyPickPort(2) == 0 ? NetplayRole() : 1;
}

int FloorKey() {
    return Status() != nullptr ? selectMapNo * 256 + Status()->cur_floor : -1;
}

// The two play together on a dungeon floor, under control.
bool OnFloor() {
    return MenusOn() && mode == kDungeonMode && gameTask == GAME_TASK_PLAY && BtEventMode == 0 && Status() != nullptr &&
           NowDngMap != nullptr && !FullMenuLive(); // coop-fullmenu: not while the game's menu is open beside it
}

// Player 2 walks a town (no house, menu or event).
bool InTown() {
    return MenusOn() && mode == kTownMode && GameMode == kWalkMode && Status() != nullptr && !Mutated("menutownpanel") &&
           !CoopP2Building(); // coop-town: not while she builds at her own Georama
}

// The member player 2 plays: the partner loaded on the floor, else the first member who is not player 1's.
int PartnerChara() {
    if (g_partner.loaded_chara >= 0) {
        return g_partner.loaded_chara;
    }

    for (int i = 0; Status() != nullptr && i < Status()->party_size && i < 6; i++) {
        if (i != Status()->cur_chara) {
            return i;
        }
    }

    return -1;
}

int CharaOf(int player) {
    return player == 0 ? (Status() != nullptr ? (int) Status()->cur_chara : -1) : PartnerChara();
}

ITEM_PACK &PackOf(int player) {
    return player == 0 ? Status()->item_pack : PartnerPack();
}

int Pages(int player, Page *out) {
    int count = 0;

    out[count++] = kItems;
    out[count++] = kEquip;
    out[count++] = kStatus;

    if (OwnBags()) {
        out[count++] = kSend;
    }

    if (mode == kDungeonMode) {
        out[count++] = kSystem;
    }

    return count;
}

Page PageOf(int player) {
    Page pages[5];
    int  count = Pages(player, pages);

    return pages[std::clamp(g_state.panel[player].page, 0, count - 1)];
}

// Each item the player carries once, in pack order (quick-use slots first).
int Items(int player, int *out, int max) {
    if (player == 1 && OwnBags()) {
        return BagItems(out, max);
    }

    const ITEM_PACK &pack = PackOf(player);
    int              count = 0;
    auto             add = [&](int item) {
        if (item > 0 && count < max && std::find(out, out + count, item) == out + count) {
            out[count++] = item;
        }
    };

    for (int k = 0; k < 3; k++) {
        if (pack.quick_item_qty[k] > 0) {
            add(pack.quick_item_slot[k]);
        }
    }

    for (int i = 0; i < pack.num && i < 103; i++) {
        add(pack.item[i]);
    }

    return count;
}

int Count(int player, int item) {
    return player == 1 && OwnBags() ? BagCount(item) : GetNowItemNum((short) item, &PackOf(player));
}

int Weapons(int player, int *out, int max) {
    int chara = CharaOf(player);
    int count = 0;

    for (int slot = 0; chara >= 0 && slot < 11 && count < max; slot++) {
        if (Status()->chara_weapons[chara][slot].item_no > 0) {
            out[count++] = slot;
        }
    }

    return count;
}

int Rows(int player, int *out, int max) {
    switch (PageOf(player)) {
        case kItems:
        case kSend:
            return Items(player, out, max);
        case kEquip:
            return Weapons(player, out, max);
        case kSystem:
            out[0] = kPauseRow;
            out[1] = kLeaveRow;
            return player == 1 ? 2 : 1;
        default:
            return 0;
    }
}

void Notice(int player, const char *text) {
    Panel &panel = g_state.panel[player];

    std::snprintf(panel.notice, sizeof panel.notice, "%s", text);
    panel.notice_ticks = 150;
    std::printf("coop-menu: P%d %s\n", player + 1, text);
}

std::string ItemName(int item) {
    return item > 0 ? dc::battlemenu::detail::DecodeItemNamePort(item, "?") : std::string("-");
}

WEAPON_HAVE *EquippedOf(int player) {
    int chara = CharaOf(player);

    if (chara < 0) {
        return nullptr;
    }

    int slot = Status()->equipped_weapon_slot[chara];

    if (slot < 0 || slot >= 11 || Status()->chara_weapons[chara][slot].item_no <= 0) {
        return nullptr;
    }

    return &Status()->chara_weapons[chara][slot];
}

// Player 1's hand: the equipped weapon's models loaded afresh, as the game's menu does when its equip
// page changes them (DngWeaponEquipModelBuild), from the weapon folder as the partner's are.
bool ReloadPlayerWeapon(int chara) {
    static const int kFirst[6] = {ITEM_WEAPON_DAGGER_BROKEN,     ITEM_WEAPON_WOODENSLINGSHOT_BROKEN, ITEM_WEAPON_MALLET_BROKEN,
                                  ITEM_WEAPON_GOLD_RING_BROKEN, ITEM_WEAPON_FIGHTING_STICK_BROKEN,  ITEM_WEAPON_MACHINE_GUN_BROKEN};
    static std::vector<unsigned int> packs[3];
    int kinds[3] = {0, 1, Status()->chara_weapons[chara][Status()->equipped_weapon_slot[chara]].item_no - kFirst[chara]};

    for (int i = 0; i < 3; i++) {
        char name[64];
        char cfg[64];
        char path[128];
        int  size = 0;

        packs[i].resize(kPackBytes / 4);
        BtGetWeaponNamePath2(name, cfg, chara, kinds[i]);
        std::snprintf(path, sizeof path, "commenu/weapon/%s", name);

        if (LoadFile(path, packs[i].data(), &size) == 0 || size <= 0 || size > kPackBytes) {
            std::fprintf(stderr, "coop-menu: cannot load %s\n", path);
            return false;
        }
    }

    LoadWeapon2(packs[0].data(), packs[1].data(), packs[2].data(), chara, 1);

    // coop-pt1109a (Jep's 11-09 crash, her copy: his equip's poke, GetPackFile faulting 0x7FB16C76 below the pack): MenuWeaponEffectSet reads
    // WepEffectMenuReadBuf, which the game's menu fills as it opens its page, so it holds the file only in the copy that ran that menu. With
    // his menu local (stage 4) the other copy's still pointed where its own last menu left it, inside read_buffer (+0x2366C0 in the
    // dump), a floor's scratch that later loads write over. The reload reads the file itself, as the hand's three above, from the
    // status every copy agrees on. DC_COOP_MUTATE=reload_menu_effect (twin): said, and no effect reloaded (reading it can only crash:
    // the twin fails on its check).
    if (Mutated("reload_menu_effect")) {
        std::printf("coop-menu: P1 hand's effect reads the menu's buffer %p\n", (void *) WepEffectMenuReadBuf);
    } else {
        const int                        slot = Status()->equipped_weapon_slot[chara];
        BT_SHOT_EFFECT                  *effect = Get_Main_EffectPtr(chara, Status()->chara_weapons[chara][slot].best_elem);
        static std::vector<unsigned int> effect_pack;
        char                             path[128];
        int                              size = 0;

        SetMenuCharaEffectReadFlag(0);
        if (effect != nullptr) {
            std::snprintf(path, sizeof path, "dun/mainchara/wep_eff/%s.chr", effect->model_name);
            effect_pack.resize(kPackBytes / 4);
            if (LoadFile(path, effect_pack.data(), &size) == 0 || size <= 0 || size > kPackBytes) {
                std::fprintf(stderr, "coop-menu: cannot load %s\n", path);
                effect = nullptr;
            }
        }
        MainChara_Effect(effect, effect != nullptr ? effect_pack.data() : nullptr, 1);
        std::printf("coop-menu: P1 hand's effect from its own file (%s, %d bytes)\n", effect != nullptr ? effect->model_name : "none", size);
    }
    CWeaponFx.InitSet(NowWeapon->frame, (char *) "dcol0", (char *) "dcol1");
    SetWeaponAttachStatus(NowWeaponHave);
    SetWeaponColor();
    std::printf("coop-menu: P1 hand reloaded: slot item %d, the game's weapon %d\n",
                Status()->chara_weapons[chara][Status()->equipped_weapon_slot[chara]].item_no, NowWeaponHave->item_no);
    return true;
}

void Equip(int player, int slot) {
    SoundOwner sound(player + 1); // coop-menuse: a player's overlay's sounds are theirs
    int chara = CharaOf(player);

    if (chara < 0 || Status()->equipped_weapon_slot[chara] == slot) {
        return;
    }

    if (player == 1) {
        if (mode == kDungeonMode && g_partner.loaded_chara == chara) {
            MenuPartnerEquip(slot); // reloads the partner's hand
        } else {
            Status()->equipped_weapon_slot[chara] = (s8) slot;
            SndSePlay(MENU_SOUND_CONFIRM, -1, 0);
        }
    } else {
        // Not in the middle of a swing: the hand is reloaded under it.
        if (mode != kDungeonMode || BtActStatus.action_on != 0 || Mutated("menuequip")) {
            SndSePlay(MENU_SOUND_REFUSE, -1, 0);
            return;
        }

        Status()->equipped_weapon_slot[chara] = (s8) slot;

        if (!ReloadPlayerWeapon(chara)) {
            Notice(player, "weapon kept: its model did not load");
        }

        SndSePlay(MENU_SOUND_CONFIRM, -1, 0);
    }

    char line[64];
    std::snprintf(line, sizeof line, "equips %s (slot %d)", ItemName(Status()->chara_weapons[chara][slot].item_no).c_str(), slot);
    Notice(player, line);
}

// Spends one of item from the player's own bag, its quick-use slot first.
void Spend(int player, int item) {
    if (player == 1 && SpendFromSlot(item)) {
        return;
    }

    DeleteItemAfterUseItem((short) item, &PackOf(player));
}

void Use(int player, int item) {
    SoundOwner sound(player + 1); // coop-menuse
    int chara = CharaOf(player);

    if (chara < 0 || Count(player, item) <= 0) {
        SndSePlay(MENU_SOUND_REFUSE, -1, 0);
        return;
    }

    char line[64];

    // The powders work on the user's own equipped weapon.
    if (item == ITEM_REPAIR_POWDER || item == ITEM_POWERUP_POWDER) {
        WEAPON_HAVE *weapon = EquippedOf(player);
        int          used = weapon != nullptr && !Mutated("menupowder") ? ItemUseFunc(Status(), item, chara, ITEM_USE_ON_EQUIP_WEAPON, weapon) : 0;

        if (used == 0) {
            SndSePlay(MENU_SOUND_REFUSE, -1, 0);
            std::snprintf(line, sizeof line, "%s: no effect", ItemName(item).c_str());
            Notice(player, line);
            return;
        }

        Spend(player, item);
        SndSePlay(item == ITEM_REPAIR_POWDER ? (int) SE_WEAPON_REPAIR : (int) MENU_SOUND_CONFIRM, -1, 0);

        if (player == 1 && mode == kDungeonMode && g_partner.loaded_chara == chara) {
            ReloadWeapon(); // a repaired starting weapon is another model
        } else if (player == 0 && mode == kDungeonMode) {
            // The game swings with its copy of the weapon (NowWeaponHave), as its menu leaves it.
            if (weapon->item_no != NowWeaponHave->item_no) {
                ReloadPlayerWeapon(chara);
            } else {
                SetWeaponAttachStatus(NowWeaponHave);
            }
        }

        std::snprintf(line, sizeof line, "%s on %s: durability %.0f/%d level %d", ItemName(item).c_str(),
                      ItemName(weapon->item_no).c_str(), weapon->durability_f, weapon->durability, weapon->level);
        Notice(player, line);
        return;
    }

    if (player == 1) {
        // Food and drink heal the partner, bombs are thrown: as Square uses them.
        if (mode == kDungeonMode && g_partner.visible && MenuPartnerUseItem(item)) {
            std::snprintf(line, sizeof line, "uses %s", ItemName(item).c_str());
            Notice(player, line);
            return;
        }

        // A member who has joined keeps her own life, water and ailments in the party status, so the
        // rest of the game's own item effects (water, antidotes, Fruit of Eden, the gourd, doughnuts,
        // stamina) work on her as on player 1. A stand-in's life is co-op's own: those are refused.
        if (!(chara < Status()->party_size && Status()->max_hp[chara] > 0) || ItemUseFunc(Status(), item, chara, ITEM_USE_ON_CHARA, nullptr) == 0) {
            SndSePlay(MENU_SOUND_REFUSE, -1, 0);
            std::snprintf(line, sizeof line, "%s: no effect", ItemName(item).c_str());
            Notice(player, line);
            return;
        }

        Spend(player, item);
        SndSePlay(SE_CHARA_RECOVER, -1, 0);
        std::snprintf(line, sizeof line, "uses %s", ItemName(item).c_str());
        Notice(player, line);
        return;
    }

    if (ItemUseFunc(Status(), item, chara, ITEM_USE_ON_CHARA, nullptr) == 0) {
        SndSePlay(MENU_SOUND_REFUSE, -1, 0);
        return;
    }

    Spend(player, item);
    SndSePlay(SE_CHARA_RECOVER, -1, 0);
    std::snprintf(line, sizeof line, "uses %s", ItemName(item).c_str());
    Notice(player, line);
}

void Act(int player, std::uint16_t pressed) {
    SoundOwner sound(player + 1); // coop-menuse
    Panel &panel = g_state.panel[player];
    Page   pages[5];
    int    page_count = Pages(player, pages);
    int    rows[128];
    int    count = Rows(player, rows, 128);

    if ((pressed & (kInputTriangle | kInputCircle)) != 0) {
        panel.open = false;
        SndSePlay(MENU_SOUND_REFUSE, -1, 0);
        std::printf("coop-menu: P%d overlay closed\n", player + 1);
        return;
    }

    if ((pressed & (kInputLeft | kInputRight)) != 0) {
        panel.page = (panel.page + ((pressed & kInputRight) != 0 ? 1 : page_count - 1)) % page_count;
        panel.row = 0;
        SndSePlay(MENU_SOUND_CURSOR, -1, 0);
        return;
    }

    if (count > 0 && (pressed & (kInputUp | kInputDown)) != 0) {
        panel.row = (panel.row + ((pressed & kInputDown) != 0 ? 1 : count - 1)) % count;
        SndSePlay(MENU_SOUND_CURSOR, -1, 0);
    }

    panel.row = count > 0 ? std::clamp(panel.row, 0, count - 1) : 0;

    if (count == 0) {
        return;
    }

    int  at = rows[panel.row];
    Page page = PageOf(player);

    if (page == kItems && player == 1 && OwnBags() && (pressed & kInputSquare) != 0) {
        PartnerSlotAssign(at);
        return;
    }

    if ((pressed & kInputCross) == 0) {
        return;
    }

    switch (page) {
        case kItems:
            Use(player, at);
            break;
        case kEquip:
            Equip(player, at);
            break;
        case kSend: {
            Send sent = Mutated("menusend") ? Send::kNone : SendItem(player + 1, at);
            char line[64];

            std::snprintf(line, sizeof line, "%s %s to P%d", sent == Send::kSent ? "sends" : "cannot send", ItemName(at).c_str(), 2 - player);
            Notice(player, line);
            SndSePlay(sent == Send::kSent ? MENU_SOUND_CONFIRM : MENU_SOUND_REFUSE, -1, 0);
            break;
        }
        case kSystem:
            if (at == kPauseRow) {
                panel.open = false;
                g_state.pause = true;
                std::printf("coop-menu: P%d asks for the pause\n", player + 1);
            } else {
                MenuLeaveFloor(!g_state.away);
                panel.open = false;
            }
            break;
        default:
            break;
    }
}

// Player 2's Start in a town hands her the walk (and the shops); either Start gives it back.
void StepTownDriver(const InputPadState &one, const InputPadState &two) {
    bool start[2] = {(one.buttons & kInputStart) != 0, (two.buttons & kInputStart) != 0};
    bool pressed[2] = {start[0] && !g_state.start_prev[0], start[1] && !g_state.start_prev[1]};

    g_state.start_prev[0] = start[0];
    g_state.start_prev[1] = start[1];

    if (mode != kTownMode) {
        if (g_state.driver != 1) {
            std::printf("coop-menu: town walk back to P1 (town left)\n");
        }

        g_state.driver = 1;
        g_state.driver_held = 0;
        return;
    }

    if (GameMode == kWalkMode && !g_state.panel[1].open && !Mutated("menutown") && !CoopP2Building()) { // coop-town: her Georama
        // coop-townmenus: linked, her Start is her own pause (coop_townmenu), not Toan's walk (Jep, 2026-10-24: Start is
        // each player's pause). DC_COOP_MUTATE=townstart_handover: the walk handed over, as before (twin).
        const bool own_pause = NetplayLocalPlayer() != 0 && TownPartnerShown(nullptr, nullptr) && !Mutated("townstart_handover");

        if (g_state.driver == 1 && pressed[1] && !pressed[0] && !own_pause) {
            g_state.driver = 2;
            g_state.driver_held = two.buttons;
            SndSePlay(MENU_SOUND_CONFIRM, -1, 0);
            std::printf("coop-menu: town walk to P2\n");
            return;
        }

        if (g_state.driver == 2 && (pressed[0] || pressed[1])) {
            g_state.driver = 1;
            g_state.driver_held = one.buttons;
            SndSePlay(MENU_SOUND_REFUSE, -1, 0);
            std::printf("coop-menu: town walk back to P1\n");
            return;
        }
    }

    g_state.driver_held &= (g_state.driver == 1 ? one : two).buttons;
}

// With the overlay off (game.coop_menu_panel), player 2's Triangle in a town's walk is her menu: it hands her the
// walk, as her Start does, and goes on to the game, whose walk opens the town's menu on Triangle (editloop's
// EdPadDown(0x10)) from the walker's pad (Jep's 2026-10-20 "pressing y on p2 doesnt open the menu", in Norune).
// Not on a tick player 1 presses Triangle or Start. DC_COOP_MUTATE=towntriangle (t-triangle-menu town's twin): not.
// With DC_COOP_MENUDEBUG the town's mode changes are logged, for the tests.
void StepTownTriangle(const InputPadState &one, const InputPadState &two) {
    static bool prev[2] = {false, false};
    static int  logged_mode = -1;
    bool        held[2] = {(one.buttons & kInputTriangle) != 0, (two.buttons & kInputTriangle) != 0};
    bool        pressed[2] = {held[0] && !prev[0], held[1] && !prev[1]};

    prev[0] = held[0];
    prev[1] = held[1];

    if (mode != kTownMode) {
        logged_mode = -1;
        return;
    }
    if (Debug() && GameMode != logged_mode) {
        std::printf("coop-menu: town mode %d -> %d (walk P%d)\n", logged_mode, (int) GameMode, g_state.driver);
        logged_mode = GameMode;
    }
    // coop-town (2026-10-21): her Triangle opens her own menu now (coop_townmenu) and Toan stays player 1's; the walk
    // handover on her Triangle stays only as the twin DC_COOP_MUTATE=townwalktriangle.
    if (!Mutated("townwalktriangle") || MenuPanelOn() || GameMode != kWalkMode || g_state.driver != 1 || !pressed[1] || pressed[0] ||
        (one.buttons & kInputStart) != 0) {
        return;
    }

    g_state.driver = 2;
    g_state.driver_held = two.buttons & ~kInputTriangle; // her Triangle goes on to the town's menu
    std::printf("coop-menu: town walk and menu to P2 (her Triangle)\n");
}

// Test hook (DC_COOP_MENUSHOP=N): once player 2 walks the town, shop N opens as its keeper's talk opens it.
void TestShop() {
    static const char *shop = TestEnv("DC_COOP_MENUSHOP");
    static bool        done = false;

    if (shop == nullptr || done || mode != kTownMode || GameMode != kWalkMode || g_state.driver != 2) {
        return;
    }

    done = true;
    EdSetShopNo(std::atoi(shop));
    goto_menu = ED_MENU_SHOP;
    std::printf("coop-menu: test opens shop %d\n", std::atoi(shop));
}

// DC_COOP_MENUGOLD=N: once, in a town or on a floor, player 2's purse gets N gilda.
void TestGold() {
    static const char *gold = TestEnv("DC_COOP_MENUGOLD");
    static bool        done = false;

    if (gold == nullptr || done || Status() == nullptr || (mode != kTownMode && mode != kDungeonMode) || !OwnBags()) {
        return;
    }
    if (TestEnv("DC_STATE_LOAD_AT") != nullptr && !NetplayStateFileLoaded()) {
        return; // (coop-p1parts: a fixture's load puts the purses back: after it, x-town-hershop)
    }

    done = true;
    PartnerGold(std::atoi(gold));
    std::printf("coop-menu: test gives P2 %d gilda: P2 %d, P1 %d\n", std::atoi(gold), PartnerGilda(), (int) Status()->money);
}

void StepPanel(int player, const InputPadState &pad) {
    SoundOwner sound(player + 1); // coop-menuse
    Panel        &panel = g_state.panel[player];
    std::uint16_t pressed = pad.buttons & ~panel.prev;

    panel.prev = pad.buttons;

    if (panel.notice_ticks > 0) {
        panel.notice_ticks--;
    }

    // Player 1's on a floor; player 2's on a floor (down or away too) and in a town.
    bool allowed = OnFloor() || (player == 1 && InTown());

    // Player 1 cannot open it lying knocked out (the game's own Triangle menu refuses too).
    if (allowed && player == 0 && Status()->hp[Status()->cur_chara] <= 0) {
        allowed = false;
    }

    if (!allowed) {
        if (panel.open) {
            panel.open = false;
            std::printf("coop-menu: P%d overlay closed (no longer free play)\n", player + 1);
        }

        return;
    }

    if (!panel.open) {
        if ((pressed & kInputTriangle) != 0 && MenuPanelOn()) {
            panel.open = true;
            panel.row = 0;
            panel.page += Mutated("menudesync") ? 1 : 0; // the soak's twin (copy B only): a page only the checksum sees
            panel.opened++;
            SndSePlay(MENU_SOUND_CONFIRM, -1, 0);
            std::printf("coop-menu: P%d overlay open (%s)\n", player + 1, mode == kTownMode ? "town" : "floor");
            if (player != 0) {
                // coop-pt1111p: what its Items and Status pages read, beside the party's (player 1's: the same pack and purse when
                // the bags are shared)
                int mine[128], party[128];
                std::printf("coop-menu: P%d overlay's bag %d items, %d G; the party's %d items, %d G (bags %s)\n", player + 1,
                            Items(player, mine, 128), PartnerGilda(), Items(0, party, 128), (int) Status()->money, OwnBags() ? "own" : "shared");
            }
        }

        return;
    }

    Act(player, pressed);
}

const InputPadState &Neutral() {
    static InputPadState pad = [] {
        InputPadState p;
        p.connected = true;
        return p;
    }();
    return pad;
}

// DC_COOP_MENUDEBUG: both players' positions, the task and the overlays every 30 ticks, for the tests.
void DebugLog() {
    static int ticks = 0;

    if (!Debug() || ++ticks % 30 != 0 || Status() == nullptr) {
        return;
    }

    float town[4] = {0.0f, 0.0f, 0.0f, 0.0f};

    if (mode == kTownMode && MainChara.frame != nullptr) {
        MainChara.GetPosition(town);
    }

    std::printf("coop-menu: tick %d mode %d task %d p1 (%.1f %.1f %.1f) p2 (%.1f %.1f %.1f) open %d %d away %d drawn %d %d cover %d "
                "owner %d local %d hash %08x floor %d prompt %d vis %d town (%.1f %.1f)\n",
                ticks, (int) mode, (int) gameTask, CharaMain.pos[0], CharaMain.pos[1], CharaMain.pos[2], g_partner.pos[0], g_partner.pos[1],
                g_partner.pos[2], (int) g_state.panel[0].open, (int) g_state.panel[1].open, (int) g_state.away, g_drawn[0], g_drawn[1],
                g_cover_drawn, MenuGameMenuOwner(), NetplayLocalPlayer(), MenuHash(), (int) Status()->cur_floor,
                (int) g_state.prompt, (int) g_partner.visible, town[0], town[2]);
}

// Test hooks, once, the first tick on a floor with the partner loaded (both copies on the same tick):
//   DC_COOP_MENUGIVE=item,...  puts the items in player 2's bag.
//   DC_COOP_MENUWEAR=N         player 2's member holds a weapon (its starting one in slot 0 when it has
//                              none) and that weapon's durability is set to N.
//   DC_COOP_MENUWEAR_P1=N      (coop-soundpt) the player's own weapon's durability is set to N.
void TestSetup() {
    static bool done = false;
    const char *give = TestEnv("DC_COOP_MENUGIVE");
    const char *wear = TestEnv("DC_COOP_MENUWEAR");
    const char *wear1 = TestEnv("DC_COOP_MENUWEAR_P1");

    if (done || (give == nullptr && wear == nullptr && wear1 == nullptr) || !OnFloor() || g_partner.loaded_chara < 0) {
        return;
    }

    done = true;

    for (const char *at = give; at != nullptr && *at != '\0';) {
        int item = std::atoi(at);

        if (item > 0) {
            std::printf("coop-menu: test gives P2 item %d: %s\n", item, PartnerGetItem(item, -1, false) ? "in the bag" : "refused");
        }

        at = std::strchr(at, ',');
        at = at != nullptr ? at + 1 : nullptr;
    }

    if (wear != nullptr) {
        int chara = g_partner.loaded_chara;

        if (EquippedOf(1) == nullptr) {
            WepDataListToHaveCopy(StarterWeapon(chara), &Status()->chara_weapons[chara][0]);
            Status()->equipped_weapon_slot[chara] = 0;
            ReloadWeapon();
        }

        WEAPON_HAVE *weapon = EquippedOf(1);

        weapon->durability_f = (float) std::atoi(wear);
        std::printf("coop-menu: test P2 weapon item %d durability %.0f/%d level %d\n", weapon->item_no, weapon->durability_f,
                    weapon->durability, weapon->level);
    }

    if (wear1 != nullptr) {
        CUserStatus *status = Status();
        WEAPON_HAVE *weapon = &status->chara_weapons[status->cur_chara][(int) status->equipped_weapon_slot[status->cur_chara]];

        weapon->durability_f = (float) std::atoi(wear1);
        std::printf("coop-menu: test P1 weapon item %d durability %.0f/%d\n", weapon->item_no, weapon->durability_f, weapon->durability);
    }
}

} // namespace

// coop-pt1111o (pt1111m's find: two players with different game.coop_menus desynced every tick on MenuHash): one setting per game,
// the host's. A joiner takes the host's (the Hello's, netplay_hello.cpp) for this run only: config.json is not written. Same for
// game.coop_menu_panel, which moves Triangle/Start and the pauses. DC_COOP_MUTATE=menus_own (twin): the joiner keeps its own.
namespace {
std::optional<bool> g_host_menus, g_host_panel;
} // namespace

bool MenusSetting() {
    return g_host_menus.value_or(dc::platform::ConfigGet().coop_menus);
}

bool MenuPanelSetting() {
    return g_host_panel.value_or(dc::platform::ConfigGet().coop_menu_panel);
}

void MenusAdopt(bool menus, bool panel) {
    if (Mutated("menus_own")) {
        return;
    }
    g_host_menus = menus;
    g_host_panel = panel;
    std::printf("coop-menus: this run plays by the host's settings: game.coop_menus %s, game.coop_menu_panel %s (this copy's own: %s, %s; config.json unchanged)\n",
                menus ? "on" : "off", panel ? "on" : "off", dc::platform::ConfigGet().coop_menus ? "on" : "off",
                dc::platform::ConfigGet().coop_menu_panel ? "on" : "off");
}

void MenusAdoptEnd() {
    g_host_menus.reset();
    g_host_panel.reset();
}

bool MenusOn() {
    return Enabled() && MenusSetting();
}

bool MenuOpen(int player) {
    return player >= 1 && player <= 2 && g_state.panel[player - 1].open;
}

void MenuTick() {
    if (!MenusOn()) {
        VpadTick(); // the test's virtual gamepads also drive a copy without the co-op menus (mp-menu-pointer-pad)
        return;
    }

    TestSetup();
    TestGold();
    StepTownDriver(Pad(0), Pad(1));
    StepTownTriangle(Pad(0), Pad(1));
    TestShop();

    // Player 2's prompt lasts until play is back under control (or the floor is left).
    if (g_state.prompt) {
        if (mode != kDungeonMode || (gameTask == GAME_TASK_PLAY && BtEventMode == 0)) {
            g_state.prompt = false;
            std::printf("coop-menu: P2's prompt over (mode %d task %d floor %d)\n", (int) mode, (int) gameTask,
                        Status() != nullptr ? (int) Status()->cur_floor : -1);
        } else {
            g_state.prompt_held &= Pad(1).buttons;
        }
    }

    StepPanel(0, Pad(0));
    StepPanel(1, Pad(1));
    StepKeyPickFor(1, KeyPickPad(1)); // coop-keypick: player 1's own picker at a lock, the game plays on
    if (mode == kDungeonMode) {
        StepUsePrompt(1, Pad(0)); // coop-hischest: his own prompt at a trapped or clown chest, the game plays on
    }
    if (mode == kTownMode) {
        StepKeyPickFor(2, KeyPickPad(2)); // coop-doorprompt: hers at the town's cave door (on a floor her walk steps it)
    }

    if (mode == kDungeonMode) {
        StepSwapQuestion(); // coop-townwheel: the swap question, also while the world holds for it
    }
    // coop-pause: each player's own pause (Start) and the zone's hold.
    PauseTick(Pad(0), Pad(1));
    VpadTick();  // the test's virtual gamepads (DC_TEST_VPAD)
    SlotTrace(); // coop-inventory: the slots' test trace (DC_COOP_SLOTTRACE)

    // The non-pause test's twin: an open overlay holds the game, as a pause would.
    if (Mutated("menupause") && (g_state.panel[0].open || g_state.panel[1].open)) {
        driveStepHold = 1;
    }

    DebugLog();
}

void MenuFilterPort(int port, PAD_STATUS *status) {
    if (port == 0) {
        status->button |= CoopWorldsTestPadButtons(); // coop-bagapart: her presses apart (a test hook; 0 otherwise)
    }
    if (port == 0) {
        CoopWorldsGamePad0(&status->button); // coop-gatepopup3: the game's own pad, for an apart call's keys
    }
    if (port == 0 && mode == kTownMode) {
        CoopWorldsDoorQuestionPad(&status->button); // coop-doorgate: a shut door's question, its hand's keys
    }
    if (port == 0 && mode == kDungeonMode) {
        CoopWorldsEntrancePad(&status->button); // coop-gatepopup2: the floor menu's pick of a floor that needs both
    }
    // coop-gatepopup: the asker or the asked of a gate's question stands meanwhile (its keys are read from the agreed pads).
    // Test hook (DC_WORLDS_TEST_QUESTION_STICK=1, both copies): his stick held down while a door's call is open (x-door-gate hold).
    static const bool question_stick = TestEnvOn("DC_WORLDS_TEST_QUESTION_STICK");

    if (port == 0 && question_stick && (CoopWorldsDoorAsks(1) || CoopWorldsDoorAsks(2))) {
        status->left_y = 0xFF;
    }
    if (port == 0 && CoopWorldsQuestionHolds(0)) {
        status->button = 0;
        status->left_x = status->left_y = status->right_x = status->right_y = 0x80;
        return;
    }
    // coop-doorfix: the cave door acting on the game's walker (retail's refusal, the walk in) holds its pad, as its event did.
    if (port == 0 && CoopTownCaveHoldsGamePad()) {
        status->button = 0;
        status->left_x = status->left_y = status->right_x = status->right_y = 0x80;
        return;
    }
    // coop-cavehold: the Cross that closed the walker's door message stays off the game until let go (else the door's event,
    // run each tick in reach, takes it as a new Cross and shows the message again).
    if (port == 0 && mode == kTownMode) {
        status->button &= ~TownNoteHeldAfter(1) & 0xFFFF;
    }

    // coop-pt1111n: a non-holder of a character-limited floor sits out its fights, player 1 as player 2 (Square and R2 off the pad).
    if (port == 0) {
        ZoneFilterPort(status);
    }

    if (!MenusOn() || port != 0) {
        return;
    }

    // Player 2's prompt: port 1 reads her pad (what she held as she took it stays released until let go),
    // so she answers it; player 1's pad answers nothing meanwhile.
    if (g_state.prompt && !Mutated("menuprompt")) {
        InputPadState two = Pad(1);

        status->button = two.buttons & ~g_state.prompt_held & 0xFFFF;
        status->left_x = two.left_x;
        status->left_y = two.left_y;
        status->right_x = two.right_x;
        status->right_y = two.right_y;
        return;
    }

    // coop-p2prompt: asked about her exit gate beside his game, his Cross and Circle answer it; his stick still walks Toan.
    if (mode == kDungeonMode && CoopWorldsVoteAsks(1) && !Mutated("voteholdpad")) {
        status->button &= ~(kInputCross | kInputCircle | kInputUp | kInputDown) & 0xFFFF; // (coop-floorgate: Up and Down move the hand)
    }
    // coop-keypick: player 1's picker at a lock has his pad (it steps in MenuTick); Toan stands, the world goes on.
    // coop-doorprompt: and his picker at the town's cave door.
    // coop-hischest: and his prompt at a trapped or clown chest; the Cross that closed it stays his until let go (else the
    // game's DunMoveChara takes it on the closing tick: the chest disarmed by it opens at once).
    if (mode == kDungeonMode) {
        status->button &= ~UsePromptHeldAfter(1) & 0xFFFF;
    }
    // coop-skeldoor: and his wrong item's refusal at a key door (retail's script holds its player through it).
    // coop-doorfix: in a town apart, the picker of this copy's player (the game's walker).
    if (((mode != kTownMode || TownDoorPickOpenFor(GamePadPlayer())) && KeyPickOpenFor(GamePadPlayer())) ||
        (mode == kDungeonMode && (UsePromptOpen(1) || DoorRefusing(1)))) {
        status->button = 0;
        status->left_x = status->left_y = status->right_x = status->right_y = 0x80;
        return;
    }

    // In a town player 2 may walk Toan (and talk to the shopkeepers): port 1 reads her pad, less what she
    // held as she took it, and her Triangle is her overlay's (which, open, holds her). The driver's
    // Start that changed hands is held back from the town's own menu.
    if (mode == kTownMode) {
        if (g_state.driver == 2) {
            InputPadState two = Pad(1);

            // Her Triangle is the overlay's only with game.coop_menu_panel on; else the town's menu (StepTownTriangle).
            const std::uint16_t overlay = MenuPanelOn() ? kInputTriangle : 0;

            status->button = g_state.panel[1].open ? 0 : two.buttons & ~g_state.driver_held & ~overlay & 0xFFFF;
            status->left_x = g_state.panel[1].open ? 0x80 : two.left_x;
            status->left_y = g_state.panel[1].open ? 0x80 : two.left_y;
            status->right_x = g_state.panel[1].open ? 0x80 : two.right_x;
            status->right_y = g_state.panel[1].open ? 0x80 : two.right_y;
        } else {
            status->button &= ~g_state.driver_held;
        }

        return;
    }

    // Player 1's overlay has his pad: his character stands and nothing else sees the buttons.
    if (g_state.panel[0].open && !Mutated("menuhold")) {
        status->button = 0;
        status->left_x = status->left_y = status->right_x = status->right_y = 0x80;
        return;
    }

    // coop-pause: Start is each player's own pause on a floor; player 1's pad reaches nothing while he is in it.
    PauseFilterPort(status);

    // On a floor Triangle is the overlay's, not the game's menu (with the overlay on; and in the trianglemenu
    // twin, as before game.coop_menu_panel).
    if ((MenuPanelOn() || Mutated("trianglemenu")) && mode == kDungeonMode && gameTask == GAME_TASK_PLAY && !Mutated("menutriangle")) {
        status->button &= ~kInputTriangle;
    }
}

void MenuShopBegin() {
    CUserStatus *status = SaveData->GetDngStatus();

    if (!MenusOn() || !OwnBags() || g_state.driver != 2 || g_state.shop_swapped || status == nullptr || Mutated("menushop")) {
        return;
    }

    ITEM_PACK    &bag = PartnerPack();
    P2Bag        &mine = g_bag;
    std::uint16_t money = status->money;

    std::swap(status->item_pack, bag);
    std::swap(status->active_item_vol, mine.active_item_vol);
    status->money = mine.gilda;
    mine.gilda = money;
    g_state.shop_swapped = true;
    std::printf("coop-menu: shop opens for P2: she pays %d gilda from her own bag (%d slots); P1 keeps %d\n", (int) status->money,
                (int) status->item_pack.num, (int) mine.gilda);
}

void MenuShopTrace(int talk, int side, int cursor, int money, int done) {
    static int ticks = 0;

    if (Debug() && (++ticks % 10 == 0 || done != 0)) {
        std::printf("coop-menu: shop tick %d talk %d side %d cursor %d money %d done %d\n", ticks, talk, side, cursor, money, done);
    }
}

// coop-itemsend: whether a MenuSwapP2Bag has player 2's bag in the party status now.
static bool g_p2_bag_in_pack = false;

// coop-localmenu 3c: player 1's hand reloaded from the weapon folder (ReloadPlayerWeapon), for an equip from a local menu.
bool MenuReloadPlayerWeapon(int chara) {
    return ReloadPlayerWeapon(chara);
}

bool MenuSwapP2Bag() {
    CUserStatus *status = Status();

    if (!OwnBags() || status == nullptr) {
        return false;
    }

    std::uint16_t money = status->money;

    std::swap(status->item_pack, PartnerPack());
    std::swap(status->active_item_vol, g_bag.active_item_vol);
    status->money = g_bag.gilda;
    g_bag.gilda = money;
    g_p2_bag_in_pack = !g_p2_bag_in_pack;
    return true;
}

bool MenuP2BagInPack() {
    return g_p2_bag_in_pack;
}

void MenuShopEnd() {
    CUserStatus *status = SaveData->GetDngStatus();

    if (g_state.shop_swapped && status != nullptr) {
        ITEM_PACK    &bag = g_bag.pack;
        std::uint16_t money = status->money;

        std::swap(status->item_pack, bag);
        std::swap(status->active_item_vol, g_bag.active_item_vol);
        status->money = g_bag.gilda;
        g_bag.gilda = money;
        g_state.shop_swapped = false;
        std::printf("coop-menu: shop closes: P2 has %d gilda, P1 %d\n", (int) g_bag.gilda, (int) status->money);
    }

    if (MenusOn() && g_state.driver == 2) {
        g_state.driver = 1;
        g_state.driver_held = Pad(0).buttons;
        std::printf("coop-menu: town walk back to P1 (shop closed)\n");
    }
}

int MenuTaskNow() {
    return (int) gameTask;
}

bool MenuNotePrompt(bool started, int task_before) {
    // coop-p2atla: only what started a task asks anything (a script, a trapped chest). What her Cross
    // did at once (an Atla taken, a chest she opened herself, a refusal, a swap question) leaves play under control, and
    // owning the prompt for that tick drew the cover on his copy for a frame (Jep: "the screen flashes Xiao is
    // choosing on a black screen for a frame"). DC_COOP_MUTATE=atlaprompt is the twin.
    // coop-pt1109c (Jep's 11-09 playtest 1h: she took an Atla during his chest and "Xiao is choosing..." blacked his screen): the task that was running
    // already (his chest's) is not hers. Her Cross's own acts beside his presentation start no task. DC_COOP_MUTATE=prompt_his_task (twin): any task, as before.
    const bool task_started = gameTask != GAME_TASK_PLAY && (task_before < 0 || (int) gameTask != task_before || Mutated("prompt_his_task"));

    if (started && MenusOn() && (task_started || Mutated("atlaprompt"))) {
        g_state.prompt = true;
        g_state.prompt_held = Pad(1).buttons;
        g_state.panel[1].open = false;
        std::printf("coop-menu: P2 owns the prompt (task %d)\n", (int) gameTask);
    }

    return started;
}

void MenuDrawPromptCover() {
    int local = NetplayLocalPlayer();

    if (!MenusOn() || !g_state.prompt || local != 1 || mode != kDungeonMode || Mutated("menupromptcover")) {
        return;
    }

    static const spRGBA kCover = {0x00, 0x00, 0x00, 0x80};
    static int          said = 0;

    set2DSpriteC4(Vif1Packet, CRect_i_(0, 0, 640, 448), (spRGBA *) &kCover, (spRGBA *) &kCover, (spRGBA *) &kCover, (spRGBA *) &kCover);

    int         chara = PartnerChara();
    std::string line = std::string(chara >= 0 ? kNames[chara] : "Your partner") + " is choosing...";

    dc::text::caption::LineStyle style;

    style.status_font = true;
    style.edge_width = 1.5f;
    dc::text::caption::DrawLine(line, 320.0f, 210.0f, 12.0f, 1.0f, 400.0f, style, 1.0f, 1.0f, nullptr);
    g_cover_drawn++;

    if (said++ == 0 && Debug()) {
        std::printf("coop-menu: prompt cover: %s\n", line.c_str());
    }
}

bool MenuHoldsPartner(InputPadState &pad) {
    if (!MenusOn()) {
        return false;
    }

    // DC_COOP_MUTATE=menupartnerhold (menu-p2-drift's twin): her open overlay leaves her pad to her character.
    // coop-pause: in her own pause she stands too.
    if ((g_state.panel[1].open && !Mutated("menupartnerhold")) || PausePlayer(2) || CoopWorldsQuestionHolds(1)) { // (coop-gatepopup: and in a gate's question)
        CombatCancel(pad.buttons);
        pad = Neutral();
        return true;
    }

    pad.buttons &= ~kInputTriangle;
    return false;
}

void MenuLeaveFloor(bool leave) {
    if (leave == g_state.away || Mutated("menuleave")) {
        return;
    }

    g_state.away = leave;
    g_state.away_key = FloorKey();
    g_state.rejoin = !leave;
    SoundOwner sound(2); // coop-menuse: her overlay's Leave floor / Rejoin
    SndSePlay(MENU_SOUND_CONFIRM, -1, 0);
    std::printf("coop-menu: P2 %s the floor\n", leave ? "leaves" : "rejoins");
}

bool MenuPartnerAway() {
    if (!MenusOn()) {
        return false;
    }

    // A new floor brings her back.
    if (g_state.away && FloorKey() != g_state.away_key) {
        g_state.away = false;
        g_state.rejoin = false;
        std::printf("coop-menu: P2 back on the new floor\n");
    }

    if (g_state.rejoin && !g_state.away) {
        g_state.rejoin = false;
        PlaceBesidePlayer("menu: P2 rejoins the floor");
    }

    return g_state.away;
}

bool MenuTownWalk() {
    return InTown();
}

bool MenuPanelOn() {
    return MenusOn() && MenuPanelSetting();
}

bool MenuOpensGameMenu() {
    // Triangle (the overlay off) or Start (the overlay on). DC_COOP_MUTATE=trianglemenu (t-triangle-menu's twin):
    // Start again with the overlay off, and Triangle taken off the floor's pad (MenuFilterPort), as before. Not while the game's menu is already open beside the world: the world's
    // step reads the other player's pad on port 1 then (coop_fullmenu.cpp), and the game has one menu.
    int button = MenuPanelOn() || Mutated("trianglemenu") ? PAD_START : PAD_TRIANGLE;

    const bool opens = MenusOn() && !FullMenuLive() && GamePad.Down(button) != 0 && Status()->hp[Status()->cur_chara] > 0;

    // coop-localmenu stage 3a: linked, the menu opens on its player's copy alone beside the world, and no menu task starts.
    return opens && !FullMenuOpenLocal();
}

bool MenuTakePause() {
    if (!g_state.pause) {
        return false;
    }

    g_state.pause = false;
    return true;
}

int MenuGameMenuOwner() {
    return NetplayMenuOwner();
}

void MenuDraw() {
    if (!MenusOn() || Status() == nullptr) {
        return;
    }

    PauseDraw(); // coop-pause

    int local = NetplayLocalPlayer();

    if (mode == kTownMode && g_state.driver == 2) {
        dc::text::caption::LineStyle banner;
        int                chara = PartnerChara();

        banner.status_font = true;
        banner.edge_width = 1.5f;
        banner.fill[0] = 120;
        banner.fill[1] = 200;

        std::string text = local == 2 ? std::string("You have control  -  Start: give it back")
                                      : std::string(chara >= 0 ? kNames[chara] : "Player 2") + " has control  -  Start: take it back";

        dc::text::caption::DrawLine(text, 320.0f, 30.0f, 9.0f, 1.0f, 300.0f, banner, 1.0f, 1.0f, nullptr);
    }

    for (int player = 0; player < 2; player++) {
        Panel &panel = g_state.panel[player];

        // Linked: only this copy's own player's. Couch: both, each in its corner.
        if (local != 0 && local != player + 1) {
            continue;
        }

        if (!panel.open && panel.notice_ticks == 0) {
            continue;
        }

        dc::text::caption::LineStyle style;

        style.status_font = true;
        style.edge_width = 1.5f;

        int   left = player == 0 ? 0x18 : 0x158;
        float centre = (float) left + 100.0f;
        int   top = 0xA8; // under both status panels and the day banner

        if (!panel.open) {
            dc::text::caption::DrawLine(panel.notice, centre, (float) top, 8.0f, 1.0f, 200.0f, style, 1.0f, 1.0f, nullptr);
            continue;
        }

        g_drawn[player]++;

        Page pages[5];
        int  page_count = Pages(player, pages);
        Page page = PageOf(player);
        int  rows[128];
        int  count = Rows(player, rows, 128);
        int  chara = CharaOf(player);

        std::vector<std::string> lines;

        switch (page) {
            case kItems:
            case kSend:
                for (int i = 0; i < count; i++) {
                    lines.push_back(ItemName(rows[i]) + "  x" + std::to_string(Count(player, rows[i])));
                }
                break;
            case kEquip:
                for (int i = 0; i < count; i++) {
                    const WEAPON_HAVE &have = Status()->chara_weapons[chara][rows[i]];
                    std::string        text = ItemName(have.item_no) + "  " + std::to_string(have.attack);

                    if (rows[i] == Status()->equipped_weapon_slot[chara]) {
                        text += "  (E)";
                    }

                    lines.push_back(text);
                }
                break;
            case kStatus: {
                int life = player == 0 ? Status()->hp[chara] : (mode == kDungeonMode && chara >= 0 && chara == g_partner.loaded_chara ? Life() : Status()->hp[std::max(0, chara)]);
                int most = player == 0 ? Status()->max_hp[chara] : (mode == kDungeonMode && chara >= 0 && chara == g_partner.loaded_chara ? MaxLife() : Status()->max_hp[std::max(0, chara)]);
                const WEAPON_HAVE *weapon = EquippedOf(player);
                int gilda = player == 0 ? Status()->money : PartnerGilda();

                lines.push_back(std::string(chara >= 0 ? kNames[chara] : "-") + "  life " + std::to_string(life) + "/" + std::to_string(most));
                if (chara >= 0) {
                    lines.push_back("water " + std::to_string((int) Status()->water_now[chara]) + "/" + std::to_string((int) Status()->water_max[chara]) +
                                    "  defence " + std::to_string((int) Status()->defense[chara]));
                }
                if (weapon != nullptr) {
                    lines.push_back(ItemName(weapon->item_no) + " lv " + std::to_string(weapon->level) + "  atk " + std::to_string(weapon->attack));
                    lines.push_back("durability " + std::to_string((int) weapon->durability_f) + "/" + std::to_string(weapon->durability) +
                                    "  abs " + std::to_string(weapon->experience));
                }
                lines.push_back(std::to_string(gilda) + " G");
                break;
            }
            case kSystem:
                lines.push_back("Pause game");
                if (player == 1) {
                    lines.push_back(g_state.away ? "Rejoin" : "Leave floor");
                }
                break;
        }

        int first = std::max(0, std::min(panel.row - 4, (int) lines.size() - 9));
        int shown = std::min((int) lines.size() - first, 9);

        static const spRGBA kBack = {0x08, 0x10, 0x48, 0x70};

        set2DSpriteC4(Vif1Packet, CRect_i_(left, top - 4, 200, 30 + 11 * std::max(1, shown)), (spRGBA *) &kBack, (spRGBA *) &kBack,
                      (spRGBA *) &kBack, (spRGBA *) &kBack);

        std::string title = std::string(player == 0 ? "1P " : "2P ") + (chara >= 0 ? kNames[chara] : "") + "  < " + kPageNames[page] + " >";

        dc::text::caption::DrawLine(title, centre, (float) top, 8.0f, 1.0f, 190.0f, style, 1.0f, 1.0f, nullptr);

        if (lines.empty()) {
            dc::text::caption::DrawLine("(none)", centre, (float) top + 12.0f, 8.0f, 1.0f, 190.0f, style, 0.7f, 1.0f, nullptr);
        }

        for (int i = 0; i < shown; i++) {
            int                at = first + i;
            bool               current = page != kStatus && at == panel.row;
            dc::text::caption::LineStyle row_style = style;
            std::string        text = lines[at];

            if (current) {
                row_style.fill[2] = 120;
                text = "> " + text;
            }

            dc::text::caption::DrawLine(text, centre, (float) top + 12.0f + 11.0f * i, 8.0f, 1.0f, 190.0f, row_style, current ? 1.0f : 0.75f, 1.0f,
                              nullptr);
        }

        const char *hint = page == kItems ? (player == 1 && OwnBags() ? "X use  [] slot" : "X use")
                           : page == kEquip ? "X equip" : page == kSend ? "X send" : page == kSystem ? "X choose" : "";
        std::string foot = panel.notice_ticks > 0 ? std::string(panel.notice) : std::string(hint) + "   /\\ close";

        dc::text::caption::DrawLine(foot, centre, (float) top + 14.0f + 11.0f * std::max(1, shown), 7.0f, 1.0f, 190.0f, style, 0.8f, 1.0f, nullptr);
        (void) page_count;
    }
}

void MenuDrawGameMenuCover() {
    int local = NetplayLocalPlayer();
    int owner = MenuGameMenuOwner();

    if (!MenusOn() || local == 0 || local == owner || Status() == nullptr || Mutated("menucover")) {
        return;
    }
    // coop-town (Jep, 2026-10-21: "a black box covers everything saying Xiao is in the menu"): no cover in a town;
    // each copy draws its own player's menu and the other's out of sight (coop_townmenu). DC_COOP_MUTATE=towncover:
    // the cover as before (twin).
    if (mode == kTownMode && !Mutated("towncover")) {
        return;
    }
    // Nor on a dungeon floor (Jep, 2026-10-24: it flashed over the other player's screen, and over the second player's
    // own menu once both could be in the menu): the menus run beside the world and each copy shows its own player's.
    // DC_COOP_MUTATE=dungeoncover: the cover as before (twin of t-triangle-menu-both's cover check).
    if (mode != kTownMode && !Mutated("dungeoncover")) {
        return;
    }

    static const spRGBA kCover = {0x00, 0x00, 0x00, 0x80};

    set2DSpriteC4(Vif1Packet, CRect_i_(0, 0, 640, 448), (spRGBA *) &kCover, (spRGBA *) &kCover, (spRGBA *) &kCover, (spRGBA *) &kCover);

    int         chara = owner == 1 ? (int) Status()->cur_chara : PartnerChara();
    std::string line = std::string(chara >= 0 ? kNames[chara] : "Your partner") + " is in the menu";

    dc::text::caption::LineStyle style;

    style.status_font = true;
    style.edge_width = 1.5f;
    dc::text::caption::DrawLine(line, 320.0f, 210.0f, 12.0f, 1.0f, 400.0f, style, 1.0f, 1.0f, nullptr);

    if (g_cover_drawn++ == 0 && Debug()) {
        std::printf("coop-menu: cover: %s\n", line.c_str());
    }
}

std::uint32_t MenuHash() {
    if (!MenusOn()) {
        return 0;
    }

    std::uint32_t hash = 2166136261u;
    auto          add = [&](int value) { hash = (hash ^ (std::uint32_t) value) * 16777619u; };

    for (const Panel &panel : g_state.panel) {
        add(panel.open);
        add(panel.page);
        add(panel.row);
        add(panel.prev);
    }

    add(g_state.away);
    add(g_state.away_key);
    add(g_state.rejoin);
    add(g_state.pause);
    add(g_state.prompt);
    add(g_state.prompt_held);
    add(g_state.driver);
    add(g_state.driver_held);
    add(g_state.start_prev[0]);
    add(g_state.start_prev[1]);
    add(g_state.shop_swapped);
    add((int) PauseHash()); // coop-pause
    return hash;
}

void MenuResetForSession() {
    g_state = State{};
    PauseResetForSession(); // coop-pause
    PresentResetForSession(); // coop-opt7
}

bool MenuGameHelp(int flag) {
    switch (flag) {
        case BTLMENU_STATE_CHARA:
        case BTLMENU_STATE_CHARA_OPEN:
        case BTLMENU_STATE_CHARA_CLOSE:
        case BTLMENU_STATE_WEAPON:
        case BTLMENU_STATE_WEAPON_OPEN:
        case BTLMENU_STATE_WEAPON_CLOSE:
        case BTLMENU_STATE_ITEM:
        case BTLMENU_STATE_ITEM_OPEN:
        case BTLMENU_STATE_ITEM_CLOSE:
        case BTLMENU_STATE_ATLA:
        case BTLMENU_STATE_ATLA_OPEN:
        case BTLMENU_STATE_ATLA_CLOSE:
            return !Mutated("menuhelp_off");
        default:
            return false;
    }
}

void MenuGameHelpDrawn(int flag, int message) {
    static int last = -1;
    static int last_mode = -1;

    if (message != last || WepMenu.mode != last_mode) {
        last = message;
        last_mode = WepMenu.mode;
        std::fprintf(stderr, "coop-attachhelp: the game's help window shows message %d (menu state %d, weapon mode %d, copy of player %d)\n",
                     message, flag, (int) WepMenu.mode, NetplayRole());
    }
}

} // namespace coop
