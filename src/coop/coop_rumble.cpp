#include "coop/game_ns.hpp"
#include "coop/coop_rumble.hpp"

#include "coop/coop_core.hpp"
#include "coop/coop_worlds.hpp"
#include "coop/netplay.hpp"

#include <cstdio>

#include <libpad.h>

#include "platform/input.hpp"

namespace {

struct Motors {
    unsigned char level[2] = {0, 0};
    int           timer[2] = {0, 0};
};

Motors g_partner;          // the partner's hits' motors (both copies alike; not game state)
int    g_device = -1;      // the controller this copy last shook, -1 none
bool   g_shaking = false;  // whether it shakes now (the log's starts and stops)

// The player the game's own walker (Chara, CharaMain) is on this copy: player 1 together; apart, this copy's player;
// while her town event runs on the game's pad (CoopWorldsP2Drives), player 2.
int WalkerPlayer(int local) {
    if (CoopWorldsP2Drives()) {
        return 2;
    }

    return CoopWorldsApart() ? local : 1;
}

void Say(int local, int device, bool shaking, const char *whose, int owner) {
    if (shaking == g_shaking && (device == g_device || !shaking)) {
        return;
    }

    if (shaking) {
        std::fprintf(stderr, "coop-rumble: copy of p%d: controller %d starts (%s motors, player %d's) at link tick %lld\n", local, device + 1,
                     whose, owner, (long long) NetplayLinkTick());
    } else {
        std::fprintf(stderr, "coop-rumble: copy of p%d: controller %d stops at link tick %lld\n", local, device + 1, (long long) NetplayLinkTick());
    }
}

} // namespace

void CoopRumblePartner(int motor, int intensity, int duration) {
    if (motor < 0 || motor >= 2 || duration < 0 || duration > 1200) {
        return;
    }

    g_partner.timer[motor] = duration;
    g_partner.level[motor] = (unsigned char) (motor == 0 ? intensity != 0 : intensity);
}

void CoopRumbleSend(const unsigned char *game, int enabled) {
    // The partner's timers, as CGamePad::Step steps the game's.
    for (int motor = 0; motor < 2; motor++) {
        if (!enabled) {
            g_partner.level[motor] = 0;
        }

        if (g_partner.timer[motor] > 0) {
            g_partner.timer[motor]--;
        } else {
            g_partner.timer[motor] = 0;
            g_partner.level[motor] = 0;
        }
    }

    const int local = NetplayLocalPlayer();

    // Not linked (single player, a copy waiting for its partner): one player, the game's walker, as retail.
    if (local == 0) {
        scePadSetActDirect(0, 0, const_cast<unsigned char *>(game));
        return;
    }

    const int walker = WalkerPlayer(local);

    if (coop::Mutated("rumble_retail")) {
        const bool shaking = game[0] != 0 || game[1] != 0;

        Say(local, 0, shaking, "the game's", walker);
        g_device = 0;
        g_shaking = shaking;
        scePadSetActDirect(0, 0, const_cast<unsigned char *>(game));
        return;
    }

    // This copy's player's motors: the game's when it plays the walker, else the partner's (the other player's body).
    const bool           own_walker = walker == local;
    const unsigned char *mine = own_walker ? game : g_partner.level;
    const int            device = NetplayOwnPad();
    const bool           shaking = device >= 0 && (mine[0] != 0 || mine[1] != 0);

    // A controller this copy no longer plays is let go (never another player's: only the one this copy shook).
    if (g_device >= 0 && g_device != device) {
        InputSetRumble(g_device, {});
    }

    Say(local, device, shaking, own_walker ? "the game's" : "the partner's", own_walker ? walker : 3 - walker);
    g_device = device;
    g_shaking = shaking;

    if (device >= 0) {
        InputSetRumble(device, {mine[0] != 0, mine[1]});
    }
}
