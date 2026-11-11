#include "coop/game_ns.hpp"
#include "title/title_mpmenu.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "text/caption_text.hpp"
#include "common.h"
#include "coop/coop.hpp"
#include "coop/coop_lobby.hpp" // coop-lobby
#include "coop/coop_lobby_models.hpp" // coop-lobbymodels: the dev hook (DC_LOBBY_MODELS_DEMO)
#include "coop/netplay.hpp"
#include "coop/netplay_steam.hpp"
#include "coop/steam_check.hpp" // coop-steamsetup
#include "gamepad.hpp"
#include "localize/localize.hpp" // the main menu's START and CONTINUE, under Host and Local too
#include "sound.hpp"
#include "menu_draw.hpp"
#include "mglib.hpp"
#include "rect.hpp"
#include "snd.hpp"
#include "texture.hpp"
#include "title/title.hpp"
#include "title/title_menu.hpp" // Jep's 2a (2026-11-10): the submenus on the title menu's rows, in its letters

namespace dc::game {
void TiPlayVolSE(int group, int no, int voice, float volume);
} // namespace dc::game

namespace {

// coop-mainmenu (Jep, 2026-10-29 and 30): MULTIPLAYER > HOST / JOIN; JOIN > IP / CODE / STEAM; HOST > START / CONTINUE.
// coop-lobby (Jep, 2026-10-30): HOST has no IP / CODE / STEAM level any more (the
// lobby shows the outside address and the code, and Steam is the host's Select there), and the save picked under HOST
// opens the lobby (coop_lobby.hpp) instead of starting the game. Players on this PC: game.lobby_local "combined" (the
// default) has no LOCAL row (Start on a free controller in the lobby takes a seat); "separate" keeps MULTIPLAYER > LOCAL >
// START / CONTINUE, whose lobby is local only. No SPLITSCREEN (N) / MULTISCREEN (N) rows: the count is how many pads press
// Start. At most four rows on a level: three choices at most, then Back (Circle too). CODE is NAT's code trade and STEAM
// NAT by Steam (Steam hands the codes over, play goes direct).
enum Level {
    kTop,   // the title menu itself
    kMulti, // Host, Join (Local too under the separate shape)
    kWay,   // IP, Code, Steam (under Join)
    kStart,   // Start, Continue (the main menu's New Game and Load), then the lobby
    kFriends, // Join > Steam: the Steam friends hosting, one picked before the join goes in (coop-lobbysteam)
};
enum Way { kIp, kCode, kSteam, kLocal };

Level g_level = kTop;
int   g_row = 0;
int   g_side = 1; // 1 Host (Local too), 2 Join
int   g_way = kIp;
int   g_via = 0; // the way picked: 1 Host (IP and code alike), 3 Local
// Back from the load screen Continue opened: the submenu kept (TitleMpClose), so Circle there comes back to it.
bool g_kept = false;

// coop-lobbysteam (Jep, 2026-10-30: a Steam join lists every friend detected hosting and lets you pick, as a safety measure,
// even with one): Join > Steam's rows are the friends hosting (netplay_steam.hpp NetSteamFriendsHosting), three to a page
// (Left/Right for the others), then Back; Cross on one picks that friend's lobby and opens the join, which joins it alone.
// DC_TITLE_MP_MUTATE=steam_no_list (the twin): the join opens at once and takes whoever it finds, as before.
std::vector<NetSteamFriendHost> g_friends;
int                             g_friends_answer = 0; // NetSteamFriendsHosting's: 1 a list, 0 still looking, -1 Steam out
int                             g_page = 0;
constexpr int                   kFriendsPage = 3;

int FriendsOnPage() {
    return std::max(0, std::min(kFriendsPage, (int) g_friends.size() - g_page * kFriendsPage));
}

int Pages() {
    return std::max(1, ((int) g_friends.size() + kFriendsPage - 1) / kFriendsPage);
}

const NetSteamFriendHost *FriendAt(int row) {
    return row >= 0 && row < FriendsOnPage() ? &g_friends[(std::size_t) (g_page * kFriendsPage + row)] : nullptr;
}

constexpr const char *kWayWords[3] = {"IP", "CODE", "STEAM"};
constexpr const char *kWayNames[4] = {"IP", "Code", "Steam", "Local"};

const char *Side() {
    return g_side == 1 ? "Host" : "Join";
}

// The level's choices; then Back, its last row (Jep, 2026-10-30: back as a row again now no level has more than three).
// DC_TITLE_MP_MUTATE=no_back_row (the test's twin): no Back row, as before.
int Choices() {
    switch (g_level) {
        case kMulti:
            return coop::lobby::Local() == coop::lobby::LocalShape::Separate ? 3 : 2;
        case kWay:
            return 3;
        case kStart:
            return 2;
        case kFriends:
            return FriendsOnPage();
        case kTop:
            break;
    }
    return 0;
}

int Count() {
    return Choices() + (g_level != kTop && !TestEnvIs("DC_TITLE_MP_MUTATE", "no_back_row") ? 1 : 0);
}

bool IsBack(int row) {
    return row >= Choices();
}

std::string Word(int row) {
    static const char *const kMultiWords[3] = {"HOST", "JOIN", "LOCAL"};

    if (IsBack(row)) {
        return "BACK";
    }
    switch (g_level) {
        case kMulti:
            return kMultiWords[row];
        case kWay:
            return kWayWords[row];
        case kStart:
            // The main menu's own START and CONTINUE, in its language (title_menu.cpp's keys).
            return row == 0 ? dc::localize::LocalizeText("title.menu.start", "START") : dc::localize::LocalizeText("title.menu.continue", "CONTINUE");
        case kFriends:
            return FriendAt(row) != nullptr ? FriendAt(row)->name : "";
        case kTop:
            break;
    }
    return "";
}

// The row's name in the log.
std::string RowName(int row) {
    return Word(row);
}

// Whether a row does something: a friend on another build is listed but refused (its lobby would refuse the join).
bool Built(int row) {
    if (TestEnvIs("DC_TITLE_MP_MUTATE", "all_built") || g_level != kFriends) {
        return true;
    }
    return IsBack(row) || (FriendAt(row) != nullptr && !FriendAt(row)->other_build);
}

std::string LevelName() {
    switch (g_level) {
        case kMulti:
            return "Multi-player";
        case kWay:
            return Side();
        case kStart:
            return g_via == 3 ? "Local start" : "Host start";
        case kFriends:
            return "Join > Steam";
        case kTop:
            break;
    }
    return "menu";
}

const char *ViaName() {
    return g_via == 3 ? "Local" : "Host";
}

// The status font's lettering, for a Steam name the title menu's letters do not have: the menu's cream when lit, grey
// when dimmed (the font has no alpha, only a fade to black), from the menu's left edge (x 263), a touch smaller than its
// capitals, as it is heavier.
constexpr unsigned char kFill[3] = {226, 253, 210};
constexpr float         kLeft = 263.0f;
constexpr float         kCapHeight = 15.5f;
constexpr float         kSqueeze = 1.12f;
constexpr unsigned char kDimFill[3] = {96, 98, 94};

void Go(Level level, int row) {
    g_level = level;
    g_row = row;
    CCursol.Set(dc::title::menu::HandY(row));
    std::fprintf(stderr, "title-mp: %s, row %d\n", LevelName().c_str(), row);
}

void Back() {
    if (TestEnvIs("DC_TITLE_MP_MUTATE", "no_back")) {
        return;
    }

    TiPlayVolSE(MIDI_PORT_UNK_D, 122, 24, 1.0f);

    switch (g_level) {
        case kMulti:
            g_level = kTop;
            CCursol.select = TITLE_MENU_MULTIPLAYER;
            CCursol.Set(dc::title::menu::HandY(dc::title::menu::RowOf(TITLE_MENU_MULTIPLAYER)));
            std::fprintf(stderr, "title-mp: back to the menu on Multi-player\n");
            break;
        case kWay:
            Go(kMulti, g_side - 1);
            break;
        case kFriends:
            // (Steam goes with the list: no Spacewar on this player's profile while it looks for nobody)
            NetSteamJoinPick(0);
            NetSteamStop("the friend list closed");
            g_friends.clear();
            g_friends_answer = 0;
            Go(kWay, kSteam);
            break;
        case kStart:
            NetplayTitleHost(0); // (it forgets a NAT pick too)
            coop::lobby::Pend(false);
            Go(kMulti, g_via == 3 ? 2 : 0);
            break;
        case kTop:
            break;
    }
}

// Returns whether the game starts (Start or Continue under Host): the title then goes as its New Game or Load does.
bool Choose() {
    if (IsBack(g_row)) {
        Back();
        return false;
    }

    if (!Built(g_row)) {
        TiPlayVolSE(MIDI_PORT_UNK_D, 122, 24, 1.0f);
        std::fprintf(stderr, "title-mp: %s > %s refused (another build)\n", LevelName().c_str(), RowName(g_row).c_str());
        return false;
    }
    TiPlayVolSE(MIDI_PORT_SE_DEFAULT, 38, 20, 1.0f);

    switch (g_level) {
        case kMulti:
            if (g_row == 2) {
                // Local (the separate shape): this copy hosts on 127.0.0.1, its local-only lobby follows the save's pick,
                // and a second window starts for a player on this PC (netplay_local.hpp).
                g_side = 1; // (this copy hosts the others)
                g_way = kLocal;
                g_via = 3;
                std::fprintf(stderr, "title-mp: Local picked\n");
                NetplayTitleHost(3);
                coop::lobby::Pend(true, true);
                Go(kStart, 0);
                break;
            }
            g_side = g_row + 1;
            if (g_side == 1) {
                // Host: one way for IP and code (the code is the outside address in letters), the lobby showing both.
                g_way = kIp;
                g_via = 1;
                std::fprintf(stderr, "title-mp: Host picked\n");
                if (!TestEnvIs("DC_TITLE_MP_MUTATE", "no_host_via")) {
                    NetplayTitleHost(1);
                }
                coop::lobby::Pend(true, false);
                Go(kStart, 0);
                break;
            }
            Go(kWay, 0);
            break;
        case kWay:
            g_way = g_row;
            std::fprintf(stderr, "title-mp: %s > %s picked\n", Side(), kWayNames[g_way]);
            // Join: the address entry, the code entry, or Steam's friend search; closing it comes back here.
            if (g_way == kIp) {
                NetplayTitleOpenJoin(false);
            } else if (g_way == kCode) {
                NetplayTitleNat(2);
            } else if (TestEnvIs("DC_TITLE_MP_MUTATE", "steam_no_list")) {
                NetplayTitleNatSteam(2);
            } else {
                g_friends.clear();
                g_friends_answer = 0;
                g_page = 0;
                Go(kFriends, 0);
            }
            break;
        case kStart:
            std::fprintf(stderr, "title-mp: %s > %s: the game starts\n", ViaName(), g_row == 0 ? "Start" : "Continue");
            // The pick flashes on this submenu's own rows (the title's MENU_BLINK draws an open submenu instead of its
            // main rows), not on START / CONTINUE with MULTI-PLAYER under them (Jep, 2026-10-24); the title closes it
            // once the screen has faded (TitleMpClose). DC_TITLE_MP_MUTATE=flash_on_main (a test's twin): closed now,
            // as before, so the main rows flash.
            if (TestEnvIs("DC_TITLE_MP_MUTATE", "flash_on_main")) {
                g_level = kTop;
            }
            CCursol.select = g_row == 0 ? TITLE_MENU_NEW_GAME : TITLE_MENU_LOAD;
            CCursol.Set(dc::title::menu::HandY(g_row));
            return true;
        case kFriends:
            // The friend picked: the join opens and joins that friend's lobby alone (closing it comes back to this list).
            if (const NetSteamFriendHost *host = FriendAt(g_row)) {
                std::fprintf(stderr, "title-mp: Join > Steam > %s picked (lobby %llu%s, %d listed)\n", host->name.c_str(), (unsigned long long) host->lobby,
                             host->invited ? ", invited" : "", (int) g_friends.size());
                NetSteamJoinPick(host->lobby);
                NetplayTitleNatSteam(2);
            }
            break;
        case kTop:
            break;
    }

    return false;
}

// Join > Steam's list, looked at each frame while it shows and nothing is picked yet: the hand kept on the same friend
// when the list changes under it (on Back when that friend is gone).
void FriendsLook() {
    if (g_level != kFriends || NetSteamJoinPicked() != 0) {
        return;
    }
    // DC_TITLE_MP_MUTATE=list_once (mp-nat -EarlyJoin's twin): the list keeps its first answer and never looks again.
    if (g_friends_answer != 0 && TestEnvIs("DC_TITLE_MP_MUTATE", "list_once")) {
        return;
    }
    const NetSteamFriendHost       *on = FriendAt(g_row);
    const std::uint64_t             was = on != nullptr ? on->lobby : 0;
    const bool                      on_back = IsBack(g_row);
    std::vector<NetSteamFriendHost> found;
    const int                       answer = NetSteamFriendsHosting(found);
    bool                            changed = answer != g_friends_answer || found.size() != g_friends.size();

    for (std::size_t i = 0; !changed && i < found.size(); i++) {
        changed = found[i].lobby != g_friends[i].lobby || found[i].name != g_friends[i].name || found[i].other_build != g_friends[i].other_build;
    }
    g_friends = found;
    g_friends_answer = answer;
    if (!changed) {
        return;
    }
    g_page = std::min(g_page, Pages() - 1);

    int row = on_back || was == 0 ? std::min(g_row, Count() - 1) : Count() - 1;

    for (std::size_t i = 0; !on_back && was != 0 && i < g_friends.size(); i++) {
        if (g_friends[i].lobby == was) {
            g_page = (int) i / kFriendsPage;
            row = (int) i % kFriendsPage;
        }
    }
    if (on_back) {
        row = Count() - 1;
    }
    std::fprintf(stderr, "title-mp: Join > Steam lists %d friend(s) hosting (answer %d), page %d of %d\n", (int) g_friends.size(), answer, g_page + 1,
                 Pages());
    Go(kFriends, row);

    // Test hook (DC_TITLE_MP_TEST_LOOKED_FILE=<file>): the list's first answer with nobody hosting writes that go file (mp-nat
    // -EarlyJoin: the host's Steam lobby waits on it, DC_MOCK_STEAM_LOBBY_GO, so this look comes first by order, not by time).
    if (const char *looked = TestEnv("DC_TITLE_MP_TEST_LOOKED_FILE"); looked != nullptr && answer != 0 && g_friends.empty()) {
        if (std::FILE *file = std::fopen(looked, "w")) {
            std::fclose(file);
            std::fprintf(stderr, "title-mp: Join > Steam: test: nobody hosting yet, go file written\n");
        }
    }

    // A test that walks Join > Steam to test what comes after the pick (mp-nat, mp-steam-join, l-lan-pack): its pick made
    // here, the hand on that friend and Cross as a player's, once the friend is listed (no fixed-frame Cross that may land
    // on Back while the list is still empty). DC_TITLE_MP_TEST_PICK=first (the first friend of this build) or a friend's
    // Steam name; a player never sets it.
    // Once a run: after a join given up (mp-nat -Steam -Blocked) the list fills again, and a second pick would start the
    // punch over, which a player at the menu does not (and its link file then lost the first join's story).
    static bool test_picked = false;

    if (const char *pick = test_picked ? nullptr : TestEnv("DC_TITLE_MP_TEST_PICK")) {
        for (std::size_t i = 0; i < g_friends.size(); i++) {
            if (!g_friends[i].other_build && (std::strcmp(pick, "first") == 0 || g_friends[i].name == pick)) {
                g_page = (int) i / kFriendsPage;
                Go(kFriends, (int) i % kFriendsPage);
                std::fprintf(stderr, "title-mp: Join > Steam: test pick %s (DC_TITLE_MP_TEST_PICK=%s)\n", g_friends[i].name.c_str(), pick);
                test_picked = true;
                Choose();
                break;
            }
        }
    }
}

// Left or Right on Join > Steam's list: the next or last page of friends, when there is more than one.
void Page(int by) {
    const int was = g_page;

    g_page = std::max(0, std::min(Pages() - 1, g_page + by));
    if (g_page != was) {
        TiPlayVolSE(MIDI_PORT_UNK_D, 122, 24, 1.0f);
        std::fprintf(stderr, "title-mp: Join > Steam: page %d of %d\n", g_page + 1, Pages());
        Go(kFriends, std::min(g_row, Count() - 1));
    }
}

// The status font's lettering on a row, for a Steam name the menu's letters cannot show (digits, symbols, other
// alphabets): from the menu's left edge, their cream when lit, the dimmed rows' grey otherwise.
void DrawLettering(const char *text, int row, bool lit, int fade) {
    dc::text::caption::LineStyle  style;
    dc::text::caption::LineBounds bounds;
    const float         cap_top = (float) dc::title::menu::TextTop(row);

    style.status_font = true;
    style.ink_fit = true;
    for (int c = 0; c < 3; c++) {
        style.fill[c] = lit ? kFill[c] : kDimFill[c];
        style.edge[c] = lit ? 0 : 24;
    }
    style.edge_width = 1.0f;
    style.letter = 1.0f;
    dc::text::caption::DrawLine(text, -1000.0f, cap_top, kCapHeight, kSqueeze, 400.0f, style, 0.0f, 1.0f, &bounds); // measured off screen
    const float width = 2.0f * (-1000.0f - bounds.left);
    dc::text::caption::DrawLine(text, kLeft + width / 2.0f, cap_top, kCapHeight, kSqueeze, 400.0f, style, (float) fade / 128.0f, 1.0f, nullptr);
}

// The line under the menu: what the row the hand is on does.
const char *RowHint() {
    if (IsBack(g_row)) {
        return "Back to the menu before";
    }
    switch (g_level) {
        case kMulti:
            return g_row == 0 ? "Host a game: friends join your lobby" : g_row == 1 ? "Join a friend's game" : "Every player on this PC";
        case kWay:
            switch (g_row) {
                case kIp:
                    return "Type the host's IP address";
                case kCode:
                    return "Type the host's code: no port forwarding";
                default: {
                    // coop-steamsetup: what Steam joins still need, when something is missing (steam_check.hpp).
                    static std::string hint;

                    hint = coop::steamcheck::Get().Ready() ? "Pick a Steam friend who is hosting" : coop::steamcheck::Line();
                    return hint.c_str();
                }
            }
        case kFriends: {
            static std::string hint;
            const NetSteamFriendHost *host = FriendAt(g_row);

            if (host == nullptr) {
                hint = g_friends_answer < 0 ? std::string(NetSteamStatus()) + " (trying again every few seconds)" : g_friends_answer == 0 ? "Looking for Steam friends hosting..." : "No Steam friend is hosting yet: the list looks again every few seconds";
            } else if (host->other_build) {
                hint = host->name + " runs another build of Dark Cloud co-op";
            } else {
                hint = (host->invited ? host->name + " invited you: join their game" : "Join " + host->name + "'s game") +
                       (Pages() > 1 ? " (page " + std::to_string(g_page + 1) + " of " + std::to_string(Pages()) + ")" : "");
            }
            return hint.c_str();
        }
        case kStart:
        case kTop:
            break;
    }
    return "";
}
} // namespace

