#include "coop/game_ns.hpp"
#include "coop/coop_lobby.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "coop/coop.hpp"
#include "coop/coop_inventory.hpp" // coop-lobbyslots: each bag is its profile's
#include "coop/coop_party.hpp"
#include "coop/netplay.hpp"
#include "coop/netplay_local.hpp"
#include "coop/netplay_socket.hpp"
#include "coop/netplay_steam.hpp"
#include "coop/netplay_upnp.hpp"
#include "coop/steam_check.hpp" // coop-steamsetup
#include "common.h"
#include "dataread.hpp"
#include "dngstatusdata.hpp"
#include "dun/gameloop.hpp"
#include "gamepad.hpp"
#include "memcard.hpp"
#include "menu_draw.hpp"
#include "mglib.hpp"
#include "rect.hpp"
#include "save/save_slots.hpp" // coop-lobbyslots: DC_LOBBY_TEST_RELOAD_SLOT
#include "snd.hpp"
#include "sound.hpp"
#include "main.hpp" // coop-titleopen51: mode, GAME_MODE_TITLE
#include "texture.hpp"
#include "title/scfader.hpp"
#include "title/title.hpp"
#include "platform/config.hpp"
#include "platform/input.hpp"
#include "platform/paths.hpp"
#include "ui/framework.hpp"

#if __has_include("coop/coop_lobby_models.hpp")
#include "coop/coop_lobby_models.hpp" // coop-lobbymodels: the 3D models in the wells (plan 4.9)
#define DC_LOBBY_MODELS 1
#else
#define DC_LOBBY_MODELS 0
#endif

namespace dc::game {
extern s32 mode; // coop-titleopen51: the game's mode (main.hpp's GameMode)
void TiPlayVolSE(int group, int no, int voice, float volume);
void MenuHelpWinDraw(int x, int y, float width, float height, int alpha);
void setbilinear(int n);
} // namespace dc::game
bool NetplayMutated(const char *name); // netplay_test.cpp (coop-reds1109: friend_code_late)

namespace coop {
namespace lobby {
namespace {

// The party in story order (plan 4.2: the boards across, in party order).
constexpr const char *kMembers[kSeats] = {"Toan", "Xiao", "Goro", "Ruby", "Ungaga", "Osmond"};

// Seat colours, the same on every screen (player-colours: P1 pink, P2 teal; P3 to P6 the plan's proposal, a picker later).
struct Rgb {
    unsigned char r, g, b;
};
constexpr Rgb kSeatColour[kSeats] = {
    {244, 142, 226}, // pink
    {40,  220, 230}, // teal
    {240, 190, 60 }, // amber
    {120, 220, 90 }, // green
    {170, 130, 240}, // violet
    {250, 140, 60 }, // orange
};

// The texture block the message windows' parts come from: the Options screen's own pack (option.pac), loaded as the title's
// Options screen loads it, into the block the title gives its child screens.
constexpr int kBlock = 2;
// The Items menu's pack (its boards): a block of its own beside the models' 0x3A-0x3F.
constexpr int kBoardBlock = 0x39;

// Layout C in the UI's 640 x 480 (lobby-mock-C2-host.png at half size; the seat cards 48 high, the stone plate's least).
constexpr int kLeft = 60;
constexpr int kPitch = 88;
constexpr int kColumn = 84;
constexpr int kHeaderY = 20;
constexpr int kHeaderH = 56;
constexpr int kSeatY = 82;
constexpr int kSeatH = 48;
constexpr int kBoardY = 136;
constexpr int kBoardH = 222;
constexpr int kWellInset = 8;
constexpr int kWellTop = 30;
constexpr int kWellH = 150;
constexpr int kHintX = 56;
constexpr int kHintY = 364;
constexpr int kHintW = 282;
constexpr int kHintH = 110;
constexpr int kNoteX = 346;
constexpr int kNoteY = 364;
constexpr int kNoteW = 238;
constexpr int kNoteH = 62;
constexpr int kPillY = 434;

enum class Phase { Closed, FadeIn, Run, FadeOut };

// Where the host's hand is: on the boards (choosing), or on one of the two check rows (Up from the boards; Cross opens its
// dialog box).
enum class Focus { Boards, PortRow, UpnpRow };

// ---- the lobby's messages (plan 4.7) ----
// "BLCD" datagrams over the link's own socket (or Steam path), before the lockstep: a guest's Hello (wants a seat) and
// Request (its hover, its wanted member, its leave question; each 250 ms and at once on a change: the state, never an
// event, so a lost one costs nothing), the host's Table (the whole seat table, the guest's seat, its refusals; each 250 ms
// and every frame of its fade out) and a guest's Bye. Their own version byte: the table may grow without a Hello bump.
constexpr std::uint32_t kLobbyMagic = 0x44434C42; // "BLCD"
constexpr std::uint8_t  kLobbyVersion = 3; // 2: the cards and models live (coop-lobbyleave); 3: coop-joinlobby's kTablePlaying
enum Kind : std::uint8_t { kHello = 1, kTable = 2, kRequest = 3, kBye = 4 };
enum TablePhase : std::uint8_t { kTableOpen = 0, kTableStarted = 1, kTableClosed = 2, kTablePlaying = 3 }; // 3: coop-joinlobby, the host's game takes a joiner
constexpr std::uint8_t kNone = 0xFF;

#pragma pack(push, 1)
struct WireSeat {
    std::uint8_t  present;
    std::uint8_t  pick;
    std::uint8_t  hover;
    std::uint8_t  leaving;
    std::uint64_t identity;
    char          name[16];
    std::uint8_t  naming; // the seat's card asks for its profile (plan 4.10)
    std::uint8_t  active; // the player pressed Start on their controller (the seat lit)
    std::int8_t   drive_x; // its locked model's stick (-127..127) and swing, its naming card's text (coop-lobbyleave)
    std::int8_t   drive_y;
    std::uint8_t  attack;
    std::uint8_t  typing;
    char          shown[16];
};
struct Wire {
    std::uint32_t magic;
    std::uint32_t player; // the sender's link player (1 host, 2 guest), as the link's own packets carry it
    std::uint8_t  version;
    std::uint8_t  kind;
    std::uint16_t seq;
    std::uint64_t identity; // the sender's
    // kTable
    std::uint8_t  your_seat;
    std::uint8_t  party;
    std::uint8_t  phase;
    std::uint8_t  refused; // the member last refused to your seat
    std::uint16_t refuse_seq;
    std::uint16_t echo; // the guest's last Request seq heard (its round trip)
    WireSeat      seats[kSeats];
    char          profiles[8][16]; // the host's profiles free for the guest to pick ("" past the last)
    std::uint8_t  name_refused;    // the name the guest asked for is in use
    // kHello / kRequest
    std::uint8_t  hover;
    std::uint8_t  want;
    std::uint8_t  leaving;
    char          name[16];
    std::uint8_t  named; // the guest picked or made its profile: name is it
    std::uint8_t  active; // the guest has pressed Start (its seat lit)
    std::int8_t   drive_x; // the guest's locked model's stick and swing, its naming card's text (coop-lobbyleave)
    std::int8_t   drive_y;
    std::uint8_t  attack;
    std::uint8_t  typing;
    char          shown[16];
};
#pragma pack(pop)

// One check's row word and its dialog box's text (plan 4.3), and the log's names for it.
struct Check {
    std::string row;
    std::string text;
    std::string state; // the log's state
    std::string from;  // what it was built from
};

struct State {
    Phase       phase = Phase::Closed;
    Result      leaving = Result::Running; // what the fade out ends in
    bool        pending = false;
    bool        local_only = false;
    bool        new_game = false;
    int         party = 1;
    Seat        seats[kSeats];
    bool        tex_loading = false;
    bool        tex_ready = false;
    int         tex_rereads = 0; // the background reads given again after another reader took the slot (LoadTextures)
    CTexture   *board_tex = nullptr; // the Items menu's stone board ("status"), once its pack is read
    std::string note;
    int         note_frames = 0;
    int         frames = 0;
    int         shot_frames = 0;
    // The IP way's checks (stage 3): the host's own.
    Focus                                 focus = Focus::Boards;
    bool                                  lan_shown = false;
    std::chrono::steady_clock::time_point opened{};
    std::string                           port_said, upnp_said, code_said;
    // The guests' side (stage 4).
    bool                                  guest = false; // this copy joined another's lobby
    int                                   me = 0;        // this copy's seat
    int                                   want = -1;     // a guest's wanted member (its lock, until the host's table says)
    int                                   refuse_seen = 0;
    std::uint16_t                         seq = 0;
    std::uint16_t                         echo_seen = 0;
    int                                   rtt_ms = -1;
    std::chrono::steady_clock::time_point sent_at[16]{};
    std::chrono::steady_clock::time_point heard{};    // a guest: the host's last table
    std::chrono::steady_clock::time_point send_due{}; // the next periodic send
    bool                                  starting = false; // a guest: the host started
    bool                                  playing = false;  // coop-joinlobby: the host's game serves a joiner's card (the lobby closed here)
    std::vector<std::string>              guest_profiles;   // a guest: the host's profiles it may pick
    std::string                           asked_name;       // a guest: the profile it asked for
    // The host's Friend's code (plan 4.3a): the entry open (Triangle), its letters, the letter under change; the code taken.
    bool                                  code_open = false;
    std::string                           code_text;
    int                                   code_letter = 0;
    std::string                           friend_code;
    std::chrono::steady_clock::time_point friend_at{};
    // DC_LOBBY_TEST_PRESS: the scripted presses done so far.
    std::size_t                           pressed = 0;
    std::chrono::steady_clock::time_point shown_at{};
    // Per seat input (Jep's playtest): the keyboard's keys last frame (KeyboardTyping); the locking Cross let go (a swing
    // only after it); a guest picking its profile again (its card asks though the host's table says it is named).
    bool          presses_done = false; // every DC_LOBBY_TEST_PRESS press made (or none given)
    bool          keys[38] = {};
    bool          attack_armed = true;
    bool          renaming = false;
    bool          steam_waiting = false; // coop-pt1108s: Select asked while Steam was not up: the helper tries again, the lobby opens when it can
    bool          steam_asked = false; // the host's Select asked for Steam (listening, or its lobby still opening)
    // coop-consent (Jep, 2026-11-02: never decided for the player): the host's outside IP shown in the header
    // (game.lobby_show_ip, R1; hidden until its player shows it).
    bool          ip_shown = false;
    // Each character card's state (Card, -1 not drawn yet), its brightness this frame, the frame its state began and
    // whether its ease has settled (CardShade).
    int           card_state[kSeats] = {-1, -1, -1, -1, -1, -1};
    float         card_shade[kSeats] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    int           card_since[kSeats] = {};
    bool          card_settled[kSeats] = {};
    // Players on this PC (plan 4.6): the controller that walked the title (the host's own seat), each controller's last
    // buttons (Start on one nobody holds takes a seat).
    int           host_pad = 0;
    std::uint16_t host_down = 0; // the host's own controller this frame (StepLocalPads), for its seat (OwnPad)
    std::uint16_t host_on = 0;
    std::uint16_t pad_last[kInputPadCount] = {};
    // A guest copy's own controller (StepGuestPad): this frame's presses, its buttons, and its stick for a locked model.
    std::uint16_t guest_down = 0;
    std::uint16_t guest_on = 0;
    std::uint16_t guest_last = 0;
    InputPadState guest_pad;
    // The live parts this copy last sent (LiveSign: its seats' cards and models), and when.
    std::uint64_t                         live_sent = 0;
    std::chrono::steady_clock::time_point live_at{};
    bool                                  live_said[kSeats] = {};
};

// The code's letters (natcode: base 30, no B I L O S Z).
constexpr char kCodeLetters[] = "0123456789ACDEFGHJKMNPQRTUVWXY";
constexpr int  kCodeLetterCount = 30;

dc::ui::DialogBox g_dialog;

State g;

// This copy's identity in the lobby: game.netplay_player_id (made once, kept in its own config), as its Hello carries it.
std::uint64_t Identity() {
    return NetplayPlayerId();
}

// ---- profiles (plan 4.10: Smash-style, kept by the host in its save folder, netplay-profiles.json) ----
struct Profile {
    std::string                name;
    int                        last = -1; // the member last played
    std::vector<std::uint64_t> ids;       // the identities that used it (a returning player is matched by itself)
};
std::vector<Profile> g_profiles;
bool                 g_profiles_read = false;

std::filesystem::path ProfilesPath() {
    return PathsSaveRoot() / "netplay-profiles.json";
}

void ReadProfiles() {
    if (g_profiles_read) {
        return;
    }
    g_profiles_read = true;
    std::ifstream in(ProfilesPath());

    if (!in) {
        return;
    }
    try {
        const nlohmann::json root = nlohmann::json::parse(in);

        for (const auto &item : root.value("profiles", nlohmann::json::array())) {
            Profile profile;

            profile.name = item.value("name", std::string());
            profile.last = item.value("last", -1);
            for (const auto &id : item.value("ids", nlohmann::json::array())) {
                profile.ids.push_back(std::strtoull(id.get<std::string>().c_str(), nullptr, 16));
            }
            if (!profile.name.empty()) {
                g_profiles.push_back(profile);
            }
        }
    } catch (const std::exception &e) {
        std::fprintf(stderr, "lobby: profiles not read (%s)\n", e.what());
    }
    std::fprintf(stderr, "lobby: %zu profiles\n", g_profiles.size());
}

void WriteProfiles() {
    nlohmann::json root;

    root["profiles"] = nlohmann::json::array();
    for (const Profile &profile : g_profiles) {
        nlohmann::json item;
        char           id[17];

        item["name"] = profile.name;
        item["last"] = profile.last;
        item["ids"] = nlohmann::json::array();
        for (std::uint64_t value : profile.ids) {
            std::snprintf(id, sizeof id, "%016llx", (unsigned long long) value);
            item["ids"].push_back(id);
        }
        root["profiles"].push_back(item);
    }
    std::ofstream out(ProfilesPath(), std::ios::trunc);

    out << root.dump(2) << "\n";
}

// Profile names match whatever their letters' case (the lobby types capitals: JEP is Jep's profile, never a second one).
bool SameName(const std::string &a, const std::string &b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) { return std::toupper((unsigned char) x) == std::toupper((unsigned char) y); });
}

Profile *ProfileNamed(const std::string &name) {
    for (Profile &profile : g_profiles) {
        if (SameName(profile.name, name)) {
            return &profile;
        }
    }
    return nullptr;
}

Profile *ProfileOf(std::uint64_t identity) {
    for (Profile &profile : g_profiles) {
        if (std::find(profile.ids.begin(), profile.ids.end(), identity) != profile.ids.end()) {
            return &profile;
        }
    }
    return nullptr;
}

// Per seat, the host's bookkeeping of a guest: when it was last heard, the member it wants, the last refusal.
struct Remote {
    std::chrono::steady_clock::time_point heard{};
    int                                   want = -1;
    int                                   refused = -1;
    int                                   refuse_seq = 0;
    std::uint16_t                         echo = 0;
    bool                                  name_refused = false;
};
Remote g_remote[kSeats];

// coop-joinlobby: the joiner's link Hellos wait for its card (its pick accepted by the host's game, or the host's lobby
// started: JoinHoldsHello); and the host's pick for the joiner at its link (LateJoin), carried by the catch-up's last blob
// (JoinPickTake) and given on both copies at the lockstep.
bool        g_join_go = false;
Wire        g_last_table{};             // coop-reds1109: the host's last table, for DC_LOBBY_TEST_STALE_TABLE
bool        g_have_last_table = false;
bool        g_join_carded = false;      // coop-reds1109: this join went on from its card (the host took its seat)
bool        g_stale_fed = false;        // coop-reds1109: DC_LOBBY_TEST_STALE_TABLE fed its one stale table
bool        g_join_pick_set = false;
int         g_join_pick_member = -1;
std::string g_join_pick_name;

// coop-joinlobby step 3b: the players who left seat 2 of this host's game to another joiner (player id -> its profile and
// member), for their rejoin with no card. The host's session (cleared when its lobby opens again), never saved.
struct Rejoin {
    int         pick = -1;
    std::string name;
};
std::map<std::uint64_t, Rejoin> g_rejoin;

constexpr std::chrono::milliseconds kSendEvery{250};
constexpr std::chrono::seconds      kSilence{10};

void Send(Kind kind);

bool Mutated(const char *what) {
    return TestEnvIs("DC_LOBBY_MUTATE", what);
}

int Present() {
    int n = 0;

    for (const Seat &seat : g.seats) {
        n += seat.present ? 1 : 0;
    }
    return n;
}

// Members on offer: the save's party, and the next members in story order until there are as many as players (Jep, 20:45Z:
// N players always have N members; a party of one still lends Xiao, as the game's stand-in rule does).
int Usable() {
    return std::clamp(std::max({g.party, 2, Present()}), 1, kSeats);
}

bool IsUsable(int member) {
    return member >= 0 && member < Usable();
}

// Every member's model, those not yet in the story too (Jep, 2026-10-31: all six on screen, those as silhouettes; the
// cards set each one's silhouette as they first draw, CardShade).
void OpenModels() {
#if DC_LOBBY_MODELS
    int members[kSeats];
    for (int m = 0; m < kSeats; m++) {
        members[m] = m;
        g.card_state[m] = -1;
    }
    lobbymodels::Open(members, kSeats);
#endif
}

// The seat that locked member, -1 none.
int LockedBy(int member) {
    for (int s = 0; s < kSeats; s++) {
        if (g.seats[s].present && g.seats[s].pick == member) {
            return s;
        }
    }
    return -1;
}

bool Free(int member, int for_seat) {
    const int by = LockedBy(member);

    // DC_LOBBY_MUTATE=pick_shared (l-lobby-two-lock's twin): a face locked by one seat may be locked by another too.
    return IsUsable(member) && (by < 0 || by == for_seat || Mutated("pick_shared"));
}

bool EveryoneLocked() {
    if (Mutated("start_unlocked")) {
        return true;
    }
    for (const Seat &seat : g.seats) {
        if (seat.present && seat.pick < 0) {
            return false;
        }
    }
    return Present() > 0;
}

std::string SeatName(int s) {
    return g.seats[s].name.empty() ? "Player " + std::to_string(s + 1) : g.seats[s].name;
}

void Note(const std::string &text) {
    if (g.playing) {
        return; // (coop-joinlobby: the host's game draws no lobby: its player sees nothing of a joiner's card)
    }
    g.note = text;
    g.note_frames = 180;
}

void SoundMove() {
    TiPlayVolSE(MIDI_PORT_UNK_D, 122, 24, 1.0f);
}

void SoundPick() {
    TiPlayVolSE(MIDI_PORT_SE_DEFAULT, 38, 20, 1.0f);
}

void SoundRefuse() {
    TiPlayVolSE(MIDI_PORT_UNK_D, 122, 25, 1.0f);
}

// The first member at or after from (in step's direction) the seat may take; from itself if none.
int NextFree(int s, int from, int step) {
    const int usable = Usable();

    for (int i = 0; i < usable; i++) {
        const int member = ((from + step * i) % usable + usable) % usable;

        if (Free(member, s)) {
            return member;
        }
    }
    return from;
}

// The member Left / Right shows next in seat s's own slot (Jep, 2026-11-01: each player's own slot, Left/Right changes who
// is in it): every member in turn, those another seat has locked (darkened) and those not yet in the story (silhouettes)
// too, so the slot shows why one is not on offer; Cross refuses those (Lock). DC_LOBBY_MUTATE=slots_free_only
// (l-lobby -Case slots' twin): the old board walk, free members only.
int NextShown(int s, int from, int step) {
    if (Mutated("slots_free_only")) {
        return NextFree(s, from + step, step);
    }
    return ((from + step) % kSeats + kSeats) % kSeats;
}

// A new seat's first member: the first free one no other seat's slot shows (two players never start on one face), else
// the first free one.
int FirstShown(int s) {
    for (int member = 0; member < Usable(); member++) {
        bool shown = false;

        for (int other = 0; other < kSeats; other++) {
            shown = shown || (other != s && g.seats[other].present && (g.seats[other].pick == member || g.seats[other].hover == member));
        }
        if (!shown && Free(member, s)) {
            return member;
        }
    }
    return NextFree(s, 0, 1);
}

bool NameInUse(const std::string &name, int except) {
    for (int s = 0; s < kSeats; s++) {
        if (s != except && g.seats[s].present && !g.seats[s].naming && SameName(g.seats[s].name, name)) {
            return true;
        }
    }
    return false;
}

// The host: seat s takes profile name (made if new), remembered for its identity.
bool Name(int s, const std::string &name) {
    Seat &seat = g.seats[s];

    // DC_LOBBY_MUTATE=join_refuse_off (l-lobby-join-card-taken's twin): the host's game takes a joiner's profile in use.
    if (name.empty() || (NameInUse(name, s) && !(g.playing && Mutated("join_refuse_off")))) {
        std::fprintf(stderr, "lobby: seat %d profile %s refused (in use)\n", s + 1, name.c_str());
        return false;
    }
    Profile *profile = ProfileNamed(name);

    if (profile == nullptr) {
        g_profiles.push_back(Profile{name});
        profile = &g_profiles.back();
        std::fprintf(stderr, "lobby: profile %s made\n", name.c_str());
    }
    if (seat.identity != 0 && std::find(profile->ids.begin(), profile->ids.end(), seat.identity) == profile->ids.end()) {
        for (Profile &other : g_profiles) {
            other.ids.erase(std::remove(other.ids.begin(), other.ids.end(), seat.identity), other.ids.end());
        }
        profile->ids.push_back(seat.identity);
    }
    seat.name = profile->name; // (the profile's own spelling)
    seat.naming = false;
    WriteProfiles();
    std::fprintf(stderr, "lobby: seat %d is %s\n", s + 1, name.c_str());
    return true;
}

// The profiles a seat may pick: the host's, less those another present seat uses.
std::vector<std::string> FreeProfiles(int s) {
    std::vector<std::string> out;

    for (const Profile &profile : g_profiles) {
        if (!NameInUse(profile.name, s)) {
            out.push_back(profile.name);
        }
    }
    return out;
}

void TakeSeat(int s, bool local, std::uint64_t identity = 0) {
    Seat &seat = g.seats[s];

    seat = Seat{};
    seat.present = true;
    seat.local = local;
    seat.identity = identity;
    seat.hover = FirstShown(s);
    // The seat is lit by its player's Start (Jep, 2026-10-31); the host's own already pressed through the title.
    seat.active = local && s == 0;
    // A returning player is matched to its profile by itself; else its card asks (plan 4.10). DC_LOBBY_TEST_NAME=name: the
    // host's own seat takes that profile without the ask (a later test's).
    ReadProfiles();
    const Profile *known = identity != 0 ? ProfileOf(identity) : nullptr;
    const char    *test_name = s == 0 ? TestEnv("DC_LOBBY_TEST_NAME") : nullptr;

    seat.naming = true;
    if (known != nullptr && !NameInUse(known->name, s)) {
        Name(s, known->name);
        if (known->last >= 0 && Free(known->last, s)) {
            seat.hover = known->last;
        }
    } else if (test_name != nullptr) {
        Name(s, test_name);
    }
    std::fprintf(stderr, "lobby: seat %d taken (%s, on %s)\n", s + 1, seat.naming ? "asking its profile" : SeatName(s).c_str(), local ? "this PC" : "the link");
}

// A player on this PC takes seat s with its controller: lit at once (its Start took the seat), the screen question first,
// then its profile on its card. Its identity is this copy's and the seat's (one per seat on this PC, never 0): its window
// joins the game with it later, and its profile keeps it (a returning player on that seat picks its profile again).
void TakeLocalSeat(int s, int pad) {
    TakeSeat(s, true);

    Seat &seat = g.seats[s];

    seat.identity = (Identity() ^ (0x9E3779B97F4A7C15ull * (std::uint64_t) (s + 1))) | 1;
    seat.pad = pad;
    seat.active = true;
    seat.screen_ask = true;
}

// One seat's controller this frame (Jep's playtest, 2026-10-31: P2's presses reached P1's hand and P1's Leave?): the host's
// own seat takes its own controller alone (LAN pack 7, 2026-11-01: the game's pad was P2's controller, so masking her
// presses out of it left P1 none); each other local controller drives its own seat; a guest copy's seat its own controller
// (StepGuestPad).
struct Pad {
    std::uint16_t down = 0;
    std::uint16_t on = 0;

