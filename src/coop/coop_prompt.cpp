#include "coop/game_ns.hpp"
#include "coop/coop.hpp"
#include "coop/coop_audio.hpp" // coop-menuse
#include "coop/coop_prompt.hpp"
#include "coop/netplay.hpp"
#include "coop/netplay_cmd.hpp" // coop-wave2: kCmdQuestion

#include <libvu0.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
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
#include "platform/config.hpp" // trade3: game.coop_swap_same_zone
#include "snd.hpp"
#include "savedata.hpp" // coop-townally: the party's status in a town (SaveData)
#include "userstatus.hpp"

#include "coop/coop_actor.hpp"  // coop-swapcombo: kMaxPlayers
#include "coop/coop_combat.hpp" // coop-swapcombo: g_combat (her charge and throw)
#include "coop/coop_core.hpp"
#include "coop/coop_fullmenu.hpp" // coop-localmenu: FullMenuLocalPick
#include "coop/coop_inventory.hpp"
#include "coop/coop_interact.hpp" // coop-revivecross: ReviveOfferTrace
#include "coop/coop_party.hpp"
#include "coop/coop_town.hpp"     // coop-swapcombo: TownPlayerMember (her member apart)
#include "coop/coop_townmenu.hpp" // coop-townally: CoopTownMenuStepPlayer
#include "coop/coop_worlds.hpp"   // coop-swapcombo: CoopWorldsApart, the trade across zones' records
#include "coop/coop_zonechara.hpp" // coop-pt1111k

namespace dc::game {
extern s32 gameTask;
extern s16 charachangeid; // game/menu_misc.cpp: the member the Characters page loads next (its voice, CharaChangeInitToGL2)
extern s32 mode;          // coop-swapcombo: the game's mode (3 a dungeon)
} // namespace dc::game

namespace coop {

// coop-floorscene: ChangePlayer's item-sheet reload waits for the floor's scene to end (a tick of play, StepPrompt). In the
// state set (qa\netplay\snapshot-allow.txt), so a copy that took the state mid-scene reloads on the same tick.
int g_icon_due = 0;

namespace {

const char *const kNames[6] = {"Toan", "Xiao", "Goro", "Ruby", "Ungaga", "Osmond"};

// coop-townally: the trade question in a town (GAME_MODE_EDIT, linked: the players together there). Its members are the members'
// own word (Members), not the game's player (Toan's town body) nor the partner body.
bool InTownQuestion() {
    return mode == 2 && !CoopWorldsApart(); // (mode 2: GAME_MODE_EDIT, main.hpp)
}

// Its step waits while the town is not in its walk (a talk, an event, a shop, the Georama: coop_town's CoopTownQuestionFree): the
// question waits there, never dropped (the askwait rule: no auto-reject for a busy player). DC_COOP_MUTATE=town_question_off (twin):
// never stepped in a town, as before.
bool TownQuestionInPlay() {
    return !Mutated("town_question_off") && CoopTownQuestionFree();
}

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
    // coop-swapcombo part 3: the question is up (drawn, holding both players, its five seconds running) once both players'
    // actions are over; until then it waits unseen, and the ticks it waited.
    bool          shown = false;
    int           waited = 0;
    // coop-wave2 (talks, questions and prompts local): which question this is (the link tick it was asked on, every copy alike), and
    // the answer its command brought (QuestionAnswer), taken at the question's step on every copy alike; 0 none.
    long long     id = -1;
    int           answer_due = 0;
};

Prompt g_prompt;

// coop-wave2 (localaudit 3.5, Jep-approved Wave 2): linked together, the swap question's hand (YES/NO) and the buttons that answer or
// withdraw it are this copy's own: the asked player's copy moves its hand and takes its answer from that player's agreed pad, the
// asker's copy takes the asker's Circle, and the answer or the withdrawal goes out as one command (kCmdQuestion), which every copy
// takes at the question's step on its tick (Trade or Close, as before). Before, every copy stepped the hand and the buttons from both
// pads (shared state the asked player's pad wrote). What holds the players and the world (LockPlayer, the waits for an action or a
// menu) stays every copy's, read from the shared rows. The revive question and a catch-up's replay as before.
// DC_COOP_MUTATE=question_lockstep (twin): the hand and the answer stepped on every copy, as before.
constexpr int kAnswerYes = 1, kAnswerCircle = 2, kAnswerNo = 3, kAnswerWithdraw = 4;
struct QuestionOwn {
    long long     question = -1;
    int           cursor = 0;
    std::uint16_t held = 0;
    std::uint16_t asker_held = 0;
    bool          answered = false; // this copy's player answered or withdrew: its command is on its way
};
QuestionOwn g_question_own; // this copy's own, never state

// (coop-wave2: the revive question too, "Use Revive Powder?": the reviver's hand and answer on the reviver's copy, the same command)
bool QuestionLocal() {
    return NetplayHasPlayer() && NetplayPartnerLinked() && !CoopWorldsApart() && !NetplayReplaying() && !Mutated("question_lockstep");
}

void QuestionHandlerOnce();

QuestionOwn &OwnQuestion() {
    if (g_question_own.question != g_prompt.id) {
        g_question_own = QuestionOwn{};
        g_question_own.question = g_prompt.id;
        g_question_own.held = g_prompt.held;
        g_question_own.asker_held = g_prompt.asker_held;
    }
    return g_question_own;
}

void QuestionAnswer(int answer) {
    NetCommand command;

    command.kind = kCmdQuestion;
    command.a = (std::uint8_t) answer;
    command.c = (std::uint32_t) g_prompt.id;
    NetplayQueueCommand(command);
    g_question_own.answered = true;
    std::fprintf(stderr, "coop: %s question: player %d's %s on its own copy at link tick %lld: sent\n", g_prompt.revive ? "revive" : "swap", NetplayLocalPlayer(),
                 answer == kAnswerYes ? "YES" : answer == kAnswerWithdraw ? "withdrawal" : "NO", (long long) NetplayLinkTick());
}

void ApplyQuestion(int player, const NetCommand &command, std::int64_t tick) {
    if (!g_prompt.active || (g_prompt.revive && command.a == kAnswerWithdraw) || (long long) command.c != (g_prompt.id & 0xFFFFFFFFll) ||
        player != (command.a == kAnswerWithdraw ? g_prompt.asker + 1 : 2 - g_prompt.asker)) {
        std::fprintf(stderr, "coop: swap question: player %d's answer %d at link tick %lld finds no question of theirs: ignored\n", player, (int) command.a,
                     (long long) tick);
        return;
    }
    g_prompt.answer_due = command.a;
}

void QuestionHandlerOnce() {
    static bool handler = false;

    if (!handler) {
        handler = true;
        NetplaySetCommandHandler(kCmdQuestion, ApplyQuestion);
    }
}

// coop-revive: the host's switch, once the link has adopted it; until then this copy's own.
std::optional<bool> g_revive_host;
// coop-revive2: the test hook's Cross per player (ReviveTestCross), and the copy's own Cross edges while unlinked (one copy:
// solo, couch); the no-powder box's frames per player (presentation only, never read by the step).
bool          g_revive_test_cross[kMaxPlayers] = {};
std::uint16_t g_revive_held[kMaxPlayers] = {};
int           g_revive_none[kMaxPlayers] = {};

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

// trade3: whether a player is in an action (his: the game's BtActStatus; hers: her swing, charge or throw), and whether the swap
// question therefore leaves them unheld (see StepPrompt). Reads the simulation only, every copy alike.
bool SwapInAction(int port) {
    return port == 0 ? BtActStatus.action_on != 0 : g_partner.attacking || g_combat.charging || g_combat.throw_stage != 0;
}

bool SwapHeldOff(int port) {
    return SwapInAction(port) && !Mutated("swap_ask_midaction");
}

void Close() {
    LockPlayer(false);
    g_prompt.active = false;
    g_prompt.revive = false;
    g_prompt.wanted = -1;
    g_prompt.answer_due = 0; // coop-wave2
}

// coop-crashswapq (soak seed 51117193: both copies crashed in DrawSwapQuestion's snprintf as the copies split coming out of a
// house): the swap question between the players (g_prompt) is the linked pair's, read from the shared world (their members, the
// partner body, the town's Members). Player 2 asked for Toan, player 1's menu held the question up unanswered, and she walked out
// of the room meanwhile: the split left the question active on both copies. Apart, InTownQuestion is false, so the question read
// the floor's partner body for the asker's member: g_partner.loaded_chara, -1 apart, and the popup's name table took index -1
// (a wild pointer). Its step held player 1's pad (LockPlayer) on his copy alone. A trade apart is the trade across zones' (g_apart),
// never this one: on the first step after a split, on every copy alike, the question ends (as the asker's withdrawal does: no
// trade, the pad given back); the asker may ask again apart. DC_COOP_MUTATE=swap_survives_split (twin): it lives on, as before.
bool EndQuestionApart(const char *where) {
    if (!g_prompt.active || !CoopWorldsApart() || Mutated("swap_survives_split")) {
        return false;
    }
    std::fprintf(stderr, "coop: swap question ended: the copies went apart (player %d asked player %d for %d; %s, link tick %lld; the partner body's member here %d)\n",
                 g_prompt.asker + 1, 2 - g_prompt.asker, g_prompt.wanted, where, (long long) NetplayLinkTick(), g_partner.loaded_chara);
    Close();
    g_prompt = Prompt{};
    return true;
}

} // namespace
int  GamePlayerIndex();
namespace {

// The player becomes chara, as the party menus' change does it (CharaChangeInitToGL's files, then
// BtMenuLoadChara and the quick change's end), loaded at once from the disc into buffers of its own.
bool ChangePlayer(int chara, MemberWhy why = MemberWhy::kChange) {
    // coop-memberowner: a member another player holds is not taken by a plain change (asked for as a trade instead: AskSwap);
    // refused here before anything loads. A role swap changes no member, so it is not asked.
    if (!CoopWorldsRolesSwapping() && !MemberFree(GamePlayerIndex(), chara, why)) {
        MemberRefused(GamePlayerIndex(), chara, why);
        return false;
    }
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
    if (!CoopWorldsRolesSwapping()) { // coop-rc2 (swapcombo (A) x swapduel, b9arrive): a role swap is not anyone's member
        SetMember(GamePlayerIndex(), chara, why); // coop-swapcombo (A)
    }
    return true;
}

} // namespace

