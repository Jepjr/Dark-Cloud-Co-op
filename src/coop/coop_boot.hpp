#pragma once

#include "coop/game_ns.hpp"

#include "platform/input_script.hpp"

// coop-boot: the start-up logos. The loading screen for the attract movie (map 801, nowload.cpp)
// shows the SCE logo and then the Level-5 logo, about seven seconds each; retail reads no buttons
// there. Start or Cross on either controller (and the keyboard's bindings, Enter and Space by default) ends
// them: the logo on screen fades out quickly and the game goes on to the attract movie. The title's
// own steps are left as they are.
//
// The logos run before a mode's first tick and change nothing the game simulates (the port's reads
// are synchronous, so the movie's data is the same however long they show), so a linked copy may
// end them on its own. With game.netplay set only a controller ends them: the keyboard and the
// mouse are ignored, as the link ignores them once it is up. DC_COOP_MUTATE=no_bootskip turns the
// skip off (the test's twin); bootskip_keys lets the keyboard through on a linked copy (the linked
// test's twin).

// Whether a skip button is down now. nowload.cpp asks once a tick while the logos show.
bool CoopBootSkipPressed();

// main.cpp hands over a copy of the --input script, so the keys it holds count as the keyboard.
void CoopBootKeepScript(const InputScript &script);

// coop-areacard (Jep, 2026-11-09: levers to try a shorter area name card): the card a mode's load shows (nowload.cpp:
// retail's 20 ticks before it, 128 fading in, 120 held, 128 fading out, about 6.6 s at 60 a second). config.json's game.*:
//   area_card            "retail" (default, retail's card), "short" (a resync's card: about 7 ticks), "custom" (the four below)
//   area_card_start      ticks before the card shows (retail 20)        area_card_fade_in   ticks fading in (retail 128)
//   area_card_hold       ticks held (retail 120)                         area_card_fade_out  ticks fading out (retail 128)
//   area_card_speed      "custom" only: every segment divided by it (2 = twice as fast; 1 by default)
//   area_card_coop_only  true (default): a single-player game keeps retail's card; false: every game
// --fast-load and a resync's load keep their short card whatever is set. The card runs between a mode's ticks, inside its
// load (no netplay tick, no game clock: retail's play-time count is off while it shows), so its length is drawing and wall
// time only: the copies may differ; a linked copy whose card ends first waits at its first tick for the other's pads.
// DC_COOP_MUTATE=area_card_ignored (twin): retail's card whatever is set.
struct CoopAreaCardPlan {
    int         start = 20;      // ticks before the card shows
    float       fade_in = 1.0f;  // alpha a tick fading in (128 is full)
    int         hold = 120;      // ticks held at full
    float       fade_out = 1.0f; // alpha a tick fading out
    const char *kind = "retail";
};

// The card for a mode's load that is not --fast-load's or a resync's.
CoopAreaCardPlan CoopAreaCard();

// Each area change's time, for the log (one "areacard:" line each): gameloop.cpp marks a mode's load (its ModeInit) start
// and end, nowload.cpp the card's showing (init_now_loading) and its end (the fade out's last tick).
// nowload.cpp's wait_now_loading_vsync and check_now_loading: a load yielding to the card's next tick (retail's loaders wait
// one between their reads; a town's EditInit spins on the card's end before its parts), so the line can tell the load's own
// work from its waits. (Only inside the mode's load: gameloop's own spin after it is the card past the load.)
void CoopAreaCardLoadStart(int map_no);
void CoopAreaCardOn(int title_number, const char *kind);
void CoopAreaCardOff(int ticks);
void CoopAreaCardLoaded();
void CoopAreaCardWaited(double ms);