    bool Down(int mask) const {
        return (down & mask) != 0;
    }
    bool On(int mask) const {
        return (on & mask) != 0;
    }
};

Pad OwnPad() {
    Pad pad;

    // The host's seat: its own controller as StepLocalPads read it this frame, never the game's pad (on the title that is the
    // first controller, or the one client's controller last pressed: P2's in Jep's LAN pack 7 playtest).
    // DC_LOBBY_MUTATE=pad_follows (l-lobby -Case pads' twin): the game's pad, as before.
    if (!g.guest && !Mutated("pad_follows")) {
        pad.down = g.host_down;
        pad.on = g.host_on;
        return pad;
    }
    // A guest's seat (coop-lobbyleave, LAN pack 8, 2026-11-04: the friend's Start never lit his seat): its own controller's
    // presses as StepGuestPad read them. The game's pad cannot give them: GamePad.Down and On answer 0 or 1, not the buttons,
    // and Down is 0 for any mask with L3 or R3 in it, so Down(0xFFFF) read nothing at all; the tests lit a guest's seat by
    // DC_LOBBY_TEST_PRESS, past the pad. DC_LOBBY_MUTATE=guest_gamepad (l-lobby -Case guest-pad's twin): the game's pad, as before.
    if (g.guest && !Mutated("guest_gamepad")) {
        pad.down = g.guest_down;
        pad.on = g.guest_on;
        return pad;
    }
    pad.down = (std::uint16_t) GamePad.Down(0xFFFF);
    pad.on = (std::uint16_t) GamePad.On(0xFFFF);
    return pad;
}

// A guest copy's own controller this frame (NetplayLobbyOwnPad: the one its join reads), read every frame of its lobby so
// a press is new once. (kInput* and PAD_* are the same bits; a controller pulled out reads released.)
void StepGuestPad() {
    g.guest_pad = NetplayLobbyOwnPad();
    const std::uint16_t buttons = g.guest_pad.connected ? g.guest_pad.buttons : 0;

    g.guest_down = (std::uint16_t) (buttons & ~g.guest_last);
    g.guest_on = buttons;
    g.guest_last = buttons;
}

// The keyboard's typing for a name or a code (Jep: the keyboard should work for both): letters and digits as typed (only
// those in allowed), Backspace deletes, Enter is done. True when a key was typed or is still held this frame (its pad presses
// then go unread: the keyboard's letters are the pad's buttons too). Read every frame of a typing seat (StepLocalPads), so a
// key's last state is never stale.
// Test hook (DC_LOBBY_TEST_KEYS="keys@from-to,..."): the keyboard as if each item's keys (A-Z, 0-9; '<' Backspace, '>'
// Enter) were held from..to seconds after the lobby showed, beside the real keyboard (an input script's keys never reach
// SDL's keyboard state, which the lobby reads). A row's typist on the lobby's own clock (l-lobby -Case typist).
bool TestKeyHeld(SDL_Scancode code) {
    static const char *const spec = TestEnv("DC_LOBBY_TEST_KEYS");

    if (spec == nullptr || g.phase != Phase::Run) {
        return false;
    }
    const double now = std::chrono::duration<double>(std::chrono::steady_clock::now() - g.shown_at).count();

    for (const char *at = spec; at != nullptr && *at != '\0';) {
        const char *sign = std::strchr(at, '@');
        double      from = 0.0, to = -1.0;

        if (sign == nullptr || std::sscanf(sign + 1, "%lf-%lf", &from, &to) != 2) {
            return false;
        }
        for (const char *c = at; c < sign && now >= from && now <= to; c++) {
            const SDL_Scancode key = *c >= 'A' && *c <= 'Z' ? (SDL_Scancode) (SDL_SCANCODE_A + (*c - 'A'))
                                     : *c == '0'            ? SDL_SCANCODE_0
                                     : *c >= '1' && *c <= '9' ? (SDL_Scancode) (SDL_SCANCODE_1 + (*c - '1'))
                                     : *c == '<'            ? SDL_SCANCODE_BACKSPACE
                                     : *c == '>'            ? SDL_SCANCODE_RETURN
                                                            : SDL_SCANCODE_UNKNOWN;
            if (key == code) {
                return true;
            }
        }
        at = std::strchr(sign, ',');
        at = at != nullptr ? at + 1 : nullptr;
    }
    return false;
}

// Whether any key is held for the test (DC_LOBBY_TEST_KEYS).
bool TestAnyKeyHeld() {
    for (int code = SDL_SCANCODE_A; code <= SDL_SCANCODE_0; code++) {
        if (TestKeyHeld((SDL_Scancode) code)) {
            return true;
        }
    }
    return TestKeyHeld(SDL_SCANCODE_RETURN) || TestKeyHeld(SDL_SCANCODE_BACKSPACE);
}

bool KeyboardTyping(std::string &text, const char *allowed, std::size_t most, bool *enter) {
    const bool *state = SDL_GetKeyboardState(nullptr);
    bool        typed = false;

    *enter = false;
    if (state == nullptr) {
        return false;
    }
    for (int i = 0; i < 38; i++) {
        const bool      letter = i < 26;
        const char      c = letter ? (char) ('A' + i) : i < 36 ? (char) ('0' + (i - 26)) : 0;
        const SDL_Scancode code = letter ? (SDL_Scancode) (SDL_SCANCODE_A + i)
                                  : i < 36 ? (SDL_Scancode) (i == 26 ? SDL_SCANCODE_0 : SDL_SCANCODE_1 + (i - 27))
                                  : i == 36 ? SDL_SCANCODE_BACKSPACE : SDL_SCANCODE_RETURN;
        const bool      held = state[code] || TestKeyHeld(code);

        if (held && !g.keys[i]) {
            typed = true;
            if (i == 36) {
                if (!text.empty()) {
                    text.pop_back();
                }
            } else if (i == 37) {
                *enter = true;
            } else if (std::strchr(allowed, c) != nullptr && text.size() < most) {
                text += c;
            }
        }
        g.keys[i] = held;
        // (a key still held keeps the pad's read off too: the keyboard's keys are the first controller's buttons and stick)
        typed = typed || held;
    }
    return typed;
}

// The profile ask on this copy's own card: Left/Right through the free profiles and "New", Cross picks; "New" opens the
// name entry: the keyboard types it, or the pad (Up/Down a letter, Right add, Left delete); Cross or Enter done, Circle back.
// A seat's locking Cross let go: this copy's own seat's, or another local player's on the host's copy (each its own).
bool &Armed(int s) {
    return !g.guest && s != 0 && g.seats[s].local ? g.seats[s].armed : g.attack_armed;
}

// Another seat on this copy types its profile's name (the keyboard has one typist at a time: Jep's playtest).
bool OtherTyping(int s) {
    for (int other = 0; other < kSeats; other++) {
        if (other != s && g.seats[other].present && g.seats[other].typing) {
            return true;
        }
    }
    return false;
}

// The seat on this copy the keyboard belongs to now: the one typing its profile's name, or the host's while its friend's
// NAT code entry is open; -1 none.
int KeyboardTypist() {
    for (int s = 0; s < kSeats; s++) {
        if (g.seats[s].present && g.seats[s].naming && g.seats[s].typing) {
            return s;
        }
    }
    return g.code_open ? 0 : -1;
}

// Whether a key is held for a typist other than seat s (its keys are not s's buttons).
bool KeyboardHeldByOther(int s) {
    const int typist = KeyboardTypist();

    if (typist < 0 || typist == s) {
        return false;
    }
    int         count = 0;
    const bool *state = SDL_GetKeyboardState(&count);

    for (int i = 0; state != nullptr && i < count; i++) {
        if (state[i]) {
            return true;
        }
    }
    return TestAnyKeyHeld();
}

void StepNaming(int s, const Pad &pad) {
    Seat                          &seat = g.seats[s];
    const std::vector<std::string> names = g.guest ? g.guest_profiles : FreeProfiles(s);
    const int                      count = (int) names.size() + 1; // the last is "New"

    if (seat.typing) {
        static constexpr char kLetters[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
        constexpr int         kLetterCount = 36;
        bool                  enter = false;

        if (KeyboardTyping(seat.entry, kLetters, 10, &enter) && !enter) {
            return;
        }
        if (enter && !seat.entry.empty()) {
            seat.typing = false;
            if (g.guest) {
                g.asked_name = seat.entry;
                Send(kRequest);
            } else if (!Name(s, seat.entry)) {
                SoundRefuse();
                Note(seat.entry + " is in use");
                return;
            }
            SoundPick();
            return;
        }
        if (pad.Down(PAD_UP) || pad.Down(PAD_DOWN)) {
            if (seat.entry.empty()) {
                seat.entry = "A";
            }
            seat.letter = (seat.letter + (pad.Down(PAD_UP) ? kLetterCount - 1 : 1)) % kLetterCount;
            seat.entry.back() = kLetters[seat.letter];
            SoundMove();
        } else if (pad.Down(PAD_RIGHT) && seat.entry.size() < 10) {
            seat.entry += kLetters[seat.letter];
            SoundMove();
        } else if (pad.Down(PAD_LEFT) && !seat.entry.empty()) {
            seat.entry.pop_back();
            SoundMove();
        } else if (pad.Down(PAD_CROSS) && !seat.entry.empty()) {
            seat.typing = false;
            if (g.guest) {
                g.asked_name = seat.entry;
                Send(kRequest);
            } else if (!Name(s, seat.entry)) {
                SoundRefuse();
                Note(seat.entry + " is in use");
                return;
            }
            SoundPick();
        } else if (pad.Down(PAD_CIRCLE)) {
            seat.typing = false;
            SoundMove();
        }
        return;
    }
    if (pad.Down(PAD_LEFT) || pad.Down(PAD_RIGHT)) {
        seat.profile = (seat.profile + (pad.Down(PAD_LEFT) ? count - 1 : 1)) % count;
        SoundMove();
    } else if (pad.Down(PAD_CROSS)) {
        if (seat.profile >= (int) names.size()) {
            // One typist at a time on this copy: not while the friend's NAT code or another local player's name is typed.
            if (g.code_open || OtherTyping(s)) {
                SoundRefuse();
                return;
            }
            seat.typing = true;
            seat.entry.clear();
            seat.letter = 0;
            SoundPick();
            return;
        }
        SoundPick();
        if (g.guest) {
            g.asked_name = names[seat.profile];
            Send(kRequest);
        } else if (!Name(s, names[seat.profile])) {
            SoundRefuse();
        }
    }
}

// ---- the live parts (coop-lobbyleave, Jep's pack 8 playtest, 2026-11-04: "it didn't sync that P2 was selecting their profile
// until they had already picked it. Neither player can see each other moving in the lobby"): a seat's naming card and its
// locked model's stick and swing go to the other copies as they change, not only its hand and its lock.
// DC_LOBBY_MUTATE=live_local (l-lobby -Case live's twin): neither is sent or shown, as before.

std::string NameOf(const char in[16]);

// Whether this copy plays seat s (the host: its own seat and its local controllers'; a guest: its own).
bool PlayedHere(int s) {
    return g.guest ? s == g.me : g.seats[s].local;
}

// A seat played here: its naming card's text as this copy shows it (the name typed, else the profile under its hand).
std::string CardText(int s) {
    const Seat &seat = g.seats[s];

    if (!seat.naming) {
        return "";
    }
    if (seat.typing) {
        return seat.entry;
    }
    const std::vector<std::string> names = g.guest ? g.guest_profiles : FreeProfiles(s);

    return seat.profile < (int) names.size() ? names[seat.profile] : "New";
}

std::int8_t StickByte(float v) {
    return (std::int8_t) std::lround(std::clamp(v, -1.0f, 1.0f) * 127.0f);
}

// The live parts of the seats this copy plays, as one number (a change sends them at once).
std::uint64_t LiveSign() {
    std::uint64_t sign = 1469598103934665603ull;

    for (int s = 0; s < kSeats; s++) {
        const Seat &seat = g.seats[s];

        if (!seat.present || !PlayedHere(s)) {
            continue;
        }
        const std::string text = CardText(s) + (seat.typing ? "_" : "");
        const int         parts[] = {s, StickByte(seat.drive_x) / 16, StickByte(seat.drive_y) / 16, seat.drive_attack ? 1 : 0};

        for (int part : parts) {
            sign = (sign ^ (std::uint64_t) (part + 1000)) * 1099511628211ull;
        }
        for (char c : text) {
            sign = (sign ^ (std::uint8_t) c) * 1099511628211ull;
        }
    }
    return sign;
}

// Another copy's seat, as its message carried it: its model's stick and swing, its card's text (logged as it changes).
void TakeLive(int s, std::int8_t x, std::int8_t y, std::uint8_t attack, std::uint8_t typing, const char shown[16]) {
    Seat             &seat = g.seats[s];
    const std::string text = NameOf(shown);

    seat.drive_x = x / 127.0f;
    seat.drive_y = y / 127.0f;
    seat.drive_attack = attack != 0;
    if (text != seat.shown || (typing != 0) != seat.shown_typing) {
        seat.shown = text;
        seat.shown_typing = typing != 0;
        if (!text.empty() || seat.shown_typing) {
            std::fprintf(stderr, "lobby: seat %d's card shows %s%s%s (its copy's)\n", s + 1, seat.shown_typing ? "Name: " : "< ", text.c_str(),
                         seat.shown_typing ? "_" : " >");
        }
    }
    if ((x != 0 || y != 0 || attack != 0) && !g.live_said[s]) {
        g.live_said[s] = true;
        std::fprintf(stderr, "lobby: seat %d's model driven by its copy (stick %d %d, swing %d)\n", s + 1, x, y, attack);
    }
}

// Each frame: the locked models of the seats played on other copies, driven as those copies sent them.
void DriveOthers() {
#if DC_LOBBY_MODELS
    if (Mutated("live_local")) {
        return;
    }
    for (int s = 0; s < kSeats; s++) {
        const Seat &seat = g.seats[s];

        if (seat.present && seat.pick >= 0 && !PlayedHere(s)) {
            lobbymodels::Drive(seat.pick, seat.drive_x, seat.drive_y, seat.drive_attack);
        }
    }
#endif
}

void Lock(int s) {
    Seat &seat = g.seats[s];

    if (seat.naming) {
        return;
    }
    if (!Free(seat.hover, s)) {
        SoundRefuse();
        const int by = LockedBy(seat.hover);
        std::fprintf(stderr, "lobby: seat %d refused %s (%s)\n", s + 1, seat.hover >= 0 ? kMembers[seat.hover] : "?",
                     by >= 0 ? ("taken by seat " + std::to_string(by + 1)).c_str() : "not yet in the story");
        // (refused, the hand stays: Jep's playtest, 2026-11-03, the slot already says why. DC_LOBBY_MUTATE=refuse_moves (twin):
        // it moves on to the next free face, as before)
        if (Mutated("refuse_moves")) {
            seat.hover = NextFree(s, seat.hover, 1);
        }
        return;
    }
    SoundPick();
    if (g.guest) {
        // A guest asks; the host's table says whether it got it (one owner: two locks on one face cannot both win).
        g.want = seat.hover;
        std::fprintf(stderr, "lobby: seat %d asks to lock %s\n", s + 1, kMembers[g.want]);
        Send(kRequest);
        return;
    }
    Armed(s) = false; // (the Cross that locked is not also a swing: Jep's playtest)
    seat.pick = seat.hover;
    std::fprintf(stderr, "lobby: seat %d locked %s\n", s + 1, kMembers[seat.pick]);
#if DC_LOBBY_MODELS
    lobbymodels::SetLocked(seat.pick, true);
#endif
    Note(EveryoneLocked() ? "Everyone is locked in: Start when ready" : "");
}

void Unlock(int s) {
    Seat &seat = g.seats[s];

    SoundMove();
    std::fprintf(stderr, "lobby: seat %d unlocked %s\n", s + 1, kMembers[seat.pick]);
#if DC_LOBBY_MODELS
    lobbymodels::SetLocked(seat.pick, false);
#endif
    seat.hover = seat.pick;
    seat.pick = -1;
    if (g.guest) {
        g.want = -1;
        Send(kRequest);
    }
}

void Leave(int s) {
    std::fprintf(stderr, "lobby: seat %d left\n", s + 1);
    if (g.guest && s == g.me) {
        // A guest leaves: its Bye (three, in case one is lost), then its join is given up, back to JOIN's level.
        for (int i = 0; i < 3; i++) {
            Send(kBye);
        }
        NetplayLobbyLeaveJoin(nullptr);
        g.leaving = Result::Closed;
        g.phase = Phase::FadeOut;
        return;
    }
    if (s == 0) {
        // The host's Leave closes the lobby (plan 4.1): back to MULTIPLAYER > HOST's Start / Continue.
        std::fprintf(stderr, "lobby: closed by the host\n");
        g.leaving = Result::Closed;
        g.phase = Phase::FadeOut;
        return;
    }
    Note(SeatName(s) + " left");
    g.seats[s] = Seat{};
    g_remote[s] = Remote{};
}

// One local seat's pad this frame (OwnPad: the host's own seat, or a guest copy's).
void StepSeat(int s, const Pad &pad) {
    Seat &seat = g.seats[s];

    if (seat.pick < 0) {
        seat.drive_x = seat.drive_y = 0.0f; // (its model stands, for the other copies too)
        seat.drive_attack = false;
    }

    if (seat.naming && !seat.leaving) {
        // The profile ask first (plan 4.10); Circle there still asks Leave? (not while typing a name).
        if (pad.Down(PAD_CIRCLE) && !seat.typing) {
            SoundMove();
            seat.leaving = true;
            seat.leave_yes = false;
            return;
        }
        StepNaming(s, pad);
        return;
    }
    if (seat.leaving) {
        // "Leave?  Yes / No", No first; Left/Right choose, Cross answers, Circle is No (plan 4.4).
        if (pad.Down(PAD_LEFT) || pad.Down(PAD_RIGHT)) {
            SoundMove();
            seat.leave_yes = !seat.leave_yes;
        } else if (pad.Down(PAD_CROSS)) {
            seat.leaving = false;
            std::fprintf(stderr, "lobby: seat %d leave? %s\n", s + 1, seat.leave_yes ? "yes" : "no");
            if (seat.leave_yes) {
                SoundPick();
                Leave(s);
            } else {
                SoundMove();
            }
        } else if (pad.Down(PAD_CIRCLE)) {
            SoundMove();
            seat.leaving = false;
            std::fprintf(stderr, "lobby: seat %d leave? no (circle)\n", s + 1);
        }
        return;
    }

    if (seat.pick >= 0) {
        // Locked: Circle unlocks; the stick and Cross run the model (plan 4.9), nothing else.
        if (pad.Down(PAD_CIRCLE)) {
            Unlock(s);
            return;
        }
#if DC_LOBBY_MODELS
        // The stick (this copy's own controller), else the d-pad. Cross swings only once the Cross that locked was let go.
        // (another local player's seat: its own controller's stick)
        const InputPadState own = g.guest ? g.guest_pad : InputGetPad(s != 0 && seat.local ? seat.pad : g.host_pad);
        float               x = std::abs((int) own.left_x - 128) > 24 ? ((int) own.left_x - 128) / 127.0f : 0.0f;
        float               y = std::abs((int) own.left_y - 128) > 24 ? ((int) own.left_y - 128) / 127.0f : 0.0f;

        if (x == 0.0f && y == 0.0f) {
            x = pad.On(PAD_LEFT) ? -1.0f : pad.On(PAD_RIGHT) ? 1.0f : 0.0f;
            y = pad.On(PAD_UP) ? -1.0f : pad.On(PAD_DOWN) ? 1.0f : 0.0f;
        }
        if (!pad.On(PAD_CROSS)) {
            Armed(s) = true;
        }
        seat.drive_x = std::clamp(x, -1.0f, 1.0f);
        seat.drive_y = std::clamp(y, -1.0f, 1.0f);
        seat.drive_attack = Armed(s) && pad.On(PAD_CROSS);
        lobbymodels::Drive(seat.pick, seat.drive_x, seat.drive_y, seat.drive_attack);
#endif
        return;
    }

    if (pad.Down(PAD_LEFT) || pad.Down(PAD_RIGHT)) {
        const int step = pad.Down(PAD_LEFT) ? -1 : 1;
        const int was = seat.hover;

        seat.hover = NextShown(s, seat.hover, step);
        if (seat.hover != was) {
            SoundMove();
            std::fprintf(stderr, "lobby: seat %d on %s\n", s + 1, kMembers[seat.hover]);
            if (g.guest) {
                Send(kRequest);
            }
        }
    } else if (pad.Down(PAD_CROSS)) {
        Lock(s);
    } else if (pad.Down(PAD_SQUARE) && !g.code_open) {
        // The profile again (Jep: after picking one, there was no way back to it).
        SoundPick();
        seat.naming = true;
        seat.typing = false;
        seat.profile = 0;
        g.renaming = g.guest;
        std::fprintf(stderr, "lobby: seat %d changes its profile\n", s + 1);
    } else if (pad.Down(PAD_CIRCLE)) {
        SoundMove();
        seat.leaving = true;
        seat.leave_yes = false;
        std::fprintf(stderr, "lobby: seat %d asked leave?\n", s + 1);
    }
}

// ---- the messages ----

void CopyName(char out[16], const std::string &name) {
    std::memset(out, 0, 16);
    std::memcpy(out, name.data(), std::min<std::size_t>(name.size(), 15));
}

std::string NameOf(const char in[16]) {
    return std::string(in, strnlen(in, 16));
}

Wire Make(Kind kind) {
    Wire wire{};

    wire.magic = kLobbyMagic;
    wire.player = kind == kTable ? 1 : 2;
    wire.version = kLobbyVersion;
    wire.kind = kind;
    wire.seq = ++g.seq;
    wire.identity = Identity();
    return wire;
}

// The host's table, as this guest (seat for) sees it.
Wire TableFor(int seat) {
    Wire wire = Make(kTable);

    wire.your_seat = seat >= 0 ? (std::uint8_t) seat : kNone;
    wire.party = (std::uint8_t) g.party;
    wire.phase = g.playing ? kTablePlaying : g.phase == Phase::FadeOut ? (g.leaving == Result::Start ? kTableStarted : kTableClosed) : kTableOpen;
    if (seat >= 0) {
        wire.refused = g_remote[seat].refused >= 0 ? (std::uint8_t) g_remote[seat].refused : kNone;
        wire.refuse_seq = (std::uint16_t) g_remote[seat].refuse_seq;
        wire.echo = g_remote[seat].echo;
    }
    for (int s = 0; s < kSeats; s++) {
        const Seat &from = g.seats[s];
        WireSeat   &to = wire.seats[s];

        to.present = from.present ? 1 : 0;
        to.pick = from.pick >= 0 ? (std::uint8_t) from.pick : kNone;
        to.hover = from.hover >= 0 ? (std::uint8_t) from.hover : kNone;
        to.leaving = from.leaving ? 1 : 0;
        to.identity = from.identity;
        CopyName(to.name, SeatName(s));
        to.naming = from.naming ? 1 : 0;
        to.active = from.active ? 1 : 0;
        if (!Mutated("live_local")) {
            const bool here = PlayedHere(s);

            to.drive_x = StickByte(from.drive_x);
            to.drive_y = StickByte(from.drive_y);
            to.attack = from.drive_attack ? 1 : 0;
            to.typing = here ? (from.typing ? 1 : 0) : (from.shown_typing ? 1 : 0);
            CopyName(to.shown, here ? CardText(s) : from.shown);
        }
    }
    if (seat >= 0) {
        const std::vector<std::string> names = FreeProfiles(seat);

        for (std::size_t i = 0; i < names.size() && i < 8; i++) {
            CopyName(wire.profiles[i], names[i]);
        }
        wire.name_refused = g_remote[seat].name_refused ? 1 : 0;
    }
    return wire;
}

void Send(Kind kind) {
    Wire wire = Make(kind);

    if (kind == kHello) {
        // A multi-screen copy started for a seat (DC_LOBBY_SEAT) asks for that seat.
        static const int seat = TestEnvInt("DC_LOBBY_SEAT", -1);

        wire.your_seat = seat >= 0 && seat < kSeats ? (std::uint8_t) seat : kNone;
    }
    if (kind != kTable) {
        const Seat &mine = g.seats[g.me];

        wire.hover = mine.hover >= 0 ? (std::uint8_t) mine.hover : kNone;
        wire.want = g.want >= 0 ? (std::uint8_t) g.want : kNone;
        wire.leaving = mine.leaving ? 1 : 0;
        wire.named = g.asked_name.empty() ? 0 : 1;
        wire.active = mine.active ? 1 : 0;
        CopyName(wire.name, g.asked_name);
        if (!Mutated("live_local")) {
            wire.drive_x = StickByte(mine.drive_x);
            wire.drive_y = StickByte(mine.drive_y);
            wire.attack = mine.drive_attack ? 1 : 0;
            wire.typing = mine.typing ? 1 : 0;
            CopyName(wire.shown, CardText(g.me));
        }
        g.sent_at[wire.seq & 15] = std::chrono::steady_clock::now();
    }
    NetSocketSend(&wire, sizeof wire);
}

// The host's table to the guest at the socket's peer (the link is two copies: one guest, whose address the socket kept).
// Seats past kLinkPlayers need the N-player link's per-guest addresses.
void SendTable(int seat) {
    const Wire wire = TableFor(seat);

    NetSocketSend(&wire, sizeof wire);
}

int SeatOf(std::uint64_t identity) {
    for (int s = 0; s < kSeats; s++) {
        if (g.seats[s].present && !g.seats[s].local && g.seats[s].identity == identity) {
            return s;
        }
    }
    return -1;
}

// The host: a guest's message.
void HostReceive(const Wire &wire) {
    int s = SeatOf(wire.identity);

    NetSocketKeepSender();
    NetSocketAnswerHello(); // (the reply goes to whoever spoke)

    if (wire.kind == kBye) {
        if (s >= 0) {
            std::fprintf(stderr, "lobby: seat %d said bye\n", s + 1);
            Leave(s);
        }
        return;
    }
    // A multi-screen copy started for a seat asks for it (its Hello carries the seat).
    if (s < 0 && g.playing && wire.your_seat < kSeats && g.seats[wire.your_seat].present && g.seats[wire.your_seat].awaiting_copy) {
        // coop-joinlobby: the handed window of a seat locked in the lobby, joining the host's game: its seat as it was locked
        // (its card is done: the table says so and its copy goes on at once).
        s = wire.your_seat;
        g.seats[s].identity = wire.identity;
        g.seats[s].awaiting_copy = false;
        g.seats[s].local = false;
        g_remote[s] = Remote{};
        std::fprintf(stderr, "lobby: the host's game: seat %d's own window joins as locked (%s, %s)\n", s + 1, SeatName(s).c_str(),
                     g.seats[s].pick >= 0 ? kMembers[g.seats[s].pick] : "nobody");
    }
    // coop-joinlobby round 3: the lobby's handed window (its Multi seat on this PC, handed its controller at the start) says
    // Hello with that seat's identity and no seat number: its seat as it was locked, no card (its card was done in the
    // lobby). Before, local seats were never matched here, so the host's game gave it a new card that only her title
    // presses locked. DC_LOBBY_MUTATE=handed_card (l-lobby-handed~card): not matched, a card as before.
    if (s < 0 && g.playing && wire.kind == kHello && !Mutated("handed_card")) {
        for (int h = 1; h < kSeats; h++) {
            Seat &handed = g.seats[h];

            if (handed.present && handed.local && handed.screen_multi && handed.pad >= 0 && handed.identity == wire.identity) {
                s = h;
                handed.local = false;
                g_remote[s] = Remote{};
                // Test hook (DC_LOBBY_TEST_HANDED_CARD=1, l-lobby-handed-card): its member taken as if player 1 had swapped to
                // it before the window's join (PlayingReceive's "player 1's now"): its card opens, its profile kept, and the
                // window's presses go to the card only (its title stays locked until its link).
                if (TestEnvIs("DC_LOBBY_TEST_HANDED_CARD", "1") && handed.pick >= 0) {
                    std::fprintf(stderr, "lobby: test: seat %d's member %s taken as if by player 1: its card\n", s + 1, kMembers[handed.pick]);
                    handed.pick = -1;
                }
                std::fprintf(stderr, "lobby: the host's game: seat %d's handed window joins as locked (%s, %s)\n", s + 1, SeatName(s).c_str(),
                             handed.pick >= 0 ? kMembers[handed.pick] : "its card: its member is taken");
                break;
            }
        }
    }
    if (s < 0 && wire.kind == kHello && wire.your_seat < kSeats && g.seats[wire.your_seat].awaiting_copy) {
        s = wire.your_seat;
        const int pad = g.seats[s].pad;

        g_remote[s] = Remote{};
        TakeSeat(s, false, wire.identity);
        std::fprintf(stderr, "lobby: seat %d's own window joined (controller %d)\n", s + 1, pad + 1);
    }
    if (s < 0) {
        if (wire.kind != kHello && wire.kind != kRequest) {
            return;
        }
        // A new guest: the first open seat within the link's capacity, else "full".
        for (int free = 0; free < std::min(kSeats, kLinkPlayers); free++) {
            if (!g.seats[free].present) {
                s = free;
                break;
            }
        }
        // DC_LOBBY_TEST_AUTOSTART=1: the host starts alone, so a joiner comes in after the start as a late joiner.
        if (TestEnvIs("DC_LOBBY_TEST_AUTOSTART", "1")) {
            s = -1;
        }
        if (g.playing) {
            s = 1; // (coop-joinlobby: the host's game takes one joiner, in seat 2: PlayingReceive cleared it)
        }
        if (s < 0 || (g.phase != Phase::Run && !g.playing)) {
            std::fprintf(stderr, "lobby: a guest asked for a seat: %s\n", s < 0 ? "full" : "not open");
            SendTable(-1);
            return;
        }
        g_remote[s] = Remote{};
        TakeSeat(s, false, wire.identity);
        Note(SeatName(s) + " joined");
        if (!g.playing) {
            SoundPick();
        } else {
            std::fprintf(stderr, "lobby: the host's game: a joiner's card for seat %d (%s)\n", s + 1, g.seats[s].naming ? "asking its profile" : SeatName(s).c_str());
        }
    }
    Seat   &seat = g.seats[s];
    Remote &remote = g_remote[s];

    remote.heard = std::chrono::steady_clock::now();
    remote.echo = wire.seq;
    if (wire.kind == kRequest) {
        TakeLive(s, wire.drive_x, wire.drive_y, wire.attack, wire.typing, wire.shown);
    }
    if (wire.kind == kRequest && wire.named && (seat.naming || NameOf(wire.name) != seat.name)) {
        const std::string asked = NameOf(wire.name);

        remote.name_refused = !Name(s, asked);
        if (!remote.name_refused) {
            Note(asked + " joined");
            const Profile *profile = ProfileNamed(asked);

            if (profile != nullptr && profile->last >= 0 && Free(profile->last, s)) {
                seat.hover = profile->last;
            }
        }
    }
    if (wire.kind == kRequest && !seat.naming) {
        // (any member: its slot may show one taken or not yet in the story; its lock asks Free)
        seat.hover = wire.hover < kSeats ? wire.hover : seat.hover;
        seat.leaving = wire.leaving != 0;
        if (wire.active != 0 && !seat.active) {
            seat.active = true;
            std::fprintf(stderr, "lobby: seat %d pressed Start: lit\n", s + 1);
        }

        const int want = wire.want != kNone ? wire.want : -1;

        if (want != seat.pick) {
            if (want < 0) {
                std::fprintf(stderr, "lobby: seat %d unlocked %s\n", s + 1, kMembers[seat.pick]);
#if DC_LOBBY_MODELS
                if (!g.playing) {
                    lobbymodels::SetLocked(seat.pick, false);
                }
#endif
                seat.pick = -1;
                remote.refused = -1;
            } else if (Free(want, s) || (g.playing && Mutated("join_refuse_off"))) { // (the twin: a member in use taken)
                seat.pick = want;
                remote.refused = -1;
                std::fprintf(stderr, "lobby: seat %d locked %s\n", s + 1, kMembers[want]);
#if DC_LOBBY_MODELS
                if (!g.playing) {
                    lobbymodels::SetLocked(want, true);
                }
#endif
            } else if (remote.refused != want) {
                remote.refused = want;
                remote.refuse_seq++;
                const int by = LockedBy(want);
                std::fprintf(stderr, "lobby: seat %d refused %s (%s)\n", s + 1, IsUsable(want) ? kMembers[want] : "?",
                             by >= 0 ? ("taken by seat " + std::to_string(by + 1)).c_str() : "joins later");
            }
        }
    }
    SendTable(s);
}

// coop-joinlobby (Jep, 2026-11-07): a first join into the host's game in progress gets its card on its own copy, its
// profile preselected (by its player id), and locks profile and member before it loads on. The host's game answers here,
// from the link's receive on its own frame, with nothing on its screen or in its play: the table (kTablePlaying) with
// its free profiles and members. Seat 1 is the host (its member the game's player now), seat 2 the joiner: a seat 2 of
// another identity silent past kSilence is given up to it; one of the same identity (it was in this game) keeps its
// profile and member and needs no card, unless that member is the host's now (then its card, its profile preselected).
void PlayingReceive(const Wire &wire) {
    const auto now = std::chrono::steady_clock::now();

    if (!g.playing) {
        g.playing = true;
        std::fprintf(stderr, "lobby: the host's game answers a joiner's lobby (its own frame, nothing on its screen)\n");
    }
    Seat &host = g.seats[0];

    if (!host.present) {
        host = Seat{};
        host.present = true;
        host.local = true;
        host.active = true;
        host.identity = Identity();
        ReadProfiles();
        if (const Profile *profile = ProfileOf(Identity())) {
            host.name = profile->name;
        }
    }
    host.naming = false;
    if (UserStatus != nullptr) {
        host.pick = UserStatus->cur_chara;
    }
    if (UserStatus != nullptr) {
        g.party = std::clamp<int>(UserStatus->party_size, 1, kSeats);
    }
    Seat &seat = g.seats[1];

    // coop-joinlobby step 3b (Jep, 2026-11-07: a non-host player who was in this game rejoins by its player id, no card): a
    // seat 2 that played, about to go to another identity's join (here, or HostReceive's new guest below, which takes seat 2
    // whoever has it), is kept by its identity for the host's session (loads within it too; not in the slot).
    // DC_LOBBY_MUTATE=rejoin_forgets (l-lobby-join-rejoin's twin): not kept, so its rejoin gets a card.
    const bool other_joins = wire.kind == kHello && seat.present && seat.identity != wire.identity && !seat.awaiting_copy;

    if (other_joins && seat.identity != 0 && seat.pick >= 0 && !seat.naming && !Mutated("rejoin_forgets") &&
        g_rejoin.find(seat.identity) == g_rejoin.end()) {
        g_rejoin[seat.identity] = Rejoin{seat.pick, seat.name};
        std::fprintf(stderr, "lobby: the host's game keeps %s's seat for a rejoin (%s)\n", SeatName(1).c_str(), kMembers[seat.pick]);
    }
    if (seat.present && seat.identity != wire.identity && !seat.awaiting_copy && now - g_remote[1].heard > kSilence) {
        std::fprintf(stderr, "lobby: the host's game: seat 2 (%s, silent) given up to a new joiner\n", SeatName(1).c_str());
        seat = Seat{};
        g_remote[1] = Remote{};
    }
    // A player who was in this game and lost seat 2 to another joiner: its seat back as it was, no card (as HostReceive's
    // new guest would, it takes seat 2 from whoever has it now).
    if (const auto back = g_rejoin.find(wire.identity); wire.kind == kHello && back != g_rejoin.end() && !(seat.present && seat.identity == wire.identity)) {
        if (seat.present) {
            std::fprintf(stderr, "lobby: the host's game: seat 2 (%s) given up to its player before\n", SeatName(1).c_str());
        }
        TakeSeat(1, false, wire.identity);
        seat.naming = false;
        seat.name = back->second.name;
        seat.pick = back->second.pick;
        seat.hover = back->second.pick;
        seat.active = true;
        g_remote[1] = Remote{};
        g_remote[1].heard = now;
        std::fprintf(stderr, "lobby: the host's game: %s rejoins seat 2 as %s (kept by its player id): no card\n", SeatName(1).c_str(), kMembers[seat.pick]);
        g_rejoin.erase(back);
    }
    if (seat.present && seat.identity == wire.identity && seat.pick >= 0 && seat.pick == host.pick) {
        std::fprintf(stderr, "lobby: the host's game: seat 2's member %s is player 1's now: its card\n", kMembers[seat.pick]);
        seat.pick = -1;
    }
    HostReceive(wire);
    // Test hook (DC_LOBBY_TEST_AUTOSTART=1, the rows that host through the title and test what comes after the lobby): the
    // joiner's card is locked for it as it opens: no profile (as before the card), the first free member. Not with
    // DC_LOBBY_TEST_JOIN_CARD=1 (l-join rows: the joiner's own presses lock it).
    static const bool autolock = TestEnvIs("DC_LOBBY_TEST_AUTOSTART", "1") && !TestEnvIs("DC_LOBBY_TEST_JOIN_CARD", "1");

    if (autolock && seat.present && seat.identity == wire.identity && (seat.naming || seat.pick < 0)) {
        seat.naming = false;
        for (int m = 0; m < Usable() && seat.pick < 0; m++) {
            seat.pick = Free(m, 1) ? m : -1;
        }
        std::fprintf(stderr, "lobby: test autostart: the joiner's card locked for it (seat 2 %s, %s)\n", SeatName(1).c_str(),
                     seat.pick >= 0 ? kMembers[seat.pick] : "nobody free");
        SendTable(1);
    }
}

// A guest: the host's table.
void GuestReceive(const Wire &wire) {
    if (wire.kind != kTable) {
        return;
    }
    const auto now = std::chrono::steady_clock::now();

    g_last_table = wire;
    g_have_last_table = true;
    if (!g.guest || !Active()) {
        if (wire.phase == kTableStarted) {
            g_join_go = true; // (a lobby already starting: the join goes on as before)
        }
        // coop-reds1109 (train 55's mp-natwait red: the joiner's card opened twice): once the host's game took this copy's seat
        // and its join went on (its card closed, its Hellos out), no later table opens a card. A table sent before the seat
        // was locked can come after it (the test's autostart locks the card on the host, the card's own first message unlocks
        // it there for a moment): it opened a second card over the link. DC_LOBBY_MUTATE=card_reopens (l-lobby-join-card~stale):
        // it does, as before.
        if (g_join_go && g_join_carded && !Mutated("card_reopens")) {
            return;
        }
        // coop-joinlobby: the host's game took this copy's seat as it is (its handed window, locked in the lobby): no card.
        if (wire.phase == kTablePlaying && wire.your_seat < kSeats && wire.seats[wire.your_seat].pick != kNone && wire.seats[wire.your_seat].naming == 0 &&
            wire.seats[wire.your_seat].identity == Identity()) {
            if (!g_join_go) {
                g_join_go = true;
                std::fprintf(stderr, "lobby: the host's game takes seat %d as %s (%s), no card: the join goes on\n", wire.your_seat + 1,
                             wire.seats[wire.your_seat].pick < kSeats ? kMembers[wire.seats[wire.your_seat].pick] : "?", NameOf(wire.seats[wire.your_seat].name).c_str());
            }
            return;
        }
        if (wire.phase != kTableOpen && wire.phase != kTablePlaying) {
            return; // (a lobby already starting or closed: the join goes on as today)
        }
        // The first table opens this copy's lobby over its title (its join was picked: NetplayLobbyJoining).
        if (wire.your_seat == kNone || wire.your_seat >= kSeats) {
            static bool said = false;

            if (!said) {
                said = true;
                std::fprintf(stderr, "lobby: the host's lobby has no seat for this copy\n");
            }
            return;
        }
        // coop-titleopen51 (Jep, 2026-11-04; train 51: a guest joined while its boot movie still ran, its lobby opened
        // there, and the title's load after the movie crashed: at InitProcI on a frame the lobby's reads had overwritten, or
        // in LoadTextures on a read the title's load had emptied): the lobby opens on the title only; the host's table comes
        // again every quarter second. DC_LOBBY_MUTATE=open_before_title (l-lobby-guest-pad~boot): opened at once, as before;
        // it says so and first gives the test's stop file 3 s (the twin stops on that line, before the crash it leads to).
        if (mode != GAME_MODE_TITLE && Mutated("open_before_title")) {
            std::fprintf(stderr, "lobby: the host's table opens this copy's lobby off its title (mode %d)\n", mode);
            const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(3);

            while (std::chrono::steady_clock::now() < until) {
                if (NetplayQaStopSeen()) {
                    return;
                }
                SDL_Delay(50);
            }
        }
        if (mode != GAME_MODE_TITLE && !Mutated("open_before_title")) {
            static bool waited = false;

            if (!waited) {
                waited = true;
                std::fprintf(stderr, "lobby: the host's table waits for this copy's title (mode %d)\n", mode);
            }
            return;
        }
        g = State{};
        g.guest = true;
        g.me = wire.your_seat;
        g.phase = Phase::FadeIn;
        g.opened = now;
        CFade.value = 0;
        u_long128 *data = MenuCalcBufAlignment((u_long128 *) read_buffer);
        StartReadBG();
        g.tex_loading = LoadFileBGMenuData(const_cast<char *>("option.pac"), data) > 0;
        g.party = std::clamp<int>(wire.party, 1, kSeats);
        g.guest_last = NetplayLobbyOwnPad().buttons; // (the press that picked Join is not a Start)
        std::fprintf(stderr, "lobby: joined the host's %s as seat %d%s\n", wire.phase == kTablePlaying ? "game (its card)" : "lobby", g.me + 1,
                     mode == GAME_MODE_TITLE ? "" : " (not on its title)");
        // A window this PC's host started for a controller that pressed Start already: lit at once (Jep's playtest).
        if (TestEnvInt("DC_LOBBY_SEAT", -1) >= 0) {
            g.seats[g.me].active = true;
            std::fprintf(stderr, "lobby: seat %d pressed Start: lit (its window was started by that Start)\n", g.me + 1);
        }
        OpenModels();
    }
    g.heard = now;
    if (wire.echo != 0 && wire.echo != g.echo_seen) {
        g.echo_seen = wire.echo;
        g.rtt_ms = (int) std::chrono::duration_cast<std::chrono::milliseconds>(now - g.sent_at[wire.echo & 15]).count();
    }
    g.party = std::clamp<int>(wire.party, 1, kSeats);

    // The table, but this copy's own hand and question stay its own (they answer at once on this screen).
    const Seat mine = g.seats[g.me];

    for (int s = 0; s < kSeats; s++) {
        const WireSeat &from = wire.seats[s];
        Seat           &to = g.seats[s];
        const int       was_pick = to.pick;

        to.present = from.present != 0;
        to.local = s == g.me;
        to.identity = from.identity;
        to.name = NameOf(from.name);
        to.pick = from.pick != kNone ? from.pick : -1;
        to.hover = from.hover != kNone ? from.hover : -1;
        to.leaving = from.leaving != 0;
        to.naming = from.naming != 0;
        to.active = from.active != 0;
#if DC_LOBBY_MODELS
        if (was_pick != to.pick) {
            if (was_pick >= 0) {
                lobbymodels::SetLocked(was_pick, false);
            }
            if (to.pick >= 0) {
                lobbymodels::SetLocked(to.pick, true);
            }
        }
#else
        (void) was_pick;
#endif
    }
    for (int s = 0; s < kSeats; s++) {
        if (s != g.me && g.seats[s].present) {
            TakeLive(s, wire.seats[s].drive_x, wire.seats[s].drive_y, wire.seats[s].attack, wire.seats[s].typing, wire.seats[s].shown);
        }
    }
    Seat &me = g.seats[g.me];

    me.hover = mine.hover >= 0 && !mine.naming ? mine.hover : me.hover;
    me.leaving = mine.leaving;
    me.leave_yes = mine.leave_yes;
    me.active = mine.active || me.active; // (its own Start answers at once on this screen)
    me.typing = mine.typing;
    me.entry = mine.entry;
    me.letter = mine.letter;
    me.profile = mine.profile;
    g.guest_profiles.clear();
    for (const auto &name : wire.profiles) {
        if (name[0] != '\0') {
            g.guest_profiles.push_back(NameOf(name));
        }
    }
    if (me.naming && !g.asked_name.empty() && wire.name_refused) {
        SoundRefuse();
        Note(g.asked_name + " is in use");
        std::fprintf(stderr, "lobby: profile %s refused by the host (in use)\n", g.asked_name.c_str());
        g.asked_name.clear();
    }
    if (!me.naming && mine.naming) {
        std::fprintf(stderr, "lobby: seat %d is %s (the host's table)\n", g.me + 1, me.name.c_str());
    }
    if (me.pick >= 0 && me.pick != mine.pick) {
        g.attack_armed = false; // (the Cross that asked is not also a swing)
        std::fprintf(stderr, "lobby: seat %d locked %s (the host's table)\n", g.me + 1, kMembers[me.pick]);
    }
    // Picking its profile again (Square): its card keeps asking until the host's table carries the new name.
    if (g.renaming) {
        if (!g.asked_name.empty() && me.name == g.asked_name) {
            g.renaming = false;
            g.asked_name.clear();
        } else {
            me.naming = true;
        }
    }
    // Refused: the face was taken first; the hand stays on it, darkened (Jep, 2026-11-03).
    if (wire.refuse_seq != g.refuse_seen) {
        g.refuse_seen = wire.refuse_seq;
        if (wire.refused != kNone && g.want == wire.refused) {
            SoundRefuse();
            std::fprintf(stderr, "lobby: seat %d refused %s (taken)\n", g.me + 1, kMembers[wire.refused]);
            Note(std::string(kMembers[wire.refused]) + " is taken");
            g.want = -1;
            me.hover = Mutated("refuse_moves") ? NextFree(g.me, wire.refused, 1) : wire.refused; // (the hand stays on it, refused)
        }
    }
    // coop-joinlobby: the host's game accepted this card (its profile named, its member locked): the card closes and the
    // join goes on (its link Hellos, JoinHoldsHello). The host's player saw nothing of it.
    if (wire.phase == kTablePlaying && !g.starting && me.pick >= 0 && !me.naming && !g.renaming) {
        g.starting = true;
        g_join_go = true;
        g_join_carded = true;
        std::fprintf(stderr, "lobby: the host's game took seat %d as %s (%s): the card closes, the join goes on\n", g.me + 1, kMembers[me.pick], me.name.c_str());
        g.leaving = Result::Closed;
        g.phase = Phase::FadeOut;
        return;
    }
    if (wire.phase == kTableStarted && !g.starting) {
        g.starting = true;
        g_join_go = true;
        std::fprintf(stderr, "lobby: the host started (seat %d plays %s)\n", g.me + 1, me.pick >= 0 ? kMembers[me.pick] : "?");
        Note("The host is starting the game...");
        g.leaving = Result::Closed;
        g.phase = Phase::FadeOut;
    } else if (wire.phase == kTableClosed && g.phase != Phase::FadeOut) {
        std::fprintf(stderr, "lobby: the host closed the lobby\n");
        NetplayLobbyLeaveJoin("The host closed the lobby");
        g.leaving = Result::Closed;
        g.phase = Phase::FadeOut;
    }
}

// Each frame from the link's tick (NetTick): the host's table and timeouts; a guest's Hello or Request, and the host's
// silence.
void HostNet() {
    const auto now = std::chrono::steady_clock::now();
    bool       due = now >= g.send_due || g.phase == Phase::FadeOut;

    // (a card or a model of a seat played here changed: sent now, at most each 33 ms)
    if (const std::uint64_t live = LiveSign(); live != g.live_sent && now - g.live_at >= std::chrono::milliseconds(33) && !Mutated("live_local")) {
        g.live_sent = live;
        g.live_at = now;
        due = true;
    }
    if (due) {
        g.send_due = now + kSendEvery;
    }
    for (int s = 0; s < kSeats; s++) {
        if (!g.seats[s].present || (g.seats[s].local && !g.seats[s].awaiting_copy)) {
            continue;
        }
        if (now - g_remote[s].heard > (g.seats[s].awaiting_copy ? 3 * kSilence : kSilence) && g.phase == Phase::Run) {
            std::fprintf(stderr, "lobby: seat %d lost connection (%lld s silent)\n", s + 1, (long long) kSilence.count());
            Note(SeatName(s) + " lost connection");
            g.seats[s] = Seat{};
            g_remote[s] = Remote{};
            continue;
        }
        if (due && !g.seats[s].awaiting_copy) {
            SendTable(s);
        }
    }
}

void GuestNet() {
    const auto now = std::chrono::steady_clock::now();

    // DC_LOBBY_TEST_NAME=name[,name...] (a test's guest): its card's profile ask answered with that name; after a refusal the
    // next one (l-lobby-join-card-taken: the host's own profile first), the last again when none is left.
    if (static const char *test_name = TestEnv("DC_LOBBY_TEST_NAME"); test_name != nullptr && g.seats[g.me].naming && g.asked_name.empty()) {
        static std::size_t asked = 0;
        const std::string  all = test_name;
        std::size_t        at = 0;

        for (std::size_t i = 0; i < asked && all.find(',', at) != std::string::npos; i++) {
            at = all.find(',', at) + 1;
        }
        const std::size_t end = all.find(',', at);

        g.asked_name = all.substr(at, end == std::string::npos ? std::string::npos : end - at);
        asked++;
        std::fprintf(stderr, "lobby: seat %d asks for profile %s (test)\n", g.me + 1, g.asked_name.c_str());
        g.send_due = now;
    }
    if (const std::uint64_t live = LiveSign(); live != g.live_sent && now - g.live_at >= std::chrono::milliseconds(33) && !Mutated("live_local")) {
        g.live_sent = live;
        g.live_at = now;
        g.send_due = now;
    }
    if (now >= g.send_due) {
        g.send_due = now + kSendEvery;
        Send(kRequest);
    }
    if (g.phase == Phase::Run && now - g.heard > kSilence) {
        std::fprintf(stderr, "lobby: the host's connection was lost (%lld s silent)\n", (long long) kSilence.count());
        NetplayLobbyLeaveJoin("The host's connection was lost");
        g.leaving = Result::Closed;
        g.phase = Phase::FadeOut;
    }
}

// The host's Select (plan 4.3): Steam listens for friends' Join Game, or stops; never on a guest's copy.
void SteamSelect() {
    if (g.guest || g.local_only) {
        return;
    }
    // (asked, not yet open: Select again still turns it off; NetSteamListening is true only once Steam's lobby is open)
    const bool on = !g.steam_asked && !g.steam_waiting;

    SoundPick();
    std::fprintf(stderr, "lobby: select: Steam %s\n", on ? "listen" : "off");
    g.steam_asked = NetplayLobbySteam(on) && on;
    g.steam_waiting = on && !g.steam_asked && NetSteamListenWaits();
    if (!g.steam_asked && on) {
        // (Steam's own words start "Steam: "; the note shows one short line, the log the whole of it)
        std::string why = NetSteamStatus();

        if (why.rfind("Steam: ", 0) == 0) {
            why = why.substr(7);
        }
        Note(why.find("helper") != std::string::npos ? "Steam helper missing" : "Steam is not available");
    }
}

// coop-consent (Jep, 2026-11-02): R1 shows or hides the host's outside IP in the header, kept for the next lobby
// (game.lobby_show_ip). DC_CONSENT_MUTATE=ip_forget (a twin): this lobby only, as if never kept.
void ToggleIp() {
    SoundPick();
    g.ip_shown = !g.ip_shown;
    std::fprintf(stderr, "lobby: R1: outside IP %s\n", g.ip_shown ? "shown" : "hidden");
    if (!TestEnvIs("DC_CONSENT_MUTATE", "ip_forget")) {
        dc::platform::Config config = dc::platform::ConfigGet();

        config.lobby_show_ip = g.ip_shown;
        ConfigChange(config);
    }
}

void TryStart() {
    if (!EveryoneLocked()) {
        SoundRefuse();
        for (int s = 0; s < kSeats; s++) {
            if (g.seats[s].present && g.seats[s].pick < 0) {
                std::fprintf(stderr, "lobby: start refused (seat %d not locked)\n", s + 1);
                Note("Waiting for " + SeatName(s) + " to lock in");
                break;
            }
        }
        return;
    }
    SoundPick();
    std::string picks;
    for (int s = 0; s < kSeats; s++) {
        // DC_LOBBY_MUTATE=start_unlocked lets an unlocked seat start: it plays the face it shows (its hover), never
        // member -1 (kMembers[-1] crashed l-lobby-host-alone's twin instead of letting it fail on its check).
        if (g.seats[s].present && g.seats[s].pick < 0) {
            g.seats[s].pick = g.seats[s].hover >= 0 ? g.seats[s].hover : 0;
            std::fprintf(stderr, "lobby: seat %d started unlocked: plays %s (mutation start_unlocked)\n", s + 1, kMembers[g.seats[s].pick]);
        }
        if (g.seats[s].present) {
            picks += " seat" + std::to_string(s + 1) + "=" + kMembers[g.seats[s].pick];
        }
    }
    std::fprintf(stderr, "lobby: start (%d players, picks%s)\n", Present(), picks.c_str());
    g.leaving = Result::Start;
    g.phase = Phase::FadeOut;
}

// The game's own menu parts, read one pack at a time in the background: option.pac's picture (the dark message window)
// into kBlock, as the Options screen loads it; then the Items menu's pack (dungeon/dunmenu4.pak's btlmenu.img: its stone
// boards, "status") into kBoardBlock, as the in-game menu loads it (Jep, 2026-10-31: the lobby looked unpolished on
// Chronicle's generic plates).
void LoadTextures() {
    if (!g.tex_loading) {
        return;
    }
    ReadBG();
    if (ReadBGSync() != 0) {
        return;
    }
    const bool        boards = g.tex_ready; // (the second pack)
    const int         block = boards ? kBoardBlock : kBlock;
    LOADTEXTURE_INFO2 textures[2] = {
        {nullptr, block, 0},
        {nullptr, block, 0},
    };
    BG_READ_INFO *file = GetReadBGFile(0);
    // coop-lobbytex51 (train 51: l-lobby-guest-pad's guest crashed here, file null): the background reader is shared, and
    // anything that starts its own read (StartReadBG) between the read and this take empties the slot; the guest's lobby
    // opens over its title, whose own map change does (and drops textures loaded before it, so the take stays where it
    // was, a frame after the read). The pack is read again and taken at once (twice at most), else the lobby goes on with
    // Chronicle's plates. (coop-titleopen51 found the cause, the guest's lobby opened in its boot movie; this stays as a guard.)
    if (file == nullptr || file->buffer == nullptr) {
        const char *pack = boards ? "dungeon/dunmenu4.pak" : "option.pac";

        if (++g.tex_rereads > 2) {
            std::fprintf(stderr, "lobby: the background read of %s was taken by another reader, not read again\n", pack);
            g.tex_loading = false;
            return;
        }
        std::fprintf(stderr, "lobby: the background read of %s was taken by another reader, read again (frame %d)\n", pack, g.frames);
        StartReadBG();
        g.tex_loading = LoadFileBGMenuData(const_cast<char *>(pack), MenuCalcBufAlignment((u_long128 *) read_buffer)) > 0;
        if (g.tex_loading) {
            LoadTextures();
        }
        return;
    }
    textures[0].name = (char *) GetPackFile((u_int *) file->buffer, const_cast<char *>(boards ? "btlmenu.img" : "option.img"), nullptr);
    TexManager.DeleteTextureBlock(block);
    TexManager.CleanUpTextureList();
    if (textures[0].name != nullptr) {
        TexManager.LoadTextureBlockEX(-1, textures);
    }
    if (!boards) {
        g.tex_ready = true;
        std::fprintf(stderr, "lobby: window parts loaded (frame %d)\n", g.frames);
        u_long128 *data = MenuCalcBufAlignment((u_long128 *) read_buffer);
        StartReadBG();
        g.tex_loading = LoadFileBGMenuData(const_cast<char *>("dungeon/dunmenu4.pak"), data) > 0;
        if (!g.tex_loading) {
            std::fprintf(stderr, "lobby: dunmenu4.pak not read: Chronicle's plates instead of the game's boards\n");
        }
        return;
    }
    g.tex_loading = false;
    g.board_tex = TexManager.GetTexture(const_cast<char *>("status"), kBoardBlock);
    std::fprintf(stderr, "lobby: the Items menu's boards %s (frame %d)\n", g.board_tex != nullptr ? "loaded" : "not found", g.frames);
}

// ---- the IP way's checks (plan 4.3) ----

// a.b.c.d's four numbers, false for anything else.
bool Quad(const char *text, int out[4]) {
    return text != nullptr && std::sscanf(text, "%d.%d.%d.%d", &out[0], &out[1], &out[2], &out[3]) == 4;
}

// Inside a home or carrier network: never an outside sender's address.
bool PrivateAddress(const char *text) {
    int q[4];

    if (!Quad(text, q)) {
        return true;
    }
    return q[0] == 10 || q[0] == 127 || (q[0] == 192 && q[1] == 168) || (q[0] == 172 && q[1] >= 16 && q[1] < 32) || (q[0] == 169 && q[1] == 254) ||
           (q[0] == 100 && q[1] >= 64 && q[1] < 128);
}

bool CarrierAddress(const char *text) {
    int q[4];

    return Quad(text, q) && q[0] == 100 && q[1] >= 64 && q[1] < 128;
}

double Seconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - g.opened).count();
}

