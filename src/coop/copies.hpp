#pragma once

#include "coop/game_ns.hpp"

// Which of Kazarov's input pads the game's controller port reads: port itself, unless input.controller
// binds this copy's player to one controller (port/src/coop/copies.cpp).
int CopiesPadSource(int port);

// Gives the window game.window_title and moves it to video.window_left/top, once, and logs the
// controller binding. pad_button_read calls it.
void CopiesApplyWindow();
