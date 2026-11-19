#pragma once

#include "coop/game_ns.hpp"

// The co-op menus' glue (always on in co-op; coopoptions, Jep 11-17, retired game.coop_menus and game.coop_menu_panel):
// each player's own game menu opens beside the running world on Triangle (coop_fullmenu.hpp), Start is each player's own
// pause (coop_pause.hpp); this module steps the town's walk handover, player 2's prompt of a shared scene, the floor she left,
// and the test hooks, and keeps their state in the link's checksum.

#include <cstdint>

#include "platform/input.hpp"

namespace dc::game {
struct PAD_STATUS;
} // namespace dc::game

namespace coop {

// co-op on (Enabled): the co-op menus run.
bool MenusOn();

// From pad_button_read once a tick (port 0, after the link's exchange): steps the co-op menus' glue. Then for
// every port, MenuFilterPort takes from the game's pad what the co-op menus own (prompts, pickers, the pause).
void MenuTick();
void MenuFilterPort(int port, PAD_STATUS *status);

// From Walk: pad 2 as the partner reads it. While player 2 is in her pause or a gate's question, returns true (the
// partner stands, combat is cancelled) and pad is released; else Triangle is taken out of pad.
bool MenuHoldsPartner(InputPadState &pad);

// From CoopStep after the partner is loaded: true while player 2 has left the floor (the partner is
// not stepped or drawn). Puts her back beside player 1 the tick she rejoins.
bool MenuPartnerAway();

// From where player 2's Cross acts (coop_combat.cpp, Interact's result): a script she started as the game's task (a shared
// story scene). Its windows are hers until play is back under control: port 1 reads pad 2. Nothing is drawn for it (coop-choosing:
// every copy sees the scene itself). Returns started.
bool MenuNotePrompt(bool started, int task_before = -1); // (task_before: gameTask before her Cross acted)
int  MenuTaskNow(); // coop-pt1109c: gameTask

// Player 2 leaves the floor (or comes back): the one place a later "separate worlds" split replaces.
void MenuLeaveFloor(bool leave);

// A town's walk with the menus on (the menu presses' test hook counts from it in a town, as from free play on a floor).
bool MenuTownWalk();

// From MoveChara on a floor under control: Triangle opens the game's menu (true: open it).
bool MenuOpensGameMenu();
// coop-soakmenu: from MoveChara's first-person view (GAME_TASK_EYE_CAMERA) on his Triangle: true when it opened his local menu
// (linked), as MenuOpensGameMenu does in play; false: retail's opening (the menu task).
bool MenuEyeOpensLocal();
// coop-bagoverflow: player 1's overflow_flag as the walk loop finds it (movechara, recipe 0036): 0 the game's own opening (co-op
// menus off), 1 his menu is open beside the world and holds it (retail's throw-away page keeps the flag set while it is up, and the
// world runs on here: the loop would open the menu again under it), 2 his local menu opened for it, as his Triangle opens it.
int MenuOverflowOpens();

// Shops (port/src/shop.cpp): as one opens, while player 2 walks the town, her
// purse and bag go in place of the party's (the shop charges her and fills her bag); as it closes they
// go back, and the walk goes back to player 1.
void MenuShopBegin();
void MenuShopEnd();

// DC_COOP_MENUDEBUG: the shop's talk, side, cursor and the money it sees, each tenth tick of a shop.
void MenuShopTrace(int talk, int side, int cursor, int money, int done);

// The player who has the game's menu (1 or 2): the link's menu owner, else 1.

// Drawing (DrawDungeon after the HUD, and the town's draw): the pauses and the town's walk banner, and over the game's own
// menu on the copy whose player did not open it, a cover saying who is in the menu (twins only now).
void MenuDraw();
void MenuDrawGameMenuCover();

// coop-fullmenu: with separate bags, swaps player 2's bag and gilda into the party status and back (each call
// swaps), so the game's menu she opened shows and changes her own. False when there is nothing to swap.
bool MenuSwapP2Bag();
// coop-localmenu 3c: player 1's hand (member chara's equipped weapon) loaded afresh from the weapon folder.
bool MenuReloadPlayerWeapon(int chara);
// coop-itemsend: whether it has player 2's bag in the party status now (an odd number of swaps).
bool MenuP2BagInPack();
// coop-016 (option A): this copy's own pad (port 0, the game's), for her menu kept open apart with no agreed pads.
InputPadState MenuOwnPad();

// The co-op menus' state for the link's checksum (0 out of co-op).
std::uint32_t MenuHash();

// Session reset (CoopResetForSession): player 2 back on the floor, the walk player 1's.
void MenuResetForSession();

// coop-attachhelp (Jep's pack 8 playtest, 11-04: "the description box in the attachments window isn't loading"): whether
// the ring's draw (main_ring.cpp BattleMenuDraw) shows the game's own help window, CommonMenuMes2 in HelpWinHead's box, for
// the menu state flag: the Allies, Weapons, Items and Atla pages, as retail does (Chronicle e719e2d8 restored it after
// the commit we pin; before, those pages chose their description and nothing drew it). DC_COOP_MUTATE=menuhelp_off: false
// (twin, the pinned look). MenuGameHelpDrawn: after the draw, a line each time the window shows another message or weapon mode.
bool MenuGameHelp(int flag);
void MenuGameHelpDrawn(int flag, int message);

} // namespace coop

// coop_combat.cpp: player 2's own item use and weapon equip.
namespace coop {
bool MenuPartnerUseItem(int item);
void MenuPartnerEquip(int slot);
} // namespace coop
