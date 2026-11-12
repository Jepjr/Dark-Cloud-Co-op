// Per-player inventories and item sending (coop_inventory.hpp). Player 2's bag and purse, the sends
// between the two, the save slot's sidecar, and the test bot (DC_COOP_INVTEST).

#include "coop/game_ns.hpp"
#include "coop/coop_townmenu.hpp" // coop-stage4: CoopTownMenuRunsLocalHere
#include "coop/coop_inventory.hpp"
#include "coop/coop_actor.hpp" // coop-eventslots: kMaxPlayers
#include "coop/coop_audio.hpp" // coop-menuse
#include "coop/coop_worlds.hpp" // coop-dungeonp2

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <system_error>
#include <nlohmann/json.hpp>

#include "battlemenu.hpp" // coop-keytab: ItemMenuMode
#include "battlemenu/internal.hpp"
#include "clsmes.hpp"
#include "text/gametext.hpp"
#include "localize/localize.hpp"
#include "platform/glyphs.hpp"
#include "dngmessageman.hpp"
#include "dngstatusdata.hpp"
#include "edit.hpp" // coop-bags: EdAddMaxItem (the test hook)
#include "dun/gameloop.hpp"
#include "gamepad.hpp"
#include "dungeonmap.hpp"
#include "itemdata.hpp"
#include "main.hpp"
#include "memcard.hpp"
#include "menu_draw.hpp"
#include "menu_inventory.hpp"
#include "platform/config.hpp"
#include "platform/files.hpp"
#include "randomitem.hpp"
#include "save/save_slots.hpp"
#include "savedata.hpp"
#include "snd.hpp"
#include "sound_effect.h"
#include "shot_freefuncs.hpp"
#include "stealitem.hpp"
#include "mathutil.hpp"
#include <cmath>
#include <libvu0.h>
#include "texture.hpp"
#include "ui/framework/surfaces.hpp" // coop-sharedkeys: the shared box
#include "sysmes.hpp"
#include "userstatus.hpp"

#include "coop/coop.hpp"
#include "coop/coop_combat.hpp"
#include "coop/coop_core.hpp"
#include "coop/coop_menu.hpp" // coop-itemsend: MenuP2BagInPack
#include "coop/coop_fullmenu.hpp" // coop-bagsendlocal: FullMenuRunsLocalFor
#include "coop/netplay.hpp"
#include "coop/netplay_cmd.hpp" // coop-bagsendlocal: kCmdGive
#include "coop/coop_message.hpp" // coop-itemprompt

namespace dc::game {
extern s32        mode;
extern s32        gameTask;
extern s32        itemNowSel;
extern CFrameVu1 *CharaFrame;
} // namespace dc::game

