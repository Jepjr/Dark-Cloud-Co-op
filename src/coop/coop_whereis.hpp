#pragma once

// coop-whereis (Jep, 2026-11-12): where is everyone. Each player sees where every other player is: a town, a house in it, a
// dungeon's entrance or floor, and what they do there (fishing, the Georama, a scene, a talk). Wanted because a joiner loads
// into town on their own (join-as-load), so players are often apart.
//
// The data is the real work; the drawing is a PLACEHOLDER (Jep 11-12: the real UI is designed later), one function
// (DrawWhereIs) behind one switch (game.coop_whereis, on): a short list at the top right, one line a player in their colour.
//
// Where each copy gets it, nothing in lockstep or the checksum, nothing waited on:
//   - its own player: the copy's own zone (coop_worlds' Here) and what it does now (CoopWorldsOwnActivity);
//   - a player together with it (the same map, in lockstep): the same zone, "here", and that player's own fishing;
//   - a player apart: the worlds presence that player's copy already sends twice a second while apart (its zone and busy
//     word, plus one activity byte), kept per player (CoopWorldsWhere).
// A table of kMaxPlayers, entry k player k + 1: never "the other player".

#include <cstddef>
#include <cstdint>

namespace coop {

enum class WhereKind : std::uint8_t {
    Unknown, // not heard yet
    Away,    // a load, the title, between places
    Town,    // a town's outside, or (room != 0) a building in it
    Dungeon, // a dungeon: floor >= 0 a floor (0 up), -1 its entrance (the floor menu), -2 on the way to a floor
};

enum WhereActivity : int {
    kWhereNone = 0,
    kWhereScene = 1,   // an event or a cutscene
    kWhereGeorama = 2, // building in the town's Georama
    kWhereFishing = 3,
    kWhereTalk = 4, // talking to someone in a building
};

// One player's place as this copy knows it.
struct Where {
    WhereKind kind = WhereKind::Unknown;
    int       map = -1;   // the town (MapNo) or the dungeon (selectMapNo)
    int       floor = -1; // Dungeon: as WhereKind says
    int       room = 0;   // Town: the building's number (CoopWorldsInteriorId of its name), 0 outdoors
    int       activity = kWhereNone;
    bool      together = false; // in lockstep with this copy, on its map
    bool      stale = false;    // apart and not heard for 30 s: where it was last
};

// The place's words: "Norune", "Norune, the mayor's house", "Divine Beast Cave, floor 3", "Wise Owl Forest entrance", with
// " - fishing" and the like after; "here" when together; "last seen: ..." when stale; "?" not heard yet.
void WhereText(const Where &where, char *out, std::size_t size);

// One line of the list: whose (1 up), its words, its colour.
struct WhereLine {
    int           player = 0;
    char          text[96] = "";
    unsigned char rgb[3] = {255, 255, 255};
    bool          dim = false; // together: "here", drawn dimmer
};

// The list player viewer sees: a line for every other player in table[0..count-1] (entry k player k + 1) that is in the
// session (table[k].kind != Unknown or heard), in player order; its own entry never. Returns the number of lines.
// DC_COOP_MUTATE=whereis_peer_as_self (twin): each line from the viewer's own entry (the bug it guards: yourself shown as them).
int WhereLines(int viewer, const Where *table, int count, WhereLine *out, int max);

// Player n's colour (1 up): 1 pink (retail's arrow), 2 teal (coop-uinotice's), 3 yellow, 4 green, 5 orange, 6 violet.
void WhereColour(int player, unsigned char rgb[3]);
const char *WhereColourName(int player);

// game.coop_whereis, on unless the config says false.
bool WhereIsSetting();

// Its own player's line excluded, the list this copy shows now (from CoopWorldsWhere); 0 alone (not linked).
int WhereIsNow(WhereLine *out, int max);

// THE placeholder: the list at the top right of this player's screen. Drawn only while the switch is on, the copy is linked
// (2 players or more) and this player is free (walking a town or a room, playing a floor: no menu, talk window, scene, the
// entrance's floor menu, the Georama, fishing, a question): never over the game's own UI, nothing in single player.
// Called with the town's late 2D (CoopP2TownMenuDraw) and the floor's HUD (CoopDrawHud). The real UI replaces this function.
void DrawWhereIs();

// coop-whereisz: the draw order of the list against the game's HUD in the dungeon's HUD pass, for the test (DC_COOP_TEST_WHEREIS).
// WhereIsHudBegin opens a pass, WhereIsHudDrawn is called as the game's top status (the life/quick-item panel) is drawn; the
// list stamps itself in DrawWhereIs. Each pass says "list K, game HUD M: UNDER|OVER" (a smaller number is drawn first = under).
void WhereIsHudBegin();
void WhereIsHudDrawn();

// Each frame (NetplayFrameStart): the test hooks. DC_COOP_TEST_WHEREIS=log: a line each time the list this copy holds changes
// ("coop-whereis: copy of p1 holds: P2 here") or its drawing does; =table: once a state file is loaded, the text table's
// cases (2 and 3 players), "coop-whereis: table: N of M right", and this copy's own list and drawing.
void WhereIsFrame();

} // namespace coop