// Port forward: open / closed / unknown, claimed only as far as what was seen bears it out (the nat-punch audit: a
// changed STUN port alone is not "closed", Consistent NAT changes it too).
Check PortCheck() {
    const int   port = NetplayLobbyPort();
    int         wan_port = 0;
    const char *wan = NetSocketWan(wan_port);
    int         upnp_port = 0;
    const char *upnp_outside = NetUpnpOutside(upnp_port);
    const int   upnp = NetUpnpState();
    long long   sent = 0, heard = 0;
    const char *last_from = "";
    char        buf[200];

    NetSocketUdpCounts(sent, heard, last_from);

    // coop-pt1108s (Jep's 11-08 playtest: proven by any datagram): only a datagram from an address this socket had not sent to
    // proves the way in (a reply to our own punch or Hello comes through the hole we opened). DC_LOBBY_TEST_PUBLIC_OK=1: the
    // loopback addresses of one PC count as outside (the row's two copies).
    long long   fresh = 0;
    const char *fresh_from = "";

    NetSocketUdpFresh(fresh, fresh_from);
    if (fresh > 0 && fresh_from != nullptr && fresh_from[0] != '\0' && (!PrivateAddress(fresh_from) || TestEnvIs("DC_LOBBY_TEST_PUBLIC_OK", "1"))) {
        return {"open", "Your port is open. Friends can join by your address", "open (proven)", std::string("a datagram from ") + fresh_from};
    }
    if (CarrierAddress(wan) || (upnp_outside[0] != '\0' && !NetUpnpPublic())) {
        return {"closed", "Your router is behind another one (carrier NAT).\nJoining by address will not work here; use CODE or STEAM", "closed (carrier NAT)",
                CarrierAddress(wan) ? std::string("STUN address ") + wan : std::string("UPnP outside address ") + upnp_outside};
    }
    if ((upnp == kUpnpMapped || upnp == kUpnpForwarded) && NetUpnpPublic()) {
        return {"open", "Your port looks open.\nIt is proven once a friend's first packet arrives", "open (likely)", upnp == kUpnpMapped ? "UPnP mapped" : "UPnP: forwarded by hand"};
    }
    if (wan[0] != '\0' && wan_port == port && !PrivateAddress(wan)) {
        return {"open", "Your port looks open.\nIt is proven once a friend's first packet arrives", "open (likely)", "STUN kept the port"};
    }
    if (wan[0] != '\0' && wan_port != port && upnp == kUpnpFailed) {
        std::snprintf(buf, sizeof buf, "Your port is closed. Forward UDP %d to this PC,\nor enable UPnP on your router to make this work", port);
        return {"closed", buf, "closed", "STUN changed the port and UPnP failed"};
    }
    if (wan[0] != '\0' && wan_port != port) {
        return {"unknown", "Your router changed the port; it may still work.\nIt is proven once a friend's first packet arrives", "unknown (port changed)",
                "STUN changed the port, UPnP " + std::string(upnp == kUpnpTrying ? "asking" : "off")};
    }
    if (Seconds() > 10.0) {
        return {"unknown", "Could not check your port:\nno answer from the STUN server", "unknown (no STUN answer)", "10 s without STUN"};
    }
    return {"checking", "Still checking your port...", "unknown (checking)", "STUN asked"};
}

