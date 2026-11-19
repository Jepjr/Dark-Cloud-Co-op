#include "coop/game_ns.hpp"
#include "coop/coop_lightbar.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "coop/coop.hpp"
#include "coop/coop_actor.hpp"
#include "coop/coop_core.hpp"
#include "coop/coop_lobby.hpp"
#include "coop/coop_whereis.hpp"
#include "coop/netplay.hpp"
#include "dun/gameloop.hpp"
#include "main.hpp"
#include "platform/input.hpp"
#include "userstatus.hpp"

namespace coop {
namespace {

constexpr int kPulseTicks = 90;  // one pulse: down and up
constexpr int kPulseLowest = 25; // the dimmest point, percent of the colour

// What retail rests at where no player is shown (the title, the lobby before a seat): dualsense.cpp's own idle blue.
constexpr LightbarRgb kIdle = {0, 48, 160};

struct Shown {
    int         local = -1;
    int         player = -2;
    bool        pulse = false;
    LightbarRgb at_full;
};

Shown g_shown[kInputPadCount];
long  g_tick = 0;

int Brightness(long tick) {
    const int phase = (int) (tick % kPulseTicks);
    const int k = phase < kPulseTicks / 2 ? phase : kPulseTicks - phase; // 0 to 45 and back

    return kPulseLowest + (100 - kPulseLowest) * (kPulseTicks / 2 - k) / (kPulseTicks / 2);
}

bool Logging() {
    return TestEnv("DC_COOP_TEST_LIGHTBAR") != nullptr;
}

// Sets one pad's light bar and, for the test, says what it shows when that changes.
void Show(int pad, int player, bool pulse, LightbarRgb colour, LightbarRgb at_full, long tick) {
    InputSetLightbar(pad, colour.r, colour.g, colour.b);

    if (!Logging() || pad < 0 || pad >= kInputPadCount) {
        return;
    }

    Shown     &was = g_shown[pad];
    const bool changed = was.local != NetplayLocalPlayer() || was.player != player || was.pulse != pulse || std::memcmp(&was.at_full, &at_full, sizeof at_full) != 0;

    if (changed) {
        was.local = NetplayLocalPlayer();
        was.player = player;
        was.pulse = pulse;
        was.at_full = at_full;
        std::fprintf(stderr, "coop-lightbar: copy of p%d: pad %d shows player %d colour %d %d %d %s\n", NetplayLocalPlayer(), pad, player, at_full.r,
                     at_full.g, at_full.b, pulse ? "pulsing" : "steady");
    }

    if (pulse && std::strcmp(TestEnv("DC_COOP_TEST_LIGHTBAR"), "ticks") == 0) {
        std::fprintf(stderr, "coop-lightbar: copy of p%d: pad %d tick %ld sends %d %d %d\n", NetplayLocalPlayer(), pad, tick, colour.r, colour.g, colour.b);
    }
}

void ShowPlayer(int pad, int player, bool in_dungeon, long tick) {
    bool      pulse = false;
    const int chara = ActorChara(player - 1);

    if (in_dungeon && UserStatus != nullptr && chara >= 0 && chara < (int) (sizeof UserStatus->hp / sizeof UserStatus->hp[0])) {
        pulse = LightbarLow(UserStatus->hp[chara], UserStatus->max_hp[chara]);
    }

    Show(pad, player, pulse, LightbarColour(player, pulse, tick), LightbarColour(player, false, 0), tick);
}

// The table, once: every player's colour, and what a whole low-life pulse sends (the six-seat row and the pulse row).
void TableOnce() {
    static bool said = false;

    if (said || !Logging()) {
        return;
    }

    said = true;

    for (int player = 1; player <= kMaxPlayers; player++) {
        const LightbarRgb full = LightbarColour(player, false, 0);
        int               lowest = 100, highest = 0;

        for (long t = 0; t < kPulseTicks; t++) {
            const int b = Brightness(t);

            lowest = b < lowest ? b : lowest;
            highest = b > highest ? b : highest;
        }

        const LightbarRgb dim = LightbarColour(player, true, kPulseTicks / 2);

        std::fprintf(stderr, "coop-lightbar: table player %d colour %d %d %d, low-life brightness %d%% to %d%% (dimmest sends %d %d %d)\n", player, full.r,
                     full.g, full.b, lowest, highest, dim.r, dim.g, dim.b);
    }
}

// DC_COOP_TEST_LIGHTBAR_HURT=<percent> (the low-life row): once the floor runs, player 1's member is hurt to that percent of its
// life, and healed again 180 ticks on (two pulses), so the row reads the pulse and the steady colour after it from one run.
void TestHurt() {
    static const char *hurt = TestEnv("DC_COOP_TEST_LIGHTBAR_HURT");
    static long        since = -1;

    const int chara = ActorChara(0);

    if (hurt == nullptr || UserStatus == nullptr || chara < 0 || chara >= (int) (sizeof UserStatus->hp / sizeof UserStatus->hp[0])) {
        return;
    }

    const int max_hp = UserStatus->max_hp[chara];

    if (since < 0) {
        if (g_tick < 150) {
            return; // (the fixture loads at tick 60 and puts its own life back)
        }

        since = g_tick;
        UserStatus->hp[chara] = (short) (max_hp * std::atoi(hurt) / 100);
        std::fprintf(stderr, "coop-lightbar: test: player 1's member %d hurt to %d of %d\n", chara, UserStatus->hp[chara], max_hp);
    } else if ((g_tick - since) % 10 == 0 && g_tick - since < 180) {
        std::fprintf(stderr, "coop-lightbar: test: +%ld ticks member %d life %d of %d, actor chara %d\n", g_tick - since, chara, UserStatus->hp[chara], max_hp, ActorChara(0));
    } else if (g_tick - since == 180) {
        UserStatus->hp[chara] = (short) max_hp;
        std::fprintf(stderr, "coop-lightbar: test: player 1's member %d healed to %d\n", chara, max_hp);
    }
}

} // namespace

bool LightbarLow(int hp, int max_hp) {
    return max_hp > 0 && (float) hp < 0.3f * (float) max_hp;
}

LightbarRgb LightbarColour(int player, bool pulse, long tick) {
    unsigned char rgb[3];

    WhereColour(player, rgb);

    const int percent = pulse ? Brightness(tick) : 100;

    return {(unsigned char) (rgb[0] * percent / 100), (unsigned char) (rgb[1] * percent / 100), (unsigned char) (rgb[2] * percent / 100)};
}

bool LightbarUpdate() {
    if (!Enabled()) {
        return false;
    }

    const long tick = g_tick++;

    // (the test's hurt and heal run for the twin too, so it has the same life to show)
    if (mode == GAME_MODE_DUNGEON) {
        TestHurt();
    }

    if (Mutated("lightbar_retail")) {
        return false;
    }

    TableOnce();

    // DC_COOP_TEST_LIGHTBAR_SEATS=1 (the six-seat row, no real pads): six seats' pads 0 to 5 play players 6 down to 1, so a colour
    // that followed the pad number would show; each is asked for through the colour a pad shows its player (once).
    static bool seats_said = false;

    if (!seats_said && TestEnv("DC_COOP_TEST_LIGHTBAR_SEATS") != nullptr) {
        seats_said = true;

        for (int pad = 0; pad < kMaxPlayers; pad++) {
            const int         player = kMaxPlayers - pad;
            const LightbarRgb c = LightbarColour(player, false, 0);

            std::fprintf(stderr, "coop-lightbar: test seats: pad %d shows player %d colour %d %d %d\n", pad, player, c.r, c.g, c.b);
        }
    }

    const int local = NetplayLocalPlayer();

    // The lobby: a taken seat's controller shows the seat's player; the others rest at idle blue.
    if (lobby::Active()) {
        for (int pad = 0; pad < kInputPadCount; pad++) {
            const int seat = lobby::SeatOfPad(pad);

            if (seat >= 0) {
                ShowPlayer(pad, seat + 1, false, tick);
            } else {
                Show(pad, 0, false, kIdle, kIdle, tick);
            }
        }

        return true;
    }

    const bool in_dungeon = mode == GAME_MODE_DUNGEON;

    if (!in_dungeon && mode != GAME_MODE_EDIT && mode != GAME_MODE_LOADER) {
        Show(0, 0, false, kIdle, kIdle, tick);
        return true;
    }

    if (local > 0) {
        // Linked: this copy's own player, on the controller it plays (the one in hand when it is a lone player's).
        const int pad = NetplayOwnPad();

        if (pad >= 0) {
            ShowPlayer(pad, local, in_dungeon, tick);
        }

        return true;
    }

    // One copy for everyone: controller 1 plays player 1, and controller 2 the second player where couch co-op has one.
    ShowPlayer(0, 1, in_dungeon, tick);

    if (ActorPresent(1)) {
        ShowPlayer(1, 2, in_dungeon, tick);
    }

    return true;
}

} // namespace coop