bool TitleMpShown() {
    return NetplayTitleMenuShown();
}

int TitleMpRows() {
    return g_level != kTop ? Count() : dc::title::menu::RowCount();
}

void TitleMpEnter() {
    if (!TitleMpShown()) {
        return;
    }

    TiPlayVolSE(MIDI_PORT_SE_DEFAULT, 38, 20, 1.0f);
    Go(kMulti, 0);
}

void TitleMpClose() {
    coop::lobbymodels::DemoClose(); // coop-lobbymodels: the dev hook's models out as the title fades to a pick
    // coop-mainmenu (Jep, 2026-10-30: Back on the load screen Continue opened went to the main menu's top): Continue's
    // submenu stays, so the title comes back to it when the load screen closes; a save loaded starts the game, which
    // hosts, and the menu is then the plain one (TitleMpOpen). DC_TITLE_MP_MUTATE=close_on_load (the test's twin):
    // closed, as before.
    if (g_level == kStart && g_row == 1 && !TestEnvIs("DC_TITLE_MP_MUTATE", "close_on_load")) {
        if (!g_kept) {
            g_kept = true;
            std::fprintf(stderr, "title-mp: %s > Continue kept under the load screen\n", ViaName());
            NetplayTestInputMark("load"); // (a test's held --input script goes on: DC_TEST_INPUT_HOLD)
        }
        return;
    }
    g_level = kTop;
}

