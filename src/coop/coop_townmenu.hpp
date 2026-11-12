#pragma once

#include "coop/game_ns.hpp"

#include <cstddef>

// coop-town: the game's menu in a linked town, each player's own; see coop_townmenu.cpp.

// From the town's walk as it opens its menu (editloop): which menu (ED_MENU_*).
void CoopTownNoteMenu(int menu_no);

// Player 1's battle menu is open in the linked town (player 2 walks on through it).
bool CoopTownP1Menu();
// coop-localmenu: whether this copy runs that player's own town menu now (what its open and close commands report).
bool CoopTownMenuRunsFor(int player);
// coop-stage4 (local menus stage 4): whether this copy runs that player's town menu as a local one (its engine here alone; the
// other copies know it by the in-menu bit). Her closed local menu's map jump for the close command (jump + 1, 0 for none), and
// that command applied on every copy on its link tick.
bool CoopTownMenuRunsLocalHere(int player);
int  CoopTownMenuCloseJump(int player);
void CoopTownMenuClosedByCommand(int player, int code, long long tick);
void CoopTownMenuQuestShopTick(); // coop-pt1111e: each tick, every copy: the open quest shop's first good, for its close's line
// coop-stage4 4b: his battle menu from his walk (editloop, edit_in) opens local (true: the town stays in its walk; his bit on
// every copy, the engine on his); its close command's map jump for the town's walk to take (-1 none), and indoors the room's way
// out (true: MenuMapJumpMode and the room's out flag set as retail's close leaves them). Whether his local menu is open (the bit).
bool CoopTownP1MenuOpensLocal(int menu_no);
bool CoopTownP1MenuLocalOn(); // coop-stage4: his battle menu opens local (linked, local menus on)
bool CoopTownHerShopOpens(int kind); // coop-p1parts: her keeper's talk ended on every copy: her own shop (ED_MENU_*) next; false: not hers (fix off)
bool CoopTownMenuLinked();    // coop-p1parts: this copy steps the linked town (together, or a catch-up's alike ticks)
bool CoopTownP1MenuRunsLocal(int menu_no); // coop-p1parts: the menu retail would open for him (ED_MENU_*) runs on his copy alone
int  CoopTownP1MenuTakeJump();
bool CoopTownP1MenuTakeRoomOut();
bool CoopTownP1MenuLocal();
// coop-stage4 4c: this copy's player has retail's battle menu open apart, which a merge carries (no gate holds the merge for it); as the
// merge's link starts the sender's goes on as a local menu, the taker's parks and opens again once landed.
bool CoopTownMenuCarries();
void CoopTownMenuRetailToLocal(int player);
void CoopTownMenuPark(int player);
// coop-soakfix5: a resync that loads the town again (not the sender's own checkpoint take) parks a menu carried through the merge.
void CoopTownMenuParkCarriedForResync();
// coop-inbuild: inside a building, player 2's copy draws the room under his menu (his drawn out of sight), as in town.
bool CoopTownDrawsRoomUnderMenu();
bool CoopTownRoomMenuProjection(); // coop-roommenu: the room's projection again after his menu's draw (her copy)

// Around the town's menu step (EdMenuMode, which draws his menu): her copy draws it where nobody sees it.
void CoopTownMenuStepBegin();
void CoopTownMenuStepEnd();

// Player 2's own menu: whether it is open; its step (after the town's mode step) and its draw (after the town's
// frame, beside coop::MenuDraw); session reset.
bool CoopP2TownMenuOn();
void CoopP2TownMenuTick();
void CoopP2TownMenuDraw();
void CoopP2TownMenuResetForSession();

// coop-townmenus: her menu open, and not yet half a second (his Triangle waits for that, or for good with the twin
// townmenuboth_off); her menu's draw in progress (BattleMenuDraw leaves out its still frame: hers is over the live town).
bool CoopP2TownMenuBlocksP1();
bool CoopTownRoomShopTestOpen(); // coop-stage4 r6: test hook DC_COOP_TEST_TOWN_RETAILMENU's shop in a room, once
// coop-menuanim: his battle menu over the live town (linked, player 2 in the town): its step leaves out its draw, which
// comes after the town's frame (EditMainDraw, beside hers; dimmed on his copy, out of sight on hers); its draw in progress.
bool CoopTownP1MenuLive();
// coop-menuflash: his live menu closed (no map jump): whether its close tick draws the town (DC_COOP_MUTATE=townclose_flash: no).
bool CoopTownMenuCloseFrame();
// coop-clockblink: his menu's opening tick (MENU_INIT) over the live town keeps the clock (DC_COOP_MUTATE=clockblink: no).
bool CoopTownMenuOpenKeepsClock();
// The frame's note (CoopTownDrawnMode, before the town's mode is set to walking for the draw), then the draw.
void CoopTownP1MenuNoteFrame();
void CoopTownP1MenuDraw();
bool CoopTownP1MenuDrawing();
bool CoopP2TownMenuDrawing();
// Her own pause in the linked town (Start): she stands, her copy shows the town's pause.
bool CoopP2TownPaused();
void CoopP2TownPauseForTest(); // coop-mergecam test hook: her pause opened now (DC_COOP_TEST_PAUSE_AT_SPLIT)
// coop-mergecam: her pause with a merge's state set (an extra beside her town place): taken on the sender, put back on the
// taker after the apply.
unsigned char *CoopP2TownPauseBytes(std::size_t *size);
void           CoopP2TownPauseTake();
void           CoopP2TownPauseApply();
// coop-menucross: her menu or her pause has her pad (open, or let go of it on this link tick: the press that closed it
// is the menu's). Her Cross is no door's or event's meanwhile (CoopTownHerPadFree).
bool CoopP2TownMenuHoldsPad();

// The map her menu chose to leave town for as it closed (its "go to the dungeon"), once, else -1 (editloop takes it
// as soon as the town walks).
int CoopP2TownMenuTakeJump();

// coop-saveload (Jep's note 14, 2026-10-25: his save screen answered her pad, she saw it, and he could not load while
// she was in her menu): the ring's Load in her menu in a linked town is refused (the host's game is both copies');
// a load done (from the ring's load monitor) asks the host for the resync that takes both copies to his loaded save;
// that resync closes her menu, with a notice on her copy.
bool CoopMenuLoadAllowed();
void CoopMenuLoaded();
void CoopTownMenuClosedByLoad();
// After the load's resync reset every module: her menu, Georama and pause as they are now (logged; the tests read it).
void CoopTownAfterLoad();