namespace coop {

P2Bag     g_bag;
ITEM_PACK g_key_ring;

namespace {

namespace fs = std::filesystem;

constexpr const char   *kFileName = "coop-p2.dat";
constexpr std::uint32_t kFileMagic = 0x32504344; // "DCP2"
constexpr std::uint32_t kFileVersion = 2; // 2: the quick-use selection after the gilda
constexpr int           kMostGilda = 0xFFFF;
constexpr int           kNoticeTicks = 150;

std::string g_notice;
int         g_notice_ticks = 0;
int         g_ticks = 0; // ticks InventoryStep has run, for the send log (the same in linked copies)

// coop-keyring: the gate keys, as CDngStatusData::LostGateKey names them.
bool GateKeyItem(int item) {
    switch (item) {
        case ITEM_DRAN_S_CREST:
        case ITEM_SHINY_STONE:
        case ITEM_RED_BERRY:
        case ITEM_HOOK:
        case ITEM_KING_S_SLATE:
        case ITEM_GUN_POWDER:
        case ITEM_CLOCK_HANDS:
        case ITEM_POINTY_CHESTNUT:
        case ITEM_BLACK_KNIGHT_CREST:
            return true;
    }

    return false;
}

bool BagItem(int item) {
    return item >= ITEM_DUNGEON_START && item < ITEM_WEAPON_START;
}

// The gate keys DunMoveChara gives through its own sequence, and the items the menus sort as quest or
// story items: what the doors' and events' scripts look for in player 1's pack.
bool StoryItem(int item) {
    // coop-p2keys: DC_COOP_MUTATE=keybag (x-dungeon-p2 keydoor's second twin): a gate key she picks up goes to
    // her own bag, out of the party's ring that the gate's window reads.
    if (NowDngMap != nullptr && item == NowDngMap->gate_key_no && Mutated("keybag")) {
        return false;
    }

    switch (item) {
        case ITEM_DRAN_S_CREST:
        case ITEM_SHINY_STONE:
        case ITEM_RED_BERRY:
        case ITEM_HOOK:
        case ITEM_KING_S_SLATE:
        case ITEM_GUN_POWDER:
        case ITEM_CLOCK_HANDS:
        case ITEM_POINTY_CHESTNUT:
        case ITEM_BLACK_KNIGHT_CREST:
        case ITEM_MAP:
        case ITEM_MAP + 1:
        case ITEM_WORLDMAP:
            return true;
    }

    if (item < ITEM_ATTACH_START || item >= ITEM_WEAPON_START) {
        return false;
    }

    int sort = ITEM_LIST[item - ITEM_ATTACH_START].sort_key;
    return sort == ITEM_SORT_QUEST || sort == ITEM_SORT_KEY_ITEM;
}

std::string Name(int item) {
    return item > 0 ? dc::battlemenu::detail::DecodeItemNamePort(item, "?") : std::string("-");
}

// coop-bags (Jep 2026-11-09: a bag upgrade is not repeatable, so one raises EVERY profile's bag, including profiles made
// later). Retail raises the bag in one place, EdAddMaxItem (the event scripts' _SADD_MAXITEM and _ADD_MAXITEM_MES), on
// the pack the game holds in the save: player 1's, or player 2's while her bag is swapped in (apart on her copy, a menu
// of hers). So the size is a fact of the party, not of a bag, and it is written where it changes, not copied each tick:
// EdAddMaxItem (recipe: CoopBagSizeRaised) raises every bag at once, the live packs and every kept profile's; the apart
// slot sync's header apply (a remote copy's upgrade) and the loads (CoopInventoryLoad, a join's InventoryRead) take the
// largest size either live pack holds (BagSizeNow; nothing in retail ever lowers one); a profile's bag is given the
// party's size (GiveP2) and a profile made new starts at it (EmptyBag(status size)). A kept bag of another game's size
// never raises this one. Opened slots are cleared as retail does (EdAddMaxItem: "a slot is read up to the count").
// DC_COOP_MUTATE=bagsize (the rows' twins) turns it off: the bag keeps its own size, as before.
void BagSizeTo(ITEM_PACK &pack, int size) {
    size = std::clamp(size, 1, 100);
    if (pack.num == size) {
        return;
    }
    for (int i = std::max((int) pack.num, 0); i < size && i < 103; i++) {
        pack.item[i] = -1;
        pack.item_vol[i] = 0;
    }
    // Smaller (a kept bag from a game with a bigger bag is given to this one): what sat past the new end moves into free
    // slots below it; what does not fit is said, not dropped quietly.
    for (int i = size; i < pack.num && i < 103; i++) {
        if (pack.item[i] < ITEM_DUNGEON_START) {
            continue;
        }
        int free = 0;
        while (free < size && pack.item[free] >= ITEM_DUNGEON_START) {
            free++;
        }
        if (free < size) {
            pack.item[free] = pack.item[i];
            pack.item_vol[free] = pack.item_vol[i];
        } else {
            std::fprintf(stderr, "coop-inv: bag size: no room below slot %d for item %d (slot %d)\n", size, (int) pack.item[i], i);
        }
        pack.item[i] = -1;
        pack.item_vol[i] = 0;
    }
    pack.num = (s8) size;
}

void BagSizeSync() {
    CUserStatus *status = UserStatus != nullptr ? UserStatus : SaveData != nullptr ? (CUserStatus *) SaveData->GetDngStatus() : nullptr;

    if (status == nullptr || !g_bag.seeded || !OwnBags() || Mutated("bagsize")) {
        return;
    }
    const int size = std::min((int) std::max(status->item_pack.num, g_bag.pack.num), 100);

    if (size > 0 && (status->item_pack.num != size || g_bag.pack.num != size)) {
        std::fprintf(stderr, "coop-inv: bag size: the party's is %d (the game's pack had %d, the other bag %d) at link tick %lld\n", size,
                     (int) status->item_pack.num, (int) g_bag.pack.num, (long long) NetplayLinkTick());
        BagSizeTo(status->item_pack, size);
        BagSizeTo(g_bag.pack, size);
    }
}

// Player 2's bag, seeded the first time it is used after a new game: as many slots as player 1's pack,
// all empty.
P2Bag &Bag() {
    if (!g_bag.seeded) {
        g_bag.pack = ITEM_PACK{};
        g_bag.pack.num = UserStatus != nullptr && UserStatus->item_pack.num > 0 ? UserStatus->item_pack.num : 50;

        for (int i = 0; i < 3; i++) {
            g_bag.pack.quick_item_slot[i] = -1;
            g_bag.pack.quick_item_qty[i] = 0;
            g_bag.active_item_vol[i] = 0;
        }

        g_bag.selected = 1;

        for (int i = 0; i < 103; i++) {
            g_bag.pack.item[i] = -1;
            g_bag.pack.item_vol[i] = 0;
        }

        g_bag.seeded = true;
    }

    return g_bag;
}

// As CDngStatusData::CheckItemGet counts a pack: bag items in its slots and copies in its quick-use
// slots.
int Carried(const ITEM_PACK &pack) {
    int count = 0;

    for (int i = 0; i < pack.num && i < 103; i++) {
        count += BagItem(pack.item[i]) ? 1 : 0;
    }

    for (int k = 0; k < 3; k++) {
        if (pack.quick_item_slot[k] != -1) {
            count += pack.quick_item_qty[k];
        }
    }

    return count;
}

bool HasRoom(const ITEM_PACK &pack) {
    return Carried(pack) < pack.num;
}

int Held(const ITEM_PACK &pack, int item) {
    return GetNowItemNum((short) item, const_cast<ITEM_PACK *>(&pack));
}

// Puts an item into the first empty slot of the pack; false when there is none.
bool Put(ITEM_PACK &pack, int item, int volume) {
    for (int i = 0; i < pack.num && i < 103; i++) {
        if (pack.item[i] < ITEM_DUNGEON_START) {
            pack.item[i] = (s16) item;
            pack.item_vol[i] = (s16) volume;
            return true;
        }
    }

    return false;
}

// Takes one of an item out of a pack, from its slots first and then its quick-use slots, as
// DeleteItemAfterUseItem does; returns its uses left, or -1 when the pack has none. Player 1's
// quick-use slot goes through DelActiveItem, which also lets go of its hand model.
int Take(ITEM_PACK &pack, std::int32_t *quick_vol, int item, bool players_own) {
    for (int i = 0; i < pack.num && i < 103; i++) {
        if (pack.item[i] == item) {
            int volume = pack.item_vol[i];

            pack.item[i] = -1;
            pack.item_vol[i] = 0;
            return volume;
        }
    }

    for (int k = 0; k < 3; k++) {
        if (pack.quick_item_slot[k] != item || pack.quick_item_qty[k] <= 0) {
            continue;
        }

        int volume = pack.quick_item_qty[k] == 1 && quick_vol[k] > 0 ? quick_vol[k] : ItemDataToHaveCopy(item);

        if (players_own && NowDngMap != nullptr) {
            DelActiveItem(k + 1);
        } else if (--pack.quick_item_qty[k] <= 0) {
            pack.quick_item_slot[k] = -1;
            pack.quick_item_qty[k] = 0;
        }

        return volume;
    }

    return -1;
}

void Notice(const std::string &text) {
    g_notice = text;
    g_notice_ticks = kNoticeTicks;
}

const char *kPlayerNames[2] = {"Toan", "Xiao"};

// The buttons player 1's pad holds this tick, as the game's port 1 reads them (the link's agreed pad
// while linked).
std::uint16_t PlayerOneButtons() {
    InputPadState linked;
    const InputPadState &pad = NetplayPad(0, linked) ? linked : InputGetPad(0);

    return pad.buttons;
}

// Jep (note 30, 2026-10-25; coop-sendsame: the R3 send "must not exist for anything"): there is no R3 send and no
// line under player 2's bar for it. Both players send the same way, Select on the bag screen's Items page
// (BagScreenSend). Pad 1's R3 only logs, for v-no-r3-menu -Case p1; DC_COOP_MUTATE=r3send, its twin, puts the old
// send and its notice back.
void StepPlayerOne(bool playing) {
    SoundOwner sound(1); // coop-menuse
    static std::uint16_t last = 0;
    std::uint16_t        buttons = PlayerOneButtons();
    std::uint16_t        pressed = buttons & ~last;

    last = buttons;

    // debug start: L3 held makes R3 the debug start picker's chord (gameloop.cpp).
    if (!playing || gameTask != GAME_TASK_PLAY || (pressed & kInputR3) == 0 || (buttons & kInputL3) != 0 || !OwnBags()) {
        return;
    }

    if (!Mutated("r3send")) {
        std::printf("coop-inv: pad 1 R3, no send\n");
        return;
    }

    int slot = std::clamp((int) itemNowSel, 1, 3) - 1;
    int item = UserStatus->item_pack.quick_item_slot[slot];

    if (item < 0) {
        SndSePlay(MENU_SOUND_REFUSE, -1, 0);
        Notice("Pick an item to send with Left/Right");
        std::printf("coop-inv: send P1->P2 refused: quick-use slot %d is empty\n", slot + 1);
        return;
    }

    SendItem(1, item);
}

} // namespace

// coop-keypick: the items the party shares (StoryItem), which her key picker lists from the party's pack.
bool SharedItem(int item) {
    return StoryItem(item);
}

namespace {

// coop-worldkeys: the ring empty, every slot open (num 103: the pickers and the Keys tab read up to num).
void RingClear() {
    g_key_ring = ITEM_PACK{};
    g_key_ring.num = 103;
    for (int k = 0; k < 3; k++) {
        g_key_ring.quick_item_slot[k] = -1;
    }
    for (int i = 0; i < 103; i++) {
        g_key_ring.item[i] = -1;
    }
}

} // namespace

// DC_COOP_MUTATE=ringbag (twin): no ring of its own; the keys in player 1's pack, as before.
bool RingOn() {
    return OwnBags() && !Mutated("ringbag");
}

bool RingItem(int item) {
    return item >= ITEM_DUNGEON_START && item < ITEM_WEAPON_START && (unsigned) (item - ITEM_MAP) > 1 && item != ITEM_WORLDMAP && SharedItem(item);
}

ITEM_PACK &SharedRing() {
    if (RingOn()) {
        if (g_key_ring.num == 0) {
            RingClear(); // (zeroed: never set since the start, so empty)
        }
        return g_key_ring;
    }
    return CoopWorldsBagsSwapped() ? Bag().pack : UserStatus->item_pack;
}

int RingAdopt(ITEM_PACK &pack, const char *from) {
    if (!RingOn() || &pack == &g_key_ring) {
        return 0;
    }
    ITEM_PACK &ring = SharedRing();
    int        moved = 0;

    for (int i = 0; i < 103; i++) {
        if (!RingItem(pack.item[i])) {
            continue;
        }
        if (!Put(ring, pack.item[i], pack.item_vol[i])) {
            std::fprintf(stderr, "coop-ring: no room in the ring for item %d from %s\n", pack.item[i], from);
            continue;
        }
        std::fprintf(stderr, "coop-ring: item %d from %s into the ring\n", pack.item[i], from);
        pack.item[i] = -1;
        pack.item_vol[i] = 0;
        moved++;
    }
    for (int k = 0; k < 3; k++) {
        if (pack.quick_item_slot[k] != -1 && RingItem(pack.quick_item_slot[k])) {
            for (int n = 0; n < pack.quick_item_qty[k]; n++) {
                Put(ring, pack.quick_item_slot[k], 0);
                moved++;
            }
            pack.quick_item_slot[k] = -1;
            pack.quick_item_qty[k] = 0;
        }
    }
    if (moved > 0) {
        pack.item_count = (s8) Carried(pack);
    }
    return moved;
}

int RingCount(int item) {
    const ITEM_PACK &ring = SharedRing();
    int              count = 0;

    for (int i = 0; i < ring.num && i < 103; i++) {
        count += ring.item[i] == item ? 1 : 0;
    }
    return count;
}

// The keys a floor's visit gives, as CDngStatusData::LostGateKey drops them: the gate keys and the floor keys from the
// Bone Key to the Silver Key.
bool FloorKeyItem(int item) {
    return GateKeyItem(item) || (item >= ITEM_BONE_KEY && item <= ITEM_SILVER_KEY);
}

// coop-keyfloor (Jep: a kept key must not open a gate on any floor but its own): a floor key carries the floor it was
// found on in its slot's uses (item_vol, which a key never uses), set where the game gives it (CDngStatusData::GetItem).
// The uses go wherever the key goes (the menus' moves, the apart slot sync, RingSweep, a snapshot), so both copies agree
// on every key's floor. A key without one (co-op off when it was found) fits any floor, as before.
namespace {

constexpr int kKeyFloorMark = 0x4000;

int FloorTag(int map, int floor) {
    return kKeyFloorMark | (map & 0x3F) << 8 | (floor & 0xFF);
}

bool Tagged(int volume) {
    return (volume & 0xC000) == kKeyFloorMark;
}

// This copy's floor as a tag, -1 off a floor.
int TagHere() {
    if (mode != GAME_MODE_DUNGEON || UserStatus == nullptr || NowDngMap == nullptr) {
        return -1;
    }

    return FloorTag(selectMapNo, UserStatus->cur_floor);
}

int LoseFloorKeys(ITEM_PACK &pack) {
    int lost = 0;

    for (int i = 0; i < 103; i++) {
        const int item = pack.item[i];

        if (FloorKeyItem(item)) {
            pack.item[i] = -1;
            lost++;
        }
    }

    return lost;
}

} // namespace

int KeyFloorTag(int item, int volume) {
    const int here = TagHere();

    if (!Enabled() || !FloorKeyItem(item) || here < 0) {
        return volume;
    }

    std::fprintf(stderr, "coop-keyfloor: key %d found on map %d floor %d (player %d's copy)\n", item, (here >> 8) & 0x3F, here & 0xFF,
                 NetplayRole());
    return here;
}

// DC_COOP_MUTATE=keyfloor (twin): any floor's key fits, as on coop-keyring.
bool KeyFitsHere(const ITEM_PACK &pack, int slot) {
    const int item = pack.item[slot];
    const int volume = pack.item_vol[slot];

    return !FloorKeyItem(item) || !Tagged(volume) || volume == TagHere() || Mutated("keyfloor");
}

bool KeyFloorOf(const ITEM_PACK &pack, int slot, int *map, int *floor) {
    if (!FloorKeyItem(pack.item[slot]) || !Tagged(pack.item_vol[slot])) {
        return false;
    }

    *map = (pack.item_vol[slot] >> 8) & 0x3F;
    *floor = pack.item_vol[slot] & 0xFF;
    return true;
}

// coop-keyring: a copy leaving a floor, or starting a new one, drops the floor's keys (LostGateKey). The keys are one ring
// that the live sync keeps the same on both copies apart, so a copy dropping them drops them for the other player too:
// while the other player is still on a floor (or on the way down to one), this copy keeps the ring as it is, and the copy
// left on the floor drops the keys when it goes. Otherwise the ring is cleared wherever this copy keeps it (her copy
// apart: g_bag), and the game's own pack too. True when this took the call (co-op on).
// coop-keyfloor: a key is kept only while someone is on its floor: with the other player on a floor, the keys of that
// floor stay and the other floors' keys go (untagged keys stay too). On the way down (floor -2) the ring stays whole.
// DC_COOP_MUTATE=ringdrop (twin): the ring is cleared even with the other player on a floor, as retail.
bool RingLostGateKey(CDngStatusData *status) {
    if (!Enabled() || UserStatus == nullptr) {
        return false;
    }

    int map = -1;
    int floor = -1;

    if (CoopWorldsPartnerOnFloor(&map, &floor) && !Mutated("ringdrop")) {
        ITEM_PACK &ring = SharedRing();
        int        lost = 0;

        for (int i = 0; i < 103 && floor >= 0 && !Mutated("keyfloor"); i++) {
            if (FloorKeyItem(ring.item[i]) && Tagged(ring.item_vol[i]) && ring.item_vol[i] != FloorTag(map, floor)) {
                ring.item[i] = -1;
                lost++;
            }
        }

        std::fprintf(stderr, "coop-keyring: this copy (player %d) keeps the floor keys: player %d is on map %d floor %d (%d of other floors dropped)\n",
                     NetplayRole(), 3 - NetplayRole(), map, floor, lost);
        return true;
    }

    ITEM_PACK &ring = SharedRing();
    int        lost = LoseFloorKeys(ring);

    if (&status->item_pack != &ring) {
        lost += LoseFloorKeys(status->item_pack);
    }

    std::fprintf(stderr, "coop-keyring: this copy (player %d) drops the floor keys (%d)\n", NetplayRole(), lost);
    return true;
}

bool OwnBags() {
    // DC_COOP_OWN_BAGS=1 turns it on for one launch whatever the config says: the co-op launchers set
    // it, so test copies, which start from their own configs, keep the default.
    static const bool launcher = TestEnvIs("DC_COOP_OWN_BAGS", "1");

    return Enabled() && (dc::platform::ConfigGet().coop_own_bags || launcher);
}

namespace {
// coop-pt1111p (pt1111o's find: player 2's overlay crashed both copies in town with game.coop_menu_panel on and the bags shared,
// 0xC0000005 reading 0x4368 in coop_menu.cpp Rows): the party's status, as player 1's overlay reads it (coop_menu.cpp Status) and
// the game keeps it: UserStatus on a floor, the save's own in a town, where UserStatus is null. A partner that shares the party's
// bag and purse reads them there too. DC_COOP_MUTATE=partner_userstatus (twin): UserStatus alone, as before, with a blank status
// standing in for the null one (before: the crash) so the twin's copies live to show the partner's empty bag and purse.
CUserStatus *PartyStatus() {
    if (UserStatus != nullptr) {
        return UserStatus;
    }
    if (Mutated("partner_userstatus")) {
        alignas(CUserStatus) static unsigned char blank[sizeof(CUserStatus)] = {};
        return reinterpret_cast<CUserStatus *>(blank);
    }
    return SaveData != nullptr ? (CUserStatus *) SaveData->GetDngStatus() : nullptr;
}
} // namespace

ITEM_PACK &PartnerPack() {
    if (!OwnBags() || Mutated("invuse")) {
        return PartyStatus()->item_pack;
    }

    return Bag().pack;
}

int PartnerGilda() {
    return OwnBags() ? Bag().gilda : PartyStatus()->money;
}

std::int32_t *PartnerActiveItemVol() {
    return OwnBags() ? Bag().active_item_vol : PartyStatus()->active_item_vol;
}

void PartnerGold(int amount) {
    if (amount <= 0) {
        return;
    }

    if (!OwnBags() || Mutated("invpickup")) {
        PartyStatus()->money = (u16) std::min(kMostGilda, PartyStatus()->money + amount);
        return;
    }

    Bag().gilda = (std::uint16_t) std::min(kMostGilda, Bag().gilda + amount);
    std::printf("coop-inv: P2 gilda +%d = %d\n", amount, Bag().gilda);
}

// coop: gaps2
void PartnerSetGilda(int gilda) {
    gilda = std::clamp(gilda, 0, kMostGilda);

    if (!OwnBags()) {
        PartyStatus()->money = (u16) gilda;
        return;
    }

    Bag().gilda = (std::uint16_t) gilda;
}

bool PartnerKeeps(int item) {
    return OwnBags() && BagItem(item) && !StoryItem(item) && !Mutated("invpickup");
}

int PartnerCheckItemGet(int item) {
    if (PartnerKeeps(item)) {
        return HasRoom(Bag().pack) ? 0 : 1;
    }

    return ((CDngStatusData *) UserStatus)->CheckItemGet(item);
}

bool PartnerGetItem(int item, int volume, bool message) {
    if (!PartnerKeeps(item) || Mutated("invchest") || !HasRoom(Bag().pack)) {
        return false;
    }

    if (!Put(Bag().pack, item, volume >= 0 ? volume : ItemDataToHaveCopy(item))) {
        return false;
    }

    if (message) {
        MessageBy by(2); // coop-itemprompt: on her screen only
        ItemGetMes(item, -1, 0x96, 0);
        DngMessMan.hide_timer = 0x96;
        SndSePlay(SE_ITEM_GET, -1, 0);
    }

    std::printf("coop-inv: P2 bag gets item %d, now %d of it, %d/%d carried\n", item, Held(Bag().pack, item), Carried(Bag().pack),
                Bag().pack.num);
    return true;
}

int BagItems(int *out, int max) {
    const ITEM_PACK &pack = Bag().pack;
    int              count = 0;

    for (int i = 0; i < pack.num && i < 103 && count < max; i++) {
        int item = pack.item[i];

        if (BagItem(item) && std::find(out, out + count, item) == out + count) {
            out[count++] = item;
        }
    }

    for (int k = 0; k < 3 && count < max; k++) {
        int item = pack.quick_item_slot[k];

        if (BagItem(item) && pack.quick_item_qty[k] > 0 && std::find(out, out + count, item) == out + count) {
            out[count++] = item;
        }
    }

    return count;
}

int BagCount(int item) {
    return Held(Bag().pack, item);
}

Send SendItem(int from, int item) {
    SoundOwner sound(from); // coop-menuse: the sender's refuse
    const char *to_name = kPlayerNames[from == 1 ? 1 : 0];

    if (!OwnBags()) {
        return Send::kOff;
    }

    if (item < ITEM_DUNGEON_START) {
        SndSePlay(MENU_SOUND_REFUSE, -1, 0);
        Notice("Nothing to send");
        std::printf("coop-inv: send P%d->P%d refused: no item picked\n", from, 3 - from);
        return Send::kNone;
    }

    if (!BagItem(item) || StoryItem(item)) {
        SndSePlay(MENU_SOUND_REFUSE, -1, 0);
        Notice(Name(item) + " stays with Toan");
        std::printf("coop-inv: send P%d->P%d item %d refused: a story item\n", from, 3 - from, item);
        return Send::kStory;
    }

    ITEM_PACK    &source = from == 1 ? UserStatus->item_pack : Bag().pack;
    ITEM_PACK    &target = from == 1 ? Bag().pack : UserStatus->item_pack;
    std::int32_t *source_vol = from == 1 ? UserStatus->active_item_vol : Bag().active_item_vol;

    if (Held(source, item) <= 0) {
        SndSePlay(MENU_SOUND_REFUSE, -1, 0);
        std::printf("coop-inv: send P%d->P%d item %d refused: none to send\n", from, 3 - from, item);
        return Send::kNone;
    }

    // A full bag refuses before anything leaves the sender (the invfull twin skips the check).
    if (!HasRoom(target) && !Mutated("invfull")) {
        SndSePlay(MENU_SOUND_REFUSE, -1, 0);
        Notice(std::string(to_name) + "'s bag is full");
        std::printf("coop-inv: send P%d->P%d item %d refused: the bag is full (%d/%d)\n", from, 3 - from, item, Carried(target),
                    target.num);
        return Send::kFull;
    }

    int volume = Mutated("invsend") ? ItemDataToHaveCopy(item) : Take(source, source_vol, item, from == 1);

    if (volume < 0) {
        volume = ItemDataToHaveCopy(item);
    }

    Put(target, item, volume);

    // The invdesync twin: player 2's copy alone gives one more, which only the bag's part of the
    // checksum sees.
    if (Mutated("invdesync") && NetplayLocalPlayer() == 2 && from == 1) {
        Put(target, item, volume);
    }

    SndSePlay(SE_ITEM_GET, -1, 0);
    Notice("Sent " + Name(item) + " to " + to_name);
    std::printf("coop-inv: send P%d->P%d item %d (uses %d) at tick %d: sent; P1 has %d, P2 has %d\n", from, 3 - from, item, volume, g_ticks,
                Held(UserStatus->item_pack, item), Held(Bag().pack, item));
    return Send::kSent;
}

bool BagScreenSendOn() {
    return OwnBags();
}

bool BagScreenSend(ITEM_PACK *pack, int slot) {
    ITEM_PACK *other = &Bag().pack;

    if (!OwnBags() || pack == nullptr || pack == other || Mutated("bagsend")) {
        return false;
    }

    int item = slot >= 0 && slot < pack->num && slot < 103 ? pack->item[slot] : -1;

    if (item < ITEM_DUNGEON_START) {
        std::printf("coop-inv: bag screen send refused: slot %d is empty\n", slot);
        return false;
    }

    if (!BagItem(item) || StoryItem(item)) {
        Notice(Name(item) + " stays with Toan");
        std::printf("coop-inv: bag screen send item %d refused: a story item\n", item);
        return false;
    }

    if (!HasRoom(*other)) {
        Notice("The other bag is full");
        std::printf("coop-inv: bag screen send item %d refused: the other bag is full (%d/%d)\n", item, Carried(*other), other->num);
        return false;
    }

    int volume = pack->item_vol[slot];

    // coop-bagsendlocal (Jep's 11-04 pack 8 playtest: P2 sent a Repair Powder to P1 and the copies went out of sync): a local
    // menu runs on its owner's copy alone, and only its writes to the party status reach every copy (its pokes). The other bag
    // lies outside the status (g_bag; his, under her view), so the item goes to it as a command (kCmdGive) every copy applies
    // on one tick; the slot's clearing below is a poke. DC_COOP_MUTATE=bagsend_here (twin): put here at once, on the owner's
    // copy alone, as before.
    // (coop-stage4: a town menu run local too)
    if ((FullMenuRunsLocalFor(NetplayLocalPlayer()) || CoopTownMenuRunsLocalHere(NetplayLocalPlayer())) && !Mutated("bagsend_here")) {
        NetCommand command;

        command.kind = kCmdGive;
        command.b = (std::uint16_t) item;
        command.c = (std::uint32_t) (std::uint16_t) volume;
        NetplayQueueCommand(command);
    } else {
        Put(*other, item, volume);
    }
    pack->item[slot] = -1;
    pack->item_vol[slot] = 0;
    Notice("Sent " + Name(item));
    std::printf("coop-inv: bag screen send item %d (uses %d) from slot %d at tick %d: sent; shown bag has %d, other bag has %d\n", item, volume,
                slot, g_ticks, Held(*pack, item), Held(*other, item));
    std::fflush(stdout); // a test stops the copies soon after (the log's stdout part)
    return true;
}

// kCmdGive, every copy on its tick (outside any menu's view): the item into the other player's bag. A bag filled meanwhile
// gives it back to the sender's (her slot still holds it: its poke comes after); both full, it is lost, on every copy alike.
void InventoryApplyGive(int player, const NetCommand &command, std::int64_t tick) {
    // (coop-stage4: in a town the party status is the save's dungeon status, UserStatus a floor's: a town menu's send too)
    CUserStatus *status = UserStatus != nullptr ? UserStatus : mode == 2 && SaveData != nullptr ? reinterpret_cast<CUserStatus *>(SaveData->GetDngStatus()) : nullptr;

    if (status == nullptr || !OwnBags()) {
        return;
    }

    const bool   p2_in_status = MenuP2BagInPack() != CoopWorldsBagsSwapped();
    ITEM_PACK   &p1 = p2_in_status ? Bag().pack : status->item_pack;
    ITEM_PACK   &p2 = p2_in_status ? status->item_pack : Bag().pack;
    const int    item = command.b;
    const int    volume = (s16) command.c;
    const char  *how = Put(player == 1 ? p2 : p1, item, volume) ? "given" : Put(player == 1 ? p1 : p2, item, volume) ? "back to the sender (the bag filled)" : "lost (both bags full)";

    std::printf("coop-inv: player %d's send of item %d (uses %d) at link tick %lld: %s; P1 has %d, P2 has %d\n", player, item, volume,
                (long long) tick, how, Held(p1, item), Held(p2, item));
    std::fflush(stdout);
}

// Retail's Select symbol has a wider cell than the face buttons' (PS2's SELECT art), so the Send line's word would
// start right of the others. Its word moves to where the first line's starts and its symbol to the middle of the face
// button's cell above it, unless the symbol needs the room (a keyboard key, which may be wide). DC_COOP_MUTATE=bagsendalign
// leaves the layout as the window made it (twin).
static void AlignSendLine(ClsMes &mes, bool key) {
    int square = -1, select = -1;

    for (int i = 0; i < mes.win_line_num; i++) {
        square = square < 0 && mes.win_line[i].code == -0x2F7 ? i : square;
        select = mes.win_line[i].code == -0x300 ? i : select;
    }

    if (key || Mutated("bagsendalign") || square < 0 || select < 0 || square + 1 >= mes.win_line_num || select + 1 >= mes.win_line_num ||
        mes.win_line[square + 1].y != mes.win_line[square].y || mes.win_line[select + 1].y != mes.win_line[select].y) {
        return;
    }

    const int shift = mes.win_line[square + 1].x - mes.win_line[select + 1].x;

    if (shift >= 0) {
        return;
    }

    mes.win_line[select].x = (s16) (mes.win_line[select].x + shift / 2);

    for (int i = select + 1; i < mes.win_line_num && mes.win_line[i].y == mes.win_line[select].y; i++) {
        mes.win_line[i].x = (s16) (mes.win_line[i].x + shift);
    }
    std::printf("coop-inv: bag screen help: the Send line moved %d left to line up with retail's\n", -shift);
}

namespace {

// coop-keypick: whether the help box has the shared line now (BagScreenHelpExtraLines grows the box by it).
bool g_shared_line = false;

// "Shared: Red Berry, Map" for the shared keys and story items in the pack beside the one shown (Bag().pack while
// hers is in the game's), each once; empty when there are none. DC_COOP_MUTATE=keysharedline: none (twin).
// coop-sharedkeys: none while the shared box (BagScreenSharedDraw) names them; the line ran out of the help box into it.
std::string SharedLine() {
    if (!Mutated("sharedbox")) {
        return {};
    }

    const ITEM_PACK &pack = Bag().pack;
    std::string      names;
    int              seen[16];
    int              count = 0;

    for (int i = 0; i < pack.num && i < 103 && count < 16 && !Mutated("keysharedline"); i++) {
        int item = pack.item[i];

        if (SharedItem(item) && std::find(seen, seen + count, item) == seen + count) {
            seen[count++] = item;
            names += (names.empty() ? "" : ", ") + dc::battlemenu::detail::DecodeItemNamePort(item, "?");
        }
    }

    return names.empty() ? names : dc::localize::LocalizeFormat(dc::localize::LocalizeText("coop.bag.shared", "Shared: %1"), {names});
}

} // namespace

// coop-bagapart: test trace (DC_COOP_BAGTRACE): the items the item menu shows (MenuItemPackPt) and its member, each time they
// change, with which player's copy it is and whether her copy apart has her bag swapped in. Jep's 2026-10-27 playtest:
// "P2 has P1's inventory when P1 isn't in the area".
void BagScreenTrace() {
    static const bool  on = TestEnvOn("DC_COOP_BAGTRACE");
    static std::string last;

    if (!on || MenuItemPackPt == nullptr) {
        return;
    }

    std::string line;

    for (int i = 0; i < MenuItemPackPt->num && i < 103; i++) {
        if (MenuItemPackPt->item[i] >= ITEM_DUNGEON_START) {
            line += std::to_string(MenuItemPackPt->item[i]) + ";";
        }
    }
    line += " member " + std::to_string(BtlMenuStatusPt != nullptr ? (int) BtlMenuStatusPt->cur_chara : -1);
    if (line == last) {
        return;
    }
    last = line;
    std::fprintf(stderr, "coop-inv: bag screen shows items=%s (copy of player %d, apart %d, swapped %d, her menu %d)\n", line.c_str(),
                 NetplayRole(), (int) CoopWorldsApart(), (int) CoopWorldsBagsSwapped(), (int) MenuP2BagInPack());
}

namespace {

// coop-keytab (Jep, 2026-10-31: "make sure that the send to P2 option doesn't show up when you hover over a key"): the Items
// page's cursor on a shared key or story item, which never leaves the party's pack, so the help keeps retail's two lines.
// DC_COOP_MUTATE=keysendline (twin): the Send line over a key too.
bool CursorOnSharedItem() {
    const PERSONAL_BOARD &board = ItemMenuMode.board;

    if (MenuItemPackPt == nullptr || ItemMenuMode.mode != ITEM_MENU_BOARD || board.page != BOARD_PAGE_ITEM ||
        board.cursor_area != PERSONAL_BOARD_AREA_CELLS || board.cursor < 0 || board.cursor >= 103 || Mutated("keysendline")) {
        return false;
    }

    const int item = MenuItemPackPt->item[board.cursor];

    return item >= ITEM_DUNGEON_START && (SharedItem(item) || FloorKeyItem(item));
}

} // namespace

bool BagScreenHelp(ClsMes &mes, int retail_id) {
    // An id no message file of the game's uses, so the window remakes retail's text when another page shows.
    constexpr int       kHelpId = 0x7EB5;
    static dc::text::GameTextFile file;
    static std::string  made;
    static bool         made_key = false;
    static int          made_to = 0;
    static std::string  made_shared;
    // (coop-warmconvert, WS7: said again at each job on a warm pair, as a fresh copy says it; trace_once_kept twin: kept)
    COOP_TEST_TRACE_RESET(made, std::string());

    if (!OwnBags() || Mutated("bagsendhelp")) {
        return false;
    }

    BagScreenTrace();

    static bool on_key = false;

    COOP_TEST_TRACE_RESET(on_key, false);
    if (CursorOnSharedItem() != on_key) {
        on_key = !on_key;
        std::fprintf(stderr, "coop-inv: bag screen help: %s (cursor %d, copy of player %d)\n", on_key ? "no Send line over a shared key" : "Send line back",
                     ItemMenuMode.board.cursor, NetplayRole());
    }

    if (on_key) {
        return false;
    }

    // The symbol's style follows the device last used (input.glyph_device): made again when it turns into a key or back.
    glyphs::Image image;
    const bool    key = glyphs::Find(glyphs::Button::Select, image) && image.key;
    // Who it goes to (Jep, 2026-10-24: "more descriptive like send to P2/P1"): the bag shown is the menu's owner's, the
    // game's pack, which holds player 2's while her menu or her copy apart has swapped it in (each swap flips it).
    const int to = MenuP2BagInPack() != CoopWorldsBagsSwapped() ? 1 : 2;
    // coop-keypick (Jep, 2026-10-25: "the keys should show up in the inventory"): her bag shown, the party's shared
    // keys and story items sit in the other pack (g_bag holds it while hers is swapped in); a line names them.
    const std::string shared = to == 1 ? SharedLine() : std::string();

    if (mes.mes_made == kHelpId && key == made_key && to == made_to && shared == made_shared) {
        return true;
    }

    short *retail = mes.buff != nullptr ? mes.GetTextLineDataTop(retail_id) : nullptr;

    if (retail == nullptr) {
        static bool logged = false;
        COOP_TEST_TRACE_RESET(logged, false);
        if (!logged) {
            logged = true;
            std::printf("coop-inv: bag screen help: no message %d in the window's file (%s)\n", retail_id, mes.buff != nullptr ? "has one" : "none");
        }
        return false;
    }

    // The new line spaced as retail's are: its glyph, then the gap retail puts after each glyph ("{gap 8}").
    std::string       text = dc::text::GameTextDecode(retail);
    const std::size_t gap = text.find("}{gap ");
    const std::size_t gap_end = gap != std::string::npos ? text.find('}', gap + 1) : std::string::npos;
    const std::string spacing = gap_end != std::string::npos ? text.substr(gap + 1, gap_end - gap) : " ";

    text += "\n{select}" + spacing + dc::localize::LocalizeFormat(dc::localize::LocalizeText("coop.bag.send_to", "Send to P%1."), {std::to_string(to)});

    if (!shared.empty()) {
        text += "\n" + shared;
    }

    g_shared_line = !shared.empty();

    if (text != made) {
        if (file.Set(kHelpId, text) < 0) {
            return false;
        }
        made = text;
        std::printf("coop-inv: bag screen help: \"%s\"\n", text.c_str());
    }

    short *buff = mes.buff;
    char  *body = mes.text;

    mes.SetBuff(file.Data());
    mes.mes_made = -1;
    mes.MakeMesWin(kHelpId);
    mes.buff = buff;
    mes.text = body;
    made_key = key;
    made_shared = shared;
    made_to = to;
    AlignSendLine(mes, key);
    return true;
}

int BagScreenHelpExtraLines() {
    return OwnBags() && !Mutated("bagsendhelp") && !CursorOnSharedItem() ? (g_shared_line ? 2 : 1) : 0;
}

// coop-sharedkeys (Jep's 2026-10-27 playtest: "is anything drawn in the empty bottom-right of the inventory screen?" Nothing
// was: the Items page draws its active items, character, board, trash, money and help box only): the party's shared keys
// and story items (SharedItem), each once, named in a help box there, on either player's menu while it has any. The ring is
// the pack beside the one shown while her bag is shown (as BagScreenHelp's "to"), else the one shown. On stderr each time
// it changes: "coop-inv: shared box shows \"...\" (copy of player P)". DC_COOP_MUTATE=sharedbox: no box (twin).
// coop-uinotice (Jep, 2026-10-30: "the spot ... actually belongs to a textbox that gives item descriptions, so at the very
// least, the shared items text should be removed from it and the item descriptions restored"): the box is no longer drawn
// there, so retail's item description has its place again; what the shared key ring looks like is still to be decided.
// Said once a menu: "coop-inv: shared box off: the corner is the item description's". DC_COOP_MUTATE=sharedbox_drawn (twin):
// drawn there, as before.
const ITEM_PACK &ShownRing() {
    if (RingOn()) {
        return SharedRing(); // coop-worldkeys
    }
    return OwnBags() && MenuP2BagInPack() != CoopWorldsBagsSwapped() ? Bag().pack : *MenuItemPackPt;
}

void BagScreenSharedDraw() {
    static std::string last = "-";

    COOP_TEST_TRACE_RESET(last, std::string("-")); // (said again on a warm pair's next job: warm-repeat p1bagback after p1attach)
    if (!Enabled() || MenuItemPackPt == nullptr || Mutated("sharedbox")) {
        return;
    }

    if (!Mutated("sharedbox_drawn")) {
        if (last != "off") {
            last = "off";
            std::fprintf(stderr, "coop-inv: shared box off: the corner is the item description's (copy of player %d)\n", NetplayRole());
        }
        return;
    }

    const bool       hers = OwnBags() && MenuP2BagInPack() != CoopWorldsBagsSwapped();
    const ITEM_PACK &ring = ShownRing();
    std::string      names;
    int              seen[16];
    int              count = 0;

    for (int i = 0; i < ring.num && i < 103 && count < 16; i++) {
        const int item = ring.item[i];

        if (SharedItem(item) && std::find(seen, seen + count, item) == seen + count) {
            seen[count++] = item;
            names += (names.empty() ? "" : ", ") + dc::battlemenu::detail::DecodeItemNamePort(item, "?");
        }
    }

    if (names != last) {
        last = names;
        std::fprintf(stderr, "coop-inv: shared box shows \"%s\" (copy of player %d, her bag shown %d)\n", names.c_str(), NetplayRole(), (int) hers);
    }

    if (names.empty()) {
        return;
    }

    // Right of the help box (0x36, about 230 wide), under the money bar and the trash, its bottom on the help box's.
    dc::ui::HelpBox::DrawTitled(0x12C, 0x1B6, 0x140, 0, dc::localize::LocalizeText("coop.bag.shared_title", "Shared key items"), names, 0x80, 22);
}

void InventoryTestSend(ITEM_PACK &pack, int item) {
    for (int i = 0; i < pack.num && i < 103; i++) {
        if (pack.item[i] < ITEM_DUNGEON_START) {
            pack.item[i] = (s16) item;
            pack.item_vol[i] = (s16) ItemDataToHaveCopy(item);
            BagScreenSend(&pack, i);
            return;
        }
    }
}

void InventoryTestSeed() {
    COOP_TEST_SWITCH(const char *, spec, TestEnv("DC_COOP_BAGSEED")); // (coop-warmconvert WS7 round 4: read again at each job)
    // In a town the party's status is the save's (UserStatus is the floor's).
    CUserStatus *status = UserStatus != nullptr ? UserStatus : SaveData != nullptr ? (CUserStatus *) SaveData->GetDngStatus() : nullptr;

    if (spec == nullptr || status == nullptr) {
        return;
    }

    // "items/items": player 1's bag's items, then player 2's (comma-separated ids), each with its own uses.
    ITEM_PACK *packs[2] = {&status->item_pack, &Bag().pack};
    int        player = 0;

    for (const char *at = spec; *at != '\0' && player < 2;) {
        int item = std::atoi(at);

        if (item >= ITEM_DUNGEON_START) {
            Put(*packs[player], item, ItemDataToHaveCopy(item));
        }
        at += std::strcspn(at, ",/");
        player += *at == '/' ? 1 : 0;
        at += *at != '\0' ? 1 : 0;
    }
    std::printf("coop-inv: test seed: player 1 carries %d, player 2 carries %d\n", Carried(status->item_pack), Carried(Bag().pack));
}

int PartnerSlotItem(std::uint16_t pressed) {
    P2Bag &bag = Bag();

    if ((pressed & (kInputLeft | kInputRight)) != 0) {
        // As DunMoveChara moves player 1's: Left from 1 wraps to 3, Right from 3 to 1.
        bag.selected = (pressed & kInputLeft) != 0 ? (bag.selected < 2 ? 3 : bag.selected - 1) : (bag.selected >= 3 ? 1 : bag.selected + 1);
        SndSePlay(MENU_SOUND_CURSOR, -1, 0);
        std::printf("coop-inv: P2 quick-use slot %d (item %d x%d)\n", bag.selected, bag.pack.quick_item_slot[bag.selected - 1],
                    bag.pack.quick_item_qty[bag.selected - 1]);
    }

    bag.selected = std::clamp((int) bag.selected, 1, 3);

    int item = bag.pack.quick_item_slot[bag.selected - 1];
    return item >= ITEM_DUNGEON_START && bag.pack.quick_item_qty[bag.selected - 1] > 0 ? item : -1;
}

bool SpendFromSlot(int item) {
    // The invuse twin spends from the party's pack instead (PartnerPack gives it).
    if (!OwnBags() || Mutated("invuse")) {
        return false;
    }

    P2Bag &bag = Bag();
    int    k = bag.selected - 1;

    if (k < 0 || k > 2 || bag.pack.quick_item_slot[k] != item || bag.pack.quick_item_qty[k] <= 0) {
        return false;
    }

    if (--bag.pack.quick_item_qty[k] <= 0) {
        bag.pack.quick_item_slot[k] = -1;
        bag.pack.quick_item_qty[k] = 0;
        bag.active_item_vol[k] = 0;
    } else {
        bag.active_item_vol[k] = ItemDataToHaveCopy(item);
    }

    return true;
}

// coop-p2feather
bool SpendBoostFrame() {
    if (!OwnBags()) {
        return false;
    }

    P2Bag &bag = Bag();
    int    k = bag.selected - 1;

    if (k < 0 || k > 2 || bag.pack.quick_item_slot[k] != ITEM_DRAN_S_FEATHER || bag.pack.quick_item_qty[k] <= 0) {
        return false;
    }

    // Test trace (DC_COOP_TEST_FEATHER_TRACE, x-p2feather-run): her slot's uses left as they change, one a frame while she runs.
    static const bool trace = TestEnvOn("DC_COOP_TEST_FEATHER_TRACE");

    if (trace) {
        std::fprintf(stderr, "coop-test: P2 quick slot %d (item %d x%d) has %d uses left at link tick %lld\n", k + 1, (int) bag.pack.quick_item_slot[k],
                     (int) bag.pack.quick_item_qty[k], (int) bag.active_item_vol[k] - 1, (long long) NetplayLinkTick());
    }
    if (--bag.active_item_vol[k] > 0) {
        return true;
    }

    // That use is run out: the next feather in the slot, else the slot empties.
    if (--bag.pack.quick_item_qty[k] <= 0) {
        bag.pack.quick_item_slot[k] = -1;
        bag.pack.quick_item_qty[k] = 0;
        bag.active_item_vol[k] = 0;
    } else {
        bag.active_item_vol[k] = ItemDataToHaveCopy(ITEM_DRAN_S_FEATHER);
    }

    return true;
}

// coop-revive
int RevivePowders(int player) {
    return Held(player == 2 ? PartnerPack() : UserStatus->item_pack, ITEM_REVIVAL_POWDER);
}

bool SpendRevivePowder(int player) {
    ITEM_PACK &pack = player == 2 ? PartnerPack() : UserStatus->item_pack;
    bool       party = &pack == &UserStatus->item_pack;
    int        left = Take(pack, party ? UserStatus->active_item_vol : Bag().active_item_vol, ITEM_REVIVAL_POWDER, party);

    std::printf("coop-inv: P%d spends a Revive Powder from %s: %s, %d left\n", player, party ? "the party's pack" : "P2's bag",
                left >= 0 ? "spent" : "none", Held(pack, ITEM_REVIVAL_POWDER));
    return left >= 0;
}

void PartnerSlotAssign(int item) {
    P2Bag &bag = Bag();
    int    k = bag.selected - 1;

    if (!BagItem(item) || bag.pack.quick_item_slot[k] == item) {
        return;
    }

    // In another slot already: the two slots change places.
    for (int j = 0; j < 3; j++) {
        if (j != k && bag.pack.quick_item_slot[j] == item) {
            std::swap(bag.pack.quick_item_slot[j], bag.pack.quick_item_slot[k]);
            std::swap(bag.pack.quick_item_qty[j], bag.pack.quick_item_qty[k]);
            std::swap(bag.active_item_vol[j], bag.active_item_vol[k]);
            std::printf("coop-inv: P2 slots %d and %d change places\n", j + 1, k + 1);
            return;
        }
    }

    // What the slot held goes back into the bag (there is room: the slot's copies are counted as
    // carried already), then every copy of the item in the bag moves into the slot.
    if (bag.pack.quick_item_slot[k] != -1) {
        int old = bag.pack.quick_item_slot[k];

        for (int q = 0; q < bag.pack.quick_item_qty[k]; q++) {
            Put(bag.pack, old, q == 0 && bag.active_item_vol[k] > 0 ? bag.active_item_vol[k] : ItemDataToHaveCopy(old));
        }
    }

    int count = 0;
    int volume = 0;

    for (int i = 0; i < bag.pack.num && i < 103; i++) {
        if (bag.pack.item[i] == item) {
            volume = count == 0 ? bag.pack.item_vol[i] : volume;
            bag.pack.item[i] = -1;
            bag.pack.item_vol[i] = 0;
            count++;
        }
    }

    bag.pack.quick_item_slot[k] = (s16) (count > 0 ? item : -1);
    bag.pack.quick_item_qty[k] = (s16) count;
    bag.active_item_vol[k] = count > 0 ? volume : 0;
    SndSePlay(count > 0 ? MENU_SOUND_CONFIRM : MENU_SOUND_REFUSE, -1, 0);
    std::printf("coop-inv: P2 slot %d holds item %d x%d\n", k + 1, item, count);
}

std::string SlotsLine() {
    P2Bag      &bag = Bag();
    std::string line;

    for (int k = 0; k < 3; k++) {
        int         item = bag.pack.quick_item_slot[k];
        std::string cell = item >= ITEM_DUNGEON_START ? Name(item) + " x" + std::to_string(bag.pack.quick_item_qty[k]) : std::string("-");

        if (k + 1 == bag.selected) {
            cell = "[" + cell + "]";
        }

        line += (k == 0 ? "" : "  ") + cell;
    }

    return line;
}

namespace {

// What the top of the screen last drew, for the test, and the items whose icons are in the HUD's
// cells on player 2's copy (-2 when none have been put there).
struct Drawn {
    bool valid = false;
    int  items[3] = {-1, -1, -1};
    int  counts[3] = {};
    int  selected = 0;
};

Drawn g_drawn;
int   g_icons[3] = {-2, -2, -2};
bool  g_icons_again = false; // coop-keydoor: the HUD's cells to be filled again once the icon sheet is back (CoopHudIconsAgain)

// Player 2's own window: its linked copy, with separate bags. The invslotsdraw twin draws player 1's.
bool PlayerTwoView() {
    // coop-dungeonp2: apart, her copy plays from the game's own pack, which holds her bag there (CoopWorldsBagsBack).
    return OwnBags() && NetplayLocalPlayer() == 2 && !CoopWorldsApart() && !Mutated("invslotsdraw");
}

// coop-crash2 (Jep's 2026-10-27 run, P1's copy): the cell copy BtItemListCashFlag starts at the next draw (DngActiveItemTextureCopy,
// CoopQuickSlotTextureCopy, DngActiveWeaponTextureCopy) looks up "itemicon", "wepicon" and "itempack" by name, and the port's
// setItemToReserved faults on any one missing. On the copy whose player is P1 no icon sheet is loaded on a floor (coop_hud's 0x43 is
// P2's copy's): the first are a menu's. P2's menu beside the world loads "itemicon" a tick before "wepicon" (it had just dropped the
// floor's blocks), and the copy, set on "itemicon" alone, faulted on "wepicon". The twin (DC_COOP_MUTATE=cellsheets): that old gate.
bool CellCopySheetsLoaded() {
    char item[] = "itemicon";
    char weapon[] = "wepicon";
    char page[] = "itempack";

    if (Mutated("cellsheets")) {
        return TexManager.GetTexture(item, -1) != nullptr;
    }

    return TexManager.GetTexture(item, -1) != nullptr && TexManager.GetTexture(weapon, -1) != nullptr &&
           TexManager.GetTexture(page, -1) != nullptr;
}

} // namespace

ITEM_PACK *CoopTopSlots(ITEM_PACK *players, int *selected) {
    ITEM_PACK *pack = players;

    // coop-crash2 test hook (DC_COOP_TEST_CELLS_AGAIN=1, t-triangle-menu -Case cellsagain): the refill armed on the first floor draw,
    // as a join's state set arms it (CoopHudIconsAgain), without the join.
    if (static bool armed = false; !armed && TestEnvOn("DC_COOP_TEST_CELLS_AGAIN")) {
        armed = true;
        g_icons_again = true;
        std::fprintf(stderr, "coop: test: the HUD's cell refill armed\n");
    }

    // coop-keydoor: after a join's apply the cells (itempack, a blank page there) are filled again, either player's view, as
    // soon as the sheets the copy reads are all loaded (CellCopySheetsLoaded; the game's copy faults on a missing one).
    // (Not while the replay runs: its frames are not drawn, and the copy, a draw, would be lost with them.)
    if (g_icons_again && !NetplayCatchUnderWay() && CellCopySheetsLoaded()) {
        g_icons_again = false;
        g_icons[0] = g_icons[1] = g_icons[2] = -2;
        BtItemListCashFlag = 1;
        std::fprintf(stderr, "coop: the HUD's item cells filled again after the state set\n");
    }

    if (PlayerTwoView()) {
        pack = &Bag().pack;
        *selected = Bag().selected;

        static const bool trace = TestEnv("DC_COOP_INVTRACE") != nullptr;
        if (trace) {
            std::fprintf(stderr, "coop-inv: top slots P2 %d %d %d sel %d\n", pack->quick_item_slot[0], pack->quick_item_slot[1],
                         pack->quick_item_slot[2], *selected);
        }

        // A menu copies player 1's icons into the cells as it closes: off the floor, ours are gone.
        if (gameTask != GAME_TASK_PLAY) {
            g_icons[0] = g_icons[1] = g_icons[2] = -2;
        }

        // New items in player 2's slots: their icons go into the HUD's cells at the next draw's start
        // (DunMainDraw reloads the icon sheet, block 0x28, and copies them while this is set), but
        // only once the game has loaded that sheet: reloading a block never loaded faults. Drawing only.
        bool changed = false;

        for (int k = 0; k < 3; k++) {
            changed = changed || (g_icons[k] != pack->quick_item_slot[k] && pack->quick_item_slot[k] >= ITEM_DUNGEON_START);
        }

        if (changed && CellCopySheetsLoaded()) {
            BtItemListCashFlag = 1;
        }
    }

    g_drawn.valid = true;
    g_drawn.selected = *selected;

    for (int k = 0; k < 3; k++) {
        g_drawn.items[k] = pack->quick_item_slot[k];
        g_drawn.counts[k] = pack->quick_item_qty[k];
    }

    return pack;
}

void CoopHudIconsAgain() {
    g_icons_again = true;
}

void CoopQuickSlotTextureCopy() {
    if (!PlayerTwoView()) {
        return;
    }

    char source[] = "itemicon";
    char target[] = "itempack";

    for (int k = 0; k < 3; k++) {
        int item = Bag().pack.quick_item_slot[k];

        g_icons[k] = item;

        if (item < ITEM_DUNGEON_START) {
            continue;
        }

        COM_ITEM_INFO *info = GetCommonItemInfo(item);
        static const bool trace = TestEnv("DC_COOP_INVTRACE") != nullptr;

        if (trace) {
            std::fprintf(stderr, "coop-inv: icon slot %d item %d info %p icon %d sheet %p cells %p\n", k + 1, item, (void *) info,
                         info != nullptr ? info->icon_index : -1, (void *) TexManager.GetTexture(source, -1),
                         (void *) TexManager.GetTexture(target, -1));
        }

        if (info != nullptr && TexManager.GetTexture(source, -1) != nullptr && TexManager.GetTexture(target, -1) != nullptr) {
            int icon = info->icon_index;

            if (icon >= 0 && icon <= 256) {
                setItemToReserved(source, (icon % 8) * 32, (icon >> 3) * 32, target, k * 32 + 32, 0);
            }
        }
    }
}

bool DrawnSlots(int items[3], int counts[3], int *selected) {
    for (int k = 0; k < 3; k++) {
        items[k] = g_drawn.items[k];
        counts[k] = g_drawn.counts[k];
    }

    *selected = g_drawn.selected;
    return g_drawn.valid;
}

// coop-dranscheat (coop_inventory.hpp)
bool DransCheatOn() {
    static const bool on = [] {
        const bool set = TestEnvOn("DC_QA_CHEATS");
        if (set) {
            std::printf("coop-inv: test cheats on (DC_QA_CHEATS)\n");
        }
        return set;
    }();
    return on;
}

void DransCheatGive(int index) {
    CUserStatus *status = UserStatus != nullptr ? UserStatus : SaveData != nullptr ? (CUserStatus *) SaveData->GetDngStatus() : nullptr;

    if (status == nullptr || index < 0 || index > 1) {
        return;
    }

    // Entry index's bag on this copy now: the game's pack unless the bags are separate and the other player's is swapped in
    // there (her copy apart, CoopWorldsBagsSwapped; her menu, MenuP2BagInPack), as the bag screen's send reads it.
    const bool partner_in_game = OwnBags() && MenuP2BagInPack() != CoopWorldsBagsSwapped();
    auto       in_game = [&](int k) { return !OwnBags() || (k == 1) == partner_in_game; };
    ITEM_PACK &pack = in_game(index) ? status->item_pack : Bag().pack;
    std::int32_t *vol = in_game(index) ? status->active_item_vol : Bag().active_item_vol;
    const long long tick = (long long) NetplayLinkTick();
    // coop-p2throw: DC_QA_CHEAT_ITEM=<item> gives that item instead of the feather (a test hook: a thrown item for her Square).
    static const int give = TestEnvInt("DC_QA_CHEAT_ITEM", ITEM_DRAN_S_FEATHER);

    if (Mutated("dranscheat")) {
        std::printf("coop-inv: dranscheat P%d at link tick %lld: twin, nothing given\n", index + 1, tick);
        return;
    }

    if (pack.quick_item_slot[0] == give && pack.quick_item_qty[0] > 0) {
        pack.quick_item_qty[0]++;
    } else {
        // What the slot held goes back into the bag first, as PartnerSlotAssign does; refused when the bag has no room for it.
        const int old = pack.quick_item_slot[0] >= ITEM_DUNGEON_START ? pack.quick_item_slot[0] : -1;
        const int old_qty = old >= 0 ? pack.quick_item_qty[0] : 0;
        int       free = 0;

        for (int i = 0; i < pack.num && i < 103; i++) {
            free += pack.item[i] < ITEM_DUNGEON_START ? 1 : 0;
        }

        if (free < old_qty) {
            std::printf("coop-inv: dranscheat P%d at link tick %lld refused: no bag slot for slot 1's item %d x%d\n", index + 1, tick, old,
                        old_qty);
            return;
        }

        for (int q = 0; q < old_qty; q++) {
            Put(pack, old, q == 0 && vol[0] > 0 ? vol[0] : ItemDataToHaveCopy(old));
        }

        pack.quick_item_slot[0] = give;
        pack.quick_item_qty[0] = 1;
        vol[0] = ItemDataToHaveCopy(give);
    }

    g_icons_again = true; // the HUD's cells take the new icon once the sheet is loaded (drawing only)

    const ITEM_PACK &one = in_game(0) ? status->item_pack : Bag().pack;
    const ITEM_PACK &two = in_game(1) ? status->item_pack : Bag().pack;
    std::printf("coop-inv: dranscheat P%d at link tick %lld: P1 slot 1 item %d x%d, P2 slot 1 item %d x%d\n", index + 1, tick,
                one.quick_item_slot[0], one.quick_item_qty[0], two.quick_item_slot[0], two.quick_item_qty[0]);
}

void BagSizeTestTick(); // coop-bags (below)

// coop-bags test hook (w-town-apart -Case bagup): the sizes, labelled, on this copy.
void BagSizesSay(const char *label) {
    CUserStatus *status = UserStatus != nullptr ? UserStatus : SaveData != nullptr ? (CUserStatus *) SaveData->GetDngStatus() : nullptr;

    if (status != nullptr) {
        std::fprintf(stderr, "coop-inv: bag sizes %s (player %d's copy): the game's pack %d, the other bag %d\n", label, NetplayLocalPlayer(),
                     (int) status->item_pack.num, (int) Bag().pack.num);
    }
}

// coop-bags: the test hooks, every tick in the dungeon (InventoryStep), the town (CoopTownStep) and a room (CoopTownInteriorStep).
void BagSizeStep() {
    BagSizeTestTick();
}

void InventoryStep(bool playing) {
    g_ticks++;

    if (g_notice_ticks > 0) {
        g_notice_ticks--;
    }

    BagSizeTestTick(); // (no size copy here: the writers set it, see BagSizeTo's comment)
    InventoryTestTick();
    StepPlayerOne(playing);
}

int PanelPages() {
    return OwnBags() ? 3 : 2;
}

const char *InventoryNotice() {
    return g_notice_ticks > 0 ? g_notice.c_str() : "";
}

void InventoryBytes(std::vector<unsigned char> &out) {
    const P2Bag &bag = Bag();
    const auto  *pack = reinterpret_cast<const unsigned char *>(&bag.pack);
    const auto  *vol = reinterpret_cast<const unsigned char *>(bag.active_item_vol);

    out.insert(out.end(), pack, pack + sizeof bag.pack);
    out.insert(out.end(), vol, vol + sizeof bag.active_item_vol);
    out.push_back((unsigned char) (bag.gilda & 0xFF));
    out.push_back((unsigned char) (bag.gilda >> 8));
    out.push_back((unsigned char) bag.selected);

    // coop-worldkeys: the party's key ring after the bag (the save's file, the link's state, the checksum); an older file
    // ends before it.
    if (RingOn()) {
        const auto *ring = reinterpret_cast<const unsigned char *>(&SharedRing());

        out.insert(out.end(), ring, ring + sizeof(ITEM_PACK));
    }
}

std::uint32_t InventoryHash() {
    std::vector<unsigned char> bytes;
    std::uint32_t              hash = 2166136261u;

    InventoryBytes(bytes);

    // Items in flight from a steal, and who stole each: where they fly and whose bag they land in. Only on a
    // dungeon's floor: NowDngMap stays set once the game has been in a dungeon, so a host back in town from one
    // hashed its stale steal slots and a joiner fresh from town did not (a catch-up join went OUT OF SYNC every
    // checksum and no resync healed it: bagjoin.ps1). The stale_steal twin keeps the old test.
    if (NowDngMap != nullptr && (mode == GAME_MODE_DUNGEON || Mutated("stale_steal"))) {
        for (int i = 0; i < STEAL_ITEM_MAX; i++) {
            std::int32_t slot[3] = {static_cast<std::int32_t>(StealItem.state[i]), StealItem.state[i] == StealItemState::None ? -1 : StealItem.aux_state[i],
                                    StealItem.state[i] == StealItemState::None ? -1 : StealItem.item[i]};
            const auto *raw = reinterpret_cast<const unsigned char *>(slot);

            bytes.insert(bytes.end(), raw, raw + sizeof slot);
        }
    }

    for (unsigned char byte : bytes) {
        hash = (hash ^ byte) * 16777619u;
    }

    return hash;
}

int StealRoomCheck(int owner, int item) {
    return OwnBags() && PartnerHit(owner) ? PartnerCheckItemGet(item) : ((CDngStatusData *) UserStatus)->CheckItemGet(item);
}

void StealSet(float *position, int item, int owner) {
    int slot = -1;

    for (int i = 0; i < STEAL_ITEM_MAX && slot == -1; i++) {
        slot = StealItem.state[i] == StealItemState::None ? i : -1;
    }

    StealItem.Set(position, item);

    // The invsteal twin leaves every steal player 1's, as retail does.
    if (slot != -1 && OwnBags() && PartnerHit(owner) && !Mutated("invsteal")) {
        StealItem.aux_state[slot] = 2;
        std::printf("coop-inv: P2 steals item %d (slot %d)\n", item, slot);
    }
}

void InventoryNewGame() {
    g_bag = P2Bag{};
    RingClear();
    g_notice_ticks = 0;
}

void InventoryWrite(std::vector<unsigned char> &out) {
    const std::uint32_t header[2] = {kFileMagic, kFileVersion};
    const auto         *bytes = reinterpret_cast<const unsigned char *>(header);

    out.insert(out.end(), bytes, bytes + sizeof header);
    InventoryBytes(out);
}

bool InventoryRead(const unsigned char *data, std::size_t size) {
    std::uint32_t header[2];
    P2Bag         bag;
    std::size_t   need = sizeof header + sizeof bag.pack + sizeof bag.active_item_vol + 3;

    if (data == nullptr || size < need) {
        return false;
    }

    std::memcpy(header, data, sizeof header);

    if (header[0] != kFileMagic || header[1] != kFileVersion) {
        return false;
    }

    data += sizeof header;
    std::memcpy(&bag.pack, data, sizeof bag.pack);
    data += sizeof bag.pack;
    std::memcpy(bag.active_item_vol, data, sizeof bag.active_item_vol);
    data += sizeof bag.active_item_vol;
    bag.gilda = (std::uint16_t) (data[0] | (data[1] << 8));
    bag.selected = std::clamp((int) data[2], 1, 3);

    if (bag.pack.num <= 0 || bag.pack.num > 100) {
        return false;
    }

    bag.seeded = true;
    g_bag = bag;

    // coop-worldkeys: the ring, when the bytes carry one (a file from before has none: the ring stays as it is, and the
    // keys the save holds in the bags are moved into it after the load).
    if (size >= need + sizeof(ITEM_PACK)) {
        ITEM_PACK ring;

        std::memcpy(&ring, data + 3, sizeof ring);
        if (ring.num == 103) {
            g_key_ring = ring;
        }
    }
    // coop-bags: the bag just read (a save's coop-p2.dat, a join's state) may be of another size than the game's pack (a save from
    // before the party's size; the joiner's pack comes with the state set): both take the larger, kept bags too.
    // DC_COOP_MUTATE=bagsize_read (twin): not done.
    if (!Mutated("bagsize_read")) {
        BagSizeNow();
    }
    return true;
}

// ---- profile bags (Jep, 2026-11-01 23:57Z: each bag is the lobby profile's, whoever sits where) ----
// A save slot keeps beside it coop-bags.json: whose each player's bag is (the game's pack player 1's, coop-p2.dat player
// 2's) and the bags of profiles not playing. At the lobby's start (ProfileBagsStart) each player takes their profile's bag:
// the two swap when the seats did, a profile with no bag starts empty, a bag nobody takes is kept for its profile. The party's
// story items and gate keys stay in player 1's pack whoever's bag it is (SharedItem: the doors look there). Only the host
// keeps them (as coop-p2.dat): a joiner gets the host's state with the link's apply.
namespace {

struct ProfileBag {
    ITEM_PACK     pack{};
    std::int32_t  active_item_vol[3] = {};
    std::uint16_t gilda = 0;
    std::int32_t  selected = 1;
};

constexpr const char *kBagsFile = "coop-bags.json";

} // namespace
// coop-eventslots: each profile's own fishing record (the file's end)
nlohmann::json FishProfilesJson(const std::string &p2_owner);
void           FishProfilesRead(const nlohmann::json &root, const std::string &p2_owner);
void           FishModeSwitch(bool to_per_player);
namespace {

std::string                       g_bag_owner[2]; // the profile each player's bag is ("" not known: a save from before)
std::map<std::string, ProfileBag> g_kept;         // the bags of profiles not playing, by name
bool                              g_owners_for_new_game = false; // the lobby named the owners of the new game about to start
// The lobby's players, kept for the slot's own load after the lobby (Host > Continue reads the slot again as the game starts:
// Jep's pack 7 log, 2026-11-03, P2's bag back from coop-p2.dat over her profile's), then given their bags again there.
std::string                       g_pending[2];
bool                              g_pending_set = false;

ProfileBag EmptyBag(int num) {
    ProfileBag bag;

    bag.pack.num = (s8) std::clamp(num, 1, 103);
    for (int i = 0; i < 3; i++) {
        bag.pack.quick_item_slot[i] = -1;
    }
    for (int i = 0; i < 103; i++) {
        bag.pack.item[i] = -1;
    }
    return bag;
}

// A pack's bag with the party's story items and keys left out (they stay in player 1's pack).
ProfileBag BagOf(const ITEM_PACK &pack, const std::int32_t vol[3], int gilda, int selected) {
    ProfileBag bag;

    bag.pack = pack;
    std::memcpy(bag.active_item_vol, vol, sizeof bag.active_item_vol);
    bag.gilda = (std::uint16_t) std::clamp(gilda, 0, kMostGilda);
    bag.selected = selected;
    for (int i = 0; i < 103; i++) {
        if (bag.pack.item[i] >= ITEM_DUNGEON_START && SharedItem(bag.pack.item[i])) {
            bag.pack.item[i] = -1;
            bag.pack.item_vol[i] = 0;
        }
    }
    for (int k = 0; k < 3; k++) {
        if (bag.pack.quick_item_slot[k] != -1 && SharedItem(bag.pack.quick_item_slot[k])) {
            bag.pack.quick_item_slot[k] = -1;
            bag.pack.quick_item_qty[k] = 0;
            bag.active_item_vol[k] = 0;
        }
    }
    bag.pack.item_count = (s8) Carried(bag.pack);
    return bag;
}

// Player 1's pack becomes bag, keeping the party's story items and keys it held (in the first free slots).
void GiveP1(CUserStatus &status, const ProfileBag &bag) {
    const ITEM_PACK old = status.item_pack;
    ITEM_PACK       pack = bag.pack;

    pack.num = old.num;
    for (int i = 0; i < 103; i++) {
        if (old.item[i] >= ITEM_DUNGEON_START && SharedItem(old.item[i]) && RingOn() && RingItem(old.item[i])) {
            if (!Put(SharedRing(), old.item[i], old.item_vol[i])) { // coop-worldkeys: the world's, never the new bag's
                std::fprintf(stderr, "coop-ring: no room in the ring for story item %d\n", old.item[i]);
            }
        } else if (old.item[i] >= ITEM_DUNGEON_START && SharedItem(old.item[i]) && !Put(pack, old.item[i], old.item_vol[i])) {
            std::fprintf(stderr, "coop-inv: profile bags: no room for story item %d in player 1's pack\n", old.item[i]);
        }
    }
    for (int k = 0; k < 3; k++) {
        if (old.quick_item_slot[k] != -1 && SharedItem(old.quick_item_slot[k])) {
            for (int n = 0; n < old.quick_item_qty[k]; n++) {
                Put(RingOn() && RingItem(old.quick_item_slot[k]) ? SharedRing() : pack, old.quick_item_slot[k], status.active_item_vol[k]);
            }
        }
    }
    pack.item_count = (s8) Carried(pack);
    status.item_pack = pack;
    std::memcpy(status.active_item_vol, bag.active_item_vol, sizeof status.active_item_vol);
    status.money = bag.gilda;
}

void GiveP2(const ProfileBag &bag) {
    g_bag.pack = bag.pack;
    // coop-bags: the profile's bag takes the party's size, not the size it was last kept at (another game's, or from before
    // an upgrade). The game's pack is player 1's here (not swapped): a give runs at the lobby, a load or a join.
    if (!Mutated("bagsize") && SaveData != nullptr && ((CUserStatus *) SaveData->GetDngStatus())->item_pack.num > 0) {
        BagSizeTo(g_bag.pack, ((CUserStatus *) SaveData->GetDngStatus())->item_pack.num);
    }
    std::memcpy(g_bag.active_item_vol, bag.active_item_vol, sizeof g_bag.active_item_vol);
    g_bag.gilda = bag.gilda;
    g_bag.selected = std::clamp((int) bag.selected, 1, 3);
    g_bag.seeded = true;
}

nlohmann::json BagJson(const ProfileBag &bag) {
    nlohmann::json out, items = nlohmann::json::array(), quick = nlohmann::json::array();

    for (int i = 0; i < bag.pack.num && i < 103; i++) {
        items.push_back({bag.pack.item[i], bag.pack.item_vol[i]});
    }
    for (int k = 0; k < 3; k++) {
        quick.push_back({bag.pack.quick_item_slot[k], bag.pack.quick_item_qty[k], bag.active_item_vol[k]});
    }
    out["slots"] = bag.pack.num;
    out["items"] = items;
    out["quick"] = quick;
    out["gilda"] = bag.gilda;
    out["selected"] = bag.selected;
    return out;
}

ProfileBag JsonBag(const nlohmann::json &in) {
    ProfileBag bag = EmptyBag(in.value("slots", 50));
    int        i = 0, k = 0;

    for (const auto &item : in.value("items", nlohmann::json::array())) {
        if (i < bag.pack.num && item.is_array() && item.size() == 2) {
            bag.pack.item[i] = item[0].get<s16>();
            bag.pack.item_vol[i] = item[1].get<s16>();
        }
        i++;
    }
    for (const auto &slot : in.value("quick", nlohmann::json::array())) {
        if (k < 3 && slot.is_array() && slot.size() == 3) {
            bag.pack.quick_item_slot[k] = slot[0].get<s16>();
            bag.pack.quick_item_qty[k] = slot[1].get<s16>();
            bag.active_item_vol[k] = slot[2].get<std::int32_t>();
        }
        k++;
    }
    bag.gilda = (std::uint16_t) std::clamp(in.value("gilda", 0), 0, kMostGilda);
    bag.selected = std::clamp(in.value("selected", 1), 1, 3);
    bag.pack.item_count = (s8) Carried(bag.pack);
    return bag;
}

} // namespace

void ProfileBagsGive(CUserStatus &status_ref, const std::string names[2]);

void ProfileBagsStart(const std::string &p1, const std::string &p2, bool new_game) {
    // DC_COOP_MUTATE=profilebags (l-lobby -Case bags' twin): the bags stay where the save had them.
    if (!OwnBags() || Mutated("profilebags")) {
        std::fprintf(stderr, "coop-inv: profile bags: %s\n", OwnBags() ? "left as they are (DC_COOP_MUTATE=profilebags)" : "off (one bag for the party)");
        return;
    }
    CUserStatus *status = SaveData != nullptr ? (CUserStatus *) SaveData->GetDngStatus() : nullptr;
    const std::string names[2] = {p1, p2};

    if (new_game || status == nullptr) {
        // A new game: both start with the game's empty bags; they are the players' from here.
        g_kept.clear();
        g_bag_owner[0] = p1;
        g_bag_owner[1] = p2;
        g_owners_for_new_game = new_game;
        g_pending[0] = p1;
        g_pending[1] = p2;
        g_pending_set = !Mutated("profilebags_once");
        std::fprintf(stderr, "coop-inv: profile bags: a new game, P1 %s, P2 %s\n", p1.c_str(), p2.c_str());
        return;
    }
    g_pending[0] = p1;
    g_pending[1] = p2;
    g_pending_set = !Mutated("profilebags_once"); // (DC_COOP_MUTATE=profilebags_once, twin: the start's giving alone, none at loads)
    FishModeSwitch(FishingPerPlayer()); // coop-eventslots: the setting changed since the slot's file: every point kept
    ProfileBagsGive(*status, names);
    RingSay("after the lobby's start"); // coop-worldkeys
}

void FishProfilesGive(const std::string owners[2], const std::string names[2]); // (coop-eventslots, below)

void ProfileBagsGive(CUserStatus &status_ref, const std::string names[2]) {
    CUserStatus *status = &status_ref;
    const std::string fish_owners[2] = {g_bag_owner[0], g_bag_owner[1]};

    FishProfilesGive(fish_owners, names); // coop-eventslots: each player's own fishing record follows their profile, as their bag

    // The bags as they are, under their owners (a save from before the profile bags: whoever plays now owns them).
    const ProfileBag held[2] = {BagOf(status->item_pack, status->active_item_vol, status->money, 1),
                                BagOf(Bag().pack, g_bag.active_item_vol, g_bag.gilda, g_bag.selected)};

    // coop-joinlobby (Jep, 2026-11-07: a nameless bag is never dropped). A bag nobody owns (a seat nobody named: a lobby
    // started alone, a save from before the profile bags) stays in its seat while nobody takes that seat, and is kept apart
    // under a name no player can type ("?P2": the lobby's names are A-Z and 0-9) when its seat's new player has a bag of
    // their own. Before, both were dropped. (DC_COOP_MUTATE=nameless_drop, l-lobby -Case nameless's twins: dropped again.)
    const bool keep_nameless = !Mutated("nameless_drop");
    bool       stays[2] = {false, false};

    for (int p = 0; p < 2; p++) {
        const std::string &owner = !g_bag_owner[p].empty() ? g_bag_owner[p] : names[p];

        if (owner.empty()) {
            stays[p] = keep_nameless;
        } else if (g_kept.count(owner) == 0) {
            g_kept[owner] = held[p];
        } else if (g_bag_owner[p].empty() && keep_nameless && (Carried(held[p].pack) > 0 || held[p].gilda > 0)) {
            std::string key = "?P" + std::to_string(p + 1);

            for (int n = 2; g_kept.count(key) != 0; n++) {
                key = "?P" + std::to_string(p + 1) + "#" + std::to_string(n);
            }
            g_kept[key] = held[p];
            std::fprintf(stderr, "coop-inv: profile bags: P%d's nameless bag kept apart as %s (%d carried, %d gilda; %s has a bag of their own)\n", p + 1, key.c_str(),
                         Carried(held[p].pack), held[p].gilda, owner.c_str());
        }
    }
    // Each player takes their profile's bag, else an empty one.
    for (int p = 0; p < 2; p++) {
        if (stays[p]) {
            std::fprintf(stderr, "coop-inv: profile bags: P%d is nobody, the seat's nameless bag stays (%d carried, %d gilda)\n", p + 1, Carried(held[p].pack),
                         held[p].gilda);
            continue;
        }
        auto             found = names[p].empty() ? g_kept.end() : g_kept.find(names[p]);
        const ProfileBag bag = found != g_kept.end() ? found->second : EmptyBag(status->item_pack.num);
        const char      *how = found != g_kept.end() ? "its own" : "none yet: empty";

        if (found != g_kept.end()) {
            g_kept.erase(found);
        }
        if (p == 0) {
            GiveP1(*status, bag);
        } else {
            GiveP2(bag);
        }
        std::fprintf(stderr, "coop-inv: profile bags: P%d is %s, bag %s (%d carried, %d gilda; was %s's)\n", p + 1, names[p].empty() ? "?" : names[p].c_str(), how,
                    Carried(bag.pack), bag.gilda, g_bag_owner[p].empty() ? "nobody's yet" : g_bag_owner[p].c_str());
        g_bag_owner[p] = names[p];
    }
}



// coop-bags: the party's size, written where it changes (see BagSizeTo's comment).
// An upgrade (EdAddMaxItem's recipe line): every bag takes the new size at once, the live packs and every profile's kept bag.
void BagSizeRaised(int size) {
    CUserStatus *status = UserStatus != nullptr ? UserStatus : SaveData != nullptr ? (CUserStatus *) SaveData->GetDngStatus() : nullptr;

    if (!OwnBags() || Mutated("bagsize") || status == nullptr) {
        return;
    }
    std::fprintf(stderr, "coop-inv: bag size: EdAddMaxItem raised the pack to %d: every bag takes it (%d kept) at link tick %lld\n", size, (int) g_kept.size(),
                 (long long) NetplayLinkTick());
    BagSizeTo(status->item_pack, size);
    if (g_bag.seeded) {
        BagSizeTo(g_bag.pack, size);
    }
    for (auto &entry : g_kept) {
        if (entry.second.pack.num < size) {
            BagSizeTo(entry.second.pack, size);
        }
    }
}

// A copy's own bags after a change it did not make here (the apart slot sync's header apply: the other copy's upgrade; a load;
// a join's bag): the largest size either live pack holds goes to both, and up to the kept bags.
void BagSizeNow() {
    CUserStatus *status = UserStatus != nullptr ? UserStatus : SaveData != nullptr ? (CUserStatus *) SaveData->GetDngStatus() : nullptr;

    if (!OwnBags() || Mutated("bagsize") || status == nullptr || !g_bag.seeded) {
        return;
    }
    BagSizeSync();
    for (auto &entry : g_kept) {
        if (entry.second.pack.num < status->item_pack.num) {
            BagSizeTo(entry.second.pack, status->item_pack.num);
        }
    }
}

// coop-bags test hooks (rows i-bag-size and w-town-apart -Case bagsize; every one only when its variable is set):
//   DC_COOP_BAGUP=T:P:N[,...]       at link tick T the copy playing P (0: every copy) runs retail's own EdAddMaxItem(N), the call
//                                   the event scripts' bag upgrade makes (_SADD_MAXITEM)
//   DC_COOP_BAGPROFILES=T:A:B[,...] at link tick T every copy gives the seats profiles A and B (ProfileBagsGive: the lobby's
//                                   and a load's path), so a profile's bag is kept, made new and given back in a row
//   DC_COOP_BAGREPORT=T[,...]       at link tick T each copy says the sizes of the game's pack, the other bag and the kept bags
void BagSizeTestTick() {
    static const char *up = TestEnv("DC_COOP_BAGUP");
    static const char *profiles = TestEnv("DC_COOP_BAGPROFILES");
    static const char *report = TestEnv("DC_COOP_BAGREPORT");
    static const char *load = TestEnv("DC_COOP_BAGLOAD");
    static std::set<std::string> fired;

    if (up == nullptr && profiles == nullptr && report == nullptr && load == nullptr && TestEnv("DC_COOP_BAGGIVE") == nullptr && TestEnv("DC_COOP_STOCKGIVE") == nullptr) {
        return;
    }
    CUserStatus *status = UserStatus != nullptr ? UserStatus : SaveData != nullptr ? (CUserStatus *) SaveData->GetDngStatus() : nullptr;
    const long long now = (long long) NetplayLinkTick();

    if (status == nullptr || !NetplayHasPlayer()) {
        return;
    }
    auto each = [&](const char *spec, auto &&run) {
        for (const char *at = spec; at != nullptr && *at != '\0';) {
            const char *end = std::strchr(at, ',');
            const std::string item = end != nullptr ? std::string(at, end) : std::string(at);

            if (!item.empty() && now >= std::atoll(item.c_str()) && fired.insert(std::string(spec) + "@" + item).second) {
                run(item);
            }
            at = end != nullptr ? end + 1 : nullptr;
        }
    };

    if (up != nullptr) {
        each(up, [&](const std::string &item) {
            int p = 0, n = 0;

            if (std::sscanf(item.c_str(), "%*d:%d:%d", &p, &n) == 2 && (p == 0 || p == NetplayLocalPlayer())) {
                const int got = EdAddMaxItem(n);

                std::fprintf(stderr, "coop-inv: test: EdAddMaxItem(%d) on player %d's copy at link tick %lld: the pack holds %d\n", n, NetplayLocalPlayer(), now, got);
                BagSizesSay("in the tick of the upgrade"); // (the second bag read at once, g_bag itself, no accessor)
            }
        });
    }
    // DC_COOP_BAGGIVE=T:P:ITEM[,...]: at link tick T every copy puts ITEM (a full copy) in player P's bag (the storage rows' stock to store)
    static const char *give = TestEnv("DC_COOP_BAGGIVE");

    if (give != nullptr) {
        each(give, [&](const std::string &item) {
            int p = 0, id = 0;

            if (std::sscanf(item.c_str(), "%*d:%d:%d", &p, &id) == 2) {
                ITEM_PACK &pack = p == 1 ? status->item_pack : Bag().pack;
                const bool put = Put(pack, id, ItemDataToHaveCopy(id));

                std::fprintf(stderr, "coop-inv: test: item %d into player %d's bag at link tick %lld (%d)\n", id, p, now, (int) put);
            }
        });
    }
    // DC_COOP_STOCKGIVE=T:SLOT:ITEM[,...]: at link tick T every copy puts ITEM (a full copy) in the house storage's slot SLOT (the take rows' stock)
    static const char *stock_give = TestEnv("DC_COOP_STOCKGIVE");

    if (stock_give != nullptr && SaveData != nullptr && SaveData->GetStockItem() != nullptr) {
        each(stock_give, [&](const std::string &item) {
            int slot = 0, id = 0;

            if (std::sscanf(item.c_str(), "%*d:%d:%d", &slot, &id) == 2 && slot >= 0 && slot < 60) {
                CStockItem *stock = SaveData->GetStockItem();

                stock->dungeon_items[slot] = (s16) id;
                stock->dungeon_item_vols[slot] = (s16) ItemDataToHaveCopy(id);
                std::fprintf(stderr, "coop-inv: test: item %d into the storage's slot %d at link tick %lld\n", id, slot, now);
            }
        });
    }
    // DC_COOP_BAGLOAD=T:DIR: at link tick T every copy makes the save of an OLD game beside DIR\pN (player 2's bag file written at size 50 while the
    // game's pack is larger: a save from before the party's size) and loads it with CoopInventoryLoad, the slot load's own call
    if (load != nullptr) {
        each(load, [&](const std::string &item) {
            const std::size_t colon = item.find(':');
            if (colon == std::string::npos) {
                return;
            }
            const fs::path dir = fs::path(item.substr(colon + 1)) / ("p" + std::to_string(NetplayLocalPlayer()));
            std::error_code error;
            fs::create_directories(dir, error);
            g_bag.pack.num = 50;
            std::vector<unsigned char> bytes;
            InventoryWrite(bytes);
            std::ofstream(dir / kFileName, std::ios::binary).write(reinterpret_cast<const char *>(bytes.data()), (std::streamsize) bytes.size());
            std::fprintf(stderr, "coop-inv: test: an old save's P2 bag (size 50, the game's pack %d) beside the slot, loaded at link tick %lld\n", (int) status->item_pack.num, now);
            CoopInventoryLoad(dir);
            BagSizesSay("after loading the old save");
        });
    }
    if (profiles != nullptr) {
        each(profiles,[&](const std::string &item) {
            char a[16] = {}, b[16] = {};

            if (std::sscanf(item.c_str(), "%*d:%15[^:]:%15s", a, b) == 2) {
                const std::string names[2] = {a, b};

                std::fprintf(stderr, "coop-inv: test: profiles %s and %s take the seats at link tick %lld\n", a, b, now);
                ProfileBagsGive(*status, names);
            }
        });
    }
    if (report != nullptr) {
        each(report, [&](const std::string &) {
            std::string kept;

            for (const auto &entry : g_kept) {
                kept += " " + entry.first + "=" + std::to_string((int) entry.second.pack.num);
            }
            std::fprintf(stderr, "coop-inv: bag sizes at link tick %lld (player %d's copy): the game's pack %d, the other bag %d, kept:%s\n", now, NetplayLocalPlayer(),
                         (int) status->item_pack.num, (int) Bag().pack.num, kept.empty() ? " none" : kept.c_str());
        });
    }
}
// coop-joinlobby: a joiner's profile bag at a join into a game in progress. The host (ProfileBagsJoinBag, as the catch-up's
// last blob is made) reads the bag the joiner's profile takes: player 2's bag as it is when it is that profile's already (a
// rejoin), else the profile's kept bag, else an empty one; both copies (ProfileBagsJoinTake, at the lockstep, on one tick)
// give it to player 2. The host also files the bag player 2 had under its owner, or as ?P2 when nobody owned it (step 1's
// rule: a nameless bag is never dropped). Bags off (OwnBags): nothing.
bool ProfileBagsJoinBag(const std::string &name, std::vector<unsigned char> &out) {
    out.clear();
    if (!OwnBags() || name.empty() || SaveData == nullptr) {
        return false;
    }
    ProfileBag bag;

    if ((!g_bag_owner[1].empty() && g_bag_owner[1] == name)) {
        bag = BagOf(Bag().pack, g_bag.active_item_vol, g_bag.gilda, g_bag.selected);
    } else if (auto found = g_kept.find(name); found != g_kept.end()) {
        bag = found->second;
    } else {
        bag = EmptyBag(((CUserStatus *) SaveData->GetDngStatus())->item_pack.num);
    }
    out.resize(sizeof bag);
    std::memcpy(out.data(), &bag, sizeof bag);
    return true;
}

void ProfileBagsJoinTake(const std::string &name, const std::vector<unsigned char> &bytes, bool host) {
    if (bytes.size() != sizeof(ProfileBag)) {
        return;
    }
    ProfileBag bag;

    std::memcpy(&bag, bytes.data(), sizeof bag);
    const bool        same = (!g_bag_owner[1].empty() && g_bag_owner[1] == name);
    const std::string was = g_bag_owner[1];

    if (host && !same) {
        const ProfileBag held = BagOf(Bag().pack, g_bag.active_item_vol, g_bag.gilda, g_bag.selected);

        if (!was.empty()) {
            g_kept[was] = held;
        } else if (Carried(held.pack) > 0 || held.gilda > 0) {
            std::string key = "?P2";

            for (int n = 2; g_kept.count(key) != 0; n++) {
                key = "?P2#" + std::to_string(n);
            }
            g_kept[key] = held;
            std::fprintf(stderr, "coop-inv: profile bags: the joiner's seat's nameless bag kept apart as %s (%d carried, %d gilda)\n", key.c_str(),
                         Carried(held.pack), held.gilda);
        }
        g_kept.erase(name);
        if (g_pending_set) {
            g_pending[1] = name;
        }
    }
    if (!same) {
        GiveP2(bag);
    }
    g_bag_owner[1] = name;
    std::fprintf(stderr, "coop-inv: profile bags: the joiner is %s, bag %s (%d carried, %d gilda; was %s's)\n", name.c_str(), same ? "kept (its own already)" : "given",
                 Carried(bag.pack), bag.gilda, was.empty() ? "nobody" : was.c_str());
}

void ProfileBagsWrite(const std::filesystem::path &save_directory) {
    std::error_code error;
    const fs::path  path = save_directory / kBagsFile;

    if (g_bag_owner[0].empty() && g_bag_owner[1].empty() && g_kept.empty()) {
        fs::remove(path, error); // (no lobby named anyone: the slot's bags are the players' as before)
        return;
    }
    nlohmann::json root;

    root["owners"] = {{"p1", g_bag_owner[0]}, {"p2", g_bag_owner[1]}};
    root["fishing"] = FishProfilesJson(g_bag_owner[1]); // coop-eventslots: each profile's own fishing record, and the setting it was kept under
    root["kept"] = nlohmann::json::object();
    for (const auto &[name, bag] : g_kept) {
        root["kept"][name] = BagJson(bag);
    }
    const std::string text = root.dump(2) + "\n";

    if (FilesWrite(path, text.data(), text.size(), true) != FilesResult::kWritten) {
        std::fprintf(stderr, "coop-inv: could not write %s\n", path.string().c_str());
        return;
    }
    std::fprintf(stderr, "coop-inv: profile bags saved (P1 %s, P2 %s, %zu kept)\n", g_bag_owner[0].c_str(), g_bag_owner[1].c_str(), g_kept.size());
}

// Every slot load while the lobby's players are seated (ProfileBagsStart kept them; Jep, 2026-11-03: a save loaded again after
// the start put the old bags back): the loaded slot's owners may be others, so each player takes their profile's bag from
// what the slot holds. Logged "coop-inv: profile bags: given again after the slot's load".
void ProfileBagsAfterLoad() {
    if (!g_pending_set || !OwnBags() || SaveData == nullptr) {
        return;
    }
    std::fprintf(stderr, "coop-inv: profile bags: given again after the slot's load (P1 %s, P2 %s)\n", g_pending[0].c_str(), g_pending[1].c_str());
    ProfileBagsGive(*(CUserStatus *) SaveData->GetDngStatus(), g_pending);
}

void ProfileBagsSay(const char *why) {
    const CUserStatus *status = SaveData != nullptr ? (CUserStatus *) SaveData->GetDngStatus() : nullptr;

    std::fprintf(stderr, "coop-inv: bags now (%s): P1 %d carried %d gilda, P2 %d carried %d gilda\n", why, status != nullptr ? Carried(status->item_pack) : -1,
                 status != nullptr ? (int) status->money : -1, Carried(Bag().pack), (int) g_bag.gilda);

    // coop-worldkeys test hook DC_COOP_TEST_RING_GET=ITEM (l-lobby -Case keyring): the game gives the party ITEM here, as a
    // floor's pickup does (CDngStatusData::GetItem), once; then where the keys are.
    static const int get = TestEnvInt("DC_COOP_TEST_RING_GET", 0);
    static bool      got = false;

    if (get > 0 && !got && status != nullptr) {
        CDngStatusData *dng = (CDngStatusData *) SaveData->GetDngStatus();

        got = true;
        dng->overflow_flag = false;
        const int slot = dng->GetItem(get, 0);

        std::fprintf(stderr, "coop-inv: the game gave item %d (slot %d, bag full asked %d)\n", get, slot, (int) dng->overflow_flag);
    }
    RingSay(why);
}

namespace {

int PackKeys(const ITEM_PACK &pack) {
    int count = 0;

    for (int i = 0; i < 103; i++) {
        count += RingItem(pack.item[i]) ? 1 : 0;
    }
    for (int k = 0; k < 3; k++) {
        count += pack.quick_item_slot[k] != -1 && RingItem(pack.quick_item_slot[k]) ? pack.quick_item_qty[k] : 0;
    }
    return count;
}

} // namespace

// coop-worldkeys: where the keys are, on stderr: "coop-inv: keys now (why): ring R, P1's bag K, P2's bag J[, the game finds
// item I: yes|no]" (the ring "off" without one; I: DC_COOP_TEST_RING_SEED's item, through SearchItemIndexNo).
void RingSay(const char *why) {
    CUserStatus     *status = SaveData != nullptr ? (CUserStatus *) SaveData->GetDngStatus() : nullptr;
    static const int seed = TestEnvInt("DC_COOP_TEST_RING_SEED", 0);
    char             ring[16];
    char             finds[64] = "";

    if (status == nullptr) {
        return;
    }
    std::snprintf(ring, sizeof ring, "%d", PackKeys(g_key_ring));
    if (seed > 0) {
        std::snprintf(finds, sizeof finds, ", the game finds item %d: %s", seed, ((CDngStatusData *) status)->SearchItemIndexNo(seed) >= 0 ? "yes" : "no");
    }
    std::fprintf(stderr, "coop-inv: keys now (%s): ring %s, P1's bag %d, P2's bag %d%s\n", why, RingOn() ? ring : "off", PackKeys(status->item_pack),
                 PackKeys(Bag().pack), finds);
}

// After a slot's load: the seated players' bags (ProfileBagsAfterLoad), then every key the bags still hold (a save from
// before the ring) into the ring. Without the ring, a ring the file carried goes back into player 1's pack, so a save
// played with it still opens its doors. Test hook DC_COOP_TEST_RING_SEED=ITEM: the first load puts ITEM in player 1's pack
// first, as a save from before keeps a key.
void RingAfterLoad() {
    CUserStatus     *status = SaveData != nullptr ? (CUserStatus *) SaveData->GetDngStatus() : nullptr;
    static const int seed = TestEnvInt("DC_COOP_TEST_RING_SEED", 0);
    static bool      seeded = false;

    if (status != nullptr && seed > 0 && !seeded) {
        seeded = true;
        Put(status->item_pack, seed, 0);
        std::fprintf(stderr, "coop-inv: test seed: item %d in player 1's pack, as a save from before\n", seed);
    }
    ProfileBagsAfterLoad();
    if (status == nullptr) {
        return;
    }
    if (RingOn()) {
        RingAdopt(status->item_pack, "player 1's pack");
        RingAdopt(Bag().pack, "player 2's bag");
    } else if (g_key_ring.num == 103) {
        for (int i = 0; i < 103; i++) {
            if (RingItem(g_key_ring.item[i]) && Put(status->item_pack, g_key_ring.item[i], g_key_ring.item_vol[i])) {
                std::fprintf(stderr, "coop-ring: ring off: item %d back into player 1's pack\n", g_key_ring.item[i]);
            }
        }
        RingClear();
    }
    RingSay("after the slot's load");
}

void ProfileBagsSeatsClear() {
    g_pending_set = false;
}

void ProfileBagsRead(const std::filesystem::path &save_directory) {
    g_bag_owner[0].clear();
    g_bag_owner[1].clear();
    g_kept.clear();
    g_owners_for_new_game = false;
    std::ifstream in(save_directory / kBagsFile);

    if (!in) {
        return;
    }
    try {
        const nlohmann::json root = nlohmann::json::parse(in);
        const nlohmann::json owners = root.value("owners", nlohmann::json::object());

        g_bag_owner[0] = owners.value("p1", std::string());
        g_bag_owner[1] = owners.value("p2", std::string());
        for (const auto &[name, bag] : root.value("kept", nlohmann::json::object()).items()) {
            g_kept[name] = JsonBag(bag);
        }
        FishProfilesRead(root, g_bag_owner[1]); // coop-eventslots
    } catch (const std::exception &e) {
        std::fprintf(stderr, "coop-inv: %s not read (%s)\n", kBagsFile, e.what());
        return;
    }
    std::fprintf(stderr, "coop-inv: profile bags loaded (P1's bag %s's, P2's %s's, %zu kept)\n", g_bag_owner[0].c_str(), g_bag_owner[1].c_str(), g_kept.size());
}

} // namespace coop