Check UpnpCheck() {
    if (!NetplayLobbyUpnpWanted()) {
        return {"disabled", "UPnP is turned off in your settings", "off", "game.netplay_upnp"};
    }
    switch (NetUpnpState()) {
        case kUpnpMapped:
            return {"on", "Your router forwarded the port for you", "on", "mapped"};
        case kUpnpForwarded:
            return {"already forwarded", "Your port was already forwarded by hand.\nNothing to do", "already forwarded", "forwarded"};
        case kUpnpFailed: {
            std::string why = NetUpnpWhy();
            const bool  none = why.find("no UPnP") != std::string::npos;

            return {none ? "not detected" : "failed", "Enable UPnP on your router to make this work.\n(Reason: " + why + ".)", none ? "no router" : "failed", why};
        }
        default:
            return {"asking", "Asking your router...", "asking", "trying"};
    }
}

// The checks' log lines, once per change (plan 4.8).
void SayChecks() {
    if (g.local_only) {
        return;
    }
    const Check port = PortCheck();
    const Check upnp = UpnpCheck();
    const std::string code = NetplayLobbyCode();

    if (port.state != g.port_said) {
        g.port_said = port.state;
        std::fprintf(stderr, "lobby: port forward %s (%s)\n", port.state.c_str(), port.from.c_str());
    }
    if (upnp.state != g.upnp_said) {
        g.upnp_said = upnp.state;
        std::fprintf(stderr, "lobby: upnp %s (%s)\n", upnp.state.c_str(), upnp.from.c_str());
    }
    if (code != g.code_said) {
        g.code_said = code;
        std::fprintf(stderr, "lobby: code %s\n", code.empty() ? "(none yet)" : code.c_str());
    }
}

