#include "coop/game_ns.hpp"
#include "coop/coop_mouse.hpp"

#include "coop/coop.hpp"

#include <cstdio>
#include <cstdlib>

#include <SDL3/SDL.h>

#include "platform/config.hpp"
#include "platform/mouse.hpp"

namespace {

int g_logged = -1;
// Where the system cursor was before InputInit: its capture on focus (dropped again right after for a linked
// copy) leaves SDL's relative mode, which warps the cursor to the last position SDL saw, 0,0 at start-up.
bool  g_have_start = false;
bool  g_restored = false;
float g_start_x = 0.0f;
float g_start_y = 0.0f;

bool Twin() {
    return TestEnvOn("DC_COOP_MOUSE_TWIN");
}

} // namespace

void CoopMouseBeforeInput() {
    SDL_GetGlobalMouseState(&g_start_x, &g_start_y);
    g_have_start = true;
}

void CoopMouseApply() {
    const dc::platform::Config &config = dc::platform::ConfigGet();
    // The Online launchers link through DC_NETPLAY, not game.netplay (as netplay.cpp's Settings reads it);
    // DC_COOP_MOUSE_ENV_TWIN is the twin that reads only the config.
    int           netplay = TestEnvOn("DC_COOP_MOUSE_ENV_TWIN") ? config.netplay : TestEnvInt("DC_NETPLAY", config.netplay);
    bool          linked = netplay != 0 && config.netplay_controller_only;
    bool          capture = config.mouse_capture;
    if (linked && !Twin()) {
        bool was_captured = MouseCaptured();
        capture = false;
        MouseConfigure(false, {});
        // Hidden only over the window's client area (SDL hides it for its own window); outside it the cursor is
        // the desktop's, and it is never held inside.
        MouseSetPointerCursor(true);
        if (was_captured && g_have_start && !g_restored && !TestEnvIs("DC_COOP_MOUSE_MUTATE", "corner")) {
            SDL_WarpMouseGlobal(g_start_x, g_start_y);
            std::fprintf(stderr, "coop-mouse: cursor kept at %.0f,%.0f\n", g_start_x, g_start_y);
        }
        g_restored = true;
    }
    if (g_logged != (int) capture) {
        g_logged = (int) capture;
        std::fprintf(stderr, "coop-mouse: capture on focus %s (%s)\n", capture ? "on" : "off",
                     linked ? "linked, controllers only" : "not linked: input.mouse_capture");
    }
}