void TitleMpFromLobby() {
    // coop-lobby: the host left its lobby (Leave? Yes): back to Start / Continue under Host (or Local), the way kept. A guest's
    // lobby left (or the host started, closed or went silent): the menu stays at JOIN's level, its line saying why.
    if (coop::lobby::WasGuest()) {
        std::fprintf(stderr, "title-mp: back from a guest's lobby to %s\n", LevelName().c_str());
        return;
    }
    Go(kStart, 0);
    std::fprintf(stderr, "title-mp: back from the lobby to %s\n", LevelName().c_str());
}

// The lobby jump (DC_TITLE_MP_TEST_HOST, title_mpmenu.hpp): 0 not asked, 1 asked, 2 the pick made (its Choose due in the
// next TitleMpStep), 3 done.
static int &TestHostState() {
    static int state = TestEnv("DC_TITLE_MP_TEST_HOST") != nullptr ? 1 : 0;
    return state;
}

bool TitleMpTestStart() {
    return TestHostState() == 1;
}

bool TitleMpOpen() {
    if (g_level != kTop && !TitleMpShown()) {
        g_level = kTop; // hosted or joined meanwhile: the menu is the plain one again
    }
    // coop-fx51f (the front-end fixtures, Jep 2026-11-04: lobby and title rows start at their state too): =join-code or
    // =join-steam takes the guest's way instead, Multi-player > Join > Code (the code entry open, for the D-pad's typing)
    // or > Steam (the friend list), picked as a player's Crosses pick them, at the menu's first frame.
    // mp-title-row (Jep's 2a, 2026-11-10): =menu takes the title's Start presses only and leaves the menu as it opens, for
    // the row's own presses (anchored on this line).
    if (TestHostState() == 1 && TitleMpShown() && TestEnvIs("DC_TITLE_MP_TEST_HOST", "menu")) {
        TestHostState() = 3;
        std::fprintf(stderr, "title-mp: test: at the menu (DC_TITLE_MP_TEST_HOST=menu)\n");
    }
    if (TestHostState() == 1 && TitleMpShown() && (TestEnvIs("DC_TITLE_MP_TEST_HOST", "join-code") || TestEnvIs("DC_TITLE_MP_TEST_HOST", "join-steam"))) {
        const bool code = TestEnvIs("DC_TITLE_MP_TEST_HOST", "join-code");

        Go(kMulti, 1);
        Choose(); // (Join: its ways)
        Go(kWay, code ? kCode : kSteam);
        TestHostState() = 3;
        std::fprintf(stderr, "title-mp: test join: Multi-player > Join > %s (DC_TITLE_MP_TEST_HOST)\n", code ? "Code" : "Steam");
        Choose();
    }
    // coop-fx51e (l-local-fixture's front-local-seed): =local-continue takes Multi-player > Local, then Continue, as Jep's
    // own way in picks it, at the menu's first frame.
    if (TestHostState() == 1 && TitleMpShown() && TestEnvIs("DC_TITLE_MP_TEST_HOST", "local-continue")) {
        Go(kMulti, 2);
        Choose(); // (Local: its lobby pending, the start's rows)
        Go(kStart, 1);
        TestHostState() = 2;
        std::fprintf(stderr, "title-mp: test host: Multi-player > Local > Continue (DC_TITLE_MP_TEST_HOST)\n");
    }
    // The lobby jump: at the menu's first frame, Multi-player > Host, then Start (or Continue) chosen in TitleMpStep.
    if (TestHostState() == 1 && TitleMpShown()) {
        const bool continue_ = TestEnvIs("DC_TITLE_MP_TEST_HOST", "continue");

        Go(kMulti, 0);
        Choose(); // (Host: the way, the lobby pending)
        Go(kStart, continue_ ? 1 : 0);
        TestHostState() = 2;
        std::fprintf(stderr, "title-mp: test host: Multi-player > Host > %s (DC_TITLE_MP_TEST_HOST)\n", continue_ ? "Continue" : "Start");
    }
    // coop-lobby: the address entry's Square (SELECT's entry too): JOIN > STEAM's list of friends hosting, a pick to join.
    if (NetplayTitleTakeSteamList()) {
        g_side = 2;
        g_way = kSteam;
        g_friends.clear();
        g_friends_answer = 0;
        g_page = 0;
        Go(kFriends, 0);
    }

    return g_level != kTop;
}

