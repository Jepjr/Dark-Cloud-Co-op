#include "coop/game_ns.hpp"
#include "coop/netplay_session.hpp"

#include <cstdio>

#include "coop/netplay_test.hpp"

namespace {

NetSession g_session = NetSession::Title;

// The changes the link can make. A row is the state it leaves; true for each state it may go to.
bool Allowed(NetSession from, NetSession to) {
    using S = NetSession;

    switch (from) {
        case S::Title: return to == S::Hosting || to == S::Joining;
        case S::Hosting: return to == S::Linked || to == S::Title;
        case S::Joining: return to == S::Linked || to == S::Title;
        case S::Linked: return to == S::Apart || to == S::Dropped || to == S::Title;
        case S::Apart: return to == S::Linked || to == S::Dropped || to == S::Rejoining || to == S::Title;
        case S::Dropped: return to == S::Linked || to == S::Rejoining || to == S::Apart || to == S::Title;
        case S::Rejoining: return to == S::Linked || to == S::Dropped || to == S::Title;
    }
    return false;
}

} // namespace

bool NetSessionOff() {
    return NetplayMutated("session_off");
}

NetSession NetSessionNow() {
    return g_session;
}

const char *NetSessionName(NetSession state) {
    switch (state) {
        case NetSession::Title: return "title";
        case NetSession::Hosting: return "hosting";
        case NetSession::Joining: return "joining";
        case NetSession::Linked: return "linked";
        case NetSession::Apart: return "apart";
        case NetSession::Dropped: return "dropped";
        case NetSession::Rejoining: return "rejoining";
    }
    return "?";
}

bool NetSessionTo(NetSession next, const char *why) {
    if (next == g_session) {
        return true;
    }
    if (!Allowed(g_session, next)) {
        std::fprintf(stderr, "netplay: session: refused %s -> %s (%s)\n", NetSessionName(g_session), NetSessionName(next), why);
        return false;
    }
    std::fprintf(stderr, "netplay: session: %s -> %s (%s)\n", NetSessionName(g_session), NetSessionName(next), why);
    g_session = next;
    return true;
}
