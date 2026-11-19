#pragma once

#include "coop/game_ns.hpp"

// Per-player inventories (always in co-op since coopoptions retired game.coop_own_bags): player 2 carries an
// item bag and a purse of gilda of their own. Player 1 keeps the game's own (UserStatus's item pack and
// money), untouched; storage, weapons, attachments, Atla and story progress stay the party's.
//
// Player 2's bag has the game's ITEM_PACK layout (the same number of slots player 1's pack has when it
// is seeded), the quick-use slots' uses left beside it as the game keeps player 1's, and its gilda as a
// u16 like UserStatus.money. A new game seeds it empty with no gilda; a save keeps it in a file beside
// the slot (saves/<N>/coop-p2.dat), never inside the game's save, so every save still loads in a game
// without co-op.
//
// Story items (the gate keys and the dungeons' key and quest items) are the world's, not a player's: they live in the
// party's key ring (coop-worldkeys, RingOn), outside both bags, and cannot be sent.
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
    // coop-bagoverflow: retail's CUserStatus::overflow_flag for this bag: set by a give past the room (CDngStatusData::GetItem,
    // the item in the three slots past pack.num), it opens its player's menu on the throw-away page as retail's walk loop opens
    // his (movechara); her menu has it in the status while her bag is there (MenuSwapP2Bag), where the menu writes it.
    std::int32_t  overflow = 0;
};

// coop-bags: each tick (the dungeon's and the town's step): the party's bag size to both bags, and the bag-size test hooks.
void BagSizeStep();
void BagSizeRaised(int size); // EdAddMaxItem's upgrade (the global CoopBagSizeRaised, recipe 0068): every bag takes it
void BagSizeNow();             // after a change made elsewhere (apart header apply, a load, a join's bag): the largest size to every bag
void BagSizesSay(const char *label);

// Whether the players have a bag each: co-op on (game.coop).
bool OwnBags();
// coop-eventslots: the player (1 up) whose own bag P2Bag keeps (the game's pack is player 1's); one more bag a player is later work.
int OwnBagPlayer();

