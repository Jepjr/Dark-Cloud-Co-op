#include "coop/game_ns.hpp"
#include "coop/coop_whereis.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "battlemenu/internal.hpp" // DecodeMesFromBuffer (coop-whereishouse: the Georama's part names)
#include "data/data_read.hpp"
#include "dataread.hpp"  // LoadFile2, GetPackFile
#include "editatra.hpp"  // GetEditAtraPartsData
#include "editpartsdata.hpp" // EDIT_PARTS_ATRA
#include "mainselect.hpp" // LanguageCode
#include "coop/coop_actor.hpp" // kMaxPlayers
#include "coop/coop_core.hpp"  // Mutated, TestEnv
#include "coop/coop.hpp"
#include "coop/coop_worlds.hpp"
#include "coop/netplay.hpp"
#include "gfx/gfx.hpp" // UiAnchorScope: the list placed in the whole frame
#include "platform/config.hpp"
#include "text/caption_text.hpp"

// coop-whereis (see coop_whereis.hpp). Written by Jep.

namespace coop {

namespace {

const char *TownName(int map) {
    const char *name = CoopWorldsTownName(map);

    return name != nullptr ? name : "a town";
}

// coop-whereishouse (Jep's rc1 playtest: the list said "a house" for every house but the mayor's). A room is named as the
// Georama names its building: each town's mapinfo.cfg says which building part (BLD_PARTS / ON_RIVER_PARTS n) a room's door
// (MAPJUMP "i01h02") hangs off, and the part's name is the first line of its message in the Georama menu's atrames.bin
// (emenu.pak): message 1000 + 200 * town + the part's msg_no (GetEditAtraPartsData), e.g. "Macho's House". A room whose
// building is no Georama part is named by its resident (the mayor: editsys.bin villager 13) or by hand. Keyed by
// CoopWorldsInteriorId(room name), the id a record carries.
struct HouseName {
    int         id;
    char        room[16];
    int         town; // 0 to 4
    int         part; // -1: a building that is no Georama part (the mayor's house, ...)
    std::string name; // "" none
};

// The rooms with no Georama part and no villager text of their own, kept apart: the Muska Racka item shop (the game's
// e04talk message 240: "This is Odd Gaffer's Item Shop.").
struct Hand {
    const char *room;
    const char *text;
};

const Hand kHand[] = {
    {"i04h09", "Odd Gaffer's Item Shop"},
};

// The first line of a message, its quotation marks off: "\"Macho's House\"|..." is Macho's House.
std::string FirstLine(const std::string &text) {
    std::string line = text.substr(0, text.find('\n'));

    if (line.size() >= 2 && line.front() == '"' && line.back() == '"') {
        line = line.substr(1, line.size() - 2);
    }
    return line;
}

// The Georama's name for a town's building part: its message in atrames.bin, the first line.
std::string GeoramaPartName(short *georama, int town, int part) {
    const EDIT_PARTS_ATRA *atra = GetEditAtraPartsData(town, part);

    return atra != nullptr ? FirstLine(dc::battlemenu::detail::DecodeMesFromBuffer(georama, 1000 + 200 * town + atra->msg_no)) : std::string();
}

std::vector<HouseName> &Houses() {
    static std::vector<HouseName> houses;
    static bool                   built = false;

    if (built) {
        return houses;
    }
    built = true;

    // The villagers' names (the mayor's room): editsys.bin; the Georama's part names: atrames.bin in the menu pack of the
    // game's language (the dir a_eng here).
    std::vector<short> system((16u << 20) / sizeof(short));
    int                got = 0;
    const bool         have_names = LoadFile2("gedit/system/editsys.bin", system.data(), &got, 0) != 0;
    const char *const        kLanguageDirs[] = {"a_jpn", "a_usa", "a_eng", "a_fre", "a_ger", "a_ita", "a_spa"};
    std::vector<u_int>       pack((8u << 20) / sizeof(u_int));
    short                   *georama = nullptr;

    for (const char *dir : {LanguageCode >= 0 && LanguageCode < 7 ? kLanguageDirs[LanguageCode] : "a_eng", "a_eng"}) {
        char path[64];

        std::snprintf(path, sizeof path, "commenu/%s/emenu.pak", dir);
        if (LoadFile2(path, pack.data(), &got, 0) != 0) {
            georama = reinterpret_cast<short *>(GetPackFile(pack.data(), (char *) "atrames.bin", nullptr));
            if (georama != nullptr) {
                break;
            }
        }
    }

    for (int town = 0; town < 5; town++) {
        char path[64];

        std::snprintf(path, sizeof path, "gedit/e%02d/mapinfo.cfg", town + 1);

        const std::uintmax_t size = dc::data::DataFileSize(path);

        if (size == 0 || size > (1u << 20)) {
            continue;
        }

        std::vector<char> text(((std::size_t) size / 2048 + 2) * 2048, 0);

        if (LoadFile2(path, text.data(), &got, 0) == 0) {
            continue;
        }

        int         part = -1;
        std::size_t at = 0;

        while (at < (std::size_t) got) {
            std::size_t end = at;

            while (end < (std::size_t) got && text[end] != '\n') {
                end++;
            }

            const std::string line(&text[at], end - at);
            std::size_t       k = line.find_first_not_of(" \t");

            at = end + 1;
            if (k == std::string::npos) {
                continue;
            }

            int         n = 0;
            char        room[16] = "";
            const char *s = line.c_str() + k;
            const char *word_end = s;

            while (*word_end >= 'A' && *word_end <= 'Z' || *word_end == '_') {
                word_end++;
            }

            const std::string word(s, word_end);

            if (word.size() > 5 && word.compare(word.size() - 5, 5, "PARTS") == 0 && std::sscanf(word_end, " %d", &n) == 1) {
                part = n; // BLD_PARTS n, ON_RIVER_PARTS n: the building part the lines after it belong to
            } else if (word == "MAPJUMP" && std::sscanf(word_end, " %*d , \"%15[^\"]\"", room) == 1) {
                HouseName h{};

                std::snprintf(h.room, sizeof h.room, "%s", room);
                h.id = CoopWorldsInteriorId(room);
                h.town = town;
                h.part = part;
                if (part >= 0 && georama != nullptr) {
                    h.name = GeoramaPartName(georama, town, part);
                } else if (part < 0 && have_names && std::strcmp(room, "i01h06") == 0) {
                    h.name = dc::battlemenu::detail::DecodeMesFromBuffer(system.data(), 1013) + "'s House"; // villager 13, "Mayor"
                }
                for (const Hand &hand : kHand) {
                    if (std::strcmp(hand.room, room) == 0) {
                        h.name = hand.text;
                    }
                }
                houses.push_back(h);
            }
        }
    }
    return houses;
}

const char *RoomName(int room) {
    static char out[80];
    if (!Mutated("whereis_house_names_off")) {
        for (const HouseName &h : Houses()) {
            if (h.id == room && !h.name.empty()) {
                std::snprintf(out, sizeof out, "%s", h.name.c_str());
                return out;
            }
        }
    }
    return "a house";
}

const char *ActivityText(int activity) {
    switch (activity) {
    case kWhereScene:
        return " - in a scene";
    case kWhereGeorama:
        return " - Georama";
    case kWhereFishing:
        return " - fishing";
    case kWhereTalk:
        return " - talking";
    default:
        return "";
    }
}

} // namespace

void WhereText(const Where &where, char *out, std::size_t size) {
    char place[80];

    if (where.together) {
        std::snprintf(out, size, "here%s", ActivityText(where.activity));
        return;
    }
    switch (where.kind) {
    case WhereKind::Unknown:
        std::snprintf(out, size, "?");
        return;
    case WhereKind::Away:
        std::snprintf(place, sizeof place, "on the way");
        break;
    case WhereKind::Town:
        if (where.room != 0) {
            std::snprintf(place, sizeof place, "%s, %s", TownName(where.map), RoomName(where.room));
        } else {
            std::snprintf(place, sizeof place, "%s", TownName(where.map));
        }
        break;
    case WhereKind::Dungeon:
        if (where.floor >= 0) {
            std::snprintf(place, sizeof place, "%s, floor %d", CoopWorldsDungeonName(where.map), where.floor + 1);
        } else if (where.floor == -1) {
            std::snprintf(place, sizeof place, "%s entrance", CoopWorldsDungeonName(where.map));
        } else {
            std::snprintf(place, sizeof place, "on the way to %s", CoopWorldsDungeonName(where.map));
        }
        break;
    }
    std::snprintf(out, size, "%s%s%s", where.stale ? "last seen: " : "", place, where.stale ? "" : ActivityText(where.activity));
}

void WhereColour(int player, unsigned char rgb[3]) {
    static const unsigned char kColours[kMaxPlayers][3] = {
        {244, 142, 226}, // 1 pink: retail's own arrow
        {0x10, 0xFF, 0xC0}, // 2 teal: CoopMiniMapOtherColour's
        {255, 220, 60},  // 3 yellow
        {90, 220, 90},   // 4 green
        {255, 150, 40},  // 5 orange
        {170, 120, 255}, // 6 violet
    };
    const int k = player >= 1 && player <= kMaxPlayers ? player - 1 : 0;

    rgb[0] = kColours[k][0];
    rgb[1] = kColours[k][1];
    rgb[2] = kColours[k][2];
}

const char *WhereColourName(int player) {
    static const char *const kNames[kMaxPlayers] = {"pink", "teal", "yellow", "green", "orange", "violet"};

    return player >= 1 && player <= kMaxPlayers ? kNames[player - 1] : "pink";
}

int WhereLines(int viewer, const Where *table, int count, WhereLine *out, int max) {
    const bool as_self = Mutated("whereis_peer_as_self");
    int        lines = 0;

    for (int k = 0; k < count && lines < max; k++) {
        const int player = k + 1;

        if (player == viewer) {
            continue;
        }

        Where where = as_self && viewer >= 1 && viewer <= count ? table[viewer - 1] : table[k];
        // (the twin's own entry is never "together" with itself: as the bug drew it, its own place)
        WhereLine &line = out[lines++];

        line.player = player;
        WhereText(where, line.text, sizeof line.text);
        WhereColour(player, line.rgb);
        line.dim = where.together;
    }
    return lines;
}

bool WhereIsSetting() {
    return dc::platform::ConfigGet().coop_whereis;
}

int WhereIsNow(WhereLine *out, int max) {
    const int players = CoopWorldsPlayers();

    if (players < 2) {
        return 0;
    }

    Where table[kMaxPlayers];

    for (int k = 0; k < players && k < kMaxPlayers; k++) {
        if (!CoopWorldsWhere(k + 1, &table[k])) {
            table[k] = Where{};
        }
    }
    return WhereLines(CoopWorldsSelf(), table, players < kMaxPlayers ? players : kMaxPlayers, out, max);
}

namespace {

// Whether the list may be drawn on this screen now, and if not why (for the log).
bool Shown(const char **why) {
    if (!WhereIsSetting()) {
        *why = "game.coop_whereis is off";
        return false;
    }
    if (CoopWorldsPlayers() < 2) {
        *why = "single player";
        return false;
    }
    if (!CoopWorldsWhereIsFree(why)) {
        return false;
    }
    return true;
}

struct DrawLog {
    std::string said;
};
DrawLog g_draw_log;

void SayDrawn(const std::string &now) {
    static const bool log = TestEnv("DC_COOP_TEST_WHEREIS") != nullptr;

    if (!log || now == g_draw_log.said) {
        return;
    }
    g_draw_log.said = now;
    std::fprintf(stderr, "coop-whereis: copy of p%d %s\n", CoopWorldsSelf(), now.c_str());
    std::fflush(stderr);
}

} // namespace

namespace {

struct HudPass {
    bool pass = false;
    int  seq = 0, list = 0, hud = 0;
};
HudPass g_pass;

void SayPass() {
    static const bool log = TestEnv("DC_COOP_TEST_WHEREIS") != nullptr;

    if (!g_pass.pass || g_pass.list == 0 || g_pass.hud == 0) {
        return;
    }
    g_pass.pass = false;
    if (!log) {
        return;
    }
    std::fprintf(stderr, "coop-whereis: copy of p%d draw order: list %d, game HUD %d: %s\n", CoopWorldsSelf(), g_pass.list, g_pass.hud,
                 g_pass.list < g_pass.hud ? "UNDER" : "OVER");
    std::fflush(stderr);
}

} // namespace

void WhereIsHudBegin() {
    g_pass = HudPass{};
    g_pass.pass = true;
}

void WhereIsHudDrawn() {
    if (g_pass.pass && g_pass.hud == 0) {
        g_pass.hud = ++g_pass.seq;
        SayPass();
    }
}

void DrawWhereIs() {
    const char *why = "";

    if (!Shown(&why)) {
        SayDrawn(std::string("draws nothing: ") + why);
        return;
    }

    WhereLine lines[kMaxPlayers];
    const int count = WhereIsNow(lines, kMaxPlayers);

    if (count == 0) {
        SayDrawn("draws nothing: no other player");
        return;
    }

    // The placeholder: at the right, a line a player, in the whole frame (not under the floor HUD's anchor), 640 x 448 units.
    // Its top under the game's own top-right pieces (Jep's screenshots 11-12: the town's clock dial ends near 92, a floor's
    // "Floor N" badge and gate key near 62), each line's right edge at 624 (from the width it measured last frame).
    gfx::UiAnchorScope whole{gfx::UiAnchor{}};
    constexpr float    kRight = 640.0f - 16.0f;
    constexpr float    kTop = 100.0f;
    constexpr float    kStep = 13.0f;
    static std::string measured[kMaxPlayers];
    static float       width[kMaxPlayers] = {};
    std::string        said = "draws " + std::to_string(count) + " line(s) at the top right:";

    for (int i = 0; i < count; i++) {
        dc::text::caption::LineStyle style;
        char                         text[112];

        style.status_font = true;
        style.edge_width = 1.5f;
        style.fill[0] = lines[i].rgb[0];
        style.fill[1] = lines[i].rgb[1];
        style.fill[2] = lines[i].rgb[2];
        style.alpha = lines[i].dim ? 0.6f : 1.0f;
        std::snprintf(text, sizeof text, "P%d  %s", lines[i].player, lines[i].text);
        dc::text::caption::LineBounds bounds;
        const float                  w = measured[i] == text ? width[i] : 120.0f;

        dc::text::caption::DrawLine(text, kRight - w / 2.0f, kTop + kStep * (float) i, 9.0f, 1.0f, 200.0f, style, 1.0f, 1.0f, &bounds);
        measured[i] = text;
        width[i] = bounds.right > bounds.left ? bounds.right - bounds.left : w;
        said += std::string(" [") + text + "]";
    }
    SayDrawn(said);
    if (g_pass.pass && g_pass.list == 0) {
        g_pass.list = ++g_pass.seq;
        SayPass();
    }
}

namespace {

struct Case {
    const char *name;
    int         viewer;
    int         count;
    Where       table[3];
    const char *want; // the lines, "P<n> (<colour>) <text>" joined by " | "
};

std::string Joined(const WhereLine *lines, int count) {
    std::string s;

    for (int i = 0; i < count; i++) {
        char one[128];

        std::snprintf(one, sizeof one, "%sP%d (%s) %s", i > 0 ? " | " : "", lines[i].player, WhereColourName(lines[i].player), lines[i].text);
        s += one;
    }
    return s;
}

Where Town(int map, int room = 0, int activity = kWhereNone) {
    Where w;

    w.kind = WhereKind::Town;
    w.map = map;
    w.room = room;
    w.activity = activity;
    return w;
}

Where Dungeon(int map, int floor, int activity = kWhereNone) {
    Where w;

    w.kind = WhereKind::Dungeon;
    w.map = map;
    w.floor = floor;
    w.activity = activity;
    return w;
}

Where Together(Where w, int activity = kWhereNone) {
    w.together = true;
    w.activity = activity;
    return w;
}

Where Stale(Where w) {
    w.stale = true;
    return w;
}

// The text table: what each copy holds for every other player, both orientations, apart and together, and three players.
void RunTable() {
    const int mayor = CoopWorldsInteriorId("i01h06");
    const int other = CoopWorldsInteriorId("i01h01");
    const Case cases[] = {
        {"town, P1 sees P2", 1, 2, {Town(0), Town(0)}, "P2 (teal) Norune"},
        {"town, P2 sees P1", 2, 2, {Town(1), Dungeon(0, 0)}, "P1 (pink) Matataki"},
        {"house, P1 sees P2", 1, 2, {Dungeon(0, 2), Town(0, mayor)}, "P2 (teal) Norune, Mayor's House"},
        {"house, P2 sees P1", 2, 2, {Town(0, other), Town(0)}, "P1 (pink) Norune, My House"},
        {"floor N, P1 sees P2", 1, 2, {Town(1), Dungeon(1, 0)}, "P2 (teal) Wise Owl Forest, floor 1"},
        {"floor N, P2 sees P1", 2, 2, {Dungeon(0, 2), Town(0)}, "P1 (pink) Divine Beast Cave, floor 3"},
        {"entrance", 1, 2, {Town(0), Dungeon(1, -1)}, "P2 (teal) Wise Owl Forest entrance"},
        {"merged, P1 sees P2", 1, 2, {Town(0), Together(Town(0))}, "P2 (teal) here"},
        {"merged, P2 sees P1", 2, 2, {Together(Dungeon(0, 2)), Dungeon(0, 2)}, "P1 (pink) here"},
        {"merged, fishing", 2, 2, {Together(Town(0), kWhereFishing), Town(0)}, "P1 (pink) here - fishing"},
        {"apart, fishing", 1, 2, {Dungeon(0, 0), Town(0, 0, kWhereFishing)}, "P2 (teal) Norune - fishing"},
        {"apart, Georama", 2, 2, {Town(1, 0, kWhereGeorama), Town(0)}, "P1 (pink) Matataki - Georama"},
        {"apart, scene", 1, 2, {Town(1), Town(0, mayor, kWhereScene)}, "P2 (teal) Norune, Mayor's House - in a scene"},
        {"not heard for 30 s", 1, 2, {Town(1), Stale(Town(0))}, "P2 (teal) last seen: Norune"},
        {"not heard yet", 2, 2, {Where{}, Town(0)}, "P1 (pink) ?"},
        // Three players (design for three): every other player, in player order, never the viewer.
        {"3 players, P1 sees", 1, 3, {Town(0), Town(0, mayor), Dungeon(2, 3)}, "P2 (teal) Norune, Mayor's House | P3 (yellow) Shipwreck, floor 4"},
        {"3 players, P2 sees", 2, 3, {Together(Town(0)), Town(0), Dungeon(2, 3)}, "P1 (pink) here | P3 (yellow) Shipwreck, floor 4"},
        {"3 players, P3 sees", 3, 3, {Town(0), Town(0, mayor), Dungeon(2, 3)}, "P1 (pink) Norune | P2 (teal) Norune, Mayor's House"},
    };
    int right = 0;
    int total = 0;

    // coop-whereishouse: every room the towns' doors lead to, read from the game's mapinfo.cfg and message file: the text a
    // record in that room gets. The wants are what the game's data holds (Georama part name = the resident's); a room whose
    // part has no message in the file (Queens' and Muska Racka's, one shop of Matataki's) stays "a house".
    struct HouseCase {
        const char *room;
        int         town;
        const char *want;
    };
    static const HouseCase houses[] = {
        {"i01h01", 0, "My House"}, {"i01h011", 0, "My House"}, {"i01h02", 0, "Macho's House"}, {"i01h03", 0, "Laura's House"},
        {"i01h04", 0, "Claude's House"}, {"i01h05", 0, "Alnet's House"}, {"i01h051", 0, "Alnet's House"}, {"i01h06", 0, "Mayor's House"},
        {"i01h07", 0, "Dran's Windmill"}, {"i01h10", 0, "Paige's House"}, {"i01h11", 0, "Hag's House"},
        {"i02c09", 1, "Watermill 1"}, {"i02c15", 1, "Watermill 2"}, {"i02c16", 1, "Watermill 3"}, {"i02h01", 1, "Pao's house"},
        {"i02h02", 1, "Cacao's house"}, {"i02h03", 1, "Bunbuku's house"}, {"i02h04", 1, "Kye & Momo's house"}, {"i02h05", 1, "Baron's house"},
        {"i02h06", 1, "Couscous' house"}, {"i02h07", 1, "Gob's house"}, {"i02h08", 1, "Mushroom house"}, {"i02h10", 1, "Wise Owl Shop"},
        {"i03h04", 2, "Jack's"}, {"i03h05", 2, "Joker's"}, {"i03h06", 2, "Divining House"}, {"i03h07", 2, "Cathedral"},
        {"i03h08", 2, "Basker's"}, {"i03h081", 2, "Basker's"}, {"i03h09", 2, "King's Hideout"}, {"i03h10", 2, "Sheriff's Office"},
        {"i04h01", 3, "Chief Bonka's House"}, {"i04h02", 3, "Jibubu's House"}, {"i04h03", 3, "Zabo's House"}, {"i04h04", 3, "The 3 Sisters' House"},
        {"i04h05", 3, "Brooke's House"}, {"i04h06", 3, "Enga"}, {"i04h08", 3, "Toto's House"}, {"i04h09", 3, "Odd Gaffer's Item Shop"},
    };    for (const HouseCase &h : houses) {
        Where       w[2] = {Town(0), Town(h.town, CoopWorldsInteriorId(h.room))};
        WhereLine   line[kMaxPlayers];
        const int   n = WhereLines(1, w, 2, line, kMaxPlayers);
        std::string want = std::string(CoopWorldsTownName(h.town)) + ", " + h.want;
        const bool  ok = n > 0 && want == line[0].text;

        total++;
        right += ok ? 1 : 0;
        std::fprintf(stderr, "coop-whereis: table: house %s: \"%s\" %s%s%s\n", h.room, n > 0 ? line[0].text : "", ok ? "right" : "WRONG, want \"", ok ? "" : want.c_str(),
                     ok ? "" : "\"");
    }
    for (const Case &c : cases) {
        WhereLine  lines[kMaxPlayers];
        const int  n = WhereLines(c.viewer, c.table, c.count, lines, kMaxPlayers);
        const auto got = Joined(lines, n);
        const bool ok = got == c.want;

        total++;
        right += ok ? 1 : 0;
        std::fprintf(stderr, "coop-whereis: table: %s: \"%s\" %s%s%s\n", c.name, got.c_str(), ok ? "right" : "WRONG, want \"", ok ? "" : c.want,
                     ok ? "" : "\"");
    }
    std::fprintf(stderr, "coop-whereis: table: %d of %d right\n", right, total);
    std::fflush(stderr);
}

} // namespace

void WhereIsFrame() {
    static const char *test = TestEnv("DC_COOP_TEST_WHEREIS");

    if (test == nullptr) {
        return;
    }

    // What this copy holds for every other player, said when it changes (and the drawing, by DrawWhereIs).
    {
        static std::string said = "\x01";
        WhereLine          lines[kMaxPlayers];
        const int          n = WhereIsNow(lines, kMaxPlayers);
        const std::string  now = n == 0 ? std::string("nothing (") + (CoopWorldsPlayers() < 2 ? "single player)" : "no other player)") : Joined(lines, n);

        // coop-lastseen: a list held unchanged past the stale time (DC_COOP_TEST_WHEREIS_STALE_MS, else 30 s), said once: a
        // player heard all that while still shows where it is, never "last seen" (w-whereis -Case split, A's long scene).
        static const int stale_ms = TestEnvInt("DC_COOP_TEST_WHEREIS_STALE_MS", 30000);
        static auto      since = std::chrono::steady_clock::now();
        static bool      held_said = false;

        if (now != said) {
            said = now;
            since = std::chrono::steady_clock::now();
            held_said = false;
            std::fprintf(stderr, "coop-whereis: copy of p%d holds: %s\n", CoopWorldsSelf(), now.c_str());
            std::fflush(stderr);
        } else if (!held_said && std::chrono::steady_clock::now() - since > std::chrono::milliseconds(stale_ms)) {
            held_said = true;
            std::fprintf(stderr, "coop-whereis: copy of p%d held past the stale time (%d ms): %s\n", CoopWorldsSelf(), stale_ms, now.c_str());
            std::fflush(stderr);
        }
    }

    static bool tabled = false;

    if (std::strstr(test, "table") != nullptr && !tabled && NetplayStateFileLoaded()) {
        tabled = true;
        RunTable();
        // (single player: the list's own check, from the live state: this copy is not linked)
        const char *why = "";

        std::fprintf(stderr, "coop-whereis: table: this copy shows the list: %s\n", Shown(&why) ? "yes" : (std::string("no, ") + why).c_str());
        std::fflush(stderr);
    }
}

} // namespace coop