using namespace coop;

bool CoopInventorySave(const std::filesystem::path &save_directory) {
    // The host keeps the bag; a joiner's copy (player 2 of a link) writes none of its own.
    if (!OwnBags() || NetplayLocalPlayer() == 2 || Mutated("invsave")) {
        return true;
    }

    std::vector<unsigned char> bytes;

    InventoryWrite(bytes);

    // coop-worldkeys: DC_COOP_MUTATE=ringnosave (l-lobby -Case keysave's twin): the file without the ring, so a load loses it.
    if (RingOn() && Mutated("ringnosave") && bytes.size() >= sizeof(ITEM_PACK)) {
        bytes.resize(bytes.size() - sizeof(ITEM_PACK));
    }

    fs::path path = save_directory / kFileName;

    if (FilesWrite(path, bytes.data(), bytes.size(), true) != FilesResult::kWritten) {
        std::fprintf(stderr, "coop-inv: could not write %s\n", path.string().c_str());
        return false;
    }

    std::printf("coop-inv: P2 bag saved beside the slot (%d carried, %d gilda)\n", Carried(g_bag.pack), g_bag.gilda);
    ProfileBagsWrite(save_directory);
    return true;
}

void CoopInventoryLoad(const std::filesystem::path &save_directory) {
    unsigned char buffer[1024];
    fs::path      path = save_directory / kFileName;
    std::size_t   size = FilesRead(path, buffer, sizeof buffer);

    InventoryNewGame();
    ProfileBagsRead(save_directory);

    if (size == 0) {
        std::printf("coop-inv: no P2 bag beside the slot; P2 starts with an empty bag\n");
        RingAfterLoad();
        return;
    }

    if (!InventoryRead(buffer, size)) {
        std::fprintf(stderr, "coop-inv: %s is not a P2 bag; P2 starts with an empty bag\n", path.string().c_str());
        RingAfterLoad();
        return;
    }

    std::printf("coop-inv: P2 bag loaded (%d carried, %d gilda)\n", Carried(g_bag.pack), g_bag.gilda);
    RingAfterLoad();
}

