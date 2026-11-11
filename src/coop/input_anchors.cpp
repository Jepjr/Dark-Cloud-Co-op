#include "coop/game_ns.hpp"
#include "coop/input_anchors.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "coop/netplay.hpp"
#include "platform/paths.hpp"

bool NetplayMutated(const char *name); // netplay_test.cpp (its header holds game types)

namespace {

std::vector<InputAnchor> g_anchors;
std::size_t              g_next = 0;     // the first anchor not yet happened
std::int64_t             g_held = 0;     // script frames held so far
bool                     g_said_wait = false;
int                      g_releases = 0;    // holds let go (CoopInputAnchorsReleases)
std::int64_t             g_wait_from = 0;   // the frame the anchor began to hold

std::string_view Trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) {
        text.remove_suffix(1);
    }
    return text;
}

// This copy's own log, read back as the game writes it: the file its stdout (and stderr, the harness's one stream, or
// its own file) goes to. Only a test copy started with its output in files has one; a console reads nothing.
struct LogTail {
    bool                     opened = false;
    std::vector<std::FILE *> files;
    std::vector<std::string> carry;
    std::vector<std::string> lines; // complete lines not yet looked at

    void Open() {
        opened = true;
#ifdef _WIN32
        // (the harness's one stream sends stderr to stdout's file through another handle: one file is read once)
        BY_HANDLE_FILE_INFORMATION seen[2] = {};
        int                        count = 0;
        for (int fd : {_fileno(stdout), _fileno(stderr)}) {
            HANDLE                     handle = reinterpret_cast<HANDLE>(_get_osfhandle(fd));
            BY_HANDLE_FILE_INFORMATION info = {};
            if (handle == INVALID_HANDLE_VALUE || handle == nullptr || GetFileType(handle) != FILE_TYPE_DISK ||
                !GetFileInformationByHandle(handle, &info)) {
                continue;
            }
            if (count == 1 && seen[0].dwVolumeSerialNumber == info.dwVolumeSerialNumber &&
                seen[0].nFileIndexHigh == info.nFileIndexHigh && seen[0].nFileIndexLow == info.nFileIndexLow) {
                continue;
            }
            seen[count++] = info;
            HANDLE again = ReOpenFile(handle, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0);
            if (again == INVALID_HANDLE_VALUE) {
                continue;
            }
            int own = _open_osfhandle(reinterpret_cast<intptr_t>(again), _O_RDONLY | _O_BINARY);
            std::FILE *file = own >= 0 ? _fdopen(own, "rb") : nullptr;
            if (file != nullptr) {
                files.push_back(file);
            }
        }
#else
        struct stat seen = {};
        for (const char *path : {"/proc/self/fd/1", "/proc/self/fd/2"}) {
            struct stat info = {};
            if (stat(path, &info) != 0 || !S_ISREG(info.st_mode) || (files.size() == 1 && info.st_dev == seen.st_dev && info.st_ino == seen.st_ino)) {
                continue;
            }
            seen = info;
            std::FILE *file = std::fopen(path, "rb");
            if (file != nullptr) {
                files.push_back(file);
            }
        }
#endif
        carry.resize(files.size());
        std::fprintf(stderr, "input anchor: reading this copy's log from %d file(s)\n", static_cast<int>(files.size()));
    }

    void Read() {
        if (!opened) {
            Open();
        }
        char buffer[4096];
        for (std::size_t i = 0; i < files.size(); ++i) {
            std::clearerr(files[i]);
            std::size_t got = 0;
            while ((got = std::fread(buffer, 1, sizeof buffer, files[i])) > 0) {
                carry[i].append(buffer, got);
            }
            std::size_t at = 0;
            std::size_t end = 0;
            while ((end = carry[i].find('\n', at)) != std::string::npos) {
                std::size_t from = at;
                // (coop-fx51a: a fixture copy's lines start "f<frame> ", DC_QA_TICK_MARK; an anchor's pattern reads the line
                // without it, as the harness's readers do: x-door-gate-cavebothmenu's "@event ^coop-state: loaded" never fired)
                if (from < end && carry[i][from] == 'f') {
                    std::size_t d = from + 1;
                    if (d < end && carry[i][d] == '-') {
                        d++;
                    }
                    const std::size_t digits = d;
                    while (d < end && carry[i][d] >= '0' && carry[i][d] <= '9') {
                        d++;
                    }
                    if (d > digits && d < end && carry[i][d] == ' ') {
                        from = d + 1;
                    }
                }
                lines.push_back(carry[i].substr(from, end - from));
                at = end + 1;
            }
            carry[i].erase(0, at);
        }
    }
};

LogTail g_log;

// DC_NETPLAY_MUTATE=anchor_hold_leak (input-anchors-tick~leak): the lines after an anchor on the hold's own frame and the
// link's read ahead past it, as before the 2026-11-04 fix: a frame-0 line shows through the hold.
bool HoldLeaks() {
    static const bool leak = NetplayMutated("anchor_hold_leak");
    return leak;
}