// ---- players on this PC (plan 4.6, Jep 21:22Z) ----

int SeatOfPad(int pad) {
    for (int s = 0; s < kSeats; s++) {
        if (g.seats[s].present && g.seats[s].local && g.seats[s].pad == pad) {
            return s;
        }
    }
    return -1;
}

// The host's copy: Start on a controller nobody holds takes the next open seat for a player on this PC; that seat's card
// asks "Split screen / Multi screen" with that controller (Left/Right, Cross; Circle gives the seat back), then the seat is
// that controller's in this window like any other: its profile, its hand, its lock, its Leave? (Jep, 2026-11-01: no second
// window until the lobby is settled and calls for it). At the start each Multi screen seat's window opens, its controller
// handed over to it (HandOffLocalSeats); split screen is not built: "coming later", the question stays up. The separate
// shape (game.lobby_local): HOST's lobby refuses, MULTIPLAYER > LOCAL's takes.
// Test hook (DC_LOBBY_TEST_PAD_PRESS="N:what@seconds,..."): controller N's presses (start, left, right, up, down, cross,
// circle), each at its time after the lobby showed, as if made on that controller: a row's other local player on the
// lobby's own clock (not a script's tick count that races the lobby's show on a busy PC: Jep's root-cause rule). One press
// a frame at most, three frames apart, so a stall delays the presses and never merges them. Logged "lobby: test pad press".
std::uint16_t TestPadPress(int pad) {
    static const char *const spec = TestEnv("DC_LOBBY_TEST_PAD_PRESS");
    static std::size_t       done = 0;
    static int               last_frame = -100;

    if (spec == nullptr || g.phase != Phase::Run || g.frames - last_frame < 3) {
        return 0;
    }
    const char *list = std::strchr(spec, ':');
    const int   first = std::atoi(spec) - 1;

    if (list == nullptr) {
        return 0;
    }
    const std::string all = list + 1;
    std::size_t       at = 0;
    std::size_t       index = 0;

    while (at < all.size()) {
        const std::size_t end = all.find(',', at);
        const std::string item = all.substr(at, end == std::string::npos ? std::string::npos : end - at);

        at = end == std::string::npos ? all.size() : end + 1;
        if (index++ < done) {
            continue;
        }
        // (an item may name its own controller, "N:what@seconds"; else the spec's first; the presses keep their order)
        const std::size_t colon = item.find(':');
        const int         item_pad = colon != std::string::npos ? std::atoi(item.c_str()) - 1 : first;
        const std::size_t from = colon != std::string::npos ? colon + 1 : 0;
        const std::size_t sign = item.find('@');
        const std::string what = item.substr(from, sign == std::string::npos ? std::string::npos : sign - from);
        const double      when = sign != std::string::npos ? std::atof(item.c_str() + sign + 1) : 0.0;

        if (item_pad != pad) {
            return 0;
        }
        if (std::chrono::duration<double>(std::chrono::steady_clock::now() - g.shown_at).count() < when) {
            return 0;
        }
        done++;
        last_frame = g.frames;
        std::fprintf(stderr, "lobby: test pad press %s on controller %d\n", what.c_str(), pad + 1);
        return what == "start"   ? kInputStart
               : what == "left"  ? kInputLeft
               : what == "right" ? kInputRight
               : what == "up"    ? kInputUp
               : what == "down"  ? kInputDown
               : what == "cross" ? kInputCross
               : what == "circle" ? kInputCircle
                                  : 0;
    }
    return 0;
}

void StepLocalPads() {
    for (int pad = 0; pad < kInputPadCount; pad++) {
        InputPadState state = InputGetPad(pad);

        const std::uint16_t test = TestPadPress(pad);

        state.buttons |= test;
        state.connected = state.connected || test != 0;
        std::uint16_t down = (std::uint16_t) (state.buttons & ~g.pad_last[pad]);

        g.pad_last[pad] = state.buttons;
        // The keyboard is its typist's (Jep, 2026-11-01: whoever has the keyboard open uses it): it folds into the first
        // controller, so while another seat on this PC types a name (or the host its friend's NAT code) and a key is held,
        // the first controller's seat reads nothing this frame (its letters are that seat's buttons too). DC_LOBBY_MUTATE=
        // keys_shared (l-lobby -Case typist's twin): the keys reach it, as before.
        if (pad == 0 && KeyboardHeldByOther(pad == g.host_pad ? 0 : SeatOfPad(pad)) && !Mutated("keys_shared")) {
            if (down != 0) {
                std::fprintf(stderr, "lobby: controller 1's press held back (seat %d types)\n", KeyboardTypist() + 1);
            }
            down = 0;
            state.buttons = 0;
        }
        if (pad == g.host_pad) {
            g.host_down = state.connected ? down : 0;
            g.host_on = state.connected ? state.buttons : 0;
            continue;
        }
        const int s = SeatOfPad(pad);

        // A seated controller's seat steps every frame, as the host's own does, pressed or not (Jep's LAN pack 7 playtest,
        // 2026-11-01): stepped only on a frame with a new press, its locked model was driven on press frames alone (the
        // stick never read, a d-pad press ran on until the next press, a swing re-armed only by another press) and its
        // name's keyboard was read on those frames alone (letters with no button of their own never typed; Backspace's
        // key state went stale after one press). DC_LOBBY_MUTATE=step_on_press (l-lobby -Case pads' twin): as before.
        if (s >= 0 && !g.seats[s].screen_ask && !(Mutated("step_on_press") && down == 0)) {
            // (kInput* and PAD_* are the same bits; a controller pulled out reads released)
            StepSeat(s, state.connected ? Pad{down, state.buttons} : Pad{});
            continue;
        }
        if (!state.connected || down == 0) {
            continue;
        }

        if (s < 0) {
            if ((down & kInputStart) == 0) {
                continue;
            }
            if (Local() == LocalShape::Separate && !g.local_only) {
                SoundRefuse();
                Note("Local players: MULTIPLAYER > LOCAL");
                std::fprintf(stderr, "lobby: controller %d's Start refused (local players are MULTIPLAYER > LOCAL's)\n", pad + 1);
                continue;
            }
            int free = -1;

            for (int i = 1; i < std::min(kSeats, kLinkPlayers) && free < 0; i++) {
                free = !g.seats[i].present ? i : -1;
            }
            if (free < 0) {
                SoundRefuse();
                Note("No open seat");
                std::fprintf(stderr, "lobby: controller %d's Start: no open seat\n", pad + 1);
                continue;
            }
            SoundPick();
            TakeLocalSeat(free, pad);
            std::fprintf(stderr, "lobby: controller %d took seat %d: split or multi screen?\n", pad + 1, free + 1);
            continue;
        }
        Seat &seat = g.seats[s];

        if (!seat.screen_ask) {
            continue; // (step_on_press's frame with no press)
        }
        // Split above Multi on the card: Up/Down choose (Jep's LAN pack 7 playtest: Left/Right alone moved between them);
        // Left/Right still do. DC_LOBBY_MUTATE=screen_lr_only (twin): Left/Right alone, as before.
        const std::uint16_t choose = Mutated("screen_lr_only") ? kInputLeft | kInputRight : kInputLeft | kInputRight | kInputUp | kInputDown;

        if (down & choose) {
            seat.screen_multi = !seat.screen_multi;
            SoundMove();
        } else if (down & kInputCircle) {
            SoundMove();
            std::fprintf(stderr, "lobby: controller %d gave seat %d back\n", pad + 1, s + 1);
            g.seats[s] = Seat{};
        } else if (down & kInputCross) {
            if (!seat.screen_multi) {
                SoundRefuse();
                Note("Split screen is coming later");
                std::fprintf(stderr, "lobby: seat %d split screen: coming later\n", s + 1);
                continue;
            }
            SoundPick();
            seat.screen_ask = false;
            std::fprintf(stderr, "lobby: seat %d multi screen: its window opens at the start\n", s + 1);
        }
    }
}

// The start: each Multi screen seat on this PC gets its own window, its controller handed over to it (Jep, 2026-11-01: "if
// they have multiscreen selected, it hands that controller off to the multiscreen instance"). netplay starts the copies once
// this one hosts; each joins as that seat's identity (its profile and member, LateJoin) on that controller alone.
void HandOffLocalSeats() {
    for (int s = 1; s < kSeats; s++) {
        const Seat &seat = g.seats[s];

        if (seat.present && seat.local && seat.screen_multi && seat.pad >= 0) {
            // DC_LOBBY_MUTATE=handoff_unpaired (l-lobby -Case local's twin): the window gets no controller (it waits for a
            // press to claim one, as Local's plain window did).
            NetplayLobbyHandOff(s, g.host_pad, Mutated("handoff_unpaired") ? -1 : seat.pad, seat.identity);
            std::fprintf(stderr, "lobby: seat %d hands controller %d to its own window (%s as %s)\n", s + 1, seat.pad + 1,
                         SeatName(s).c_str(), seat.pick >= 0 ? kMembers[seat.pick] : "?");
        }
    }
}

// ---- the host's Friend's code (plan 4.3a) ----

// The entry: Up/Down change the last letter, Right adds one, Left deletes one, Cross takes the code, Circle cancels (the
// code entry's keys, Join > Code).
void StepCodeEntry(const Pad &pad) {
    bool enter = false;

    if (KeyboardTyping(g.code_text, kCodeLetters, 10, &enter) && !enter) {
        return;
    }
    if (enter) {
        if (NetplayLobbyFriendCode(g.code_text.c_str())) {
            SoundPick();
            g.friend_code = g.code_text;
            g.friend_at = std::chrono::steady_clock::now();
            g.code_open = false;
            std::fprintf(stderr, "lobby: friend's code %s taken: punching\n", g.friend_code.c_str());
        } else {
            SoundRefuse();
            Note("A code is 8 or 10 letters and digits");
        }
        return;
    }
    if ((pad.Down(PAD_UP) || pad.Down(PAD_DOWN)) && !g.code_text.empty()) {
        g.code_letter = (g.code_letter + (pad.Down(PAD_UP) ? 1 : kCodeLetterCount - 1)) % kCodeLetterCount;
        g.code_text.back() = kCodeLetters[g.code_letter];
        SoundMove();
    } else if (pad.Down(PAD_RIGHT)) {
        // (the same keys as Join > Code's entry: Right adds a 0, Up / Down count it on)
        if (g.code_text.size() < 10) {
            g.code_text += '0';
            g.code_letter = 0;
            SoundMove();
        }
    } else if (pad.Down(PAD_LEFT)) {
        if (!g.code_text.empty()) {
            g.code_text.pop_back();
            SoundMove();
        }
    } else if (pad.Down(PAD_CROSS)) {
        if (NetplayLobbyFriendCode(g.code_text.c_str())) {
            SoundPick();
            g.friend_code = g.code_text;
            g.friend_at = std::chrono::steady_clock::now();
            g.code_open = false;
            std::fprintf(stderr, "lobby: friend's code %s taken: punching\n", g.friend_code.c_str());
        } else {
            SoundRefuse();
            Note("A code is 8 or 10 letters and digits");
        }
    } else if (pad.Down(PAD_CIRCLE) || pad.Down(PAD_TRIANGLE)) {
        SoundMove();
        g.code_open = false;
        std::fprintf(stderr, "lobby: friend's code entry closed\n");
    }
}

