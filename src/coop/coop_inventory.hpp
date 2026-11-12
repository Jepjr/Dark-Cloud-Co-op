#pragma once

#include "coop/game_ns.hpp"

// Per-player inventories (game.coop_own_bags, or DC_COOP_OWN_BAGS=1 for one launch; off by default; needs game.coop): player 2 carries an
// item bag and a purse of gilda of their own. Player 1 keeps the game's own (UserStatus's item pack and
// money), untouched; storage, weapons, attachments, Atla and story progress stay the party's.
//
// Player 2's bag has the game's ITEM_PACK layout (the same number of slots player 1's pack has when it
// is seeded), the quick-use slots' uses left beside it as the game keeps player 1's, and its gilda as a
// u16 like UserStatus.money. A new game seeds it empty with no gilda; a save keeps it in a file beside
// the slot (saves/<N>/coop-p2.dat), never inside the game's save, so every save still loads in a game
// without co-op.
//
// Story items (the gate keys and the dungeons' key and quest items) are the world's, not a player's: with separate bags
// they live in the party's key ring (coop-worldkeys, RingOn), outside both bags, and cannot be sent. Without separate
// bags they go to player 1's pack as retail's.
//
// Sending an item to the other player: one copy of the item leaves the sender's bag and enters the
// receiver's, or nothing happens when the receiver's bag is full (checked before anything is taken).
//   Player 2: Select+Square sends the item Square would use (D-pad Left and Right pick it). In pad 2's
//             panel (Triangle), Square sends the item under the cursor on the Items page, and the Bag
//             page lists everything in the bag with Cross sending. (While linked on this branch the
//             link gives pad 2's Triangle to the game's menu, so there the chord is the way.)
//   Player 1: R3 sends nothing (Jep, 2026-10-25; v-no-r3-menu -Case p1). Only the bot test (DC_COOP_INVTEST) and
//             DC_COOP_MUTATE=r3send keep the old send of one of the item in the quick-use slot the D-pad has picked.
//   Both:     Select in the game's own bag screen (the item menu's Items page) sends the item under the
//             cursor, in either player's menu, together or apart, on a floor or in a town; the screen's
//             help box says so ("{select} Send to P2." or "... P1." under retail's two lines). coop-itemsend.
// Sends come from the pads alone, so linked copies (which agree on both pads every tick) send on the
// same tick, and couch play sends at once.

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "platform/input.hpp"
#include "userstatus.hpp"
#include "coop/netplay_cmd.hpp" // coop-bagsendlocal: InventoryApplyGive's command

namespace dc::game {
class ClsMes;
} // namespace dc::game

