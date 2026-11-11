#include "coop/game_ns.hpp"
#include "coop/copies.hpp"

#include <SDL3/SDL.h>

#include <cstdio>

#include "platform/config.hpp"
#include "platform/window.hpp"

// Two copies of the game side by side, each played with its own controller: input.controller binds a
// copy's player to the first or second controller Kazarov's input opened (in connection order, after
// SDL's ignored devices), and the window takes the title and place game.window_title and
// video.window_left/top give it. The launcher sets SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS so the copy
// whose window is not in front still reads its controller.

int CopiesPadSource(int port) {
    int bound = dc::platform::ConfigGet().controller;

    if (bound == 0 || port < 0 || port > 1) {
        return port;
    }

    // The bound controller is the player's pad; the other is left to the game's second port, which a
    // copy without co-op does not read.
    return port == 0 ? bound - 1 : 2 - bound;
}

void CopiesApplyWindow() {
    static bool applied = false;

    if (applied) {
        return;
    }

    applied = true;

    const dc::platform::Config &config = dc::platform::ConfigGet();
    SDL_Window   *window = WindowHandle();

    if (window != nullptr && !config.window_title.empty()) {
        SDL_SetWindowTitle(window, config.window_title.c_str());
    }

    if (window != nullptr && config.window_left >= 0 && config.window_top >= 0) {
        SDL_SetWindowPosition(window, config.window_left, config.window_top);
    }

    if (config.controller != 0) {
        std::fprintf(stderr, "copies: this copy's player reads controller %d only%s\n", config.controller,
                     SDL_GetHintBoolean(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, false) ? ", also while the window is behind" : "");
    }
}
