// Per-player inventories and item sending (coop_inventory.hpp). Player 2's bag and purse, the sends
// between the two, the save slot's sidecar, and the test bot (DC_COOP_INVTEST).

#include "coop/game_ns.hpp"
#include "coop/coop_townmenu.hpp" // coop-stage4: CoopTownMenuRunsLocalHere
#include "coop/coop_inventory.hpp"
#include "coop/coop_actor.hpp" // coop-eventslots: kMaxPlayers
#include "coop/coop_audio.hpp" // coop-menuse
#include "coop/coop_worlds.hpp" // coop-dungeonp2
#include "coop/coop_opener.hpp" // coop-atlaparts: AtlaRepairAfterLoad

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
#include "menuetc.hpp" // coop-fishing3: AtoraNameMes (the fishing log's fish names)
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
#include "coop/coop_members.hpp" // coop-memberowner: SetPick
#include "coop/coop_menu.hpp" // coop-itemsend: MenuP2BagInPack
#include "coop/coop_fullmenu.hpp" // coop-bagsendlocal: FullMenuRunsLocalFor
#include "coop/netplay.hpp"
#include "coop/netplay_cmd.hpp" // coop-bagsendlocal: kCmdGive
#include "coop/coop_message.hpp" // coop-itemprompt
#include "coop/coop_party.hpp" // save6: coop-party.dat in the slot self-test

