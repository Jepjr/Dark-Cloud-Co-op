// coop-actorswap: the player table and the actor swap (coop_actor.hpp says what and why).

#include "coop/game_ns.hpp"
#include "coop/coop_opener.hpp" // coop-opt7
#include "coop/coop_actor.hpp"

#include <libvu0.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "character.hpp"
#include "dun/gameloop.hpp"
#include "gamepad.hpp"
#include "gamepad.hpp"
#include "btmisc.hpp"
#include "dataread.hpp"
#include "frame.hpp"
#include "itembombeffect.hpp"
#include "itemdata.hpp" // coop-feathersync: ITEM_DUNGEON_START
#include "mainitemmodel.hpp"
#include "shot_freefuncs.hpp"
#include "dngmessageman.hpp"
#include "menu_draw.hpp"
#include "menu_dungeon.hpp"
#include "savedata.hpp"
#include "snd.hpp"
#include "sound_effect.h"
#include "userstatus.hpp"

#include "coop/coop.hpp"
#include "coop/coop_audio.hpp"
#include "coop/coop_core.hpp"
#include "coop/coop_fullmenu.hpp" // coop-localmenu: the menus each player runs
#include "coop/coop_townmenu.hpp"
#include "coop/coop_inventory.hpp"
#include "coop/coop_message.hpp"
#include "coop/coop_present.hpp" // coop-router: the swapped player's presentation
#include "coop/coop_party.hpp"
#include "coop/netplay.hpp"
#include "coop/netplay_cmd.hpp"
#include "coop/snapshot_state.hpp"

namespace dc::game {
class CFrameVu1;
extern CFrameVu1 *CharaFrame;
} // namespace dc::game

