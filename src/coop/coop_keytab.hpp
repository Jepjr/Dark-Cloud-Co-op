#pragma once

#include "coop/game_ns.hpp"

// coop-keytab (Jep's pick of mock 3, 2026-10-30: "a 4th tab for shared keys and story items, after attachments, looking
// exactly like the other tabs"): with co-op on, the item menu's board (battlemenu.cpp's ItemMenuMode) has a fourth page,
// KEYS, after Items, Weapons and Attachments. It lists the party's key ring (SharedItem and FloorKeyItem, each slot of
// ShownRing, in pack order) as icons in the board's cells, the cursor and the item description as on the Items page, and
// nothing moves: Cross, Square and Triangle refuse there and the trash cannot be reached. The ring stays in player 1's
// pack (SharedRing); the page only shows it, so it is the same on both copies and in either player's menu.
// Drawn with retail's own pieces (menu_draw.cpp's PersonalBoardTagDraw): the four tabs are slices of the perbrd tab strip,
// each 0x42 wide, set 0x35 apart (retail 0x41, which leaves no room for a fourth on the board's top band) so they sit
// centred over the board; the fourth is the Attachments tab with its chest covered by the band's stone, row by row, and
// the Silver Key's icon on it (DrawIconParts); its label is the ITEM label's yellow plate stretched round "KEYS" in the
// game's thin font (Jep, 2026-10-31: Silver Key; no overlap from the bar below; KEYS's font). The board's count bubble moves 0x28 right, off
// the fourth tab. L1/R1 page through the four (BoardModeChangeKey), the pointer's tabs too (menu_mouse.cpp).
// Retail's look stays where the tab is off: co-op off, another board (shops, Atla), the menu's throw-away mode, or
// DC_COOP_MUTATE=keytab (twin: three tabs, no Keys page). The weapon menu's Attach board shows the four tabs, the ones
// behind dimmed as retail dims its three, but has no Keys page (coop-attachhelp).

namespace dc::game {
class CTexture;
} // namespace dc::game

namespace coop {

// The Keys page's board page number, past Atla's (BOARD_PAGE_ATLA_VILLAGE 3 to 8), so no retail switch takes it.
constexpr int kKeysTabPage = 9;

// Whether the board being stepped or drawn (PerBoardPt) is the item menu's with the Keys tab.
bool KeysTabOn();
// L1 (-1) or R1 (1) on the item menu's board with the tab on: the next page of the four, true (the caller's retail
// paging is skipped); false with the tab off.
bool KeysTabPage(int step);
// The page's cells (rows of 5, at least the board's 4 rows) and how many keys fill them.
int KeysTabCells();
int KeysTabCount();
// The key in a cell of the page (-1: none).
int KeysTabItemAt(int cell);
// CommonIconDraw's part for the page: the keys' icons in the cells.
void KeysTabIcons(int x, int y, int top, int bottom, int alpha);
// PersonalBoardTagDraw's part with the tab on, or on the weapon Attach board (the tabs behind dimmed): the four tabs, tag's in
// front, and tag's label. False otherwise.
bool KeysTabTagDraw(int tag, int x, int y, CTexture *texture, int alpha);
// How far right the board's count bubble moves (0 where KeysTabTagDraw draws retail's tabs).
int KeysTabBubbleShift();
// The pointer's tabs (menu_mouse.cpp): how many, the page of each, and each one's left edge and width from the board's x.
int KeysTabTabs();
int KeysTabPageOf(int tab);
int KeysTabLeft(int tab);
int KeysTabWidth();
// The page the menu cursor keeps in the save (ExitItemSelect's SetCursorChara): Items for the Keys page, so a save never
// holds a page retail does not have.
int KeysTabSavedPage(int page);

} // namespace coop
