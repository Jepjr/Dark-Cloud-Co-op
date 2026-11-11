#include "coop/game_ns.hpp"
#include "coop/coop_georama.hpp"

#include <chrono>
#include <cstdint>
#include <cstdio>

#include "text/caption_text.hpp"
#include "coop/coop.hpp"
#include "coop/coop_core.hpp"
#include "coop/copies.hpp"
#include "coop/netplay.hpp"
#include "editmapscript.hpp"
#include "menu_draw.hpp"
#include "platform/config.hpp"
#include "platform/input.hpp"
#include "snd.hpp"

namespace dc::game {
extern s32           mode;
extern int           EdInteriorFlag;
extern sceVu0FVECTOR NowCursorPos;
} // namespace dc::game

namespace {

// GAME_MODE_EDIT (main.hpp, whose enum shares the name of the editor's GameMode).
constexpr int kTownMode = 2;

struct Turn {
    int           holder = 0;
    bool          pass_held[2] = {false, false};
    std::uint16_t held = 0;
};

Turn g_turn;

// What a player's controller holds this tick: the pad the linked copies agreed on, else the one
// pad_button_read would read for that port.
InputPadState Pad(int player) {
    InputPadState linked;

    if (NetplayPad(player, linked)) {
        return linked;
    }

    return InputGetPad(CopiesPadSource(player));
}

// Build mode, its parts menu and its pause menu, in a town (not a house, whose modes are its own).
bool Building() {
    if (!dc::platform::ConfigGet().coop || mode != kTownMode || EdInteriorFlag != 0) {
        return false;
    }

    return GameMode == ED_MODE_GEORAMA || GameMode == ED_MODE_GEORAMA_MENU_INIT || GameMode == ED_MODE_GEORAMA_MENU ||
           GameMode == ED_MODE_RETURN_MENU_GEORAMA;
}

} // namespace

void CoopGeoramaTick() {
    InputPadState pads[2] = {Pad(0), Pad(1)};
    bool          pass[2];

    // R3 with all four shoulders is the debug toggle (GameCheckDebugToggle), not a pass.
    constexpr std::uint16_t kShoulders = kInputL1 | kInputR1 | kInputL2 | kInputR2;

    for (int i = 0; i < 2; i++) {
        pass[i] = pads[i].connected && (pads[i].buttons & kInputR3) != 0 && (pads[i].buttons & kShoulders) != kShoulders;
    }

    if (!Building() || !pads[g_turn.holder].connected) {
        if (g_turn.holder != 0) {
            std::fprintf(stderr, "coop: georama editor back to player 1 (%s)\n",
                         Building() ? "player 2's controller is gone" : "build mode left");
        }

        g_turn.holder = 0;
        g_turn.held = 0;
    } else if (GameMode == ED_MODE_GEORAMA && pass[g_turn.holder] && !g_turn.pass_held[g_turn.holder] &&
               pads[1 - g_turn.holder].connected && (NetplayLocalPlayer() == 0 || coop::Mutated("townpause_shared"))) {
        // Linked, player 1's Georama is his own (player 2 walks on and never sees it), so R3 passes it only on one
        // screen (coop-town, CoopTownDrawnMode).
        // Only at the cursor, not in a menu: the menu the holder opened is theirs to close.
        g_turn.holder = 1 - g_turn.holder;
        g_turn.held = pads[g_turn.holder].buttons;
        SndSePlay(MENU_SOUND_CONFIRM, -1, 0);
        std::fprintf(stderr, "coop: georama editor to player %d\n", g_turn.holder + 1);
    } else {
        g_turn.held &= pads[g_turn.holder].buttons;
    }

    g_turn.pass_held[0] = pass[0];
    g_turn.pass_held[1] = pass[1];

    // DC_COOP_DEBUG: who builds, the editor's mode and where the cursor is, every tenth pad read, for
    // the headless tests.
    static const bool debug = CoopDebug();
    static int        reads = 0;

    if (debug && Building() && ++reads % 10 == 0) {
        std::fprintf(stderr, "coop-debug: georama read %d player %d mode %d cursor (%.1f %.1f %.1f)\n", reads, g_turn.holder + 1,
                     GameMode, NowCursorPos[0], NowCursorPos[1], NowCursorPos[2]);
    }
}

int CoopGeoramaSource(int port) {
    if (g_turn.holder == 0 || port < 0 || port > 1) {
        return port;
    }

    return port == 0 ? 1 : -1;
}

int CoopGeoramaHeld(int port) {
    return port == 0 ? g_turn.held : 0;
}

bool CoopGeoramaPartnerHolds() {
    return g_turn.holder != 0;
}

void CoopGeoramaDraw() {
    // coop-uiflash (DC_COOP_DEBUG): each run of frames this copy draws the game's Georama bar (its parts menu: the town in
    // Georama mode here), so a test can tell that player 1's copy never shows one he did not open.
    if (static const bool trace = CoopDebug(); trace) {
        static std::chrono::steady_clock::time_point last{};
        const auto                                   now = std::chrono::steady_clock::now();

        if (now - last > std::chrono::milliseconds(250)) {
            std::fprintf(stderr, "coop-georama: p%d draws the Georama bar (link tick %lld)\n", NetplayLocalPlayer(), (long long) NetplayLinkTick());
        }
        last = now;
    }

    // coop-town: her own Georama has no turns to show, and linked R3 passes nothing.
    if (!Building() || CoopP2Building() || (NetplayLocalPlayer() != 0 && !coop::Mutated("townpause_shared"))) {
        return;
    }

    static const bool debug = CoopDebug();

    // coop-georamatext (Jep, 2026-10-27): no "R3: pass to 1P/2P" line at all. Both players build at once (coop-p2townpause),
    // so it told the builder to hand over a Georama nobody hands over: on player 1's own Georama, and on her copy's before
    // its link or after a drop (playing as player 1 then, so her screen read "1P building - R3: pass to 2P"). R3 still
    // passes the editor where it ever did (two pads on one unlinked copy); only the line is gone.
    // DC_COOP_MUTATE=georama_handover_caption draws it as before (t-georama-text's twin). DC_COOP_DEBUG: each run of frames
    // the line is left out or drawn, for that test.
    const bool draw = coop::Mutated("georama_handover_caption");

    if (debug) {
        static std::chrono::steady_clock::time_point last{};
        const auto                                   now = std::chrono::steady_clock::now();

        if (now - last > std::chrono::milliseconds(250)) {
            std::fprintf(stderr, "coop-debug: p%d georama hand-over caption %s (holder %d)\n", NetplayLocalPlayer(),
                         draw ? "drawn" : "left out", g_turn.holder + 1);
        }
        last = now;
    }

    if (!draw) {
        return;
    }

    static bool said = false;

    if (debug && !said && NetplayLocalPlayer() != 0) {
        said = true;
        std::fprintf(stderr, "coop-debug: georama caption drawn while linked\n");
    }

    dc::text::caption::LineStyle style;

    style.status_font = true;
    style.edge_width = 1.5f;

    if (g_turn.holder != 0) {
        style.fill[0] = 120;
        style.fill[1] = 200;
    }

    const char *line = g_turn.holder == 0 ? "1P building  -  R3: pass to 2P" : "2P building  -  R3: pass to 1P";

    dc::text::caption::DrawLine(line, 320.0f, 30.0f, 9.0f, 1.0f, 260.0f, style, 1.0f, 1.0f, nullptr);
}

// coop-party: session reset. A resync starts both copies from the host's save; whoever held the
// editor goes back to player 1, as at the start of a session.
void CoopGeoramaResetForSession() {
    g_turn = Turn{};
}
