// Test hook (DC_TEST_VPAD=N:SPEC): SDL virtual gamepads, so a test drives a copy through the same input path as
// a real controller (SDL, kazarov's gamepad read, the copy's controller binding), not the scripted pad's override.
// N gamepads are attached at the first tick; SPEC is "T:PAD:AXIS:VALUE,..." (a tick of this copy's own count, the
// pad 1..N, AXIS lx/ly/rx/ry or a button name dup/ddown/dleft/dright/a/b/x/y/start/back, VALUE -32768..32767 or 0/1).
// Jep's 2026-10-20 quick-slot report: his Xbox pad's left stick moved player 2's quick-use slot.

#include "coop/game_ns.hpp"
#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "coop/coop.hpp"
#include "coop/netplay.hpp"

namespace coop {

namespace {

struct Step {
    long tick;
    int  pad;
    char what[8];
    int  value;
};

struct Vpad {
    bool                      read = false;
    std::vector<SDL_Joystick *> pads;
    std::vector<Step>         steps;
    long                      ticks = 0;
} g_vpad;

int Axis(const char *what) {
    return std::strcmp(what, "lx") == 0 ? SDL_GAMEPAD_AXIS_LEFTX
           : std::strcmp(what, "ly") == 0 ? SDL_GAMEPAD_AXIS_LEFTY
           : std::strcmp(what, "rx") == 0 ? SDL_GAMEPAD_AXIS_RIGHTX
           : std::strcmp(what, "ry") == 0 ? SDL_GAMEPAD_AXIS_RIGHTY
                                          : -1;
}

int Button(const char *what) {
    return std::strcmp(what, "dup") == 0 ? SDL_GAMEPAD_BUTTON_DPAD_UP
           : std::strcmp(what, "ddown") == 0 ? SDL_GAMEPAD_BUTTON_DPAD_DOWN
           : std::strcmp(what, "dleft") == 0 ? SDL_GAMEPAD_BUTTON_DPAD_LEFT
           : std::strcmp(what, "dright") == 0 ? SDL_GAMEPAD_BUTTON_DPAD_RIGHT
           : std::strcmp(what, "a") == 0 ? SDL_GAMEPAD_BUTTON_SOUTH
           : std::strcmp(what, "b") == 0 ? SDL_GAMEPAD_BUTTON_EAST
           : std::strcmp(what, "x") == 0 ? SDL_GAMEPAD_BUTTON_WEST
           : std::strcmp(what, "y") == 0 ? SDL_GAMEPAD_BUTTON_NORTH
           : std::strcmp(what, "start") == 0 ? SDL_GAMEPAD_BUTTON_START
           : std::strcmp(what, "back") == 0 ? SDL_GAMEPAD_BUTTON_BACK
                                            : -1;
}

void Attach(int count, const char *spec) {
    for (int i = 0; i < count; i++) {
        SDL_VirtualJoystickDesc desc;
        SDL_INIT_INTERFACE(&desc);
        desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
        desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
        desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
        desc.vendor_id = 0x1209; // pid.codes' test VID: not a real pad's
        desc.product_id = (Uint16) (0x7000 + i);
        desc.name = "coop test pad";
        SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
        SDL_Joystick  *joystick = id != 0 ? SDL_OpenJoystick(id) : nullptr;
        g_vpad.pads.push_back(joystick);
        std::printf("coop-vpad: virtual gamepad %d attached (id %u%s)\n", i + 1, (unsigned) id, joystick != nullptr ? "" : ", not opened");
    }
    for (const char *at = spec; at != nullptr && *at != '\0';) {
        Step step{};
        if (std::sscanf(at, "%ld:%d:%7[a-z]:%d", &step.tick, &step.pad, step.what, &step.value) == 4) {
            g_vpad.steps.push_back(step);
        }
        at = std::strchr(at, ',');
        at = at != nullptr ? at + 1 : nullptr;
    }
}

} // namespace

void VpadTick() {
    if (!g_vpad.read) {
        g_vpad.read = true;
        const char *spec = TestEnv("DC_TEST_VPAD");
        if (spec != nullptr) {
            const char *colon = std::strchr(spec, ':');
            Attach(std::atoi(spec), colon != nullptr ? colon + 1 : "");
        }
    }
    if (g_vpad.pads.empty()) {
        return;
    }
    // coop-fx51d: DC_TEST_VPAD_FROM=press: the steps' ticks count from the tick the menu presses' clock starts (after a
    // fixture's load or a join's catch-up, NetplayTestPressClock), then on with this copy's own count, apart too (where
    // that clock stands still): a row on a fixture times its pads on the game, not on its boot (q-p2-quickslot -Fixture).
    static const bool from_press = TestEnvIs("DC_TEST_VPAD_FROM", "press");

    if (from_press && g_vpad.ticks == 0 && NetplayTestPressClock() < 0) {
        return;
    }
    g_vpad.ticks++;
    for (const Step &step : g_vpad.steps) {
        if (step.tick != g_vpad.ticks || step.pad < 1 || step.pad > (int) g_vpad.pads.size() || g_vpad.pads[step.pad - 1] == nullptr) {
            continue;
        }
        SDL_Joystick *joystick = g_vpad.pads[step.pad - 1];
        if (Axis(step.what) >= 0) {
            SDL_SetJoystickVirtualAxis(joystick, Axis(step.what), (Sint16) step.value);
        } else if (Button(step.what) >= 0) {
            SDL_SetJoystickVirtualButton(joystick, Button(step.what), step.value != 0);
        }
    }
}

} // namespace coop