const char *Describe(const InputAnchor &anchor, std::string &text) {
    text = anchor.event ? "@event " + anchor.pattern : "@tick " + std::to_string(anchor.tick);
    return text.c_str();
}

// Whether the next anchor has happened: its tick reached, or a log line since the last anchor matches. Lines after the
// matching one stay for the anchors after it.
bool Happened(const InputAnchor &anchor) {
    if (!anchor.event) {
        return NetplayLinkUp() && NetplayLinkTick() >= anchor.tick;
    }
    g_log.Read();
    for (std::size_t i = 0; i < g_log.lines.size(); ++i) {
        const std::string &line = g_log.lines[i];
        if (line.starts_with("input anchor")) {
            continue; // (its own lines name the pattern)
        }
        if (std::regex_search(line, anchor.regex)) {
            g_log.lines.erase(g_log.lines.begin(), g_log.lines.begin() + static_cast<std::ptrdiff_t>(i) + 1);
            return true;
        }
    }
    g_log.lines.clear();
    return false;
}

} // namespace

bool CoopInputScriptParse(std::string_view text, InputScript &script, std::vector<InputAnchor> &anchors, std::string &error) {
    anchors.clear();
    std::string  plain;        // the script with every frame made absolute and the anchors as empty lines
    std::int64_t base = 0;     // the frame the latest anchor holds at (lines after it count from it)
    std::int64_t last = 0;     // the latest absolute frame so far
    int          number = 0;
    while (!text.empty()) {
        std::size_t      end = text.find('\n');
        std::string_view line = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view() : text.substr(end + 1);
        ++number;
        std::string_view body = Trim(line.substr(0, line.find('#')));
        if (body.empty()) {
            plain += '\n';
            continue;
        }
        if (body.front() == '@') {
            InputAnchor anchor;
            anchor.frame = last;
            anchor.line = number;
            std::size_t      space = body.find_first_of(" \t");
            std::string_view word = body.substr(0, space);
            std::string_view rest = space == std::string_view::npos ? std::string_view() : Trim(body.substr(space));
            if (word == "@tick") {
                long long tick = -1;
                if (rest.empty() || std::sscanf(std::string(rest).c_str(), "%lld", &tick) != 1 || tick < 0 ||
                    std::to_string(tick) != rest) {
                    error = "line " + std::to_string(number) + ": @tick takes a non-negative tick";
                    return false;
                }
                anchor.tick = tick;
            } else if (word == "@event") {
                if (rest.empty()) {
                    error = "line " + std::to_string(number) + ": @event takes a pattern";
                    return false;
                }
                anchor.event = true;
                anchor.pattern = std::string(rest);
                try {
                    anchor.regex = std::regex(anchor.pattern, std::regex::ECMAScript | std::regex::optimize);
                } catch (const std::regex_error &) {
                    error = "line " + std::to_string(number) + ": @event pattern is not a regex";
                    return false;
                }
            } else {
                error = "line " + std::to_string(number) + ": unknown anchor \"" + std::string(word) + "\" (@tick or @event)";
                return false;
            }
            anchors.push_back(std::move(anchor));
            // The lines after it start on the frame after its hold: the hold shows only the lines before it (a frame-0 line
            // at the hold's own frame was held, and read ahead by the link's delay, before the anchor happened).
            base = HoldLeaks() ? last : last + 1;
            last = base;
            plain += '\n';
            continue;
        }
        std::size_t      space = body.find_first_of(" \t");
        std::string_view first = body.substr(0, space);
        long long        frame = -1;
        if (anchors.empty() || std::sscanf(std::string(first).c_str(), "%lld", &frame) != 1 || std::to_string(frame) != first) {
            plain += std::string(body) + '\n'; // (before any anchor, or not a frame: the input script says why)
            if (anchors.empty() && std::sscanf(std::string(first).c_str(), "%lld", &frame) == 1 && frame > last) {
                last = frame;
            }
            continue;
        }
        std::int64_t absolute = base + frame;
        last = std::max(last, absolute);
        plain += std::to_string(absolute);
        plain += space == std::string_view::npos ? std::string_view() : body.substr(space);
        plain += '\n';
    }
    return InputScriptParse(plain, script, error);
}

bool CoopInputScriptLoad(const std::filesystem::path &path, InputScript &script, std::string &error) {
    std::ifstream file(path);
    if (!file) {
        error = "cannot read " + PathsDisplay(path);
        return false;
    }
    std::stringstream text;
    text << file.rdbuf();
    std::vector<InputAnchor> anchors;
    if (!CoopInputScriptParse(text.str(), script, anchors, error)) {
        error = PathsDisplay(path) + ": " + error;
        return false;
    }
    CoopInputAnchorsInstall(std::move(anchors));
    return true;
}