namespace coop {

namespace {

// An entry's own storage for what player 2 keeps in other modules (her body in g_partner, her bag in coop_inventory):
// the players past 2 have it here until their own modules exist (the six-player plan).
struct Player {
    CGamePad      pad{};
    ITEM_PACK     pack{};
    std::int32_t  active_item_vol[3] = {};
    std::uint16_t gilda = 0;
    int           chara = -1;
    // The item models this entry loaded into retail's item model pool for its throws (activeItem.models: model slot, item).
    int           throw_model[3] = {-1, -1, -1};
    int           throw_item[3] = {-1, -1, -1};
    std::uint8_t  in_menu = 0; // coop-localmenu: the menus open (ActorMenuKind bits), as the commands set them
};

Player g_players[kMaxPlayers];

// The swap under way: who, and what the exchange keeps to put back exactly.
struct Swapped {
    int           index = -1;
    CCharacter   *body = nullptr; // the entry's body CharaMain was exchanged with (null: none)
    CFrameVu1    *chara_frame = nullptr;
    s32           unit_now = 0;
    sceVu0FVECTOR partner_pos = {};
    sceVu0FVECTOR partner_heading = {};
    bool          bags = false;
    // A stand-in member (one the party has not joined: its life is g_partner.stand_in_life's, not the status's) has its
    // life in the status for the swap, as retail reads it; the status's own values are put back after.
    int           stand_in = -1;
    s16           status_hp = 0, status_max_hp = 0, status_next_hp = 0, status_life_step = 0, in_hp = 0;
    int           stand_in_life = -1;
    // The player's weapon as retail's actions read it (NowWeaponHave: the equipped weapon with its attachments counted,
    // SetWeaponAttachStatus), made for the swapped-in member and put back after.
    WEAPON_HAVE   weapon_have{};
    bool          weapon_kept = false;
    // The dungeon's message window (DngMessMan) as it stood: one set inside the swap is the swapped player's (DngMesBy).
    s32           dng_message = 0, dng_timer = 0;
    std::optional<SoundOwner> sound;
    std::optional<MessageBy>  message;
} g_swap;

bool Leak() {
    static const bool leak = Mutated("actorswap_leak");
    return leak;
}

CCharacter *Body(int index) {
    if (index == 1 && g_partner.chara != nullptr && g_partner.loaded_chara >= 0) {
        return g_partner.chara;
    }

    return nullptr;
}

// The body's own pointers that may point into the body itself: cloth (it starts at cloth_buf) and each motion set's
// (motion[0] and shadow_motion[0] are motion_type and shadow_motion_type, the others motion_storage's; found by the
// self-test's scan, 2026-10-30). A body swap rebases them to the object they now sit in.
template <typename F> void ForOwnPointers(CCharacter &c, F &&visit) {
    visit(reinterpret_cast<void **>(&c.cloth));
    for (int i = 0; i < CHARA_MOTION_MAX; i++) {
        visit(reinterpret_cast<void **>(&c.motion[i]));
        visit(reinterpret_cast<void **>(&c.shadow_motion[i]));
    }
}

void Rebase(CCharacter &now, const CCharacter *was) {
    const auto from = reinterpret_cast<std::uintptr_t>(was);
    const auto to = reinterpret_cast<std::uintptr_t>(&now);

    ForOwnPointers(now, [&](void **field) {
        const auto value = reinterpret_cast<std::uintptr_t>(*field);

        if (value >= from && value < from + sizeof(CCharacter)) {
            *field = reinterpret_cast<void *>(value - from + to);
        }
    });
}

// CharaMain's bytes and another body's, exchanged, each body's own pointers rebased.
void ExchangeBodies(CCharacter &a, CCharacter &b) {
    alignas(CCharacter) static unsigned char held[sizeof(CCharacter)];
    auto *pa = reinterpret_cast<unsigned char *>(&a);
    auto *pb = reinterpret_cast<unsigned char *>(&b);

    std::memcpy(held, pa, sizeof held);
    std::memcpy(pa, pb, sizeof held);
    std::memcpy(pb, held, sizeof held);
    Rebase(a, &b);
    Rebase(b, &a);
}

// Words in a body that point inside the body itself, other than the ones the swap rebases: a body swap would leave them
// pointing at the other object. The self-test prints how many (0 expected; ForOwnPointers grows by what it finds).
int SelfPointers(const CCharacter &c) {
    const auto  from = reinterpret_cast<std::uintptr_t>(&c);
    const auto  to = from + sizeof(CCharacter);
    const auto *words = reinterpret_cast<const std::uintptr_t *>(&c);
    int         found = 0;
    std::vector<const void *> rebased;

    ForOwnPointers(const_cast<CCharacter &>(c), [&](void **field) { rebased.push_back(field); });
    for (std::size_t i = 0; i < sizeof(CCharacter) / sizeof(std::uintptr_t); i++) {
        if (std::find(rebased.begin(), rebased.end(), &words[i]) != rebased.end()) {
            continue;
        }
        if (words[i] >= from && words[i] < to) {
            if (found == 0) {
                std::fprintf(stderr, "actor: body self-pointer at +0x%zx (to +0x%zx)\n", i * sizeof(std::uintptr_t), (std::size_t) (words[i] - from));
            }
            found++;
        }
    }

    return found;
}

ITEM_PACK *Pack(int index) {
    return index == 1 ? &PartnerPack() : &g_players[index].pack;
}

std::int32_t *ActiveVol(int index) {
    return index == 1 ? PartnerActiveItemVol() : g_players[index].active_item_vol;
}

// The member entry index plays, and where it is kept (player 2's in g_partner, as an int).
void ExchangeChara(int index) {
    if (UserStatus == nullptr) {
        return;
    }

    int &kept = index == 1 ? g_partner.loaded_chara : g_players[index].chara;

    if (kept < 0) {
        return;
    }

    const int resident = UserStatus->cur_chara;

    UserStatus->cur_chara = (s8) kept;
    kept = resident;
}

void ExchangePurse(int index) {
    const int resident = UserStatus->money;

    if (index == 1) {
        UserStatus->money = (u16) PartnerGilda();
        PartnerSetGilda(resident);
    } else {
        UserStatus->money = g_players[index].gilda;
        g_players[index].gilda = (std::uint16_t) resident;
    }
}

struct Waiting {
    std::string sequence;
    int         holder = -1;
    int         waiter = -1;
};

std::vector<Waiting> g_sequences;

} // namespace

int ActorNumber(int index) {
    return index + 1;
}

bool ActorPresent(int index) {
    if (index == 0) {
        return true;
    }

    return index == 1 && Enabled() && g_partner.chara != nullptr && g_partner.loaded_chara >= 0;
}

int ActorChara(int index) {
    if (index < 0 || index >= kMaxPlayers) {
        return -1;
    }

    const auto kept = [](int i) { return i == 1 ? g_partner.loaded_chara : g_players[i].chara; };
    const int  status = UserStatus != nullptr ? UserStatus->cur_chara : -1;

    // Swapped in, an entry's member is in the globals and the resident's is kept in that entry's place.
    if (g_swap.index > 0 && index == g_swap.index) {
        return status;
    }
    if (index == 0) {
        return g_swap.index > 0 ? kept(g_swap.index) : status;
    }

    return kept(index);
}

int ActorLocalIndex() {
    const int local = NetplayLocalPlayer();

    return local > 0 && !Mutated("players_local_fixed") ? local - 1 : 0;
}

void ActorTableTrace() {
    static const bool on = TestEnvOn("DC_COOP_TEST_PLAYERS");
    static bool       said = false;

    COOP_TEST_TRACE_RESET(said, false);
    if (!on || said || !ActorPresent(1)) {
        return;
    }

    said = true;
    std::string entries;

    for (int i = 0; i < kMaxPlayers; i++) {
        entries += " [" + std::to_string(ActorNumber(i)) + " " + (ActorPresent(i) ? "present" : "absent") + " " + std::to_string(ActorChara(i)) + "]";
    }
    std::fprintf(stderr, "players: local index %d;%s\n", ActorLocalIndex(), entries.c_str());
    std::fflush(stderr);
}

// ---- coop-localmenu stage 2: the in-menu bits (coop_actor.hpp) -------------------------------------------------------------

namespace {

// Per copy, never state: the kinds this copy last sent for each player it runs, and the link tick it sent them on.
std::uint8_t g_menu_sent[kMaxPlayers] = {};
std::int64_t g_menu_sent_at[kMaxPlayers] = {};
// A command sent waits its input delay before it applies; one not applied after this (a state load put other bits) goes again.
constexpr std::int64_t kMenuResendTicks = 120;
bool         g_menu_handler = false;

// The menus a player's own engine runs on this copy now.
std::uint8_t MenusRunning(int player) {
    return (std::uint8_t) ((FullMenuRunsFor(player) ? kActorMenuFull : 0) | (CoopTownMenuRunsFor(player) ? kActorMenuTown : 0) |
                           (FullMenuRunsLocalFor(player) || CoopTownMenuRunsLocalHere(player) ? kActorMenuLocal : 0));
}

// kCmdMenu: a = the kinds open, b = the player (1 up). Linked, a copy speaks for its own player alone.
void ApplyMenu(int sender, const NetCommand &command, std::int64_t tick) {
    const int player = command.b;

    if (player < 1 || player > kMaxPlayers || (NetplayLinkUp() && player != sender)) {
        std::fprintf(stderr, "actor: menu command for player %d from player %d refused at tick %lld\n", player, sender, (long long) tick);
        return;
    }

    Player &entry = g_players[player - 1];

    if (entry.in_menu != command.a) {
        const bool closed_local = (entry.in_menu & kActorMenuLocal) != 0 && (command.a & kActorMenuLocal) == 0;
        const bool was_town = (entry.in_menu & kActorMenuTown) != 0; // coop-stage4: a town's local menu closes the town's way

        std::fprintf(stderr, "actor: player %d's menus %d -> %d at tick %lld\n", player, (int) entry.in_menu, (int) command.a, (long long) tick);
        entry.in_menu = command.a;
        if (closed_local && was_town) {
            CoopTownMenuClosedByCommand(player, (int) command.c, (long long) tick);
        } else if (closed_local) {
            FullMenuClosedByCommand(player, (int) command.c);
        }
    }
}

} // namespace

// coop-soakfix5 (the soak's leave seed 1167: OUT OF SYNC at tick 60 after a dropped player's new copy rejoined by
// the host's resync, parts "in menu" 00000006 against 00000000, menus, stages): the rejoin's link runs link ticks 1-33 before its
// resync on two copies that are not alike, and on hers his pad opened his town menu at tick 33 (ActorMenuOpenHere: his bits town
// and local on her copy alone; his own copy stood elsewhere). The resync's session reset closed every menu engine on both copies
// but left the bits, which no command could clear: only his copy speaks for him, and its engine and bits were both closed. Her
// copy's town then ran under his menu. The bits go with the engines: none, and nothing sent, so a menu still running (a merge's
// carried one, its reopen) sends its bits again and every copy takes them on one tick. DC_COOP_MUTATE=resync_menubits_kept
// (twin): each copy keeps its bits through the resync, as before.
void ActorMenusResetForResync() {
    if (Mutated("resync_menubits_kept")) {
        return;
    }
    for (int i = 0; i < kMaxPlayers; i++) {
        if (g_players[i].in_menu != 0) {
            std::fprintf(stderr, "actor: player %d's menus %d -> 0 at the resync (tick %lld)\n", ActorNumber(i), (int) g_players[i].in_menu,
                         (long long) NetplayLinkTick());
        }
        g_players[i].in_menu = 0;
        g_menu_sent[i] = 0;
        g_menu_sent_at[i] = 0;
    }
}

void ActorMenuOpenHere(int player, std::uint8_t kinds) {
    if (player < 1 || player > kMaxPlayers) {
        return;
    }

    Player &entry = g_players[player - 1];

    std::fprintf(stderr, "actor: player %d's menus %d -> %d at tick %lld\n", player, (int) entry.in_menu, (int) (entry.in_menu | kinds),
                 (long long) NetplayLinkTick());
    entry.in_menu |= kinds;
}

bool PlayerInMenu(int player, std::uint8_t kinds) {
    return player >= 1 && player <= kMaxPlayers && (g_players[player - 1].in_menu & kinds) != 0;
}

std::uint32_t ActorMenuBits() {
    std::uint32_t bits = 0;

    for (int i = 0; i < kMaxPlayers; i++) {
        bits |= (std::uint32_t) (g_players[i].in_menu & 0xF) << (4 * i);
    }
    return bits;
}

void ActorMenuSync() {
    if (!g_menu_handler) {
        NetplaySetCommandHandler(kCmdMenu, ApplyMenu);
        PresentInstall(); // coop-opt7: kCmdPresent, on every copy from its first tick
        if (!Mutated("menu_commands_at_open")) { // (twin: at a menu's opening on this copy alone, as before)
            FullMenuCommandHandlers();
        }
        g_menu_handler = true;
    }

    const int local = NetplayLocalPlayer();

    for (int i = 0; i < kMaxPlayers; i++) {
        const int player = ActorNumber(i);

        // Alone, a player who left (the link down with their menu open) has none.
        if (local > 0 && player != local) {
            continue;
        }

        const std::uint8_t now = local > 0 || ActorPresent(i) ? MenusRunning(player) : 0;
        const std::int64_t tick = NetplayLinkTick();

        // A state load (a merge, a resync) can bring bits other than the last sent: the command goes again when the bits
        // still disagree with the engine once the last has had its time to apply.
        if (now == g_menu_sent[i] && (now == g_players[i].in_menu || (tick >= g_menu_sent_at[i] && tick < g_menu_sent_at[i] + kMenuResendTicks))) {
            continue;
        }

        const bool town_closes = (g_menu_sent[i] & kActorMenuTown) != 0 && (now & kActorMenuTown) == 0; // coop-stage4

        g_menu_sent[i] = now;
        g_menu_sent_at[i] = tick;
        if (Mutated("menubit_here")) {
            std::fprintf(stderr, "actor: player %d's menus %d -> %d at tick %lld\n", player, (int) g_players[i].in_menu, (int) now, (long long) tick);
            g_players[i].in_menu = now;
            continue;
        }

        NetCommand command;

        command.kind = kCmdMenu;
        command.a = now;
        command.b = (std::uint16_t) player;
        command.c = (now & kActorMenuLocal) != 0 ? 0u : town_closes ? (std::uint32_t) CoopTownMenuCloseJump(player) : (std::uint32_t) FullMenuCloseJump(player);
        NetplayQueueCommand(command);
    }
}

int ActorSwapped() {
    return g_swap.index;
}

int ActorPresenting() {
    return g_swap.index >= 0 ? ActorNumber(g_swap.index) : 0;
}

int ActorOther(int index) {
    return index == 0 ? 1 : 0;
}

void ActorFeedPad(int index, const InputPadState &now, const InputPadState &before) {
    if (index < 0 || index >= kMaxPlayers) {
        return;
    }

    PadUpdateFrom(g_players[index].pad, before);
    PadUpdateFrom(g_players[index].pad, now);
}

bool ActorSwapIn(int index, const char *why) {
    if (index < 0 || index >= kMaxPlayers || g_swap.index >= 0 || UserStatus == nullptr) {
        std::fprintf(stderr, "actor: swap of player %d refused (%s): %s\n", ActorNumber(index), why,
                     g_swap.index >= 0 ? "another swap under way" : "no player status");
        return false;
    }
    if (index == 0) {
        return true;
    }

    g_swap.index = index;
    g_swap.body = Body(index);
    g_swap.chara_frame = CharaFrame;
    g_swap.unit_now = nowUnitNow;
    sceVu0CopyVector(g_swap.partner_pos, g_partner.pos);
    sceVu0CopyVector(g_swap.partner_heading, g_partner.heading);
    g_swap.bags = OwnBags();

    // The other one: the partner slot holds the resident (its place and facing as the resident's body has them).
    if (g_swap.body != nullptr) {
        sceVu0CopyVector(g_partner.pos, CharaMain.pos);
        g_partner.pos[3] = 1.0f;
        ExchangeBodies(CharaMain, *g_swap.body);
        CharaFrame = reinterpret_cast<CFrameVu1 *>(CharaMain.frame);
    }

    ExchangeChara(index);
    nowUnitNow = UserStatus->cur_chara;

    if (const int chara = UserStatus->cur_chara; index == 1 && chara >= 0 && chara < 6 && !OwnLife(chara)) {
        g_swap.stand_in = chara;
        g_swap.status_hp = UserStatus->hp[chara];
        g_swap.status_max_hp = UserStatus->max_hp[chara];
        g_swap.status_next_hp = UserStatus->next_hp[chara];
        g_swap.status_life_step = UserStatus->life_step[chara];
        g_swap.stand_in_life = g_partner.stand_in_life[chara];
        // As coop_party's MaxLife and Life give a stand-in's (they read g_partner.loaded_chara, which holds the resident's now).
        const int most = UserStatus->max_hp[chara] > 0 ? UserStatus->max_hp[chara] : 70;
        const int life = g_swap.stand_in_life < 0 ? most : std::min(g_swap.stand_in_life, most);
        UserStatus->max_hp[chara] = (s16) most;
        UserStatus->hp[chara] = (s16) life;
        UserStatus->next_hp[chara] = (s16) life;
        UserStatus->life_step[chara] = 0;
        g_swap.in_hp = (s16) life;
    }

    if (g_swap.bags) {
        std::swap(UserStatus->item_pack, *Pack(index));
        std::swap_ranges(UserStatus->active_item_vol, UserStatus->active_item_vol + 3, ActiveVol(index));
        ExchangePurse(index);
    }

    std::swap(GamePad, g_players[index].pad);

    g_swap.weapon_kept = NowWeaponHave != nullptr && SaveData != nullptr;
    if (g_swap.weapon_kept) {
        g_swap.weapon_have = *NowWeaponHave;
        SetWeaponAttachStatus(NowWeaponHave);
    }
    g_swap.dng_message = DngMessMan.message;
    g_swap.dng_timer = DngMessMan.timer;

    g_swap.sound.emplace(ActorNumber(index));
    g_swap.message.emplace(ActorNumber(index));
    return true;
}

void ActorSwapOut() {
    const int index = g_swap.index;

    if (index < 0) {
        return;
    }

    g_swap.message.reset();
    g_swap.sound.reset();

    if (DngMessMan.message != g_swap.dng_message || DngMessMan.timer != g_swap.dng_timer) {
        DngMesBy(ActorNumber(index));
    }
    if (g_swap.weapon_kept) {
        *NowWeaponHave = g_swap.weapon_have;
        g_swap.weapon_kept = false;
    }

    std::swap(GamePad, g_players[index].pad);

    if (g_swap.bags) {
        ExchangePurse(index);
        std::swap_ranges(UserStatus->active_item_vol, UserStatus->active_item_vol + 3, ActiveVol(index));
        std::swap(UserStatus->item_pack, *Pack(index));
    }

    if (const int chara = g_swap.stand_in; chara >= 0) {
        // Her life as the action left it (a gauge retail would step there over the next ticks: its end at once), or exactly
        // what it was when nothing changed it.
        const s16 hp = UserStatus->life_step[chara] != 0 ? UserStatus->next_hp[chara] : UserStatus->hp[chara];

        g_partner.stand_in_life[chara] = hp == g_swap.in_hp ? g_swap.stand_in_life : hp;
        UserStatus->hp[chara] = g_swap.status_hp;
        UserStatus->max_hp[chara] = g_swap.status_max_hp;
        UserStatus->next_hp[chara] = g_swap.status_next_hp;
        UserStatus->life_step[chara] = g_swap.status_life_step;
        g_swap.stand_in = -1;
    }

    if (!Leak()) {
        ExchangeChara(index);
    }

    nowUnitNow = g_swap.unit_now;

    if (g_swap.body != nullptr) {
        ExchangeBodies(CharaMain, *g_swap.body);
    }

    CharaFrame = g_swap.chara_frame;
    sceVu0CopyVector(g_partner.pos, g_swap.partner_pos);
    sceVu0CopyVector(g_partner.heading, g_swap.partner_heading);
    g_swap.index = -1;
    g_swap.body = nullptr;
}

bool ActorPorted(const char *action) {
    static const bool off = TestEnvIs("DC_COOP_ACTORSWAP", "off") || Mutated("actorswap_off");

    return !off && !Mutated(("actorswap_" + std::string(action) + "_off").c_str());
}

namespace {

// A loaded model of item in retail's item model pool for entry index's throw: one already there (player 1's quick-use slot,
// or this entry's earlier load) is shared, else it is loaded from the disc as BtSetActiveItemModel loads player 1's. Each
// entry keeps up to three; one for an item it no longer carries is let go first. -1 when the pool has no room.
int ThrowModel(int index, int item) {
    CMainItemModel *pool = activeItem.models;
    Player         &p = g_players[index];

    if (pool == nullptr) {
        return -1;
    }

    const auto loaded = [&](int no, int of) {
        return no >= 0 && no < 16 && pool->model[no] == ITEM_MODEL_LOADED && pool->model_cash[no] >= 0 && pool->cash_item[pool->model_cash[no]] == of;
    };

    for (int i = 0; i < 3; i++) {
        if (p.throw_item[i] >= 0 && !loaded(p.throw_model[i], p.throw_item[i])) { // the floor's pool was made afresh
            p.throw_model[i] = p.throw_item[i] = -1;
        }
    }
    for (int no = 0; no < 16; no++) {
        if (pool->model[no] == ITEM_MODEL_LOADED && loaded(no, item)) {
            return no;
        }
    }

    // Room: let go of this entry's models for items its pack no longer holds (the pack is swapped in: it is the entry's).
    for (int i = 0; i < 3; i++) {
        if (p.throw_item[i] < 0) {
            continue;
        }

        bool carried = false;

        for (int s = 0; s < UserStatus->item_pack.num && s < 103 && !carried; s++) {
            carried = UserStatus->item_pack.item[s] == p.throw_item[i];
        }
        for (int s = 0; s < 3 && !carried; s++) {
            carried = UserStatus->item_pack.quick_item_slot[s] == p.throw_item[i];
        }
        if (!carried) {
            pool->DeleteModel(p.throw_model[i]);
            p.throw_model[i] = p.throw_item[i] = -1;
        }
    }

    int free = -1;

    for (int i = 0; i < 3 && free < 0; i++) {
        if (p.throw_item[i] < 0) {
            free = i;
        }
    }
    if (free < 0 || pool->GetFreeCashNo() < 0 || pool->GetFreeModelNo() < 0) {
        std::fprintf(stderr, "actor: player %d's throw of item %d: no room in the item model pool\n", ActorNumber(index), item);
        return -1;
    }

    static std::vector<u_int> buffer(256 * 1024); // 1 MB: the item models and textures are a few KB
    char model_path[64], texture_path[64];
    int  model_size = 0, texture_size = 0;

    BtGetItemNamePath(model_path, texture_path, item);
    LoadFile(model_path, buffer.data(), &model_size);
    model_size = (u_int) (((model_size >> 6) + 1) << 6) >> 2;
    LoadFile(texture_path, &buffer[model_size], &texture_size);

    const int no = pool->SetCashModel(item, buffer.data(), &buffer[model_size], texture_size);

    p.throw_model[free] = no;
    p.throw_item[free] = item;
    std::fprintf(stderr, "actor: player %d's item %d model loaded (%s, slot %d)\n", ActorNumber(index), item, model_path, no);
    return no;
}

} // namespace

// coop-feathersync (Jep's pack 8 playtest, 11-04: "there was a desync on this dungeon level when P2 opened a mimic"; both of
// that floor's desyncs were player 1's place parting a second after a local menu, while his Square ran): retail's Items page,
// as it closes, sets the items the quick-use slots run (activeItem.item: a Dran's Feather there boosts his Square run and
// spends a use each frame) from the menu's own view of the slots (DngActItemModelBuild). A local menu runs on its owner's copy
// alone, so that copy alone took them: his menu gave his copy the feather the cheat had put in his slot; hers (her bag swapped
// in) gave her copy her slots as his. Each world step, on every copy, the slots run what the game's pack holds (the state every
// copy agrees on), a changed slot's model loaded as the floor's start loads it (BtSetActiveItemModel). Logged on a change.
// DC_COOP_MUTATE=activeitem_local (the twin): as before, the menu's copy alone.
void ActorActiveItemsAgree() {
    CMainItemModel *pool = activeItem.models;

    if (UserStatus == nullptr || pool == nullptr) {
        return;
    }

    // Test trace (DC_COOP_TEST_FEATHER_TRACE, x-feather-run): slot 1's uses left as they change (a feather spends one a frame).
    static const bool trace = TestEnvOn("DC_COOP_TEST_FEATHER_TRACE");
    static int        left = -1;

    if (trace && UserStatus->active_item_vol[0] != left) {
        left = UserStatus->active_item_vol[0];
        std::fprintf(stderr, "coop-test: quick slot 1 (item %d, runs %d) has %d uses left at link tick %lld\n",
                     (int) UserStatus->item_pack.quick_item_slot[0], (int) activeItem.item[1], left, (long long) NetplayLinkTick());
    }
    if (Mutated("activeitem_local")) {
        return;
    }

    for (int slot = 1; slot <= 3; slot++) {
        const int held = UserStatus->item_pack.quick_item_slot[slot - 1];
        const int want = held >= ITEM_DUNGEON_START ? held : -1;
        const int was = activeItem.item[slot];

        if (was == want) {
            continue;
        }

        const int  no = activeItem.model[slot];
        const bool kept = want >= 0 && no >= 0 && no < 16 && pool->model[no] == ITEM_MODEL_LOADED && pool->model_cash[no] >= 0 &&
                          pool->cash_item[pool->model_cash[no]] == want;

        if (!kept && no != -1) {
            pool->DeleteModel(no);
            activeItem.model[slot] = -1;
        }
        if (!kept && want >= 0) {
            static std::vector<u_int> buffer(256 * 1024); // (as ThrowModel's: the item models and textures are a few KB)
            char model_path[64], texture_path[64];
            int  model_size = 0, texture_size = 0;

            BtGetItemNamePath(model_path, texture_path, want);
            LoadFile(model_path, buffer.data(), &model_size);
            model_size = (u_int) (((model_size >> 6) + 1) << 6) >> 2;
            LoadFile(texture_path, &buffer[model_size], &texture_size);
            activeItem.model[slot] = pool->SetCashModel(want, buffer.data(), &buffer[model_size], texture_size);
            activeItem.model[slot + 4] = 0;
        }
        activeItem.item[slot] = want;
        std::fprintf(stderr, "coop-inv: quick slot %d runs item %d on this copy (was %d), as the pack holds it (model %d)\n", slot, want, was,
                     activeItem.model[slot]);
    }
}

int ActorThrowItem(int index, int item, const float *to, bool locked) {
    // coop-router: the thrower's refusal is the thrower's; the throw itself is placed at the hand (below). MayStep: the throw runs
    // alike on every copy, in the step.
    present::Scope thrower(present::For{ActorNumber(index)}, COOP_PRESENT_HERE);
    thrower.MayStep();
    ActorSwap      swap(index, "throw");

    if (index != 0 && !swap.in()) {
        return -1;
    }

    const int source = ThrowModel(index, item);

    if (source < 0) {
        SndSePlay(MENU_SOUND_REFUSE, -1, 0);
        return 0;
    }

    // From the swapped-in body's hand, as BattleActionThlow throws: a lob to the point, or straight at a locked target.
    sceVu0FVECTOR from = {CharaMain.pos[0], CharaMain.pos[1] + 12.0f, CharaMain.pos[2], 1.0f};
    sceVu0FVECTOR vector;
    float         offset[4] = {0.0f, 0.0f, 0.0f, 1.0f};

    if (CFrame *hand = CharaMain.frame != nullptr ? CharaMain.frame->SearchFrame("item") : nullptr; hand != nullptr) {
        hand->GetWorldPosition(from, offset);
    }

    if (locked) {
        vector[0] = to[0] - from[0];
        vector[1] = 5.0f + (to[1] - from[1]);
        vector[2] = to[2] - from[2];
        vector[3] = 0.0f;
        sceVu0Normalize(vector, vector);
        sceVu0ScaleVectorXYZ(vector, vector, 4.5f);
    } else {
        sceVu0FVECTOR at = {to[0], to[1], to[2], 1.0f};

        ParabolicInitialVector(vector, from, at, 0.12f, 20.0f);
        sceVu0ScaleVectorXYZ(vector, vector, 0.8f);
    }

    const int thrown = activeItem.models->SetThrowModel(source, from, vector);

    {
        const present::Scope placed(present::At{from}, COOP_PRESENT_HERE); // heard by distance from the hand, as hers were
        SndSePlay(SE_THROW, -1, 0);
    }
    std::fprintf(stderr, "actor: player %d threw item %d (model slot %d) from (%.0f %.0f %.0f)%s\n", ActorNumber(index), item, thrown, from[0], from[1],
                 from[2], locked ? " at its lock" : "");
    return 1;
}

int ActorUseItem(int index, int item, bool quick) {
    // coop-router: the user's own sounds (the refusal, the recovery, retail's power-up inside usedActiveItem). MayStep: the use runs
    // alike on every copy, in the step.
    present::Scope user(present::For{ActorNumber(index)}, COOP_PRESENT_HERE);
    user.MayStep();
    ActorSwap      swap(index, "item");

    if (index != 0 && !swap.in()) {
        return -1;
    }

    s16      &slot = UserStatus->item_pack.quick_item_slot[0];
    const s16 kept = slot;

    slot = (s16) item;
    const bool used = checkItemUsed(0) != 0;
    slot = kept;

    if (!used) {
        if (!quick || Mutated("quick_refuse_sound")) { // coop-playfix: the field's Square refuses quietly, as retail's
            SndSePlay(MENU_SOUND_REFUSE, -1, 0);
        }
        return 0;
    }

    usedActiveItem(UserStatus, item);
    SndSePlay(SE_CHARA_RECOVER, -1, 0);
    return 1;
}

bool ActorSequenceTake(const char *sequence, int index) {
    for (Waiting &w : g_sequences) {
        if (w.sequence != sequence) {
            continue;
        }
        if (w.holder == index) {
            return true;
        }
        if (w.waiter != index) {
            w.waiter = index;
            std::fprintf(stderr, "actor: %s serializes: player %d waits for player %d\n", sequence, ActorNumber(index), ActorNumber(w.holder));
        }
        return false;
    }

    g_sequences.push_back({sequence, index, -1});
    return true;
}

void ActorSequenceRelease(const char *sequence, int index) {
    for (std::size_t i = 0; i < g_sequences.size(); i++) {
        if (g_sequences[i].sequence == sequence && g_sequences[i].holder == index) {
            if (g_sequences[i].waiter >= 0) {
                std::fprintf(stderr, "actor: %s free: player %d's wait ends\n", sequence, ActorNumber(g_sequences[i].waiter));
            }
            g_sequences.erase(g_sequences.begin() + (std::ptrdiff_t) i);
            return;
        }
    }
}

void ActorSequencesReset() {
    g_sequences.clear();
}

// The swap-roundtrip self-test (DC_COOP_TEST_SWAP_ROUNDTRIP=1,2,6): for each player number, the state set before a swap
// in and after its swap out, nothing run between, page-diffed; and what the globals held while it was in.
void ActorTestTick() {
    static const char *spec = TestEnv("DC_COOP_TEST_SWAP_ROUNDTRIP");
    static bool        done = false;
    static int         up = 0;

    if (spec == nullptr || done || !ActorPresent(1)) {
        return;
    }
    // The partner up and stepped for a second first, so its body and the floor are settled.
    if (++up < 60) {
        return;
    }

    done = true;

    const char *at = spec;

    while (*at != '\0') {
        char     *end = nullptr;
        const int number = (int) std::strtol(at, &end, 10);

        if (end == at) {
            break;
        }
        at = *end == ',' ? end + 1 : end;

        const int index = number - 1;

        if (index < 0 || index >= kMaxPlayers) {
            std::fprintf(stderr, "actor: swap roundtrip player %d: no such entry\n", number);
            continue;
        }

        std::vector<unsigned char> before, after, list, need_own;
        std::string                why;

        if (!SnapshotTakeBytes(before, why, {})) {
            std::fprintf(stderr, "actor: swap roundtrip player %d: state set not taken (%s)\n", number, why.c_str());
            continue;
        }

        const int   resident_chara = UserStatus->cur_chara;
        const float resident_pos[3] = {CharaMain.pos[0], CharaMain.pos[1], CharaMain.pos[2]};
        const int   own_chara = ActorChara(index);
        CCharacter *own_body = Body(index);
        const float own_pos[3] = {own_body != nullptr ? own_body->pos[0] : 0.0f, own_body != nullptr ? own_body->pos[1] : 0.0f,
                                  own_body != nullptr ? own_body->pos[2] : 0.0f};
        const int   self_pointers = SelfPointers(CharaMain) + (own_body != nullptr ? SelfPointers(*own_body) : 0);

        if (!ActorSwapIn(index, "roundtrip test")) {
            continue;
        }

        // While in: the globals hold the entry's actor, the partner slot the resident's.
        const int  in_chara = UserStatus->cur_chara;
        const bool body_in = own_body == nullptr || (CharaMain.pos[0] == own_pos[0] && CharaMain.pos[1] == own_pos[1] && CharaMain.pos[2] == own_pos[2]);
        const bool other_resident = own_body == nullptr || (g_partner.pos[0] == resident_pos[0] && g_partner.pos[2] == resident_pos[2] &&
                                                            g_partner.chara->pos[0] == resident_pos[0]);
        const bool frame_follows = (void *) CharaFrame == (void *) CharaMain.frame;
        const int  presenting = ActorPresenting();

        ActorSwapOut();

        if (!SnapshotTakeBytes(after, why, {})) {
            std::fprintf(stderr, "actor: swap roundtrip player %d: state set not taken again (%s)\n", number, why.c_str());
            continue;
        }

        std::size_t differ = 0, first = (std::size_t) -1;

        for (std::size_t i = 0; i < before.size() && i < after.size(); i++) {
            if (before[i] != after[i]) {
                if (differ++ == 0) {
                    first = i;
                }
            }
        }

        std::size_t pages = 0, needed = 0, listed = 0;
        std::vector<std::uint32_t> need;

        if (SnapshotPageList(after, list, &listed)) {
            SnapshotPageNeed(list, before, need, &pages, &needed);
        }

        std::fprintf(stderr,
                     "actor: swap roundtrip player %d: %s, member %d (own %d, resident %d), body %s, other %s, frame %s, presenting %d, "
                     "self-pointers %d, size %zu/%zu, pages %zu needed %zu, bytes differ %zu (first at %lld)\n",
                     number, index == 0 ? "resident" : (ActorPresent(index) ? "present" : "absent"), in_chara, own_chara, resident_chara,
                     own_body == nullptr ? "none" : (body_in ? "in" : "NOT IN"), other_resident ? "resident" : "NOT RESIDENT",
                     frame_follows ? "follows" : "STALE", presenting, self_pointers, before.size(), after.size(), pages, needed, differ,
                     differ != 0 ? (long long) first : -1LL);
    }

    std::fprintf(stderr, "actor: roundtrip done\n");
}

} // namespace coop
