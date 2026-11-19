// The co-op menus' glue (each player's own menu beside the world, the town walk, the prompt); see coop_menu.hpp.

#include "coop/game_ns.hpp"
#include "coop/coop_opener.hpp" // coop-opt7
#include "coop/coop_menu.hpp"
#include "coop/coop_audio.hpp" // coop-menuse
#include "coop/coop_combat.hpp" // coop-comboaudio: CombatTestP1Buttons
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
#include "coop/coop_actor.hpp" // coop-bagoverflow: PlayerInMenu
#include "coop/coop_inventory.hpp"
#include "coop/coop_interact.hpp" // coop-keypick
#include "coop/coop_worlds.hpp"   // coop-p2prompt: CoopWorldsVoteAsks
#include "coop/coop_prompt.hpp"   // coop-townwheel: StepSwapQuestion
#include "coop/coop_party.hpp"
#include "coop/coop_pause.hpp"
#include "coop/coop_wheel.hpp" // coop-p2controls3: StepElementRings
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

struct State {
    bool  away = false;   // player 2 has left the floor
    int   away_key = -1;  // the floor she left (selectMapNo * 256 + floor)
    bool  rejoin = false; // put her back beside player 1 on the next step

    // A script player 2 started that runs as the game's one task (a shared story scene: the world holds for everyone): its
    // windows are hers, so port 1 reads her pad. coop-choosing (Jep 11-12): nothing is drawn for it on any copy (the black
    // "<name> is choosing..." cover is gone); every copy shows the scene as the game draws it. Doors, chests, keys and the floor
    // menu never take it (they run beside the game, each player's own). held: the buttons pad 2 still held as it took port 1.
    bool          prompt = false;
    std::uint16_t prompt_held = 0;
    int           prompt_key = -1000; // coop-entrance2: the floor her prompt began on (selectMapNo * 256 + floor)

    // In a town: who walks Toan and so talks to the shopkeepers (1 or 2; player 2's Start takes it, either
    // Start gives it back, a shop closing gives it back), the buttons the new driver still held as it
    // changed hands, Start last tick on each pad, and whether a shop has player 2's purse and bag in.
    int           driver = 1;
    std::uint16_t driver_held = 0;
    bool          start_prev[2] = {false, false};
    bool          shop_swapped = false;
};

State g_state;