// coop-swapcombo (A) (Jep's rule, relayed 11-13: a trade survives splits, merges and town visits). Retail makes the game's player
// Toan whenever it leaves a dungeon (dun/gameloop.cpp GameLoop, existFlag: UserStatus->cur_chara = CHARA_TOAN), in a town's init
// (editloop.cpp: SaveData->GetDngStatus()->cur_chara = 0) and in a dungeon's load (dun/gameloop.cpp LoadData: cur_chara = 0): in
// retail Toan is the one member outside a floor. In co-op a trade (or player 1's own pick) gives player 1 another member and player
// 2 maybe Toan; retail's reset then left them both Toan, and the next floor's partner pick (PickPartner) moved her off Toan: the
// trade undone. Each player's member is kept here (per player, the state set's, every copy alike: kMaxPlayers, 3+ ready), written
// where a member really changes (ChangePlayer for the game's player, his Characters page, a trade, a trade across zones), and at
// each floor's first tick of play (the floor's opening scene played by retail's Toan, as retail's) player 1 becomes his kept
// member again; a town stays retail's, Toan's model alone. Player 2's is her pick (g_partner.choice), which her copy apart already
// plays on each floor (coop_worlds' become). DC_COOP_MUTATE=member_reset_retail (twin): no restore, retail's reset stands.
// coop-memberowner: the members themselves are coop_members.cpp's (g_member_of, written by SetMember alone).
bool g_member_restore_due = false;

// The game's player on this copy is which player's: together player 1's on every copy; apart this copy's own.
int GamePlayerIndex() {
    // coop-mergeload (DCO-157): a copy catching up replays the other's ticks on the other's state (from the state
    // set's apply, or tick 0 with none), whose game's player is the other's. The copies still count as apart there, so her copy
    // took her own index: his switch of character in her catch-up was written as hers on her copy alone (player 2's member Goro,
    // player 1's still Toan) and they met OUT OF SYNC. DC_COOP_MUTATE=change_replay_self (twin): this copy's own, as before.
    if (CoopWorldsApart() && NetplayReplaying() && NetplayCatchupStepsAlike() && !Mutated("change_replay_self")) {
        return CoopWorldsSelf() == 2 ? 0 : 1;
    }
    return CoopWorldsApart() && CoopWorldsSelf() == 2 ? 1 : 0;
}

bool MemberRestorePending() {
    // (not only once the floor started: the dungeon's entrance loads the partner first, retail's reset already made)
    return mode == 3 && !Mutated("member_reset_retail") && UserStatus != nullptr && GamePlayerIndex() == 0 && g_member_of[0] >= 0 &&
           g_member_of[0] != UserStatus->cur_chara;
}

void MemberFloorStarted() {
    g_member_restore_due = true;
}

// Each sim tick (coop_worlds' CoopWorldsSimTick, every copy on one tick together): at the floor's first tick of play, player 1's
// kept member back as the game's player (as his Characters page changes it: ChangePlayer).
void MemberRestoreTick() {
    if (!g_member_restore_due || UserStatus == nullptr || !CoopPlainPlay() || BtEventMode != 0) {
        return;
    }
    g_member_restore_due = false;

    const int kept = g_member_of[0];

    if (GamePlayerIndex() != 0 || kept < 0 || kept == UserStatus->cur_chara) {
        return;
    }
    if (Mutated("member_reset_retail")) {
        std::fprintf(stderr, "coop: player 1 stays %d on this floor (retail's start), his kept member %d not restored (member_reset_retail)\n",
                     (int) UserStatus->cur_chara, kept);
        return;
    }
    const int slot = UserStatus->equipped_weapon_slot[kept];

    if (kept >= UserStatus->party_size || slot < 0 || slot >= 11 || UserStatus->hp[kept] <= 0 || (g_partner.loaded_chara >= 0 && g_partner.choice == kept)) {
        std::fprintf(stderr, "coop: player 1's kept member %d cannot play this floor (party %d, slot %d, life %d, her pick %d): he stays %d\n", kept,
                     (int) UserStatus->party_size, slot, (int) UserStatus->hp[kept], g_partner.choice, (int) UserStatus->cur_chara);
        return;
    }
    const int was = UserStatus->cur_chara;

    if (ChangePlayer(kept)) {
        std::fprintf(stderr, "coop: player 1 plays his member %d (%s) again on this floor (retail's start made him %d)\n", kept, kNames[kept], was);
    }
}

namespace {

int TownAlliesPlays(int player); // (below: the member a player plays in town, a kept none resolved)

// coop-deadswap (the developer's rc4 playtest 11-18: "P2 can ask to swap with P1 while P1 is dead, even though Toan is greyed out in the character
// select wheel, and if they accept, both characters just die and it kicks them out of the dungeon"). Retail's Characters page and wheel
// grey a member with no life and refuse its pick with the refuse sound and nothing else (menu_dungeon.cpp: hp <= 0 or knocked out).
// A trade puts each player on the other's member, so the asker's member and the one asked for both take that rule: before, a member
// another player lay down on was asked for, and the yes put the living player on it; the game then saw its player with no life while
// the other lay down, both down, its own death and out of the dungeon. A player lying down (player 1's co-op down, the partner's fall)
// or any member at no life is "no life", on a floor (a town has no downs). The one test of every ask (AskSwap: the wheels, the Allies
// page, the D-pad ring, the Characters page, a character spot), of an open question each step and of the trade itself.
// DC_COOP_MUTATE=deadswap (twin): no such rule, as before (only the partner's fall dropped an open question).
bool ActorDownForTrade(int index) {
    if (index == 0) {
        return g_combat.player_down > 0 || UserStatus->hp[UserStatus->cur_chara] <= 0;
    }
    return index == 1 && (g_partner.down > 0 || Life() <= 0); // (the one partner body today; more players add theirs here)
}

bool MemberWithoutLife(int member) {
    if (Mutated("deadswap") || UserStatus == nullptr || InTownQuestion()) {
        return false;
    }
    if (member < 0 || member > 5) {
        return true;
    }
    for (int index = 0; index < kMaxPlayers; index++) {
        if (ActorPresent(index) && ActorChara(index) == member && ActorDownForTrade(index)) {
            return true;
        }
    }
    return OwnLife(member) && UserStatus->hp[member] <= 0;
}

// The member player index plays on this floor together (the asker's offer, the asked one's).
// (-1 with no floor status: in a town the game has none, UserStatus null; MemberWithoutLife is false there)
int TradeMemberOf(int index) {
    if (UserStatus == nullptr) {
        return -1;
    }
    return index == 0 ? (int) UserStatus->cur_chara : index == 1 ? g_partner.loaded_chara : ActorChara(index);
}

// Yes: the two trade members. The partner's new member loads on the next tick (PickPartner).
void Trade() {
    // coop-townally: in a town the two members change hands and nothing else: Toan stays the town's player (his walk, talks, shops,
    // events, Georama), player 1's body is drawn as his new member (coop-p1body) and her town walker loads hers (TownMember).
    // (no twin of its own: the floor's trade, ChangePlayer, in a town crashes the copy; the ~question and ~raw twins guard this trade)
    if (InTownQuestion()) {
        const int his = TownAlliesPlays(0), hers = TownAlliesPlays(1);

        if (his < 0 || hers < 0 || his == hers) {
            std::fprintf(stderr, "coop: town trade dropped: members %d and %d\n", his, hers);
            return;
        }
        SetMember(0, hers, MemberWhy::kTrade);
        SetPick(1, his, MemberWhy::kTrade);
        std::fprintf(stderr, "coop-allies: town trade: player 1 plays %d (%s), player 2 plays %d (%s) at link tick %lld\n", Member(0), kNames[Member(0)], Member(1),
                     kNames[Member(1)], (long long) NetplayLinkTick());
        return;
    }
    int player = UserStatus->cur_chara;
    int partner = g_partner.loaded_chara;

    // coop-deadswap: never a player put onto a member with no life (the question's step drops it first; this is the last word).
    if (MemberWithoutLife(player) || MemberWithoutLife(partner)) {
        std::fprintf(stderr, "coop: swap not applied: a member without life (player 1's %d %s, player 2's %d %s) at link tick %lld\n", player,
                     MemberWithoutLife(player) ? "no life" : "up", partner, MemberWithoutLife(partner) ? "no life" : "up", (long long) NetplayLinkTick());
        return;
    }
    std::printf("coop: swap accepted, player %d and partner %d trade\n", player, partner);

    if (ChangePlayer(partner, MemberWhy::kTrade)) {
        SetPick(1, player, MemberWhy::kTrade); // coop-swapcombo (A)
        // DC_COOP_MUTATE=member_dup (t-memberowner's twin: the invariant broken on purpose): her member written as his.
        if (Mutated("member_dup")) {
            MemberBreakForTest(1, partner);
        }
    }
}

} // namespace

// coop-worlds: the player becomes chara (player 2's copy staying on a floor alone). Only a member with a
// weapon of its own equipped (a stand-in that never joined has none, and its weapon frame would be null).
bool PromptChangePlayer(int chara, MemberWhy why) {
    int slot = chara >= 0 && chara < 6 ? UserStatus->equipped_weapon_slot[chara] : -1;

    if (slot < 0 || slot >= 11 || UserStatus->chara_weapons[chara][slot].item_no <= 0) {
        std::printf("coop: member %d has no weapon equipped; the player stays %d\n", chara, (int) UserStatus->cur_chara);
        return false;
    }

    return ChangePlayer(chara, why);
}

bool PromptTradeForTest() {
    const int partner = g_partner.loaded_chara;

    Trade();
    return UserStatus->cur_chara == partner;
}

bool PromptAsksPartner() {
    return g_prompt.active && g_prompt.asker == 0;
}

bool PromptSwapOpen() {
    return g_prompt.active && !g_prompt.revive;
}

bool PromptQuestionOf(int player) {
    const int port = player - 1;

    return g_prompt.active && (port == g_prompt.asker || port == 1 - g_prompt.asker); // (the one asked: StepSwapQuestion's target)
}

// coop-swapcombo part 2 (Jep's rc1 playtest 2026-11-12, and his rule: players must be able to trade between zones, for the
// character gates such as Xiao's gaps). Apart, each copy is its own world, so a trade is a question between the copies, not a
// lockstep one: the asker's copy sends it (a worlds progress record, kWhatTrade, resent until acknowledged, both ways), the asked
// player sees the same popup as together (DrawSwapQuestion) on their own copy in their own zone, once in play between actions, and
// answers with their own pad; nothing changes until their yes. On it the asked copy applies its side at once and answers; the
// asker's copy applies its side when the yes arrives, whatever it did meanwhile (a yes that crosses the asker's withdrawal still
// trades on both, so the two copies always agree). No, no answer (five seconds once up) or a withdrawal changes nothing anywhere.
// The asker keeps playing meanwhile, with the waiting box (its Circle withdraws); the asked player is held only while the popup is
// up, as in their own menu. A merge waits until neither copy has a record unacknowledged, so both sides are in before the copies
// meet, and each copy's state names the new members (player 1's copy: his member the game's player, hers the partner's pick;
// player 2's copy: hers the pick, played on a floor; his, in a town, the game's player she stands in for).
// The other player's member is the members' own word (coop_members: alike while linked; apart, a trade or an ask brings it), or
// the other's presence on a floor where heard (coop-memberowner: no stored copy of its own).
// DC_COOP_MUTATE=apart_trade_refused (the rows' twin): the 15450f0c stopgap, the ask refused with a notice.
namespace {

constexpr int kDungeonMode = 3; // GAME_MODE_DUNGEON (main.hpp)

struct ApartTrade {
    int           role = 0;    // 0 none, 1 this copy's player asked and waits, 2 this copy's player is asked
    int           other = 0;   // the other player's number
    int           mine = -1;   // this copy's player's member at the ask
    int           theirs = -1; // the other player's member at the ask
    int           ticks = 0;
    int           waited = 0; // coop-askwait: the asked copy's ticks its question waited for its player (busy), all told
    int           up = 0;     // (and its ticks up since it last came up)
    bool          shown = false;
    int           cursor = 0;
    std::uint16_t held = 0;
    std::uint16_t asker_held = 0;
};

ApartTrade g_apart;
int        g_apart_due = -1;   // the member this copy's player becomes at its next tick of play between actions (on a floor)
bool       g_apart_traded = false; // (the tests' lines: a trade across zones applied here)
bool       g_apart_changed = false; // (coop-memberstage4 test hook DC_COOP_TEST_APART_CHANGE: this copy's player changed member apart)

}  // namespace