// coop-eventslots (Jep 11-11), coop-fishpools (Jep 11-17): each player's own fishing record always (the points, the log, Mardan
// Garayan's count), kept with their profile in coop-bags.json; game.coop_fishing_shared picks what the save's record (the one the
// game's fishing, exchange and log use) shows: the total of every player in the game (on, the default: a spend comes off every
// player equally, a catch is the catcher's own), or each player's own (off, with own bags; DC_COOP_FISHING_PER_PLAYER=1 for the
// tests). See coop_inventory.cpp's coop-fishpools block.
bool          FishingPerPlayer();
bool          FishPoolsOn();                 // the records are the players' (game.coop and own bags)
std::string   FishLogWho(int rank);          // coop-fishing3: the shown fishing log's entry's catcher, by profile name ("" none)
void          FishLogNamesDraw(int board_x, int board_y, int alpha, int top, int cursor); // the log board's names and fish (shop.cpp)
bool          FishLogColumnsOn();            // the log board is RANK | NAME | FISH | SIZE of FISH (the records are the players')
// coop-fishing3: the fishing log board's columns (Jep 11-18: RANK | NAME | FISH | SIZE of FISH), from the board's left edge, in the
// game's 640 x 448 screen; the board's own art (FishMenuTex) for every header: RANK, NAME, and FISH made of the SIZE of FISH button's
// left cap and its FISH end.
namespace fishlog {
constexpr int kBoardW = 0x160, kBoardH = 0xD0;
constexpr int kHeadW = 0x44, kSizeHeadW = 0x74;                         // the RANK and NAME buttons' width; SIZE of FISH's
constexpr int kHeadRankX = 0x0C, kHeadNameX = 0x52, kHeadFishX = 0x98, kHeadSizeX = 0xDC;
constexpr int kFishCapW = 6, kFishWordX = 64, kFishWordW = 52;          // in the SIZE of FISH art: its left cap; its "FISH" and right cap
constexpr int kNameX = 0x46, kNameW = 0x52;                             // the player names' column
constexpr int kFishX = 0xA0;                                            // the fish names' column (up to the size number)
constexpr int kSizeX = 0x118, kDigitW = 0xE, kGap = 4;                  // the size's "cm" (its number right-aligned before it)
constexpr int kRowTop = 6 + 0x16, kRowStep = 0x22;                      // a row's text top from the board's top
} // namespace fishlog
void          FishPoolsStep();               // every tick (the town's and a room's step): the save's record made again for now
void          FishPoolsReset();              // a new game or a slot's load: no records yet
void          FishRecordSwap(int index);     // around player index's activity (in, then out): the save's record theirs, what changed theirs
int           FishPointsOf(int index);       // player index's own points
std::uint32_t FishPoolsHash();               // the records and the view, for the sync check
void          FishPoolsTake(std::vector<unsigned char> &out);       // ... and carried by a resync (netplay.cpp kCarried)
void          FishPoolsPut(const unsigned char *data, std::size_t size);
void          FishExchangeOpens(int point);  // the exchange's open (shop.cpp): the points it shows
int           FishExchangeOpenPoint();       // ... and at its close, what it showed
bool          FishBuyByCommand();            // coop-fishpools2: the exchange's buys go by lockstep claims (linked, together)
void          FishBuyClaim(int item, int price);
bool          FishBuyWaiting();              // this copy's claim not landed yet
int           FishBuyLanded(int *item);      // once: 1 granted, -1 refused, 0 nothing
void          FishExchangeLive(int *point);  // the exchange's shown points as they stand (claims spend them)
void          FishBuyCommandHandler();       // kCmdFishBuy
int           FishSlotRead(int which, int index, std::uint8_t *out); // the apart sync's fishing slots (coop_worlds.cpp)
void          FishSlotWrite(int which, int index, const std::uint8_t *in);
void          FishTestSeed();                // DC_COOP_TEST_FISHING_POINTS_P2=N: player 2's own points
void ProfileBagsFileSelfTest(const char *directory); // DC_COOP_TEST_BAGS_FILE=<a scratch folder>: the bags' file by player (candreds2)
// save6 (Jep 11-12: the co-op save holds 6 players now). A slot's co-op files written by an older release (v0.1.0/v0.1.1: no
// players' section) are copied once beside them as <file>.v0.1.1-backup before this release's first write into the slot
// (CoopInventorySave, CoopPartySave); a backup already there is never written over. Rename one back to restore it by hand.
constexpr const char *kSlotBackupSuffix = ".v0.1.1-backup";
bool CoopSlotOldLayout(const std::filesystem::path &save_directory);
void CoopSlotKeepOldLayout(const std::filesystem::path &save_directory);
void Save6SelfTest(const char *directory); // DC_COOP_TEST_SAVE6=<a scratch folder>: the slot's files for 6 players (x-eventslots save6)

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
// coop-bagoverflow: a player's own bag's overflow flag (player 2 up; player 1's is the party status's own): set after a give
// past its room, pending until that player's menu opens (taken there, as movechara takes player 1's).
void PartnerOverflowSet(int player);
bool PartnerOverflowPending(int player);
void PartnerOverflowTake(int player);

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
// Around a resync's state take: before (true) keeps this copy's ring for the twin; after (false) says the ring under the
// test seed (coop-ringguard).
void RingStateTake(bool before);
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
// coop-sendtarget: with three players or more BagScreenSend opens the send list (the other players by profile name, a full bag
// greyed) in the Items page's help box instead of sending. While it is open the item menu's idle step hands its presses here
// first: BagScreenTargetsStep(pressed, &sound) gives 0 when no list is open (retail's keys run), 1 when it took the press, 2 when
// the press sent the item; sound is the menu sound to play (-1 none). BagScreenTargetsOpen: the help box shows the list (the draw
// marks its choice rows). BagScreenTargetsClose: a menu starting shuts any list left open. Logs "coop-inv: send list ...".
// DC_COOP_MUTATE=sendtarget_first (twin): no list; sent at once to the first other player.
constexpr int kTargetsUp = 1, kTargetsDown = 2, kTargetsPick = 4, kTargetsCancel = 8;
int  BagScreenTargetsStep(int pressed, int *sound);
bool BagScreenTargetsOpen();
// The help text's left edge for retail's (the draw's 0x4A): kTargetsTextShift right while the list shows, so retail's hand at its
// rows stands inside the help box. DC_COOP_MUTATE=sendtarget_hand (twin): retail's edge, the hand left of the box.
constexpr int kTargetsTextShift = 22;
int BagScreenHelpTextX(int retail_x);
void BagScreenTargetsClose(const char *why);
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
// them, and a selection of its own). Pad 2's D-pad Left and Right move
// the selection and Square uses the selected slot's item (CombatInput calls PartnerSlotItem with the
// buttons pressed this tick; it gives the item, or -1 for an empty slot). SpendFromSlot spends one
// from the selected slot when it holds the item. PartnerSlotAssign (the panel's Bag page, Square) puts
// every copy of an item into the selected slot, what was there going back into the bag.
int  PartnerSlotItem(std::uint16_t pressed);
void InventoryTestQuick();  // coop-p2controls: DC_COOP_TEST_QUICK (the party's quick-use slots)
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
// coop-debuggate: also on while game.debug_mode is, the session's value (the host's for a joiner; DransCheatNoteOwnDebug is NetplayHelloAdopt's
// note of the joiner's own, for the twin DC_COOP_MUTATE=dranscheat_own that reads each copy's own file).
bool DransCheatOn();
void DransCheatNoteOwnDebug(bool on);
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
// candreds2: players by player index (players[0] is player 1's profile), not a fixed pair.
void ProfileBagsStart(const std::vector<std::string> &players, bool new_game);
// From then on every slot load gives the seated players their profiles' bags again (the loaded slot's owners may be others);
// a new lobby forgets the seats.
void ProfileBagsSeatsClear();
// Both players' bags as they are now, on stderr ("coop-inv: bags now (why): P1 N carried G gilda, P2 ..."): a row's check.
// coop-worldkeys: and where the keys are ("coop-inv: keys now (why): ring R, P1's bag K, P2's bag J, the game finds I: yes").
void ProfileBagsSay(const char *why);
// coop-joinlobby: a joiner's profile bag at a join into a game in progress: the host reads it as the catch-up's last blob is
// made (false: bags off), both copies give it to the joiner's seat at the lockstep (the host files the old one: never
// dropped). player: the joiner's player number (1-based; candreds2: by player, not a fixed player 2).
bool ProfileBagsJoinBag(int player, const std::string &name, std::vector<unsigned char> &out);
void ProfileBagsJoinTake(int player, const std::string &name, const std::vector<unsigned char> &bytes, bool host);

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