void CoopInputAnchorsInstall(std::vector<InputAnchor> anchors) {
    g_anchors = std::move(anchors);
    g_next = 0;
    g_held = 0;
    g_said_wait = false;
    g_log.lines.clear(); // (an event counts from the script's start)
    if (!g_anchors.empty()) {
        std::fprintf(stderr, "input anchor: %d in the script%s\n", static_cast<int>(g_anchors.size()),
                     NetplayMutated("no_input_anchor") ? " (mutation no_input_anchor: read as plain frames)" : "");
    }
}

std::int64_t CoopInputAnchorsFrame(std::int64_t frame) {
    static const bool off = NetplayMutated("no_input_anchor");
    if (g_next >= g_anchors.size() || off) {
        return frame - g_held;
    }
    std::string text;
    while (g_next < g_anchors.size()) {
        const InputAnchor &anchor = g_anchors[g_next];
        std::int64_t       at = frame - g_held;
        if (!Happened(anchor)) {
            if (at < anchor.frame) {
                return at;
            }
            if (!g_said_wait) {
                g_said_wait = true;
                g_wait_from = frame;
                std::fprintf(stderr, "input anchor %d/%d (line %d, %s): waits at script frame %lld (link tick %lld)\n",
                             static_cast<int>(g_next + 1), static_cast<int>(g_anchors.size()), anchor.line,
                             Describe(anchor, text), static_cast<long long>(anchor.frame),
                             static_cast<long long>(NetplayLinkTick()));
            }
            g_held = frame - anchor.frame;
            return anchor.frame;
        }
        // Happened. Held there: the script goes on from the frame after the hold now (this frame is the lines' frame 0).
        // Not reached yet: it runs on as it was, and the next anchor's event counts from here.
        bool waited = at >= anchor.frame; // (reached: held there, if only for this frame)
        if (waited) {
            g_held = frame - (anchor.frame + (HoldLeaks() ? 0 : 1));
            ++g_releases;
        }
        if (!anchor.event && g_log.opened) { // (a tick: the next event counts from now)
            g_log.Read();
            g_log.lines.clear();
        }
        std::fprintf(stderr,
                     "input anchor %d/%d (line %d, %s): happened at script frame %lld, link tick %lld (%s %lld frames)\n",
                     static_cast<int>(g_next + 1), static_cast<int>(g_anchors.size()), anchor.line, Describe(anchor, text),
                     static_cast<long long>(waited ? frame - g_held : at), static_cast<long long>(NetplayLinkTick()),
                     waited ? "held" : "before the script reached it, held", static_cast<long long>(waited && g_said_wait ? frame - g_wait_from : 0));
        ++g_next;
        g_said_wait = false;
        if (waited) {
            break;
        }
    }
    return frame - g_held;
}

int CoopInputAnchorsReleases() {
    return HoldLeaks() ? 0 : g_releases;
}

std::int64_t CoopInputAnchorsLimit() {
    static const bool off = NetplayMutated("no_input_anchor");
    if (g_next >= g_anchors.size() || off || HoldLeaks()) {
        return INT64_MAX;
    }
    return g_anchors[g_next].frame;
}
// coop-gatekey52: test hooks' log events (input_anchors.hpp), read through a tail of their own so the script's anchors
// keep every line they wait for.
namespace {

struct LogEvent {
    std::string             spec;
    std::vector<std::regex> steps;
    std::size_t             at = 0;
};

LogTail               g_event_log;
std::vector<LogEvent> g_events;

} // namespace

int CoopLogEventAdd(std::string_view spec) {
    LogEvent event;
    event.spec = std::string(spec);
    for (std::size_t from = 0; from <= spec.size();) {
        std::size_t      cut = spec.find(">>", from);
        std::string_view step = Trim(spec.substr(from, cut == std::string_view::npos ? std::string_view::npos : cut - from));
        if (!step.empty()) {
            event.steps.emplace_back(std::string(step), std::regex::ECMAScript);
        }
        if (cut == std::string_view::npos) {
            break;
        }
        from = cut + 2;
    }
    g_events.push_back(std::move(event));
    return static_cast<int>(g_events.size()) - 1;
}

bool CoopLogEventSeen(int id) {
    if (id < 0 || id >= static_cast<int>(g_events.size())) {
        return false;
    }
    g_event_log.Read();
    for (const std::string &line : g_event_log.lines) {
        if (line.starts_with("netplay: test: hook waits")) {
            continue; // (its own lines name the pattern)
        }
        for (LogEvent &event : g_events) {
            if (event.at < event.steps.size() && std::regex_search(line, event.steps[event.at])) {
                ++event.at;
            }
        }
    }
    g_event_log.lines.clear();
    return g_events[id].at >= g_events[id].steps.size();
}