int MemberBodyExpected(const char **scope) {
    *scope = nullptr;
    const int player = GamePlayer();
    const int kept = Member(player);

    if (UserStatus == nullptr || mode != kDungeonMode || !CoopPlainPlay() || player < 0 || kept < 0) {
        return -1;
    }
    if (CoopWorldsRolesSwapping()) {
        *scope = "role swap";
    } else if (g_scene_handover.due != 0) {
        *scope = "scene hand-over";
    } else if (g_member_restore_due || MemberRestorePending()) {
        *scope = "restore due";
    } else if (g_apart_due >= 0) {
        *scope = "trade change due";
    } else if (BtEventMode != 0) {
        *scope = "scene";
    }
    return kept;
}

namespace {

int ApartSelf() {
    return CoopWorldsSelf();
}

// coop-townally (the 11-16 apart row: his town ask across zones reached her copy in town and was dropped unsaid): the party's status
// for a trade across zones. The game sets UserStatus only on a dungeon's start (GameInit: the save's dungeon status), so a session
// that has played no floor yet (a town save's load, a fixture) has none in town, and the asked copy's ask, question and apply all
// returned at once. In town after a floor UserStatus is that same save status, so this is the one object either way.
// DC_COOP_MUTATE=apart_trade_status_game (twin): the game's alone, as before.
CUserStatus *TradeStatus() {
    if (UserStatus != nullptr || Mutated("apart_trade_status_game")) {
        return UserStatus;
    }
    return SaveData != nullptr ? (CUserStatus *) SaveData->GetDngStatus() : nullptr;
}

// This copy's player's member, and the other's as last known.
int ApartOwnMember() {
    // (coop-townally: player 1 in a town plays his member, the members' word; the game's player there is Toan's town body)
    if (ApartSelf() == 1 && mode != kDungeonMode && Member(0) >= 0 && !Mutated("town_apart_own_status")) {
        return Member(0);
    }
    return ApartSelf() == 1 ? (int) TradeStatus()->cur_chara : TownPlayerMember();
}

// coop-memberowner stage 3: the other player's member is the members' word (Member: kept alike while linked, a trade across zones
// writes both, the asker's word in its ask), not a copy of its own; on player 2's copy the other's floor presence, when heard, says
// the body he plays there now. DC_COOP_MUTATE=apart_other_unknown (twin): no word, every ask across zones refused.
int ApartOtherMember() {
    if (Mutated("apart_other_unknown")) {
        return -1;
    }
    // (coop-memberstage4: the other's presence writes its member into Members, MembersHeard: no read of the heard body here)
    return Member(2 - ApartSelf());
}

void ApartClose() {
    if (g_apart.role == 2 && g_apart.shown) {
        LockPlayer(false);
    }
    g_apart = ApartTrade{};
}

// This copy's side of a done trade: its player becomes own, the other other.
void ApartApply(int own, int other) {
    // (the model changes at once only on a floor in play; anywhere else, a floor being left among them, the member is set now, so
    // the state a merge sends right after already has it)
    const bool on_floor = mode == kDungeonMode && CoopPlainPlay();

    // (coop-memberstage4: her member before the trade, read before its writes: whether her copy's game player is her body on the floor)
    const int member_before = TownPlayerMember();

    // coop-memberowner: both players' members first (a trade: each takes the other's), then this copy's bodies.
    SetMember(ApartSelf() - 1, own, MemberWhy::kApartTrade);
    if (ApartSelf() == 1) {
        SetPick(1, other, MemberWhy::kApartTrade);
        if (on_floor) {
            g_apart_due = own;
        } else {
            TradeStatus()->cur_chara = (s8) own;
        }
    } else {
        const bool played = on_floor && TradeStatus()->cur_chara == member_before;

        SetPick(1, own, MemberWhy::kApartTrade);
        SetMember(0, other, MemberWhy::kApartTrade);
        if (played) {
            g_apart_due = own;
        } else {
            TradeStatus()->cur_chara = (s8) other; // (her copy in a town: the game's player stands in for his member)
        }
    }
    g_apart_traded = true;
    std::fprintf(stderr, "coop: apart trade applied on this copy (player %d): it plays %d (%s), player %d plays %d (%s)%s\n", ApartSelf(), own,
                 kNames[own], 3 - ApartSelf(), other, kNames[other], g_apart_due >= 0 ? "; its change at its next tick of play" : "");
    char line[96];

    std::snprintf(line, sizeof line, "Traded: you are now %s", kNames[own]);
    NetplayShowNotice(line);
}

// The asker's side: this copy's player asks the other, apart, for wanted.
void ApartAsk(int wanted) {
    const int mine = ApartOwnMember();
    const int theirs = ApartOtherMember();

    // (coop-deadswap: on a floor, this copy's player offers no member with no life; the asked copy checks its own, PromptApartTradeHeard)
    const bool no_life = mine >= 0 && mode == kDungeonMode && TradeStatus()->hp[mine] <= 0 && !Mutated("deadswap");

    if (g_apart.role != 0 || theirs < 0 || wanted != theirs || mine == wanted || mine < 0 || no_life) {
        {
            SoundOwner sound(ApartSelf()); // coop-rc2 (router lint): apart, a sound of this copy's own player's trade box
            SndSePlay(MENU_SOUND_REFUSE, -1, 0);
        }
        std::fprintf(stderr, "coop: apart trade: player %d's ask for %d refused here (open %d, mine %d, theirs %d)\n", ApartSelf(), wanted, g_apart.role,
                     mine, theirs);
        return;
    }
    g_apart = ApartTrade{};
    g_apart.role = 1;
    g_apart.other = 3 - ApartSelf();
    g_apart.mine = mine;
    g_apart.theirs = theirs;
    g_apart.asker_held = CoopWorldsCallPadButtons();
    CoopWorldsTradeSend(0, mine, wanted, 0);
    {
        SoundOwner sound(ApartSelf()); // coop-rc2 (router lint): apart, a sound of this copy's own player's trade box
        SndSePlay(MENU_SOUND_CURSOR, -1, 0);
    }
    std::fprintf(stderr, "coop: apart trade: player %d asks player %d for %d (%s), offering %d (%s), across zones\n", ApartSelf(), g_apart.other,
                 wanted, kNames[wanted], mine, kNames[mine]);
}

void ApartAnswer(int result) {
    CoopWorldsTradeSend(1, g_apart.theirs, result, g_apart.mine);
    std::fprintf(stderr, "coop: apart trade: player %d answers player %d's ask: %s\n", ApartSelf(), g_apart.other,
                 result == 1 ? "yes" : result == 0 ? "no" : result == 3 ? "no answer" : "cannot");
}

// Each tick on every copy (coop_worlds' tick, every mode): the last-known member while linked, the change due, the asker's
// withdrawal, the asked player's popup.
void StepApartTrade() {
    EndQuestionApart("the worlds' tick"); // coop-crashswapq: every mode, every copy
    if (TradeStatus() == nullptr) {
        return;
    }
    MembersSelfTest(); // coop-memberowner: every tick of every mode, on every copy
    if (!CoopWorldsApart()) {
        if (g_apart.role != 0) {
            std::fprintf(stderr, "coop: apart trade: the copies are together again; the question across zones closes (role %d)\n", g_apart.role);
            ApartClose();
        }
    }

    if (g_apart_due >= 0 && CoopPlainPlay() && BtEventMode == 0 && BtActStatus.action_on == 0) {
        const int due = g_apart_due;

        g_apart_due = -1;
        if (mode != kDungeonMode) {
            TradeStatus()->cur_chara = (s8) due;
        } else if (TradeStatus()->cur_chara != due) {
            PromptChangePlayer(due);
        }
    }

    // Test lines and hook: once together again after a trade across zones, each copy's members (both players'); and
    // DC_COOP_TEST_APART_ESCAPE=1, this copy's player escapes the dungeon once its side is done (to the town the other went to).
    static bool said_together = false;
    static bool escaped = false;

    static int linked_ticks = 0;

    linked_ticks = g_apart_traded && !CoopWorldsApart() && NetplayLinkUp() ? linked_ticks + 1 : 0; // (only after a trade across zones: the fidelity row's globals)
    if (g_apart_traded && !said_together && linked_ticks >= 2 && g_partner.choice >= 0) {
        said_together = true;
        std::fprintf(stderr, "coop-test: apart trade: together again: player 1 plays %d, player 2 plays %d (picked %d), kept %d %d\n", (int) TradeStatus()->cur_chara,
                     g_partner.loaded_chara, g_partner.choice, g_member_of[0], g_member_of[1]);
    }
    // coop-rc3misc (rc3 landing red, PC-02 loaded: 46 s, no 'together'): the escape goes to the town the other went to, after he is
    // heard there (his town loaded, its first visit's mark sent before that word). Escaping on her own clock, a slow town load of
    // his let both copies start the town's first-visit scene apart (each sent its mark as the other's town had already begun it);
    // her catch-up then waits for her own scene to end, which waits on a Cross no test presses. DC_COOP_MUTATE=apart_escape_unordered
    // (twin): her pick at once and her escape right after it, without that word: both towns' first visits, as under that load.
    static const bool unordered = Mutated("apart_escape_unordered");

    if ((g_apart_traded || g_apart_changed) && !escaped && TestEnvOn("DC_COOP_TEST_APART_ESCAPE") && CoopWorldsApart() && g_apart_due < 0 && mode == kDungeonMode &&
        CoopPlainPlay() && BtActStatus.action_on == 0 && (unordered || CoopWorldsPeerHeardInATown())) {
        escaped = true;
        std::fprintf(stderr, "coop-test: apart trade: this copy's player escapes the dungeon (as %d)\n", (int) TradeStatus()->cur_chara);
        gameTask = GAME_TASK_ESCAPE_INIT;
        return;
    }

    if (g_apart.role == 0 || !CoopWorldsApart()) {
        return;
    }
    if (g_apart.role == 1) {
        // (the asker plays on; its Circle withdraws, as the waiting box says; the game's pad as the call's box reads it)
        // coop-askwait (Jep 11-15: "if the requester has a cancel button, there's no need to have it automatically expire"): no
        // clock: it waits till the answer or its Circle (it withdrew itself after a minute). DC_COOP_MUTATE=ask_lapse (twin): the
        // minute, as before. Test hook DC_COOP_TEST_APART_WITHDRAW=N: the asker's Circle at its Nth tick waiting.
        static const int    withdraw_at = TestEnvInt("DC_COOP_TEST_APART_WITHDRAW", 0);
        std::uint16_t       buttons = CoopWorldsCallPadButtons();

        if (withdraw_at > 0 && g_apart.ticks + 1 == withdraw_at && (g_apart.asker_held & kInputCircle) == 0) {
            buttons |= kInputCircle;
            std::fprintf(stderr, "coop-test: apart trade: the asker's Circle at its tick %d waiting\n", withdraw_at);
        }
        const std::uint16_t pressed = buttons & ~g_apart.asker_held;

        g_apart.asker_held = buttons;
        if ((pressed & kInputCircle) != 0 || (++g_apart.ticks > 60 * 60 && Mutated("ask_lapse"))) {
            {
                SoundOwner sound(ApartSelf()); // coop-rc2 (router lint): apart, a sound of this copy's own player's trade box
                SndSePlay(MENU_SOUND_REFUSE, -1, 0);
            }
            CoopWorldsTradeSend(3, g_apart.mine, g_apart.theirs, 0);
            std::fprintf(stderr, "coop: apart trade: player %d withdraws the ask (%s)\n", ApartSelf(), (pressed & kInputCircle) != 0 ? "Circle" : "a minute");
            ApartClose();
        }
        return;
    }
    if (g_apart.role != 2) {
        return;
    }
    if (!g_apart.shown) {
        // As together (part 3): up once this copy's player is between actions and out of its menu.
        // (free as the call's rule has it, in a town too; on a floor also between actions)
        if (++g_apart.ticks % 300 == 1) {
            char why[320];

            CoopWorldsCallFreeWhy(why, sizeof why);
            std::fprintf(stderr, "coop: apart trade: player %d's question waits (free %d, %s, action %d)%c", ApartSelf(), (int) CoopWorldsCallFree(), why,
                         (int) BtActStatus.action_on, 10);
        }
        if (!CoopWorldsCallFree() || (mode == kDungeonMode && BtActStatus.action_on != 0)) {
            return;
        }
        const int waited = g_apart.ticks - 1; // (coop-arrivalwait: the ticks the ask waited here for this copy's player to be free)

        g_apart.shown = true;
        g_apart.waited += waited;
        g_apart.up = 0;
        g_apart.ticks = kAnswerTicks;
        g_apart.held = CoopWorldsCallPadButtons();
        LockPlayer(true);
        std::fprintf(stderr, "coop: apart trade: player %d's question up: player %d wants %d (%s) for %d (%s), after %d ticks waiting\n", ApartSelf(),
                     g_apart.other, g_apart.mine, kNames[g_apart.mine], g_apart.theirs, kNames[g_apart.theirs], g_apart.waited);
    }

    // coop-askwait: a scene or a menu took the player: the question goes back to waiting (hidden, the player let go: never over the
    // game's own UI) and comes up again once the player is free. (Before, it stayed drawn and held over the scene, unanswered.)
    if (!CoopWorldsCallFree()) {
        if (!Mutated("ask_lapse")) {
            g_apart.shown = false;
            g_apart.ticks = 0;
            LockPlayer(false);
            std::fprintf(stderr, "coop: apart trade: player %d's question goes back to waiting (its player taken)\n", ApartSelf());
        }
        return;
    }
    g_apart.up++;

    const std::uint16_t buttons = CoopWorldsCallPadButtons();
    const std::uint16_t pressed = buttons & ~g_apart.held;

    g_apart.held = buttons;
    if ((pressed & (kInputUp | kInputDown | kInputLeft | kInputRight)) != 0) {
        g_apart.cursor = 1 - g_apart.cursor;
        {
            SoundOwner sound(ApartSelf()); // coop-rc2 (router lint): apart, a sound of this copy's own player's trade box
            SndSePlay(MENU_SOUND_CURSOR, -1, 0);
        }
    }

    // Test hook (DC_COOP_TEST_APART_ANSWER=yes|no[@N]): the one asked answers half a second (N ticks) after the popup is up.
    static const char *test_answer = TestEnv("DC_COOP_TEST_APART_ANSWER");
    static const char *test_at = test_answer != nullptr ? std::strchr(test_answer, '@') : nullptr;
    static const int   test_up = test_at != nullptr ? std::atoi(test_at + 1) : 30;
    const bool         test_now = test_answer != nullptr && g_apart.up >= test_up;
    const bool         yes = ((pressed & kInputCross) != 0 && g_apart.cursor == 0) || (test_now && std::strncmp(test_answer, "yes", 3) == 0);
    const bool         no = (pressed & kInputCircle) != 0 || ((pressed & kInputCross) != 0 && g_apart.cursor == 1) || (test_now && std::strncmp(test_answer, "no", 2) == 0);

    if (yes) {
        {
            SoundOwner sound(ApartSelf()); // coop-rc2 (router lint): apart, a sound of this copy's own player's trade box
            SndSePlay(MENU_SOUND_CONFIRM, -1, 0);
        }
        const int own = g_apart.theirs;
        const int other = g_apart.mine;

        // coop-deadswap: this copy's player lost the member's life while the question was up: "can't trade now", nothing changes
        if (mode == kDungeonMode && TradeStatus()->hp[other] <= 0 && !Mutated("deadswap")) {
            std::fprintf(stderr, "coop: apart trade: player %d's member %d has no life: the yes is not taken\n", ApartSelf(), other);
            ApartAnswer(2);
            ApartClose();
            return;
        }
        ApartAnswer(1);
        ApartClose();
        ApartApply(own, other);
        return;
    }
    // (coop-askwait: up till answered or withdrawn; DC_COOP_MUTATE=ask_lapse, twin: kAnswerTicks unanswered answers "no answer", as before)
    if (no || (--g_apart.ticks <= 0 && Mutated("ask_lapse"))) {
        {
            SoundOwner sound(ApartSelf()); // coop-rc2 (router lint): apart, a sound of this copy's own player's trade box
            SndSePlay(MENU_SOUND_REFUSE, -1, 0);
        }
        ApartAnswer(no ? 0 : 3);
        ApartClose();
    }
}

} // namespace