namespace coop {

// Player 2's bag and purse.
struct P2Bag {
    ITEM_PACK     pack{};
    std::int32_t  active_item_vol[3] = {};
    std::uint16_t gilda = 0;
    std::int32_t  selected = 1;   // the quick-use slot (1 to 3) pad 2's D-pad Left and Right have picked
    bool          seeded = false; // pack.num is set; false until the first use after a new game
};

// coop-bags: each tick (the dungeon's and the town's step): the party's bag size to both bags, and the bag-size test hooks.
void BagSizeStep();
void BagSizeRaised(int size); // EdAddMaxItem's upgrade (the global CoopBagSizeRaised, recipe 0068): every bag takes it
void BagSizeNow();             // after a change made elsewhere (apart header apply, a load, a join's bag): the largest size to every bag
void BagSizesSay(const char *label);

// Whether the players have a bag each: game.coop and game.coop_own_bags.
bool OwnBags();
// coop-eventslots: the player (1 up) whose own bag P2Bag keeps (the game's pack is player 1's); one more bag a player is later work.
int OwnBagPlayer();

// coop-eventslots (Jep 11-11, FP and the fishing log: game.coop_fishing_shared, default shared): each player's own fishing record when
// they are each player's (FishingPerPlayer: the config off, with own bags; DC_COOP_FISHING_PER_PLAYER=1 for the rows). The game's
// player's record is the save's own (SaveData); every other player's is kept here by actor index, with their profile in
// coop-bags.json beside their bag, and exchanged with the save's around what reads or writes it (their fishing, their own events,
// their menus' writes). A switch of the setting on a save keeps every point (FishModeSwitch).
bool FishingPerPlayer();
void FishRecordSwap(int index);        // the save's fishing record and player index's (index > 0), exchanged (twice puts back)
int  FishPointsOf(int index);          // player index's points as they stand (the save's for the game's player, or the party's)
void FishTestSeed();                   // DC_COOP_TEST_FISHING_POINTS_P2=N: player 2's own points (per-player rows)
void FishSwitchSelfTest(const char *directory); // DC_COOP_TEST_FISH_SWITCH=<a scratch folder>: the switch both ways and the profile file, checked

// The pack player 2's items come from and go to: its own while the bags are separate, else the party's.
ITEM_PACK &PartnerPack();

// Player 2's purse: its own gilda while the bags are separate, else the party's money.
int PartnerGilda();

// coop-actorswap: the uses left in player 2's quick-use slots (her own while the bags are separate, else the party's).
std::int32_t *PartnerActiveItemVol();

// Gold player 2 picks up: into its own purse, or the party's.
void PartnerGold(int amount);

// coop: gaps2
// Sets player 2's purse (its own gilda, or the party's money), held to 0 to 0xFFFF: a trap circle's
// money up or down sprung by player 2.
void PartnerSetGilda(int gilda);

// Whether an item player 2 takes goes into its own bag (a bag item that is not a story item, while the
// bags are separate). Everything else goes where the game puts it for the party.
bool PartnerKeeps(int item);

// As CDngStatusData::CheckItemGet, for an item player 2 takes: 0 when it fits, 1 when the bag it
// goes into is full, 2 when the attachments are.
int PartnerCheckItemGet(int item);

// Puts an item player 2 takes into its bag (with volume uses, -1 for the item's own), and with message
// shows the item's get message and sound. False when the item is not one PartnerKeeps or the bag is
// full; the caller then gives it to the party as before.
bool PartnerGetItem(int item, int volume, bool message);

// coop-keypick: a story item or gate key, which stays in the party's pack for both players.
bool SharedItem(int item);

// coop-keyring: the party's key ring. coop-worldkeys (Jep, 2026-11-04: "key items shouldn't go into P1s inventory because
// P1 could use a seperate profile next time"): with separate bags it is g_key_ring, the world's, in no player's bag; it is
// saved with the slot (coop-p2.dat), goes with the link's state and the apart slot sync, and the game's own lookups
// (CDngStatusData's GetItem, SearchItemIndexNo, LostItem, CheckItemGet, the event Use Item window) find its items there.
// Without separate bags (or DC_COOP_MUTATE=ringbag, the twin): player 1's pack, wherever this copy keeps it (coop::g_bag on
// her copy apart, where her bag is the game's pack; the game's pack everywhere else), as before.
ITEM_PACK &SharedRing();
bool       RingOn();
// An item the ring keeps (RingOn): a story item or key (SharedItem), not the floor's map and crystal or the world map
// (flags the game sets, never carried).
bool RingItem(int item);
// Moves every ring item out of pack into the ring (a save from before, a bag from a profile file); the count moved.
int RingAdopt(ITEM_PACK &pack, const char *from);
// How many of item the ring holds.
int RingCount(int item);
// Where the keys are now, on stderr ("coop-inv: keys now (why): ring R, P1's bag K, P2's bag J ...").
void RingSay(const char *why);
// After a slot's load (CoopInventoryLoad): the seated players' bags, then the bags' keys into the ring.
void RingAfterLoad();

// coop-keyfloor: a gate key or floor key (Bone Key to Silver Key), as LostGateKey drops them.
bool FloorKeyItem(int item);
// The uses a floor key found here gets: the floor it was found on (any other item, or off a floor: volume).
int KeyFloorTag(int item, int volume);
// Whether pack's slot may be used on this copy's floor: not a floor key, untagged, or tagged for this floor.
bool KeyFitsHere(const ITEM_PACK &pack, int slot);
// The floor a floor key in pack's slot was found on (false: none recorded).
bool KeyFloorOf(const ITEM_PACK &pack, int slot, int *map, int *floor);

// Items in player 2's bag, each once in bag order (BagItems), and how many of one it holds.
int BagItems(int *out, int max);
int BagCount(int item);

// The result of a send.
enum class Send { kSent, kNone, kFull, kStory, kOff };

// Sends one of item from player from (1 or 2) to the other. Logs "coop-inv: send ..." either way.
Send SendItem(int from, int item);

// coop-itemsend: the bag screen's Select (battlemenu.cpp's ItemMenuMainKey, the board's Items page). While a menu
// runs the game's pack is the bag it shows, its owner's (coop_fullmenu and coop_townmenu swap player 2's in for her
// menu; the worlds swap it in on her copy apart), and g_bag is the other player's. So the item under the cursor goes
// from the one to the other whoever's menu it is. Together both copies step the menu from the agreed pads; apart only
// the owner's copy runs it and the bags' slots go over the live sync (coop_worlds' bag regions, first to act wins).
// BagScreenSendOn: separate bags on. BagScreenSend: pack is the menu's (MenuItemPackPt), slot the board's cursor;
// logs "coop-inv: bag screen send ..." either way and gives whether it sent (the caller plays the menu's sound).
// DC_COOP_MUTATE=bagsend: Select does nothing (twin).
bool BagScreenSendOn();
bool BagScreenSend(ITEM_PACK *pack, int slot);
// coop-bagsendlocal: from a local menu (its owner's copy alone) the item goes to the other bag as a command (kCmdGive), which
// this applies on every copy on its tick; it logs "coop-inv: player N's send of item ... at link tick T". DC_COOP_MUTATE=
// bagsend_here (twin): the owner's copy puts it at once, alone (Jep's 11-04 pack 8 desync).
void InventoryApplyGive(int player, const NetCommand &command, std::int64_t tick);

// The bag screen's help box (ItemMenuMainKey picks the text, ItemMenuModeDraw draws the box): on the Items page,
// retail's text (system message 0xB5, "{square} Organize." / "{triangle} To active item.") with a third line,
// "{select} Send to P2." (or P1. in her menu), made in the same window (AtoraNameMes) so it is laid out and drawn as retail's lines are and its
// glyphs follow the pad or keyboard as theirs do. BagScreenHelp makes it when the window does not hold it yet and
// gives true while it is the one shown; the box then grows by BagScreenHelpExtraLines() lines, upward.
// DC_COOP_MUTATE=bagsendhelp: retail's two lines and box (twin).
bool BagScreenHelp(ClsMes &mes, int retail_id);
// coop-bagapart: test trace (DC_COOP_BAGTRACE): the bag the item menu shows (MenuItemPackPt) and its member, on stderr each
// time they change ("coop-inv: bag screen shows items=... member N (copy of player P, apart A, ...)"). From the Items page's
// help and around the town's own menu (CoopTownApartMenuEnd, before her member goes back).
void BagScreenTrace();
int  BagScreenHelpExtraLines();
// coop-sharedkeys: the party's shared keys and story items, named in a box in the item menu's empty bottom-right corner, on
// whichever player's menu shows (from ItemMenuModeDraw).
void BagScreenSharedDraw();
// coop-keytab: the party's ring as the item menu sees it (MenuItemPackPt set): the pack beside the one shown while her bag is
// shown, else the one shown (the Keys tab and the shared box list it).
const ITEM_PACK &ShownRing();

// Test hook (DC_COOP_BAGSEED=items/items, the bag screen tests): puts those items in player 1's bag and player 2's.
// From the menu presses' clock start (netplay.cpp), the same tick on both copies.
void InventoryTestSeed();

// Test hook (coop_worlds' DC_WORLDS_TEST_BAGSEND, apart): item into the first empty slot of pack (the copy's own
// player's bag), then the bag screen's send of that slot.
void InventoryTestSend(ITEM_PACK &pack, int item);

// Player 2's quick-use slots, as player 1's (the pack's quick_item_slot/qty, the uses left beside
// them, and a selection of its own). While the bags are separate, pad 2's D-pad Left and Right move
// the selection and Square uses the selected slot's item (CombatInput calls PartnerSlotItem with the
// buttons pressed this tick; it gives the item, or -1 for an empty slot). SpendFromSlot spends one
// from the selected slot when it holds the item. PartnerSlotAssign (the panel's Bag page, Square) puts
// every copy of an item into the selected slot, what was there going back into the bag.
int  PartnerSlotItem(std::uint16_t pressed);
bool SpendFromSlot(int item);

// coop-p2feather: one frame of her Dran's Feather's run boost from her selected quick-use slot; false when it holds none.
bool SpendBoostFrame();
void PartnerSlotAssign(int item);

// coop-revive: the Revive Powders player (1 or 2) carries (its own bag while the bags are separate, else the party's
// pack; bag slots and quick-use slots alike), and one of them spent, from its slots first as Take does. Spend logs
// "coop-inv: P<n> spends a Revive Powder ..." and is false when there was none.
int  RevivePowders(int player);
bool SpendRevivePowder(int player);

// coop-dranscheat (Jep, 2026-11-01): a test cheat, on only while DC_QA_CHEATS is on, which the harness's Start-Copy sets on every
// test copy and no launcher a player runs sets (Playtest.cmd, Play Dark Cloud Co-op.exe, the LAN pack). A player who holds
// L1+R1 and presses Select+Triangle on the agreed pads gets one Dran's Feather in quick-use slot 1 of their own bag, once per press
// of the whole chord (netplay.cpp's Route, the same tick on both copies). While that player holds L1 and R1, their Select, Triangle
// and Start reach nothing else (no ring, menu, menu claim or pause) until all three are let go; the chord keeps clear of the
// game's raw pad-1 debug chords (netplay.cpp says which). DransCheatGive(index): entry index (0 player 1, 1 player 2)
// gets it: a feather already in slot 1 counts one more, else what the slot held goes back into the bag (refused, logged, when the
// bag has no room for it). Logs "coop-inv: dranscheat P<n> at link tick T: P1 slot 1 item I x Q, P2 slot 1 item I x Q".
// DC_COOP_MUTATE=dranscheat: the press is logged and nothing is given (twin); =dranscheat_mask: Select, Triangle and Start not
// taken off (twin).
bool DransCheatOn();
void DransCheatGive(int index);

// Player 2's slots as a short line for its HUD row: the selected one in brackets.
std::string SlotsLine();

// Drawing: the quick-use slots the top of the screen shows. On player 2's linked copy (its own
// window) with separate bags, player 2's slots and selection; else player 1's. topStatusInfo calls it;
// it also records what was drawn for the test (DrawnSlots). CoopQuickSlotTextureCopy, after
// DngActiveItemTextureCopy, puts player 2's icons in the HUD's icon cells on that copy.
ITEM_PACK *CoopTopSlots(ITEM_PACK *players, int *selected);
// While coop_hud draws the other player's bar through topStatusInfo: its life, weapon and water rows only.
bool       StatusBarsOnly();
void       CoopQuickSlotTextureCopy();
// coop-keydoor: after a join's apply, the HUD's item cells are filled again once its icon sheet is back (DrawBlocksSnapshotApplied).
void       CoopHudIconsAgain();
// coop-keydoor (coop_hud.cpp): the icon block (0x43) holds itemlst.img's sheets again, entered after a join's apply.
void       HudIconSheetEntered();
bool       DrawnSlots(int items[3], int counts[3], int *selected);

// What player 2's panel shows: the number of pages (2, or 3 with the Bag page while the bags are
// separate), and a short line of the last send for a few seconds (empty when none).
int         PanelPages();
const char *InventoryNotice();

// Once a tick from CoopStep: player 1's R3 send, and the test bot (DC_COOP_INVTEST).
void InventoryStep(bool playing);

// The test bot's pad 2 buttons (DC_COOP_INVTEST), as CombatTestPad; false when it holds none.
bool InventoryTestPad(InputPadState &pad);

// The test bot (coop_inventory_test.cpp): its tick.
void InventoryTestTick();

// The bag's bytes for the linked copies' checksum and desync dump, and its hash.
void          InventoryBytes(std::vector<unsigned char> &out);
std::uint32_t InventoryHash();

// Player 2's bag and purse back to a new game's: empty and no gilda.
void InventoryNewGame();

// The resync seam for the link (coop-netplay): the host appends its bag after its SaveData, the
// joiner reads it back. Read returns false (and leaves the bag) when the bytes are not a bag.
void InventoryWrite(std::vector<unsigned char> &out);
bool InventoryRead(const unsigned char *data, std::size_t size);

// Separate worlds (later): a send to a player in another world will need an in-transit record and an
// acknowledgement. Until then SendItem moves the item in one step, and this is where that goes.

// coop-lobbyslots (Jep, 2026-11-01 23:57Z): each bag is a lobby profile's. The save slot's coop-bags.json (beside
// coop-p2.dat) says whose each player's bag is and keeps the bags of profiles not playing. At the lobby's start (the host:
// coop_lobby's fade out, after the picks) player 1 takes p1's profile's bag and player 2 p2's ("" none), an empty one for
// a profile with none; a bag nobody takes is kept. Story items and gate keys stay in player 1's pack. new_game: both
// bags are empty, the owners are named. Logs "coop-inv: profile bags: ...". Separate bags only (OwnBags).
void ProfileBagsStart(const std::string &p1, const std::string &p2, bool new_game);
// From then on every slot load gives the seated players their profiles' bags again (the loaded slot's owners may be others);
// a new lobby forgets the seats.
void ProfileBagsSeatsClear();
// Both players' bags as they are now, on stderr ("coop-inv: bags now (why): P1 N carried G gilda, P2 ..."): a row's check.
// coop-worldkeys: and where the keys are ("coop-inv: keys now (why): ring R, P1's bag K, P2's bag J, the game finds I: yes").
void ProfileBagsSay(const char *why);
// coop-joinlobby: a joiner's profile bag at a join into a game in progress: the host reads it as the catch-up's last blob is
// made (false: bags off), both copies give it to player 2 at the lockstep (the host files the old one: never dropped).
bool ProfileBagsJoinBag(const std::string &name, std::vector<unsigned char> &out);
void ProfileBagsJoinTake(const std::string &name, const std::vector<unsigned char> &bytes, bool host);

extern P2Bag     g_bag;
extern ITEM_PACK g_key_ring; // coop-worldkeys: the party's key ring (103 slots, num 103)

} // namespace coop

