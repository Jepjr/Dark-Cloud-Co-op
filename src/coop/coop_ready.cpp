// coop-readyscreen: the one ready check for story starts and item scenes (see coop_ready.hpp).

#include "coop/coop_ready.hpp"

#include "coop/coop.hpp" // TestEnv

#include <cstdio>
#include <cstring>
#include <string>

namespace coop::ready {
namespace {

Session g_session;
bool    g_open = false;
Session g_last; // the last closed session's statuses (Accepted after Decide, for the caller's start)

// DC_WORLDS_MUTATE (the twins'): ready_no_autostart (every accept does not start it by itself), ready_start_now_off (START NOW
// takes nothing), ready_cancel_starts (CANCEL starts with the accepted, as a START NOW), ready_status_frozen (no answer changes a
// row), ready_two_players (only the first other linked player is asked: the old two-player way).
bool Mutated(const char *what) {
    const char *value = TestEnv("DC_WORLDS_MUTATE");

    return value != nullptr && std::strstr(value, what) != nullptr;
}

const char *KindName(Kind kind) {
    return kind == kStoryDoor ? "story start" : kind == kItemScene ? "item scene" : kind == kStoryArrival ? "story arrival" : "none";
}

void Say(const char *line) {
    std::fprintf(stderr, "ready: %s\n", line);
    std::fflush(stderr);
}

std::string RowsText(const Session &s) {
    std::string text;

    for (int p = 1; p <= kMaxPlayers; p++) {
        if (p == s.initiator || s.status[p] == kNotAsked) {
            continue;
        }
        text += (text.empty() ? "" : ", ") + std::string("player ") + std::to_string(p) + " " + StatusText(s.status[p]);
    }
    return text.empty() ? "nobody" : text;
}

void SetStatus(int player, Status status, const char *how) {
    if (!g_open || player < 1 || player > kMaxPlayers || player == g_session.initiator || g_session.status[player] == kNotAsked) {
        return;
    }

    char line[200];

    if (Mutated("ready_status_frozen")) {
        std::snprintf(line, sizeof line, "player %d %s: the row is left as it was (twin ready_status_frozen)", player, how);
        Say(line);
        return;
    }
    if (g_session.status[player] == status) {
        return;
    }
    g_session.status[player] = status;
    g_session.changes++;
    std::snprintf(line, sizeof line, "player %d: %s (%s); rows: %s", player, StatusText(status), how, RowsText(g_session).c_str());
    Say(line);
}

} // namespace

const char *StatusText(Status status) {
    switch (status) {
    case kAccepted:
        return "Accepted";
    case kDeclined:
        return "Declined";
    case kPending:
        return "Not answered yet";
    default:
        return "-";
    }
}

bool Open(Kind kind, int initiator, const bool linked[kMaxPlayers + 1]) {
    if (g_open || initiator < 1 || initiator > kMaxPlayers) {
        return false;
    }

    Session s;

    s.kind = kind;
    s.initiator = initiator;
    for (int p = 1; p <= kMaxPlayers; p++) {
        if (p != initiator && linked[p]) {
            s.status[p] = kPending;
            s.question[p] = true;
            if (Mutated("ready_two_players")) {
                break;
            }
        }
    }
    if (RowsText(s) == "nobody") {
        return false;
    }
    g_session = s;
    g_open = true;

    char line[240];

    std::snprintf(line, sizeof line, "player %d starts a %s: asking every other player at once; rows: %s", initiator, KindName(kind),
                  RowsText(g_session).c_str());
    Say(line);
    return true;
}

void Buttons(bool cancel, bool start_now) {
    if (!g_open || (g_session.cancel == cancel && g_session.start_now == start_now)) {
        return;
    }
    g_session.cancel = cancel;
    g_session.start_now = start_now;
    g_session.cursor = kButtonStartNow;
    g_session.changes++;

    char line[120];

    std::snprintf(line, sizeof line, "the box offers %s%s", start_now ? "START NOW" : "no START NOW", cancel ? " and CANCEL" : ", no CANCEL");
    Say(line);
}

bool IsOpen() {
    return g_open;
}

const Session &Get() {
    return g_open ? g_session : g_last;
}

bool Asking(int player) {
    return g_open && player >= 1 && player <= kMaxPlayers && g_session.question[player] && !g_session.waits[player] &&
           g_session.status[player] == kPending;
}

bool Waits(int player) {
    return g_open && player >= 1 && player <= kMaxPlayers && g_session.question[player] && g_session.waits[player] &&
           g_session.status[player] == kPending;
}

void QuestionWaits(int player, const char *why) {
    if (!Asking(player)) {
        return;
    }
    g_session.waits[player] = true;

    char line[200];

    std::snprintf(line, sizeof line, "player %d: busy (%s): its question waits for the player to be free; still Not answered yet", player, why);
    Say(line);
}

void QuestionComesUp(int player) {
    if (!Waits(player)) {
        return;
    }
    g_session.waits[player] = false;

    char line[120];

    std::snprintf(line, sizeof line, "player %d: free: its question comes up", player);
    Say(line);
}

int FirstAsked() {
    for (int p = 1; p <= kMaxPlayers; p++) {
        if (Asking(p)) {
            return p;
        }
    }
    return 0;
}

void Answer(int player, bool accepted) {
    if (Asking(player)) {
        g_session.question[player] = false;
    }
    SetStatus(player, accepted ? kAccepted : kDeclined, accepted ? "said yes" : "said no");
}

void QuestionTimedOut(int player) {
    if (!Asking(player)) {
        return;
    }
    g_session.question[player] = false;

    char line[120];

    std::snprintf(line, sizeof line, "player %d: its question closed unanswered: Not answered yet", player);
    Say(line);
}

void Left(int player) {
    if (g_open && player >= 1 && player <= kMaxPlayers) {
        g_session.question[player] = false;
    }
    SetStatus(player, kDeclined, "left");
}

void MoveCursor() {
    if (g_open) {
        g_session.cursor = !g_session.cancel ? kButtonStartNow : g_session.cursor == kButtonStartNow ? kButtonCancel : kButtonStartNow;
    }
}

Outcome Decide(Pick pick) {
    if (!g_open) {
        return kWaiting;
    }

    bool all = true;

    for (int p = 1; p <= kMaxPlayers; p++) {
        all = all && (p == g_session.initiator || g_session.status[p] == kNotAsked || g_session.status[p] == kAccepted);
    }

    Outcome     outcome = kWaiting;
    const char *why = nullptr;

    if (pick == kPickCancel) {
        if (Mutated("ready_cancel_starts")) {
            outcome = kStartNow;
            why = "CANCEL taken as START NOW (twin ready_cancel_starts)";
        } else {
            outcome = kCancel;
            why = "CANCEL: nothing starts";
        }
    } else if (pick == kPickStartNow) {
        if (!g_session.start_now) {
            Say("START NOW is not offered: the box waits (coop-firstvisit: player 1 neither started it nor accepted)");
        } else if (Mutated("ready_start_now_off")) {
            Say("START NOW does nothing (twin ready_start_now_off)");
        } else {
            outcome = kStartNow;
            why = "START NOW";
        }
    } else if (all && !Mutated("ready_no_autostart")) {
        outcome = kStartAll;
        why = "every other player accepted: it starts by itself";
    }
    if (outcome == kWaiting) {
        return kWaiting;
    }

    char        line[300];
    std::string with = "player " + std::to_string(g_session.initiator);
    std::string behind;

    for (int p = 1; p <= kMaxPlayers; p++) {
        if (p == g_session.initiator || g_session.status[p] == kNotAsked) {
            continue;
        }
        std::string &list = outcome != kCancel && g_session.status[p] == kAccepted ? with : behind;

        list += (list.empty() ? "player " : ", player ") + std::to_string(p);
    }
    if (outcome == kCancel) {
        std::snprintf(line, sizeof line, "%s: %s; nobody moves", KindName(g_session.kind), why);
    } else {
        std::snprintf(line, sizeof line, "%s: %s: starts with %s; left behind: %s", KindName(g_session.kind), why, with.c_str(),
                      behind.empty() ? "nobody" : behind.c_str());
    }
    Say(line);
    g_last = g_session;
    g_open = false;
    return outcome;
}

bool Accepted(int player) {
    const Session &s = Get();

    return player >= 1 && player <= kMaxPlayers && (player == s.initiator || s.status[player] == kAccepted);
}

int CountAccepted() {
    int count = 0;

    for (int p = 1; p <= kMaxPlayers; p++) {
        count += Get().status[p] == kAccepted ? 1 : 0;
    }
    return count;
}

void Close(const char *why) {
    if (!g_open) {
        return;
    }

    char line[160];

    std::snprintf(line, sizeof line, "%s closes: %s", KindName(g_session.kind), why);
    Say(line);
    g_last = g_session;
    g_open = false;
}

int Rows(int players[kMaxPlayers]) {
    const Session &s = Get();
    int            count = 0;

    for (int p = 1; p <= kMaxPlayers; p++) {
        if (p != s.initiator && s.status[p] != kNotAsked) {
            players[count++] = p;
        }
    }
    return count;
}

void TableTest() {
    struct Step {
        char what; // a accept, d decline, t time out, l left, s START NOW, c CANCEL, w wait (Decide with no pick)
        int  player;
    };
    struct Case {
        const char *name;
        int         players; // linked players, 1..players
        int         initiator;
        Step        steps[8];
    };
    // Three and four players, every path: the rows change as answers come, the box's two buttons, its own start.
    static const Case kCases[] = {
        {"3 players, all accept", 3, 1, {{'w', 0}, {'a', 2}, {'w', 0}, {'a', 3}, {'w', 0}}},
        {"3 players, one declines, START NOW", 3, 1, {{'a', 3}, {'d', 2}, {'w', 0}, {'s', 0}}},
        {"3 players, one unanswered, START NOW", 3, 2, {{'a', 1}, {'t', 3}, {'w', 0}, {'s', 0}}},
        {"3 players, CANCEL", 3, 3, {{'a', 1}, {'c', 0}}},
        {"3 players, one left while asked", 3, 1, {{'l', 2}, {'a', 3}, {'w', 0}, {'s', 0}}},
        {"4 players, mixed, START NOW", 4, 4, {{'d', 1}, {'a', 2}, {'t', 3}, {'w', 0}, {'s', 0}}},
    };

    if (g_open) {
        Close("the table test");
    }
    for (const Case &c : kCases) {
        bool linked[kMaxPlayers + 1] = {};

        for (int p = 1; p <= c.players; p++) {
            linked[p] = true;
        }

        char line[400];

        if (!Open(kStoryDoor, c.initiator, linked)) {
            std::snprintf(line, sizeof line, "table: %s -> not opened", c.name);
            Say(line);
            continue;
        }
        std::snprintf(line, sizeof line, "table: %s: asked %d", c.name, [] { int n = 0; for (int p = 1; p <= kMaxPlayers; p++) { n += Asking(p) ? 1 : 0; } return n; }());
        Say(line);

        Outcome outcome = kWaiting;

        for (const Step &step : c.steps) {
            if (step.what == 0 || outcome != kWaiting) {
                break;
            }
            switch (step.what) {
            case 'a': Answer(step.player, true); break;
            case 'd': Answer(step.player, false); break;
            case 't': QuestionTimedOut(step.player); break;
            case 'l': Left(step.player); break;
            default: break;
            }
            outcome = Decide(step.what == 's' ? kPickStartNow : step.what == 'c' ? kPickCancel : kPickNone);
        }

        std::string with;
        std::string rows;
        int         order[kMaxPlayers];
        const int   count = Rows(order);

        for (int i = 0; i < count; i++) {
            rows += (i ? ", " : "") + std::to_string(order[i]) + ":" + StatusText(Get().status[order[i]]);
        }
        for (int p = 1; p <= c.players && outcome != kCancel && outcome != kWaiting; p++) {
            with += Accepted(p) ? (with.empty() ? "" : "+") + std::to_string(p) : "";
        }
        std::snprintf(line, sizeof line, "table: %s -> %s with %s; rows %s", c.name,
                      outcome == kStartAll ? "starts by itself" : outcome == kStartNow ? "START NOW" : outcome == kCancel ? "cancelled" : "still waiting",
                      with.empty() ? "nobody" : with.c_str(), rows.c_str());
        Say(line);
        if (g_open) {
            Close("the table case ends");
        }
    }
    Say("table: done");
}

} // namespace coop::ready