void PromptApartTradeTick() {
    StepApartTrade();

    // coop-swapcombo test hook (DC_COOP_TEST_APART_ASK=1, set on player 1's copy alone: t-zonechara -Case apart-ask): once apart and he has
    // played 60 ticks on his own, his Characters page picks player 2's member on his copy, as Jep's did (MenuCharaChange first).
    static const bool apart_ask = TestEnvOn("DC_COOP_TEST_APART_ASK");
    static int        apart_ticks = 0;
    static bool       apart_said = false;

    if (apart_ask && !apart_said && CoopWorldsApart()) {
        apart_said = true;
        std::fprintf(stderr, "coop-test: apart ask: apart here (player %d's copy, task %d, partner member %d, choice %d)\n", NetplayLocalPlayer(),
                     (int) gameTask, g_partner.loaded_chara, g_partner.choice);
    }

    // (coop-memberowner: no word of the other's member after 600 apart ticks, said once: the apart_other_unknown twin's stop)
    static int unknown_ticks = 0;

    if (apart_ask && apart_ticks >= 0 && TradeStatus() != nullptr && CoopWorldsApart() && ApartOtherMember() < 0 && ++unknown_ticks == 600) {
        std::fprintf(stderr, "coop-test: apart ask: no word of the other player's member after 600 apart ticks: no ask\n");
    }
    // coop-memberstage4 test hook (DC_COOP_TEST_APART_CHANGE=c, t-memberowner -Case heard): apart, after 60 ticks of play on a floor,
    // this copy's player picks member c on the Characters page (her own change where she is: the other copy hears it).
    static const int apart_change = TestEnvInt("DC_COOP_TEST_APART_CHANGE", -1);
    static int       change_ticks = 0;

    if (apart_change >= 0 && change_ticks >= 0 && TradeStatus() != nullptr && CoopWorldsApart() && mode == kDungeonMode && CoopPlainPlay() &&
        ++change_ticks >= (Mutated("apart_escape_unordered") ? 1 : 60)) { // (twin apart_escape_unordered: at once)
        change_ticks = -1;
        const int taken = MenuCharaChange(apart_change);

        std::fprintf(stderr, "coop-test: apart change: player %d picks %d on the Characters page (taken %d) at link tick %lld\n", CoopWorldsSelf(), apart_change,
                     taken, (long long) NetplayLinkTick());
        if (taken >= 0) {
            PromptChangePlayer(taken);
            g_apart_changed = true; // (DC_COOP_TEST_APART_ESCAPE: she escapes to his town next, the merge carries his copy's word)
        }
    }
    // (coop-memberstage4: with DC_COOP_TEST_APART_CHANGE set too, t-memberowner -Case pageask, the ask waits for that change)
    static const bool change_first = TestEnvInt("DC_COOP_TEST_APART_CHANGE", -1) >= 0;

    if (apart_ask && apart_ticks >= 0 && TradeStatus() != nullptr && CoopWorldsApart() && ApartOtherMember() >= 0 && (!change_first || g_apart_changed) &&
        ++apart_ticks >= 60) {
        const int wanted = ApartOtherMember();

        apart_ticks = -1;
        std::fprintf(stderr, "coop-test: apart ask: player %d asks for the other player's member %d while apart\n", CoopWorldsSelf(), wanted);
        // (player 1's copy through his Characters page, as Jep's was; player 2's copy asks as her own pick would reach AskSwap)
        if (CoopWorldsSelf() == 2) {
            AskSwap(1, ApartOtherMember());
        } else if (MenuCharaChange(wanted) >= 0) {
            PromptChangePlayer(wanted);
        }
    }
}