void CoopInventoryDelete(const std::filesystem::path &save_directory) {
    std::error_code error;
    fs::remove(save_directory / kFileName, error);
    fs::remove(save_directory / kBagsFile, error);
}

void CoopInventoryNewGame() {
    InventoryNewGame();
    // (the owners the lobby named for this new game stay; any other new game forgets the last slot's)
    if (!g_owners_for_new_game) {
        g_bag_owner[0].clear();
        g_bag_owner[1].clear();
    }
    g_kept.clear();
    g_owners_for_new_game = false;
}

// Retail's, but a slot player 2 stole (aux_state 2) homes onto the partner while it is on the floor.
// Player 1's slots, and every slot while the bags are shared, move exactly as retail's.
namespace dc::game { // repin: was PORT_OVERRIDE; a recipe removes the game's own definition
void CStealItem::Step() {
    sceVu0FVECTOR player;
    sceVu0FVECTOR partner;
    sceVu0FVECTOR direction;

    sceVu0CopyVector(player, CharaMain.pos);
    player[1] = 15.0f;
    sceVu0CopyVector(partner, player);

    if (coop::g_partner.visible) {
        partner[0] = coop::g_partner.pos[0];
        partner[2] = coop::g_partner.pos[2];
    }

    this->angle += 0.31415927f;

    if (this->angle >= TWO_PI) {
        this->angle -= TWO_PI;
    }

    for (int i = 0; i < STEAL_ITEM_MAX; i++) {
        float *target = this->aux_state[i] == 2 ? partner : player;

        switch (this->state[i]) {
            case StealItemState::Rise:
                this->speed[i] += 0.05f;
                this->pos[i][1] = this->base_height[i] + 12.0f * sinf(this->phase[i]);
                this->phase[i] += 0.034906585f;
                direction[0] = target[0] - this->pos[i][0];
                direction[1] = target[1] - this->pos[i][1];
                direction[2] = target[2] - this->pos[i][2];
                direction[3] = 1.0f;
                sceVu0Normalize(direction, direction);
                sceVu0ScaleVectorXYZ(direction, direction, this->speed[i]);
                this->pos[i][0] += direction[0];
                this->pos[i][1] += direction[1];
                this->pos[i][2] += direction[2];

                if (this->phase[i] >= HALF_PI) {
                    this->state[i] = StealItemState::Home;
                }

                break;

            case StealItemState::Home:
                if (DistVector(target, this->pos[i]) <= 5.0f) {
                    this->state[i] = StealItemState::Arrived;
                    break;
                }

                this->speed[i] += 0.02f;
                direction[0] = target[0] - this->pos[i][0];
                direction[1] = target[1] - this->pos[i][1];
                direction[2] = target[2] - this->pos[i][2];
                direction[3] = 1.0f;
                sceVu0Normalize(direction, direction);
                sceVu0ScaleVectorXYZ(direction, direction, this->speed[i]);
                this->pos[i][0] += direction[0];
                this->pos[i][1] += direction[1];
                this->pos[i][2] += direction[2];
                break;

            default:
                break;
        }
    }
}
} // namespace dc::game

