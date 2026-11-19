#include "coop/game_ns.hpp"
#include "coop/coop_rumble.hpp"

#include "coop/coop_actor.hpp"
#include "coop/coop_audio.hpp"
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

Motors        g_motors[coop::kMaxPlayers]; // each player's own motors but the walker's game pad (every copy alike; not game state)
int           g_device = -1;               // the controller this copy last shook, -1 none
unsigned char g_said[2] = {0, 0};          // what the log last said was sent (and to which controller)
int           g_said_device = -2;
int           g_said_owner = 0;

// The player the game's own walker (Chara, CharaMain) is on this copy: player 1 together; apart, this copy's player;
// while her town event runs on the game's pad (CoopWorldsP2Drives), player 2.
int WalkerPlayer(int local) {
    if (CoopWorldsP2Drives()) {
        return 2;
    }

    return CoopWorldsApart() ? local : 1;
}

// The send's line, when what goes out (or where) changes.
void Say(int local, int device, int owner, const unsigned char *sent) {
    if (sent[0] == g_said[0] && sent[1] == g_said[1] && device == g_said_device && owner == g_said_owner) {
        return;
    }
    g_said[0] = sent[0];
    g_said[1] = sent[1];
    g_said_device = device;
    g_said_owner = owner;
    std::fprintf(stderr, "coop-rumble: copy of p%d: player %d's motors (%d %d) to controller %d at link tick %lld\n", local, owner, sent[0], sent[1],
                 device + 1, (long long) NetplayLinkTick());
}

} // namespace

void CoopRumbleFor(int player, int motor, int intensity, int duration) {
    if (player < 1 || player > coop::kMaxPlayers || motor < 0 || motor >= 2 || duration < 0 || duration > 1200) {
        return;
    }

    Motors &m = g_motors[player - 1];

    m.timer[motor] = duration;
    m.level[motor] = (unsigned char) (motor == 0 ? intensity != 0 : intensity);
}

void CoopRumbleNote(bool game, int motor, int intensity, int duration) {
    const int owner = coop::SoundOwnerPlayer();

    if (!game || owner == 0 || coop::Mutated("rumble_owner_off")) {
        return;
    }

    CoopRumbleFor(owner, motor, intensity, duration);
}

void CoopRumbleStopAll() {
    for (Motors &m : g_motors) {
        m = Motors{};
    }
}

void CoopRumbleSend(const unsigned char *game, int enabled) {
    // Every player's timers, as CGamePad::Step steps the game's.
    for (Motors &m : g_motors) {
        for (int motor = 0; motor < 2; motor++) {
            if (!enabled) {
                m.level[motor] = 0;
            }

            if (m.timer[motor] > 0) {
                m.timer[motor]--;
            } else {
                m.timer[motor] = 0;
                m.level[motor] = 0;
            }
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
        Say(local, 0, walker, game);
        g_device = 0;
        scePadSetActDirect(0, 0, const_cast<unsigned char *>(game));
        return;
    }

    // This copy's player's motors: the game's when it plays the walker, and their own from here (their own steps' and hits'),
    // the stronger of the two. Never another player's.
    const bool    own_walker = walker == local;
    const Motors &own = g_motors[(local - 1) % coop::kMaxPlayers];
    unsigned char mine[2];

    for (int motor = 0; motor < 2; motor++) {
        const unsigned char from_game = own_walker && enabled ? game[motor] : 0;

        mine[motor] = from_game > own.level[motor] ? from_game : own.level[motor];
    }

    const int device = NetplayOwnPad();

    // A controller this copy no longer plays is let go (never another player's: only the one this copy shook).
    if (g_device >= 0 && g_device != device) {
        InputSetRumble(g_device, {});
    }

    Say(local, device, local, mine);
    g_device = device;

    if (device >= 0) {
        InputSetRumble(device, {mine[0] != 0, mine[1]});
    }
}