// The draw count the tests read (DC_COOP_MENUDEBUG): the game menu's cover.
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
    return MenusOn() && mode == kDungeonMode && CoopPlainPlay() && BtEventMode == 0 && Status() != nullptr &&
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

    if (GameMode == kWalkMode && !Mutated("menutown") && !CoopP2Building()) { // coop-town: her Georama
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

// Player 2's Triangle in a town's walk is her menu: it hands her the
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
    if (!Mutated("townwalktriangle") || GameMode != kWalkMode || g_state.driver != 1 || !pressed[1] || pressed[0] ||
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

const InputPadState &Neutral() {
    static InputPadState pad = [] {
        InputPadState p;
        p.connected = true;
        return p;
    }();
    return pad;
}

// DC_COOP_MENUDEBUG: both players' positions and the task every 30 ticks, for the tests.
void DebugLog() {
    static int ticks = 0;

    if (!Debug() || ++ticks % 30 != 0 || Status() == nullptr) {
        return;
    }

    float town[4] = {0.0f, 0.0f, 0.0f, 0.0f};

    if (mode == kTownMode && MainChara.frame != nullptr) {
        MainChara.GetPosition(town);
    }

    std::printf("coop-menu: tick %d mode %d task %d p1 (%.1f %.1f %.1f) p2 (%.1f %.1f %.1f) away %d cover %d "
                "owner %d local %d hash %08x floor %d prompt %d vis %d town (%.1f %.1f)\n",
                ticks, (int) mode, (int) gameTask, CharaMain.pos[0], CharaMain.pos[1], CharaMain.pos[2], g_partner.pos[0], g_partner.pos[1],
                g_partner.pos[2], (int) g_state.away,
                g_cover_drawn, 1, NetplayLocalPlayer(), MenuHash(), (int) Status()->cur_floor,
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

    if (done || (give == nullptr && wear == nullptr && wear1 == nullptr && TestEnv("DC_COOP_TEST_P1_WEAPONS") == nullptr) || !OnFloor() ||
        g_partner.loaded_chara < 0) {
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

    // coop-wepmenu: DC_COOP_TEST_P1_WEAPONS=N: the player's own member holds N more weapons (its next N after its starting one, in
    // slots 1..N), a bag of weapons as a player's some floors in, for the weapons page's first open (its models built from each).
    if (const int more = std::clamp(TestEnvInt("DC_COOP_TEST_P1_WEAPONS", 0), 0, 9); more > 0) {
        CUserStatus *status = Status();
        const int    chara = status->cur_chara;

        for (int i = 1; i <= more; i++) {
            WepDataListToHaveCopy(StarterWeapon(chara) + i, &status->chara_weapons[chara][i]);
        }
        std::printf("coop-menu: test P1 member %d holds %d more weapons (items %d to %d)\n", chara, more, StarterWeapon(chara) + 1,
                    StarterWeapon(chara) + more);
    }

    if (wear1 != nullptr) {
        CUserStatus *status = Status();
        WEAPON_HAVE *weapon = &status->chara_weapons[status->cur_chara][(int) status->equipped_weapon_slot[status->cur_chara]];

        weapon->durability_f = (float) std::atoi(wear1);
        std::printf("coop-menu: test P1 weapon item %d durability %.0f/%d\n", weapon->item_no, weapon->durability_f, weapon->durability);
    }
}

} // namespace

// coopoptions (Jep 11-17): the co-op menus are always on in co-op (game.coop_menus and game.coop_menu_panel retired: the menus
// open beside the running world, each player's own; the old per-player overlay is gone).
bool MenusOn() {
    return Enabled();
}

void MenuTick() {
    if (!MenusOn()) {
        StepElementRings(Pad(0), Pad(1)); // coop-p2controls3: each player's element ring (no pause rule without the co-op menus)
        VpadTick(); // the test's virtual gamepads also drive a copy without the co-op menus (mp-menu-pointer-pad)
        return;
    }

    TestSetup();
    TestGold();
    StepTownDriver(Pad(0), Pad(1));
    StepTownTriangle(Pad(0), Pad(1));
    TestShop();

    // Player 2's prompt lasts until play is back under control (or the floor is left).
    // coop-entrance2 (Jep 11-12: "the 'Toan is choosing...' overlay stays up through the whole walk-in when P2 picked the floor"): a
    // floor left by her pick stays in the dungeon's mode, and the next floor's walk-in (script 100) is an event, so play was not
    // "back under control" till that walk-in ended: his screen kept the cover over the floor's opening. The floor changing ends it
    // too (her pick made). (At the candidate cand1112c, with coop-choosing: no cover is drawn any more and the floor menu is never
    // hers for him, so the prompt routes only a shared scene's windows; its twin prompt_outlives_floor is retired, the end kept.)
    const int floor_key = Status() != nullptr ? selectMapNo * 256 + Status()->cur_floor : -1000;

    if (g_state.prompt) {
        const bool floor_left = g_state.prompt_key != floor_key;

        if (mode != kDungeonMode || (CoopPlainPlay() && BtEventMode == 0) || floor_left) {
            g_state.prompt = false;
            std::printf("coop-menu: P2's prompt over (mode %d task %d floor %d)%s\n", (int) mode, (int) gameTask,
                        Status() != nullptr ? (int) Status()->cur_floor : -1, floor_left ? ": the floor left (her pick made)" : "");
            std::fprintf(stderr, "coop-menu: P2's prompt over (task %d event %d floor %d)%s\n", (int) gameTask, (int) BtEventMode,
                         Status() != nullptr ? (int) Status()->cur_floor : -1, floor_left ? ": the floor left (her pick made)" : "");
        } else {
            g_state.prompt_held &= Pad(1).buttons;
        }
    }

    StepKeyPickFor(1, KeyPickPad(1)); // coop-keypick: player 1's own picker at a lock, the game plays on
    if (mode == kDungeonMode) {
        StepUsePrompt(1, Pad(0)); // coop-hischest: his own prompt at a trapped or clown chest, the game plays on
    }
    if (mode == kTownMode) {
        StepKeyPickFor(2, KeyPickPad(2)); // coop-doorprompt: hers at the town's cave door (on a floor her walk steps it)
    }

    if (mode == kDungeonMode || mode == kTownMode) { // (coop-townally: in a town too, its Allies page's trade)
        StepSwapQuestion(); // coop-townwheel: the swap question, also while the world holds for it
    }
    // coop-p2controls3: each player's element ring, a pause state of theirs (before the pauses read the zone's hold), stepped here
    // where the world's hold does not stop it, as the pauses are.
    StepElementRings(Pad(0), Pad(1));
    // coop-pause: each player's own pause (Start) and the zone's hold.
    PauseTick(Pad(0), Pad(1));
    VpadTick();  // the test's virtual gamepads (DC_TEST_VPAD)
    SlotTrace(); // coop-inventory: the slots' test trace (DC_COOP_SLOTTRACE)

    DebugLog();
}

void MenuFilterPort(int port, PAD_STATUS *status) {
    if (port == 0) {
        status->button |= CoopWorldsTestPadButtons(); // coop-bagapart: her presses apart (a test hook; 0 otherwise)
        status->button |= BoneDoorTestButtons();     // coop-choosing: x-back-half -Later's presses (a test hook; 0 otherwise)
        status->button |= CombatTestP1Buttons();     // coop-comboaudio: the combo rows' P1 attack (a test hook; 0 otherwise)
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
        // coop-p2actions: and the press that closed his key picker (the Cross that used the key swung his weapon: MenuTick
        // closes the picker before this tick's pad is read, so it was a new press to DunMoveChara).
        status->button &= ~KeyPickHeldAfter(1) & 0xFFFF;
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
    // held as she took it. The driver's Start that changed hands is held back from the town's own menu.
    if (mode == kTownMode) {
        if (g_state.driver == 2) {
            InputPadState two = Pad(1);

            status->button = two.buttons & ~g_state.driver_held & 0xFFFF;
            status->left_x = two.left_x;
            status->left_y = two.left_y;
            status->right_x = two.right_x;
            status->right_y = two.right_y;
        } else {
            status->button &= ~g_state.driver_held;
        }

        return;
    }

    // coop-pause: Start is each player's own pause on a floor; player 1's pad reaches nothing while he is in it.
    PauseFilterPort(status);

    // The trianglemenu twin: Triangle taken off the floor's pad, as before Triangle opened the game's menu.
    if (Mutated("trianglemenu") && mode == kDungeonMode && CoopPlainPlay() && !Mutated("menutriangle")) {
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
    // coop-bagoverflow: her overflow flag with her bag: retail's menu sets and clears the status's own on its throw-away page
    // (BattleMenuInit, Btl_InitItemMode, the throw), which with her bag in is hers; the party's waits beside it.
    std::swap(status->overflow_flag, g_bag.overflow);
    status->money = g_bag.gilda;
    g_bag.gilda = money;
    g_p2_bag_in_pack = !g_p2_bag_in_pack;
    return true;
}

bool MenuP2BagInPack() {
    return g_p2_bag_in_pack;
}

InputPadState MenuOwnPad() {
    return Pad(0);
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
    // owning the prompt for that tick held his pad for a tick (and, before coop-choosing, drew a cover on his copy for a frame).
    // DC_COOP_MUTATE=atlaprompt is the twin.
    // coop-pt1109c (Jep's 11-09 playtest 1h: she took an Atla during his chest and "Xiao is choosing..." blacked his screen): the task that was running
    // already (his chest's) is not hers. Her Cross's own acts beside his presentation start no task. DC_COOP_MUTATE=prompt_his_task (twin): any task, as before.
    const bool task_started = !CoopPlainPlay() && (task_before < 0 || (int) gameTask != task_before || Mutated("prompt_his_task"));

    if (started && MenusOn() && (task_started || Mutated("atlaprompt"))) {
        g_state.prompt = true;
        g_state.prompt_held = Pad(1).buttons;
        g_state.prompt_key = Status() != nullptr ? selectMapNo * 256 + Status()->cur_floor : -1000;
        std::printf("coop-menu: P2 owns the prompt (task %d)\n", (int) gameTask);
    }

    return started;
}

bool MenuHoldsPartner(InputPadState &pad) {
    if (!MenusOn()) {
        return false;
    }

    // coop-pause: in her own pause she stands.
    if (PausePlayer(2) || CoopWorldsQuestionHolds(1)) { // (coop-gatepopup: and in a gate's question)
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
    SoundOwner sound(2); // coop-menuse: her Leave floor / Rejoin (coop_zonechara)
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

bool MenuOpensGameMenu() {
    // Triangle. DC_COOP_MUTATE=trianglemenu (t-triangle-menu's twin): Start, and Triangle taken off the floor's pad
    // (MenuFilterPort), as before. Not while the game's menu is already open beside the world: the world's
    // step reads the other player's pad on port 1 then (coop_fullmenu.cpp), and the game has one menu.
    int button = Mutated("trianglemenu") ? PAD_START : PAD_TRIANGLE;

    const bool pressed = MenusOn() && !FullMenuLive() && GamePad.Down(button) != 0;
    // coop-downmenu: lying down, his Triangle opens his own menu for the dungeon's way out alone (FullMenuDownOpens).
    const bool opens = pressed && (Status()->hp[Status()->cur_chara] > 0 || FullMenuDownOpens(1));

    // coop-soakfloor (release soak seed 1244, dun1-f1: OUT OF SYNC at tick 1020, the melt timer and the stages apart): down
    // (life 0, lying in his knockout) his Triangle opens no menu here, but retail's standing Triangle below opens one for a
    // player whose movement is locked whatever his life, so retail's menu started (FullMenuBegin, every copy) and closed at once
    // (down). Beside her local menu, on her copy alone, her copy's loop ran hers and not that menu's opening tick: the copies
    // parted (as coop-soakmenu's first-person opening did, MenuEyeOpensLocal). Linked, where every menu is a local one, that
    // Triangle is no one's. DC_COOP_MUTATE=down_menu_retail (twin): retail's opening, as before.
    if (pressed && !opens && button == PAD_TRIANGLE && FullMenuLocalOn() && !Mutated("down_menu_retail")) {
        FullMenuDropPortOne(PAD_TRIANGLE);
    }

    // coop-localmenu stage 3a: linked, the menu opens on its player's copy alone beside the world, and no menu task starts.
    return opens && !FullMenuOpenLocal();
}

// coop-soakmenu (release soak seed 1625, floor menus: OUT OF SYNC at tick 360, task 0 on his copy and 0xA on hers, view and
// stages apart): linked, his Triangle in play opens his local menu (MenuOpensGameMenu), but in the first-person view retail's
// own check opened the menu task, and with it the menu beside the running world (FullMenuBegin, g_full), which every copy steps
// in FullMenuGameLoop. Her local menu was open already, so her copy ran her menu's engine (LocalGameLoop) instead and never
// stepped his: its retail close task put the view back (task 0xA) while his copy's g_full set play (task 0) and stepped his menu.
// Now his first-person Triangle opens his local menu too, the view held under it (his pad reaches the world as nothing), and its
// close command runs retail's close task, which goes back to the view (FullMenuClosedByCommand).
// DC_COOP_MUTATE=eye_menu_lockstep (twin): retail's opening, the menu beside the world, as before.
bool MenuEyeOpensLocal() {
    return MenusOn() && !FullMenuLive() && !Mutated("eye_menu_lockstep") && FullMenuOpenLocal();
}

// DC_COOP_MUTATE=overflow_retail_open (twin): the game's own opening whatever is open, as before (the menu opened again
// under itself every few ticks).
int MenuOverflowOpens() {
    CUserStatus *status = Status();

    if (!MenusOn() || status == nullptr || Mutated("overflow_retail_open")) {
        return 0;
    }
    if (FullMenuLive() || PlayerInMenu(1)) {
        return 1;
    }

    const int flag = status->overflow_flag;

    status->overflow_flag = false;
    if (FullMenuOpenLocal()) {
        std::printf("coop-localmenu: player 1's bag is past its room: his menu opens for the throw-away at link tick %lld\n", (long long) NetplayLinkTick());
        return 2;
    }
    status->overflow_flag = flag;
    return 0;
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
}

void MenuDrawGameMenuCover() {
    int local = NetplayLocalPlayer();
    int owner = 1; // (coop-wave2: retail's menu is player 1's; menu_owner retired)

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

    add(g_state.away);
    add(g_state.away_key);
    add(g_state.rejoin);
    add(g_state.prompt);
    add(g_state.prompt_held);
    add(g_state.prompt_key);
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
