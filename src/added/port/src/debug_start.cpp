// Debug start (--jump pick): a list of the five towns and seven dungeons drawn with the developer menu's
// font, at boot (--jump pick) or at any time from pad 1's L3+R3 (gameloop.cpp), in the developer menu's place.
// Up and Down pick a row; on a dungeon Left and Right pick the floor (L1 and R1 by ten); Cross starts there,
// Circle goes back where it was opened (at boot, on to the title).
#include "coop/game_ns.hpp"
#include "debug_start.hpp"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "coop/coop.hpp"
#include "coop/coop_worlds.hpp"
#include "dun/gameloop.hpp"
#include "userstatus.hpp"
#include "debugfont.hpp"
#include "gamepad.hpp"
#include "main.hpp"
#include "mainselect.hpp"
#include "mglib.hpp"
#include "savedata.hpp"
#include "texture.hpp"

namespace dc::game {
extern CDebugFont DebugFont;
extern s32        mode;
} // namespace dc::game

namespace {

constexpr int kFloors[7] = {15, 17, 18, 18, 15, 25, 100}; // retail's maxFloorTbl

struct Row {
    const char *name;
    int         dungeon; // -1 for a town
    int         map;     // the town's MapNo, or the dungeon's number
};

constexpr Row kRows[] = {
    {"Norune Village",     -1, 0},
    {"Matataki Village",   -1, 1},
    {"Queens",             -1, 2},
    {"Muska Racka",        -1, 3},
    {"Yellow Drops",       -1, 4},
    {"Divine Beast Cave",  0,  0},
    {"Wise Owl Forest",    1,  1},
    {"Shipwreck",          2,  2},
    {"Sun & Moon Temple",  3,  3},
    {"Moon Sea",           4,  4},
    {"Gallery of Time",    5,  5},
    {"Demon Shaft",        6,  6},
};
constexpr int kRowCount = static_cast<int>(sizeof(kRows) / sizeof(kRows[0]));

int  g_row = 0;
int  g_floor[7] = {1, 1, 1, 1, 1, 1, 1};
bool g_logged = false;

// A join's resync (DebugStartOnResync), until the next mode's init latches the floor it re-enters, and that
// floor (0 up; -1 none).
bool g_resync_pending = false;
int  g_resync_floor = -1;

// Adds a line to the developer font's text, as far as it fits.
[[gnu::format(printf, 1, 2)]] void line(const char *format, ...) {
    int room = static_cast<int>(sizeof(DebugFont.text)) - DebugFont.length;
    if (room <= 1) {
        return;
    }
    std::va_list args;
    va_start(args, format);
    int written = std::vsnprintf(&DebugFont.text[DebugFont.length], room, format, args);
    va_end(args);
    DebugFont.length += std::clamp(written, 0, room - 1);
}

} // namespace

int DebugStartFloors(int dungeon) {
    return dungeon >= 0 && dungeon < 7 ? kFloors[dungeon] : 0;
}

bool DebugStartMutated(const char *what) {
    static const std::string value = std::string(",") + TestEnvOr("DC_DEBUG_START_MUTATE", "") + ",";
    return value.find(std::string(",") + what + ",") != std::string::npos;
}

// pt1111c test hook (DC_DEBUG_START_SEEN=F[/F...], the dun0-f8-solo fixture): game flags a test start marks set too, besides
// its scenes (the cave's floor 8 walk-in story, 25: unset, its marker would start Seda's scene as the fixture is made).
int g_jump_pending = -1; // pt1111c: the floor a test start goes to, until a join's resync reads it

void DebugStartSeenFlags(int floor) {
    g_jump_pending = floor;
    const char *list = TestEnvOr("DC_DEBUG_START_SEEN", "");
    for (const char *at = list; *at != 0;) {
        const int flag = std::atoi(at);
        if (flag > 0 && SaveData != nullptr) {
            SaveData->SetGameFlag(flag, 1);
            std::fprintf(stderr, "debug start: game flag %d set (DC_DEBUG_START_SEEN)\n", flag);
        }
        const char *slash = std::strchr(at, '/');
        at = slash != nullptr ? slash + 1 : at + std::strlen(at);
    }
}

