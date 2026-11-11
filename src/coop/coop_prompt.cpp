#include "coop/game_ns.hpp"
#include "coop/coop.hpp"
#include "coop/coop_audio.hpp" // coop-menuse
#include "coop/coop_prompt.hpp"
#include "coop/netplay.hpp"

#include <libvu0.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>
#include <optional>
#include <string>

#include "btactstatus.hpp"
#include "btitem.hpp" // coop-speedicon
#include "btmisc.hpp"
#include "bt_shot_effect.hpp"
#include "dataread.hpp"
#include "dun/gameloop.hpp"
#include "gamepad.hpp"
#include "weaponeffect.hpp"
#include "itemdata.hpp"
#include "menu_dungeon.hpp"
#include "platform/arena.hpp" // coop-joinvisuals: ChangePlayer's files
#include "snd.hpp"
#include "userstatus.hpp"

#include "coop/coop_core.hpp"
#include "coop/coop_fullmenu.hpp" // coop-localmenu: FullMenuLocalPick
#include "coop/coop_inventory.hpp"
#include "coop/coop_party.hpp"

namespace dc::game {
extern s32 gameTask;
} // namespace dc::game

namespace coop {

// coop-floorscene: ChangePlayer's item-sheet reload waits for the floor's scene to end (a tick of play, StepPrompt). In the
// state set (qa\netplay\snapshot-allow.txt), so a copy that took the state mid-scene reloads on the same tick.
int g_icon_due = 0;

namespace {

const char *const kNames[6] = {"Toan", "Xiao", "Goro", "Ruby", "Ungaga", "Osmond"};

// Five seconds to answer.
constexpr int kAnswerTicks = 300;

struct Prompt {
    bool          active = false;
    bool          revive = false; // coop-revive: "Use Revive Powder?" (asker is then the one lying down)
    int           asker = 0;
    int           wanted = -1;
    int           spot = -1;
    int           ticks = 0;
    std::uint16_t held = 0;
    bool          locked = false;
    // coop-townwheel (Jep, 2026-11-02: the swap question "drawn with the in game yes/no menu"): the asked player's hand on the
    // popup (0 yes, 1 no), stepped from their pad; the asker's buttons (their Circle withdraws the question).
    int           cursor = 0;
    std::uint16_t asker_held = 0;
};

Prompt g_prompt;

// coop-revive: the host's switch, once the link has adopted it; until then this copy's own.
std::optional<bool> g_revive_host;
// coop-revive: the reviver (0 the player, 1 the partner) said no; asked again only after stepping away.
bool g_revive_declined[2] = {};

// The player's member as it last was on the floor (the menus overwrite cur_chara before they load).
int g_player_chara = -1;

InputPadState Pad(int port) {
    InputPadState linked;

    // coop-charaswap (Jep's 2026-10-28 run): each player answers with their own pad. Port 1 as the link routes it is the
    // menu owner's, so while her menu was open her Cross answered the swap she had just asked of player 1.
    // DC_COOP_MUTATE=promptpad (twin): the routed port, as before.
    if (!Mutated("promptpad") && NetplayPlayerPad(port + 1, linked)) {
        return linked;
    }
    return NetplayPad(port, linked) ? linked : InputGetPad(port);
}

// While the player is asked, the game reads none of its buttons (Cross would swing); it answers
// through the shared pad instead.
void LockPlayer(bool lock) {
    if (lock && !g_prompt.locked) {
        GamePad.KeyLock(1);
        g_prompt.locked = true;
    } else if (!lock && g_prompt.locked) {
        GamePad.KeyLock(0); // the prompt runs only in play, where the pad is never locked otherwise
        g_prompt.locked = false;
    }
}

void Close() {
    LockPlayer(false);
    g_prompt.active = false;
    g_prompt.revive = false;
    g_prompt.wanted = -1;
}

// The player becomes chara, as the party menus' change does it (CharaChangeInitToGL's files, then
// BtMenuLoadChara and the quick change's end), loaded at once from the disc into buffers of its own.
bool ChangePlayer(int chara) {
    static const char *const files[6] = {"dun/mainchara/c01d.chr", "dun/mainchara/c04b.chr", "dun/mainchara/c06b.chr",
                                         "dun/mainchara/c05a.chr", "dun/mainchara/c10b.chr", "dun/mainchara/c18a.chr"};
    static const int         first[6] = {ITEM_WEAPON_DAGGER_BROKEN,     ITEM_WEAPON_WOODENSLINGSHOT_BROKEN, ITEM_WEAPON_MALLET_BROKEN,
                                         ITEM_WEAPON_GOLD_RING_BROKEN, ITEM_WEAPON_FIGHTING_STICK_BROKEN,  ITEM_WEAPON_MACHINE_GUN_BROKEN};
    constexpr size_t         kBytes = 8u * 1024 * 1024;
    static u_char           *buffer = nullptr;
    static const char        kChangeArena = 0;

    // coop-joinvisuals (Jep's 2026-10-27 run: P1 joining her floor saw Toan's face from behind with the back of his head gone):
    // the player's model, textures and skinning work keep pointing into these files. On the C heap they lay at each copy's own
    // address, outside the state set, so a copy that took the other's state (her Toan, loaded here at her floor merge) drew
    // from bytes of its own that were something else. A pinned arena lies at one address on both copies and travels with
    // the state set. DC_COOP_MUTATE=change_heap (twin): the heap, as before.
    if (buffer == nullptr) {
        buffer = Mutated("change_heap") ? new (std::align_val_t{64}) u_char[kBytes] : ArenaBlock(&kChangeArena, kBytes);
    }

    u_char *at = buffer;
    u_int  *parts[5] = {};
    auto    load = [&](int part, const char *path) {
        int size = 0;

        at = buffer + ((at - buffer + 63) & ~(std::ptrdiff_t) 63);
        parts[part] = reinterpret_cast<u_int *>(at);

        if (LoadFile((char *) path, at, &size) == 0 || size <= 0 || at + size > buffer + kBytes) {
            std::fprintf(stderr, "coop: cannot load %s\n", path);
            return false;
        }

        at += size;
        return true;
    };

    char name[64];
    char cfg[64];
    char path[128];
    int  slot = UserStatus->equipped_weapon_slot[chara];
    int  kind = slot >= 0 && slot < 11 ? UserStatus->chara_weapons[chara][slot].item_no - first[chara] : 1;

    if (!load(0, files[chara])) {
        return false;
    }

    for (int part = 1; part <= 3; part++) {
        BtGetWeaponNamePath2(name, cfg, chara, part == 1 ? 0 : part == 2 ? 1 : std::max(0, kind));
        std::snprintf(path, sizeof path, "commenu/weapon/%s", name);

        if (!load(part, path)) {
            return false;
        }
    }

    int             element = slot >= 0 && slot < 11 ? UserStatus->chara_weapons[chara][slot].best_elem : 0;
    BT_SHOT_EFFECT *effect = Get_Main_EffectPtr(chara, element);

    if (effect != nullptr) {
        std::snprintf(path, sizeof path, "dun/mainchara/wep_eff/%s.chr", effect->model_name);

        if (!load(4, path)) {
            effect = nullptr;
        }
    }

    UserStatus->cur_chara = (s8) chara;
    LoadChara2(chara, 0, parts[0], parts[1], parts[2], parts[3]);
    SetWeaponAttachStatus(NowWeaponHave);
    CWeaponFx.InitSet(NowWeapon->frame, (char *) "dcol0", (char *) "dcol1");
    SetWeaponColor();
    MainChara_Effect(effect, effect != nullptr ? parts[4] : nullptr, 0);
    SndVoiceLoad(chara);
    BtActStatus.player_visible = 1;
    BtActStatus.motion_no = 0;
    BtActStatus.action_on = 0;
    LockOffTargte();
    nowUnitNow = chara;
    // coop-speedicon (Jep's Local-mode playtest: P2 alone on a floor saw Toan's weapon by the speed gauge): selectChrUnit
    // ends by reloading the HUD's item sheet and asking the next frame to copy the new player's weapon pictures into
    // "itempack" (LoadActiveItemIcon); without it the floor's copy, made while the player was Toan, stayed.
    // DC_COOP_MUTATE=change_icon (twin): skipped, as before.
    // coop-floorscene (x-floor-herscene: her merge into her own B1 lesson changed her player, and both copies crashed at the
    // lesson's next picture, GetPackFile from _LOAD_SPRITE_TEXTURE): the reload reads itemlst.img into read_buffer, where a
    // running scene's script keeps its picture pack (B1's e29img.pak). Under a scene it waits for the scene's end.
    // DC_COOP_MUTATE=scene_icon_now (twin): at once, as before. (coop-iconcrash51: a crash is no catch since coop-servecrash's
    // CRASH scoring; the twin says the reload it would make over the running scene and stops short of the read, as the
    // clutcrash twin does.)
    if (!Mutated("change_icon") && BtEventMode != 0 && !Mutated("scene_icon_now")) {
        g_icon_due = 1;
        std::printf("coop: the item sheet reload waits for the scene (script %d) to end\n", (int) BtEventInfo.running_script_no);
    } else if (!Mutated("change_icon") && BtEventMode != 0) {
        std::fprintf(stderr, "coop: the item sheet reloaded inside the scene (script %d), over its picture pack (scene_icon_now): read skipped\n",
                     (int) BtEventInfo.running_script_no);
    } else if (!Mutated("change_icon")) {
        LoadActiveItemIcon();
    }
    ResetStatusInfo();
    gameTask = GAME_TASK_CHARA_CHANGED;
    std::printf("coop: player becomes %d (%s)\n", chara, kNames[chara]);
    return true;
}

// Yes: the two trade members. The partner's new member loads on the next tick (PickPartner).
void Trade() {
    int player = UserStatus->cur_chara;
    int partner = g_partner.loaded_chara;

    std::printf("coop: swap accepted, player %d and partner %d trade\n", player, partner);

    if (ChangePlayer(partner)) {
        g_partner.choice = player;
    }
}

} // namespace

// coop-worlds: the player becomes chara (player 2's copy staying on a floor alone). Only a member with a
// weapon of its own equipped (a stand-in that never joined has none, and its weapon frame would be null).
bool PromptChangePlayer(int chara) {
    int slot = chara >= 0 && chara < 6 ? UserStatus->equipped_weapon_slot[chara] : -1;

    if (slot < 0 || slot >= 11 || UserStatus->chara_weapons[chara][slot].item_no <= 0) {
        std::printf("coop: member %d has no weapon equipped; the player stays %d\n", chara, (int) UserStatus->cur_chara);
        return false;
    }

    return ChangePlayer(chara);
}

bool PromptTradeForTest() {
    const int partner = g_partner.loaded_chara;

    Trade();
    return UserStatus->cur_chara == partner;
}

bool PromptAsksPartner() {
    return g_prompt.active && g_prompt.asker == 0;
}

void AskSwap(int asker, int wanted, int spot) {
    if (g_prompt.active || wanted < 0 || wanted > 5) {
        return;
    }

    g_prompt.active = true;
    g_prompt.asker = asker;
    g_prompt.wanted = wanted;
    g_prompt.spot = spot;
    g_prompt.ticks = kAnswerTicks;
    // A button already down (the menu's Cross that asked) is no answer.
    g_prompt.held = Pad(1 - asker).buttons;
    g_prompt.asker_held = Pad(asker).buttons;
    g_prompt.cursor = 0;
    SoundOwner sound(2 - asker); // coop-menuse: the question's sound is the asked player's
    SndSePlay(MENU_SOUND_CURSOR, -1, 0);
    std::printf("coop: %dP asks for %d (%s); %dP to answer\n", asker + 1, wanted, kNames[wanted], 2 - asker);
}

// coop-townwheel: the asked player's Up, Down, Left or Right moves the popup's hand between YES and NO (as the menu's
// Go Outside question's). True when it moved.
bool MoveCursor(std::uint16_t pressed) {
    if ((pressed & (kInputUp | kInputDown | kInputLeft | kInputRight)) == 0) {
        return false;
    }
    g_prompt.cursor = 1 - g_prompt.cursor;
    SndSePlay(MENU_SOUND_CURSOR, -1, 0);
    return true;
}

void StepPrompt() {
    g_player_chara = UserStatus->cur_chara;

    if (g_icon_due != 0 && BtEventMode == 0 && gameTask == GAME_TASK_PLAY) { // coop-floorscene: ChangePlayer's, after the scene
        g_icon_due = 0;
        LoadActiveItemIcon();
        std::fprintf(stderr, "coop: the item sheet reloaded after the scene\n");
    }

}

void StepSwapQuestion() {
    // (in play only, as the partner's step ran it: a scene or a floor change waits)
    if (!g_prompt.active || UserStatus == nullptr || gameTask != GAME_TASK_PLAY) {
        return;
    }

    int        target = 1 - g_prompt.asker;
    SoundOwner sound(target + 1); // coop-menuse: the answer's sound is the answering player's

    // The revive question steps where the down does (ReviveAsk).
    if (g_prompt.revive) {
        LockPlayer(target == 0);
        return;
    }

    // The member asked for is no longer the other's (a swap, a floor, a knockout): nothing to ask.
    int theirs = target == 0 ? UserStatus->cur_chara : g_partner.loaded_chara;

    if (theirs != g_prompt.wanted || g_partner.down > 0) {
        std::printf("coop: swap question dropped\n");
        Close();
        return;
    }

    // coop-townwheel: a player's own menu keeps their buttons (the asker's still closing after its Allies pick, or the one asked
    // in theirs), and neither the popup nor the box draws over it (DrawSwapQuestion): the one asked answers, and the five
    // seconds run, once their menu is shut; the asker's Circle cancels once theirs is. DC_COOP_MUTATE=swap_over_menu (twin):
    // answered from inside the menu, as before.
    const bool asked_menu = FullMenuOpenFor(target + 1) && !Mutated("swap_over_menu");
    const bool asker_menu = FullMenuOpenFor(g_prompt.asker + 1) && !Mutated("swap_over_menu");

    // (coop-townwheel: player 1 waiting on his own question is held too: his Circle is its cancel)
    LockPlayer((target == 0 && !asked_menu) || (g_prompt.asker == 0 && !asker_menu));

    // coop-townwheel (Jep's rule for questions between players, 2026-11-02): the asker waits in its box and its Circle withdraws
    // the question. DC_COOP_MUTATE=swap_nocancel (twin): the asker's Circle does nothing, as before.
    std::uint16_t asker_buttons = Pad(g_prompt.asker).buttons;
    std::uint16_t asker_pressed = asker_menu ? 0 : asker_buttons & ~g_prompt.asker_held;

    g_prompt.asker_held = asker_buttons;
    if ((asker_pressed & kInputCircle) != 0 && !Mutated("swap_nocancel")) {
        SoundOwner asker_sound(g_prompt.asker + 1);

        SndSePlay(MENU_SOUND_REFUSE, -1, 0);
        std::printf("coop: %dP withdraws the swap question\n", g_prompt.asker + 1);
        std::fprintf(stderr, "coop: swap question withdrawn by player %d at link tick %lld\n", g_prompt.asker + 1, (long long) NetplayLinkTick());
        Close();
        return;
    }
    if (asked_menu) {
        g_prompt.held = Pad(target).buttons; // (a button down as the menu shuts is no answer)
        return;
    }

    std::uint16_t buttons = Pad(target).buttons;
    std::uint16_t pressed = buttons & ~g_prompt.held;

    g_prompt.held = buttons;
    MoveCursor(pressed);

    // Cross takes the hand's answer, Circle is no. (coop-townwheel: the old swapdecline mutation went, no row turned it on; the
    // NO answer's own row is t-triangle-menu-p1alliesno.)
    bool yes = (pressed & kInputCross) != 0 && g_prompt.cursor == 0;

    if (yes) {
        SndSePlay(MENU_SOUND_CONFIRM, -1, 0);
        Close();
        Trade();
        return;
    }

    if ((pressed & (kInputCircle | kInputCross)) != 0 || --g_prompt.ticks <= 0) {
        SndSePlay(MENU_SOUND_REFUSE, -1, 0);
        std::printf("coop: swap declined\n");
        std::fprintf(stderr, "coop: swap declined by player %d (%s) at link tick %lld\n", target + 1,
                     g_prompt.ticks <= 0 ? "no answer" : (pressed & kInputCircle) != 0 ? "Circle" : "NO", (long long) NetplayLinkTick());
        Close();
    }
}

bool MenuCharaAsks(int chara) {
    // (her menu's view has her member in place, so her picks never name it)
    if (!Enabled() || UserStatus == nullptr || g_partner.loaded_chara < 0 || chara != g_partner.loaded_chara || Mutated("menu_takes_partner")) {
        return false;
    }
    std::fprintf(stderr, "coop: the menu's Cross on the other player's member %d leaves the player's member %d in place\n", chara,
                 (int) UserStatus->cur_chara);
    return true;
}

bool PromptHolds(int port) {
    return g_prompt.active && !g_prompt.revive && (port == 0 || port == 1) && !Mutated("swap_no_hold");
}

bool PromptQuestion(PromptView *view) {
    if (!g_prompt.active) {
        return false;
    }
    view->asked = 2 - g_prompt.asker;
    view->asker = g_prompt.asker + 1;
    view->revive = g_prompt.revive;
    view->wanted = g_prompt.wanted;
    view->own = g_prompt.asker == 0 ? UserStatus->cur_chara : g_partner.loaded_chara;
    view->spot = g_prompt.spot;
    view->cursor = g_prompt.cursor;
    view->ticks = g_prompt.ticks;
    view->carried = g_prompt.revive ? RevivePowders(view->asked) : 0;
    return true;
}

int MenuCharaChange(int wanted) {
    int player = g_player_chara;

    if (!Enabled() || g_partner.loaded_chara < 0 || player < 0) {
        return wanted;
    }

    // coop-localmenu 3c: her local menu's pick, or his of her member, goes to every copy as a command (FullMenuLocalPick).
    if (FullMenuLocalPick(2, wanted) || (wanted == g_partner.loaded_chara && FullMenuLocalPick(1, wanted))) {
        UserStatus->cur_chara = (s8) player;
        return -1;
    }

    // The menus player 2 holds change the partner, never the player.
    if (NetplayMenuOwner() == 2 && !Mutated("menuowner")) {
        UserStatus->cur_chara = (s8) player;

        if (wanted == player) {
            AskSwap(1, wanted);
        } else if (wanted != g_partner.loaded_chara && wanted >= 0 && wanted < UserStatus->party_size) {
            g_partner.choice = wanted;
            std::printf("coop: 2P's menu picks %d for the partner\n", wanted);
        }

        return -1;
    }

    if (wanted == g_partner.loaded_chara && !Mutated("samechara")) {
        UserStatus->cur_chara = (s8) player;
        AskSwap(0, wanted);
        std::printf("coop: 1P's menu asks for the partner's %d; the player stays %d\n", wanted, player);
        return -1;
    }

    return wanted;
}

bool RevivePowderOn() {
    static const bool launcher = TestEnvOn("DC_COOP_REVIVE_POWDER");

    return Enabled() && g_revive_host.value_or(launcher);
}

void RevivePowderAdopt(bool on) {
    g_revive_host = on;
}

bool ReviveAsk(int reviver, bool beside) {
    bool mine = g_prompt.active && g_prompt.revive && 1 - g_prompt.asker == reviver;

    if (!beside) {
        g_revive_declined[reviver] = false;

        if (mine) {
            std::printf("coop: revive question dropped\n");
            Close();
        }

        return false;
    }

    if (!mine) {
        int carried = RevivePowders(reviver + 1);

        if (g_prompt.active || g_revive_declined[reviver] || carried <= 0) {
            return false;
        }

        g_prompt.active = true;
        g_prompt.revive = true;
        g_prompt.asker = 1 - reviver;
        g_prompt.wanted = -1;
        g_prompt.spot = -1;
        g_prompt.ticks = kAnswerTicks;
        g_prompt.held = Pad(reviver).buttons;
        g_prompt.cursor = 0;
        LockPlayer(reviver == 0);
        SndSePlay(MENU_SOUND_CURSOR, -1, 0);
        std::printf("coop: %dP asked: Use Revive Powder? (%d carried)\n", reviver + 1, carried);
        return false;
    }

    std::uint16_t buttons = Pad(reviver).buttons;

    // Test hook (DC_COOP_REVIVETEST, coop_party.cpp): the reviver answers a second after it is asked, Circle for the
    // decline case, else Cross. Ticks of the question, so linked copies answer alike.
    static const char *revive_test = TestEnv("DC_COOP_REVIVETEST");

    if (revive_test != nullptr && g_prompt.ticks == kAnswerTicks - 60) {
        buttons |= std::strcmp(revive_test, "decline") == 0 ? kInputCircle : kInputCross;
        std::printf("coop-debug: revivetest: %dP answers %s\n", reviver + 1, std::strcmp(revive_test, "decline") == 0 ? "Circle" : "Cross");
    }

    std::uint16_t pressed = buttons & ~g_prompt.held;

    g_prompt.held = buttons;
    MoveCursor(pressed);

    if ((pressed & kInputCross) != 0 && g_prompt.cursor == 0) {
        Close();

        // DC_COOP_MUTATE=revivepowder: yes spends nothing and gets nobody up (the twin).
        if (Mutated("revivepowder") || !SpendRevivePowder(reviver + 1)) {
            SndSePlay(MENU_SOUND_REFUSE, -1, 0);
            g_revive_declined[reviver] = true;
            std::printf("coop: %dP's Revive Powder not used\n", reviver + 1);
            return false;
        }

        SndSePlay(MENU_SOUND_CONFIRM, -1, 0);
        std::printf("coop: %dP uses a Revive Powder\n", reviver + 1);
        return true;
    }

    if ((pressed & (kInputCircle | kInputCross)) != 0 || --g_prompt.ticks <= 0) {
        SndSePlay(MENU_SOUND_REFUSE, -1, 0);
        std::printf("coop: %dP declines the Revive Powder\n", reviver + 1);
        Close();
        g_revive_declined[reviver] = true;
    }

    return false;
}

void PromptResetForSession() {
    Close();
    g_prompt = Prompt{};
    g_player_chara = -1;
    g_revive_declined[0] = g_revive_declined[1] = false;
}

} // namespace coop

using namespace coop;

// coop-townwheel (Jep, 2026-11-02: "the white text prompts that we're trying to eliminate"): the asked player's copy shows the
// question in the game's Yes/No popup, the asker's its waiting box (coop_useprompt.cpp DrawSwapQuestion).
void CoopDrawPrompt() {
    DrawSwapQuestion();
}