// What the Friend's code row says: the code and its punch ("punching... 4 s", "heard"); empty once a guest has a seat.
std::string FriendLine() {
    if (g.code_open) {
        return g.code_text + "_";
    }
    if (g.friend_code.empty()) {
        return "_ _ _ _ _ - _ _ _ _ _";
    }
    for (int s = 1; s < kSeats; s++) {
        if (g.seats[s].present && !g.seats[s].local) {
            return g.friend_code + "   linked";
        }
    }
    long long   sent = 0, heard = 0;
    const char *last_from = "";
    std::string friend_ip = NetplayLobbyFriend();

    NetSocketUdpCounts(sent, heard, last_from);
    friend_ip = friend_ip.substr(0, friend_ip.rfind(':'));
    if (last_from != nullptr && !friend_ip.empty() && std::string(last_from).rfind(friend_ip, 0) == 0) {
        return g.friend_code + "   heard";
    }
    const long long s = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - g.friend_at).count();

    return g.friend_code + "   punching... " + std::to_string(s) + " s";
}

// ---- DC_LOBBY_TEST_PRESS (a later test's scripted player, plan 5): "what@seconds,..." from the lobby's first shown frame,
// what one of left, right, lock, unlock, leave, yes, no, start, up, down, cross, circle, square, triangle, select, r1
// (coop-consent); done on this copy's own seat as its pad would. Logged "lobby: test press <what>".
void TestPresses(double shown_s) {
    static const char *const spec = TestEnv("DC_LOBBY_TEST_PRESS");

    if (spec == nullptr) {
        g.presses_done = true;
        return;
    }
    std::string all = spec;
    std::size_t at = 0;
    std::size_t index = 0;

    while (at < all.size()) {
        std::size_t       end = all.find(',', at);
        const std::string item = all.substr(at, end == std::string::npos ? std::string::npos : end - at);

        at = end == std::string::npos ? all.size() : end + 1;
        if (index++ < g.pressed) {
            g.presses_done = at >= all.size();
            continue;
        }
        const std::size_t sign = item.find('@');
        const std::string what = item.substr(0, sign);
        const double      when = sign != std::string::npos ? std::atof(item.c_str() + sign + 1) : 0.0;

        if (shown_s < when) {
            return;
        }
        // "startall": Start, held until every seat in the link's capacity is present and locked (a test's host that waits
        // for its guest, whatever the guest's timing).
        if (what == "startall") {
            bool ready = Present() >= std::min(kSeats, kLinkPlayers) && EveryoneLocked();

            for (int s = 0; s < kSeats; s++) {
                ready = ready && !(g.seats[s].present && g.seats[s].naming);
            }
            if (!ready) {
                return;
            }
        }
        g.pressed++;
        std::fprintf(stderr, "lobby: test press %s\n", what.c_str());

        Seat &seat = g.seats[g.me];

        if (what == "left" || what == "right") {
            const int step = what == "left" ? -1 : 1;

            seat.hover = NextShown(g.me, seat.hover, step);
            std::fprintf(stderr, "lobby: seat %d on %s\n", g.me + 1, kMembers[seat.hover]);
            if (g.guest) {
                Send(kRequest);
            }
        } else if (what == "lock" && seat.pick < 0) {
            Lock(g.me);
        } else if (what == "unlock" && seat.pick >= 0) {
            Unlock(g.me);
        } else if (what == "leave") {
            seat.leaving = true;
            seat.leave_yes = false;
        } else if (what == "yes" || what == "no") {
            seat.leaving = false;
            std::fprintf(stderr, "lobby: seat %d leave? %s\n", g.me + 1, what.c_str());
            if (what == "yes") {
                Leave(g.me);
            }
        } else if (what == "start" && g.guest && !seat.active) {
            seat.active = true;
            std::fprintf(stderr, "lobby: seat %d pressed Start: lit\n", g.me + 1);
            Send(kRequest);
        } else if ((what == "start" || what == "startall") && !g.guest) {
            TryStart();
        } else if (what.rfind("want", 0) == 0 && g.guest) {
            // A guest's lock request for a member whatever its own screen shows (a request crossing another's lock).
            g.want = std::atoi(what.c_str() + 4);
            std::fprintf(stderr, "lobby: seat %d asks to lock %s\n", g.me + 1, IsUsable(g.want) ? kMembers[g.want] : "?");
            Send(kRequest);
        } else if (what == "select") {
            if (g.guest) {
                std::fprintf(stderr, "lobby: select ignored on a guest\n");
            } else {
                SteamSelect();
            }
        } else if (what == "r1" && !g.guest && !g.local_only) {
            ToggleIp(); // coop-consent
        }
    }
}

// ---- drawing ----

void Fill(int x, int y, int w, int h, Rgb c, int alpha) {
    spRGBA rgba = {c.r, c.g, c.b, (unsigned char) std::clamp(alpha, 0, 128)};

    set2DSpriteC4(GetVif1Packet(), CRect_i_(x, y, w, h), &rgba, &rgba, &rgba, &rgba);
}

void Frame(int x, int y, int w, int h, int thick, Rgb c, int alpha) {
    Fill(x, y, w, thick, c, alpha);
    Fill(x, y + h - thick, w, thick, c, alpha);
    Fill(x, y, thick, h, c, alpha);
    Fill(x + w - thick, y, thick, h, c, alpha);
}

void Text(const std::string &text, int x, int y, int alpha, float scale = 1.0f, dc::ui::Alignment align = dc::ui::Alignment::Left) {
    dc::ui::Label label(text, dc::ui::ColorWhite, align, true, scale);

    label.Draw(x, y, alpha);
}

void TextRgb(const std::string &text, int x, int y, Rgb c, int alpha, float scale = 1.0f, dc::ui::Alignment align = dc::ui::Alignment::Left) {
    dc::ui::Label label(text, dc::ui::ColorWhite, align, true, scale);

    label.SetCustomRgb(c.r, c.g, c.b);
    label.Draw(x, y, alpha);
}

void TextColour(const std::string &text, int x, int y, u32 colour, int alpha, float scale = 1.0f, dc::ui::Alignment align = dc::ui::Alignment::Left) {
    dc::ui::Label label(text, colour, align, true, scale);

    label.Draw(x, y, alpha);
}

constexpr Rgb kDim = {120, 124, 132};
constexpr Rgb kBlack = {0, 0, 0};

// The type sizes: names on boards and the header's words, the body, the small print (one grid: Jep's "unpolished", 10-31).
constexpr float kTitleSize = 0.9f;
constexpr float kBodySize = 0.62f;
constexpr float kSmallSize = 0.52f;

// The Items menu's stone board ("status": its character panel, 248 x 133 at 0, 80), drawn as a nine-slice so it takes any
// size: 16-texel corners, edges and middle taken from plain stretches of it (no icons). Chronicle's BlueStone plate until
// the pack is read, or if it is missing.
void Plate(int x, int y, int w, int h, int alpha) {
    if (g.board_tex == nullptr || w < 32 || h < 32) {
        dc::ui::Window::Draw(x, y, w, h, alpha, dc::ui::WindowPreset::BlueStone);
        return;
    }
    MenuTextureReload(kBoardBlock);
    setbilinear(1);
    constexpr int c = 16;                       // corner
    const int     sx[3] = {0, 120, 232};        // source columns: left corner, middle, right corner
    const int     sw[3] = {c, 100, c};
    const int     sy[3] = {80, 100, 197};       // source rows: top corner, middle, bottom corner
    const int     sh[3] = {c, 90, c};
    const int     dx[3] = {x, x + c, x + w - c};
    const int     dw[3] = {c, w - 2 * c, c};
    const int     dy[3] = {y, y + c, y + h - c};
    const int     dh[3] = {c, h - 2 * c, c};

    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 3; col++) {
            // (edges: the side strips from a plain stretch, x 0..16 at y 100..138 holds no icon)
            CRect_i_ src(sx[col], sy[row], sw[col], sh[row]);

            if (col == 0 && row == 1) {
                src = CRect_i_(0, 100, c, 38);
            }
            DrawMenu2DSprite(g.board_tex, CRect_i_(dx[col], dy[row], dw[col], dh[row]), src, alpha);
        }
    }
}

// A seat's badge: its number on a chip of its colour (the mock's round marks, square-cornered in the game's flat sprites).
void Badge(int s, int x, int y, int alpha) {
    const Rgb c = kSeatColour[s];

    Fill(x - 1, y - 1, 20, 20, {16, 20, 32}, alpha);
    Fill(x, y, 18, 18, c, alpha);
    TextRgb(std::to_string(s + 1), x + 9, y + 1, {20, 24, 36}, alpha, kBodySize, dc::ui::Alignment::Center);
}

// The dark message window (the game's help window, as the Options screen's).
void MessageWindow(int x, int y, int w, int h, int alpha) {
    if (!g.tex_ready) {
        return;
    }
    MenuTextureReload(kBlock);
    MenuHelpWinDraw(x, y, std::max(0.0f, (w - 48) / 16.0f), std::max(0.0f, (h - 44) / 16.0f), alpha);
}

void DrawHeader(int alpha) {
    Plate(kLeft, kHeaderY, kSeats * kPitch - 4, kHeaderH, alpha);
    if (g.guest) {
        // A guest's header: whose game, linked and its round trip; never how it got in (Jep).
        TextColour(SeatName(0) + "'s game", kLeft + 18, kHeaderY + 8, dc::ui::ColorGold, alpha, kTitleSize);
        Text(g.rtt_ms >= 0 ? "linked, " + std::to_string(g.rtt_ms) + " ms" : "linked", kLeft + 280, kHeaderY + 11, alpha, kBodySize);
        Text(g.starting ? "The host is starting the game..." : "Waiting for the host to start", kLeft + 18, kHeaderY + 34, alpha, kBodySize);
        return;
    }
    if (g.local_only) {
        TextColour("Local game", kLeft + 18, kHeaderY + 8, dc::ui::ColorGold, alpha, kTitleSize);
        Text("Everyone plays on this PC", kLeft + 18, kHeaderY + 34, alpha, kBodySize);
        return;
    }
    // The outside address and the code (plan 4.2: "IP", never the LAN address unless Square shows it for a LAN party).
    int         wan_port = 0;
    const char *wan = NetSocketWan(wan_port);
    int         upnp_port = 0;
    const char *upnp = NetUpnpOutside(upnp_port);
    const bool  by_upnp = upnp[0] != '\0' && NetUpnpPublic();
    const char *code = NetplayLobbyCode();
    std::string address;
    // coop-consent (Jep, 2026-11-02): the outside IP only once its player shows it (R1, ToggleIp); the NAT code stays
    // (Jep: "leave that one"). DC_CONSENT_MUTATE=ip_shown (the twin): shown from the start, as before.
    const bool  ip_shown = g.ip_shown || TestEnvIs("DC_CONSENT_MUTATE", "ip_shown");
    const char *kind = "address";

    if (g.lan_shown) {
        address = std::string(NetSocketLocalAddress()) + " : " + std::to_string(NetplayLobbyPort());
    } else if (!ip_shown) {
        address = "hidden";
        kind = "hidden";
    } else if (by_upnp) {
        address = std::string(upnp) + " : " + std::to_string(upnp_port);
    } else if (wan[0] != '\0') {
        address = std::string(wan) + " : " + std::to_string(wan_port);
    } else {
        address = Seconds() > 10.0 ? "unknown" : "checking...";
        kind = Seconds() > 10.0 ? "unknown" : "checking";
    }
    // (what the header draws, never the address itself: a log may go to the partner, partner-log.txt)
    static std::string said;

    if (said != kind) {
        said = kind;
        std::fprintf(stderr, "lobby: header IP: %s\n", kind);
    }
    // One baseline: the word, then each label small and its value in gold.
    TextColour("Hosting", kLeft + 18, kHeaderY + 8, dc::ui::ColorGold, alpha, kTitleSize);
    Text(g.lan_shown ? "LAN" : "IP", kLeft + 150, kHeaderY + 12, alpha, kSmallSize);
    if (std::strcmp(kind, "hidden") == 0) {
        Text(address, kLeft + 176, kHeaderY + 10, alpha, kBodySize);
    } else {
        TextColour(address, kLeft + 176, kHeaderY + 10, dc::ui::ColorGold, alpha, kBodySize);
    }
    // "NAT code", the same words Join's way uses (Jep, 2026-10-31).
    Text("NAT code", kLeft + 340, kHeaderY + 12, alpha, kSmallSize);
    TextColour(code[0] != '\0' ? code : "...", kLeft + 396, kHeaderY + 10, dc::ui::ColorGold, alpha, kBodySize);
    if (NetSteamListening()) {
        TextColour("Steam: listening", kLeft + 396, kHeaderY + 34, dc::ui::ColorYellow, alpha, kSmallSize);
    }
    // coop-steamsetup (Jep, 2026-11-03): what Steam joins need, checked only (steam_check.hpp); the Friend's NAT code entry
    // takes this line while open.
    if (!g.code_open) {
        const steamcheck::State &steam = steamcheck::Get();

        TextColour(steamcheck::Line(), kLeft + 18, kHeaderY + 35, steam.Ready() ? dc::ui::ColorGold : dc::ui::ColorYellow, alpha, kSmallSize);
    }
    // The friend's NAT code (plan 4.3a) only while its entry is open (Jep, 2026-10-31: visible only when selected).
    if (g.code_open) {
        Text("Friend's NAT code", kLeft + 18, kHeaderY + 35, alpha, kSmallSize);
        TextColour(FriendLine(), kLeft + 130, kHeaderY + 33, dc::ui::ColorYellow, alpha, kBodySize);
    }
}

void DrawSeat(int s, int alpha) {
    const Seat &seat = g.seats[s];
    const int   x = kLeft + s * kPitch;
    const int   a = alpha;

    Plate(x, kSeatY, kColumn, kSeatH, a);
    Badge(s, x + 8, kSeatY + 15, a);
    // Open, then Connected / press Start to join, then the player's profile (Jep's playtest, 2026-10-31). Every seat can be
    // joined (no "later": the link's capacity is the host's to grow, never the player's to see).
    if (!seat.present) {
        Text("Open", x + 32, kSeatY + 18, alpha * 3 / 4, kBodySize);
        return;
    }
    if (!seat.active) {
        Text("Connected", x + 32, kSeatY + 9, alpha, kBodySize);
        TextColour("press Start", x + 30, kSeatY + 28, dc::ui::ColorYellow, alpha, 0.46f);
        return;
    }
    if (seat.screen_ask) {
        // A player on this PC: which screen (on this PC only; its controller answers).
        Text("Controller " + std::to_string(seat.pad + 1), x + 28, kSeatY + 4, alpha, 0.56f);
        TextColour("Split", x + 28, kSeatY + 18, seat.screen_multi ? dc::ui::ColorWhite : dc::ui::ColorYellow, alpha, 0.56f);
        TextColour("Multi", x + 28, kSeatY + 32, seat.screen_multi ? dc::ui::ColorYellow : dc::ui::ColorWhite, alpha, 0.56f);
        Text("screen", x + 58, kSeatY + 25, alpha * 2 / 3, 0.56f);
        return;
    }
    if (seat.awaiting_copy) {
        Text(SeatName(s), x + 28, kSeatY + 7, alpha, 0.56f);
        Text("window...", x + 28, kSeatY + 25, alpha, 0.56f);
        return;
    }
    if (seat.naming && !seat.leaving) {
        // The profile ask, on that player's own copy only (the host's copy for every player on this PC); others see
        // "naming..." (plan 4.10).
        if (s != g.me && !(seat.local && !g.guest)) {
            // Another copy's card as that copy shows it (coop-lobbyleave); "naming..." until its first message says.
            if (seat.shown.empty() && !seat.shown_typing) {
                Text("naming...", x + 28, kSeatY + 16, alpha, 0.56f);
            } else if (seat.shown_typing) {
                Text("Name:", x + 28, kSeatY + 7, alpha, 0.56f);
                TextColour(seat.shown + "_", x + 28, kSeatY + 25, dc::ui::ColorYellow, alpha, 0.56f);
            } else {
                Text("Profile:", x + 28, kSeatY + 7, alpha, 0.56f);
                TextColour("< " + seat.shown + " >", x + 24, kSeatY + 25, dc::ui::ColorYellow, alpha, 0.56f);
            }
            return;
        }
        if (seat.typing) {
            Text("Name:", x + 28, kSeatY + 7, alpha, 0.56f);
            TextColour(seat.entry + "_", x + 28, kSeatY + 25, dc::ui::ColorYellow, alpha, 0.56f);
            return;
        }
        const std::vector<std::string> names = g.guest ? g.guest_profiles : FreeProfiles(s);
        const std::string              shown = seat.profile < (int) names.size() ? names[seat.profile] : "New";

        Text("Profile:", x + 28, kSeatY + 7, alpha, 0.56f);
        TextColour("< " + shown + " >", x + 24, kSeatY + 25, dc::ui::ColorYellow, alpha, 0.56f);
        return;
    }
    if (seat.leaving) {
        // The leave question takes the whole card (Jep, 21:19Z).
        Text("Leave?", x + 28, kSeatY + 7, alpha, 0.56f);
        TextColour("Yes", x + 28, kSeatY + 25, seat.leave_yes ? dc::ui::ColorYellow : dc::ui::ColorWhite, seat.leave_yes ? alpha : alpha * 2 / 3, 0.56f);
        TextColour("No", x + 58, kSeatY + 25, seat.leave_yes ? dc::ui::ColorWhite : dc::ui::ColorYellow, seat.leave_yes ? alpha * 2 / 3 : alpha, 0.56f);
        return;
    }
    Text(SeatName(s), x + 32, kSeatY + 9, alpha, kBodySize);
    if (seat.pick >= 0) {
        TextRgb("LOCKED IN", x + 32, kSeatY + 27, kSeatColour[s], alpha, kSmallSize);
    } else {
        Text("choosing", x + 32, kSeatY + 27, alpha * 3 / 4, kSmallSize);
    }
}

// The slots (Jep, 2026-11-01 17:16Z): board s is seat s's own, showing the member its player has on it (Left / Right change
// who), never all six at once. A slot's state: the member not yet in the story a silhouette, a member another seat has
// locked darkened, else bright in full colour; locked, the same brightness with the model run by its player (plan 4.9).
// The card is drawn in game every frame, so a change of state eases (CardShade) instead of snapping. A seat not present
// or not lit has an empty slot.
enum class Card { Selected, Darkened, Silhouette };

const char *CardName(Card card) {
    return card == Card::Selected ? "bright" : card == Card::Darkened ? "darkened" : "a silhouette";
}

// The member slot s shows, -1 none (an empty seat, or one not lit yet).
int SlotMember(int s) {
    const Seat &seat = g.seats[s];

    if (!seat.present || !seat.active) {
        return -1;
    }
    return seat.pick >= 0 ? seat.pick : seat.hover;
}

Card CardOf(int s) {
    const int member = SlotMember(s);
    const int by = LockedBy(member);

    if (!IsUsable(member)) {
        return Card::Silhouette;
    }
    return by >= 0 && by != s ? Card::Darkened : Card::Selected;
}

// The coop-lobbymodels SetShade switch (defined with it in coop_lobby_models.hpp).
#ifndef DC_LOBBY_MODELS_SHADE
#define DC_LOBBY_MODELS_SHADE 0
#endif

// This frame's brightness of slot s's card (kCardDark .. 1), eased a fifth of the way to its state's each frame (about a
// quarter second). DC_LOBBY_MUTATE=cards_flat (l-lobby -Case slots' twin): every card at full brightness. Logged once per
// change of member or state ("lobby: slot N <member> <state>"), and once more when the ease settles.
constexpr float kCardDark = 0.45f;