// Retail's, but an arrived slot player 2 stole puts a bag item straight into player 2's bag (with its
// message) instead of handing it to DunMoveChara, which gives the party what it returns. An
// attachment, or a bag item player 2 no longer has room for, goes to the party as retail's does.
namespace dc::game { // repin: was PORT_OVERRIDE; a recipe removes the game's own definition
int CStealItem::checkEvent() {
    for (int i = 0; i < STEAL_ITEM_MAX; i++) {
        if (this->state[i] != StealItemState::Arrived) {
            continue;
        }

        this->state[i] = StealItemState::None;

        bool partners = this->aux_state[i] == 2;

        this->aux_state[i] = -1;

        if (partners && coop::PartnerGetItem(this->item[i], -1, true)) {
            std::printf("coop-inv: P2's stolen item %d into P2's bag\n", this->item[i]);
            continue;
        }

        if (partners) {
            coop::PartnerPickupRaised(this->item[i]); // coop-itemprompt: his tick shows it, but it is hers
        }

        return this->item[i];
    }

    return -1;
}
} // namespace dc::game

namespace coop {

void SlotTrace() {
    static const bool on = TestEnv("DC_COOP_SLOTTRACE") != nullptr;
    static int        was[2] = {-1, -1};
    static int        ticks = 0;

    if (!on) {
        return;
    }
    ticks++; // (cand57: 86c6a100 dropped it; the trace said tick 0 forever and q-p2-quickslot never reached its end line)
    int now[2] = {(int) itemNowSel, (int) Bag().selected};

    if (now[0] != was[0] || now[1] != was[1] || ticks % 120 == 0) {
        InputPadState local = InputGetPad(0);
        std::printf("coop-slot: tick %d P1 slot %d P2 slot %d local %d pad0 buttons %04x stick %d %d menu mode %d at %.0f %.0f task %#x mode %d\n", ticks,
                    now[0], now[1], NetplayLocalPlayer(), (unsigned) local.buttons, (int) local.left_x, (int) local.left_y, PadMenuThreshold(),
                    CharaMain.pos[0], CharaMain.pos[2], (unsigned) gameTask, BtGameModeFlag);
        was[0] = now[0];
        was[1] = now[1];
    }
}

} // namespace coop