// The save slot's sidecar (saves/<N>/coop-p2.dat): written beside the save, read as it loads, removed
// with it. A slot without the file loads with an empty bag. memorycardaccess.cpp calls them beside the
// quests' (quest_host.hpp).
bool CoopInventorySave(const std::filesystem::path &save_directory);
void CoopInventoryLoad(const std::filesystem::path &save_directory);
void CoopInventoryDelete(const std::filesystem::path &save_directory);
void CoopInventoryNewGame();

// Stealing from a monster (a weapon's steal attribute, MONSTOR.steal_item): retail flies the item to
// the player, who gets it. While the bags are separate, an item player 2's hit steals flies to the
// partner, and a bag item goes into player 2's bag (attachments stay the party's). The flying slot's
// spare aux_state (retail resets it to -1 and never reads it) records who stole it: 2 for player 2.
// The combat thread's monster damage (coop_combat_damage.cpp) calls these in place of retail's room
// check and StealItem.Set. CStealItem::Step and ::checkEvent are overridden in coop_inventory.cpp.
namespace coop {
int  StealRoomCheck(int owner, int item);
void StealSet(float *position, int item, int owner);
} // namespace coop

// Test trace (DC_COOP_SLOTTRACE): each quick-use slot change of either player (player 1's itemNowSel, player 2's
// own slot), with the pads this tick, from MenuTick. Jep's 2026-10-20: P2's stick moved her slot.
namespace coop {
void SlotTrace();
void VpadTick(); // coop_vpad.cpp: the test's virtual gamepads (DC_TEST_VPAD)
} // namespace coop
