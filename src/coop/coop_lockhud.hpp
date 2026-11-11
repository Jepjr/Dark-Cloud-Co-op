#pragma once

#include "coop/game_ns.hpp"

// coop-lockhud
// The lock-on marker, the locked monster's name and its life bar on each linked copy follow that copy's own player's
// lock (Jep, 2026-10-25: "Enemy lock on still shows up for both players no matter the distance", "P2 sees the name
// above the enemy that it does for P1 even if P2 is locked onto an enemy near them"). The player's lock is retail's
// (setTargetCursor, run alike on both copies: it keeps or lets go of the lock and plays its sounds); the partner's is
// coop_combat's g_combat.lock. On the copy whose player is P2 her name and bar are drawn and the player's are not; the
// other player's corners are drawn dim, and only while that monster is near this copy's own player (within its lock
// range). Draw only: nothing here changes what the tick reads. Couch co-op (one copy, two pads) is as before.
// DC_COOP_MUTATE=lockhud: as before (the player's name and bar on both copies, both players' corners at any distance).

namespace coop {

// DunMainDraw, round setTargetCursor(lockOnTargetFlag): Begin before it, End after it and its nearest-monster marker.
void LockHudBegin();
void LockHudEnd();

// Whether the player's nearest-monster marker (cursorFrame, shown while he has no lock) draws on this copy.
bool LockHudPlayerPreview();

// The alpha the partner's lock corners draw at on this copy (0: not drawn). CoopDrawShots asks for monster.
int LockHudPartnerCursor(int monster);

// The corners of a lock-on cursor round world (DrawtargetCursor's four sprites of d01e04), tinted teal or retail's.
void DrawLockCorners(float *world, float width, float height, int alpha, bool teal);

// coop-lockcolour: whether player 2's bar and corners draw teal (player 1's are always retail's).
bool LockHudP2Teal();

// What the last finished frame showed (-1: nothing): the monster whose name and bar were drawn, and the monsters the
// player's and the partner's corners and the player's nearest-monster marker were drawn round. For the lockhud check.
struct LockHudShown {
    int name = -1;
    int bar = -1;
    int player_marker = -1;
    int partner_marker = -1;
    int preview = -1;
    // coop-lockcolour: whether the bar and each player's corners were drawn teal (player 2's colour).
    bool bar_teal = false;
    bool player_teal = false;
    bool partner_teal = false;
};

const LockHudShown &LockHudLastShown();

} // namespace coop