// coop-keyring: CDngStatusData::LostGateKey's co-op rule (coop::RingLostGateKey).
bool CoopRingLostGateKey(CDngStatusData *status) {
    return coop::RingLostGateKey(status);
}

// coop-keyfloor: CDngStatusData::GetItem's uses for a new item: a floor key's are its floor (coop::KeyFloorTag).
int CoopKeyFloorTag(int item, int volume) {
    return coop::KeyFloorTag(item, volume);
}

// coop-worldkeys: CDngStatusData's lookups for a ring item (RingOn): the world's ring, whichever status asks (the party's,
// a menu's copy: the ring is in none of them). Each logs "coop-ring: ..." for a give and a loss.
bool CoopRingSearch(int item, int *slot) {
    if (!coop::RingOn() || !coop::RingItem(item)) {
        return false;
    }
    const ITEM_PACK &ring = coop::SharedRing();

    *slot = -1;
    for (int i = 0; i < ring.num && i < 103 && *slot < 0; i++) {
        *slot = ring.item[i] == item ? i : -1;
    }
    return true;
}

bool CoopRingLost(int item, int *slot) {
    if (!CoopRingSearch(item, slot)) {
        return false;
    }
    if (*slot >= 0) {
        ITEM_PACK &ring = coop::SharedRing();

        ring.item[*slot] = -1;
        ring.item_vol[*slot] = 0;
        std::fprintf(stderr, "coop-ring: item %d lost from the ring (slot %d)\n", item, *slot);
    }
    return true;
}

