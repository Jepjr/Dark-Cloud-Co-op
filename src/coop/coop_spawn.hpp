#pragma once

#include "coop/game_ns.hpp"

#include <cstdint>

// coop-spawn: two spots at every arrival (Jep, 2026-10-24). A character the game puts at an area's entrance takes spot 1,
// the game's own arrival point; when the other player already stands there, it takes spot 2, one step to the side. Two
// arriving at once never both take spot 1: player 1 takes it and player 2 stands beside (the partner's own placement).
//
// A merge of the two worlds (coop_worlds.cpp) keeps each player where their own copy had them: player 2's copy tells the
// host where she stands (its presence), the host takes that as the stand both copies use (a catch-up's recording, or the
// resync's state), and the town partner's load puts her there instead of beside Toan. If one stands on the other, the one
// who arrived last moves to spot 2 of its arrival.

namespace coop {

// How far spot 2 lies beside spot 1, and how near two characters count as on one spot (units).
constexpr float kSpawnSpot = 14.0f;
constexpr float kSpawnOccupied = 12.0f;
// coop-soakfix3: a join's stand taken inside a town event (his arrival walk-out from a dungeon: Toan mid-way on its gate's ground): no
// spot yet; she goes to spot 2 of Toan's stand on the first walk after the event's end, on every copy (CoopTownStandAfterEvent).
constexpr int kSpawnAfterEvent = 3;
// coop-localfix: a stand inside one of the town's buildings has this bit on its map (the room's merge places her by it, not the town's load).
constexpr int kIndoorStand = 0x80;

struct SpawnStand {
    bool  set = false;
    int   map = -1;     // the town (MapNo) it is for
    int   arriver = 0;  // the player who arrived last there (1 or 2): it moves when the two stand on one spot; kSpawnAfterEvent: above
    float pos[3] = {};
    float yaw = 0.0f;
};

// Player 2's stand as the host takes it at a merge, packed for a catch-up's recording (what, a, b, c, d) and back.
void SpawnPack(const SpawnStand &stand, std::int32_t what, std::int32_t delta[5]);
SpawnStand SpawnUnpack(const std::int32_t delta[5]);

// Where player 2 is to stand after the merge (both copies alike), and taking it once (a town's partner load on that map).
void SpawnSetStand(const SpawnStand &stand, const char *from);
const SpawnStand &SpawnStandNow();
bool SpawnTakeStand(int map, SpawnStand *out);
// coop-localfix: the same without taking it (a merge's placing on both copies, before the town load takes it).
bool SpawnPeekStand(int map, SpawnStand *out);
void SpawnClearStand();

// coop-mergestand (Jep, 2026-10-25: "the game refreshed both players back to the town gate"): where player 1 (Toan) is to
// stand after a town resync's load, the host's spot at the resync tick, carried in the resync's state with her stand,
// so both copies put him back there instead of the town's arrival. Taken once, by the town partner's load on that map.
struct SpawnToan {
    bool  set = false;
    int   map = -1;
    float pos[3] = {};
    float yaw = 0.0f;
};

void SpawnSetToan(const SpawnToan &stand, const char *from);
bool SpawnTakeToan(int map, SpawnToan *out);
void SpawnClearToan();

// Whether two characters stand on one spot.
bool SpawnOccupied(const float *a, const float *b);

// Spot 2 of an arrival at spot (facing yaw): kSpawnSpot to its right or left, whichever is farther from the one in the way.
void SpawnSecondSpot(const float *spot, float yaw, const float *other, float *out);

} // namespace coop
