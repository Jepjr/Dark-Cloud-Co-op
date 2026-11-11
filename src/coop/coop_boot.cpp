#include "coop/game_ns.hpp"
#include "coop/coop_boot.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>

#include "coop/coop_camtest.hpp"
#include "coop/coop_core.hpp"
#include "coop/netplay.hpp"
#include "app/gameloop.hpp"
#include "platform/config.hpp"
#include "platform/input.hpp"

// coop-boot: see coop_boot.hpp.

namespace {

constexpr std::uint16_t kSkipButtons = kInputStart | kInputCross;

InputScript g_script;
bool        g_scripted = false;

// The buttons the keyboard and the mouse hold through the bindings: the live ones and a script's.
std::uint16_t KeyboardMouseButtons() {
    InputKeyboardMouse held;
    int                count = 0;

    if (const bool *keys = SDL_GetKeyboardState(&count)) {
        for (int scancode = 0; scancode < count; ++scancode) {
            if (keys[scancode]) {
                held.keys.push_back(scancode);
            }
        }
    }

    held.mouse_buttons = SDL_GetMouseState(nullptr, nullptr);

    if (g_scripted) {
        InputKeyboardMouse script = InputScriptDevicesAt(g_script, dc::app::GameFrameCount());

        held.keys.insert(held.keys.end(), script.keys.begin(), script.keys.end());
        held.mouse_buttons |= script.mouse_buttons;
    }

    return InputApplyKeyboardMouse(InputPadState{}, held).buttons;
}

} // namespace

void CoopBootKeepScript(const InputScript &script) {
    g_script = script;
    g_scripted = true;
}

bool CoopBootSkipPressed() {
    if (CoopMutate("no_bootskip")) {
        return false;
    }

    // Either controller: couch co-op's second player, and a linked player 2's own (controller 2,
    // or controller 1 on a PC of its own). The keyboard and the mouse fold into pad 1 alone.
    std::uint16_t first = InputGetPad(0).buttons;
    std::uint16_t second = InputGetPad(1).buttons;

    if (NetplayOwnsPads() && !CoopMutate("bootskip_keys")) {
        first &= static_cast<std::uint16_t>(~KeyboardMouseButtons());
    }

    return ((first | second) & kSkipButtons) != 0;
}

// coop-areacard: see coop_boot.hpp.
CoopAreaCardPlan CoopAreaCard() {
    CoopAreaCardPlan plan;
    const dc::platform::Config    &config = dc::platform::ConfigGet();

    if (coop::Mutated("area_card_ignored") || config.area_card == "retail" || config.area_card.empty()) {
        return plan;
    }
    if (config.area_card_coop_only && !config.coop && config.netplay == 0) {
        plan.kind = "retail (single player: area_card_coop_only)";
        return plan;
    }
    if (config.area_card == "short") {
        // A resync's card (nowload.cpp's Quick): one tick before it, two fading in, two held, two fading out.
        plan.start = 1;
        plan.fade_in = plan.fade_out = 64.0f;
        plan.hold = 2;
        plan.kind = "short";
        return plan;
    }
    if (config.area_card != "custom") {
        plan.kind = "retail (area_card not understood)";
        return plan;
    }

    const float speed = config.area_card_speed > 0.0f ? config.area_card_speed : 1.0f;
    auto        ticks = [&](int retail) { return (int) std::lround((double) retail / speed); };
    // A fade of f ticks adds 128 / f a tick (retail's 128 ticks: 1); none at all crosses at once.
    auto        rate = [&](int fade) { const int f = ticks(fade); return f > 0 ? 128.0f / (float) f : 256.0f; };

    plan.start = std::max(1, ticks(config.area_card_start)); // (the card's first tick comes once the count is 0)
    plan.fade_in = rate(config.area_card_fade_in);
    plan.hold = ticks(config.area_card_hold);
    plan.fade_out = rate(config.area_card_fade_out);
    plan.kind = "custom";
    return plan;
}

namespace {

struct AreaCardTimes {
    int         map_no = -1;
    int         title = -1;
    const char *kind = "";
    int         ticks = 0;
    double      waited = 0.0;
    int         yields = 0;
    std::int64_t load_at = -1, loaded_at = -1, card_on = -1, card_off = -1;
};

AreaCardTimes g_card;

std::int64_t CardNowMs() {
    return (std::int64_t) std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// Once both the load and the card are over: how much of the load the card covered.
void CardSay() {
    if (g_card.load_at < 0 || g_card.loaded_at < 0 || (g_card.card_on >= 0 && g_card.card_off < 0)) {
        return;
    }
    const long long load = g_card.loaded_at - g_card.load_at;

    if (g_card.card_on < 0) {
        std::fprintf(stderr, "areacard: map %d: no card, load %lld ms\n", g_card.map_no, load);
    } else {
        const long long card = g_card.card_off - g_card.card_on;
        const long long under = std::max<long long>(0, std::min(g_card.loaded_at, g_card.card_off) - std::max(g_card.load_at, g_card.card_on));

        std::fprintf(stderr,
                     "areacard: map %d card %d (%s, %d ticks): load %lld ms, card %lld ms; the load under the card %lld ms, the load past the card "
                     "%lld ms, the card past the load %lld ms; the load's own work %lld ms (it waited %lld ms in %d yields to the card's ticks), of it the card's own set-up "
                     "%lld ms (before the card's first tick)\n",
                     g_card.map_no, g_card.title, g_card.kind, g_card.ticks, load, card, under, std::max<long long>(0, g_card.loaded_at - g_card.card_off),
                     std::max<long long>(0, g_card.card_off - g_card.loaded_at), std::max<long long>(0, load - (long long) g_card.waited),
                     (long long) g_card.waited, g_card.yields, (long long) (g_card.card_on - g_card.load_at));
    }
    g_card = AreaCardTimes{};
}

} // namespace

void CoopAreaCardLoadStart(int map_no) {
    g_card = AreaCardTimes{};
    g_card.map_no = map_no;
    g_card.load_at = CardNowMs();
}

void CoopAreaCardOn(int title_number, const char *kind) {
    if (g_card.load_at < 0) { // (a card no mode's load brackets: no line)
        return;
    }
    g_card.title = title_number;
    g_card.kind = kind;
    g_card.card_on = CardNowMs();
}

void CoopAreaCardOff(int ticks) {
    if (g_card.card_on < 0 || g_card.card_off >= 0) {
        return;
    }
    g_card.ticks = ticks;
    g_card.card_off = CardNowMs();
    CardSay();
}

void CoopAreaCardLoaded() {
    if (g_card.load_at < 0 || g_card.loaded_at >= 0) {
        return;
    }
    g_card.loaded_at = CardNowMs();
    CardSay();
}

void CoopAreaCardWaited(double ms) {
    if (g_card.load_at >= 0 && g_card.loaded_at < 0) {
        g_card.waited += ms;
        g_card.yields++;
    }
}