int DebugStartModeLoop() {
    if (!g_logged) {
        // The tests' mode check: the picker is up.
        std::fprintf(stderr, "debug start: picker open\n");
        g_logged = true;
    }
    if (GamePad.Down(PAD_DOWN)) {
        g_row = (g_row + 1) % kRowCount;
    }
    if (GamePad.Down(PAD_UP)) {
        g_row = (g_row + kRowCount - 1) % kRowCount;
    }
    const Row &row = kRows[g_row];
    if (row.dungeon >= 0) {
        int &floor = g_floor[row.dungeon];
        int  max = kFloors[row.dungeon];
        if (GamePad.Down(PAD_RIGHT)) {
            floor++;
        }
        if (GamePad.Down(PAD_LEFT)) {
            floor--;
        }
        if (GamePad.Down(PAD_R1)) {
            floor += 10;
        }
        if (GamePad.Down(PAD_L1)) {
            floor -= 10;
        }
        floor = std::clamp(floor, 1, max);
    }

    DebugFont.length = 0;
    line("DEBUG START  (L3+R3)\n");
    for (int i = 0; i < kRowCount; i++) {
        const char *cursor = i == g_row ? ">" : " ";
        if (kRows[i].dungeon < 0) {
            line("%s%s\n", cursor, kRows[i].name);
        } else {
            line("%s%s  %d/%d\n", cursor, kRows[i].name, g_floor[kRows[i].dungeon], kFloors[kRows[i].dungeon]);
        }
    }
    line("X start  O back  L1/R1 10\n");
    TexManager.ReloadTexture(GetVif1Packet(), 0);
    DebugFont.Draw();

    // The pad's raw press: in the developer menu's mode Down(PAD_CIRCLE) also answers Cross (gamepad.cpp).
    // Back where the picker was opened (the developer menu's way back), or on to the title at boot.
    if ((GamePad.GetPadDown() & PAD_CIRCLE) != 0) {
        std::fprintf(stderr, "debug start: closed\n");
        g_logged = false;
        return -1;
    }
    if (!GamePad.Down(PAD_CROSS)) {
        return 0;
    }
    g_logged = false;
    if (row.dungeon < 0) {
        int town = DebugStartMutated("town") ? (row.map + 1) % 5 : row.map;
        std::fprintf(stderr, "debug start: town %d (%s)\n", town, row.name);
        dc::app::GameApplyDebugStart(GAME_MODE_EDIT, town, 0);
    } else {
        std::fprintf(stderr, "debug start: dungeon %d floor %d (%s)\n", row.dungeon, g_floor[row.dungeon], row.name);
        dc::app::GameApplyDebugStart(GAME_MODE_DUNGEON, row.dungeon, g_floor[row.dungeon]);
    }
    return 1;
}


void DebugStartOnResync() {
    // A join's resync only: a coop-worlds merge (the copies apart until it holds) brings the pair together where they
    // stand, so a host at its cave's floor menu with a floor in its save sent the joiner down alone; with the merge then
    // dropped (w-world-next-floor-refuse) her copy played that floor and left, and the pair never met again.
    // DC_DEBUG_START_MUTATE=merge_floor (that test's ~mergefloor twin): a merge's resync arms it too, as before.
    g_resync_pending = !DebugStartMutated("menu") && (!CoopWorldsApart() || DebugStartMutated("merge_floor"));
    g_resync_floor = -1;
}

void DebugStartBeforeModeInit() {
    // coop-soakfix3 test switch (DC_DEBUG_START_EDIT_TEST=1, the town1-built fixture): the first town init takes retail's developer-menu
    // way in (old_main_mode the menu, no R-up held), so EditInit itself loads the town's own built Georama (its gdata0.edt, every part
    // obtained and every element in: a house's parts complete) instead of the save's: what a row needs to walk into a finished house.
    // Every town init while it is on (a link's resync reloads the town: a one-shot switch let that reload take the save's empty Georama).
    if (static const bool edit_test = TestEnvOn("DC_DEBUG_START_EDIT_TEST"); edit_test && mode == GAME_MODE_EDIT) {
        old_main_mode = GAME_MODE_MENU;
        main_select_padrup = 0;
        std::fprintf(stderr, "debug start: test: town %d by the developer menu's edit test (its built Georama, gdata0.edt)\n", (int) MapNo);
    }
    if (!g_resync_pending) {
        return;
    }
    // The host's state is in place by now on both copies and the dungeon's init has not yet put the party back
    // at its entrance: the floor the host was on.
    g_resync_pending = false;
    // DebugMode too comes with the host's state: the joiner's own was off when the resync began.
    // The save's dungeon status, which UserStatus points at once the dungeon starts (a joiner coming from the
    // title has no UserStatus yet).
    const CUserStatus *status = reinterpret_cast<const CUserStatus *>(SaveData->GetDngStatus());
    // pt1111c (the dun1-f9 fixture came onto Wise Owl Forest floor 1 where it jumped to floor 9): a join's resync can come
    // before the jump's floor is entered (both copies' --jump, the resync at link tick 33, the floor's start later), and the
    // save's floor read here was still the dungeon's first; dun0-f8 had won the same race by its timing. The floor this copy's
    // own test start goes to is the one meant (both copies jump alike).
    const int jumped = g_jump_pending;
    g_jump_pending = -1;
    if (DebugMode != 0 && mode == GAME_MODE_DUNGEON && status->cur_floor >= 0) {
        g_resync_floor = std::max<int>(status->cur_floor, jumped);
        std::fprintf(stderr, "debug start: after the resync, back to floor %d\n", g_resync_floor + 1);
    }
}

int DebugStartTakeResyncFloor() {
    int floor = g_resync_floor;
    g_resync_floor = -1;
    return floor;
}

namespace {
// The game flag a debug start sets in the save data, so a joiner, which takes the host's save data, knows too.
// Retail's flags are numbered well below it (an inference: no code or script seen uses one this high).
constexpr int kDebugStartFlag = 8191;
bool          g_used = false; // a debug start in this run, or one seen in the save data; until the game restarts
} // namespace

void DebugStartMark() {
    g_used = true;
    if (SaveData != nullptr) {
        SaveData->SetGameFlag(kDebugStartFlag, 1);
    }
}

bool DebugStartSavesOff(const char *what) {
    if (SaveData != nullptr && SaveData->GetGameFlag(kDebugStartFlag) != 0) {
        g_used = true;
    }
    if (!g_used || DebugStartMutated("save")) {
        return false;
    }
    std::fprintf(stderr, "debug start: %s refused (saving is off after a debug start until the game restarts)\n", what);
    return true;
}

void DebugStartAfterLoad() {
    if (g_used) {
        DebugStartMark();
    }
}
