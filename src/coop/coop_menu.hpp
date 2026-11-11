#pragma once

#include "coop/game_ns.hpp"

// Per-player menus (game.coop_menus, off by default; needs game.coop): each player has a menu overlay
// of their own that pauses nobody, and the game's own menu belongs to whoever opened it.
//
// Buttons on a dungeon floor while the two play together:
//   Triangle  opens or closes that player's overlay. The game goes on: the other player keeps moving
//             and fighting, the task stays play. The owner's character stands still while it is open.
//   Start     opens the game's own menu (retail's Triangle menu), which pauses both as it always has.
//             On linked copies it is drawn only on the copy of the player who opened it; the other
//             copy shows "<name> is in the menu".
//   The overlay's last page (System) has the plain pause, and player 2's has Leave floor / Rejoin.
// In a town player 2's Triangle opens her overlay too (player 1 keeps the town's own menus).
//
// Pages (Left and Right turn them, Up and Down move, Cross acts, Triangle or Circle closes):
//   Items   the player's own bag (player 2's while game.coop_own_bags is on, else the party's pack):
//           Cross uses the item (food on oneself, Repair and Power Up powder on one's own equipped
//           weapon); player 2's Square puts it in her quick-use slot.
//   Equip   the player's character's weapons: Cross equips.
//   Status  life, water, defence, the weapon's attack and durability, gilda.
//   Send    (own bags) Cross sends one of the item to the other player (coop_inventory's SendItem).
//   System  Pause game; player 2 also Leave floor (her character sits out, player 1 plays on) and
//           Rejoin (or the next floor brings her back).
//
// The overlays move only with the two pads the copies agree on and the game's state, at the pad read
// that starts each tick, so linked copies keep them alike; their state is in the link's checksum.
// Each copy draws only its own player's overlay (couch: both, each in its player's corner).

#include <cstdint>

#include "platform/input.hpp"

namespace dc::game {
struct PAD_STATUS;
} // namespace dc::game

namespace coop {

// game.coop and game.coop_menus.
bool MenusOn();

// From pad_button_read once a tick (port 0, after the link's exchange): steps both overlays. Then for
// every port, MenuFilterPort takes from the game's pad what the overlays own: everything while player
// 1's overlay is open, Triangle while the two play on a floor.
void MenuTick();
void MenuFilterPort(int port, PAD_STATUS *status);

// From Walk: pad 2 as the partner reads it. While player 2's overlay is open, returns true (the partner
// stands, combat is cancelled) and pad is released; else Triangle is taken out of pad.
bool MenuHoldsPartner(InputPadState &pad);

// From CoopStep after the partner is loaded: true while player 2 has left the floor (the partner is
// not stepped or drawn). Puts her back beside player 1 the tick she rejoins.
bool MenuPartnerAway();

// From where player 2's Cross acts (coop_combat.cpp, Interact's result): a script or chest she started.
// Its prompts are hers until play is back under control: port 1 reads pad 2, and on a linked player 1's
// copy a cover says "<name> is choosing...". Returns started.
bool MenuNotePrompt(bool started, int task_before = -1); // (task_before: gameTask before her Cross acted)
int  MenuTaskNow(); // coop-pt1109c: gameTask
void MenuDrawPromptCover();

// Player 2 leaves the floor (or comes back): the one place a later "separate worlds" split replaces.
void MenuLeaveFloor(bool leave);

// game.coop_menu_panel (off by default): the overlay on Triangle. Off, the overlays never open and
// Triangle opens the game's menu (coop_fullmenu.hpp) for each player; Start is the pause.
bool MenuPanelOn();

// A town's walk with the menus on (the menu presses' test hook counts from it in a town, as from free play on a floor).
bool MenuTownWalk();

// From MoveChara on a floor under control: Triangle (Start with the overlay on) opens the game's menu
// (true: open it), and the System page's pause (true once: pause).
bool MenuOpensGameMenu();
bool MenuTakePause();

// Shops (port/src/shop.cpp): as one opens, while player 2 walks the town with game.coop_own_bags on, her
// purse and bag go in place of the party's (the shop charges her and fills her bag); as it closes they
// go back, and the walk goes back to player 1.
void MenuShopBegin();
void MenuShopEnd();

// DC_COOP_MENUDEBUG: the shop's talk, side, cursor and the money it sees, each tenth tick of a shop.
void MenuShopTrace(int talk, int side, int cursor, int money, int done);

// The player who has the game's menu (1 or 2): the link's menu owner, else 1.
int MenuGameMenuOwner();

// Drawing: the overlays (DrawDungeon after the HUD, and the town's draw), and over the game's own menu
// on the copy whose player did not open it, a cover saying who is in the menu.
void MenuDraw();
void MenuDrawGameMenuCover();

// coop-fullmenu: with separate bags, swaps player 2's bag and gilda into the party status and back (each call
// swaps), so the game's menu she opened shows and changes her own. False when there is nothing to swap.
bool MenuSwapP2Bag();
// coop-localmenu 3c: player 1's hand (member chara's equipped weapon) loaded afresh from the weapon folder.
bool MenuReloadPlayerWeapon(int chara);
// coop-itemsend: whether it has player 2's bag in the party status now (an odd number of swaps).
bool MenuP2BagInPack();

// The overlays' state for the link's checksum (0 when game.coop_menus is off).
std::uint32_t MenuHash();

// Session reset (CoopResetForSession): both overlays closed, player 2 back on the floor.
void MenuResetForSession();

// Whether a player's overlay is open (1 or 2), for the tests and the HUD.
bool MenuOpen(int player);

// coop-attachhelp (Jep's pack 8 playtest, 11-04: "the description box in the attachments window isn't loading"): whether
// the ring's draw (main_ring.cpp BattleMenuDraw) shows the game's own help window, CommonMenuMes2 in HelpWinHead's box, for
// the menu state flag: the Allies, Weapons, Items and Atla pages, as retail does (Chronicle e719e2d8 restored it after
// the commit we pin; before, those pages chose their description and nothing drew it). DC_COOP_MUTATE=menuhelp_off: false
// (twin, the pinned look). MenuGameHelpDrawn: after the draw, a line each time the window shows another message or weapon mode.
bool MenuGameHelp(int flag);
void MenuGameHelpDrawn(int flag, int message);

} // namespace coop

// coop_combat.cpp: player 2's own item use and weapon equip, as its panel does them.
namespace coop {
bool MenuPartnerUseItem(int item);
void MenuPartnerEquip(int slot);
} // namespace coop
