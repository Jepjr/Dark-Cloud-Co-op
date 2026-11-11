// coop: the dungeon floor title ("B 1", "TERRITORY 1") as the disc's own picture lays it out.
//
// StartMessageDraw is PAL retail's (ps2/src/collisiondata.cpp): it draws the picture's caption whole
// and centres the floor number on the PAL caption. An NTSC disc's floor picture holds a shorter
// caption, so drawn that way the number lands far from it (Divine Beast Cave's "B" and its "1").
// Upstream dropped NTSC's own layout (DrawFloorTitleNtsc) in 0e6ae548, "Normalize NTSC assets during
// extraction", but dcdata copies the NTSC pictures as they are (dun/img/us/dnameNN.img), so on NTSC
// data the floor title is drawn here as NTSC retail drew it.
//
// Test hooks: DC_FLOOR_TITLE_TRACE=1 prints each title's layout once per floor ("floor-title: ...");
// DC_FLOOR_TITLE_HIDE=1 draws none (a frame to subtract); DC_FLOOR_TITLE_MUTATE=pal draws PAL's
// layout on any data (v-floor-title's twin).
#include "coop/game_ns.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "collisiondata.hpp"
#include "data/data_read.hpp"
#include "dun/gameloop.hpp"
#include "mainselect.hpp"
#include "mglib.hpp"
#include "rect.hpp"
#include "snd.hpp"
#include "userstatus.hpp"

