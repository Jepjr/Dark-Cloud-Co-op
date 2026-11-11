#include "coop/game_ns.hpp"
#include "coop/coop_present.hpp"

#include "coop/coop.hpp"
#include "coop/coop_camtest.hpp" // CoopMutate
#include "coop/netplay.hpp"

#include <cstdio>
#include <cstring>

namespace dc::game {
unsigned int PortRandState(); // port/src/runtime.cpp
} // namespace dc::game

namespace coop::present {

namespace {

long g_checked = 0;
long g_caught = 0;

bool GuardOn() {
    static const bool on = TestEnvOn("DC_COOP_PRESENT_GUARD") && !CoopMutate("present_guard_off");
    return on && NetplayHasPlayer();
}

const char *Short(const char *where) {
    const char *slash = std::strrchr(where, '/');
    const char *back = std::strrchr(where, '\\');
    const char *last = slash > back ? slash : back;
    return last != nullptr ? last + 1 : where;
}

} // namespace

Scope::Scope(For who, const char *where) : where_(where) {
    owner_.emplace(who.player);
    message_.emplace(who.player);
    Enter();
}

Scope::Scope(At from, const char *where) : where_(where) {
    from_.emplace(from.pos);
    Enter();
}

Scope::Scope(Everyone, const char *where) : where_(where) {}

void Scope::Enter() {
    if (!GuardOn()) {
        return;
    }
    guarded_ = true;
    rand_ = PortRandState();
    checksum_ = NetplayTestChecksum();
}

Scope &Scope::MayStep() {
    guarded_ = false;
    return *this;
}

Scope::~Scope() {
    if (!guarded_) {
        return;
    }

    const unsigned int  rand = PortRandState();
    const std::uint32_t checksum = NetplayTestChecksum();
    const bool          caught = rand != rand_ || checksum != checksum_;

    g_checked++;
    if (rand != rand_) {
        std::fprintf(stderr, "present: scope %s changed the random generator (%08x -> %08x)\n", Short(where_), rand_, rand);
    }
    if (checksum != checksum_) {
        std::fprintf(stderr, "present: scope %s changed the lockstep checksum (%08x -> %08x)\n", Short(where_), checksum_, checksum);
    }
    if (caught) {
        g_caught++;
    }
    if (g_checked % 600 == 1) {
        std::fprintf(stderr, "present: guard checked %ld scopes, %ld changed the step's state\n", g_checked, g_caught);
    }
}

long GuardChecked() {
    return g_checked;
}

long GuardCaught() {
    return g_caught;
}

} // namespace coop::present