float CardShade(int s) {
    const int   member = SlotMember(s);
    const Card  card = CardOf(s);
    const float target = card == Card::Selected || Mutated("cards_flat") ? 1.0f : kCardDark;
    float      &shade = g.card_shade[s];
    const bool  locked = g.seats[s].pick == member;
    const int   key = (member * 4 + (int) card) * 2 + (locked ? 1 : 0); // (a lock is a new state too: its line says so)

    if (g.card_state[s] != key) {
        g.card_state[s] = key;
        g.card_since[s] = g.frames;
        g.card_settled[s] = false;
        std::fprintf(stderr, "lobby: slot %d %s %s%s\n", s + 1, kMembers[member], CardName(card), locked ? " (locked in)" : "");
    }
    shade += (target - shade) * 0.2f;
    if (std::abs(target - shade) < 0.01f) {
        shade = target;
        if (!g.card_settled[s]) {
            g.card_settled[s] = true;
            std::fprintf(stderr, "lobby: slot %d settled at %.2f after %d frames\n", s + 1, shade, g.frames - g.card_since[s]);
        }
    }
    return shade;
}

void DrawSlotFace(int s, int member, int alpha, bool step);

// One slot's board: its face, then its state's darkness over the whole card (plate, well, model and words alike). step: the
// first slot this frame to show its member (a second draws the model as it stands).
void DrawSlot(int s, int alpha, bool step) {
    const int member = SlotMember(s);
    const int x = kLeft + s * kPitch;

    if (member < 0) {
        // An empty seat: the board dimmed, its well empty.
        const dc::ui::Rect well(x + kWellInset, kBoardY + kWellTop, kColumn - 2 * kWellInset, kWellH);

        if (g.card_state[s] != -2) {
            g.card_state[s] = -2;
            g.card_shade[s] = kCardDark;
            std::fprintf(stderr, "lobby: slot %d empty\n", s + 1);
        }
        Plate(x, kBoardY, kColumn, kBoardH, alpha / 2);
        dc::ui::TrackerWindow::DrawIndentation(well, true, alpha / 2);
        Text(g.seats[s].present ? "press Start" : "Open", x + kColumn / 2, kBoardY + kWellTop + kWellH + 12, alpha / 2, kBodySize, dc::ui::Alignment::Center);
        return;
    }
    const float shade = CardShade(s);

#if DC_LOBBY_MODELS && DC_LOBBY_MODELS_SHADE
    // The model takes the card's brightness itself (its lights scaled, set before each draw: two slots may show one member
    // at two brightnesses), so the wash below leaves its well alone: the plate, its frames and words only.
    lobbymodels::SetShade(member, shade);
    constexpr bool kWashWell = false;
#else
    constexpr bool kWashWell = true;
#endif
    DrawSlotFace(s, member, alpha, step);
    if (shade < 1.0f) {
        const int a = (int) ((1.0f - shade) * (float) alpha);

        if (kWashWell) {
            Fill(x - 1, kBoardY - 1, kColumn + 2, kBoardH + 2, {0, 0, 0}, a);
        } else {
            // Round the well: above it, below it, and its two sides.
            const int wx = x + kWellInset;
            const int ww = kColumn - 2 * kWellInset;
            const int wy = kBoardY + kWellTop;

            Fill(x - 1, kBoardY - 1, kColumn + 2, kWellTop + 1, {0, 0, 0}, a);
            Fill(x - 1, wy + kWellH, kColumn + 2, kBoardH - kWellTop - kWellH + 1, {0, 0, 0}, a);
            Fill(x - 1, wy, wx - x + 1, kWellH, {0, 0, 0}, a);
            Fill(wx + ww, wy, x + kColumn + 1 - (wx + ww), kWellH, {0, 0, 0}, a);
        }
    }
}

void DrawSlotFace(int s, int member, int alpha, bool step) {
    const Seat &seat = g.seats[s];
    const int   x = kLeft + s * kPitch;
    const bool  usable = IsUsable(member);
    const int   by = LockedBy(member);
    const int   a = usable ? alpha : alpha / 2;
    const int   cx = x + kColumn / 2;
    const bool  choosing = seat.pick < 0 && !seat.naming && !seat.leaving;
    const dc::ui::Rect well(x + kWellInset, kBoardY + kWellTop, kColumn - 2 * kWellInset, kWellH);

    Plate(x, kBoardY, kColumn, kBoardH, a);
    TextColour(kMembers[member], cx, kBoardY + 7, usable ? dc::ui::ColorGold : dc::ui::ColorWhite, a, kTitleSize, dc::ui::Alignment::Center);
    // The well: recessed into the board, then a soft light from above over its floor (no black hole). The indentation's own
    // dark floor (three near-black fills) goes under the light, not over it.
    dc::ui::TrackerWindow::DrawIndentation(well, true, a);
    if (usable) {
        // (the card's darkness on the well's light too, when the model takes it itself and the wash leaves the well)
        const float  k = DC_LOBBY_MODELS && DC_LOBBY_MODELS_SHADE ? g.card_shade[s] : 1.0f;
        const auto   c = [k](int v) { return (unsigned char) std::clamp((int) ((float) v * k), 0, 255); };
        spRGBA       top = {c(58), c(70), c(96), (unsigned char) std::clamp(alpha, 0, 128)};
        spRGBA       bottom = {c(14), c(17), c(28), (unsigned char) std::clamp(alpha, 0, 128)};

        set2DSpriteC4(GetVif1Packet(), CRect_i_(well.x + 2, well.y + 2, well.w - 4, well.h - 4), &top, &top, &bottom, &bottom);
    }

#if DC_LOBBY_MODELS
    // (its rect in the title's 640 x 448 menu space; drawn over the board and its well, in the void; a member not yet in the
    // story drawn as its silhouette, set by the member, the same in every slot)
    lobbymodels::SetSilhouette(member, !usable);
    lobbymodels::Draw(member, (float) well.x, (float) (well.y - (SCREEN_HEIGHT - 448) / 2), (float) well.w, (float) well.h, alpha, step);
#else
    (void) step;
#endif
    // The seat's own colour round its slot (the seat card above has its badge), thicker once locked in.
    Frame(x - 1, kBoardY - 1, kColumn + 2, kBoardH + 2, 1, {16, 20, 32}, alpha);
    Frame(x, kBoardY, kColumn, kBoardH, seat.pick >= 0 ? 4 : 2, kSeatColour[s], seat.pick >= 0 ? alpha : alpha * 3 / 4);
    // Left / Right change who is in the slot: the game's own arrows, either side of the name while choosing.
    if (choosing && seat.active) {
        Text("<", x + 7, kBoardY + 9, alpha * 3 / 4, kBodySize);
        Text(">", x + kColumn - 13, kBoardY + 9, alpha * 3 / 4, kBodySize);
    }

    const int text_y = kBoardY + kWellTop + kWellH + 7;

    if (!usable) {
        // Not yet in the story: "Locked" on the card (Jep, 2026-10-31), under its silhouette.
        Text("Locked", cx, text_y + 5, alpha * 2 / 3, kBodySize, dc::ui::Alignment::Center);
        return;
    }
    if (seat.pick == member) {
        TextRgb("LOCKED IN", cx, text_y - 3, kSeatColour[s], alpha, kSmallSize, dc::ui::Alignment::Center);
        Text("P" + std::to_string(s + 1) + " runs it", cx, text_y + 14, alpha * 3 / 4, kSmallSize, dc::ui::Alignment::Center);
        return;
    }
    if (by >= 0) {
        // Another seat's: whose, in its colour.
        TextRgb("P" + std::to_string(by + 1) + " has it", cx, text_y + 5, kSeatColour[by], alpha, kBodySize, dc::ui::Alignment::Center);
        return;
    }
    Text(seat.naming ? "naming..." : "free", cx, text_y + 5, alpha * 3 / 4, kBodySize, dc::ui::Alignment::Center);
}

// The game's hand (the title's icon01), beside this PC's choosing player's own slot.
void DrawHand(int alpha) {
    const Seat &seat = g.seats[g.me];

    if (!seat.present || !seat.active || seat.naming || seat.pick >= 0 || seat.leaving || seat.hover < 0 || g.focus != Focus::Boards ||
        g.want >= 0) {
        return;
    }
    RECT rect = {0, 0, 32, 32};

    setbilinear(0);
    DrawObjectVibe(kLeft + g.me * kPitch - 22, kBoardY + kWellTop + kWellH / 2 - 16, TexManager.GetTexture("icon01", -1), rect, 128, alpha);
    setbilinear(1);
}

void DrawHints(int alpha) {
    MessageWindow(kHintX, kHintY, kHintW, kHintH, alpha);
    MenuTextureReload(0x1A);

    // One button a line, two columns of up to four rows on a fixed grid inside the window (Jep, 2026-10-31: the bottom row
    // was off its line and the words ran out of the box). No LAN address hint (Square still shows it).
    std::string lines[8];
    int         n = 0;

    if (g.code_open) {
        lines[n++] = "{dpad-updown} Letter";
        lines[n++] = "{dpad-sides} Add / delete";
        lines[n++] = "{cross} Punch";
        lines[n++] = "{circle} Cancel";
    } else if (!g.seats[g.me].active) {
        lines[n++] = "{start} Take your seat";
    } else if (g.seats[g.me].naming && g.seats[g.me].typing) {
        lines[n++] = "{cross} Done (or Enter)";
        lines[n++] = "{circle} Back";
        lines[n++] = "{dpad-updown} Letter (or type)";
        lines[n++] = "{dpad-sides} Add / delete";
    } else if (g.seats[g.me].naming) {
        lines[n++] = "{dpad-sides} Pick a profile";
        lines[n++] = "{cross} Use it (New: make one)";
        lines[n++] = "{circle} Leave";
    } else if (g.seats[g.me].pick >= 0) {
        // Locked in: unlock, move and attack only (Jep, 2026-10-31); the host's Start begins.
        lines[n++] = "{circle} Unlock";
        lines[n++] = "{dpad} Move";
        lines[n++] = "{cross} Attack";
        if (!g.guest) {
            lines[n++] = "{start} Start the game";
        }
        if (!g.guest && !g.local_only) {
            lines[n++] = g.ip_shown ? "{R1} Hide my IP" : "{R1} Show my IP"; // coop-consent
        }
    } else {
        lines[n++] = "{cross} Lock in";
        lines[n++] = "{circle} Unlock / leave";
        lines[n++] = "{dpad-sides} Choose";
        lines[n++] = "{square} Profile";
        if (Local() == LocalShape::Combined || g.local_only) {
            lines[n++] = "{start} New player";
        }
        // The host's own: the friend's NAT code, Steam (coop-lobbysteam).
        if (!g.local_only && !g.guest) {
            lines[n++] = "{triangle} Friend's NAT code";
            lines[n++] = NetSteamListening() ? "{select} Steam off" : "{select} Listen on Steam";
            lines[n++] = g.ip_shown ? "{R1} Hide my IP" : "{R1} Show my IP"; // coop-consent
        }
    }
    constexpr int kRow = 22;
    constexpr int kGlyph = 30; // the glyph's cell: every word starts at the same x
    const int     column = (kHintW - 36) / 2;

    for (int i = 0; i < n; i++) {
        // The glyph as the game draws it (a token draws at the message font's own size, so it goes alone, centred in its
        // cell on the row's middle), then its words in the scaled lettering on the same middle.
        const std::size_t close = lines[i].find('}');
        const int         x = kHintX + 18 + (i / 4) * column;
        const int         mid = kHintY + 22 + (i % 4) * kRow;

        Text(lines[i].substr(0, close + 1), x + kGlyph / 2, mid - 11, alpha, 1.0f, dc::ui::Alignment::Center);
        Text(lines[i].substr(close + 2), x + kGlyph, mid - 8, alpha, kBodySize);
    }
}

void DrawNote(int alpha) {
    MessageWindow(kNoteX, kNoteY, kNoteW, kNoteH, alpha);
    MenuTextureReload(0x1A);

    // A guest's own line: its seat and pick.
    if (g.guest) {
        const Seat &me = g.seats[g.me];

        Text("Your seat: " + std::to_string(g.me + 1) + "   " + (me.pick >= 0 ? std::string(kMembers[me.pick]) + ", locked" : g.want >= 0 ? "locking..." : "choosing"),
             kNoteX + 20, kNoteY + 12, alpha, 0.62f);
    }
    // The host's two checks (plan 4.3), with the hand when the host is on them (Up from the boards; Cross explains).
    if (!g.local_only && !g.guest) {
        const Check port = PortCheck();
        const Check upnp = UpnpCheck();
        const int   port_x = kNoteX + 20;
        const int   upnp_x = kNoteX + 142;

        // (shown, not chosen: Jep, 2026-10-31)
        Text("Port: " + port.row, port_x, kNoteY + 12, alpha, 0.62f);
        Text("UPnP: " + upnp.row, upnp_x, kNoteY + 12, alpha, 0.62f);
    }

    // Only news here (who joined, left, was refused): each seat card already says choosing or locked (Jep, 2026-10-31).
    TextColour(g.note, kNoteX + 20, kNoteY + 34, dc::ui::ColorYellow, alpha, 0.62f);

    if (g.guest) {
        return; // (START is the host's)
    }
    // START, lit only when every seat present is locked (plan 4.4); Start on the pad presses it.
    const bool lit = EveryoneLocked();

    // Lit: the game's gold button, breathing a little; unlit: the same button greyed down.
    const int pulse = lit ? (int) (10.0f * (1.0f + std::sin(g.frames * 0.08f))) : 0;

    dc::ui::BevelButton::Draw(dc::ui::Rect(kNoteX, kPillY, 120, 28), "START",
                          dc::ui::ButtonStyle::Thick(lit ? dc::ui::ButtonPreset::AmberGold : dc::ui::ButtonPreset::StoneGreen, lit ? 0x70 + pulse : 0x38),
                          lit ? alpha : alpha * 2 / 3);
    Text(lit ? "Everyone is locked in" : "All must lock in", kNoteX + 132, kPillY + 8, lit ? alpha : alpha * 2 / 3, kSmallSize);
}

} // namespace

LocalShape Local() {
    static int        said = -1;
    const char       *env = std::getenv("DC_LOBBY_LOCAL");
    const std::string set = env != nullptr ? env : dc::platform::ConfigGet().lobby_local;
    const LocalShape  shape = set == "separate" ? LocalShape::Separate : LocalShape::Combined;

    if (said != (int) shape) {
        said = (int) shape;
        std::fprintf(stderr, "lobby: local players %s (%s)\n", LocalName(shape), env != nullptr ? "DC_LOBBY_LOCAL" : "game.lobby_local");
    }
    return shape;
}

const char *LocalName(LocalShape shape) {
    return shape == LocalShape::Separate ? "separate" : "combined";
}

void Pend(bool pend, bool local_only) {
    if (g.pending != pend || g.local_only != local_only) {
        std::fprintf(stderr, "lobby: %s%s\n", pend ? "pending after the save's pick" : "not pending", pend && local_only ? " (local only)" : "");
    }
    g.pending = pend;
    g.local_only = pend && local_only;
}

bool Pending() {
    return g.pending;
}

void Open(bool new_game) {
    const bool local_only = g.local_only;
    const bool pending = g.pending;

    coop::ProfileBagsSeatsClear(); // (a new lobby: its players are named at its start)
    g = State{};
    g_rejoin.clear(); // (coop-joinlobby: a new lobby's game: nobody was in it yet)
    g.pending = pending;
    g.local_only = local_only;
    g.new_game = new_game;
    g.party = new_game || UserStatus == nullptr ? 1 : std::clamp<int>(UserStatus->party_size, 1, kSeats);
    // Test hook (DC_LOBBY_TEST_PARTY=N, a look's only): the party as if N members had joined (Ruby's card is the 4th).
    if (const int party = TestEnvInt("DC_LOBBY_TEST_PARTY", 0); party > 0) {
        g.party = std::clamp(party, 1, kSeats);
        std::fprintf(stderr, "lobby: test party %d\n", g.party);
    }
    g.phase = Phase::FadeIn;
    CFade.value = 0;

    // The message windows' parts, read in the background (LoadTextures takes them once read).
    u_long128 *data = MenuCalcBufAlignment((u_long128 *) read_buffer);
    StartReadBG();
    g.tex_loading = LoadFileBGMenuData(const_cast<char *>("option.pac"), data) > 0;
    if (!g.tex_loading) {
        std::fprintf(stderr, "lobby: option.pac not read: no message windows\n");
    }

    std::fprintf(stderr, "lobby: open (%s, party %d, usable %d, local players %s%s)\n", new_game ? "new game" : "slot loaded", g.party, Usable(),
                 LocalName(Local()), local_only ? ", local only" : "");
    g.opened = std::chrono::steady_clock::now();
    // coop-consent: the outside IP as its player left it (hidden until shown once). DC_CONSENT_MUTATE=ip_forget: never kept.
    g.ip_shown = !TestEnvIs("DC_CONSENT_MUTATE", "ip_forget") && dc::platform::ConfigGet().lobby_show_ip;
    if (!local_only) {
        std::fprintf(stderr, "lobby: outside IP %s (game.lobby_show_ip)\n", g.ip_shown ? "shown" : "hidden");
    }
    g.host_pad = NetplayActivePad();
    std::fprintf(stderr, "lobby: the host's seat plays controller %d\n", g.host_pad + 1);
    if (!g.guest) {
        NetplayLobbyPinPad(g.host_pad);
    }
    for (int pad = 0; pad < kInputPadCount; pad++) {
        g.pad_last[pad] = InputGetPad(pad).buttons; // (the press that opened the lobby is not a Start to join)
    }
    // The host always listens (plan 4.6): the port and UPnP rows, and a friend may join later; a local-only lobby for its own
    // windows' copies alone (no outside address, no router asked).
    NetplayLobbyListen(!local_only);
    // Test hook (DC_LOBBY_TEST_FRIEND_CODE=code): the host's friend's NAT code taken as if typed in its entry.
    // coop-reds1109 (mp-natwait-port-answer red): taken as the socket opens, not at the fade-in's end. The socket reads from
    // the moment it listens, so a friend's datagram that came in the ~1 s fade-in was from an address this copy had not yet
    // sent to and proved the port open (rightly: nothing had punched yet); the row's "A punches before it hears of B" rested
    // on which copy booted first. Now the first punch (NetplayLobbyFriendCode) goes out before the socket's first read.
    // DC_NETPLAY_MUTATE=friend_code_late (mp-natwait-port-answer~late): taken at the fade-in's end, as before.
    if (const char *code = TestEnv("DC_LOBBY_TEST_FRIEND_CODE");
        code != nullptr && !local_only && !g.guest && !NetplayMutated("friend_code_late") && NetplayLobbyFriendCode(code)) {
        g.friend_code = code;
        g.friend_at = std::chrono::steady_clock::now();
        std::fprintf(stderr, "lobby: friend's code %s taken: punching (test)\n", code);
    }
    TakeSeat(0, true, Identity());
    OpenModels();
}

bool Active() {
    return g.phase != Phase::Closed;
}