// The other copy's step of a trade across zones (coop_worlds' kWhatTrade).
void PromptApartTradeHeard(int from, int step, int a, int b, int c) {
    if (TradeStatus() == nullptr) {
        return;
    }
    switch (step) {
        case 0: // asked: a the asker's member, b the member wanted (this copy's player's)
            // (coop-deadswap: this copy's player on a floor with no life left is not traded onto: "can't trade now")
            if (g_apart.role != 0 || b != ApartOwnMember() || a == b || a < 0 || a > 5 ||
                (b >= 0 && b <= 5 && mode == kDungeonMode && TradeStatus()->hp[b] <= 0 && !Mutated("deadswap"))) {
                std::fprintf(stderr, "coop: apart trade: player %d's ask for %d cannot be taken here (open %d, this player's member %d)\n", from, b, g_apart.role,
                             ApartOwnMember());
                CoopWorldsTradeSend(1, a, 2, b);
                return;
            }
            g_apart = ApartTrade{};
            g_apart.role = 2;
            g_apart.other = from;
            g_apart.mine = b;
            g_apart.theirs = a;
            SetMember(from - 1, a, MemberWhy::kNet); // (the asker's word for its own member)
            std::fprintf(stderr, "coop: apart trade: player %d asks this copy's player for %d (%s), offering %d (%s)\n", from, b, kNames[b], a, kNames[a]);
            return;
        case 1: { // answered: a the ask's offer (this player's member then), b the result, c the member wanted
            const bool ours = g_apart.role == 1 && a == g_apart.mine && c == g_apart.theirs;

            if (b == 1) {
                // (a yes trades on both copies, even one that crossed this copy's withdrawal: the asked copy has applied it)
                if (ours) {
                    ApartClose();
                }
                ApartApply(c, a);
                return;
            }
            if (ours) {
                ApartClose();
                {
                    SoundOwner sound(ApartSelf()); // coop-rc2 (router lint): apart, a sound of this copy's own player's trade box
                    SndSePlay(MENU_SOUND_REFUSE, -1, 0);
                }
                char line[96];

                std::snprintf(line, sizeof line, b == 0 ? "%s said no" : b == 3 ? "%s didn't answer" : "%s can't trade now", NetplayShownName(from).c_str());
                NetplayShowNotice(line);
            }
            std::fprintf(stderr, "coop: apart trade: player %d's answer %d: nothing changes\n", from, b);
            return;
        }
        case 3: // withdrawn before an answer
            if (g_apart.role == 2 && g_apart.other == from) {
                std::fprintf(stderr, "coop: apart trade: player %d withdrew the ask\n", from);
                ApartClose();
            }
            return;
        default:
            return;
    }
}

int PromptApartOwnMember() {
    return TradeStatus() != nullptr ? ApartOwnMember() : -1;
}

int PromptApartOtherMember() {
    return TradeStatus() != nullptr ? ApartOtherMember() : -1;
}

void PromptApartApply(int own, int other) {
    if (TradeStatus() != nullptr) {
        ApartApply(own, other);
    }
}

bool SwapSameZone() {
    return Enabled() && dc::platform::ConfigGet().coop_swap_same_zone;
}

// trade3 (Jep's rc3 playtest 11-13, his log: P1 alone on a floor, P2 in town as Xiao; his Select opened the game's own wheel, as it
// does whenever co-op's ring is not his (OwnWheels: apart), and its Cross wrote cur_chara (menu_dungeon.cpp SetStatusChara): he
// became Xiao with nobody asked, two players on one member). The game's wheel knows only one player; its pick of a member another
// player plays now goes the way the Characters page's does (MenuCharaChange): a trade asked for, apart across zones (AskSwap), the
// player's member kept. The knocked-out menu (mode 1, his life gone) takes no other player's member: refused. DC_COOP_MUTATE=
// wheel_apart_plain (twin): the game's own pick, as before.
int CharaWheelPick(int chara, int knocked_out) {
    if (!Enabled() || UserStatus == nullptr || chara < 0 || chara > 5 || chara == UserStatus->cur_chara) {
        return 0;
    }
    const bool apart = CoopWorldsApart();
    const int  other = apart ? ApartOtherMember() : g_partner.loaded_chara;

    if (other < 0 || chara != other) {
        return 0;
    }
    if (Mutated("wheel_apart_plain")) {
        std::fprintf(stderr, "coop: the game's wheel takes member %d plainly (wheel_apart_plain)\n", chara);
        return 0;
    }
    if (knocked_out != 0) {
        SoundOwner sound(apart ? ApartSelf() : 1);

        SndSePlay(MENU_SOUND_REFUSE, -1, 0);
        std::fprintf(stderr, "coop: the game's wheel (knocked out) refuses member %d: another player plays it\n", chara);
        return 2;
    }
    std::fprintf(stderr, "coop: the game's wheel picks member %d, another player's (%s): asked as a trade, the player stays %d\n", chara,
                 apart ? "apart" : "together", (int) UserStatus->cur_chara);
    AskSwap(apart ? ApartSelf() - 1 : 0, chara);
    return 1;
}

void AskSwap(int asker, int wanted, int spot) {
    if (wanted < 0 || wanted > 5) {
        return;
    }
    // coop-swapcombo part 2: apart, the ask goes to the other copy as a trade across zones (ApartAsk, above). Before it (Jep's rc1
    // playtest: an ask while apart held his world a while, nobody was asked and it ran out), the stopgap refused it with a notice:
    // DC_COOP_MUTATE=apart_trade_refused (the trade rows' twin) is that. (coop-rc2: the stopgap row's twin apart_ask_held went
    // with that row: no test turned it on any more, the registry lint.)
    // trade3 (Jep 11-13): game.coop_swap_same_zone (the host's, default off) keeps trades to players in the same zone: apart, the
    // ask is refused at once with a plain note on the asker's screen (apart, this copy is the asker's alone). DC_COOP_MUTATE=
    // same_zone_ignored (twin): the switch is read as off.
    const bool same_zone = SwapSameZone() && !Mutated("same_zone_ignored");

    if (CoopWorldsApart() && !Mutated("apart_trade_refused") && !same_zone) {
        ApartAsk(wanted);
        return;
    }
    if (CoopWorldsApart()) {
        SoundOwner sound(ApartSelf()); // (apart: this copy's own player, whoever asked)

        SndSePlay(MENU_SOUND_REFUSE, -1, 0);
        NetplayShowNotice(same_zone ? "Trading needs both players in the same area" : "Trading needs both players in the same area for now");
        std::fprintf(stderr, "coop: player %d's ask for %d refused: the players are apart (%s)\n", ApartSelf(), wanted,
                     same_zone ? "game.coop_swap_same_zone" : "the trade needs both here");
        return;
    }
    // coop-pt1111k (Jep 11-11: two trades on one tick, a fixed order decides and the loser is refused): one question at a
    // time; the step's order (player 1's step before player 2's, the same on every copy) makes the first the one asked, and a
    // later ask while it is open is refused with the refuse sound on its asker's screen. DC_COOP_MUTATE=swapask_override
    // (twin): the later ask takes the question's place, unrefused.
    // coop-deadswap: a member with no life (the one asked for, or the asker's own offer) is not asked for: the asker's refuse sound, as
    // retail's wheel and Characters page give a greyed member, and nothing is sent.
    if (MemberWithoutLife(wanted) || MemberWithoutLife(TradeMemberOf(asker))) {
        SoundOwner sound(asker + 1);

        SndSePlay(MENU_SOUND_REFUSE, -1, 0);
        std::fprintf(stderr, "coop: player %d's ask for %d refused: %s no life (player %d's member %d) at link tick %lld\n", asker + 1, wanted,
                     MemberWithoutLife(wanted) ? "the member asked for has" : "the asker's own member has", asker + 1, TradeMemberOf(asker),
                     (long long) NetplayLinkTick());
        return;
    }
    if (g_prompt.active && !(Mutated("swapask_override") && !g_prompt.revive)) {
        SoundOwner sound(asker + 1);

        SndSePlay(MENU_SOUND_REFUSE, -1, 0);
        std::fprintf(stderr, "coop: player %d's ask for %d refused: player %d's question is open (one at a time) at link tick %lld\n", asker + 1, wanted,
                     g_prompt.asker + 1, (long long) NetplayLinkTick());
        return;
    }

    g_prompt.active = true;
    g_prompt.asker = asker;
    g_prompt.wanted = wanted;
    g_prompt.spot = spot;
    g_prompt.ticks = kAnswerTicks;
    g_prompt.shown = false;
    g_prompt.waited = 0;
    // A button already down (the menu's Cross that asked) is no answer.
    g_prompt.held = Pad(1 - asker).buttons;
    g_prompt.asker_held = Pad(asker).buttons;
    g_prompt.cursor = 0;
    g_prompt.id = NetplayLinkTick(); // coop-wave2
    g_prompt.answer_due = 0;
    QuestionHandlerOnce();
    SoundOwner sound(2 - asker); // coop-menuse: the question's sound is the asked player's
    SndSePlay(MENU_SOUND_CURSOR, -1, 0);
    std::printf("coop: %dP asks for %d (%s); %dP to answer\n", asker + 1, wanted, kNames[wanted], 2 - asker);
}

// coop-townwheel: the asked player's Up, Down, Left or Right moves the popup's hand between YES and NO (as the menu's
// Go Outside question's). True when it moved.
bool MoveCursor(std::uint16_t pressed, int &cursor) {
    if ((pressed & (kInputUp | kInputDown | kInputLeft | kInputRight)) == 0) {
        return false;
    }
    cursor = 1 - cursor;
    SndSePlay(MENU_SOUND_CURSOR, -1, 0);
    return true;
}

// coop-swapcombo part 3's test trace (DC_COOP_TEST_ASK_ACTION=1): a swap asked while a player is in an action (his: BtActStatus,
// hers: her swing), followed to that action's end: the ticks the motion advanced and the ticks the body moved while the motion
// stood (the slide Jep saw), then one line. Reads only, every copy alike.
void AskActionTrace() {
    static const bool on = TestEnvOn("DC_COOP_TEST_ASK_ACTION");
    struct Trace {
        bool  armed = false;
        bool  said = false;
        int   ticks = 0;
        int   advanced = 0;
        int   slid = 0;
        float time = 0.0f;
        float pos[3] = {};
    };
    static Trace        trace[2];
    static std::int64_t last_tick = -1;

    // (stepped from StepPrompt and from the question's own step, which runs while the world holds: once a link tick)
    if (!on || UserStatus == nullptr || !CoopPlainPlay() || NetplayLinkTick() == last_tick) {
        return;
    }
    last_tick = NetplayLinkTick();
    for (int who = 0; who < 2; who++) {
        Trace      &t = trace[who];
        const bool  busy = who == 0 ? BtActStatus.action_on != 0 : g_partner.attacking;
        CCharacter *body = who == 0 ? &CharaMain : g_partner.chara;

        if (body == nullptr || t.said) {
            continue;
        }
        const float *pos = who == 0 ? CharaMain.pos : g_partner.pos;

        if (!t.armed) {
            if (g_prompt.active && !g_prompt.revive && busy) {
                t.armed = true;
                t.time = body->motion_type.state.time;
                std::memcpy(t.pos, pos, sizeof t.pos);
            }
            continue;
        }
        if (!busy && !g_prompt.active) { // (followed until both the action and the question are over: a hold that ends the action early slides on)
            t.said = true;
            std::fprintf(stderr, "coop-test: player %d's action through the swap ask: %d ticks, its motion advanced on %d, the body moved with the motion standing on %d\n",
                         who + 1, t.ticks, t.advanced, t.slid);
            continue;
        }
        const float time = body->motion_type.state.time;
        const float dx = pos[0] - t.pos[0], dz = pos[2] - t.pos[2];

        t.ticks++;
        if (time != t.time) {
            t.advanced++;
        } else if (dx * dx + dz * dz > 0.0001f) {
            t.slid++;
        }
        t.time = time;
        std::memcpy(t.pos, pos, sizeof t.pos);
    }
}