// coop-bags (recipe 0068, EdAddMaxItem): retail's one writer of the bag size; every bag takes it at once.
void CoopBagSizeRaised(int size) {
    coop::BagSizeRaised(size);
}

bool CoopRingCheckGet(int item) {
    if (!coop::RingOn() || !coop::RingItem(item)) {
        return false;
    }
    const ITEM_PACK &ring = coop::SharedRing();

    for (int i = 0; i < ring.num && i < 103; i++) {
        if (ring.item[i] < ITEM_DUNGEON_START) {
            return true;
        }
    }
    return false; // (full: the game's own rule, as before)
}

// coop-worldkeys: the event Use Item window (InitEventItemSelect: a door's, a villager's, the town's) lists the pack it is
// given; with the ring on it gets a view of that pack with the ring's items in its empty slots (past its last slot when
// it has none), and the item the window takes (vanish) leaves the pack or the ring it came from. Logs "coop-ring: the
// Use Item window shows N of the ring's items" and "coop-ring: the Use Item window took item I from the ring".
namespace {

ITEM_PACK  g_event_view{};
ITEM_PACK *g_event_pack = nullptr;
s16        g_event_ring_slot[103]; // the ring's slot each view slot shows, -1 the pack's own

} // namespace

ITEM_PACK *CoopEventItemPack(ITEM_PACK *pack) {
    g_event_pack = nullptr;
    if (pack == nullptr || !coop::RingOn()) {
        return pack;
    }
    const ITEM_PACK &ring = coop::SharedRing();
    int              shown = 0;

    g_event_view = *pack;
    g_event_pack = pack;
    std::fill(std::begin(g_event_ring_slot), std::end(g_event_ring_slot), (s16) -1);
    for (int r = 0; r < ring.num && r < 103; r++) {
        if (!coop::RingItem(ring.item[r])) {
            continue;
        }
        int free = -1;

        for (int i = 0; i < 103 && free < 0; i++) {
            free = g_event_view.item[i] < ITEM_DUNGEON_START && (i < g_event_view.num || i == g_event_view.num) ? i : -1;
        }
        if (free < 0) {
            break;
        }
        if (free == g_event_view.num) {
            g_event_view.num++;
        }
        g_event_view.item[free] = ring.item[r];
        g_event_view.item_vol[free] = ring.item_vol[r];
        g_event_ring_slot[free] = (s16) r;
        shown++;
    }
    std::fprintf(stderr, "coop-ring: the Use Item window shows %d of the ring's items\n", shown);
    return &g_event_view;
}