bool TitleMpStep(int pointer_row, bool pointer_confirm, bool pointer_back) {
    if (TestHostState() == 2) {
        TestHostState() = 3;
        return Choose();
    }
    if (g_kept) {
        g_kept = false;
        std::fprintf(stderr, "title-mp: back from the load screen to %s, row %d\n", LevelName().c_str(), g_row);
    }

    FriendsLook();

    const bool arrived = CCursol.Move() != 0;

    if (pointer_row >= 0 && pointer_row < Count() && pointer_row != g_row) {
        TiPlayVolSE(MIDI_PORT_UNK_D, 122, 24, 1.0f);
        Go(g_level, pointer_row);
    }

    if (!arrived && pointer_row < 0) {
        return false;
    }

    if (GamePad.Down(PAD_UP)) {
        TiPlayVolSE(MIDI_PORT_UNK_D, 122, 24, 1.0f);
        Go(g_level, (g_row + Count() - 1) % Count());
    } else if (GamePad.Down(PAD_DOWN)) {
        TiPlayVolSE(MIDI_PORT_UNK_D, 122, 24, 1.0f);
        Go(g_level, (g_row + 1) % Count());
    } else if (g_level == kFriends && (GamePad.Down(PAD_LEFT) || GamePad.Down(PAD_RIGHT))) {
        Page(GamePad.Down(PAD_LEFT) ? -1 : 1);
    } else if (GamePad.Down(PAD_START) || GamePad.Down(PAD_CROSS) || pointer_confirm) {
        return Choose();
    } else if (GamePad.Down(PAD_CIRCLE) || GamePad.Down(PAD_TRIANGLE) || pointer_back) { // Triangle: the keyboard's Tab, as in the address entry
        Back();
    }

    return false;
}

