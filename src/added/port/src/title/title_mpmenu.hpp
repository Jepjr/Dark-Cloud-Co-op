#pragma once

#include "coop/game_ns.hpp"

#include "title/cursol.hpp"

// coop: the title menu's Multi-player row, between Continue and Option (title_menu.hpp: START, CONTINUE, MULTIPLAYER,
// OPTION, EXIT), in a plain copy and with one client (DC_NETPLAY=3, the LAN pack; netplay.hpp NetplayTitleMenuShown).
// coop-mainmenu (Jep's tree, 2026-10-29 and 30) with coop-lobby's rewire
// (2026-10-30): Cross on it opens Host / Join (and Local under game.lobby_local = separate); Join opens IP / Code / Steam;
// Host and Local open Start / Continue (the menu's New Game and Load), whose save opens the lobby (coop_lobby.hpp). Each level ends
// with a Back row (no level has more than four rows); Circle (the right mouse button; Triangle, the keyboard's Tab, as in
// the address entry) goes back too. Join > IP opens the address entry SELECT opens, Join > Code the code entry,
// Join > Steam its Steam friend search, and closing one comes back to Join's rows. Rows not built yet (split screen,
// more than two windows) show fainter and do nothing. The row and the submenus' rows are the title menu's own: its
// places, hand, pointer rows and letters (title::menu, Jep's 2a, 2026-11-10). DC_TITLE_MP_MUTATE (the test's twins): no_back, Back does
// nothing; no_host_via, Start or Continue does not tell netplay the way picked; all_built, every row does something;
// close_on_load, Back on the load screen Continue opened goes to the main menu; no_back_row, no Back row.

// The row's CCursol::select: retail's spare row, which nothing else selects.
constexpr int TITLE_MENU_MULTIPLAYER = TITLE_MENU_UNK_4;

// Whether the row shows.
bool TitleMpShown();
// Rows of the level up now: the title menu's own on the top level (title::menu::RowCount), 2 to 4 in a submenu.
int TitleMpRows();

// Cross on the row: Host / Join opens.
void TitleMpEnter();
// Whether a submenu is up (it owns the pad and the pointer).
bool TitleMpOpen();
// coop-lobby: the host left the lobby the save's pick opened: the menu comes back on Host's (or Local's) Start / Continue.
void TitleMpFromLobby();
// The submenu a pick started the game from, closed once the title has faded out (it stays up for the pick's flash);
// Continue's stays, for the load screen's Back.
void TitleMpClose();
// One frame of an open submenu. pointer_row: the row the mouse is on (-1: none); confirm and back from the pointer.
// True when a hosting way's Start or Continue was chosen: the title goes on as for New Game or Load (CCursol's
// select is set to it), the game hosting the way picked.
bool TitleMpStep(int pointer_row, bool pointer_confirm, bool pointer_back);
// coop-lobby's lobby jump, a test's only (DC_TITLE_MP_TEST_HOST=start or continue; a player never sets it, and no launcher
// does): the title's Start presses made as soon as each of its steps takes one (the fade in, the logo), then
// Multi-player > Host > Start (or Continue) picked at the menu's first frame, once: a lobby row starts at the lobby, not
// at a press script's fixed frames. TitleMpTestStart: whether this frame's title step takes a Start (false once the pick
// is made, and without the hook). coop-fx51f: =join-code / =join-steam make the guest's pick instead, Multi-player > Join >
// Code (its entry open) or > Steam (its friend list), logged "title-mp: test join: ...". =menu (mp-title-row): the Start
// presses only, the menu left as it opens, logged "title-mp: test: at the menu".
bool TitleMpTestStart();

// Draws an open submenu's rows on the title menu's rows, in its letters: the chosen one at selected_alpha (retail's
// blink), the others at other_alpha, as title::menu::Draw; fade the screen's fade (0x80 none). hint: draw the
// submenu's line under the menu.
void TitleMpDraw(int selected_alpha, int other_alpha, int fade, bool hint);
