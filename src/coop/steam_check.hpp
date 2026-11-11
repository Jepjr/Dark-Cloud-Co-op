#pragma once

#include "coop/game_ns.hpp"

#include <string>

// coop-steamsetup (Jep, 2026-11-03: "there should be an indicator in the mod UI ... it should just check for steam running,
// steamlib, and spacewar install"): what Steam joins need, looked at without starting Steam or loading its DLL (the game
// never does; the Steam helper does, only when Steam is picked). Running: Steam's own ActiveProcess pid in the registry,
// and that process alive. The lib: steam_api64.dll where the helper loads it (DC_STEAM_API, else beside the game).
// Spacewar's install is not required (coop-pt1108s, Jep's 11-08 playtest: a Steam join worked with none; the helper sets
// AppID 480 itself); it is read for the twin only. The lib is set up with "Set up Steam.bat" beside the launcher.
// Looked at again every two seconds; a change is logged ("steam-check: ..."). Test switches (a player never sets them):
// DC_STEAM_CHECK=r,l,s (each 0 or 1) in place of the real three; DC_STEAM_CHECK_MUTATE=blind (the twin) says all three yes.

namespace coop::steamcheck {

struct State {
    bool running = false;
    bool lib = false;
    bool spacewar = false;      // looked at for the twin only (coop-pt1108s): Steam joins work without the install
    bool need_spacewar = false; // DC_STEAM_CHECK_MUTATE=need_spacewar

    bool Ready() const {
        return running && lib && (spacewar || !need_spacewar);
    }
};

// The three, at most two seconds old.
const State &Get();

// One short line for the screen: "Steam ready", or what is missing and where to fix it.
std::string Line();

} // namespace coop::steamcheck
