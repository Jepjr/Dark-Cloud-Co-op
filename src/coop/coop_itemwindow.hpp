#pragma once

// coop-keylook (Jep: a duplicated menu must look like the original, drawn by the game's own code): one Use Item window's drawing
// state, so a co-op player's own key picker is drawn by the game's EventItemSelectDraw (menu_save.cpp, recipe) with its own
// cursor, scroll and fade instead of a hand-made copy. Kept on the copy that draws it (drawing only, no lockstep state).

#include "clsmes.hpp"    // ClsMes
#include "menu_save.hpp" // MINI_MENU_INFO
#include "texture.hpp"   // CTexture

namespace dc::game {

struct ITEM_PACK;

struct EventItemDraw {
    MINI_MENU_INFO menu{};          // the window's MiniMenu (cursor, scroll_row, state, state_time, lang, event_item_num)
    float          board_pos[2]{};  // EventBoardPos
    int            move_y = 0;      // EventItemMoveY (the rows' eased place)
    float          cur[2]{};        // MiniCur (the hand's eased place)
    float          bar_y = 0.0f;    // EventBarY (the scroll bar's eased place)
    CTexture      *board = nullptr; // eventmnu
    CTexture      *icons = nullptr; // itemicon (ItemIcon while it draws)
    ITEM_PACK     *pack = nullptr;  // the items it lists
    ClsMes        *mes = nullptr;   // the item's message window (CommonMenuMes2's place)
    int            block = -1;      // the pictures' texture block
};

// InitEventItemSelect's places for a window at x, y (EventBoardPos, PAL's 20 and the language's lift already in y), listing pack,
// the cursor on slot cursor: fading in, as the game's opens.
void EventItemDrawStart(EventItemDraw &draw, float x, float y, ITEM_PACK *pack, int event_items, int cursor);

// One frame of the game's window for draw: the rows scrolled by the game's rule to keep the cursor in view, the screen behind dimmed
// by the game's fade (live: a black fill over the world going on, as FrameImageDraw dims its still), then EventItemSelectDraw.
void EventItemDrawAs(EventItemDraw &draw, int cursor);

} // namespace dc::game
