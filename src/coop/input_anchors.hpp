#pragma once

#include "coop/game_ns.hpp"

#include <cstdint>
#include <filesystem>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

#include "platform/input_script.hpp"

// coop-anchors (Jep, 2026-11-03; the reds review, fix 6): an --input script times its presses on game events, not on
// boot frames. Besides the input script's own lines (platform/input_script.hpp) it takes anchor lines:
//
//     @tick N          wait here until the link's tick reaches N (the copy linked)
//     @event REGEX     wait here until a line of this copy's own log matches REGEX (ECMAScript, searched anywhere in
//                      the line; `#` still starts a comment, write \x23 for one)
//
// The script runs to the frame of its last line before the anchor and holds there (its pads keep that line's state)
// until the anchor happens; the frames of the lines after it count from that moment (0 = the frame it happened). They
// sit one frame past the hold, so no line after the anchor is read before it happens (coop-anchors fix, 2026-11-04:
// a frame-0 line shared the hold's frame and showed through the hold, a delay early against the later lines). An
// event counts from the previous anchor's moment (the first anchor's from boot), so one that comes before the script
// reaches its anchor does not hold it. Under load a boot is slower and the link later, so a press at a boot frame
// missed the event it was meant for (l-local-play-houseout-seamless 9% red quiet, 84% at load factor 1.5 or more); an
// anchored press waits for it. Each anchor says one line as it waits and one as it happens ("input anchor ...").
// DC_NETPLAY_MUTATE=no_input_anchor (the twin) reads anchors as plain frames: the script runs straight through.

struct InputAnchor {
    std::int64_t frame = 0; // the script frame it holds at
    bool         event = false;
    std::int64_t tick = 0;  // @tick
    std::string  pattern;   // @event
    std::regex   regex;
    int          line = 0;
};

// Parses a script with its anchors; on failure false and error "line N: why" (the script's own line numbers).
bool CoopInputScriptParse(std::string_view text, InputScript &script, std::vector<InputAnchor> &anchors, std::string &error);

// InputScriptLoad with anchors: the script into script, its anchors installed for CoopInputAnchorsFrame (none: cleared).
bool CoopInputScriptLoad(const std::filesystem::path &path, InputScript &script, std::string &error);

void CoopInputAnchorsInstall(std::vector<InputAnchor> anchors);

// The script frame for a script frame of the clock below it (NetplayTestInputFrame): held at each anchor not yet
// happened, shifted by the frames held so far. Called once a frame.
std::int64_t CoopInputAnchorsFrame(std::int64_t frame);

// How many holds have let go so far: on each, netplay's script reads take the input delay of that moment as their base
// (ScriptInputFor), so the lines' frame 0 goes to the frame given then, not to one given while the anchor still held
// (with the delay grown past its first, those frames were given before the anchor happened: the frame-0 press was lost).
int CoopInputAnchorsReleases();

// The last script frame that may be read now: the frame the next anchor not yet happened holds at (a read ahead for a
// later game frame, netplay's ScriptInputFor, never passes it), else no limit.
std::int64_t CoopInputAnchorsLimit();
// coop-gatekey52 (Jep, 2026-11-04: no fixed tick waits): a test hook's event instead of its tick (netplay_test.cpp's
// DC_NETPLAY_NEXT_FLOOR / DC_NETPLAY_LEAVE "@..." entries). spec is one or more ECMAScript regexes separated by ">>", each
// matched by a line of this copy's own log after the line the one before matched (the log read from the copy's start).
// Only for a copy playing on its own (apart): its log is not in lockstep with the other copy's.
int  CoopLogEventAdd(std::string_view spec);
bool CoopLogEventSeen(int id);