void CoopEventItemTaken(int slot) {
    if (g_event_pack == nullptr || slot < 0 || slot >= 103) {
        return;
    }
    if (g_event_ring_slot[slot] >= 0) {
        ITEM_PACK &ring = coop::SharedRing();
        const int  r = g_event_ring_slot[slot];

        std::fprintf(stderr, "coop-ring: the Use Item window took item %d from the ring\n", ring.item[r]);
        ring.item[r] = 0;
        g_event_ring_slot[slot] = -1;
        return;
    }
    if (slot < g_event_pack->num) {
        g_event_pack->item[slot] = 0;
    }
}

bool CoopRingGet(int item, int *slot) {
    if (!CoopRingCheckGet(item)) {
        return false;
    }
    ITEM_PACK &ring = coop::SharedRing();

    for (int i = 0; i < ring.num && i < 103; i++) {
        if (ring.item[i] < ITEM_DUNGEON_START) {
            ring.item[i] = (s16) item;
            ring.item_vol[i] = (s16) coop::KeyFloorTag(item, ItemDataToHaveCopy(item));
            *slot = i;
            std::fprintf(stderr, "coop-ring: item %d into the ring (slot %d, player %d's copy)\n", item, i, NetplayRole());
            return true;
        }
    }
    return false;
}

namespace coop {
int OwnBagPlayer() {
    return 2;
}
} // namespace coop

// ---- coop-eventslots: each player's own fishing record (Jep 11-11) ----
namespace coop {
namespace {
struct FishRecord {
    std::int32_t points = 0;
    SV_FISH_DATA log[64] = {};
    std::int32_t mardan = 0;
};

FishRecord                        g_fish_records[kMaxPlayers]; // by actor index; [0] unused (the game's player's is the save's own)
std::map<std::string, FishRecord> g_fish_kept;                 // the records of profiles not playing, by name (coop-bags.json)
bool                              g_fish_saved_per_player = false; // the setting the slot's coop-bags.json last had

FishRecord SaveRecord() {
    FishRecord r;

    if (SaveData != nullptr) {
        r.points = *SaveData->CoopFishingPointWord();
        std::memcpy(r.log, SaveData->CoopFishLog(), sizeof r.log);
        r.mardan = *SaveData->CoopMardanWord();
    }
    return r;
}

void SavePut(const FishRecord &r) {
    if (SaveData != nullptr) {
        *SaveData->CoopFishingPointWord() = r.points;
        std::memcpy(SaveData->CoopFishLog(), r.log, sizeof r.log);
        *SaveData->CoopMardanWord() = r.mardan;
    }
}

// The best of two logs (the larger rank of each fish, as SetFishingRank keeps its best): every entry either has, by fish.
void LogMerge(SV_FISH_DATA *into, const SV_FISH_DATA *from) {
    for (int i = 0; i < 64; i++) {
        if (from[i].rank > into[i].rank) {
            into[i] = from[i];
        }
    }
}

nlohmann::json RecordJson(const FishRecord &r) {
    nlohmann::json log = nlohmann::json::array();

    for (const SV_FISH_DATA &e : r.log) {
        log.push_back({e.fish_id, e.rank, e.unk_8, e.unk_C});
    }
    return {{"points", r.points}, {"mardan", r.mardan}, {"log", log}};
}

FishRecord JsonRecord(const nlohmann::json &j) {
    FishRecord r;

    r.points = j.value("points", 0);
    r.mardan = j.value("mardan", 0);
    const nlohmann::json log = j.value("log", nlohmann::json::array());

    for (std::size_t i = 0; i < 64 && i < log.size(); i++) {
        r.log[i].fish_id = log[i].at(0).get<std::int32_t>();
        r.log[i].rank = log[i].at(1).get<float>();
        r.log[i].unk_8 = log[i].at(2).get<float>();
        r.log[i].unk_C = log[i].at(3).get<float>();
    }
    return r;
}

std::int64_t TotalPoints() {
    std::int64_t total = SaveData != nullptr ? *SaveData->CoopFishingPointWord() : 0;

    for (int k = 1; k < kMaxPlayers; k++) {
        total += g_fish_records[k].points;
    }
    for (const auto &[name, r] : g_fish_kept) {
        total += r.points;
    }
    return total;
}
} // namespace

bool FishingPerPlayer() {
    static const bool test = TestEnvOn("DC_COOP_FISHING_PER_PLAYER");

    return OwnBags() && (test || !dc::platform::ConfigGet().coop_fishing_shared);
}

void FishRecordSwap(int index) {
    if (index <= 0 || index >= kMaxPlayers || SaveData == nullptr || !FishingPerPlayer()) {
        return;
    }
    FishRecord live = SaveRecord();

    SavePut(g_fish_records[index]);
    g_fish_records[index] = live;
}

int FishPointsOf(int index) {
    if (index <= 0 || !FishingPerPlayer()) {
        return SaveData != nullptr ? *SaveData->CoopFishingPointWord() : 0;
    }
    return index < kMaxPlayers ? g_fish_records[index].points : 0;
}

void FishTestSeed() {
    if (static const int points = TestEnvInt("DC_COOP_TEST_FISHING_POINTS_P2", -1); points >= 0) {
        g_fish_records[1] = FishRecord{};
        g_fish_records[1].points = points;
        std::fprintf(stderr, "coop: test: player 2's own fishing points %d\n", points);
    }
}

// The setting changed since the slot's file was written: to each player's, the party's pool stays the game's player's record (the save's)
// and every other player starts from their profile's kept record; to the party's, every kept and playing record's points add to the pool
// and the log takes the best of each fish, the records emptied. No point is lost either way. DC_COOP_MUTATE=fish_switch_drops (twin):
// the other records dropped going shared.
void FishModeSwitch(bool to_per_player) {
    if (to_per_player == g_fish_saved_per_player) {
        return;
    }
    const std::int64_t before = TotalPoints();

    if (!to_per_player && SaveData != nullptr) {
        FishRecord pool = SaveRecord();
        auto       add = [&](FishRecord &r) {
            if (!Mutated("fish_switch_drops")) {
                pool.points += r.points;
                LogMerge(pool.log, r.log);
                pool.mardan += r.mardan;
            }
            r = FishRecord{};
        };

        for (int k = 1; k < kMaxPlayers; k++) {
            add(g_fish_records[k]);
        }
        for (auto &[name, r] : g_fish_kept) {
            add(r);
        }
        g_fish_kept.clear();
        SavePut(pool);
    }
    g_fish_saved_per_player = to_per_player;
    std::fprintf(stderr, "coop-inv: fishing points now %s: %lld points before, %lld after\n", to_per_player ? "each player's" : "the party's",
                 (long long) before, (long long) TotalPoints());
}

// The profiles' records at a seat change (ProfileBagsGive's names): each player takes their profile's record, the one they held is
// kept under its owner. Only while the records are each player's (the party's pool belongs to no seat).
void FishProfilesGive(const std::string owners[2], const std::string names[2]) {
    if (!FishingPerPlayer() || SaveData == nullptr) {
        return;
    }
    const FishRecord held[2] = {SaveRecord(), g_fish_records[1]};

    for (int p = 0; p < 2; p++) {
        if (!owners[p].empty() && owners[p] != names[p]) {
            g_fish_kept[owners[p]] = held[p];
        }
    }
    for (int p = 0; p < 2; p++) {
        if (owners[p] == names[p] || names[p].empty()) {
            continue;
        }
        auto             found = g_fish_kept.find(names[p]);
        const FishRecord r = found != g_fish_kept.end() ? found->second : FishRecord{};

        if (found != g_fish_kept.end()) {
            g_fish_kept.erase(found);
        }
        if (p == 0) {
            SavePut(r);
        } else {
            g_fish_records[1] = r;
        }
        std::fprintf(stderr, "coop-inv: profile fishing: P%d is %s, %d points\n", p + 1, names[p].c_str(), r.points);
    }
}

nlohmann::json FishProfilesJson(const std::string &p2_owner) {
    nlohmann::json records = nlohmann::json::object();

    if (FishingPerPlayer() && !p2_owner.empty()) {
        records[p2_owner] = RecordJson(g_fish_records[1]);
    }
    for (const auto &[name, r] : g_fish_kept) {
        records[name] = RecordJson(r);
    }
    return {{"per_player", FishingPerPlayer()}, {"records", records}};
}

void FishProfilesRead(const nlohmann::json &root, const std::string &p2_owner) {
    g_fish_kept.clear();
    const nlohmann::json fishing = root.value("fishing", nlohmann::json::object());

    g_fish_saved_per_player = fishing.value("per_player", false);
    for (const auto &[name, r] : fishing.value("records", nlohmann::json::object()).items()) {
        if (name == p2_owner && g_fish_saved_per_player) {
            g_fish_records[1] = JsonRecord(r);
        } else {
            g_fish_kept[name] = JsonRecord(r);
        }
    }
}

// Test hook (DC_COOP_TEST_FISH_SWITCH=1, x-eventslots fpswitch): the switch both ways on a made-up save (the pool 100, player 2's own 50
// with a fish, a kept profile's 30 with another), and the profile file written and read back: each step's totals and the log said.
void FishSwitchSelfTest(const char *save_directory) {
    if (save_directory == nullptr || SaveData == nullptr) {
        return;
    }
    FishRecord pool;

    pool.points = 100;
    SavePut(pool);
    g_fish_records[1] = FishRecord{};
    g_fish_records[1].points = 50;
    g_fish_records[1].log[3] = {3, 41.0f, 0.0f, 0.0f};
    g_fish_kept.clear();
    g_fish_kept["BOB"].points = 30;
    g_fish_kept["BOB"].log[5] = {5, 22.0f, 0.0f, 0.0f};
    g_fish_saved_per_player = true;
    // The file: written and read back as the records stand.
    const std::filesystem::path dir = std::filesystem::path(save_directory) / ("fish-switch-test-p" + std::to_string(NetplayLocalPlayer())); // (each copy its own)
    std::error_code             error;

    std::filesystem::create_directories(dir, error);
    {
        std::ofstream out(dir / "coop-bags.json");

        out << nlohmann::json{{"fishing", FishProfilesJson("ANA")}}.dump(2);
    }
    const FishRecord p2_was = g_fish_records[1];
    try {
        std::ifstream in(dir / "coop-bags.json");

        FishProfilesRead(nlohmann::json::parse(in), "ANA");
    } catch (const std::exception &e) {
        std::fprintf(stderr, "coop-inv: test: fish switch: the file not read back (%s)\n", e.what());
    }
    std::fprintf(stderr, "coop-inv: test: fish switch: the file read back: player 2's %d (was %d), BOB's %d, fish 3 %.0f\n", g_fish_records[1].points, p2_was.points,
                 g_fish_kept.count("BOB") != 0 ? g_fish_kept["BOB"].points : -1, (double) g_fish_records[1].log[3].rank);
    FishModeSwitch(false);
    std::fprintf(stderr, "coop-inv: test: fish switch: to the party's: the pool %d, fish 3 %.0f, fish 5 %.0f, others %d\n", *SaveData->CoopFishingPointWord(),
                 (double) SaveData->CoopFishLog()[3].rank, (double) SaveData->CoopFishLog()[5].rank, g_fish_records[1].points + (int) g_fish_kept.size());
    FishModeSwitch(true);
    std::fprintf(stderr, "coop-inv: test: fish switch: to each player's: the game's player's %d, player 2's %d\n", *SaveData->CoopFishingPointWord(), g_fish_records[1].points);
    std::filesystem::remove_all(dir, error);
}
} // namespace coop