void TitleMpDraw(int selected_alpha, int other_alpha, int fade, bool hint) {
    coop::lobbymodels::DemoDraw(fade); // coop-lobbymodels: DC_LOBBY_MODELS_DEMO, six wells over the menu (dev hook)
    // (the Multi-player row itself is one of the title menu's rows: title::menu::Draw)
    if (!TitleMpShown() || g_level == kTop) {
        return;
    }

    for (int row = 0; row < Count(); row++) {
        const bool chosen = row == g_row;
        // A row not built yet at half its alpha.
        const int  alpha = (chosen ? selected_alpha : other_alpha) / (Built(row) ? 1 : 2);

        // A friend's name in the menu's letters when it is all capitals they have (as typed, upper-cased); any other
        // name (digits, symbols, other alphabets) in the status font's lettering, as Steam shows it.
        if (g_level == kFriends && !IsBack(row)) {
            std::string upper = Word(row);
            bool        plain = !upper.empty() && upper.size() <= 14;

            for (char &c : upper) {
                c = (char) std::toupper((unsigned char) c);
                plain = plain && ((c >= 'A' && c <= 'Z') || c == ' ' || c == '-');
            }
            if (!plain) {
                // (lit: chosen, built, and not on retail's blink, which draws the chosen row at the others' alpha)
                DrawLettering(Word(row).c_str(), row, chosen && Built(row) && selected_alpha > other_alpha, fade);
                continue;
            }
            dc::title::menu::DrawRow(row, upper, chosen, alpha);
            continue;
        }
        dc::title::menu::DrawRow(row, Word(row), chosen, alpha);
    }

    if (hint && g_level == kStart) {
        NetplayDrawTitleJoin(); // how friends join this host
    } else if (hint) {
        dc::text::caption::LineStyle style;

        style.status_font = true;
        style.edge_width = 1.5f;
        // A dark band under the two lines only where they sit over the copyright line, as the notice's (NetplayTitleNoticeBand).
        if (NetplayTitleNoticeBand()) {
            static const spRGBA kBand = {0x00, 0x00, 0x00, 0x68};

            set2DSpriteC4(Vif1Packet, CRect_i_(40, (int) NetplayTitleNoticeY(0) - 10, 560, 36), (spRGBA *) &kBand, (spRGBA *) &kBand, (spRGBA *) &kBand, (spRGBA *) &kBand);
        }
        // coop-natsplit: why the last join was given up, in the hint's place for a while (the menu stays where it was).
        const char *note = NetplayTitleNote();

        if (note[0] != '\0') {
            static std::string said;

            if (said != note) {
                said = note;
                std::fprintf(stderr, "title-mp: note drawn: %s\n", note);
            }
        }
        dc::text::caption::DrawLine(note[0] != '\0' ? note : RowHint(), 320.0f, NetplayTitleNoticeY(0), 10.0f, 1.0f, 560.0f, style, 1.0f, 1.0f, nullptr);
        dc::text::caption::DrawLine(g_level == kFriends && Pages() > 1 ? "Cross: join   Circle: back   Left/Right: more friends"
                          : g_level == kFriends              ? "Cross: join   Circle: back"
                                                             : "Cross: choose   Circle: back",
                          320.0f,
                          NetplayTitleNoticeY(1), 8.0f, 1.0f, 560.0f, style, 1.0f, 1.0f, nullptr);
    }
}