void StepPrompt() {
    g_player_chara = UserStatus->cur_chara;
    AskActionTrace(); // coop-swapcombo: test hook

    if (g_icon_due != 0 && BtEventMode == 0 && CoopPlainPlay()) { // coop-floorscene: ChangePlayer's, after the scene
        g_icon_due = 0;
        LoadActiveItemIcon();
        std::fprintf(stderr, "coop: the item sheet reloaded after the scene\n");
    }

}

void StepSwapQuestion() {
    AskActionTrace(); // coop-swapcombo: test hook (the held ticks too)
    if (EndQuestionApart("the question's step")) { // coop-crashswapq: never stepped apart (its floor reading there traded in a town)
        return;
    }

    // (in play only, as the partner's step ran it: a scene or a floor change waits; coop-townally: in a town, its walk: a talk, an
    // event, a shop or the Georama waits, and the question with it)
    const bool town = InTownQuestion();

    if (!g_prompt.active || (town ? !TownQuestionInPlay() : UserStatus == nullptr || !CoopPlainPlay())) {
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
    // (coop-townally: in a town the members' own word: the game's player is Toan there, the partner body her town walker's)
    int theirs = town ? TownAlliesPlays(target) : target == 0 ? UserStatus->cur_chara : g_partner.loaded_chara;

    if (theirs != g_prompt.wanted || (!town && g_partner.down > 0)) {
        std::printf("coop: swap question dropped\n");
        Close();
        return;
    }
    // coop-deadswap: a player went down (or a member's life ran out) while it was open: withdrawn on every copy on this tick, before
    // any answer is taken (an answer already on its way finds no question), the asker's refuse sound.
    if (!town && (MemberWithoutLife(g_prompt.wanted) || MemberWithoutLife(TradeMemberOf(g_prompt.asker)))) {
        SoundOwner asker_sound(g_prompt.asker + 1);

        SndSePlay(MENU_SOUND_REFUSE, -1, 0);
        std::fprintf(stderr, "coop: swap question withdrawn: a member without life (player %d asked for %d, offering %d) at link tick %lld\n",
                     g_prompt.asker + 1, g_prompt.wanted, TradeMemberOf(g_prompt.asker), (long long) NetplayLinkTick());
        Close();
        return;
    }

    // coop-swapcombo part 3 (Jep's rc1 playtest 2026-11-12: P2 asked while Toan swung, his swing froze and he slid on): a question
    // held both players and, with every player held, the world (coop_pause: driveStepHold stands each motion on its frame) while
    // the swing's own push (BtActStatus.move_power) kept carrying him. trade3 (Jep's rc3: the question waited unseen for the swing's
    // end): the question is up at once; a player in an action (his action_on, her swing, charge or throw) is not held until that
    // action is over (SwapHeldOff), and the answer is taken, and its five seconds run, only once neither player is in one (the game
    // changes a character only between actions, as its wheel waits for action_on 0). DC_COOP_MUTATE=swap_ask_midaction (twin): up
    // at once and holding the swinger, the freeze and the slide.
    if (!g_prompt.shown) {
        g_prompt.shown = true;
        std::fprintf(stderr, "coop: swap question up at link tick %lld at once (in an action: player 1 %d, player 2 %d)\n", (long long) NetplayLinkTick(),
                     (int) SwapInAction(0), (int) SwapInAction(1));
    }
    const bool     local = QuestionLocal(); // coop-wave2
    QuestionOwn   &own = OwnQuestion();
    std::uint16_t &held = local ? own.held : g_prompt.held;
    std::uint16_t &asker_held = local ? own.asker_held : g_prompt.asker_held;
    int           &cursor = local ? own.cursor : g_prompt.cursor;
    const int      self = NetplayLocalPlayer() - 1;

    if (SwapHeldOff(0) || SwapHeldOff(1)) {
        g_prompt.waited++;
        held = Pad(target).buttons; // (a button down while an action plays is no answer)
        asker_held = Pad(g_prompt.asker).buttons;
        LockPlayer(!SwapHeldOff(0) && (target == 0 || g_prompt.asker == 0) && !FullMenuOpenFor(1));
        return;
    }
    // coop-wave2: an answer or a withdrawal its command brought, on every copy alike (once neither player is in an action, as an answer
    // is taken).
    if (const int answer = g_prompt.answer_due; answer != 0) {
        g_prompt.answer_due = 0;
        if (answer == kAnswerWithdraw) {
            SoundOwner asker_sound(g_prompt.asker + 1);

            SndSePlay(MENU_SOUND_REFUSE, -1, 0);
            std::fprintf(stderr, "coop: swap question withdrawn by player %d at link tick %lld\n", g_prompt.asker + 1, (long long) NetplayLinkTick());
            Close();
            return;
        }
        if (answer == kAnswerYes) {
            SndSePlay(MENU_SOUND_CONFIRM, -1, 0);
            std::fprintf(stderr, "coop: swap accepted by player %d at link tick %lld (answered on its own copy)\n", target + 1, (long long) NetplayLinkTick());
            Close();
            Trade();
            return;
        }
        SndSePlay(MENU_SOUND_REFUSE, -1, 0);
        std::printf("coop: swap declined\n");
        std::fprintf(stderr, "coop: swap declined by player %d (%s) at link tick %lld\n", target + 1, answer == kAnswerCircle ? "Circle" : "NO",
                     (long long) NetplayLinkTick());
        Close();
        return;
    }
    if (g_prompt.waited > 0) {
        std::fprintf(stderr, "coop: swap question: both players' actions over at link tick %lld (the answer held %d ticks)\n", (long long) NetplayLinkTick(),
                     g_prompt.waited);
        g_prompt.waited = -1; // (said once)
    }

    // coop-townwheel: a player's own menu keeps their buttons (the asker's still closing after its Allies pick, or the one asked
    // in theirs), and neither the popup nor the box draws over it (DrawSwapQuestion): the one asked answers, and the five
    // seconds run, once their menu is shut; the asker's Circle cancels once theirs is. DC_COOP_MUTATE=swap_over_menu (twin):
    // answered from inside the menu, as before.
    // (coop-townally: in a town, either menu of theirs: the Triangle menu, a shop, the town's own; the in-menu bits, alike everywhere)
    const bool asked_menu = (town ? PlayerInMenu(target + 1) : FullMenuOpenFor(target + 1)) && !Mutated("swap_over_menu");
    const bool asker_menu = (town ? PlayerInMenu(g_prompt.asker + 1) : FullMenuOpenFor(g_prompt.asker + 1)) && !Mutated("swap_over_menu");

    // (coop-townwheel: player 1 waiting on his own question is held too: his Circle is its cancel)
    LockPlayer((target == 0 && !asked_menu) || (g_prompt.asker == 0 && !asker_menu));

    // coop-townwheel (Jep's rule for questions between players, 2026-11-02): the asker waits in its box and its Circle withdraws
    // the question. DC_COOP_MUTATE=swap_nocancel (twin): the asker's Circle does nothing, as before.
    std::uint16_t asker_buttons = Pad(g_prompt.asker).buttons;
    std::uint16_t asker_pressed = asker_menu ? 0 : asker_buttons & ~asker_held;

    asker_held = asker_buttons;
    if (local) { // coop-wave2: the asker's Circle on the asker's copy alone, a command
        if (self == g_prompt.asker && !own.answered && (asker_pressed & kInputCircle) != 0 && !Mutated("swap_nocancel")) {
            QuestionAnswer(kAnswerWithdraw);
        }
    } else if ((asker_pressed & kInputCircle) != 0 && !Mutated("swap_nocancel")) {
        SoundOwner asker_sound(g_prompt.asker + 1);

        SndSePlay(MENU_SOUND_REFUSE, -1, 0);
        std::printf("coop: %dP withdraws the swap question\n", g_prompt.asker + 1);
        std::fprintf(stderr, "coop: swap question withdrawn by player %d at link tick %lld\n", g_prompt.asker + 1, (long long) NetplayLinkTick());
        Close();
        return;
    }
    if (asked_menu) {
        held = Pad(target).buttons; // (a button down as the menu shuts is no answer)
        return;
    }

    std::uint16_t buttons = Pad(target).buttons;
    std::uint16_t pressed = buttons & ~held;

    held = buttons;
    if (local && (self != target || own.answered)) { // coop-wave2: the hand and the answer are the asked player's copy's alone
        pressed = 0;
    }
    MoveCursor(pressed, cursor);

    // Cross takes the hand's answer, Circle is no. (coop-townwheel: the old swapdecline mutation went, no row turned it on; the
    // NO answer's own row is t-triangle-menu-p1alliesno.)
    bool yes = (pressed & kInputCross) != 0 && cursor == 0;

    // coop-pt1111k test hook: the one asked says yes (coop-swapcombo: half a second after the question is up, as a person answers,
    // so a question that held a swing shows it)
    yes = yes || (ZoneTestAnswersYes() && g_prompt.ticks <= kAnswerTicks - 30 && (!local || (self == target && !own.answered)));

    if (local) { // coop-wave2: the asked player's answer, a command; every copy takes it at its tick (above)
        const bool lapsed = --g_prompt.ticks <= 0 && Mutated("swap_lapse");

        if (yes) {
            QuestionAnswer(kAnswerYes);
        } else if ((pressed & (kInputCircle | kInputCross)) != 0) {
            QuestionAnswer((pressed & kInputCircle) != 0 ? kAnswerCircle : kAnswerNo);
        } else if (lapsed) { // (the twin's lapse: every copy alike)
            SndSePlay(MENU_SOUND_REFUSE, -1, 0);
            std::fprintf(stderr, "coop: swap declined by player %d (no answer) at link tick %lld\n", target + 1, (long long) NetplayLinkTick());
            Close();
        }
        return;
    }
    if (yes) {
        SndSePlay(MENU_SOUND_CONFIRM, -1, 0);
        Close();
        Trade();
        return;
    }

    // coop-016 (Jep's rule 11-15, coop-askwait: "if the requester has a cancel button, there's no need to have it automatically
    // expire"): the question stays up till the one asked answers or the asker's Circle withdraws it (above), in a town (coop-townally's
    // Allies ask) as on a floor. Its five seconds still count for the test hook's half second. DC_COOP_MUTATE=swap_lapse (twin): five
    // seconds unanswered answer "no answer", as before.
    const bool lapsed = --g_prompt.ticks <= 0 && Mutated("swap_lapse");

    if ((pressed & (kInputCircle | kInputCross)) != 0 || lapsed) {
        SndSePlay(MENU_SOUND_REFUSE, -1, 0);
        std::printf("coop: swap declined\n");
        std::fprintf(stderr, "coop: swap declined by player %d (%s) at link tick %lld\n", target + 1,
                     lapsed ? "no answer" : (pressed & kInputCircle) != 0 ? "Circle" : "NO", (long long) NetplayLinkTick());
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
    // (coop-swapcombo: apart, this copy's player while its popup of a trade across zones is up)
    // (trade3: a player in an action is not held by the question until it is over)
    return (g_prompt.active && g_prompt.shown && !g_prompt.revive && (port == 0 || port == 1) && !SwapHeldOff(port) && !Mutated("swap_no_hold")) ||
           (g_apart.role == 2 && g_apart.shown && port == 0);
}

bool PromptQuestion(PromptView *view) {
    // coop-swapcombo part 2: apart, a trade across zones draws as together's (the same popup and box): this copy's player asks or is asked.
    if (!g_prompt.active && (g_apart.role == 1 || (g_apart.role == 2 && g_apart.shown))) {
        const bool asked = g_apart.role == 2;

        *view = PromptView{};
        view->asked = asked ? CoopWorldsSelf() : g_apart.other;
        view->asker = asked ? g_apart.other : CoopWorldsSelf();
        view->wanted = asked ? g_apart.mine : g_apart.theirs;
        view->own = asked ? g_apart.theirs : g_apart.mine;
        view->cursor = g_apart.cursor;
        view->ticks = g_apart.ticks;
        return true;
    }
    // coop-swapcombo part 3: not drawn while it waits. coop-crashswapq: nor apart, where it ends at its next step (a split's frame
    // may draw before that step: the members it names are the linked pair's).
    if (!g_prompt.active || !g_prompt.shown || (CoopWorldsApart() && !Mutated("swap_survives_split"))) {
        return false;
    }
    view->asked = 2 - g_prompt.asker;
    view->asker = g_prompt.asker + 1;
    view->revive = g_prompt.revive;
    view->wanted = g_prompt.wanted;
    view->own = InTownQuestion() ? TownAlliesPlays(g_prompt.asker) : g_prompt.asker == 0 ? UserStatus->cur_chara : g_partner.loaded_chara; // (coop-townally)
    view->spot = g_prompt.spot;
    view->cursor = QuestionLocal() ? OwnQuestion().cursor : g_prompt.cursor; // coop-wave2: the asked player's own hand
    view->ticks = g_prompt.ticks;
    view->carried = g_prompt.revive ? RevivePowders(view->asked) : 0;
    return true;
}

namespace {
int MenuCharaChangeTaken(int wanted);
} // namespace

// coop-swapcombo part 2 (Jep's rc1 playtest 2026-11-12: "P1 is still Toan on screen but hears Xiao's voice when attacking"):
// whenever co-op keeps the player's member (a trade asked for, her pick, a zone refusal), retail's next step,
// CharaChangeInitToGL2, still loads charachangeid's voice set (SndVoiceLoadBG, synced in BtMenuLoad2): the wanted member's.
// It now names the kept member, so the player's voice stays his. DC_COOP_MUTATE=menu_voice_wanted (twin): the wanted one's, as before.
int MenuCharaChange(int wanted) {
    int taken = MenuCharaChangeTaken(wanted);

    // coop-swapcombo (A): his (or, apart, her copy's) own pick, as retail's page loads it. coop-memberowner: one another player
    // holds is refused (the page keeps the player's member), as the checks above refuse the ones they know of.
    // coop-memberstage4: on her copy apart her page's change writes her member and her pick together (SetPick, as the apart trade's
    // own side does), so her pick never lags her member (DCO-100: her copy then drew and offered her old member).
    // DC_COOP_MUTATE=page_member_only (twin): her member alone, as before.
    const int  page_player = GamePlayerIndex();
    const bool page_set = taken >= 0 && (page_player == 1 && !Mutated("page_member_only") ? SetPick(1, taken, MemberWhy::kPage) : SetMember(page_player, taken, MemberWhy::kPage));

    if (taken >= 0 && !page_set) {
        MemberRefused(page_player, taken, MemberWhy::kPage);
        if (UserStatus != nullptr && g_player_chara >= 0) {
            UserStatus->cur_chara = (s8) g_player_chara;
        }
        taken = -1;
    }
    if (taken < 0 && UserStatus != nullptr && !Mutated("menu_voice_wanted")) {
        charachangeid = (s16) UserStatus->cur_chara;
        std::printf("coop: the Characters page keeps member %d: its voice set stays (member %d asked for)\n", (int) UserStatus->cur_chara, wanted);
    }
    return taken;
}

// coop-townally (Jep 11-16: "in multiplayer players must be able to change character outside the dungeon too", exactly as the
// dungeon's change: the same rules and prompts as the dungeon's Allies page and its Select ring, the menu's look unchanged).
// Retail refuses the Allies page's Cross in a town (battlemenu.cpp, BATTLE_MENU_MODE_TOWN: the refuse sound): with a co-op session
// it is a pick of the menu's player (the one whose town menu steps: player 1's, player 2's own, or this copy's apart). Toan stays
// the town's player (his walk, talks, shops, events, Georama); the pick changes the player's member alone, drawn in the town
// (player 1's body, coop-p1body; her town walker or skin, TownMember) and played from the next floor on (MemberRestoreTick).
namespace {

CUserStatus *PartyStatus() {
    return UserStatus != nullptr ? UserStatus : SaveData != nullptr ? (CUserStatus *) SaveData->GetDngStatus() : nullptr;
}

int TownAlliesPicker() {
    const int stepping = CoopTownMenuStepPlayer();

    if (stepping > 0) {
        return stepping;
    }
    // coop-016 (t-triangle-menu-p1allies-lockstep: player 2's copy named his Allies page hers): on a floor, the menu's own player.
    // A lockstep menu (DC_COOP_MUTATE=fullmenu_lockstep, the old way) steps player 1's menu on every copy; a local one, its player's.
    // DC_COOP_MUTATE=allies_picker_local (twin t-triangle-menu-p1allies-lockstep~picker): the copy's own player, as before.
    if (mode == kDungeonMode && !CoopWorldsApart() && !Mutated("allies_picker_local")) {
        if (FullMenuOwner() > 0) {
            return FullMenuOwner();
        }
        for (int player = 1; player <= kMaxPlayers; player++) {
            if (FullMenuRunsLocalFor(player)) {
                return player;
            }
        }
    }
    return CoopWorldsApart() ? CoopWorldsSelf() : std::max(1, NetplayLocalPlayer()); // (a copy with no link: player 1)
}

// A member with a weapon of its own equipped (its body and weapon load from it), as the Select ring's PlayerPickable asks.
bool TownAlliesArmed(int chara) {
    CUserStatus *status = PartyStatus();
    const int    slot = status != nullptr && chara >= 0 && chara < 6 ? status->equipped_weapon_slot[chara] : -1;

    return slot >= 0 && slot < 11 && status->chara_weapons[chara][slot].item_no > 0;
}

// The member player index plays in town now (the 11-16 run: a state file's start keeps no member, -1, for either player, and the
// raw word let player 1 take Xiao while she played her: no question). The town's reads of it: player 1's none is Toan (the town's
// player, coop-p1body's GameBodyMember), player 2's is her town body (TownMember: kept, else her body's pick, else Xiao), a third's
// is kept. DC_COOP_MUTATE=town_allies_raw (twin): the kept word alone, as before.
int TownAlliesPlays(int player) {
    const int kept = Member(player);

    if (kept >= 0 || Mutated("town_allies_raw")) {
        return kept;
    }
    return player == 0 ? CHARA_TOAN : player == 1 ? TownPlayerMember() : -1;
}

} // namespace

int TownMemberOf(int port) {
    return TownAlliesPlays(port);
}

int TownMemberHolder(int chara, int except) {
    for (int other = 0; other < kMaxPlayers; other++) {
        if (other != except && TownAlliesPlays(other) == chara) {
            return other;
        }
    }
    return -1;
}

bool TownAlliesOn() {
    // (a co-op session: linked, a catch-up's alike ticks, or apart; alone, retail's refusal)
    return Enabled() && (NetplayRole() != 0 || CoopWorldsApart());
}

int TownAlliesPick(int chara) {
    const int player = TownAlliesPicker();

    if (player < 1 || player > kMaxPlayers || chara < 0 || chara > 5) {
        return 0;
    }
    if (chara == TownAlliesPlays(player - 1)) {
        return 0; // (the dungeon's Cross on the member already played: nothing)
    }
    std::fprintf(stderr, "coop-allies: player %d's Allies pick %d (%s) in town (member %d) on player %d's copy%s at link tick %lld\n", player, chara, kNames[chara],
                 Member(player - 1), CoopWorldsApart() ? CoopWorldsSelf() : NetplayLocalPlayer(), CoopWorldsApart() ? " (apart)" : "",
                 (long long) NetplayLinkTick());
    // DC_COOP_MUTATE=town_allies_refused (twin): retail's refusal, as before.
    if (Mutated("town_allies_refused")) {
        return -1;
    }
    // (a local menu's pick goes to every copy as a command, applied on one tick: ApplyPick -> TownAlliesApply)
    if (FullMenuLocalPick(player, chara)) {
        return 1;
    }
    TownAlliesApply(player, chara);
    return 1;
}

int AlliesCursorStart(int cur) {
    if (!Enabled() || (NetplayRole() == 0 && !CoopWorldsApart())) {
        return cur; // (alone: retail's)
    }
    const int  player = TownAlliesPicker();
    const bool town = mode != kDungeonMode;
    const int  plays = town ? TownAlliesPlays(player - 1) : cur;
    // DC_COOP_MUTATE=allies_cursor_game (twin): the game's cur_chara, as before (Toan in a town).
    const int  start = plays >= 0 && plays <= 5 && !Mutated("allies_cursor_game") ? plays : cur;

    std::fprintf(stderr, "coop-allies: player %d's Allies page opens on member %d (%s) (the game's %d), player %d's member %d, in %s, on player %d's copy%s at link tick %lld\n",
                 player, start, start >= 0 && start <= 5 ? kNames[start] : "none", cur, player, Member(player - 1), town ? "town" : "a dungeon",
                 CoopWorldsApart() ? CoopWorldsSelf() : NetplayLocalPlayer(), CoopWorldsApart() ? " (apart)" : "", (long long) NetplayLinkTick());
    return start;
}

void TownAlliesApply(int player, int chara) {
    const int  p = player - 1;
    SoundOwner sound(player);

    if (p < 0 || p >= kMaxPlayers || chara < 0 || chara > 5) {
        return;
    }
    // The member another player holds is asked for (a trade), as the Select ring and the dungeon's page ask: apart across zones
    // (ApartAsk), together in the game's Yes/No popup (StepSwapQuestion, in a town too).
    int holder = -1;

    for (int other = 0; other < kMaxPlayers && holder < 0; other++) {
        holder = other != p && TownAlliesPlays(other) == chara ? other : -1;
    }
    if (CoopWorldsApart() ? chara == ApartOtherMember() : holder >= 0) {
        if (CoopWorldsApart() || (p <= 1 && holder <= 1)) {
            std::fprintf(stderr, "coop-allies: player %d asks for member %d (%s), player %d's, in town: a trade\n", player, chara, kNames[chara],
                         CoopWorldsApart() ? 3 - CoopWorldsSelf() : holder + 1);
            AskSwap(p, chara);
            return;
        }
        // (players past 2 have no trade question of their own yet: the members' refusal, said)
        std::fprintf(stderr, "coop-allies: player %d's pick of member %d refused: player %d holds it (no trade with them yet)\n", player, chara, holder + 1);
        MemberRefused(p, chara, MemberWhy::kPage);
        return;
    }
    if (!TownAlliesArmed(chara)) {
        std::fprintf(stderr, "coop-allies: player %d's pick of member %d refused: no weapon equipped\n", player, chara);
        MemberRefused(p, chara, MemberWhy::kPage);
        return;
    }
    const bool set = p == 1 ? SetPick(1, chara, MemberWhy::kPage) : SetMember(p, chara, MemberWhy::kPage);

    if (!set) {
        MemberRefused(p, chara, MemberWhy::kPage);
        return;
    }
    std::fprintf(stderr, "coop-allies: player %d plays member %d (%s) in town at link tick %lld\n", player, chara, kNames[chara], (long long) NetplayLinkTick());
}

namespace {
int MenuCharaChangeTaken(int wanted) {
    int player = g_player_chara;

    // coop-swapcombo part 2: apart, the other player's member is asked for across zones (AskSwap's ApartAsk), wherever this copy is:
    // in a town the partner is not loaded, and the pick went through as his own change (both players then on one member).
    if (Enabled() && UserStatus != nullptr && CoopWorldsApart() && wanted >= 0 && wanted == ApartOtherMember() && wanted != UserStatus->cur_chara &&
        !Mutated("apart_trade_refused")) {
        AskSwap(0, wanted);
        return -1;
    }

    if (!Enabled() || g_partner.loaded_chara < 0 || player < 0) {
        return wanted;
    }

    // coop-localmenu 3c: her local menu's pick, or his of her member, goes to every copy as a command (FullMenuLocalPick).
    if (FullMenuLocalPick(2, wanted) || (wanted == g_partner.loaded_chara && FullMenuLocalPick(1, wanted))) {
        UserStatus->cur_chara = (s8) player;
        return -1;
    }

    // (coop-wave2: "the menus player 2 holds" (menu_owner 2, her claim of retail's menu) are retired: her menu is her own local one,
    // whose pick is FullMenuLocalPick's above.)

    if (wanted == g_partner.loaded_chara && !Mutated("samechara")) {
        UserStatus->cur_chara = (s8) player;
        AskSwap(0, wanted);
        std::printf("coop: 1P's menu asks for the partner's %d; the player stays %d\n", wanted, player);
        return -1;
    }

    // coop-pt1111k: his own pick on a character-limited floor, checked as it is taken (BtMenuLoadChara, every copy on one tick):
    // one that leaves the floor's member unplayed is refused on his screen and he keeps his member.
    if (wanted != player && !ZonePickLegal(0, wanted)) {
        UserStatus->cur_chara = (s8) player;
        ZonePickRefused(0, wanted, "menu");
        return -1;
    }

    return wanted;
}
} // namespace

bool RevivePowderOn() {
    // coop-revive2: the base game's rule (no switch needed); DC_COOP_REVIVE_POWDER=0 takes the question away (tests only).
    static const char *env = TestEnv("DC_COOP_REVIVE_POWDER");
    static const bool  launcher = env == nullptr || *env != '0';

    return Enabled() && g_revive_host.value_or(launcher);
}

void RevivePowderAdopt(bool on) {
    g_revive_host = on;
}

bool ReviveFreeGetUp() {
    return dc::platform::ConfigGet().debug_mode || Mutated("getup_always");
}

void ReviveTestCross(int player) {
    if (player >= 1 && player <= kMaxPlayers) {
        g_revive_test_cross[player - 1] = true;
    }
}

int ReviveNoneFrames(int player) {
    return player >= 1 && player <= kMaxPlayers ? g_revive_none[player - 1] : 0;
}

void ReviveNoneTick() {
    for (int &frames : g_revive_none) {
        frames = std::max(0, frames - 1);
    }
}

namespace {
// The reviver's Cross this tick: linked, the press every copy agrees (the lockstep's edge, NetplayPlayerPressed); one copy alone,
// its pad's own edge.
bool ReviveCross(int player) {
    const int     k = player - 1;
    std::uint16_t buttons = Pad(k).buttons;
    const bool    edge = (buttons & ~g_revive_held[k] & kInputCross) != 0;
    bool          test = g_revive_test_cross[k];

    g_revive_held[k] = buttons;
    g_revive_test_cross[k] = false;
    return test || (NetplayLinkUp() ? NetplayPlayerPressed(player, kInputCross) : edge);
}
} // namespace

bool ReviveAsk(int reviver, int downed, bool near) {
    if (reviver < 1 || reviver > 2 || downed < 1 || downed > 2 || reviver == downed) { // (the question's two seats: g_prompt)
        return false;
    }

    const int  r = reviver - 1;
    const bool cross = ReviveCross(reviver);
    const bool mine = g_prompt.active && g_prompt.revive && g_prompt.asker == downed - 1;
    SoundOwner sound(reviver); // the question's sounds are the reviver's

    if (cross && CoopDebug()) { // coop-revivecross: what the Cross found
        std::printf("coop-debug: %dP's Cross over %dP: offered %d, question %d (revive %d); %s\n", reviver, downed, near ? 1 : 0, g_prompt.active ? 1 : 0,
                    g_prompt.revive ? 1 : 0, ReviveOfferTrace(reviver, downed));
    }

    if (!near) {
        if (mine) {
            std::printf("coop: revive question dropped\n");
            Close();
        }

        return false;
    }

    if (!mine) {
        // coop-revivecross: the press goes where the step that read it first sent it (one Cross, one action)
        const int word = cross ? ReviveCrossTaken(reviver) : -1;

        if (!cross || word == 0 || g_prompt.active || !RevivePowderOn()) {
            return false;
        }

        const int carried = RevivePowders(reviver);

        // No powder: the game's refuse and its box on the reviver's screen; nothing else happens.
        if (carried <= 0) {
            SndSePlay(MENU_SOUND_REFUSE, -1, 0);
            g_revive_none[r] = 150;
            std::printf("coop: %dP has no Revive Powder for %dP\n", reviver, downed);
            return false;
        }

        g_prompt.active = true;
        g_prompt.revive = true;
        g_prompt.asker = downed - 1;
        g_prompt.wanted = -1;
        g_prompt.spot = -1;
        g_prompt.ticks = kAnswerTicks;
        g_prompt.shown = true;
        g_prompt.held = Pad(r).buttons;
        g_prompt.cursor = 0;
        g_prompt.id = NetplayLinkTick(); // coop-wave2
        g_prompt.answer_due = 0;
        QuestionHandlerOnce();
        LockPlayer(reviver == 1);
        SndSePlay(MENU_SOUND_CURSOR, -1, 0);
        std::printf("coop: %dP asked: Use a Revive Powder? (%d carried)\n", reviver, carried);
        return false;
    }

    const auto yes = [&] {
        Close();

        // DC_COOP_MUTATE=revivepowder: yes spends nothing and gets nobody up (the twin).
        if (Mutated("revivepowder") || !SpendRevivePowder(reviver)) {
            SndSePlay(MENU_SOUND_REFUSE, -1, 0);
            std::printf("coop: %dP's Revive Powder not used\n", reviver);
            return false;
        }

        SndSePlay(MENU_SOUND_CONFIRM, -1, 0);
        std::printf("coop: %dP uses a Revive Powder\n", reviver);
        return true;
    };
    const auto decline = [&] {
        SndSePlay(MENU_SOUND_REFUSE, -1, 0);
        std::printf("coop: %dP declines the Revive Powder\n", reviver);
        Close();
    };
    // coop-wave2: linked together, the reviver's hand and answer are the reviver's copy's (QuestionOwn), its answer a command taken here
    // on every copy alike. coop-revive2 (requests never expire): no five seconds; it waits for the answer.
    const bool   local = QuestionLocal();
    QuestionOwn &own = OwnQuestion();
    int         &cursor = local ? own.cursor : g_prompt.cursor;
    const int    self = NetplayLocalPlayer() - 1;

    if (local && g_prompt.answer_due != 0) {
        const int answer = g_prompt.answer_due;

        g_prompt.answer_due = 0;
        if (answer == kAnswerYes) {
            std::fprintf(stderr, "coop: revive question: player %d's YES taken at link tick %lld (answered on its own copy)\n", reviver, (long long) NetplayLinkTick());
            return yes();
        }
        std::fprintf(stderr, "coop: revive question: player %d's NO taken at link tick %lld (answered on its own copy)\n", reviver, (long long) NetplayLinkTick());
        decline();
        return false;
    }

    std::uint16_t buttons = Pad(r).buttons;

    // Test hook (DC_COOP_REVIVETEST, coop_party.cpp): the reviver answers a second after it is asked, Circle for the
    // decline case, else Cross. Ticks of the question, so linked copies answer alike.
    static const char *revive_test = TestEnv("DC_COOP_REVIVETEST");

    if (g_prompt.ticks > 0) {
        g_prompt.ticks--; // (only the hook's clock now)
    }
    if (revive_test != nullptr && g_prompt.ticks == kAnswerTicks - 60) {
        buttons |= std::strcmp(revive_test, "decline") == 0 ? kInputCircle : kInputCross;
        std::printf("coop-debug: revivetest: %dP answers %s\n", reviver, std::strcmp(revive_test, "decline") == 0 ? "Circle" : "Cross");
    }

    std::uint16_t pressed = buttons & ~g_prompt.held;

    g_prompt.held = buttons;
    if (local && (self != r || own.answered)) {
        pressed = 0;
    }
    MoveCursor(pressed, cursor);

    if (local) {
        if ((pressed & kInputCross) != 0 && cursor == 0) {
            QuestionAnswer(kAnswerYes);
        } else if ((pressed & (kInputCircle | kInputCross)) != 0) {
            QuestionAnswer(kAnswerNo);
        }
        return false;
    }

    if ((pressed & kInputCross) != 0 && g_prompt.cursor == 0) {
        return yes();
    }

    if ((pressed & (kInputCircle | kInputCross)) != 0) {
        decline();
    }

    return false;
}

void PromptResetForSession() {
    Close();
    g_prompt = Prompt{};
    g_player_chara = -1;
    std::fill(std::begin(g_revive_test_cross), std::end(g_revive_test_cross), false);
    std::fill(std::begin(g_revive_none), std::end(g_revive_none), 0);
}

} // namespace coop

using namespace coop;

// coop-townwheel (Jep, 2026-11-02: "the white text prompts that we're trying to eliminate"): the asked player's copy shows the
// question in the game's Yes/No popup, the asker's its waiting box (coop_useprompt.cpp DrawSwapQuestion).
void CoopDrawPrompt() {
    DrawSwapQuestion();
}