namespace dc::game {
extern s32        mode;
extern s32        gameTask;
extern s32        itemNowSel;
extern CFrameVu1 *CharaFrame;
unsigned int      PortRandState();               // coop-bagoverflow: the overflow desync hook
void              PortRandSetState(unsigned int state);
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

// save6 (Jep 11-12: "If you can safely modify the save to be compatible with 6 players now, I'd prefer it"). coop-p2.dat is
// the version-2 bytes every release wrote (P2's bag, then the key ring: kPrefixSize, a zero ring when the ring is off, which
// every reader skips as its num is not 103), and after them the players' section: magic DCPN, the layout version (3), a
// record count and a record size, then one record per player from 3 on (player number, seeded, pack, quick-use vols,
// gilda, quick-use pick). v0.1.0/v0.1.1 read the file into a 1024-byte buffer (FilesRead stops there) and take only the
// version-2 bytes, so they still load P1 and P2 and the ring from a slot this release wrote; they drop players 3 to 6 if they
// save it again (nobody plays them on those releases). A later layout adds fields at a record's end (a larger record size)
// or a later section: this release reads what it knows and writes a record's later bytes back as it read them. Player 1
// stays in the game's own slot file, never touched here. The section is the slot's alone: the link's state (InventoryWrite,
// InventoryBytes, the checksum) is as before.
constexpr std::uint32_t kMoreMagic = 0x4E504344; // "DCPN"
constexpr std::uint32_t kLayout = 3;             // 2 was coop-p2.dat's own version (kFileVersion), still its header's
constexpr std::size_t   kPrefixSize = 8 + sizeof(ITEM_PACK) + sizeof(std::int32_t) * 3 + 3 + sizeof(ITEM_PACK);
constexpr std::size_t   kRecordSize = 2 + sizeof(ITEM_PACK) + sizeof(std::int32_t) * 3 + 2 + 1;
P2Bag                      g_more[kMaxPlayers];      // players 3 up (index p: player p+1); 0 and 1 unused (the game's pack, g_bag)
std::vector<unsigned char> g_more_tail[kMaxPlayers]; // a later layout's bytes past a record's known ones, kept as read
std::uint32_t              g_more_record = kRecordSize;

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
        // coop-rodring (the developer 11-18: "move the fishing rod to the key items ring then, and let it be accepted from there"): the
        // Fishing Rod is the story's one gift (retail: unsellable, never thrown) and every town's fishing board asks for it
        // (its script's item check, SearchItemIndexNo). In a bag it was the bag of whoever stood there at the gift (player 1),
        // so no other player could ever fish. It is the world's key ring's: the gift lands there on every copy, a save's rod
        // moves there on load (RingAfterLoad), and every board finds it there for every player.
        // DC_COOP_MUTATE=rodbag (twin): a bag's item again, as before.
        case ITEM_FISHING_ROD:
            return !Mutated("rodbag");
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
    if (!playing || !CoopPlainPlay() || (pressed & kInputR3) == 0 || (buttons & kInputL3) != 0 || !OwnBags()) {
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
        if (pack.item[i] == ITEM_FISHING_ROD && RingCount(ITEM_FISHING_ROD) > 0) {
            // coop-rodring: the rod is one (retail gives it once): the ring keeps its own, never a second
            std::fprintf(stderr, "coop-ring: item %d from %s: the ring has one already, not a second\n", pack.item[i], from);
        } else if (!Put(ring, pack.item[i], pack.item_vol[i])) {
            std::fprintf(stderr, "coop-ring: no room in the ring for item %d from %s\n", pack.item[i], from);
            continue;
        } else {
            std::fprintf(stderr, "coop-ring: item %d from %s into the ring\n", pack.item[i], from);
        }
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

// coopoptions (Jep 11-17): game.coop_own_bags and DC_COOP_OWN_BAGS retired: in co-op every player has a bag of their own.
bool OwnBags() {
    return Enabled();
}

namespace {
// coop-pt1111p (pt1111o's find: player 2's old overlay crashed both copies in town with the old shared bags,
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

namespace {
// The bag a player past the first keeps (player 2: g_bag, a later player: g_more), or null.
P2Bag *OwnBagOf(int player) {
    return player == 2 ? &Bag() : player > 2 && player <= kMaxPlayers ? &g_more[player - 1] : nullptr;
}
} // namespace

void PartnerOverflowSet(int player) {
    if (P2Bag *bag = OwnBagOf(player); bag != nullptr) {
        bag->overflow = 1;
        std::printf("coop-inv: P%d's bag is past its room: overflow %d %d %d, her menu asks the throw-away\n", player, (int) bag->pack.item[bag->pack.num],
                    (int) bag->pack.item[bag->pack.num + 1], (int) bag->pack.item[bag->pack.num + 2]);
    }
}

// A bag item in one of the three slots past the bag's size: retail's CheckItemThrow (battlemenu.cpp) for this bag.
static bool PastRoom(const ITEM_PACK &pack) {
    for (int i = pack.num; i < pack.num + 3 && i < 103; i++) {
        if (pack.item[i] >= ITEM_DUNGEON_START) {
            return true;
        }
    }
    return false;
}

// A throw-away is pending while the flag is up or the bag holds items past its room: retail takes the flag as the menu opens
// (movechara) and its throw-away page sets it again (BattleMenuInit), and only the throw sends the items past the room away, so
// between the two (a resync or a save in those ticks, a menu a resync closes) the slots past the room are what is left of it.
// DC_COOP_MUTATE=overflow_flag_only (twin): the flag alone, as before.
bool PartnerOverflowPending(int player) {
    const P2Bag *bag = OwnBagOf(player);
    return bag != nullptr && (bag->overflow != 0 || (PastRoom(bag->pack) && !Mutated("overflow_flag_only")));
}

void PartnerOverflowTake(int player) {
    if (P2Bag *bag = OwnBagOf(player); bag != nullptr) {
        bag->overflow = 0;
    }
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

namespace {

// coop-sendguard: a refused send's line in the help box, in the game's own window (BagScreenHelp): the player whose bag
// was full and the cursor it was refused at. Each copy's own (the sender's screen), outside the state set; the Send line
// comes back when the cursor moves.
int g_full_to = 0;
int g_full_cursor = -1;

// coop-sendguard: a send's command on its way (the owner's copy alone): its slot, so a second Select on it before its tick
// is refused at once rather than heard as a send.
int g_give_slot = -1;

// The bag a player keeps, as every copy holds it outside any menu's view (player 1's in the party status, or in g_bag while
// player 2's is swapped in for her menu or her copy apart; player 2's the other one; a later player's g_more), or null.
ITEM_PACK *PlayerPack(CUserStatus *status, int player) {
    const bool p2_in_status = MenuP2BagInPack() != CoopWorldsBagsSwapped();

    if (player == 1) {
        return p2_in_status ? &Bag().pack : &status->item_pack;
    }
    if (player == 2) {
        return p2_in_status ? &status->item_pack : &Bag().pack;
    }
    P2Bag *bag = OwnBagOf(player);
    return bag != nullptr ? &bag->pack : nullptr;
}

// Who a player's send goes to with two players: the other one. (An old command with no receiver: the sendguard_poke twin.)
int SendReceiver(int sender) {
    return sender == 1 ? 2 : 1;
}

// coop-sendtarget: a test seat (DC_COOP_TEST_SEATS="3=Name[:full]"): player 3 sits in the session with a bag of the party's size
// (empty, or full of Bread), on every copy alike, and the name its profile would show. The link carries two copies today, so the
// third seat is simulated: its bag (g_more, save6) and its row in the send list are what a third linked player's would be.
struct TestSeat {
    bool        on = false;
    bool        full = false;
    std::string name;
};

const TestSeat &Seat3() {
    COOP_TEST_SWITCH(const char *, spec, TestEnv("DC_COOP_TEST_SEATS"));
    static TestSeat    seat;
    static const char *read = nullptr;

    if (spec != read) {
        read = spec;
        seat = TestSeat{};
        if (spec != nullptr && std::strncmp(spec, "3=", 2) == 0) {
            const std::string text = spec + 2;
            const std::size_t colon = text.find(':');

            seat.on = true;
            seat.name = text.substr(0, colon);
            seat.full = colon != std::string::npos && text.substr(colon + 1) == "full";
        }
    }
    return seat;
}

// coop-sendtarget (Jep 11-18, design for three): the players in the session by number, 1 up. Players 1 and 2 are the link's two
// copies (a send went to the other bag whether or not she was linked, and still does); a player past them is in it when the link
// carries one (not yet: the test seat stands in for player 3).
int SessionPlayers(int out[kMaxPlayers]) {
    int count = 0;

    out[count++] = 1;
    out[count++] = 2;
    if (Seat3().on) {
        out[count++] = 3;
    }
    return count;
}

// The name a player's profile shows (the lobby's, as the join notices name it), "P<n>" when it has none: draw only.
std::string ShownName(int player) {
    std::string name = player == 3 && Seat3().on ? Seat3().name : NetplayPlayerName(player);

    return name.empty() ? "P" + std::to_string(player) : name;
}

CUserStatus *SendStatus() {
    // (coop-stage4: in a town the party status is the save's dungeon status, UserStatus a floor's)
    return UserStatus != nullptr ? UserStatus : mode == 2 && SaveData != nullptr ? reinterpret_cast<CUserStatus *>(SaveData->GetDngStatus()) : nullptr;
}

void SayFull(int receiver, int cursor) {
    g_full_to = receiver;
    g_full_cursor = cursor;
}

} // namespace

namespace {

// The item a bag screen send may take from the slot (logged when none), -1 none.
int SendableAt(const ITEM_PACK *pack, int slot) {
    int item = slot >= 0 && slot < pack->num && slot < 103 ? pack->item[slot] : -1;

    if (item < ITEM_DUNGEON_START) {
        std::printf("coop-inv: bag screen send refused: slot %d is empty\n", slot);
        return -1;
    }

    if (!BagItem(item) || StoryItem(item)) {
        std::printf("coop-inv: bag screen send item %d refused: a story item\n", item);
        return -1;
    }
    return item;
}

// The pack a send to the receiver lands in, as every copy holds it outside the menu's view (PlayerPack); the other bag (g_bag)
// when there is no party status, as before.
ITEM_PACK *ReceiverPack(int receiver) {
    CUserStatus *status = SendStatus();
    ITEM_PACK   *pack = status != nullptr ? PlayerPack(status, receiver) : nullptr;

    return pack != nullptr ? pack : &Bag().pack;
}

// The send itself, to the receiver chosen (the other player with two; the list's pick with more).
bool SendTo(ITEM_PACK *pack, int slot, int item, int receiver) {
    ITEM_PACK *other = ReceiverPack(receiver);
    const bool local = (FullMenuRunsLocalFor(NetplayLocalPlayer()) || CoopTownMenuRunsLocalHere(NetplayLocalPlayer())) && !Mutated("bagsend_here");

    // A full bag refuses before anything leaves the sender, as retail's pickup does (CDngStatusData::CheckItemGet: the bag's
    // count against its room, whatever it already holds of the item). Said in the menu's own help box (coop-sendguard: the
    // caption under the other bar drew over the game's menu; DC_COOP_MUTATE=sendguard_caption, twin: the caption, as before).
    if (!HasRoom(*other)) {
        if (Mutated("sendguard_caption")) {
            Notice("The other bag is full");
        } else {
            SayFull(receiver, slot);
        }
        std::printf("coop-inv: bag screen send item %d refused: player %d's bag is full (%d/%d)\n", item, receiver, Carried(*other), other->num);
        std::fflush(stdout);
        return false;
    }

    if (local && slot == g_give_slot) {
        std::printf("coop-inv: bag screen send item %d refused: slot %d's send is on its way\n", item, slot);
        return false;
    }

    int volume = pack->item_vol[slot];

    // coop-bagsendlocal (Jep's 11-04 pack 8 playtest: P2 sent a Repair Powder to P1 and the copies went out of sync): a local
    // menu runs on its owner's copy alone, and only its writes to the party status reach every copy. The other bag lies outside
    // the status (g_bag; his, under her view), so the send is a command (kCmdGive) every copy applies on one tick.
    // coop-sendguard: the whole send is that command: the room looked at, the slot emptied and the item put on its tick
    // (InventoryApplyGive), one decision on every copy. The menu's own clearing of the slot was a poke of its own, applied after
    // the give whatever it found, so a bag filled between the press and the tick sent the item back to a full sender: lost.
    // DC_COOP_MUTATE=sendguard_poke (twin): the slot cleared here, as a poke, and the give only puts, as before.
    // DC_COOP_MUTATE=bagsend_here (twin): put here at once, on the owner's copy alone, as before coop-bagsendlocal.
    // (coop-stage4: a town menu run local too)
    if (local) {
        NetCommand command;
        const bool poke = Mutated("sendguard_poke");

        command.kind = kCmdGive;
        command.a = (std::uint8_t) (poke ? 0 : receiver);
        command.b = (std::uint16_t) item;
        command.c = (std::uint32_t) (std::uint16_t) volume | (poke ? 0u : (std::uint32_t) (slot + 1) << 16);
        NetplayQueueCommand(command);
        if (poke) {
            pack->item[slot] = -1;
            pack->item_vol[slot] = 0;
        } else {
            g_give_slot = slot;
        }
        std::printf("coop-inv: bag screen send item %d (uses %d) from slot %d at tick %d: on its way to player %d\n", item, volume, slot, g_ticks, receiver);
    } else {
        Put(*other, item, volume);
        pack->item[slot] = -1;
        pack->item_vol[slot] = 0;
        std::printf("coop-inv: bag screen send item %d (uses %d) from slot %d at tick %d: sent; shown bag has %d, other bag has %d\n", item, volume,
                    slot, g_ticks, Held(*pack, item), Held(*other, item));
    }
    g_full_to = 0;
    std::fflush(stdout); // a test stops the copies soon after (the log's stdout part)
    return true;
}

// coop-sendtarget (Jep 11-18: with 2 players Select sends at once; with 3+ Select opens a small target list of the other
// players by profile name, a full bag greyed, Cancel backs out): the list, in the Items page's own help box (the game's window:
// BagScreenHelp lays it out, a row per other player, retail's hand at the cursor's row), on the menu's copy alone (draw and
// menu state, outside the state set: the send it ends in is the command every copy applies). Open while the board's cursor
// stays on the item it was opened for.
struct SendList {
    bool        open = false;
    int         slot = -1;
    int         item = -1;
    int         cursor = 0;
    int         count = 0;
    int         players[kMaxPlayers] = {};
    std::string names[kMaxPlayers];
};

SendList g_list;

bool RowFull(int player) {
    return !HasRoom(*ReceiverPack(player));
}

void ListClose(const char *why) {
    if (g_list.open) {
        g_list.open = false;
        std::fprintf(stderr, "coop-inv: send list closed: %s (copy of player %d)\n", why, NetplayRole());
        std::fflush(stderr);
    }
}

} // namespace

bool BagScreenSend(ITEM_PACK *pack, int slot) {
    if (!OwnBags() || pack == nullptr || pack == &Bag().pack || Mutated("bagsend")) {
        return false;
    }

    const int item = SendableAt(pack, slot);

    if (item < 0) {
        return false;
    }

    // The bag shown is its owner's (the game's pack under the menu's view), the other bag the receiver's.
    const int sender = MenuP2BagInPack() != CoopWorldsBagsSwapped() ? 2 : 1;
    int       players[kMaxPlayers];
    const int count = SessionPlayers(players);

    if (count <= 2) {
        return SendTo(pack, slot, item, SendReceiver(sender));
    }

    // Three players or more: the list of the others, its cursor on the first with room. DC_COOP_MUTATE=sendtarget_first (twin):
    // sent at once to the first other player, as before the list.
    g_list = SendList{};
    for (int i = 0; i < count; i++) {
        if (players[i] != sender) {
            g_list.players[g_list.count] = players[i];
            g_list.names[g_list.count] = ShownName(players[i]);
            g_list.count++;
        }
    }
    if (Mutated("sendtarget_first")) {
        return SendTo(pack, slot, item, g_list.players[0]);
    }
    while (g_list.cursor + 1 < g_list.count && RowFull(g_list.players[g_list.cursor])) {
        g_list.cursor++;
    }
    if (RowFull(g_list.players[g_list.cursor])) {
        g_list.cursor = 0;
    }
    g_list.open = true;
    g_list.slot = slot;
    g_list.item = item;

    std::string rows;

    for (int i = 0; i < g_list.count; i++) {
        rows += (i != 0 ? ", " : "") + std::string("player ") + std::to_string(g_list.players[i]) + " '" + g_list.names[i] + "'" +
                (RowFull(g_list.players[i]) ? " (full)" : "");
    }
    std::fprintf(stderr, "coop-inv: send list opened for item %d from slot %d by player %d: %s; cursor on player %d (copy of player %d)\n", item, slot,
                 sender, rows.c_str(), g_list.players[g_list.cursor], NetplayRole());
    std::fflush(stderr);
    return true;
}

int BagScreenTargetsStep(int pressed, int *sound) {
    *sound = -1;
    if (!g_list.open) {
        return 0;
    }
    if (MenuItemPackPt == nullptr || ItemMenuMode.mode != ITEM_MENU_BOARD || ItemMenuMode.board.page != BOARD_PAGE_ITEM ||
        ItemMenuMode.board.cursor != g_list.slot || MenuItemPackPt->item[g_list.slot] != g_list.item) {
        ListClose("the cursor's item changed");
        return 0;
    }

    if ((pressed & (kTargetsUp | kTargetsDown)) != 0) {
        g_list.cursor = (g_list.cursor + ((pressed & kTargetsUp) != 0 ? g_list.count - 1 : 1)) % g_list.count;
        *sound = MENU_SOUND_CURSOR;
        std::fprintf(stderr, "coop-inv: send list cursor on player %d (copy of player %d)\n", g_list.players[g_list.cursor], NetplayRole());
        return 1;
    }
    if ((pressed & kTargetsCancel) != 0) {
        *sound = MENU_SOUND_REFUSE; // (retail's Circle out of the menu's own question, ITEM_MENU_STATE_POWERUP_CONFIRM)
        ListClose("Circle");
        return 1;
    }
    if ((pressed & kTargetsPick) != 0) {
        const int receiver = g_list.players[g_list.cursor];

        if (RowFull(receiver)) {
            *sound = MENU_SOUND_REFUSE;
            std::fprintf(stderr, "coop-inv: send list: player %d's bag is full, its row greyed: refused (copy of player %d)\n", receiver, NetplayRole());
            return 1;
        }
        ListClose("Cross");

        const bool sent = SendTo(MenuItemPackPt, g_list.slot, g_list.item, receiver);

        *sound = sent ? MENU_SOUND_CONFIRM : MENU_SOUND_REFUSE;
        return sent ? 2 : 1;
    }
    return 1;
}

bool BagScreenTargetsOpen() {
    return g_list.open;
}

void BagScreenTargetsClose(const char *why) {
    ListClose(why);
}

// kCmdGive, every copy on its tick (outside any menu's view): a send from a local menu, decided here alike on every copy. The
// item leaves the sender's slot for the receiver's bag when that slot still holds it and the receiver has room; else nothing
// moves and the sender is told (the refuse sound, the help box's line) on their own copy. An old command (receiver 0, no slot:
// the sendguard_poke twin) only puts: a bag filled meanwhile gives it back to the sender's, both full it is lost.
void InventoryApplyGive(int player, const NetCommand &command, std::int64_t tick) {
    // (coop-stage4: in a town the party status is the save's dungeon status, UserStatus a floor's: a town menu's send too)
    CUserStatus *status = UserStatus != nullptr ? UserStatus : mode == 2 && SaveData != nullptr ? reinterpret_cast<CUserStatus *>(SaveData->GetDngStatus()) : nullptr;

    if (status == nullptr || !OwnBags()) {
        return;
    }

    const int  item = command.b;
    const int  volume = (s16) (command.c & 0xFFFF);
    const int  slot = (int) (command.c >> 16) - 1;
    const int  receiver = command.a != 0 ? command.a : SendReceiver(player);
    ITEM_PACK *from = PlayerPack(status, player);
    ITEM_PACK *to = PlayerPack(status, receiver);
    ITEM_PACK &p1 = *PlayerPack(status, 1);
    ITEM_PACK &p2 = *PlayerPack(status, 2);

    if (player == NetplayLocalPlayer()) {
        g_give_slot = -1;
    }
    if (from == nullptr || to == nullptr || from == to) {
        return;
    }

    if (slot < 0) {
        const char *how = Put(*to, item, volume) ? "given" : Put(*from, item, volume) ? "back to the sender (the bag filled)" : "lost (both bags full)";

        std::printf("coop-inv: player %d's send of item %d (uses %d) at link tick %lld: %s; P1 has %d (%d/%d), P2 has %d (%d/%d)\n", player, item, volume,
                    (long long) tick, how, Held(p1, item), Carried(p1), p1.num, Held(p2, item), Carried(p2), p2.num);
        std::fflush(stdout);
        return;
    }

    const char *why = slot >= from->num || slot >= 103 || from->item[slot] != item ? "the slot no longer holds it" : !HasRoom(*to) ? "the bag is full" : nullptr;

    if (why == nullptr) {
        const int uses = from->item_vol[slot];

        from->item[slot] = -1;
        from->item_vol[slot] = 0;
        Put(*to, item, uses);
    } else if (player == NetplayLocalPlayer()) {
        SoundOwner sound(player);

        SndSePlay(MENU_SOUND_REFUSE, -1, 0);
        if (HasRoom(*to) == false) {
            SayFull(receiver, slot);
        }
    }
    std::printf("coop-inv: player %d's send of item %d from slot %d to player %d at link tick %lld: %s%s; P1 has %d (%d/%d), P2 has %d (%d/%d)\n", player,
                item, slot, receiver, (long long) tick, why != nullptr ? "refused: " : "sent", why != nullptr ? why : "", Held(p1, item), Carried(p1), p1.num,
                Held(p2, item), Carried(p2), p2.num);
    if (receiver > 2) { // coop-sendtarget: a later player's bag
        std::printf("coop-inv: player %d has %d of item %d (%d/%d)\n", receiver, Held(*to, item), item, Carried(*to), to->num);
    }
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
    static int          made_full = 0;
    static int          full_frames = 0;
    constexpr int       kFullFrames = 180; // (3 s at most: a menu shut and opened again on that cell does not say it again)
    static int          made_to = 0;
    static std::string  made_shared;
    // (coop-warmconvert, WS7: said again at each job on a warm pair, as a fresh copy says it; trace_once_kept twin: kept)
    COOP_TEST_TRACE_RESET(made, std::string());

    if (!OwnBags() || Mutated("bagsendhelp")) {
        return false;
    }

    BagScreenTrace();

    // coop-sendtarget: the send list in this window: "Send <item> to:" and a row per other player by profile name, a full bag's
    // row greyed (the game's grey), retail's hand at the cursor's row (ClsMes cursor_row; the draw marks the rows under the first
    // as its choices, so the head line stays lit). The rows indented as retail's choice rows sit clear of the hand.
    static bool made_list = false;

    COOP_TEST_TRACE_RESET(made_list, false);
    if (g_list.open) {
        std::string text = dc::localize::LocalizeFormat(dc::localize::LocalizeText("coop.bag.send_pick", "Send %1 to:"), {Name(g_list.item)});
        std::string greyed;

        for (int i = 0; i < g_list.count; i++) {
            const bool full = RowFull(g_list.players[i]);

            text += "\n  " + std::string(full ? "{grey}" : "") + g_list.names[i] + (full ? "{/color}" : "");
            greyed += full ? std::to_string(g_list.players[i]) + " " : "";
        }
        if (mes.mes_made != kHelpId || text != made) {
            if (file.Set(kHelpId, text) < 0) {
                return false;
            }
            made = text;

            short *buff = mes.buff;
            char  *body = mes.text;

            mes.SetBuff(file.Data());
            mes.mes_made = -1;
            mes.MakeMesWin(kHelpId);
            mes.buff = buff;
            mes.text = body;
            std::string said = text; // (one log line: its line breaks as " / ")

            for (std::size_t at = said.find('\n'); at != std::string::npos; at = said.find('\n', at)) {
                said.replace(at, 1, " / ");
            }
            std::fprintf(stderr, "coop-inv: send list in the help box: \"%s\", %d rows, greyed: %s(copy of player %d)\n", said.c_str(), g_list.count,
                         greyed.empty() ? "none " : greyed.c_str(), NetplayRole());
        }
        mes.cursor_row = 1 + g_list.cursor;
        if (!made_list) {
            made_list = true;
            // Where retail's hand stands (ClsMes::DrawMesWin: two characters and 12 pixels left of the text) against the help box's
            // left edge (MenuHelpWinDraw at 0x36) and the text's (0x4A), in screen pixels: the tests' numbers.
            std::fprintf(stderr, "coop-inv: send list hand at x %d, the help box from x %d, rows from x %d (char width %d)\n",
                         BagScreenHelpTextX(0x4A) - 0xC - mes.char_width * 2, 0x36, BagScreenHelpTextX(0x4A), (int) mes.char_width);
        }
        made_to = 0; // (the help's lines made again when the list shuts)
        return true;
    }
    if (made_list) {
        made_list = false;
        mes.cursor_row = -1;
        made_to = 0;
    }

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
    // coop-sendguard: a send refused for a full bag says so on the Send line, in this window, until the cursor moves (as the
    // help follows the cursor); counted per frame drawn, on stderr as it starts and ends (the tests' numbers).
    if (g_full_to != 0 && (ItemMenuMode.board.cursor != g_full_cursor || full_frames >= kFullFrames)) {
        g_full_to = 0;
    }
    const int full = g_full_to;

    if (full != 0) {
        full_frames++;
        if (full_frames == 1) {
            std::fprintf(stderr, "coop-inv: bag screen help says player %d's bag is full (cursor %d, copy of player %d)\n", full, g_full_cursor, NetplayRole());
        }
    } else if (full_frames > 0) {
        std::fprintf(stderr, "coop-inv: bag screen help: the Send line back after %d frames of the full line (copy of player %d)\n", full_frames, NetplayRole());
        full_frames = 0;
    }
    // coop-keypick (Jep, 2026-10-25: "the keys should show up in the inventory"): her bag shown, the party's shared
    // keys and story items sit in the other pack (g_bag holds it while hers is swapped in); a line names them.
    const std::string shared = to == 1 ? SharedLine() : std::string();

    if (mes.mes_made == kHelpId && key == made_key && to == made_to && shared == made_shared && full == made_full) {
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

    // coop-sendtarget: with three players or more Select opens the send list, so its line names nobody ("Send."), and a refusal
    // names the player by profile, as the list does.
    int        players[kMaxPlayers];
    const bool many = SessionPlayers(players) > 2;

    text += "\n{select}" + spacing +
            (full != 0 && many ? dc::localize::LocalizeFormat(dc::localize::LocalizeText("coop.bag.send_full_name", "%1's bag is full."), {ShownName(full)})
             : full != 0       ? dc::localize::LocalizeFormat(dc::localize::LocalizeText("coop.bag.send_full", "P%1's bag is full."), {std::to_string(full)})
             : many            ? std::string(dc::localize::LocalizeText("coop.bag.send_pick_line", "Send."))
                               : dc::localize::LocalizeFormat(dc::localize::LocalizeText("coop.bag.send_to", "Send to P%1."), {std::to_string(to)}));

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
    made_full = full;
    AlignSendLine(mes, key);
    return true;
}

// coop-sendtarget: the help text's left edge (retail's 0x4A), moved right while the send list shows by the room retail's hand
// takes (ClsMes::DrawMesWin draws it two characters and 12 pixels left of the text): at retail's edge it stood left of the help
// box (x 40 against the box's 54).
int BagScreenHelpTextX(int retail_x) {
    return g_list.open && OwnBags() && !Mutated("sendtarget_hand") ? retail_x + kTargetsTextShift : retail_x;
}

int BagScreenHelpExtraLines() {
    if (g_list.open && OwnBags()) {
        return std::max(0, 1 + g_list.count - 2); // coop-sendtarget: the list's head and rows against retail's two lines
    }
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

    // coop-sendtarget: the test seat's bag (DC_COOP_TEST_SEATS), every copy alike: the party's size, empty or full of Bread.
    if (const TestSeat &seat = Seat3(); seat.on && status != nullptr) {
        P2Bag &bag = g_more[2];

        bag = P2Bag{};
        bag.pack.num = status->item_pack.num;
        for (int i = 0; i < 103; i++) {
            bag.pack.item[i] = (s16) (seat.full && i < bag.pack.num ? ITEM_BREAD : -1);
            bag.pack.item_vol[i] = (s16) (seat.full && i < bag.pack.num ? ItemDataToHaveCopy(ITEM_BREAD) : 0);
        }
        for (int k = 0; k < 3; k++) {
            bag.pack.quick_item_slot[k] = -1;
        }
        bag.seeded = true;
        std::fprintf(stderr, "coop-inv: test seat: player 3 '%s' sits in, its bag %d/%d\n", seat.name.c_str(), Carried(bag.pack), bag.pack.num);
    }

    // coop-rodring test hook DC_COOP_TEST_GIVE=ITEM: the story gives the party ITEM here, once, as an event's give does
    // (EdGetItem: GetItem on the save's status), on every copy alike; then where the keys are.
    if (static const int give = TestEnvInt("DC_COOP_TEST_GIVE", 0); give > 0 && SaveData != nullptr) {
        static bool given = false;

        if (!given) {
            given = true;
            const int slot = ((CDngStatusData *) SaveData->GetDngStatus())->GetItem(give, 0);

            std::fprintf(stderr, "coop-inv: test: the story gives item %d (slot %d)\n", give, slot);
            RingSay("after the story's gift");
        }
    }

    if (spec == nullptr || status == nullptr) {
        return;
    }

    // "items/items": player 1's bag's items, then player 2's (comma-separated ids), each with its own uses.
    ITEM_PACK *packs[2] = {&status->item_pack, &Bag().pack};
    int        player = 0;

    for (const char *at = spec; *at != '\0' && player < 2;) {
        int item = std::atoi(at);

        if (item >= ITEM_DUNGEON_START && RingOn() && RingItem(item)) {
            // coop-rodring: a ring item (the fishing rod) is the world's, in no bag: on the ring, once, as the game gives it
            if (RingCount(item) == 0) {
                Put(SharedRing(), item, ItemDataToHaveCopy(item));
            }
        } else if (item >= ITEM_DUNGEON_START) {
            Put(*packs[player], item, ItemDataToHaveCopy(item));
        }
        at += std::strcspn(at, ",/");
        player += *at == '/' ? 1 : 0;
        at += *at != '\0' ? 1 : 0;
    }
    std::printf("coop-inv: test seed: player 1 carries %d, player 2 carries %d\n", Carried(status->item_pack), Carried(Bag().pack));
}

// Test hook (coop-p2controls, DC_COOP_TEST_QUICK=item:count,item:count,item:count): the party pack's three quick-use slots (an item
// of -1 leaves one empty), at the press clock's start, on every copy alike.
void InventoryTestQuick() {
    COOP_TEST_SWITCH(const char *, spec, TestEnv("DC_COOP_TEST_QUICK"));
    CUserStatus *status = UserStatus;

    if (spec == nullptr || status == nullptr) {
        return;
    }
    const char *at = spec;

    for (int k = 0; k < 3 && *at != '\0'; k++) {
        int item = std::atoi(at);
        int count = std::strchr(at, ':') != nullptr ? std::atoi(std::strchr(at, ':') + 1) : 0;

        status->item_pack.quick_item_slot[k] = (s16) (item >= ITEM_DUNGEON_START && count > 0 ? item : -1);
        status->item_pack.quick_item_qty[k] = (s16) (item >= ITEM_DUNGEON_START && count > 0 ? count : 0);
        status->active_item_vol[k] = item >= ITEM_DUNGEON_START && count > 0 ? ItemDataToHaveCopy(item) : 0; // (coop-p2feather2: a feather's uses)
        at += std::strcspn(at, ",");
        at += *at != '\0' ? 1 : 0;
    }
    std::fprintf(stderr, "coop-inv: test: the party's quick-use slots %d x%d, %d x%d, %d x%d\n", status->item_pack.quick_item_slot[0],
                 status->item_pack.quick_item_qty[0], status->item_pack.quick_item_slot[1], status->item_pack.quick_item_qty[1],
                 status->item_pack.quick_item_slot[2], status->item_pack.quick_item_qty[2]);
}

// The quick-use slots her D-pad Left and Right pick between: her own bag's, her pick bag.selected.
static ITEM_PACK &SlotPack() {
    return Bag().pack;
}

int PartnerSlotItem(std::uint16_t pressed) {
    P2Bag     &bag = Bag();
    ITEM_PACK &pack = SlotPack();

    if ((pressed & (kInputLeft | kInputRight)) != 0) {
        // As DunMoveChara moves player 1's: Left from 1 wraps to 3, Right from 3 to 1.
        bag.selected = (pressed & kInputLeft) != 0 ? (bag.selected < 2 ? 3 : bag.selected - 1) : (bag.selected >= 3 ? 1 : bag.selected + 1);
        SndSePlay(MENU_SOUND_CURSOR, -1, 0);
        std::printf("coop-inv: P2 quick-use slot %d%s (item %d x%d)\n", bag.selected, "", pack.quick_item_slot[bag.selected - 1],
                    pack.quick_item_qty[bag.selected - 1]);
    }

    bag.selected = std::clamp((int) bag.selected, 1, 3);

    int item = pack.quick_item_slot[bag.selected - 1];
    return item >= ITEM_DUNGEON_START && pack.quick_item_qty[bag.selected - 1] > 0 ? item : -1;
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
        if (!CoopPlainPlay()) {
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
static int g_cheat_own_debug = -1; // this copy's own file value of game.debug_mode, noted before a joiner takes the host's (the twin's read)

void DransCheatNoteOwnDebug(bool on) {
    if (g_cheat_own_debug < 0) {
        g_cheat_own_debug = on ? 1 : 0;
    }
}

bool DransCheatOn() {
    static const bool env = [] {
        const bool set = TestEnvOn("DC_QA_CHEATS");
        if (set) {
            std::printf("coop-inv: test cheats on (DC_QA_CHEATS)\n");
        }
        return set;
    }();
    if (env) {
        return true;
    }

    // coop-debuggate: also on with debug mode, as the session agrees it: game.debug_mode is one of the settings a link shares (a joiner
    // plays by the host's, NetplayHelloAdopt), so every copy reads the same value, as the cheat's give on the agreed pads needs
    // (a feather on one copy only is a desync). Never the copy's own file. DC_COOP_MUTATE=dranscheat_own (twin): the own file's.
    const bool agreed = dc::platform::ConfigGet().debug_mode;
    const bool on = Mutated("dranscheat_own") && g_cheat_own_debug >= 0 ? g_cheat_own_debug != 0 : agreed;
    static bool said = false;

    if (on && !said) {
        said = true;
        std::printf("coop-inv: test cheats on (game.debug_mode%s)\n", Mutated("dranscheat_own") ? ", this copy's own: twin" : ", the session's");
    }
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
void OverflowTestTick(); // coop-bagoverflow (below)

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
    OverflowTestTick();
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
    // (coop-bagoverflow: the bag's overflow flag in the selection byte's top bit, which a selection of 1 to 3 never uses (an older
    // file has it clear): a resync and the slot's file carry it and the checksum reads it with the rest of the bag.
    // DC_COOP_MUTATE=overflow_not_carried (twin, with overflow_flag_only: a resync keeps her throw-away by either): left out, as before)
    out.push_back((unsigned char) (bag.selected | (bag.overflow != 0 && !Mutated("overflow_not_carried") ? 0x80 : 0)));

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
    for (int p = 0; p < kMaxPlayers; p++) { // save6: players 3 up
        g_more[p] = P2Bag{};
        g_more_tail[p].clear();
    }
    g_more_record = kRecordSize;
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
    bag.selected = std::clamp((int) (data[2] & 0x7F), 1, 3);
    bag.overflow = (data[2] & 0x80) != 0 ? 1 : 0; // coop-bagoverflow

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
    std::int32_t  overflow = 0; // coop-bagoverflow: past its room (its three slots past pack.num hold the rest), as P2Bag::overflow
};

constexpr const char *kBagsFile = "coop-bags.json";

} // namespace
// coop-eventslots: each profile's own fishing record (the file's end). owners: by player index, as g_bag_owner.
nlohmann::json FishProfilesJson(const std::string owners[]);
void           FishProfilesRead(const nlohmann::json &root, const std::string owners[]);
void           FishModeSwitch(bool to_per_player);
// coop-fishpools: the joiner's profile record with their bag (ProfileBagsJoinBag/Take, below them in the file).
void FishJoinRecord(int p, const std::string &name, std::vector<unsigned char> &out);
void FishJoinTake(int p, const std::string &name, const unsigned char *bytes, std::size_t size, bool host);
namespace {

// candreds2 (2026-11-12, design for 3+): the owner records are per player (index p: player p+1), not a "player 2" slot. The
// players with a live bag today are kBagSeats: player 1's is the game's own (the status), player 2's is g_bag (coop-p2.dat); a
// third player's live bag comes with its own storage, and HeldBag/GiveBag are the one place it goes in. coop-bags.json keeps
// its shape: "owners" {"p1", "p2"} always (a player past kBagSeats as "p<n>" only once one owns a bag), so old saves load and a
// two-player slot's file is byte for byte as before.
constexpr int                     kBagSeats = 2;
std::string                       g_bag_owner[kMaxPlayers]; // the profile each player's bag is ("" not known: a save from before)
std::map<std::string, ProfileBag> g_kept;                   // the bags of profiles not playing, by name
bool                              g_owners_for_new_game = false; // the lobby named the owners of the new game about to start
// The lobby's players, kept for the slot's own load after the lobby (Host > Continue reads the slot again as the game starts:
// Jep's pack 7 log, 2026-11-03, P2's bag back from coop-p2.dat over her profile's), then given their bags again there.
std::vector<std::string>          g_pending;
bool                              g_pending_set = false;

void OwnersClear() {
    for (auto &o : g_bag_owner) {
        o.clear();
    }
}

// "P1 a, P2 b" (each bag seat, and a later player once named): the log lines' list, as the two-player lines read before.
std::string PlayersSaid(const std::string names[], int count) {
    std::string s;

    for (int p = 0; p < count; p++) {
        if (p >= kBagSeats && names[p].empty()) {
            continue;
        }
        s += (s.empty() ? "P" : ", P") + std::to_string(p + 1) + " " + names[p];
    }
    return s;
}

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
    status.overflow_flag = bag.overflow != 0 ? 1 : status.overflow_flag; // coop-bagoverflow (retail's own flag: never cleared here)
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
    g_bag.overflow = bag.overflow; // coop-bagoverflow
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
    // coop-bagoverflow: a bag past its room (a pick given into its three slots past its size, the throw-away not yet taken) keeps
    // those slots and its flag, so a bag filed away then loses nothing. DC_COOP_MUTATE=kept_json_to_num (twin): up to its size, as before.
    if (!Mutated("kept_json_to_num")) {
        nlohmann::json past = nlohmann::json::array();

        for (int i = bag.pack.num; i < bag.pack.num + 3 && i < 103; i++) {
            if (bag.pack.item[i] >= ITEM_DUNGEON_START) {
                past.push_back({i - bag.pack.num, bag.pack.item[i], bag.pack.item_vol[i]});
            }
        }
        if (!past.empty() || bag.overflow != 0) {
            out["past_room"] = past;
            out["overflow"] = bag.overflow;
        }
    }
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
    for (const auto &slot : in.value("past_room", nlohmann::json::array())) { // coop-bagoverflow
        if (slot.is_array() && slot.size() == 3 && slot[0].get<int>() >= 0 && slot[0].get<int>() < 3 && bag.pack.num + slot[0].get<int>() < 103) {
            bag.pack.item[bag.pack.num + slot[0].get<int>()] = slot[1].get<s16>();
            bag.pack.item_vol[bag.pack.num + slot[0].get<int>()] = slot[2].get<s16>();
        }
    }
    bag.overflow = in.value("overflow", 0) != 0 ? 1 : 0;
    bag.pack.item_count = (s8) Carried(bag.pack);
    return bag;
}

} // namespace

void ProfileBagsGive(CUserStatus &status_ref, const std::vector<std::string> &players);

// save6: player p's live bag past player 2 (index p: player p+1), made empty at the party's size the first time it is used,
// as Bag() makes player 2's. Saved in coop-p2.dat's players' section; nobody plays these seats yet.
P2Bag &MoreBag(int p) {
    P2Bag &bag = g_more[p];

    if (!bag.seeded) {
        const CUserStatus *status = SaveData != nullptr ? (CUserStatus *) SaveData->GetDngStatus() : nullptr;

        bag = P2Bag{};
        bag.pack = EmptyBag(status != nullptr && status->item_pack.num > 0 ? status->item_pack.num : 50).pack;
        bag.seeded = true;
    }
    return bag;
}

void GiveMore(int p, const ProfileBag &bag) {
    P2Bag &more = g_more[p];

    more.pack = bag.pack;
    if (!Mutated("bagsize") && SaveData != nullptr && ((CUserStatus *) SaveData->GetDngStatus())->item_pack.num > 0) {
        BagSizeTo(more.pack, ((CUserStatus *) SaveData->GetDngStatus())->item_pack.num); // (the party's size, as GiveP2)
    }
    std::memcpy(more.active_item_vol, bag.active_item_vol, sizeof more.active_item_vol);
    more.gilda = bag.gilda;
    more.selected = std::clamp((int) bag.selected, 1, 3);
    more.overflow = bag.overflow; // coop-bagoverflow
    more.seeded = true;
}

// candreds2: player p's live bag (index p: player p+1), and giving it. The one place a player's bag storage is named:
// player 1's is the game's status, player 2's g_bag, a later player's g_more (save6).
bool HeldBag(int p, const CUserStatus &status, ProfileBag &out) {
    if (p == 0) {
        out = BagOf(status.item_pack, status.active_item_vol, status.money, 1);
        out.overflow = status.overflow_flag != 0 ? 1 : 0; // coop-bagoverflow
    } else if (p == 1) {
        out = BagOf(Bag().pack, g_bag.active_item_vol, g_bag.gilda, g_bag.selected);
        out.overflow = g_bag.overflow;
    } else if (p < kMaxPlayers) {
        const P2Bag &more = MoreBag(p);

        out = BagOf(more.pack, more.active_item_vol, more.gilda, more.selected);
        out.overflow = more.overflow;
    }
    return p >= 0 && p < kMaxPlayers;
}

bool GiveBag(int p, CUserStatus &status, const ProfileBag &bag) {
    if (p == 0) {
        GiveP1(status, bag);
    } else if (p == 1) {
        GiveP2(bag);
    } else if (p < kMaxPlayers) {
        GiveMore(p, bag);
    }
    return p >= 0 && p < kMaxPlayers;
}

void ProfileBagsStart(const std::vector<std::string> &players, bool new_game) {
    // DC_COOP_MUTATE=profilebags (l-lobby -Case bags' twin): the bags stay where the save had them.
    if (!OwnBags() || Mutated("profilebags")) {
        std::fprintf(stderr, "coop-inv: profile bags: %s\n", OwnBags() ? "left as they are (DC_COOP_MUTATE=profilebags)" : "off (one bag for the party)");
        return;
    }
    CUserStatus *status = SaveData != nullptr ? (CUserStatus *) SaveData->GetDngStatus() : nullptr;

    g_pending.assign(players.begin(), players.begin() + std::min<std::size_t>(players.size(), kMaxPlayers));
    g_pending.resize(std::max<std::size_t>(g_pending.size(), kBagSeats));
    if (new_game || status == nullptr) {
        // A new game: every player starts with the game's empty bag; they are the players' from here.
        g_kept.clear();
        OwnersClear();
        for (std::size_t p = 0; p < g_pending.size(); p++) {
            g_bag_owner[p] = g_pending[p];
        }
        g_owners_for_new_game = new_game;
        g_pending_set = !Mutated("profilebags_once");
        std::fprintf(stderr, "coop-inv: profile bags: a new game, %s\n", PlayersSaid(g_pending.data(), (int) g_pending.size()).c_str());
        return;
    }
    g_pending_set = !Mutated("profilebags_once"); // (DC_COOP_MUTATE=profilebags_once, twin: the start's giving alone, none at loads)
    FishModeSwitch(FishingPerPlayer()); // coop-eventslots: the setting changed since the slot's file: every point kept
    ProfileBagsGive(*status, g_pending);
    RingSay("after the lobby's start"); // coop-worldkeys
}

void FishProfilesGive(const std::string owners[], const std::string names[], int count); // (coop-eventslots, below)

void ProfileBagsGive(CUserStatus &status_ref, const std::vector<std::string> &players) {
    CUserStatus *status = &status_ref;
    std::string  names[kMaxPlayers];

    for (std::size_t p = 0; p < players.size() && p < (std::size_t) kMaxPlayers; p++) {
        names[p] = players[p];
    }
    // (every player with a bag seat, and any later one named: the loops below go over these)
    int count = kBagSeats;

    for (int p = kBagSeats; p < kMaxPlayers; p++) {
        if (!names[p].empty() || !g_bag_owner[p].empty()) {
            count = p + 1;
        }
    }
    FishProfilesGive(g_bag_owner, names, count); // coop-eventslots: each player's own fishing record follows their profile, as their bag

    // The bags as they are, under their owners (a save from before the profile bags: whoever plays now owns them).
    ProfileBag held[kMaxPlayers];
    bool       seated[kMaxPlayers] = {};

    for (int p = 0; p < count; p++) {
        seated[p] = HeldBag(p, *status, held[p]);
    }

    // coop-joinlobby (Jep, 2026-11-07: a nameless bag is never dropped). A bag nobody owns (a seat nobody named: a lobby
    // started alone, a save from before the profile bags) stays in its seat while nobody takes that seat, and is kept apart
    // under a name no player can type ("?P2": the lobby's names are A-Z and 0-9) when its seat's new player has a bag of
    // their own. Before, both were dropped. (DC_COOP_MUTATE=nameless_drop, l-lobby -Case nameless's twins: dropped again.)
    const bool keep_nameless = !Mutated("nameless_drop");
    bool       stays[kMaxPlayers] = {};

    for (int p = 0; p < count; p++) {
        if (!seated[p]) {
            continue; // (no live bag for this player yet: nothing of theirs to file)
        }
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
    for (int p = 0; p < count; p++) {
        if (!seated[p]) {
            std::fprintf(stderr, "coop-inv: profile bags: P%d is %s, no bag seat yet: its bag stays kept\n", p + 1, names[p].empty() ? "?" : names[p].c_str());
            continue;
        }
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
        GiveBag(p, *status, bag);
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
    for (int p = 2; p < kMaxPlayers; p++) { // save6: players 3 up, once they have a bag
        if (g_more[p].seeded) {
            BagSizeTo(g_more[p].pack, size);
        }
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
    for (int p = 2; p < kMaxPlayers; p++) { // save6: players 3 up, once they have a bag
        if (g_more[p].seeded && g_more[p].pack.num < status->item_pack.num) {
            BagSizeTo(g_more[p].pack, status->item_pack.num);
        }
    }
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

    if (up == nullptr && profiles == nullptr && report == nullptr && load == nullptr && TestEnv("DC_COOP_BAGGIVE") == nullptr && TestEnv("DC_COOP_STOCKGIVE") == nullptr &&
        TestEnv("DC_COOP_BAGHOLD") == nullptr) {
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
    // coop-sendguard: DC_COOP_BAGHOLD=T:ITEM[,...]: at link tick T each copy says how many of ITEM each of the two players' bags
    // holds and how full each is, as every copy holds them outside any menu's view
    static const char *hold = TestEnv("DC_COOP_BAGHOLD");

    if (hold != nullptr) {
        each(hold, [&](const std::string &item) {
            int id = 0;

            if (std::sscanf(item.c_str(), "%*d:%d", &id) == 1) {
                const ITEM_PACK &p1 = *PlayerPack(status, 1);
                const ITEM_PACK &p2 = *PlayerPack(status, 2);

                std::fprintf(stderr, "coop-inv: test: at link tick %lld (player %d's copy) item %d: P1 holds %d (%d/%d), P2 holds %d (%d/%d)\n", now,
                             NetplayLocalPlayer(), id, Held(p1, id), Carried(p1), p1.num, Held(p2, id), Carried(p2), p2.num);
                if (Seat3().on) { // coop-sendtarget: the test seat's bag too
                    const ITEM_PACK &p3 = *PlayerPack(status, 3);

                    std::fprintf(stderr, "coop-inv: test: at link tick %lld (player %d's copy) item %d: P3 holds %d (%d/%d)\n", now, NetplayLocalPlayer(), id,
                                 Held(p3, id), Carried(p3), p3.num);
                }
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
                const std::vector<std::string> names = {a, b};

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
// cand1112 (Jep): the give is always the blob's bag, even when this copy's owner record already names the joiner. The
// record is not in the state set: a lobby joiner that loaded in first (coop-joinload: its own copy took the profile's bag
// and the record at the load) then merges by the host's state set, whose bag is the host's checkpoint's (the seat's
// nameless bag, from before the host's off-tick give at its answer). Skipping the give on "its own already" kept that bag
// on the joiner's copy while the host had the profile's: a desync at the first checksum (l-lobby-join-card). The one
// source at the join is the joiner's profile bag the host put in the blob, given on every copy at one tick; the record
// decides only what the host files. DC_COOP_MUTATE=join_bag_by_owner (twin): the record decides the give again.
// candreds2: by the joiner's player number (1-based), not a fixed player 2: the joiner's seat's record and bag.
bool ProfileBagsJoinBag(int player, const std::string &name, std::vector<unsigned char> &out) {
    out.clear();
    const int p = player - 1;

    if (!OwnBags() || name.empty() || SaveData == nullptr || p < 0 || p >= kMaxPlayers) {
        return false;
    }
    ProfileBag bag;

    if (!g_bag_owner[p].empty() && g_bag_owner[p] == name && HeldBag(p, *(CUserStatus *) SaveData->GetDngStatus(), bag)) {
        // (its seat's bag as it is: a rejoin)
    } else if (auto found = g_kept.find(name); found != g_kept.end()) {
        bag = found->second;
    } else {
        bag = EmptyBag(((CUserStatus *) SaveData->GetDngStatus())->item_pack.num);
    }
    out.resize(sizeof bag);
    std::memcpy(out.data(), &bag, sizeof bag);
    FishJoinRecord(p, name, out); // coop-fishpools: their fishing record after it
    return true;
}

void ProfileBagsJoinTake(int player, const std::string &name, const std::vector<unsigned char> &bytes, bool host) {
    const int p = player - 1;

    if (bytes.size() < sizeof(ProfileBag) || p < 0 || p >= kMaxPlayers || SaveData == nullptr) {
        return;
    }
    // coop-fishpools: their fishing record (after the bag) takes their seat first, the seat's old one kept under its owner (the host)
    FishJoinTake(p, name, bytes.data() + sizeof(ProfileBag), bytes.size() - sizeof(ProfileBag), host);
    CUserStatus &status = *(CUserStatus *) SaveData->GetDngStatus();
    ProfileBag   bag;

    std::memcpy(&bag, bytes.data(), sizeof bag);
    const bool        same = (!g_bag_owner[p].empty() && g_bag_owner[p] == name);
    const std::string was = g_bag_owner[p];
    ProfileBag        held;

    if (host && !same && HeldBag(p, status, held)) {
        if (!was.empty()) {
            g_kept[was] = held;
        } else if (Carried(held.pack) > 0 || held.gilda > 0) {
            const std::string seat = "?P" + std::to_string(player);
            std::string       key = seat;

            for (int n = 2; g_kept.count(key) != 0; n++) {
                key = seat + "#" + std::to_string(n);
            }
            g_kept[key] = held;
            std::fprintf(stderr, "coop-inv: profile bags: the joiner's seat's nameless bag kept apart as %s (%d carried, %d gilda)\n", key.c_str(),
                         Carried(held.pack), held.gilda);
        }
        g_kept.erase(name);
        if (g_pending_set) {
            g_pending.resize(std::max<std::size_t>(g_pending.size(), (std::size_t) player));
            g_pending[p] = name;
        }
    }
    if (p >= kBagSeats) {
        std::fprintf(stderr, "coop-inv: profile bags: the joiner (P%d) is %s, no bag seat yet: its bag stays kept\n", player, name.c_str());
        return;
    }
    const bool give = !same || !Mutated("join_bag_by_owner");

    if (give) {
        GiveBag(p, status, bag);
    }
    g_bag_owner[p] = name;
    std::fprintf(stderr, "coop-inv: profile bags: the joiner is %s, bag %s (%d carried, %d gilda; was %s's)\n", name.c_str(),
                 !same ? "given" : give ? "given again (its own already)" : "kept (its own already)",
                 Carried(bag.pack), bag.gilda, was.empty() ? "nobody" : was.c_str());
    if (TestEnv("DC_COOP_TEST_ROD_SAVE") != nullptr) {
        RingSay("after the join's bag"); // (coop-rodring's test: where the keys are on each copy once she has her bag)
    }
}

// coop-bagoverflow test hooks (x-clown-full -Case resync | save), once, the first tick player 2's throw-away is pending (her menu
// up and her bag holding a pick past its room: the same tick on every copy):
//   DC_COOP_TEST_OVERFLOW_DESYNC=1  copy 2's game random knocked (a desync: the link resyncs, her bag by its bytes)
//   DC_COOP_TEST_OVERFLOW_SAVE=1   every copy: her bag through the slot file's bytes (InventoryWrite / InventoryRead, as the save
//                                  writes and a load reads coop-p2.dat) and through a kept profile's JSON (BagJson / JsonBag, as
//                                  coop-bags.json files a bag), each said: the slot past the room and the flag, before and after
void OverflowTestTick() {
    static const bool desync = TestEnvInt("DC_COOP_TEST_OVERFLOW_DESYNC", 0) != 0;
    static const bool save = TestEnvInt("DC_COOP_TEST_OVERFLOW_SAVE", 0) != 0;
    static bool       done = false;
    static long long  said = -1;
    const int         at = g_bag.pack.num;

    // (-Case resync: her bag as each copy holds it, every 60 link ticks from the knock on: what the opening after the resync reads)
    if (desync && done && NetplayLinkTick() >= said + 60 && at > 0 && at < 100) {
        said = NetplayLinkTick();
        std::fprintf(stderr, "coop-inv: test: her bag at link tick %lld: past the room %d, flag %d, pending %d, her menu %d\n", said,
                     (int) g_bag.pack.item[at], (int) g_bag.overflow, (int) PartnerOverflowPending(2), (int) PlayerInMenu(2, kActorMenuFull));
    }
    if ((!desync && !save) || done || !OwnBags() || SaveData == nullptr || !PlayerInMenu(2, kActorMenuFull) || at <= 0 || at >= 100 ||
        g_bag.pack.item[at] < ITEM_DUNGEON_START) {
        return;
    }
    done = true;
    if (desync && NetplayLocalPlayer() > 1) {
        PortRandSetState(PortRandState() ^ 0x5a5a5a5au);
        std::fprintf(stderr, "netplay: test: copy 2's game random knocked with her throw-away pending at link tick %lld (a desync)\n",
                     (long long) NetplayLinkTick());
    }
    if (save) {
        ProfileBag held;

        HeldBag(1, *(CUserStatus *) SaveData->GetDngStatus(), held);
        const ProfileBag back = JsonBag(BagJson(held));
        const P2Bag      was = g_bag;
        std::vector<unsigned char> bytes;

        InventoryWrite(bytes);
        const bool read = InventoryRead(bytes.data(), bytes.size());

        // (pending: PartnerOverflowPending's test on each bag read back: the flag, or items past the room)
        const bool file_pending = PartnerOverflowPending(2);
        const bool json_pending = back.overflow != 0 || (PastRoom(back.pack) && !Mutated("overflow_flag_only"));

        std::fprintf(stderr, "coop-inv: test: her bag through the slot's file (read %d): past the room %d, pending %d (was %d, flag %d)\n", (int) read,
                     (int) g_bag.pack.item[at], (int) file_pending, (int) was.pack.item[at], (int) was.overflow);
        std::fprintf(stderr, "coop-inv: test: her bag through a kept profile's JSON: past the room %d, pending %d (was %d, flag %d)\n",
                     (int) back.pack.item[back.pack.num], (int) json_pending, (int) held.pack.item[held.pack.num], (int) held.overflow);
    }
}

void ProfileBagsWrite(const std::filesystem::path &save_directory) {
    std::error_code error;
    const fs::path  path = save_directory / kBagsFile;

    if (std::all_of(std::begin(g_bag_owner), std::end(g_bag_owner), [](const std::string &o) { return o.empty(); }) && g_kept.empty()) {
        fs::remove(path, error); // (no lobby named anyone: the slot's bags are the players' as before)
        return;
    }
    nlohmann::json root;

    // (each bag seat as "p<n>" always, a later player only once named: a two-player slot's file as before)
    root["owners"] = nlohmann::json::object();
    for (int p = 0; p < kMaxPlayers; p++) {
        if (p < kBagSeats || !g_bag_owner[p].empty()) {
            root["owners"]["p" + std::to_string(p + 1)] = g_bag_owner[p];
        }
    }
    root["fishing"] = FishProfilesJson(g_bag_owner); // coop-eventslots: each profile's own fishing record, and the setting it was kept under
    root["kept"] = nlohmann::json::object();
    for (const auto &[name, bag] : g_kept) {
        root["kept"][name] = BagJson(bag);
    }
    const std::string text = root.dump(2) + "\n";

    if (FilesWrite(path, text.data(), text.size(), true) != FilesResult::kWritten) {
        std::fprintf(stderr, "coop-inv: could not write %s\n", path.string().c_str());
        return;
    }
    std::fprintf(stderr, "coop-inv: profile bags saved (%s, %zu kept)\n", PlayersSaid(g_bag_owner, kMaxPlayers).c_str(), g_kept.size());
}

// Every slot load while the lobby's players are seated (ProfileBagsStart kept them; Jep, 2026-11-03: a save loaded again after
// the start put the old bags back): the loaded slot's owners may be others, so each player takes their profile's bag from
// what the slot holds. Logged "coop-inv: profile bags: given again after the slot's load".
void ProfileBagsAfterLoad() {
    if (!g_pending_set || !OwnBags() || SaveData == nullptr) {
        return;
    }
    std::fprintf(stderr, "coop-inv: profile bags: given again after the slot's load (%s)\n", PlayersSaid(g_pending.data(), (int) g_pending.size()).c_str());
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
    static const int seed = TestEnvInt("DC_COOP_TEST_RING_SEED", TestEnv("DC_COOP_TEST_ROD_SAVE") != nullptr || TestEnvInt("DC_COOP_TEST_GIVE", 0) > 0 ? ITEM_FISHING_ROD : 0);
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

// coop-ringguard (cand1112b, after coop-ringfix: save6 changed the files the ring is saved in): around a resync's state take
// (NetplayModeEnded). Before: this copy's own ring is kept for the twin. After: with DC_COOP_TEST_RING_SEED set, "coop-inv:
// keys now (after the resync's state): ..." on every copy. DC_COOP_MUTATE=ring_not_carried (twin): the state's ring is not
// taken (this copy's own is put back), as a resync that left the ring out.
void RingStateTake(bool before) {
    static ITEM_PACK kept;
    static const int seed = TestEnvInt("DC_COOP_TEST_RING_SEED", 0);

    if (before) {
        kept = g_key_ring;
        return;
    }
    if (Mutated("ring_not_carried")) {
        g_key_ring = kept;
        std::fprintf(stderr, "coop-ring: test: ring_not_carried: this copy's own ring kept over the state's\n");
    }
    if (seed > 0) {
        RingSay("after the resync's state");
    }
}

namespace {

// coop-rodring: every bag the slot keeps gives its ring items to the ring: player 1's pack, player 2's bag, a later player's
// (g_more) and every profile's kept bag (g_kept: a profile not playing, whose bag a later lobby or join gives back). A save
// from before the ring (or from before the rod was the ring's) can hold them in any of these, and BagOf, which files a seat's
// bag under its profile, leaves ring items out (gone), so they move first. Once moved no bag holds one: later loads move none.
// DC_COOP_MUTATE=rodkept (twin): the two live bags alone, after the profiles' bags, as before.
void RingAdoptAll(CUserStatus *status, bool before_bags) {
    if (!RingOn()) {
        return;
    }
    const bool all = !Mutated("rodkept");

    if (before_bags && !all) {
        return;
    }
    if (status != nullptr) {
        RingAdopt(status->item_pack, "player 1's pack");
    }
    if (!before_bags || g_bag.seeded) { // (before the bags, a bag the slot had: Bag() would seed an empty one first)
        RingAdopt(Bag().pack, "player 2's bag");
    }
    for (int p = 2; p < kMaxPlayers && all; p++) {
        if (g_more[p].seeded) {
            const std::string from = "player " + std::to_string(p + 1) + "'s bag";

            RingAdopt(g_more[p].pack, from.c_str());
        }
    }
    for (auto it = g_kept.begin(); it != g_kept.end() && all; ++it) {
        const std::string from = it->first + "'s kept bag";

        RingAdopt(it->second.pack, from.c_str());
    }
}

} // namespace

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
    // coop-rodring test hook DC_COOP_TEST_ROD_SAVE=where,where (p1: player 1's pack; p2: player 2's bag; a profile's name: its
    // kept bag): a save from before keeps the fishing rod there, at every load of the slot (as its files hold it until a save).
    if (static const char *rod = TestEnv("DC_COOP_TEST_ROD_SAVE"); rod != nullptr && status != nullptr) {
        for (const char *at = rod; *at != '\0';) {
            const std::string where(at, std::strcspn(at, ","));
            ITEM_PACK        *pack = where == "p1" ? &status->item_pack : where == "p2" ? &Bag().pack : nullptr;

            if (pack == nullptr && g_kept.count(where) != 0) {
                pack = &g_kept[where].pack;
            }
            if (pack != nullptr && Put(*pack, ITEM_FISHING_ROD, ItemDataToHaveCopy(ITEM_FISHING_ROD))) {
                std::fprintf(stderr, "coop-inv: test: the save keeps the fishing rod in %s's bag\n", where.c_str());
            }
            at += where.size();
            at += *at != '\0' ? 1 : 0;
        }
    }
    RingAdoptAll(status, true); // coop-rodring: before any bag changes hands (BagOf, filing a seat's bag, leaves ring items out)
    ProfileBagsAfterLoad();
    if (status == nullptr) {
        return;
    }
    if (RingOn()) {
        RingAdoptAll(status, false);
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
    OwnersClear();
    g_kept.clear();
    g_owners_for_new_game = false;
    std::ifstream in(save_directory / kBagsFile);

    if (!in) {
        return;
    }
    try {
        const nlohmann::json root = nlohmann::json::parse(in);
        const nlohmann::json owners = root.value("owners", nlohmann::json::object());

        // (DC_COOP_MUTATE=bags_two_seats, x-eventslots-bagsfile's twin: p1 and p2 only, the player 2 store as before)
        const int seats = Mutated("bags_two_seats") ? kBagSeats : kMaxPlayers;

        for (int p = 0; p < seats; p++) {
            g_bag_owner[p] = owners.value("p" + std::to_string(p + 1), std::string());
        }
        for (const auto &[name, bag] : root.value("kept", nlohmann::json::object()).items()) {
            g_kept[name] = JsonBag(bag);
        }
        FishProfilesRead(root, g_bag_owner); // coop-eventslots
    } catch (const std::exception &e) {
        std::fprintf(stderr, "coop-inv: %s not read (%s)\n", kBagsFile, e.what());
        return;
    }
    std::string said;

    for (int p = 0; p < kMaxPlayers; p++) {
        if (p < kBagSeats || !g_bag_owner[p].empty()) {
            said += (p == 0 ? "P1's bag " : ", P" + std::to_string(p + 1) + "'s ") + g_bag_owner[p] + "'s";
        }
    }
    std::fprintf(stderr, "coop-inv: profile bags loaded (%s, %zu kept)\n", said.c_str(), g_kept.size());
}

// save6: coop-p2.dat itself and its players' section (below CoopInventoryDelete).
namespace {
int  BagFileRead(const fs::path &save_directory);
bool BagFileWrite(const fs::path &save_directory);
} // namespace

} // namespace coop

using namespace coop;

bool CoopInventorySave(const std::filesystem::path &save_directory) {
    // The host keeps the bag; a joiner's copy (player 2 of a link) writes none of its own.
    if (!OwnBags() || NetplayLocalPlayer() == 2 || Mutated("invsave")) {
        return true;
    }
    CoopSlotKeepOldLayout(save_directory); // save6: an older release's files kept once before the first write

    if (!BagFileWrite(save_directory)) {
        return false;
    }

    std::printf("coop-inv: P2 bag saved beside the slot (%d carried, %d gilda)\n", Carried(g_bag.pack), g_bag.gilda);
    ProfileBagsWrite(save_directory);
    return true;
}

void CoopInventoryLoad(const std::filesystem::path &save_directory) {
    coop::AtlaRepairAfterLoad(SaveData != nullptr ? (CUserStatus *) SaveData->GetDngStatus() : nullptr); // coop-atlaparts (#265)
    InventoryNewGame();
    FishPoolsReset(); // coop-fishpools: the slot's file gives the records (none: the save's are player 1's, FishPoolsBegin)
    ProfileBagsRead(save_directory);

    const int layout = BagFileRead(save_directory);

    if (layout < 0) {
        std::printf("coop-inv: no P2 bag beside the slot; P2 starts with an empty bag\n");
        RingAfterLoad();
        return;
    }

    if (layout == 0) {
        std::fprintf(stderr, "coop-inv: %s is not a P2 bag; P2 starts with an empty bag\n", (save_directory / kFileName).string().c_str());
        RingAfterLoad();
        return;
    }

    std::printf("coop-inv: P2 bag loaded (%d carried, %d gilda)\n", Carried(g_bag.pack), g_bag.gilda);
    if (layout < (int) kLayout) {
        std::fprintf(stderr, "coop-inv: the slot's co-op files are an older release's (layout %d): read as they are, kept once as <file>%s at the next save\n",
                     layout, kSlotBackupSuffix);
    }
    for (int p = 2; p < kMaxPlayers; p++) {
        if (g_more[p].seeded) {
            std::fprintf(stderr, "coop-inv: P%d bag loaded (%d carried, %d gilda)\n", p + 1, Carried(g_more[p].pack), g_more[p].gilda);
        }
    }
    RingAfterLoad();
}

void CoopInventoryDelete(const std::filesystem::path &save_directory) {
    std::error_code error;
    fs::remove(save_directory / kFileName, error);
    fs::remove(save_directory / kBagsFile, error);
    fs::remove(save_directory / (std::string(kFileName) + kSlotBackupSuffix), error); // (save6: the backups with the slot)
    fs::remove(save_directory / (std::string(kBagsFile) + kSlotBackupSuffix), error);
}

namespace coop {
namespace {

void Put32(std::vector<unsigned char> &out, std::uint32_t v) {
    const auto *b = reinterpret_cast<const unsigned char *>(&v);

    out.insert(out.end(), b, b + sizeof v);
}

// save6: the players' section after the version-2 bytes (see kMoreMagic). DC_COOP_MUTATE=save6_more_unsaved (twin): no records.
void MoreBagsAppend(std::vector<unsigned char> &out) {
    const std::uint32_t record = std::max<std::uint32_t>(g_more_record, kRecordSize);
    const bool          none = Mutated("save6_more_unsaved");

    out.resize(std::max(out.size(), kPrefixSize), 0);
    Put32(out, kMoreMagic);
    Put32(out, kLayout);
    Put32(out, none ? 0 : (std::uint32_t) (kMaxPlayers - 2));
    Put32(out, record);
    for (int p = 2; p < kMaxPlayers && !none; p++) {
        const P2Bag &bag = g_more[p];
        const auto  *pack = reinterpret_cast<const unsigned char *>(&bag.pack);
        const auto  *vol = reinterpret_cast<const unsigned char *>(bag.active_item_vol);
        const auto   at = out.size();

        out.push_back((unsigned char) (p + 1));
        out.push_back(bag.seeded ? 1 : 0);
        out.insert(out.end(), pack, pack + sizeof bag.pack);
        out.insert(out.end(), vol, vol + sizeof bag.active_item_vol);
        out.push_back((unsigned char) (bag.gilda & 0xFF));
        out.push_back((unsigned char) (bag.gilda >> 8));
        out.push_back((unsigned char) bag.selected);
        out.insert(out.end(), g_more_tail[p].begin(), g_more_tail[p].end());
        out.resize(at + record, 0);
    }
}

// The section, when the file has one: the layout it was written in (2: an older release's file, players 3 up empty).
int MoreBagsRead(const unsigned char *data, std::size_t size) {
    std::uint32_t head[4] = {};

    if (size < kPrefixSize + sizeof head) {
        return 2;
    }
    std::memcpy(head, data + kPrefixSize, sizeof head);
    if (head[0] != kMoreMagic || head[1] < kLayout) {
        return 2;
    }
    if (head[3] < kRecordSize) {
        std::fprintf(stderr, "coop-inv: coop-p2.dat's players' records are %u bytes, fewer than layout 3's %zu: not read\n", head[3], kRecordSize);
        return (int) head[1];
    }
    std::size_t at = kPrefixSize + sizeof head;

    for (std::uint32_t i = 0; i < head[2] && at + head[3] <= size; i++, at += head[3]) {
        const int p = (int) data[at] - 1;

        if (p < 2 || p >= kMaxPlayers) {
            continue; // (a player past 6: none in this layout)
        }
        P2Bag bag;

        std::memcpy(&bag.pack, data + at + 2, sizeof bag.pack);
        std::memcpy(bag.active_item_vol, data + at + 2 + sizeof bag.pack, sizeof bag.active_item_vol);
        const unsigned char *rest = data + at + 2 + sizeof bag.pack + sizeof bag.active_item_vol;

        bag.gilda = (std::uint16_t) (rest[0] | (rest[1] << 8));
        bag.selected = std::clamp((int) rest[2], 1, 3);
        bag.seeded = data[at + 1] != 0 && bag.pack.num > 0 && bag.pack.num <= 100;
        g_more[p] = bag.seeded ? bag : P2Bag{};
        g_more_tail[p].assign(data + at + kRecordSize, data + at + head[3]);
    }
    g_more_record = head[3];
    return (int) head[1];
}

// coop-p2.dat read into the bags: -1 no file, 0 not a bag, else its layout (2 an older release's, 3 up this one's and later).
// DC_COOP_MUTATE=save6_drop_p2 (twin): an older release's file loses player 2's bag in the move.
int BagFileRead(const fs::path &save_directory) {
    std::vector<unsigned char> buffer(65536);
    const std::size_t          size = FilesRead(save_directory / kFileName, buffer.data(), buffer.size());

    if (size == 0) {
        return -1;
    }
    if (!InventoryRead(buffer.data(), size)) {
        return 0;
    }
    const int layout = MoreBagsRead(buffer.data(), size);

    if (layout < (int) kLayout && Mutated("save6_drop_p2")) {
        g_bag = P2Bag{};
    }
    if (std::any_of(g_more + 2, g_more + kMaxPlayers, [](const P2Bag &b) { return b.seeded; }) && !Mutated("bagsize_read")) {
        BagSizeNow();
    }
    return layout;
}

bool BagFileWrite(const fs::path &save_directory) {
    std::vector<unsigned char> bytes;

    InventoryWrite(bytes);

    // coop-worldkeys: DC_COOP_MUTATE=ringnosave (l-lobby -Case keysave's twin): the file without the ring, so a load loses it.
    if (RingOn() && Mutated("ringnosave") && bytes.size() >= sizeof(ITEM_PACK)) {
        bytes.resize(bytes.size() - sizeof(ITEM_PACK));
    }
    MoreBagsAppend(bytes); // save6

    const fs::path path = save_directory / kFileName;

    if (FilesWrite(path, bytes.data(), bytes.size(), true) != FilesResult::kWritten) {
        std::fprintf(stderr, "coop-inv: could not write %s\n", path.string().c_str());
        return false;
    }
    return true;
}

} // namespace

bool CoopSlotOldLayout(const std::filesystem::path &save_directory) {
    unsigned char     bag[kPrefixSize + 4] = {};
    unsigned char     party[12] = {};
    const std::size_t bag_size = FilesRead(save_directory / kFileName, bag, sizeof bag);
    const std::size_t party_size = FilesRead(save_directory / "coop-party.dat", party, sizeof party);
    std::uint32_t     magic = 0;

    std::memcpy(&magic, bag + kPrefixSize, sizeof magic);
    return (bag_size > 0 && (bag_size < sizeof bag || magic != kMoreMagic)) || (party_size > 0 && party_size < sizeof party);
}

// An older release's co-op files, kept once beside the slot before this release's first write into it. DC_COOP_MUTATE=save6_no_backup (twin): none kept.
void CoopSlotKeepOldLayout(const std::filesystem::path &save_directory) {
    if (!CoopSlotOldLayout(save_directory)) {
        return;
    }
    if (Mutated("save6_no_backup")) {
        std::fprintf(stderr, "coop-save: an older release's co-op files: no backup (DC_COOP_MUTATE=save6_no_backup)\n");
        return;
    }
    std::string kept;

    for (const char *name : {kFileName, kBagsFile, "coop-party.dat"}) {
        const fs::path  from = save_directory / name;
        const fs::path  to = save_directory / (std::string(name) + kSlotBackupSuffix);
        std::error_code error;

        if (!fs::exists(from, error) || fs::exists(to, error)) {
            continue;
        }
        fs::copy_file(from, to, error);
        kept += (kept.empty() ? "" : ", ") + std::string(name) + (error ? " (not copied: " + error.message() + ")" : "");
    }
    if (!kept.empty()) {
        std::fprintf(stderr, "coop-save: an older release's co-op files kept once beside the slot as <file>%s: %s\n", kSlotBackupSuffix, kept.c_str());
    }
}

} // namespace coop

void CoopInventoryNewGame() {
    InventoryNewGame();
    coop::FishPoolsReset(); // coop-fishpools
    // (the owners the lobby named for this new game stay; any other new game forgets the last slot's)
    if (!g_owners_for_new_game) {
        OwnersClear();
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
// coop-stealback (Jep's rc1 playtest): retail calls this only from player 1's play step while he is up (DunMoveChara:
// CheckLife), so her stolen item, its bag and its message, waited for his knock-down, chest or script to end. Her arrived
// slots (aux_state 2) are now hers to take, on her own step (coop::PartnerStealArrives); this takes them only once she has
// handed one back (aux_state 3: another player's message waits for a key), she is off the floor, or under DC_COOP_MUTATE=steal_waits (twin).
namespace dc::game { // repin: was PORT_OVERRIDE; a recipe removes the game's own definition
int CStealItem::checkEvent() {
    for (int i = 0; i < STEAL_ITEM_MAX; i++) {
        if (this->state[i] != StealItemState::Arrived) {
            continue;
        }

        // (her slot homes onto her only while she is on the floor, Step; off it, his tick takes it as before)
        if (this->aux_state[i] == 2 && coop::g_partner.visible && !coop::Mutated("steal_waits")) {
            continue;
        }

        this->state[i] = StealItemState::None;

        bool partners = this->aux_state[i] == 2 || this->aux_state[i] == 3;

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
    if (item == ITEM_FISHING_ROD) { // coop-rodring: a fishing board's script (and anything else) asking for the rod
        std::fprintf(stderr, "coop-ring: the fishing rod asked for at link tick %lld (player %d's copy): %s\n", (long long) NetplayLinkTick(), NetplayRole(),
                     *slot >= 0 ? "on the ring" : "not on the ring");
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

// ---- coop-fishpools (Jep 11-17): each player's own fishing points, always (coop-eventslots' records, Jep 11-11) ----
// Every seated player has their own fishing record (g_fish_records, by player index, 0 = player 1): the points, the log (the
// game's 64 biggest catches) and Mardan Garayan's count. A profile not playing keeps theirs by name (g_fish_kept), and every
// one is written to the slot's coop-bags.json beside the bags. The save's own record (SaveData's fishing_point, fish_data,
// mardan_garayan_caught: what the game's fishing, its exchange and its log read and write) is only a VIEW of them (FishPut):
//   - game.coop_fishing_shared on (the party's): the TOTAL of every player in the game (points and Mardan's count summed, the
//     points shown at most 9999 as the game's counter; the log the 64 biggest of all their catches);
//   - off (each player's, FishingPerPlayer): the own record of the player whose activity runs (their fishing, their own event's
//     menus, their menu's writes: FishRecordSwap in and out around it), else player 1's.
// What the game changed in the view since it was put there is the activity's player's (FishTake): their own record takes it
// whole when the view is theirs; in the party's view a gain (a catch) goes to their own pool and its log entry to their own
// log, and a spend (the exchange) comes off every player in the game equally (FishSpend). The view is made again at every
// turn and every tick (FishPoolsStep), so switching the setting either way only changes what is shown: no point moves.
// Cause of the bug this replaces (coop-eventslots' one party pool, the save's): switching to the party's folded every
// player's record into player 1's (the save's) and emptied theirs, so switching back left every point with player 1.
// Every copy alike: the records and the view are in the state set (snapshot-allow.txt *g_fish_records, *g_fish_pools) and in
// the checksum (FishPoolsHash); the players in the game are the lockstep's (FishPresent); the join gives the joiner's profile
// record with their bag (ProfileBagsJoinBag/Take). Out of co-op (no own bags) there are no profiles: the
// save's record alone is the party's, as retail's.
namespace coop {
void ProfileBagsGive(CUserStatus &status_ref, const std::vector<std::string> &players); // (coop-fishing3's test hook seats profiles by it)
namespace {
constexpr int kFishNameBytes = 24; // (a profile name, zero-padded; longer ones are cut)
constexpr int kFishKeptNames = 16; // (the kept board's names: more profiles than that keep their biggest catches unnamed: never shown)

struct FishRecord {
    std::int32_t points = 0;
    SV_FISH_DATA log[64] = {};
    std::int32_t mardan = 0;
};

FishRecord                        g_fish_records[kMaxPlayers]; // every seated player's own, by player index (0: player 1)
std::map<std::string, FishRecord> g_fish_kept;                 // the records of profiles not playing, by name (coop-bags.json)
bool                              g_fish_saved_per_player = false; // the setting the slot's coop-bags.json was written under

struct FishPools {
    FishRecord   view;            // what FishPut last put in the save: a change to the save since is the game's (FishTake)
    std::int32_t on = 0;          // the records are the players' (a load, the lobby's give, or the first step with own bags)
    std::int32_t per_player = 0;  // the setting the view was made under
    std::int32_t present = 1;     // the players in the game it was made with (bit k: player index k)
    std::int32_t owner = 0;       // whose own record the view is (each player's), or whose activity it is (the party's)
    std::int32_t inside[kMaxPlayers] = {}; // FishRecordSwap: player k's activity under way (1), and the owner before it
    std::int32_t outer[kMaxPlayers] = {};
    // coop-fishpools2: apart, each copy's own changes to every pool (points, Mardan's count), summed since the game began, by the
    // player whose copy made them: apart[w][k]. Each is written on one copy alone (w's), so the apart sync carries them with no
    // clash (FishSlotRead/Write, coop_worlds.cpp kFishGain): the other copy adds what it had not had yet.
    std::int32_t apart[kMaxPlayers][kMaxPlayers][2] = {};
    // coop-fishing3 (Jep 11-17: the fishing log names each catch's player by profile, profiles not in the game too): each seat's
    // profile name; the kept board, the 64 biggest catches of the slot's profiles not seated (g_fish_kept is the host's alone, so
    // the board is what every copy shows of them), each with its name; and whose each entry of the view's log is (FishLogWho).
    // Every copy alike: set from the host's file at its load, carried to a joiner in the state set, changed at a join by the
    // joiner's name on every copy (FishJoinTake). Names are zero-padded, so the bytes (the checksum's) are the same everywhere.
    char         seat_names[kMaxPlayers][kFishNameBytes] = {};
    char         kept_names[kFishKeptNames][kFishNameBytes] = {};
    SV_FISH_DATA kept_log[64] = {};
    std::int8_t  kept_who[64] = {};  // (a kept name's number, -1 none)
    std::int8_t  view_who[64] = {};  // the view's log entry i: its seat (0-5), a kept name (kMaxPlayers + n), -1 none
};
FishPools g_fish_pools;

int g_exchange_open_point = 0; // this copy's own exchange's points at its open (FishExchangeOpens; only its own menu reads it)
std::int64_t g_test_seed_tick = -1; // (tests: the link tick DC_COOP_TEST_FISH_POOLS set the records on, the hooks below count from it)

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

bool LogEntry(const SV_FISH_DATA &e) {
    return e.rank > 0.0f && e.fish_id >= 0;
}

bool SameEntry(const SV_FISH_DATA &a, const SV_FISH_DATA &b) {
    return std::memcmp(&a, &b, sizeof a) == 0;
}

// A catch into a log, as the game's CSaveData::SetFishingRank does it (the first empty entry, else over the smallest; then
// biggest first).
void LogInsert(SV_FISH_DATA *log, const SV_FISH_DATA &catch_entry) {
    float lowest = -1.0f;
    int   lowest_at = -1;
    int   i = 0;

    for (; i < 64; i++) {
        if (log[i].rank <= 0.0f) {
            log[i] = catch_entry;
            break;
        }
        if (lowest < 0.0f || lowest > log[i].rank) {
            lowest_at = i;
            lowest = log[i].rank;
        }
    }
    if (i == 64 && lowest_at >= 0) {
        log[lowest_at] = catch_entry;
    }
    for (int j = 0; j < 63; j++) {
        for (int k = j + 1; k < 64; k++) {
            if (log[j].rank < log[k].rank) {
                std::swap(log[j], log[k]);
            }
        }
    }
}

// The party's log: the 64 biggest catches of every seat's record (coop-fishing3: in the game or not: a player who left keeps
// theirs in their seat) and of the kept board (the slot's other profiles), biggest first (a tie: the lower player number, then
// that player's own order, then the board's); who[i]: whose entry i is (a seat, kMaxPlayers + a kept name, -1 none).
// DC_COOP_MUTATE=fishlog_present_only (twin): the players in `present` alone, as before.
void LogOfParty(const FishRecord *records, int present, SV_FISH_DATA *out, std::int8_t *who, const FishPools *board) {
    std::vector<std::pair<SV_FISH_DATA, int>> all;
    const bool                                everyone = board != nullptr && !Mutated("fishlog_present_only");

    for (int k = 0; k < kMaxPlayers; k++) {
        for (int i = 0; (everyone || (present >> k & 1) != 0) && i < 64; i++) {
            if (LogEntry(records[k].log[i])) {
                all.emplace_back(records[k].log[i], k);
            }
        }
    }
    for (int i = 0; everyone && i < 64; i++) {
        if (LogEntry(board->kept_log[i]) && board->kept_who[i] >= 0) {
            all.emplace_back(board->kept_log[i], kMaxPlayers + board->kept_who[i]);
        }
    }
    std::stable_sort(all.begin(), all.end(), [](const auto &a, const auto &b) { return a.first.rank > b.first.rank; });
    for (int i = 0; i < 64; i++) {
        out[i] = i < (int) all.size() ? all[(std::size_t) i].first : SV_FISH_DATA{};
        if (who != nullptr) {
            who[i] = (std::int8_t) (i < (int) all.size() ? all[(std::size_t) i].second : -1);
        }
    }
}

// The view of `records` for the players in `present`: the party's total, or `owner`'s own. who (may be null): whose each log
// entry is; board (may be null): the names and the kept board (FishPools), whose catches the party's log shows too.
FishRecord ViewOf(const FishRecord *records, int present, bool per_player, int owner, std::int8_t *who = nullptr, const FishPools *board = nullptr) {
    if (per_player) {
        const int k = owner >= 0 && owner < kMaxPlayers ? owner : 0;

        for (int i = 0; who != nullptr && i < 64; i++) {
            who[i] = (std::int8_t) (LogEntry(records[k].log[i]) ? k : -1);
        }
        return records[k];
    }
    FishRecord   view;
    std::int64_t points = 0, mardan = 0;

    for (int k = 0; k < kMaxPlayers; k++) {
        if ((present >> k & 1) != 0) {
            points += records[k].points;
            mardan += records[k].mardan;
        }
    }
    view.points = (std::int32_t) std::min<std::int64_t>(points, 9999);
    view.mardan = (std::int32_t) std::min<std::int64_t>(mardan, 0x7fffffff);
    LogOfParty(records, present, view.log, who, board);
    return view;
}

// A spend off the players in `present`, equally: each pays an equal share; a player whose pool runs out pays what they have and
// the rest is shared out the same way among the others, until it is paid. A share that does not divide: one point more each
// from the largest pools left (a tie: the lower player number). Returns what could not be paid (nobody has any left).
// `field` picks the points or Mardan's count.
int SpendOf(FishRecord *records, int present, int amount, std::int32_t FishRecord::*field) {
    while (amount > 0) {
        int seats[kMaxPlayers], n = 0;

        for (int k = 0; k < kMaxPlayers; k++) {
            if ((present >> k & 1) != 0 && records[k].*field > 0) {
                seats[n++] = k;
            }
        }
        if (n == 0) {
            break;
        }
        const int share = amount / n;
        int       more = amount % n;
        int       take[kMaxPlayers] = {};

        for (int i = 0; i < n; i++) {
            take[seats[i]] = std::min(records[seats[i]].*field, share);
        }
        // (the remainder: by what is left after the share, largest first, then the lower player number)
        int order[kMaxPlayers];

        std::copy(seats, seats + n, order);
        std::stable_sort(order, order + n, [&](int a, int b) { return records[a].*field - take[a] > records[b].*field - take[b]; });
        for (int i = 0; i < n && more > 0; i++) {
            if (records[order[i]].*field - take[order[i]] > 0) {
                take[order[i]]++;
                more--;
            }
        }
        for (int i = 0; i < n; i++) {
            records[seats[i]].*field -= take[seats[i]];
            amount -= take[seats[i]];
        }
    }
    return amount;
}

// coop-fishing3: a name into a pools field, zero-padded (cut to its room).
void NameInto(char *field, const std::string &name) {
    std::memset(field, 0, kFishNameBytes);
    std::memcpy(field, name.data(), std::min<std::size_t>(name.size(), kFishNameBytes - 1));
}

// The kept board's number for a name: its own, or a free one taken (-1: the board's names are full).
int KeptName(FishPools &p, const std::string &name) {
    for (int n = 0; n < kFishKeptNames; n++) {
        if (p.kept_names[n][0] != 0 && name.compare(0, kFishNameBytes - 1, p.kept_names[n]) == 0) {
            return n;
        }
    }
    for (int n = 0; n < kFishKeptNames; n++) {
        if (p.kept_names[n][0] == 0) {
            NameInto(p.kept_names[n], name);
            return n;
        }
    }
    return -1;
}

// The board's entries plus `more` (each with its kept name's number), the 64 biggest kept, biggest first (a tie: the board's
// own order first); names no entry uses any more are let go.
void KeptBoardPut(FishPools &p, const std::vector<std::pair<SV_FISH_DATA, int>> &more) {
    std::vector<std::pair<SV_FISH_DATA, int>> all;

    for (int i = 0; i < 64; i++) {
        if (LogEntry(p.kept_log[i]) && p.kept_who[i] >= 0) {
            all.emplace_back(p.kept_log[i], p.kept_who[i]);
        }
    }
    all.insert(all.end(), more.begin(), more.end());
    std::stable_sort(all.begin(), all.end(), [](const auto &a, const auto &b) { return a.first.rank > b.first.rank; });
    bool used[kFishKeptNames] = {};

    for (int i = 0; i < 64; i++) {
        const bool in = i < (int) all.size();

        p.kept_log[i] = in ? all[(std::size_t) i].first : SV_FISH_DATA{};
        p.kept_who[i] = (std::int8_t) (in ? all[(std::size_t) i].second : -1);
        if (in) {
            used[all[(std::size_t) i].second] = true;
        }
    }
    for (int n = 0; n < kFishKeptNames; n++) {
        if (!used[n]) {
            std::memset(p.kept_names[n], 0, kFishNameBytes);
        }
    }
}

// The kept board made again from the host's kept records (a load, the lobby's seating): every one of their catches, by name.
void KeptBoardBuild() {
    FishPools &p = g_fish_pools;
    std::vector<std::pair<SV_FISH_DATA, int>> all;

    std::memset(p.kept_names, 0, sizeof p.kept_names);
    std::fill(std::begin(p.kept_log), std::end(p.kept_log), SV_FISH_DATA{});
    std::fill(std::begin(p.kept_who), std::end(p.kept_who), (std::int8_t) -1);
    // (biggest first over every profile, so the board's 16 names are those of the biggest catches)
    std::vector<std::pair<SV_FISH_DATA, std::string>> catches;

    for (const auto &[name, r] : g_fish_kept) {
        for (const SV_FISH_DATA &e : r.log) {
            if (LogEntry(e)) {
                catches.emplace_back(e, name);
            }
        }
    }
    std::stable_sort(catches.begin(), catches.end(), [](const auto &a, const auto &b) { return a.first.rank > b.first.rank; });
    for (const auto &[e, name] : catches) {
        if (all.size() >= 64) {
            break;
        }
        if (const int n = KeptName(p, name); n >= 0) {
            all.emplace_back(e, n);
        }
    }
    KeptBoardPut(p, all);
}

// A seat's profile changes on every copy (a join): the joiner's catches leave the board (their record is their seat's now),
// and the seat's old profile's record goes on it, under its name.
void KeptBoardSeat(int seat, const std::string &name) {
    FishPools        &p = g_fish_pools;
    const std::string was(p.seat_names[seat], strnlen(p.seat_names[seat], kFishNameBytes));
    std::string       cut = name.substr(0, kFishNameBytes - 1);

    for (int i = 0; i < 64; i++) {
        if (p.kept_who[i] >= 0 && cut == p.kept_names[p.kept_who[i]]) {
            p.kept_log[i] = SV_FISH_DATA{};
            p.kept_who[i] = -1;
        }
    }
    std::vector<std::pair<SV_FISH_DATA, int>> more;

    if (!was.empty() && was != cut) {
        const int n = KeptName(p, was);

        for (int i = 0; n >= 0 && i < 64; i++) {
            if (LogEntry(g_fish_records[seat].log[i])) {
                more.emplace_back(g_fish_records[seat].log[i], n);
            }
        }
    }
    KeptBoardPut(p, more);
    NameInto(p.seat_names[seat], name);
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

std::string PoolsSaid(const FishRecord *records, int present) {
    std::string said;

    for (int k = 0; k < kMaxPlayers; k++) {
        if ((present >> k & 1) != 0) {
            said += (said.empty() ? "P" : ", P") + std::to_string(k + 1) + " " + std::to_string(records[k].points);
        }
    }
    return said;
}

// The players in the game now, on every copy alike: player 1 always; the linked player from the lockstep's join tick
// (NetplayPartnerLinked: the same tick on every copy) until they leave or drop. (Netplay links two players; a third's seat
// joins this mask when the link carries one.)
int FishPresent() {
    // (coop-fishpools2: apart the link is down, but both players are still in the game, each in their own world)
    return NetplayPartnerLinked() || (CoopWorldsApart() && !Mutated("fishpools_apart_off")) ? 0b11 : 0b01;
}

bool PoolsOn() {
    return OwnBags() && SaveData != nullptr && !Mutated("fishpools_off");
}

std::int64_t TotalPoints() {
    std::int64_t total = 0;

    for (const FishRecord &r : g_fish_records) {
        total += r.points;
    }
    for (const auto &[name, r] : g_fish_kept) {
        total += r.points;
    }
    return total;
}

// coop-fishpools2: apart, each copy plays its own world: what its game changes is its own player's (no activity of another player
// runs there), and its view (each player's) is its own player's. DC_COOP_MUTATE=fishpools_apart_off (twin): as together (player 1's).
bool ApartHere() {
    return CoopWorldsApart() && !Mutated("fishpools_apart_off");
}

int DefaultOwner() {
    return ApartHere() ? std::clamp(NetplayRole() - 1, 0, kMaxPlayers - 1) : 0; // (NetplayRole: this copy's player, linked or not)
}

void FishTakeView();

// What the game changed in the view since FishPut: the view's player's (g_fish_pools.owner); apart, also counted as this copy's.
void FishTake() {
    std::int32_t before[kMaxPlayers][2];

    for (int k = 0; k < kMaxPlayers; k++) {
        before[k][0] = g_fish_records[k].points;
        before[k][1] = g_fish_records[k].mardan;
    }
    FishTakeView();
    if (ApartHere()) {
        const int w = DefaultOwner();

        for (int k = 0; k < kMaxPlayers; k++) {
            g_fish_pools.apart[w][k][0] += g_fish_records[k].points - before[k][0];
            g_fish_pools.apart[w][k][1] += g_fish_records[k].mardan - before[k][1];
        }
    }
}

// coop-signfix: what was taken is the view now, so a second take before the next FishPut takes nothing again. Cause of the bug
// (tests/test_sign.py's exchange: the save's 500 points became player 1's 1000): FishPoolsStep takes twice a step (before its
// test hooks and after them) and the view stayed the one FishPut had made, so a change of the save outside an activity's swap
// (a script's or a hook's) was taken twice in that step: a gain added twice, a spend paid twice, a catch logged twice.
// DC_COOP_MUTATE=fishtake_twice (twin): the view left as FishPut made it, as before.
void FishTaken(const FishRecord &now) {
    if (!Mutated("fishtake_twice")) {
        g_fish_pools.view = now;
    }
}

void FishTakeView() {
    FishPools       &p = g_fish_pools;
    const FishRecord now = SaveRecord();

    if (std::memcmp(&now, &p.view, sizeof now) == 0) {
        return;
    }
    const int who = p.owner >= 0 && p.owner < kMaxPlayers ? p.owner : 0;
    FishRecord &own = g_fish_records[who];

    if (p.per_player != 0) {
        if (own.points != now.points) {
            std::fprintf(stderr, "coop-fishpools: player %d's own fishing points %d -> %d at link tick %lld (each player's own)\n", who + 1, own.points, now.points,
                         (long long) NetplayLinkTick());
        }
        own = now; // (their own record was the view: what the game made of it is theirs)
        FishTaken(now);
        return;
    }
    const int gain = now.points - p.view.points;
    const int mardan = now.mardan - p.view.mardan;

    if (gain > 0) {
        own.points = std::min(own.points + gain, 9999);
        std::fprintf(stderr, "coop-fishpools: player %d gains %d fishing points into their own (%d) at link tick %lld: %s\n", who + 1, gain, own.points,
                     (long long) NetplayLinkTick(), PoolsSaid(g_fish_records, p.present).c_str());
    } else if (gain < 0) {
        const std::string was = PoolsSaid(g_fish_records, p.present);
        const int         left = SpendOf(g_fish_records, p.present, -gain, &FishRecord::points);

        std::fprintf(stderr, "coop-fishpools: player %d spends %d fishing points, every player alike, at link tick %lld: %s -> %s%s\n", who + 1, -gain,
                     (long long) NetplayLinkTick(), was.c_str(), PoolsSaid(g_fish_records, p.present).c_str(),
                     left > 0 ? (" (" + std::to_string(left) + " not paid: nobody has any left)").c_str() : "");
    }
    if (mardan > 0) {
        own.mardan += mardan;
    } else if (mardan < 0) {
        SpendOf(g_fish_records, p.present, -mardan, &FishRecord::mardan);
    }
    // The log: what is in the view now and was not before (a catch: SetFishingRank's entry) goes into their own log.
    bool used[64] = {};

    for (const SV_FISH_DATA &e : now.log) {
        if (!LogEntry(e)) {
            continue;
        }
        bool was = false;

        for (int i = 0; i < 64 && !was; i++) {
            if (!used[i] && SameEntry(p.view.log[i], e)) {
                used[i] = was = true;
            }
        }
        if (!was) {
            LogInsert(own.log, e);
            std::fprintf(stderr, "coop-fishpools: player %d's catch (fish %d, %.0f) into their own log at link tick %lld\n", who + 1, (int) e.fish_id,
                         (double) e.rank, (long long) NetplayLinkTick());
        }
    }
    FishTaken(now);
}

// The view for the players in the game now, under the setting now, into the save.
void FishPut(int owner) {
    FishPools &p = g_fish_pools;
    const int  present = FishPresent();
    const bool per_player = FishingPerPlayer();

    const bool said = per_player != (p.per_player != 0) || present != p.present;

    p.per_player = per_player ? 1 : 0;
    p.present = present;
    p.owner = owner;
    p.view = ViewOf(g_fish_records, present, per_player, owner, p.view_who, &p);
    if (said) { // (shown: what player 1's view shows outside an activity: the total, or his own)
        std::fprintf(stderr, "coop-fishpools: shown now %s, %d points, at link tick %lld (%s)\n", per_player ? "each player's own" : "the party's total",
                     (int) p.view.points, (long long) NetplayLinkTick(), PoolsSaid(g_fish_records, present).c_str());
    }
    SavePut(p.view);
}

// Test hook (DC_COOP_TEST_FISH_POOLS_MATH=1, every copy at its first pool step): the pools' rules on made-up players, three and
// more (netplay links two; the rules are written for any number): each case's records before and after, said.
void FishPoolsMathSelfTest() {
    COOP_TEST_SWITCH(bool, on, TestEnvOn("DC_COOP_TEST_FISH_POOLS_MATH"));
    static int done = -1;

    if (!on || done == TestJobGeneration()) {
        return;
    }
    done = TestJobGeneration();
    struct Case {
        const char *name;
        int         points[4];
        int         present;
        int         spend;
    };
    const Case cases[] = {
        {"three alike", {300, 300, 300, 0}, 0b0111, 90},
        {"three, a remainder of 2", {100, 100, 100, 0}, 0b0111, 8},
        {"three, one runs out", {10, 200, 300, 0}, 0b0111, 120},
        {"three, two run out", {5, 7, 500, 0}, 0b0111, 100},
        {"three, a tie for the remainder", {50, 80, 80, 0}, 0b0111, 4},
        {"four, one not in the game", {100, 100, 100, 999}, 0b0111, 30},
        {"three, more than all", {10, 20, 30, 0}, 0b0111, 100},
    };
    for (const Case &c : cases) {
        FishRecord r[kMaxPlayers];

        for (int k = 0; k < 4; k++) {
            r[k].points = c.points[k];
        }
        const FishRecord view = ViewOf(r, c.present, false, 0);
        const int        left = SpendOf(r, c.present, c.spend, &FishRecord::points);

        std::fprintf(stderr, "coop-fishpools: test: math: %s: shown %d, spend %d: %d %d %d %d, %d not paid\n", c.name, view.points, c.spend, r[0].points,
                     r[1].points, r[2].points, r[3].points, left);
    }
    // The party's log: three players' catches, the 64 biggest of them, biggest first.
    FishRecord r[kMaxPlayers];

    for (int k = 0; k < 3; k++) {
        for (int i = 0; i < 30; i++) {
            LogInsert(r[k].log, SV_FISH_DATA{k * 10 + i % 10, (float) (1 + i * 3 + k), 0.0f, 0.0f});
        }
    }
    const FishRecord view = ViewOf(r, 0b0111, false, 0);
    int              n = 0, ordered = 1;

    for (int i = 0; i < 64; i++) {
        n += LogEntry(view.log[i]) ? 1 : 0;
        ordered &= i == 0 || view.log[i - 1].rank >= view.log[i].rank ? 1 : 0;
    }
    std::fprintf(stderr, "coop-fishpools: test: math: the party's log of three: %d entries, biggest %.0f, smallest %.0f, %s\n", n, (double) view.log[0].rank,
                 (double) view.log[63].rank, ordered ? "biggest first" : "OUT OF ORDER");
}
// Test hooks, counted in link ticks from the seed's (DC_COOP_TEST_FISH_POOLS):
//   DC_COOP_TEST_FISH_SHARED_FLIP=N1/N2/...  player 1's copy turns game.coop_fishing_shared over at each, as his settings would
//                                            (a shared key: kCmdSetting runs it on every copy from one tick)
//   DC_COOP_TEST_FISH_GAIN=N:P:POINTS        every copy, at N: player P gains POINTS as a catch does (their activity's view,
//                                            SaveData->AddFishingPoint, as ebattle's catch inside their fishing step)
//   DC_COOP_TEST_FISH_LOGCATCH=N:P:FISH:SIZE    coop-fishing3, every copy, at N: player P's catch of fish FISH, SIZE long, into the log
//                                            as the game's catch puts it (their activity's view, SaveData->SetFishingRank)
//   DC_COOP_TEST_FISH_SEATS=N:A:B            coop-fishing3, every copy, at N: profiles A and B take the seats (ProfileBagsGive: the
//                                            lobby's and a load's path), the profile a seat held before kept
void FishPoolsTestHooks() {
    COOP_TEST_SWITCH(const char *, flips, TestEnv("DC_COOP_TEST_FISH_SHARED_FLIP"));
    COOP_TEST_SWITCH(const char *, gain, TestEnv("DC_COOP_TEST_FISH_GAIN"));
    COOP_TEST_SWITCH(const char *, catches, TestEnv("DC_COOP_TEST_FISH_LOGCATCH"));
    COOP_TEST_SWITCH(const char *, seats, TestEnv("DC_COOP_TEST_FISH_SEATS"));

    if (g_test_seed_tick < 0) {
        return;
    }
    const std::int64_t since = ++g_test_seed_tick; // (this copy's own steps since the seed: the link tick stops apart)

    for (const char *f = flips; f != nullptr && *f != 0 && NetplayLocalPlayer() <= 1;
         f = std::strchr(f, '/') != nullptr ? std::strchr(f, '/') + 1 : f + std::strlen(f)) {
        if (std::atoi(f) == since) {
            dc::platform::Config config = dc::platform::ConfigGet();

            config.coop_fishing_shared = !config.coop_fishing_shared;
            dc::platform::ConfigChange(config);
            std::fprintf(stderr, "coop-fishpools: test: player 1 turns shared fishing points %s at link tick %lld\n", config.coop_fishing_shared ? "on" : "off",
                         (long long) NetplayLinkTick());
        }
    }
    int at = -1, who = 0, points = 0;

    for (const char *g = gain; g != nullptr && *g != 0; g = std::strchr(g, '/') != nullptr ? std::strchr(g, '/') + 1 : g + std::strlen(g)) { // (several: N:P:POINTS/...; POINTS < 0 a buy's spend)
    if (std::sscanf(g, "%d:%d:%d", &at, &who, &points) == 3 && at == since && who >= 1 && who <= kMaxPlayers && SaveData != nullptr) {
        FishRecordSwap(who - 1);
        SaveData->AddFishingPoint(points);
        FishRecordSwap(who - 1);
        std::fprintf(stderr, "coop-fishpools: test: player %d's catch of %d points at link tick %lld\n", who, points, (long long) NetplayLinkTick());
    }
    }
    const auto items = [](const char *list, auto &&each) {
        for (const char *at = list; at != nullptr && *at != 0; at = std::strchr(at, '/') != nullptr ? std::strchr(at, '/') + 1 : at + std::strlen(at)) {
            each(at);
        }
    };
    items(catches, [&](const char *at) {
        int n = -1, player = 0, fish = 0, size = 0;

        if (std::sscanf(at, "%d:%d:%d:%d", &n, &player, &fish, &size) == 4 && n == since && player >= 1 && player <= kMaxPlayers && SaveData != nullptr) {
            FishRecordSwap(player - 1);
            SaveData->SetFishingRank(fish, (float) size);
            FishRecordSwap(player - 1);
            std::fprintf(stderr, "coop-fishpools: test: player %d's catch of fish %d, %d long, at link tick %lld\n", player, fish, size, (long long) NetplayLinkTick());
        }
    });
    items(seats, [&](const char *at) {
        int  n = -1;
        char a[16] = {}, b[16] = {};

        if (std::sscanf(at, "%d:%15[^:]:%15[^/]", &n, a, b) == 3 && n == since && SaveData != nullptr) {
            ProfileBagsGive(*(CUserStatus *) SaveData->GetDngStatus(), {a, b});
            std::fprintf(stderr, "coop-fishpools: test: profiles %s and %s take the seats at link tick %lld\n", a, b, (long long) NetplayLinkTick());
        }
    });
}

// coop-fishpools2: a buy at the fishing exchange while linked (shared or each player's): the menu claims it, and every copy grants
// or refuses it on the claim's tick, in the lockstep's order, from the points as they stand then (FishBuyApply): a second buy
// made from the same shown points sees the first's spend. The claim goes out from the town's step, after the menu's step has
// put the live state back (a command a catch-up's sender applies at once must not land inside the menu's own view).
struct FishBuy {
    int  item = -1;
    int  price = 0;
    bool due = false;     // made by this copy's menu, not sent yet
    bool waiting = false; // sent, not landed
    int  landed = 0;      // 1 granted, -1 refused: for this copy's menu to read once
    int  landed_item = -1;
};
FishBuy g_buy; // (this copy's own menu's: never in the state set)

void FishBuyQueue() {
    // (test hook DC_COOP_TEST_FISH_BUY_HOLD=N: the claim held N steps on its way, as a slow link would, so two are out at once)
    COOP_TEST_SWITCH(int, hold, TestEnvInt("DC_COOP_TEST_FISH_BUY_HOLD", 0));
    static int held = 0;

    if (!g_buy.due || (hold > 0 && ++held <= hold)) {
        return;
    }
    held = 0;
    g_buy.due = false;
    g_buy.waiting = true;
    NetCommand command;

    command.kind = kCmdFishBuy;
    command.b = (std::uint16_t) g_buy.item;
    command.c = (std::uint32_t) g_buy.price;
    NetplayQueueCommand(command);
}

// Test hook (DC_COOP_TEST_FISH_TRACE_POOLS=1): each player's own points on each change, this copy's (apart or together).
// coop-fishing3 test hook (DC_COOP_TEST_FISH_BAIT=ITEM): each change of how many ITEM (a bait) each player's own bag holds, from
// the town's step (outside every player's event: the game's pack is player 1's, the other bag player 2's).
void FishBaitTrace() {
    COOP_TEST_SWITCH(int, item, TestEnvInt("DC_COOP_TEST_FISH_BAIT", -1));
    static int said[2] = {-1, -1};

    if (item < 0 || SaveData == nullptr) {
        return;
    }
    int now[2] = {};
    const ITEM_PACK *packs[2] = {&PartyStatus()->item_pack, &PartnerPack()};

    for (int k = 0; k < 2; k++) {
        for (int i = 0; i < packs[k]->num && i < 103; i++) {
            now[k] += packs[k]->item[i] == item ? 1 : 0;
        }
    }
    if (now[0] != said[0] || now[1] != said[1]) {
        said[0] = now[0];
        said[1] = now[1];
        std::fprintf(stderr, "coop-fishpools: test: bait %d in the bags: P1 %d, P2 %d at link tick %lld\n", item, now[0], now[1], (long long) NetplayLinkTick());
    }
}

void FishPoolsTrace() {
    COOP_TEST_SWITCH(bool, on, TestEnvOn("DC_COOP_TEST_FISH_TRACE_POOLS"));
    static std::int32_t said[kMaxPlayers] = {-1, -1, -1, -1, -1, -1};

    if (!on) {
        return;
    }
    bool changed = false;

    for (int k = 0; k < kMaxPlayers; k++) {
        changed = changed || said[k] != g_fish_records[k].points;
        said[k] = g_fish_records[k].points;
    }
    if (changed) {
        std::fprintf(stderr, "coop-fishpools: test: pools P1 %d, P2 %d at link tick %lld (%s)\n", (int) said[0], (int) said[1], (long long) NetplayLinkTick(),
                     CoopWorldsApart() ? "apart" : "together");
    }
}

// Test hook (DC_COOP_TEST_FISH_POOLS=P1/P2/...): each player's own points, set once the state is loaded (every copy alike).
void FishPoolsSeed() {
    COOP_TEST_SWITCH(const char *, spec, TestEnv("DC_COOP_TEST_FISH_POOLS"));
    static int done = -1;

    if (spec == nullptr || done == TestJobGeneration() || (TestEnv("DC_STATE_LOAD_AT") != nullptr && !NetplayStateFileLoaded())) {
        return;
    }
    done = TestJobGeneration();
    g_test_seed_tick = 0;
    const char *at = spec;

    for (int k = 0; k < kMaxPlayers && *at != 0; k++) {
        g_fish_records[k].points = std::atoi(at);
        at = std::strchr(at, '/') != nullptr ? std::strchr(at, '/') + 1 : at + std::strlen(at);
    }
    FishPut(g_fish_pools.owner);
    std::fprintf(stderr, "coop-fishpools: test: each player's own points set at link tick %lld: %s\n", (long long) NetplayLinkTick(),
                 PoolsSaid(g_fish_records, 0b111111).c_str());
}
} // namespace

bool FishPoolsOn() {
    return PoolsOn();
}

// coop-fishing3: the fishing log board's columns, Jep's layout (11-18, after the mockups): RANK | NAME | FISH | SIZE of FISH. The NAME
// column is each catch's player by profile name, between the rank and the fish (the fish names move right to make room); its
// header is the board's own NAME button, the fish column's the board's own art too (FishLogBoardHeaders, shop.cpp). Only the
// game's own UI: its board, its header art, its menu font; nothing of ours over other game UI. The fish names are the game's own
// messages (the ones retail's window draws), drawn in the menu font as the names are, each fitted to its column (shrunk only
// when wider). A name is cut at kFishNameShown letters, then fitted. Test lines (DC_COOP_TEST_FISHLOG=1): each change of what the
// rows show, and the rectangles drawn, the board's, in the game's 640 x 448 screen.
bool FishLogColumnsOn() {
    return PoolsOn() && g_fish_pools.on != 0 && !Mutated("fishlog_names_off");
}

void FishLogNamesDraw(int board_x, int board_y, int alpha, int top, int cursor) {
    using namespace fishlog;
    constexpr int             kFishNameShown = 10; // (the one cut length)
    static dc::text::GameText names[5], fish[5];
    static std::string        said;
    static const bool         test = TestEnvOn("DC_COOP_TEST_FISHLOG");
    std::string               now, rects;

    if (!FishLogColumnsOn()) {
        return;
    }
    for (int i = 0; i < 5; i++) {
        const int    row_y = board_y + kRowTop + i * kRowStep;
        std::string  name = FishLogWho(top + i);
        const short *message = AtoraNameMes.mes_no[i] > 0 && AtoraNameMes.buff_system != nullptr ? AtoraNameMes.GetTextLineDataTop_system(AtoraNameMes.mes_no[i]) : nullptr;
        std::string  fish_name = message != nullptr ? dc::text::GameTextDecode(message) : std::string();
        const SV_FISH_DATA *record = SaveData != nullptr ? SaveData->GetFishingRank(top + i) : nullptr;

        while (!fish_name.empty() && (fish_name.back() == '\n' || fish_name.back() == ' ')) {
            fish_name.pop_back();
        }
        if (record == nullptr) {
            name.clear(), fish_name.clear();
        }
        if ((int) name.size() > kFishNameShown) {
            name.resize(kFishNameShown);
        }
        now += (i > 0 ? " | " : "") + name;
        if (!name.empty()) {
            names[i].KeepGameRandom(true); // (each copy's own menu: never the game's random numbers)
            names[i].Set(name);
            names[i].FitWidth(kNameW);
            names[i].Draw(board_x + kNameX, row_y, alpha);
            rects += " name " + std::to_string(board_x + kNameX) + ".." + std::to_string(board_x + kNameX + names[i].Width());
        }
        if (!fish_name.empty()) {
            // (up to the row's size number, right-aligned before the cm at kSizeX: its digits' width)
            const int digits = record->rank >= 100.0f ? 3 : record->rank >= 10.0f ? 2 : 1;
            const int room = kSizeX - digits * kDigitW - kGap - kFishX;

            fish[i].KeepGameRandom(true);
            fish[i].Set(fish_name);
            fish[i].FitWidth(room);
            fish[i].Draw(board_x + kFishX, row_y, alpha);
            rects += " fish " + std::to_string(board_x + kFishX) + ".." + std::to_string(board_x + kFishX + fish[i].Width()) + "<" +
                     std::to_string(board_x + kSizeX - digits * kDigitW);
        }
    }
    if (test && now != said) {
        said = now;
        std::fprintf(stderr, "coop-fishlog: test: rows %d..%d: %s (p%d's copy) at link tick %lld\n", top + 1, top + 5, now.c_str(), NetplayLocalPlayer(),
                     (long long) NetplayLinkTick());
        std::fprintf(stderr, "coop-fishlog: test: board (%d %d)-(%d %d), cursor row %d, headers RANK %d..%d NAME %d..%d FISH %d..%d SIZE %d..%d (y %d..%d), cm %d..%d, "
                     "rows:%s (p%d's copy) at link tick %lld\n",
                     board_x, board_y, board_x + kBoardW, board_y + kBoardH, cursor + 1, board_x + kHeadRankX, board_x + kHeadRankX + kHeadW,
                     board_x + kHeadNameX, board_x + kHeadNameX + kHeadW, board_x + kHeadFishX, board_x + kHeadFishX + kFishCapW + kFishWordW, board_x + kHeadSizeX,
                     board_x + kHeadSizeX + kSizeHeadW, board_y - 0xE, board_y - 0xE + 0x1B, board_x + kSizeX, board_x + kSizeX + 2 * kDigitW, rects.c_str(),
                     NetplayLocalPlayer(), (long long) NetplayLinkTick());
    }
}

// coop-fishing3: whose the shown log's entry `rank` is, by profile name ("" when the records are not the players': retail's log;
// "P<n>" for a seat with no profile, a save from before them).
std::string FishLogWho(int rank) {
    const FishPools &p = g_fish_pools;

    if (!PoolsOn() || p.on == 0 || rank < 0 || rank >= 64 || Mutated("fishlog_names_off")) {
        return std::string();
    }
    const int who = p.view_who[rank];

    if (who >= 0 && who < kMaxPlayers) {
        return p.seat_names[who][0] != 0 ? std::string(p.seat_names[who], strnlen(p.seat_names[who], kFishNameBytes)) : "P" + std::to_string(who + 1);
    }
    if (who >= kMaxPlayers && who < kMaxPlayers + kFishKeptNames) {
        return std::string(p.kept_names[who - kMaxPlayers], strnlen(p.kept_names[who - kMaxPlayers], kFishNameBytes));
    }
    return std::string();
}

void FishPoolsReset() {
    for (FishRecord &r : g_fish_records) {
        r = FishRecord{};
    }
    g_fish_kept.clear();
    g_fish_pools = FishPools{};
}

bool FishingPerPlayer() {
    static const bool test = TestEnvOn("DC_COOP_FISHING_PER_PLAYER");

    return OwnBags() && (test || !dc::platform::ConfigGet().coop_fishing_shared) && !Mutated("fishing_points_shared"); // (twin)
}

// Turn the records on: the save's record becomes player 1's own (a game begun without the profile records: until now the
// save's was the whole of it).
void FishPoolsBegin(const char *why) {
    FishPools &p = g_fish_pools;

    if (p.on != 0 || !PoolsOn()) {
        return;
    }
    p.on = 1;
    g_fish_records[0] = SaveRecord();
    p.per_player = FishingPerPlayer() ? 1 : 0;
    p.present = FishPresent();
    p.owner = 0;
    p.view = ViewOf(g_fish_records, p.present, p.per_player != 0, 0, p.view_who, &p);
    SavePut(p.view);
    std::fprintf(stderr, "coop-fishpools: the records are each player's (%s): %s\n", why, PoolsSaid(g_fish_records, 0b111111).c_str());
}

void FishRecordSwap(int index) {
    if (index < 0 || index >= kMaxPlayers || !PoolsOn()) {
        return;
    }
    FishPoolsBegin("a player's activity");
    FishPools &p = g_fish_pools;

    if (ApartHere()) {
        return; // (coop-fishpools2: apart every change on this copy is its own player's: FishPoolsStep)
    }
    FishTake(); // (what the game did under the view so far is its player's)
    if (p.inside[index] == 0) {
        p.inside[index] = 1;
        p.outer[index] = p.owner;
        FishPut(index);
    } else {
        p.inside[index] = 0;
        FishPut(p.outer[index]);
    }
}

// Every tick (the town's and a room's step, every copy alike): what changed in the save outside any activity is player 1's (the
// test seeds', say); then the view as the setting and the players in the game are now.
void FishPoolsStep() {
    if (!PoolsOn()) {
        return;
    }
    FishPoolsBegin("the first step with own bags");
    FishTake();
    FishPoolsSeed();
    FishPoolsMathSelfTest();
    FishPoolsTestHooks();
    FishTake(); // (a hook's change apart, where no activity's swap takes it)
    FishBuyQueue();
    bool inside = false;

    for (const std::int32_t in : g_fish_pools.inside) {
        inside = inside || in != 0;
    }
    FishPut(inside ? g_fish_pools.owner : DefaultOwner());
    FishPoolsTrace();
    FishBaitTrace();
}

bool FishBuyByCommand() {
    return PoolsOn() && NetplayPartnerLinked() && !CoopWorldsApart() && !Mutated("fishbuy_local");
}

void FishBuyClaim(int item, int price) {
    g_buy.item = item;
    g_buy.price = price;
    g_buy.due = true;
    g_buy.landed = 0;
    std::fprintf(stderr, "coop-fishpools: player %d claims item %d for %d fishing points at link tick %lld\n", NetplayLocalPlayer(), item, price,
                 (long long) NetplayLinkTick());
}

bool FishBuyWaiting() {
    return g_buy.due || g_buy.waiting;
}

int FishBuyLanded(int *item) {
    const int landed = g_buy.landed;

    if (landed < 0) {
        std::fprintf(stderr, "coop-fishpools: the exchange refuses: not enough points (the claim landed after another's) at link tick %lld\n", (long long) NetplayLinkTick());
    }

    g_buy.landed = 0;
    *item = g_buy.landed_item;
    return landed;
}

// The exchange's shown points, each of its steps while buys go by claims: the points as they stand (the claims spend them on every
// copy), and what it showed at its open moves with them, so its close writes nothing more.
void FishExchangeLive(int *point) {
    if (FishBuyByCommand() && SaveData != nullptr) {
        *point = std::clamp((int) *SaveData->CoopFishingPointWord(), 0, 9999);
        g_exchange_open_point = *point;
    }
}

namespace {
// Every copy, on the claim's tick: the buyer's view (their own, or the party's total) pays it, or it is refused.
void FishBuyApply(int player, const NetCommand &command, std::int64_t tick) {
    if (!PoolsOn() || SaveData == nullptr || player < 1 || player > kMaxPlayers) {
        return;
    }
    const int price = (int) command.c;

    FishRecordSwap(player - 1);
    const int  shown = *SaveData->CoopFishingPointWord();
    const bool granted = shown >= price;

    if (granted) {
        SaveData->AddFishingPoint(-price);
    }
    FishRecordSwap(player - 1);
    std::fprintf(stderr, "coop-fishpools: player %d's buy of item %d for %d fishing points %s at link tick %lld (%d shown to them)\n", player,
                 (int) command.b, price, granted ? "granted" : "REFUSED: not enough points", (long long) tick, shown);
    if (player == NetplayLocalPlayer() && g_buy.waiting) {
        g_buy.waiting = false;
        g_buy.landed = granted ? 1 : -1;
        g_buy.landed_item = (int) command.b;
    }
}
} // namespace

void FishBuyCommandHandler() {
    NetplaySetCommandHandler(kCmdFishBuy, FishBuyApply);
}

// coop-fishpools2: the apart sync's fishing slots (coop_worlds.cpp): which 0, kFishGain: index w * kMaxPlayers + k, pool k's
// changes made on player w's copy (points, Mardan's count; 8 bytes); which 1, kFishLog: index k * 64 + i, player k's own log's
// entry i (16 bytes; only k's copy changes it apart). Off without the records (no own bags) or with the twin.
int FishSlotRead(int which, int index, std::uint8_t *out) {
    if (!PoolsOn() || Mutated("fishpools_apart_off")) {
        return 0;
    }
    if (which == 0 && index >= 0 && index < kMaxPlayers * kMaxPlayers) {
        std::memcpy(out, g_fish_pools.apart[index / kMaxPlayers][index % kMaxPlayers], 8);
        return 8;
    }
    if (which == 1 && index >= 0 && index < kMaxPlayers * 64) {
        std::memcpy(out, &g_fish_records[index / 64].log[index % 64], sizeof(SV_FISH_DATA));
        return (int) sizeof(SV_FISH_DATA);
    }
    return 0;
}

void FishSlotWrite(int which, int index, const std::uint8_t *in) {
    if (!PoolsOn() || Mutated("fishpools_apart_off")) {
        return;
    }
    FishTake(); // (this copy's own changes first, to its own player)
    if (which == 0 && index >= 0 && index < kMaxPlayers * kMaxPlayers) {
        const int     w = index / kMaxPlayers, k = index % kMaxPlayers;
        std::int32_t  now[2];
        std::int32_t *had = g_fish_pools.apart[w][k];

        std::memcpy(now, in, 8);
        g_fish_records[k].points = std::clamp(g_fish_records[k].points + now[0] - had[0], 0, 9999);
        g_fish_records[k].mardan = std::max(0, g_fish_records[k].mardan + now[1] - had[1]);
        std::fprintf(stderr, "coop-fishpools: apart: player %d's copy changed player %d's points by %d more: now %d at link tick %lld\n", w + 1, k + 1,
                     (int) (now[0] - had[0]), (int) g_fish_records[k].points, (long long) NetplayLinkTick());
        had[0] = now[0];
        had[1] = now[1];
    } else if (which == 1 && index >= 0 && index < kMaxPlayers * 64) {
        std::memcpy(&g_fish_records[index / 64].log[index % 64], in, sizeof(SV_FISH_DATA));
    }
    FishPut(DefaultOwner());
}

int FishPointsOf(int index) {
    if (index < 0 || index >= kMaxPlayers || !PoolsOn()) {
        return SaveData != nullptr ? *SaveData->CoopFishingPointWord() : 0;
    }
    return g_fish_records[index].points;
}

int FishPointsShown() {
    return SaveData != nullptr ? *SaveData->CoopFishingPointWord() : 0;
}

std::uint32_t FishPoolsHash() {
    std::uint32_t h = 2166136261u;
    const auto    add = [&](const void *at, std::size_t bytes) {
        for (std::size_t i = 0; i < bytes; i++) {
            h = (h ^ static_cast<const unsigned char *>(at)[i]) * 16777619u;
        }
    };

    add(g_fish_records, sizeof g_fish_records);
    add(&g_fish_pools, sizeof g_fish_pools);
    return h;
}

// A resync carries the save and the carried parts, not the state set: the records and the view go with it (the host's taken).
// DC_COOP_MUTATE=fishpools_uncarried (twin): not carried.
void FishPoolsTake(std::vector<unsigned char> &out) {
    const auto *records = reinterpret_cast<const unsigned char *>(g_fish_records);
    const auto *pools = reinterpret_cast<const unsigned char *>(&g_fish_pools);

    out.insert(out.end(), records, records + sizeof g_fish_records);
    out.insert(out.end(), pools, pools + sizeof g_fish_pools);
}

void FishPoolsPut(const unsigned char *data, std::size_t size) {
    if (size != sizeof g_fish_records + sizeof g_fish_pools || Mutated("fishpools_uncarried")) {
        return;
    }
    std::memcpy(g_fish_records, data, sizeof g_fish_records);
    std::memcpy(&g_fish_pools, data + sizeof g_fish_records, sizeof g_fish_pools);
    std::fprintf(stderr, "coop-fishpools: the host's records carried: %s\n", PoolsSaid(g_fish_records, g_fish_pools.present).c_str());
}

void FishExchangeOpens(int point) {
    g_exchange_open_point = point;
    std::fprintf(stderr, "coop-fishpools: the fishing exchange opens showing %d points at link tick %lld\n", point, (long long) NetplayLinkTick());
}

int FishExchangeOpenPoint() {
    return Mutated("fish_exchange_absolute") ? (SaveData != nullptr ? *SaveData->CoopFishingPointWord() : 0) : g_exchange_open_point;
}

// Test hook, kept from coop-eventslots: DC_COOP_TEST_FISHING_POINTS_P2=N, player 2's own points.
void FishTestSeed() {
    if (static const int points = TestEnvInt("DC_COOP_TEST_FISHING_POINTS_P2", -1); points >= 0) {
        g_fish_records[1] = FishRecord{};
        g_fish_records[1].points = points;
        std::fprintf(stderr, "coop: test: player 2's own points %d\n", points);
    }
}

// The setting at a load, against the one the slot's file was written under: nothing moves (each record is its player's either
// way); kept for the file's word and the lobby's call.
void FishModeSwitch(bool to_per_player) {
    g_fish_saved_per_player = to_per_player;
}

// The profiles' records at a seat change (ProfileBagsGive's names): each player takes their profile's record, the one they held is
// kept under its owner, whichever way the setting is.
// candreds2: owners and names by player index (count of them), as the bags' (player 1's too: the records are each player's).
void FishProfilesGive(const std::string owners[], const std::string names[], int count) {
    if (!PoolsOn()) {
        return;
    }
    FishPoolsBegin("the lobby's start");
    FishTake();
    count = std::min(count, kMaxPlayers);
    FishRecord held[kMaxPlayers];

    for (int p = 0; p < count; p++) {
        held[p] = g_fish_records[p];
    }
    for (int p = 0; p < count; p++) {
        if (!owners[p].empty() && owners[p] != names[p]) {
            g_fish_kept[owners[p]] = held[p];
        }
    }
    for (int p = 0; p < count; p++) {
        if (owners[p] == names[p] || names[p].empty()) {
            continue;
        }
        auto             found = g_fish_kept.find(names[p]);
        const FishRecord r = found != g_fish_kept.end() ? found->second : FishRecord{};

        if (found != g_fish_kept.end()) {
            g_fish_kept.erase(found);
        }
        g_fish_records[p] = r;
        std::fprintf(stderr, "coop-inv: profile fishing: P%d is %s, %d points\n", p + 1, names[p].c_str(), r.points);
    }
    for (int p = 0; p < count && !Mutated("fishlog_names_off"); p++) { // coop-fishing3: the seats' names and the kept board
        NameInto(g_fish_pools.seat_names[p], names[p].empty() ? owners[p] : names[p]);
    }
    if (!Mutated("fishlog_names_off")) {
        KeptBoardBuild();
    }
    FishPut(g_fish_pools.owner);
}

// A join (ProfileBagsJoinBag on the host): the record the joiner's profile takes, after their bag: their seat's as it is when it
// is that profile's already (a rejoin), else the profile's kept one, else none.
void FishJoinRecord(int p, const std::string &name, std::vector<unsigned char> &out) {
    if (!PoolsOn() || p < 0 || p >= kMaxPlayers) {
        return;
    }
    FishRecord r;

    if (!g_bag_owner[p].empty() && g_bag_owner[p] == name) {
        r = g_fish_records[p];
    } else if (auto found = g_fish_kept.find(name); found != g_fish_kept.end()) {
        r = found->second;
    }
    const auto *bytes = reinterpret_cast<const unsigned char *>(&r);

    out.insert(out.end(), bytes, bytes + sizeof r);
}

// Every copy, on one tick (ProfileBagsJoinTake): the joiner's seat takes the record that came with their bag; the host files the
// one the seat held under its owner when the profile is another (never dropped). In the party's total from the lockstep's join
// tick, as the player is (FishPresent); a player who leaves takes theirs with them (out of the total, kept in their seat and,
// at the next save, under their profile).
void FishJoinTake(int p, const std::string &name, const unsigned char *bytes, std::size_t size, bool host) {
    if (!PoolsOn() || p < 0 || p >= kMaxPlayers || size != sizeof(FishRecord)) {
        return;
    }
    FishPoolsBegin("a join");
    FishTake();
    FishRecord r;

    std::memcpy(&r, bytes, sizeof r);
    const std::string &was = g_bag_owner[p];

    if (host && !was.empty() && was != name) {
        g_fish_kept[was] = g_fish_records[p];
    }
    if (host) {
        g_fish_kept.erase(name);
    }
    if (!Mutated("fishlog_names_off")) {
        KeptBoardSeat(p, name); // coop-fishing3 (every copy: the names and the board are in the state set)
    }
    g_fish_records[p] = r;
    FishPut(g_fish_pools.owner);
    std::fprintf(stderr, "coop-fishpools: the joiner (P%d) is %s with %d fishing points of their own at link tick %lld\n", p + 1, name.c_str(), r.points,
                 (long long) NetplayLinkTick());
    if (static const bool test = TestEnvOn("DC_COOP_TEST_FISHLOG"); test) { // coop-fishing3: whose the shown log's first catches are now
        std::string who;

        for (int i = 0; i < 5; i++) {
            who += (i > 0 ? " | " : "") + FishLogWho(i);
        }
        std::fprintf(stderr, "coop-fishlog: test: after the join (P%d %s) the log's first catchers: %s (p%d's copy) at link tick %lld\n", p + 1, name.c_str(),
                     who.c_str(), NetplayLocalPlayer(), (long long) NetplayLinkTick());
    }
}

// (owners: kMaxPlayers of them, by player index; every seat's record, player 1's too, and the kept ones; "pools": the file
// knows the records are each player's always; "shown": the save's points as written, so a later load can tell a change made
// with the game alone)
nlohmann::json FishProfilesJson(const std::string owners[]) {
    nlohmann::json records = nlohmann::json::object();

    for (const auto &[name, r] : g_fish_kept) {
        records[name] = RecordJson(r);
    }
    for (int p = 0; p < kMaxPlayers && PoolsOn(); p++) {
        if (!owners[p].empty()) {
            records[owners[p]] = RecordJson(g_fish_records[p]);
        }
    }
    nlohmann::json out = {{"per_player", FishingPerPlayer()}, {"records", records}};

    if (PoolsOn()) {
        out["pools"] = true;
        out["shown"] = FishPointsShown();
        if (owners[0].empty()) {
            out["p1"] = RecordJson(g_fish_records[0]); // (a nameless player 1's own: kept by seat)
        }
    }
    return out;
}

// Reading a slot's file. A file from before coop-fishpools ("pools" missing) kept the save's record as the party's pool (the
// setting on) or as player 1's own (off): either way it becomes player 1's own (the host's profile), so the shown total is
// what it was, nothing is guessed, and every other record stays its own. Each player's records ("pools"): as written; points
// the save gained or lost with the game alone (no co-op since) are player 1's.
void FishProfilesRead(const nlohmann::json &root, const std::string owners[]) {
    g_fish_kept.clear();
    const nlohmann::json fishing = root.value("fishing", nlohmann::json::object());
    // (DC_COOP_MUTATE=fish_old_save_unread, twin: an old file read as a new one, the save's points player 1's own never)
    const bool           pools = fishing.value("pools", false) || Mutated("fish_old_save_unread");

    g_fish_saved_per_player = fishing.value("per_player", false);
    for (FishRecord &r : g_fish_records) {
        r = FishRecord{};
    }
    for (const auto &[name, r] : fishing.value("records", nlohmann::json::object()).items()) {
        int seat = -1;

        for (int p = 0; p < kMaxPlayers && seat < 0 && (pools || g_fish_saved_per_player); p++) {
            seat = !owners[p].empty() && name == owners[p] && (pools || p > 0) ? p : -1;
        }
        if (seat >= 0) {
            g_fish_records[seat] = JsonRecord(r);
        } else {
            g_fish_kept[name] = JsonRecord(r);
        }
    }
    g_fish_pools = FishPools{};
    if (!PoolsOn()) {
        return;
    }
    for (int p = 0; p < kMaxPlayers && !Mutated("fishlog_names_off"); p++) { // coop-fishing3: the seats' names and the kept board
        NameInto(g_fish_pools.seat_names[p], owners[p]);
    }
    if (!Mutated("fishlog_names_off")) {
        KeptBoardBuild();
    }
    std::string kept;

    for (const auto &[name, r] : g_fish_kept) {
        kept += " " + name + " " + std::to_string(r.points);
    }
    std::fprintf(stderr, "coop-fishpools: the slot's file read (%s): kept profiles' records:%s\n", pools ? "each player's records" : "from before them",
                 kept.empty() ? " none" : kept.c_str());
    const FishRecord save = SaveRecord();

    if (!pools) {
        g_fish_records[0] = save; // (the file before coop-fishpools: the save's record is player 1's own)
        std::fprintf(stderr, "coop-fishpools: a slot from before each player's records: the save's %d points are player 1's own (%s)\n", save.points,
                     g_fish_saved_per_player ? "they were" : "the party's pool before");
    } else {
        if (owners[0].empty() && fishing.contains("p1")) {
            g_fish_records[0] = JsonRecord(fishing["p1"]);
        }
        const int shown = fishing.value("shown", save.points);

        if (save.points != shown) {
            g_fish_records[0].points = std::clamp(g_fish_records[0].points + save.points - shown, 0, 9999);
            std::fprintf(stderr, "coop-fishpools: the save's points moved %d with the game alone: player 1's own now %d\n", save.points - shown,
                         g_fish_records[0].points);
        }
    }
    g_fish_pools.on = 1;
    g_fish_pools.present = 0; // (said at the first view)
    FishPut(0);
}

// candreds2 test hook (DC_COOP_TEST_BAGS_FILE=<a scratch folder>, x-eventslots -Case bagsfile): the profile bags' file by player.
// An old two-player slot's coop-bags.json (as the builds before wrote it) is read and written back byte for byte; then a third
// player's owner goes through the file and back. Every record is put back after (both copies alike).
void ProfileBagsFileSelfTest(const char *save_directory) {
    if (save_directory == nullptr || SaveData == nullptr) {
        return;
    }
    const std::filesystem::path dir = std::filesystem::path(save_directory) / ("bags-file-test-p" + std::to_string(NetplayLocalPlayer())); // (each copy its own)
    std::error_code             error;
    std::string                 owners_was[kMaxPlayers];

    for (int p = 0; p < kMaxPlayers; p++) {
        owners_was[p] = g_bag_owner[p];
    }
    const auto kept_was = g_kept;
    const bool new_game_was = g_owners_for_new_game;
    const auto fish_kept_was = g_fish_kept;
    const bool fish_saved_was = g_fish_saved_per_player;
    FishRecord fish_was[kMaxPlayers]; // (coop-fishpools: a read sets the records and the view: all put back)
    const FishPools  pools_was = g_fish_pools;
    const FishRecord save_was = SaveRecord();

    std::copy(g_fish_records, g_fish_records + kMaxPlayers, fish_was);
    const auto read = [](const std::filesystem::path &p) {
        std::ifstream in(p, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    };

    // An old slot's file, in the shape ProfileBagsWrite gave it before (owners p1 and p2, the fishing record, one kept bag).
    nlohmann::json old_root;

    old_root["owners"] = {{"p1", "AL"}, {"p2", "BO"}};
    old_root["fishing"] = {{"per_player", FishingPerPlayer()}, {"records", nlohmann::json::object()}};
    old_root["kept"] = nlohmann::json::object();
    old_root["kept"]["CY"] = BagJson(EmptyBag(((CUserStatus *) SaveData->GetDngStatus())->item_pack.num));
    const std::string old_text = old_root.dump(2) + "\n";

    std::filesystem::create_directories(dir / "old", error);
    std::filesystem::create_directories(dir / "again", error);
    std::filesystem::create_directories(dir / "three", error);
    std::ofstream(dir / "old" / kBagsFile, std::ios::binary) << old_text;
    ProfileBagsRead(dir / "old");
    ProfileBagsWrite(dir / "again");
    const bool same = read(dir / "again" / kBagsFile) == old_text;

    std::fprintf(stderr, "coop-inv: test: bags file: an old two-player file read (P1 %s, P2 %s, %zu kept), written back the same: %s\n", g_bag_owner[0].c_str(),
                 g_bag_owner[1].c_str(), g_kept.size(), same ? "yes" : "no");

    // A third player's owner through the file and back.
    g_bag_owner[2] = "DEE";
    ProfileBagsWrite(dir / "three");
    ProfileBagsRead(dir / "three");
    std::fprintf(stderr, "coop-inv: test: bags file: a third player's owner through the file: P3 %s (P1 %s, P2 %s)\n", g_bag_owner[2].empty() ? "none" : g_bag_owner[2].c_str(),
                 g_bag_owner[0].c_str(), g_bag_owner[1].c_str());

    for (int p = 0; p < kMaxPlayers; p++) {
        g_bag_owner[p] = owners_was[p];
    }
    g_kept = kept_was;
    g_owners_for_new_game = new_game_was;
    g_fish_kept = fish_kept_was;
    g_fish_saved_per_player = fish_saved_was;
    std::copy(fish_was, fish_was + kMaxPlayers, g_fish_records);
    g_fish_pools = pools_was;
    SavePut(save_was);
    std::filesystem::remove_all(dir, error);
}
// save6 test hook (DC_COOP_TEST_SAVE6=<a scratch folder>, x-eventslots -Case save6): the slot's co-op files for 6 players, on the
// files themselves (no host check), each copy in its own folder, every record put back after (both copies alike).
//   DC_COOP_TEST_SAVE6_OLD=<folder>: an older release's slot files (v0.1.1 wrote them): read, saved as this release saves (the
//   backups made once), read again, saved again (the backups untouched), and read by v0.1.1's own reader (its 1024 and 8 bytes);
//   then every player 2 to 6 given a bag, an owner and a pick, through the files and back, compared.
namespace {

std::string BagSaid(const P2Bag &bag) {
    std::uint32_t hash = 2166136261u;
    const auto   *bytes = reinterpret_cast<const unsigned char *>(&bag.pack);

    for (std::size_t i = 0; i < sizeof bag.pack; i++) {
        hash = (hash ^ bytes[i]) * 16777619u;
    }
    for (int k = 0; k < 3; k++) {
        hash = (hash ^ (std::uint32_t) bag.active_item_vol[k]) * 16777619u;
    }
    char text[96];

    std::snprintf(text, sizeof text, "%d carried, %d gilda, quick-use %d, bag %08x", Carried(bag.pack), (int) bag.gilda, (int) bag.selected, hash);
    return text;
}

bool SameBag(const ProfileBag &a, const ProfileBag &b) {
    return std::memcmp(&a.pack, &b.pack, sizeof a.pack) == 0 && std::memcmp(a.active_item_vol, b.active_item_vol, sizeof a.active_item_vol) == 0 &&
           a.gilda == b.gilda && a.selected == b.selected;
}

std::string FileText(const fs::path &path) {
    std::ifstream in(path, std::ios::binary);

    return in ? std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()) : std::string("<none>");
}

} // namespace

void Save6SelfTest(const char *directory) {
    static bool done = false;

    if (directory == nullptr || done || SaveData == nullptr) {
        return;
    }
    done = true;
    CUserStatus     &status = *(CUserStatus *) SaveData->GetDngStatus();
    const fs::path   dir = fs::path(directory) / ("save6-test-p" + std::to_string(NetplayLocalPlayer())); // (each copy its own)
    const char      *old_from = TestEnv("DC_COOP_TEST_SAVE6_OLD");
    std::error_code  error;
    const char      *files[3] = {kFileName, kBagsFile, "coop-party.dat"};

    // What the test changes, put back at its end.
    const P2Bag      bag_was = g_bag;
    P2Bag            more_was[kMaxPlayers];
    std::vector<unsigned char> tails_was[kMaxPlayers];
    const std::uint32_t record_was = g_more_record;
    const ITEM_PACK  ring_was = g_key_ring;
    const ITEM_PACK  pack_was = status.item_pack;
    const int        money_was = status.money;
    std::string      owners_was[kMaxPlayers];
    int              picks_was[kMaxPlayers];
    FishRecord       fish_was[kMaxPlayers];
    const auto       kept_was = g_kept;
    const bool       new_game_was = g_owners_for_new_game;
    const auto       fish_kept_was = g_fish_kept;
    const bool       fish_saved_was = g_fish_saved_per_player;
    const FishPools  pools_was = g_fish_pools; // coop-fishpools
    const FishRecord save_was = SaveRecord();
    const int        choice_was = g_partner.choice;

    for (int p = 0; p < kMaxPlayers; p++) {
        more_was[p] = g_more[p];
        tails_was[p] = g_more_tail[p];
        owners_was[p] = g_bag_owner[p];
        picks_was[p] = CoopPartyMorePick(p);
        fish_was[p] = g_fish_records[p];
    }
    const auto read_all = [&](const fs::path &from, int picks[kMaxPlayers], int &party_layout) {
        InventoryNewGame();
        ProfileBagsRead(from);
        const int layout = BagFileRead(from);

        party_layout = CoopPartyFileRead(from, picks);
        for (int p = 2; p < kMaxPlayers; p++) {
            CoopPartyMorePickSet(p, picks[p]);
        }
        return layout;
    };
    const auto write_all = [&](const fs::path &to, int pick) {
        CoopSlotKeepOldLayout(to); // (as CoopInventorySave and CoopPartySave do first)
        BagFileWrite(to);
        ProfileBagsWrite(to);
        SetPick(1, pick, MemberWhy::kTest);
        CoopPartyFileWrite(to);
    };
    const auto said = [&](const char *what, int layout, int party_layout, int pick) {
        std::fprintf(stderr, "coop-inv: test: save6: %s: layout %d, party layout %d: P2 %s; ring %d keys; owners P1 %s, P2 %s, %zu kept; pad 2's pick %d\n", what, layout,
                     party_layout, BagSaid(g_bag).c_str(), PackKeys(g_key_ring), g_bag_owner[0].c_str(), g_bag_owner[1].c_str(), g_kept.size(), pick);
    };

    fs::remove_all(dir, error);
    fs::create_directories(dir / "old", error);
    fs::create_directories(dir / "six", error);

    if (old_from != nullptr) {
        std::string original[3];
        int         picks[kMaxPlayers];
        int         party_layout = 0;

        for (int f = 0; f < 3; f++) {
            if (fs::exists(fs::path(old_from) / files[f], error)) {
                fs::copy_file(fs::path(old_from) / files[f], dir / "old" / files[f], fs::copy_options::overwrite_existing, error);
            }
            original[f] = FileText(dir / "old" / files[f]);
        }
        int layout = read_all(dir / "old", picks, party_layout);

        said("an older release's slot read", layout, party_layout, picks[1]);
        write_all(dir / "old", picks[1]);
        std::string backups;

        for (int f = 0; f < 3; f++) {
            backups += std::string(f > 0 ? ", " : "") + files[f] + " " +
                       (original[f] == "<none>" ? "none" : FileText(dir / "old" / (std::string(files[f]) + kSlotBackupSuffix)) == original[f] ? "yes" : "no");
        }
        std::fprintf(stderr, "coop-inv: test: save6: the first save kept the older files as they were: %s\n", backups.c_str());
        layout = read_all(dir / "old", picks, party_layout);
        said("the slot read after this release's save", layout, party_layout, picks[1]);
        write_all(dir / "old", picks[1]);
        bool same = true;

        for (int f = 0; f < 3; f++) {
            same = same && (original[f] == "<none>" || FileText(dir / "old" / (std::string(files[f]) + kSlotBackupSuffix)) == original[f]);
        }
        std::fprintf(stderr, "coop-inv: test: save6: a second save left the backups as they were: %s\n", same ? "yes" : "no");

        // v0.1.1's own reading of this release's files: coop-p2.dat into its 1024 bytes through InventoryRead (unchanged since),
        // coop-party.dat's first 8 bytes, coop-bags.json as it is.
        unsigned char bytes[1024];
        std::uint32_t party[2] = {};
        const auto    size = FilesRead(dir / "old" / kFileName, bytes, sizeof bytes);
        const auto    party_size = FilesRead(dir / "old" / "coop-party.dat", party, sizeof party);

        InventoryNewGame();
        ProfileBagsRead(dir / "old");
        const bool read = InventoryRead(bytes, size);

        said("v0.1.1's reading of the files this release wrote", read ? 2 : 0, party_size == sizeof party && party[0] == 0x31505043 ? 2 : 0,
             party[1] <= 5 ? (int) party[1] : -1);
    }

    // Six players: each of players 2 to 6 a bag of their own (p copies of item 148, 100p+p gilda, quick-use pick), an owner and a pick.
    const char *names[kMaxPlayers] = {"AL", "BO", "C3", "D4", "E5", "F6"};
    ProfileBag  want[kMaxPlayers];

    for (int p = 1; p < kMaxPlayers; p++) {
        ProfileBag bag = EmptyBag(status.item_pack.num > 0 ? status.item_pack.num : 50);

        for (int n = 0; n <= p; n++) {
            Put(bag.pack, 148, ItemDataToHaveCopy(148));
        }
        bag.gilda = (std::uint16_t) (100 * (p + 1) + p + 1);
        bag.selected = p % 3 + 1;
        GiveBag(p, status, bag);
        HeldBag(p, status, want[p]);
        g_bag_owner[p] = names[p];
        CoopPartyMorePickSet(p, p);
    }
    g_bag_owner[0] = names[0];
    write_all(dir / "six", 1);
    InventoryNewGame();
    OwnersClear();
    for (int p = 2; p < kMaxPlayers; p++) {
        CoopPartyMorePickSet(p, -1);
    }
    int        picks[kMaxPlayers];
    int        party_layout = 0;
    const int  layout = read_all(dir / "six", picks, party_layout);
    std::string each;

    for (int p = 1; p < kMaxPlayers; p++) {
        ProfileBag got;
        const bool held = p == 1 || g_more[p].seeded;

        HeldBag(p, status, got);
        each += (p > 1 ? ", P" : "P") + std::to_string(p + 1) + " " + (held && SameBag(got, want[p]) ? "bag" : "BAG LOST") +
                (g_bag_owner[p] == names[p] ? " owner" : " OWNER LOST") + (picks[p] == p ? " pick" : " PICK LOST");
    }
    std::fprintf(stderr, "coop-inv: test: save6: 6 players through the slot's files (layout %d, party layout %d, coop-p2.dat %ju bytes): %s; P3 %s\n", layout,
                 party_layout, (std::uintmax_t) fs::file_size(dir / "six" / kFileName, error), each.c_str(), BagSaid(g_more[2]).c_str());

    // Put back.
    g_bag = bag_was;
    g_key_ring = ring_was;
    status.item_pack = pack_was;
    status.money = money_was;
    for (int p = 0; p < kMaxPlayers; p++) {
        g_more[p] = more_was[p];
        g_more_tail[p] = tails_was[p];
        g_bag_owner[p] = owners_was[p];
        CoopPartyMorePickSet(p, picks_was[p]);
        g_fish_records[p] = fish_was[p];
    }
    g_more_record = record_was;
    g_kept = kept_was;
    g_owners_for_new_game = new_game_was;
    g_fish_kept = fish_kept_was;
    g_fish_saved_per_player = fish_saved_was;
    g_fish_pools = pools_was;
    SavePut(save_was);
    SetPick(1, choice_was, MemberWhy::kTest);
    // (the folder stays, in the row's own output, cleared at the next run's start: its six\ is a 6-player slot's co-op files
    // that an older release can be pointed at)
}
} // namespace coop