namespace {

bool EnvIs(const char *name, const char *value) {
    const char *set = std::getenv(name);
    return set != nullptr && std::strcmp(set, value) == 0;
}

// The pieces one title drew, for the trace: " caption=x+w", " digit=x+w" in 640-wide screen units.
std::string g_pieces;
int         g_traced_dungeon = -1;
int         g_traced_floor = -1;

void Piece(CTexture *texture, int x, int y, int w, int h, int u, int v, int alpha, const char *what) {
    set2DSprite(Vif1Packet, texture, CRect_i_(x, y, w, h), CRect_i_(u, v, w, h), alpha);
    char line[48];
    std::snprintf(line, sizeof line, " %s=%d+%d", what, x, w);
    g_pieces += line;
}

// PAL retail's StartMessageDraw (ps2/src/collisiondata.cpp), for PAL data.
void DrawPal(CTexture *texture, int dungeon, int floor, int ura, int alpha) {
    static const int  center_us[7] = {0xCE, 0x125, 0x101, 0x100, 0x87, 0x57, 0x101};
    static const int  center_fr[7] = {0xCD, 0x113, 0xF8, 0xF8, 0xB2, 0x59, 0xFF};
    static const int  center_gr[7] = {0xFE, 0x103, 0x13D, 0xFD, 0x78, 0x38, 0xFF};
    static const int  center_it[7] = {0xCC, 0x129, 0xF3, 0xFF, 0xFA, 0x75, 0xF9};
    static const int  center_sp[7] = {0xCE, 0x12C, 0xF2, 0xFA, 0x7C, 0xBA, 0xFA};
    static const int *center_ptr[7] = {center_sp, center_us, center_us, center_fr, center_gr, center_it, center_sp};
    const int        *centers = center_ptr[LanguageCode];
    int               x = centers[dungeon] + 0x82;

    Piece(texture, 0x82, 0xAA, 0x17C, 0x32, 0, 0, alpha, "caption");

    floor++;
    if (dungeon == DUNGEON_GALLERY_OF_TIME) {
        floor = BtGetFloorLevel(floor - 1);
    }
    if (floor < 10) {
        Piece(texture, x - 0x13, 0xAA, 0x26, 0x32, floor % 10 * 0x26, 0x32, alpha, "digit");
    }
    if (floor >= 10 && floor < 100) {
        Piece(texture, x - 0x26, 0xAA, 0x26, 0x32, floor / 10 * 0x26, 0x32, alpha, "digit");
        Piece(texture, x, 0xAA, 0x26, 0x32, floor % 10 * 0x26, 0x32, alpha, "digit");
    }
    if (floor >= 100) {
        int digit = floor / 100;
        floor -= digit * 100;
        Piece(texture, x - 0x39, 0xAA, 0x26, 0x32, digit * 0x26, 0x32, alpha, "digit");
        Piece(texture, x - 0x13, 0xAA, 0x26, 0x32, floor / 10 * 0x26, 0x32, alpha, "digit");
        Piece(texture, x + 0x13, 0xAA, 0x26, 0x32, floor % 10 * 0x26, 0x32, alpha, "digit");
    }
    if (ura != 0) {
        Piece(texture, 0x10E, 0xE6, 0x64, 0x32, 0x114, 0x7C, alpha, "ura");
        return;
    }
    if (UserStatus->res_limit_zone_current >= 0) {
        Piece(texture, 0xC8, 0xE6, 0xF0, 0x32, 0, 0x7C, alpha, "limit");
    }
}

// NTSC retail's floor title (DrawFloorTitleNtsc before upstream 0e6ae548): the caption in pieces,
// then the floor number after it.
void DrawNtsc(CTexture *texture, int dungeon, int floor, int ura, int alpha) {
    int shift = 0;
    int nudge = floor >= 9 ? -0x24 : 0;

    if (LanguageCode == LANG_JAPANESE) {
        switch (dungeon) {
            case DUNGEON_DIVINE_BEAST_CAVE:
                Piece(texture, nudge + 0xE2, 0xAA, 0x72, 0x32, 0, 0, alpha, "caption");
                Piece(texture, 0x17D, 0xAA, 0x4C, 0x32, 0x72, 0, alpha, "caption");
                break;
            case DUNGEON_WISE_OWL_FOREST:
                Piece(texture, 0xBE, 0xAA, 0xBE, 0x32, 0, 0, alpha, "caption");
                shift = 0x40;
                break;
            case DUNGEON_SHIPWRECK:
                Piece(texture, nudge + 0xF5, 0xAA, 0x4C, 0x32, 0, 0, alpha, "caption");
                Piece(texture, 0x159, 0xAA, 0x4C, 0x32, 0x4C, 0, alpha, "caption");
                shift = -0x24;
                break;
            case DUNGEON_SUN_MOON_TEMPLE:
                Piece(texture, 0xE8, 0xAA, 0x98, 0x32, 0, 0, alpha, "caption");
                shift = 0x40;
                break;
            case DUNGEON_MOON_SEA:
                Piece(texture, nudge + 0xD7, 0xAA, 0x4C, 0x32, 0, 0, alpha, "caption");
                Piece(texture, 0x13B, 0xAA, 0xBE, 0x32, 0x4C, 0, alpha, "caption");
                shift = -0x40;
                break;
            case DUNGEON_GALLERY_OF_TIME:
                Piece(texture, 0x135, 0xAA, 0x98, 0x32, 0, 0, alpha, "caption");
                shift = -0x40;
                break;
        }
        if (ura != 0) {
            Piece(texture, 0x120, 0xE6, 0x40, 0x38, 0x13A, 0x78, alpha, "ura");
        } else if (UserStatus->res_limit_zone_current >= 0) {
            Piece(texture, 0xD4, 0xE6, 0xD8, 0x30, 0, 0x78, alpha, "limit");
        }
    } else {
        switch (dungeon) {
            case DUNGEON_DIVINE_BEAST_CAVE:
                Piece(texture, nudge + 0x122, 0xAA, 0x2D, 0x32, 0, 0, alpha, "caption");
                shift = -0x14;
                break;
            case DUNGEON_WISE_OWL_FOREST:
                Piece(texture, 0xA0, 0xAA, 0xDA, 0x32, 0, 0, alpha, "caption");
                shift = floor < 9 ? 0x20 : 0x40;
                break;
            case DUNGEON_SHIPWRECK:
                Piece(texture, 0xC8, 0xAA, 0x91, 0x32, 0, 0, alpha, "caption");
                if (floor >= 9) {
                    shift = 0x20;
                }
                break;
            case DUNGEON_SUN_MOON_TEMPLE:
            case DUNGEON_DEMON_SHAFT:
                Piece(texture, nudge + 0xDC, 0xAA, 0x8E, 0x32, 0, 0, alpha, "caption");
                shift = 0x10;
                break;
            case DUNGEON_MOON_SEA:
                Piece(texture, nudge + 0xC3, 0xAA, 0x4B, 0x32, 0, 0, alpha, "caption");
                Piece(texture, 0x13B, 0xAA, 0xC0, 0x32, 0x60, 0, alpha, "caption");
                shift = -0x40;
                break;
            case DUNGEON_GALLERY_OF_TIME:
                Piece(texture, 0x108, 0xAA, 0xE1, 0x32, 0, 0, alpha, "caption");
                shift = -0x80;
                break;
        }
        if (ura != 0) {
            Piece(texture, 0x110, 0xE6, 0x60, 0x38, 0x110, 0x78, alpha, "ura");
        } else if (UserStatus->res_limit_zone_current >= 0) {
            Piece(texture, 0xD4, 0xE6, 0xF0, 0x30, 0, 0x78, alpha, "limit");
        }
    }

    int floor_no = floor + 1;
    if (dungeon == DUNGEON_GALLERY_OF_TIME) {
        floor_no = BtGetFloorLevel(floor_no - 1);
    }
    if (floor_no >= 100) {
        int digit = floor_no / 100;
        floor_no -= digit * 100;
        Piece(texture, shift + 0x112, 0xAA, 0x26, 0x32, digit * 0x26, 0x32, alpha, "digit");
        Piece(texture, shift + 0x136, 0xAA, 0x26, 0x32, floor_no / 10 * 0x26, 0x32, alpha, "digit");
    } else if (floor_no >= 10) {
        Piece(texture, shift + 0x136, 0xAA, 0x26, 0x32, floor_no / 10 * 0x26, 0x32, alpha, "digit");
    }
    Piece(texture, shift + 0x15A, 0xAA, 0x26, 0x32, floor_no % 10 * 0x26, 0x32, alpha, "digit");
}

} // namespace

namespace dc::game { // repin: was PORT_OVERRIDE; a recipe removes the game's own definition
void StartMessageDraw(CTexture *texture, int dungeon, int floor, int ura, int alpha) {
    static const bool hide = EnvIs("DC_FLOOR_TITLE_HIDE", "1");
    static const bool trace = EnvIs("DC_FLOOR_TITLE_TRACE", "1");
    static const bool ntsc = dc::data::DataNtscRelease() && !EnvIs("DC_FLOOR_TITLE_MUTATE", "pal");
    if (hide) {
        return;
    }
    g_pieces.clear();
    if (ntsc) {
        DrawNtsc(texture, dungeon, floor, ura, alpha);
    } else {
        DrawPal(texture, dungeon, floor, ura, alpha);
    }
    if (trace && (dungeon != g_traced_dungeon || floor != g_traced_floor)) {
        g_traced_dungeon = dungeon;
        g_traced_floor = floor;
        std::fprintf(stderr, "floor-title: %s dungeon %d floor %d language %d alpha %d%s\n", ntsc ? "ntsc" : "pal", dungeon,
                     floor + 1, (int) LanguageCode, alpha, g_pieces.c_str());
    }
}
} // namespace dc::game