Result Step() {
    if (!Active()) {
        return Result::Closed;
    }
    g.frames++;
    LoadTextures();
    if (g.note_frames > 0 && --g.note_frames == 0) {
        g.note.clear();
    }

    switch (g.phase) {
        case Phase::FadeIn:
            if (CFade.In()) {
                g.phase = Phase::Run;
                g.shown_at = std::chrono::steady_clock::now();
                if (const char *code = TestEnv("DC_LOBBY_TEST_FRIEND_CODE");
                    code != nullptr && !g.guest && NetplayMutated("friend_code_late") && NetplayLobbyFriendCode(code)) {
                    g.friend_code = code;
                    g.friend_at = g.shown_at;
                    std::fprintf(stderr, "lobby: friend's code %s taken: punching (test)\n", code);
                }
                std::fprintf(stderr, "lobby: shown (frame %d)\n", g.frames);
                NetplayTestInputMark("lobby"); // (a test's held --input script goes on: DC_TEST_INPUT_HOLD)
            }
            break;
        case Phase::Run:
            // (a face another seat locks under this seat's slot stays shown there, darkened, until its player moves on:
            // the slot says it is taken, CardOf; before the slots the hand moved off by itself)
            // Test hook (DC_LOBBY_TEST_AUTOSTART=1, for rows that host through the title and test what comes after the
            // lobby): the host's seat takes the profile "Host", locks the face under its hand and starts at once, alone
            // (no guest is seated). Logged "lobby: test autostart".
            // (after the scripted presses, DC_LOBBY_TEST_PRESS, so a row can press Select or the like first)
            // DC_LOBBY_TEST_LOCAL_SEAT=N with it: controller N's Start first (seat 2 on this PC, Multi screen, profile
            // "Player 2", locked on the next free member), so its window opens at the start with controller N handed over.
            TestPresses(std::chrono::duration<double>(std::chrono::steady_clock::now() - g.shown_at).count());
            if (static const bool autostart = TestEnvIs("DC_LOBBY_TEST_AUTOSTART", "1"); autostart && !g.guest && g.phase == Phase::Run && g.presses_done) {
                Seat     &host = g.seats[0];
                const int local_pad = TestEnvInt("DC_LOBBY_TEST_LOCAL_SEAT", 0) - 1;

                if (local_pad >= 0 && local_pad != g.host_pad && !g.seats[1].present) {
                    TakeLocalSeat(1, local_pad);
                    g.seats[1].screen_ask = false;
                    Name(1, "Player 2");
                    std::fprintf(stderr, "lobby: test autostart: controller %d took seat 2 (multi screen)\n", local_pad + 1);
                }
                if (host.naming) {
                    Name(0, "Host");
                }
                if (host.pick < 0) {
                    std::fprintf(stderr, "lobby: test autostart\n");
                    Lock(0);
                }
                if (host.pick >= 0 && g.seats[1].present && g.seats[1].local && g.seats[1].pick < 0) {
                    Lock(1);
                }
                if (EveryoneLocked()) {
                    TryStart();
                }
            }
            if (g.phase != Phase::Run) {
                break;
            }
            SayChecks();
            if (!g.guest) {
                StepLocalPads(); // (the other controllers; the host's own walks the screen below)
            } else {
                StepGuestPad();
            }
            DriveOthers();
            if (g_dialog.IsOpen()) {
                g_dialog.HandleInput();
                if (!g_dialog.IsOpen()) {
                    std::fprintf(stderr, "lobby: dialog closed\n");
                }
            } else if (g.guest) {
                const Pad pad = OwnPad();

                if (pad.Down(PAD_SELECT)) {
                    std::fprintf(stderr, "lobby: select ignored on a guest\n");
                }
                if (!g.seats[g.me].active) {
                    // Not lit until its player presses Start on their controller (which also gives this copy that pad).
                    if (pad.Down(PAD_START)) {
                        SoundPick();
                        g.seats[g.me].active = true;
                        std::fprintf(stderr, "lobby: seat %d pressed Start: lit\n", g.me + 1);
                        Send(kRequest);
                    }
                } else if (g.want < 0 || g.seats[g.me].pick >= 0) {
                    StepSeat(g.me, pad);
                }
            } else {
                // The host's own seat: its own controller (and the keyboard), never another local player's presses.
                const Pad   pad = OwnPad();
                const Seat &host = g.seats[0];

                if (g.code_open) {
                    StepCodeEntry(pad);
                } else if (!g.local_only && pad.Down(PAD_R1) && !host.typing) {
                    ToggleIp(); // coop-consent
                } else if (!g.local_only && pad.Down(PAD_SELECT) && host.pick < 0) {
                    // (locked in, only Circle, the stick and Cross do anything, and the host's Start: Jep, 2026-10-31)
                    SteamSelect();
                } else if (!g.local_only && pad.Down(PAD_TRIANGLE) && !host.leaving && !host.typing && !OtherTyping(0) && host.pick < 0) {
                    // (one typist at a time: not while this seat types its profile's name)
                    SoundPick();
                    g.code_open = true;
                    g.code_text = g.friend_code;
                    g.code_letter = 0;
                    std::fprintf(stderr, "lobby: friend's code entry opened\n");
                } else if (pad.Down(PAD_START) && !host.leaving && !host.naming) {
                    TryStart();
                } else {
                    StepSeat(0, pad);
                }
            }
            break;
        case Phase::FadeOut:
            if (CFade.Out()) {
                const Result result = g.leaving;

#if DC_LOBBY_MODELS
                lobbymodels::Close();
#endif
                g.phase = Phase::Closed;
                g.pending = result == Result::Start ? false : g.pending;
                if (result == Result::Start && !g.guest) {
                    // Each profile remembers the member it played (a returning player's hand starts there).
                    for (const Seat &seat : g.seats) {
                        if (Profile *profile = seat.present ? ProfileNamed(seat.name) : nullptr) {
                            profile->last = seat.pick;
                        }
                    }
                    WriteProfiles();
                    int guest = -1;

                    for (int s = 1; s < kSeats && guest < 0; s++) {
                        guest = g.seats[s].present ? g.seats[s].pick : -1;
                    }
                    CoopPartyLobbyPicks(g.seats[0].pick, guest, g.new_game);
                    // Each player's bag is their profile's (Jep, 2026-11-01 23:57Z).
                    std::string guest_name;

                    for (int s = 1; s < kSeats && guest_name.empty(); s++) {
                        guest_name = g.seats[s].present ? SeatName(s) : std::string();
                    }
                    coop::ProfileBagsStart(SeatName(0), guest_name, g.new_game);
                    // Test hook (DC_LOBBY_TEST_RELOAD_SLOT=N): slot N read again right after the start, as a load made then (Jep's
                    // pack 7 session, 2026-11-03: his load after the start put the slot's old bags back).
                    if (const int reload = TestEnvInt("DC_LOBBY_TEST_RELOAD_SLOT", -1); reload >= 0 && !g.new_game) {
                        // coop-worldkeys (DC_LOBBY_TEST_SAVE_FIRST=1): the slot written first, as the save screen does, so the
                        // reload reads back what this start saved (the ring in coop-p2.dat).
                        if (TestEnvIs("DC_LOBBY_TEST_SAVE_FIRST", "1")) {
                            std::fprintf(stderr, "lobby: test save of slot %d before the reload: %s\n", reload, SaveSlotWriteNumbered(reload, nullptr) ? "written" : "not written");
                        }
                        std::fprintf(stderr, "lobby: test reload of slot %d after the start: %s\n", reload, SaveSlotReadNumbered(reload) ? "read" : "not read");
                        coop::ProfileBagsSay("after the test reload");
                    }
                    HandOffLocalSeats();
                }
                if (result != Result::Start && !g.guest) {
                    NetplayLobbyPinPad(-1);
                }
                std::fprintf(stderr, "lobby: %s\n", result == Result::Start ? "faded out: the game starts" : "faded out: back to the menu");
                // Test hook (DC_LOBBY_TEST_START_FILE=<file>): the start's go file once its fade is over, for a mock that
                // waits on it (DC_MOCK_STEAM_LOBBY_GO): Steam's lobby opens only after the whole start.
                if (const char *file = result == Result::Start ? TestEnv("DC_LOBBY_TEST_START_FILE") : nullptr) {
                    std::ofstream(file) << "start\n";
                }
                return result;
            }
            break;
        case Phase::Closed:
            break;
    }
    return Result::Running;
}

void Draw() {
    if (!Active()) {
        return;
    }
    const int alpha = CFade.Get(128);

    setbilinear(1);
    // The title's art dimmed behind the lobby, so its boards stand out.
    Fill(-64, 0, 768, 480, kBlack, alpha * 3 / 8);
    DrawHeader(alpha);
    for (int s = 0; s < kSeats; s++) {
        DrawSeat(s, alpha);
    }
    // Each seat's slot; a member shown in two slots steps once (the first).
    bool stepped[kSeats] = {};

    for (int s = 0; s < kSeats; s++) {
        const int member = SlotMember(s);
        const bool step = member >= 0 && !stepped[member];

        if (member >= 0) {
            stepped[member] = true;
        }
        DrawSlot(s, alpha, step);
    }
    DrawHints(alpha);
    DrawNote(alpha);
    DrawHand(alpha);
    if (g_dialog.IsOpen()) {
        g_dialog.Draw(alpha);
    }

    // Test hook (DC_LOBBY_TEST_SHOT=PATH): a frame of the lobby written to PATH (in-process), once: its 90th shown frame, or
    // with DC_LOBBY_TEST_SHOT_WHEN=mixed (Jep, 2026-10-31: show both views) the 60th frame (the lock's ease done) on which
    // seat 1 is locked and seat 2 is present and still choosing.
    static const char *const shot = TestEnv("DC_LOBBY_TEST_SHOT");
    // DC_LOBBY_TEST_SHOT_WHEN=darkened (l-lobby -Case slots): the 40th frame (its ease settled) on which seat 1 is locked and
    // seat 2's slot shows seat 1's member, darkened.
    static const bool        mixed = TestEnvIs("DC_LOBBY_TEST_SHOT_WHEN", "mixed");
    static const bool        darkened = TestEnvIs("DC_LOBBY_TEST_SHOT_WHEN", "darkened");

    if (shot != nullptr && g.phase == Phase::Run && g.shot_frames >= 0) {
        const bool both = g.seats[0].present && g.seats[0].pick >= 0 && g.seats[1].present && g.seats[1].active && g.seats[1].pick < 0 && !g.seats[1].naming;
        const bool now = darkened ? both && g.seats[1].hover == g.seats[0].pick : !mixed || both;
        const bool held = mixed || darkened;

        g.shot_frames = now ? g.shot_frames + 1 : (held ? 0 : g.shot_frames);
        if (g.shot_frames == (darkened ? 40 : mixed ? 60 : 90)) {
            NetplayTestShot(shot);
            g.shot_frames = -1;
            std::fprintf(stderr, "lobby: test shot written to %s%s\n", shot,
                         darkened ? " (seat 1 locked, seat 2's slot on its member)" : mixed ? " (seat 1 locked, seat 2 choosing)" : "");
        }
    }
}

int LateJoin(std::uint64_t identity) {
    ReadProfiles();
    // The seat this identity had (the lobby's table lives on after the start), else the first open seat in the link.
    int seat = -1;

    for (int s = 1; s < kSeats && seat < 0; s++) {
        seat = g.seats[s].present && g.seats[s].identity == identity ? s : -1;
    }
    const bool had = seat >= 0;

    for (int s = 1; s < std::min(kSeats, kLinkPlayers) && seat < 0; s++) {
        seat = !g.seats[s].present ? s : -1;
    }
    if (seat < 0) {
        std::fprintf(stderr, "lobby: late join: no seat for %016llx\n", (unsigned long long) identity);
        return -1;
    }
    // In use: another present seat's member, and the host's own (the save's player member when it never had a lobby).
    const int was = had ? g.seats[seat].pick : -1;
    int       member = -1;

    if (was >= 0 && Free(was, seat) && (UserStatus == nullptr || was != UserStatus->cur_chara)) {
        member = was;
    }
    for (int m = 0; m < Usable() && member < 0; m++) {
        member = Free(m, seat) && (UserStatus == nullptr || m != UserStatus->cur_chara) ? m : -1;
    }
    Seat &taken = g.seats[seat];

    taken.present = true;
    taken.identity = identity;
    taken.pick = member;
    if (const Profile *profile = ProfileOf(identity); profile != nullptr && taken.name.empty()) {
        taken.name = profile->name;
    }
    std::fprintf(stderr, "lobby: late join: seat %d (%s) %s as %s%s\n", seat + 1, SeatName(seat).c_str(), had ? "is back" : "joined",
                 member >= 0 ? kMembers[member] : "nobody free", was >= 0 && member != was ? " (its member was in use)" : "");
    // coop-joinlobby: the join's member and profile, for the catch-up's last blob (both copies give them at the lockstep:
    // JoinPickTake). DC_LOBBY_MUTATE=join_pick_ignored (twin): not carried, player 2 plays on as the host had her.
    g_join_pick_set = member >= 0 && !Mutated("join_pick_ignored");
    g_join_pick_member = member;
    g_join_pick_name = taken.name;
    if (Mutated("join_pick_ignored")) {
        std::fprintf(stderr, "lobby: late join: the pick is not carried (DC_LOBBY_MUTATE=join_pick_ignored)\n");
    }
    return member;
}

bool Listening() {
    return Active() && !g.guest;
}

bool JoinHoldsHello() {
    // (coop-reds1109 test hook DC_LOBBY_TEST_STALE_TABLE: held after the card closed until NetTick fed its stale table)
    if (g_join_carded && !g_stale_fed && NetplayLobbyJoining() && TestEnvIs("DC_LOBBY_TEST_STALE_TABLE", "1")) {
        return true;
    }
    if (!NetplayLobbyJoining() || g_join_go) {
        return false;
    }
    // A joining copy already in a town or a dungeon (a test's straight-into-a-town copy: l-lobby-handed's twin,
    // p2_blank_town) can never open the card (its table waits for the title), so its Hellos go as before the card;
    // holding them there left the pair unlinked for good. DC_LOBBY_MUTATE=hold_in_world (twin): held there too.
    if (mode == GAME_MODE_EDIT || mode == GAME_MODE_DUNGEON || mode == GAME_MODE_LOADER) {
        static bool said = false;
        const bool  held = Mutated("hold_in_world");

        if (!said) {
            said = true;
            std::fprintf(stderr, "lobby: this copy joins from a world of its own (mode %d): %s\n", (int) mode,
                         held ? "held (DC_LOBBY_MUTATE=hold_in_world)" : "no card, its Hellos go");
        }
        return held;
    }
    return true;
}

bool JoinPickTake(int &member, std::string &name) {
    const bool set = g_join_pick_set;

    member = g_join_pick_member;
    name = g_join_pick_name;
    g_join_pick_set = false;
    return set;
}

bool WasGuest() {
    return g.guest;
}

bool GuestIn() {
    return g.guest && Active();
}

bool Receive(std::uint32_t magic, const void *data, int size) {
    if (magic != kLobbyMagic) {
        return false;
    }
    if (size != (int) sizeof(Wire)) {
        return true; // (another version's: dropped)
    }
    Wire wire;

    std::memcpy(&wire, data, sizeof wire);
    if (wire.version != kLobbyVersion) {
        return true;
    }
    // coop-lobbyleave (LAN pack 8, 2026-11-04: the friend's pack carried Jep's save\config.json, so his copy had Jep's
    // game.netplay_player_id and Jep's lobby took his Hellos for its own and dropped them, unsaid): a message of the other
    // side (a guest's at a listening host, a host's at a guest or a joiner) is never this copy's own, so the same identity
    // there is another copy sharing this one's. Still dropped (seats and profiles go by identity), but said in the log and
    // on the screen. DC_LOBBY_MUTATE=same_id_silent (l-lobby -Case same-id's twin): dropped unsaid, as before.
    if (wire.identity == Identity()) {
        const bool other_side = (Listening() && wire.player == 2) || (wire.player == 1 && ((g.guest && Active()) || NetplayLobbyJoining()));

        if (other_side && !Mutated("same_id_silent")) {
            static auto said = std::chrono::steady_clock::time_point{};
            const auto  now = std::chrono::steady_clock::now();

            if (now - said > std::chrono::seconds(10)) {
                said = now;
                std::fprintf(stderr, "lobby: a %s's message carries this copy's own player id (%016llx): two copies share one "
                                     "game.netplay_player_id (a config.json copied from another PC); not seated\n",
                             wire.player == 2 ? "guest" : "host", (unsigned long long) wire.identity);
                Note(wire.player == 2 ? "A player with this PC's player ID can't join: delete save\\config.json on their PC"
                                      : "The host has this PC's player ID: delete save\\config.json here and start again");
            }
        }
        return true;
    }
    if (Listening() && wire.player == 2) {
        HostReceive(wire);
    } else if (wire.player == 2 && !Active() && NetplayLobbyServesJoin()) {
        PlayingReceive(wire);
    } else if (wire.player == 1 && ((g.guest && Active()) || (!Active() && NetplayLobbyJoining()))) {
        GuestReceive(wire);
    }
    return true;
}

void NetTick() {
    if (!NetplayLobbyJoining()) {
        g_join_go = false; // (coop-joinlobby: the next join's Hellos wait for its card again)
        g_join_carded = false;
        g_have_last_table = false;
    }
    // coop-reds1109 test hook (DC_LOBBY_TEST_STALE_TABLE=1, l-lobby-join-card): once this joiner's card closed with its join going
    // on, the host's last table comes once more with this copy's seat unpicked (a table sent before the host locked it, as
    // the train 55 race had it), on the first net tick after the card closed (its Hellos held till then, JoinHoldsHello: a loopback join links before its card
    // has faded, the NAT join of the red did not); the guard in GuestReceive keeps the card shut.
    if (!g_stale_fed && g_join_carded && !Active() && g_have_last_table && NetplayLobbyJoining() &&
        TestEnvIs("DC_LOBBY_TEST_STALE_TABLE", "1") && g_last_table.your_seat < kSeats) {
        g_stale_fed = true;
        Wire stale = g_last_table;

        stale.phase = kTablePlaying;
        stale.seats[stale.your_seat].pick = kNone;
        std::fprintf(stderr, "lobby: test: a stale table after the join went on (seat %d unpicked)\n", stale.your_seat + 1);
        GuestReceive(stale);
    }
    if (Listening() && g.phase != Phase::Closed) {
        HostNet();
        // coop-pt1108s (Jep's 11-08 playtest: Steam started after the helper never took): the helper tries Steam again by itself;
        // the lobby is listening as soon as it opens.
        if (g.steam_waiting && NetSteamListening()) {
            g.steam_waiting = false;
            g.steam_asked = true;
            std::fprintf(stderr, "lobby: Steam came up after the Select: listening\n");
            Note("Steam is up: friends can join");
        }
        if (NetSteamListening()) {
            const Wire table = TableFor(-1);

            NetplayLobbySteamTick(&table, (int) sizeof table);
        }
        // Closed by itself as the lobby closes (plan 4.3); at the start carried into the game instead, so a friend can
        // still join after it and a dropped player 2 can come back (no feature lost: NAT > Steam's host did both). Keyed on
        // the Select's ask, not on Steam's lobby being open yet: a start right after the Select carries it too (the
        // Steam thread's find, 2026-11-01). DC_LOBBY_MUTATE=steam_start_unseen (mp-nat-steam~unseen): only once it is
        // open, as before: Steam left up with no lobby.
        const bool asked = Mutated("steam_start_unseen") ? NetSteamListening() : g.steam_asked || g.steam_waiting;

        if (g.phase == Phase::FadeOut && asked) {
            g.steam_asked = false;
            g.steam_waiting = false;
            if (g.leaving == Result::Start) {
                NetplayLobbySteamToGame();
            } else {
                NetplayLobbySteam(false);
            }
        }
        return;
    }
    if (g.guest && Active()) {
        if (NetplayLinkUp()) {
            // Linked (the host's game is up and the catch-up began): the lobby is done on this copy.
            std::fprintf(stderr, "lobby: linked: the guest's lobby closes\n");
#if DC_LOBBY_MODELS
            lobbymodels::Close();
#endif
            g.phase = Phase::Closed;
            return;
        }
        GuestNet();
        return;
    }
    // A join picked and not yet in a lobby: a Hello each 250 ms (a host already playing ignores it: the join goes in
    // as today, plan 4.5).
    if (NetplayLobbyJoining()) {
        const auto now = std::chrono::steady_clock::now();

        if (now >= g.send_due) {
            g.send_due = now + kSendEvery;
            g.me = 0;
            Send(kHello);
        }
    }
}

int PickOf(int seat) {
    return seat >= 0 && seat < kSeats && g.seats[seat].present ? g.seats[seat].pick : -1;
}

bool NewGame() {
    return g.new_game;
}

} // namespace lobby
} // namespace coop
