#pragma once

#include "coop/game_ns.hpp"

// coop: mouse: a linked copy (game.netplay with game.netplay_controller_only) never takes the mouse.
// Its SDL event filter drops every mouse and keyboard event, so a window that captured the cursor on
// focus could never let it go again: the click and the Escape that release it never arrive. While
// linked, mouse capture is off whatever input.mouse_capture says; solo and couch co-op keep it.
//
// Logs "coop-mouse: capture on focus off|on (...)" when the state it sets changes.
// DC_COOP_MOUSE_TWIN=1 (the test's mutation twin) leaves capture as input.mouse_capture says.

// Hidden over the game window only (2026-10-21): the system cursor is hidden inside the client area and left alone
// outside it. Start-up keeps the cursor where it was ("coop-mouse: cursor kept at x,y"); DC_COOP_MOUSE_MUTATE=corner
// (twin) lets it jump to the window's corner as before.
// Records the cursor's desktop position; call before InputInit.
void CoopMouseBeforeInput();
// Applies the rule over the input settings. Called after each InputInit and InputApplyConfig.
void CoopMouseApply();
