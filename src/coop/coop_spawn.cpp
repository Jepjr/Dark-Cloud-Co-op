#include "coop/game_ns.hpp"
#include "coop/coop_spawn.hpp"

#include <cmath>
#include <cstdio>

namespace coop {

namespace {

SpawnStand g_stand;

} // namespace

// Whole units for the position and thousandths for the yaw, so the recording holds what both copies then use.
void SpawnPack(const SpawnStand &stand, std::int32_t what, std::int32_t delta[5]) {
    delta[0] = what;
    delta[1] = (std::int32_t) std::lround(stand.pos[0]);
    delta[2] = (std::int32_t) std::lround(stand.pos[1]);
    delta[3] = (std::int32_t) std::lround(stand.pos[2]);
    // The map (below 256), the arriver (1 or 2) and the yaw in thousandths of a radian (within plus or minus 7000).
    delta[4] = (stand.map & 0xff) | ((stand.arriver & 0x3) << 8) | ((std::int32_t) std::lround(stand.yaw * 1000.0f) + 8192) << 10;
}

SpawnStand SpawnUnpack(const std::int32_t delta[5]) {
    SpawnStand stand;

    stand.set = true;
    stand.pos[0] = (float) delta[1];
    stand.pos[1] = (float) delta[2];
    stand.pos[2] = (float) delta[3];
    stand.map = delta[4] & 0xff;
    stand.arriver = (delta[4] >> 8) & 0x3;
    stand.yaw = (float) ((delta[4] >> 10) - 8192) / 1000.0f;
    return stand;
}

void SpawnSetStand(const SpawnStand &stand, const char *from) {
    g_stand = stand;
    std::fprintf(stderr, "spawn: player 2 is to stand at (%.0f %.0f %.0f) in town %d after the merge, player %d arrived last (%s)\n", stand.pos[0],
                 stand.pos[1], stand.pos[2], stand.map, stand.arriver, from);
    std::fflush(stderr);
}

const SpawnStand &SpawnStandNow() {
    return g_stand;
}

bool SpawnTakeStand(int map, SpawnStand *out) {
    if (!g_stand.set || g_stand.map != map) {
        return false;
    }

    *out = g_stand;
    g_stand.set = false;
    return true;
}

bool SpawnPeekStand(int map, SpawnStand *out) {
    if (!g_stand.set || g_stand.map != map) {
        return false;
    }

    *out = g_stand;
    return true;
}

void SpawnClearStand() {
    g_stand.set = false;
}

SpawnToan g_toan; // coop-mergestand

void SpawnSetToan(const SpawnToan &stand, const char *from) {
    g_toan = stand;
    std::fprintf(stderr, "spawn: player 1 is to stand at (%.0f %.0f %.0f) in town %d after the resync (%s)\n", stand.pos[0], stand.pos[1],
                 stand.pos[2], stand.map, from);
    std::fflush(stderr);
}

bool SpawnTakeToan(int map, SpawnToan *out) {
    if (!g_toan.set || g_toan.map != map) {
        return false;
    }

    *out = g_toan;
    g_toan.set = false;
    return true;
}

void SpawnClearToan() {
    g_toan.set = false;
}

bool SpawnOccupied(const float *a, const float *b) {
    const float dx = a[0] - b[0];
    const float dz = a[2] - b[2];

    return dx * dx + dz * dz < kSpawnOccupied * kSpawnOccupied && std::fabs(a[1] - b[1]) < 3.0f * kSpawnOccupied;
}

void SpawnSecondSpot(const float *spot, float yaw, const float *other, float *out) {
    // The right hand as the partner's own placement takes it (PlaceBesideToan): (-cos, sin) of the yaw.
    const float sx = -kSpawnSpot * std::cos(yaw);
    const float sz = kSpawnSpot * std::sin(yaw);
    const float right[3] = {spot[0] + sx, spot[1], spot[2] + sz};
    const float left[3] = {spot[0] - sx, spot[1], spot[2] - sz};
    const float dr = (right[0] - other[0]) * (right[0] - other[0]) + (right[2] - other[2]) * (right[2] - other[2]);
    const float dl = (left[0] - other[0]) * (left[0] - other[0]) + (left[2] - other[2]) * (left[2] - other[2]);
    const float *pick = dl > dr ? left : right;

    out[0] = pick[0];
    out[1] = pick[1];
    out[2] = pick[2];
}

} // namespace coop
