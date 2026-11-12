#include "coop/game_ns.hpp"
#include "coop/netplay.hpp"
#include "coop/coop_writeguard.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <filesystem>
#include <memory>
#include <fstream>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "camera.hpp"
#include "text/caption_text.hpp"
#include "camerafollow.hpp"
#include "character.hpp"
#include "cloth.hpp" // repin: ClothForgetSettled (StateSetAfterApply)
#include "coop/coop.hpp"
#include "coop/coop_perf.hpp"
#include "coop/coop_partnerlog.hpp" // coop-partnerlog
#include "coop/coop_spawn.hpp"
#include "coop/coop_camera.hpp"
#include "coop/coop_camtest.hpp" // coop-lockpress: CamTestOwnPad
#include "coop/coop_combat.hpp"
#include "coop/coop_core.hpp"
#include "coop/coop_georama.hpp"
#include "coop/coop_interact.hpp"  // coop-gateopen: OpenGatesBytes
#include "coop/coop_inventory.hpp" // coop-inventory
#include "coop/coop_party.hpp"     // coop-joinvisuals: PartnerSnapshotApplied
#include "coop/netplay_hello.hpp"
#include "coop/netplay_session.hpp" // coop-session
#include "coop/netplay_local.hpp" // coop-mpmenu: Local
#include "coop/snapshot.hpp"      // coop-snapshot
#include "render/draw3d.hpp"             // coop-toanvis
#include "editmenu.hpp"           // coop-soakfix4: the Georama menu's page (test hook)
#include "memcard.hpp"            // coop-soakfix4: OPTION_STEP_RUN (test hook)
#include "options/screen.hpp"     // coop-soakfix4: the Options screen running (test hook)
namespace dc::game { // coop-soakfix4: the town edit menu's page and cursor (editmenu.cpp, its recipe; a test hook)
int  EditMenuPage();
bool EditMenuOnSelect();
bool EditMenuOnOption();
int  EditMenuCursor();
void EditMenuSetCursor(int selection);
} // namespace dc::game
#include "coop/snapshot_state.hpp" // coop-snapshot: stage 1
#include "coop/netplay_slots.hpp"
#include "coop/netplay_upnp.hpp"    // coop-upnp
#include "coop/coop_lobby.hpp"      // coop-lobby
#include "coop/coop_menu.hpp"      // coop-menus
#include "coop/coop_pause.hpp"     // coop-pause
#include "coop/coop_town.hpp"      // coop-uinotice: CoopTownPartnerMember
#include "coop/netplay_cmd.hpp" // coop-localmenu: commands
#include "coop/coop_actor.hpp" // coop-localmenu: the in-menu bits
#include "coop/coop_opener.hpp" // coop-opt7: the presentations
#include "coop/coop_town.hpp"
#include "coop/coop_townmenu.hpp" // coop-saveload
#include "coop/coop_scriptmenu.hpp" // coop-small2: a script's menu open at the merge
#include "coop/coop_worlds.hpp"    // coop-worlds
#include "coop/coop_wheel.hpp"     // coop-wheelcrash
#include "coop/netplay_socket.hpp"
#include "coop/netplay_steam.hpp"
#include "coop/netplay_test.hpp"
#include "platform/input_script.hpp" // coop-serve: a job's input script
#include "coop/input_anchors.hpp"     // coop-anchors: its @tick and @event lines
#include "coop/coop_fullmenu.hpp" // coop-fullmenu
#include "coop/coop_duntest.hpp" // coop-soakfix3: DunTestAloneFrame

int CoopTownGroundEventAt(const float *at); // coop_town.cpp (coop-soakfix3, tests)
#include "dun/gameloop.hpp"
#include "dun/motiondrive.hpp" // coop-p2absent
#include "userstatus.hpp"      // coop-snapshot: the floor-start checkpoint's floor
#include "dngmessageman.hpp" // coop-pt1111h: DC_COOP_TEST_LIMIT_ZONE_LOADED

namespace dc::game {
extern sceVu0FVECTOR ref_off; // dun/gameloop.cpp: the camera's offset for the player's move (the trip's inputs line)
extern float         inputH1;
} // namespace dc::game
#include "collisiondata.hpp" // coop-p2absent: the tick trace
#include "dungeonmap.hpp" // gaps2: the floor sum
#include "editloop.hpp"
#include "editmapscript.hpp" // coop-snapjoin: the town's walk mode (SnapshotQuietTick)
#include "edit.hpp" // coop-localfix: EdEventInfo (a debug line)
#include "app/gameloop.hpp"
#include "battlemenu.hpp" // coop-pt1106-lockup: BtlMenuReadEndFlag
#include "menuitemstep.hpp" // coop-guard: the bag's melt timer (Checksum)
#include "gfx/gfx.hpp" // coop-itemsend: DC_COOP_MENUSHOTS
#include "render/texture_registry.hpp" // coop-floorjoin2: the texture registry re-keyed after a snapshot apply
#include "texture.hpp"      // coop-joinatla: the texture log
#include "dataset.hpp"      // coop-joinatla: read_buffer
#include "platform/arena.hpp" // coop-joinatla: ArenaContains
#include "platform/clock.hpp" // coop-testspeed2: ClockSetUnbounded (DC_HEADLESS_PACED=alone)
#include "gamepad.hpp" // coop-snapshot: the trip's inputs line
#include "main.hpp"
#include "mainselect.hpp"
#include "effectmacro.hpp"
#include "menu_draw.hpp"
#include "btactstatus.hpp"
#include "mglib.hpp"
#include "rect.hpp"
#include "snd.hpp"
#include "coop/coop_audio.hpp" // coop-pt1109c: AudioSnapshotBefore, AudioSnapshotApplied
#include "objanime.hpp"
#include "textureanime.hpp"
#include "monstorunit.hpp"
#include "platform/config.hpp"
#include "platform/paths.hpp"
#include "save/save_slots.hpp"
#include "savedata.hpp"
#include "userstatus.hpp"

namespace dc::game {
unsigned int PortRandState();
void         PortRandSetState(unsigned int state);
void         PortEditLoop3ResetForResync();
void         CoopTownGeoramaToSave(); // editloop: the town's live Georama into the SaveData
void         CoopTownGeoramaPlotFromSave(int town, int plot); // editloop: one live plot from the SaveData
extern int   poison_counter; // the player's poison bite clock (BtStatusErrStep), counting every dungeon tick
extern int   EdMoveCharaRotCount; // coop-soakfix5: the town walk's camera swing behind Toan (ebattle.cpp EdMoveChara)
void         PortBuildMapGuardsClear();
} // namespace dc::game
void         PortWeaponFxSlotReset();
int          PortWeaponFxSlot(); // coop-kocatchup
namespace dc::game {
void         PortMessageCachesReset(); // gametext.cpp
bool         PortBuildMapGuardsPlaced();

extern s32        gameTask;
extern s32        mode;
extern s32        MapNo;
extern s32        existFlag;
extern s32        NextMapNo;
extern s32        OldMapNo;

void MapJump(int map_no, int event_no);
extern s32        LocalMapNo;
extern CFrameVu1 *CharaFrame;
extern bool       all_stop; // objanime.cpp: the towns' object animations stopped (a resync's state)
extern int        effect_count; // effectmacro.cpp: the effects' clock (a resync's state)
} // namespace dc::game

// WS5: coop_crashlog.cpp, the game thread sampled from the stall watch (DC_NETPLAY_TEST_STALL_WATCH).
void CoopStallAdopt();
void CoopStallSample(const char *where, bool inside, long long quiet_ms, long ticks, long long calls);

namespace dc::battlemenu {
bool BattleMenuQuitConfirmActive(); // (port/src/battlemenu/main_ring.cpp: a chooser of the ring is up)
} // namespace dc::battlemenu

namespace {

struct StallWatch {
    std::atomic<const char *> where{"none"};
    std::atomic<bool>         inside{false};
    std::atomic<long>         ticks{0};
    std::atomic<long long>    calls{0};
    std::atomic<bool>         replaying{false};
    double                    hook_ms = 0.0, hook_most = 0.0, solo_ms = 0.0, solo_most = 0.0, receive_ms = 0.0, receive_most = 0.0;
    double                    log_ms = 0.0, log_most = 0.0;
    long long                 log_pieces = 0, datagrams = 0, pass_most = 0;
    std::atomic<long long>    receive_from{0};
    std::uint32_t             magics[8] = {};
    long long                 magic_count[8] = {};
};
StallWatch g_stall_watch;

// coop-small1b: where the netplay tick's own time goes, part by part (a part runs from its TickPart to the next), for the
// long-frame line (coop_town's "a N ms wait between frames"; NetplayTickPartsSay). Only the game thread marks; a few clock
// reads a tick.
struct TickParts {
    const char                                  *at = nullptr;
    std::chrono::steady_clock::time_point        since{};
    std::vector<std::pair<const char *, double>> ms; // since the last NetplayTickPartsSay
};
TickParts g_tick_parts;

void TickPart(const char *name) {
    const auto now = std::chrono::steady_clock::now();

    if (g_tick_parts.at != nullptr) {
        const double spent = std::chrono::duration<double, std::milli>(now - g_tick_parts.since).count();
        bool         found = false;

        for (auto &part : g_tick_parts.ms) {
            if (part.first == g_tick_parts.at) {
                part.second += spent;
                found = true;
                break;
            }
        }
        if (!found) {
            g_tick_parts.ms.emplace_back(g_tick_parts.at, spent);
        }
    }
    g_tick_parts.at = name;
    g_tick_parts.since = now;
}

struct TickPartsScope {
    TickPartsScope() { TickPart("its start"); }
    ~TickPartsScope() { TickPart(nullptr); }
};

void StallWhere(const char *where) {
    g_stall_watch.where = where;
    TickPart(where);
}

// (the watch's counts in Receive only while the test switch is on: a player's copy does none of this)
bool StallWatchOn() {
    static const bool on = TestEnvInt("DC_NETPLAY_TEST_STALL_WATCH", 0) > 0;
    return on;
}

// The link's settings: config.json's game.netplay*, each replaced by its environment variable when
// set, so the online launchers (Play Dark Cloud Online (host|join).cmd) choose them without editing
// anyone's config.
struct LinkSettings {
    int         netplay = 0;
    std::string peer;
    int         port = 0;
    int         delay = 3;
    bool        controller_only = true;
    bool        join_menu = false; // DC_NETPLAY_JOIN_MENU: player 2 joins from the main menu (NetplayTitleJoin)
    bool        one_client = false; // game.netplay 3 (DC_NETPLAY=3): host or join, as the player picks (below)
    bool        keyboard = false;   // game.netplay_keyboard (DC_NETPLAY_KEYBOARD=1): the keyboard plays while linked
};

// One client (game.netplay 3, or DC_NETPLAY=3: the LAN pack's "Play Dark Cloud (LAN).cmd"): a copy that starts or
// loads a game is player 1, a host listening for anyone; one whose main menu's Join was picked, with an address
// typed there (NetplayTitleJoin), is player 2 at that address. 0 until one or the other.
int         g_role = 0;
std::string g_join_peer;
// The title's Multi-player > Host (NetplayTitleHost): 0 not picked (a game started or loaded hosts both ways, as before),
// 1 IP only (no Steam lobby; coop-upnp: the router asked to forward the port, netplay_upnp.hpp), 2 Steam, 3 Local (IP on
// this PC; player 2's copy started by this one, netplay_local.hpp).
int         g_host_via = 0;
// coop-lobby: this copy hosts a lobby (its own Start-on-a-controller starts the players' windows, so Local's spawn at the
// game's start stays out), and the lobby started a window for a player on this PC (this copy then keeps its own controller,
// as Local's player 1 does).
bool g_lobby_hosts = false;
// coop-lobby: the address entry's Square asked for JOIN > STEAM's list (the title menu opens it: NetplayTitleTakeSteamList).
bool g_entry_steam_list = false;
bool g_lobby_spawned = false;
int  g_lobby_pad = -1; // coop-lanpack7: the host's lobby's own controller (NetplayLobbyPinPad), or -1
// The lobby's start: its Multi screen seats on this PC, each to its own window once this copy hosts (NetplayLobbyHandOff).
struct HandOff {
    int           seat;
    int           host_pad;
    int           pad;
    std::uint64_t identity;
};
std::vector<HandOff> g_handoffs;
int         g_local_pad = 0; // Local: player 1's controller, the one pressed on the title when Local was picked
// One client (Jep, 2026-10-21): a copy started with no online setting (Play Dark Cloud.cmd) is single player until the
// title's Multi-player commits to a way (Host or Local Start/Continue, a Join); this run is then one client
// (DC_NETPLAY=3) with the online launchers' settings for the session only (MenuOnline), and Back undoes it.
bool        g_menu_online = false;
// NAT codes (Multi-player > Host or Join > NAT > Code; Jep, 2026-10-21): 1 hosting, 2 joining by a code; the host's
// friend's mapped address ("a.b.c.d:port") it punches towards until the link is up (NetSocketPunch).
int         g_nat = 0; // 1 hosting by code, 2 joining by code, 3 hosting by Steam, 4 joining by Steam
std::string g_nat_friend;
std::uint64_t g_nat_steam_host = 0; // joining by Steam: the host, for the code and the fallback
bool        g_nat_published = false; // hosting by Steam: this host's code is in its lobby
// NAT by Steam: the join's steps in ms since NAT > Steam was picked (-1 until reached), logged once linked
// (Jep, 2026-10-25: "it takes considerably longer to connect"), and whether Steam was shut down after the link.
struct NatSteamTimes {
    std::chrono::steady_clock::time_point picked{};
    long long                             lobby = -1, code = -1, linked = -1;
    bool                                  stopped = false, said = false;
    // coop-natsplit: a guest's join (its 15 s to the Steam relay counted from its start, its codes sent, whether it took
    // the relay), and whether this session was ever linked directly; a host's word that a split kept Steam down.
    std::chrono::steady_clock::time_point guest_start{};
    int                                   sent = 0;
    bool                                  relay = false, direct = false, apart_said = false;
};
NatSteamTimes g_nat_times;
constexpr int kNatLobbySearchMs = 20000; // coop-natsplit: NAT > Steam's join looks this long for a lobby, then NAT > Code
long long NatSince() {
    if (g_nat_times.picked == std::chrono::steady_clock::time_point{}) {
        g_nat_times.picked = std::chrono::steady_clock::now();
    }
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - g_nat_times.picked).count();
}
// coop-punchlog (Jep, 2026-10-28: did the punch get through, or did Steam carry the join, and by a Valve relay?): a NAT
// join's story on both copies, in the log and netplay-diag.jsonl ("event":"punch"): the punch's start (towards, from),
// its probes sent and datagrams heard every 3 s, the first heard, its result, the Steam leg's route, and one line with
// the path (direct-punch, direct-lan, steam-direct, steam-relay) and the join's time. Logging only (PunchLogTick).
struct PunchLog {
    bool                                  on = false, heard = false, result = false, late = false, summary = false, done = false;
    bool                                  lan = false; // linked to where the host's Hello came from on the LAN
    std::chrono::steady_clock::time_point at{}, next{}, linked{}, steam_next{};
    long long                             sent0 = 0, heard0 = 0, link_ms = -1;
    int                                   steam_looks = 0;
    std::string                           route; // the Steam leg's route last logged ("direct", "relayed by fra")
    // The Steam leg watched all session (Jep, 2026-10-28: does Steam drop the relay and go direct?): since when on the
    // route, seconds relayed and direct so far, route changes, and whether its end was logged (PunchLegEnd).
    std::chrono::steady_clock::time_point route_at{};
    double                                relayed_s = 0, direct_s = 0;
    int                                   changes = 0;
    bool                                  wan_said = false, ended = false;
    std::vector<std::string>              early; // lines before netplay-diag.jsonl was open
    bool                                  in_lobby = false; // coop-joinbudget: the lobby is up over the path (PunchLogTick)
    double                                lobby_s = 0;
};
PunchLog g_punch;

// coop-natonly (Jep, 2026-10-29: Steam is the NAT handoff only, it never carries play): Steam's own ways in (Host > Steam,
// Join > Steam, the address entry's Square, an invite taken) go by NAT > Steam: Steam finds the friend and the two swap
// codes, and play goes direct (or not at all). nat: 3 hosting, 4 joining. DC_NETPLAY_MUTATE=steam_plays (twin): play over
// Steam, as before.
bool SteamAsNat(int nat, const char *way) {
    if (NetplayMutated("steam_plays")) {
        return false;
    }
    const bool was = g_nat == nat;

    g_nat = nat;
    g_nat_times = NatSteamTimes{}; // (a search again: its 20 s from now)
    g_punch = PunchLog{};
    NatSince();
    if (!was) {
        std::fprintf(stderr, "netplay: %s goes by NAT > Steam: Steam hands the codes over, play goes direct\n", way);
    }
    return true;
}
void        MenuOnline(bool on);
// The tag a Steam lobby carries, so a guest joins only Dark Cloud co-op lobbies (the Hello then checks the build).
constexpr const char *kSteamTag = kNetSteamTag; // (coop-lobbysteam's, one tag)

LinkSettings Settings() {
    const dc::platform::Config &config = dc::platform::ConfigGet();
    LinkSettings  out{config.netplay, config.netplay_peer, config.netplay_port, config.netplay_delay, config.netplay_controller_only};

    out.netplay = TestEnvInt("DC_NETPLAY", out.netplay);
    if (out.netplay == 0 && (g_menu_online || TestEnvIs("DC_NETPLAY_MUTATE", "plain_online"))) {
        out.netplay = 3; // (plain_online: the test's twin, a plain copy always online)
    }
    if (const char *peer = TestEnv("DC_NETPLAY_PEER")) {
        out.peer = peer;
    }
    out.port = TestEnvInt("DC_NETPLAY_PORT", out.port);
    out.delay = TestEnvInt("DC_NETPLAY_DELAY", out.delay);
    out.join_menu = TestEnvOn("DC_NETPLAY_JOIN_MENU");
    out.keyboard = config.netplay_keyboard || TestEnvOn("DC_NETPLAY_KEYBOARD") || g_menu_online;

    if (out.netplay == 3) {
        out.one_client = true;
        out.netplay = g_role == 2 ? 2 : 1;
        out.peer = g_role == 2 ? g_join_peer : "any";
        out.join_menu = g_role != 1;
    }

    return out;
}

// Player 2's main-menu Join (NetplayTitleJoin): whether the launcher asked for it (or one client has not yet
// hosted), and whether it was picked.
bool JoinMenu() {
    const LinkSettings config = Settings();

    return (config.netplay == 2 || config.one_client) && config.join_menu;
}

bool g_join_picked = false;

// One client (one player on this PC): the controller last pressed, not always the first. Jep's host PC lists two
// "Xbox One Controller" pads (045e:02ff) with Steam running, the first of them not the one in his hands, so the first
// alone left his pad dead (2026-10-21). The keyboard folds into the first. Local only: the pad read is what the link sends.
// DC_NETPLAY_MUTATE=first_pad_only (the test's twin): always the first.
// Local's player 2 (DC_NETPLAY_LOCAL_CLAIM): the first controller, not player 1's, with a button pressed (or its stick
// pushed, as ActivePad takes it); -1 until one
// is (its input then reads released: OwnInput). Player 1's keyboard reaches player 1's window, which has the focus.
// DC_NETPLAY_MUTATE=local_port_pads (the test's twin): controller 2 by port, as before.
int PadNamed(const char *variable, int number);

int ClaimPad(int except) {
    static int claimed = -1;

    if (NetplayMutated("local_port_pads")) {
        return 1;
    }
    except = PadNamed("DC_NETPLAY_LOCAL_CLAIM_DEVICE", except); // (player 1's controller by its device)
    for (int i = 0; i < kInputPadCount && claimed < 0; i++) {
        InputPadState state = InputGetPad(i);

        bool          stick = std::abs((int) state.left_x - 128) > 60 || std::abs((int) state.left_y - 128) > 60;

        if (i != except && state.connected && (state.buttons != 0 || stick)) {
            claimed = i;
            std::fprintf(stderr, "netplay: local: player 2 claimed controller %d\n", i + 1);
        }
    }
    return claimed;
}

int ActivePad() {
    static int active = 0;

    if (NetplayMutated("first_pad_only")) {
        return 0;
    }

    for (int i = 0; i < kInputPadCount; i++) {
        InputPadState state = InputGetPad(i);
        bool          stick = std::abs((int) state.left_x - 128) > 60 || std::abs((int) state.left_y - 128) > 60;

        if (state.connected && (state.buttons != 0 || stick)) {
            if (i != active) {
                std::fprintf(stderr, "netplay: one client plays controller %d (it was pressed)\n", i + 1);
                active = i;
            }
            break;
        }
    }

    return active;
}

// Local's player 2's copy: the controller player 1's copy named by its device (DC_NETPLAY_LOCAL_PAD_DEVICE for the lobby's
// hand-off, DC_NETPLAY_LOCAL_CLAIM_DEVICE for the controller a claim passes over), as this copy numbers it; -1 not open
// here (yet). Each copy numbers the pads in the order it opened them, so two of one make ("Xbox One Controller" twice,
// Jep's PC) can swap numbers between the copies: his pack 7 playtest, 2026-11-02, P2's copy took "controller 2", which
// there was P1's pad, and P1's pad played both. (No twin: two pads of one make need real devices, so the untested
// pad_by_number mutation was dropped, train 50's audit.)
int PadNamed(const char *variable, int number) {
    static const char *const handed = TestEnv("DC_NETPLAY_LOCAL_PAD_DEVICE");
    static const char *const claim = TestEnv("DC_NETPLAY_LOCAL_CLAIM_DEVICE");
    const char *const        device = std::strcmp(variable, "DC_NETPLAY_LOCAL_PAD_DEVICE") == 0 ? handed : claim;

    if (device == nullptr || device[0] == '\0') {
        return number; // (no device name given: the copies agree on the numbers, or nothing better is known)
    }
    const int here = NetplayPadOfDevice(device);
    static int said[2] = {-2, -2};
    int       &last = said[device == handed ? 0 : 1];

    if (here != last) {
        last = here;
        std::fprintf(stderr, "netplay: local: %s %s is controller %d on this copy (player 1's copy numbered it %d)\n",
                     device == handed ? "the handed controller" : "player 1's controller", device, here + 1, number + 1);
    }
    return here;
}

// This copy's controller: player 1's copy reads controller 1, player 2's controller 2 (two copies on one PC),
// or the one DC_NETPLAY_PAD names (1 for the first: a joiner on its own PC). One client plays controller 1.
int OwnPad(int player) {
    static const int pad = TestEnvInt("DC_NETPLAY_PAD", 0) - 1;

    if (pad >= 0 && pad < kInputPadCount) {
        return pad;
    }

    // Local (two copies on this PC, netplay_local.hpp): player 1's copy keeps the controller pressed on its title
    // (g_local_pad); player 2's copy (DC_NETPLAY_LOCAL_CLAIM=player 1's) the first other one a button is pressed on.
    if (g_host_via == 3 || g_lobby_spawned) {
        return g_local_pad;
    }
    if (g_lobby_pad >= 0) {
        return g_lobby_pad;
    }
    // coop-lobby: a Multi screen seat's copy plays the controller its lobby seat had (handed over at the lobby's start; Jep,
    // 2026-11-01), from its first frame: no claim.
    if (static const int handed = TestEnvInt("DC_NETPLAY_LOCAL_PAD", -1); handed >= 0 && handed < kInputPadCount) {
        static bool said = false;

        if (!said) {
            said = true;
            std::fprintf(stderr, "netplay: local: player 2 claimed controller %d (handed over by the lobby)\n", handed + 1);
        }
        return PadNamed("DC_NETPLAY_LOCAL_PAD_DEVICE", handed); // (by its device: this copy's number for it)
    }
    static const int except = TestEnvInt("DC_NETPLAY_LOCAL_CLAIM", -1);
    if (except >= 0) {
        return ClaimPad(except);
    }

    return Settings().one_client ? ActivePad() : player - 1;
}

// coop-upnp: whether a host asks the router for its port by UPnP: hosting by Multi-player > Host > IP or NAT > Code, unless
// game.netplay_upnp is false (Jep, 2026-10-28: part of Host > IP, not a row of its own). DC_NETPLAY_UPNP=1 or 0 decides
// instead (the tests: 0 in every copy, so none asks the real router; 1 with the mock gateway).
bool UpnpOn() {
    if (TestEnv("DC_NETPLAY_UPNP") != nullptr) {
        return TestEnvInt("DC_NETPLAY_UPNP", 0) != 0;
    }
    return (g_host_via == 1 || g_host_via == 4) && dc::platform::ConfigGet().netplay_upnp;
}

// "Waiting for player N", with this PC's address when one client hosts (the other player types it to join).
void SetWaiting(char *status, std::size_t size, int player) {
    const char *here = NetSocketLocalAddress();

    int         wan_port = 0;
    const char *wan = NetSocketWan(wan_port);
    int         upnp_port = 0;
    const char *upnp = NetUpnpOutside(upnp_port);
    char        forward[96];
    // coop-consent (Jep, 2026-11-02): this line is on the HUD; the internet address only as the lobby's R1 left it
    // (game.lobby_show_ip). DC_CONSENT_MUTATE=ip_shown (the twin): shown, as before.
    const bool  ip_shown = dc::platform::ConfigGet().lobby_show_ip || TestEnvIs("DC_CONSENT_MUTATE", "ip_shown");

    // coop-upnp: how the port is open: by UPnP, already forwarded, or (UPnP failed or off) by hand.
    if (NetUpnpState() == kUpnpFailed) {
        std::snprintf(forward, sizeof forward, "%s: forward UDP %d by hand, if not done", NetUpnpWhy(), Settings().port + 1);
    } else {
        std::snprintf(forward, sizeof forward, "forward UDP %d", Settings().port + 1);
    }
    if (player == 1 && upnp[0] != '\0' && NetUpnpPublic()) {
        // coop-upnp: the router forwards the port, so the friend joins at its outside address and port as they are.
        std::snprintf(status, size, "Waiting for player 2 - internet %s (%s) - LAN %s%s", ip_shown ? (std::string(upnp) + ":" + std::to_string(upnp_port)).c_str() : "IP hidden",
                      NetUpnpState() == kUpnpForwarded ? "port already forwarded" : "UPnP opened it", here[0] != '\0' ? here : "none",
                      NetSteamSelf() != 0 ? " - or a Steam friend" : "");
    } else if (Settings().one_client && player == 1 && wan[0] != '\0' && !NetSocketWanLikelyCgnat()) {
        // The internet address first (a friend outside this network needs it, and the router must forward the port),
        // then the LAN one, then Steam.
        std::snprintf(status, size, "Waiting for player 2 - internet %s (%s) - LAN %s%s", ip_shown ? wan : "IP hidden", forward,
                      here[0] != '\0' ? here : "none", NetSteamSelf() != 0 ? " - or a Steam friend" : "");
    } else if (Settings().one_client && player == 1 && wan[0] != '\0') {
        std::snprintf(status, size, "Waiting for player 2 - internet: strict NAT, use a Steam friend - LAN %s", here[0] != '\0' ? here : "none");
    } else if (Settings().one_client && player == 1 && here[0] != '\0') {
        std::snprintf(status, size, "Waiting for player 2: join at %s%s", here, NetSteamSelf() != 0 ? " or as a Steam friend" : "");
    } else if (player == 1 && NetUpnpState() == kUpnpFailed) { // coop-upnp
        std::snprintf(status, size, "Waiting for player 2 (%s)", forward);
    } else {
        std::snprintf(status, size, "Waiting for player %d", 3 - player);
    }
}

constexpr std::uint32_t kMagic = 0x44434F51;       // "QOCD": the tick packet
constexpr std::uint32_t kChunkMagic = 0x44435253;  // "SRCD": a piece of the host's resync state
constexpr std::uint32_t kAckMagic = 0x44435241;    // "ARCD": the pieces the joiner holds
constexpr std::uint32_t kBusyMagic = 0x44434255;   // "UBCD": this copy is loading, not gone
constexpr std::uint32_t kLeaveMagic = 0x4443564C;  // "LVCD": this copy is quitting (a Busy's layout)
constexpr std::uint32_t kLeaveGaveUp = 2;          // coop-catchupdrop: a Leave's word: this copy gave the link up mid catch-up
constexpr std::uint32_t kLeaveEnded = 3;           // coop-quittitle: a Leave's word: the host quit, the session is over for all
constexpr std::uint32_t kNone = 0xFFFFFFFFu;
constexpr int           kRing = 128;
constexpr int           kResend = 8;
constexpr int           kChecksumTicks = 60;

// CSaveData's config (s32[18], first) and play_time (after map_no at 0x1C8, its word, now_time).
constexpr std::size_t kSaveConfigOffset = 0;
constexpr std::size_t kSaveConfigBytes = 18 * 4;
constexpr std::size_t kSavePlayTimeOffset = 0x1D4;
// coop-menucursor: CSaveData's menu_cursor (CMenuCursor, 0x2C bytes after unk_5E0), where each menu page keeps its cursor
// (ExitItemSelect on the Items page's Back). Since local menus run on their player's copy alone, it is that copy's own, as
// the config is: Jep's 11-01 Back from his bag parted the copies at its pos[0] (4 on his, 5 on hers).
constexpr std::size_t kSaveMenuCursorOffset = 0x650;
constexpr std::size_t kSaveMenuCursorBytes = 0x2C;

// Blanks the parts of a SaveData copy that each copy keeps for itself (config, play time, menu cursor).
// DC_NETPLAY_MUTATE=save_sums_cursor (twin): the menu cursor summed as before.
void BlankOwnSaveParts(unsigned char *save) {
    std::memset(save + kSaveConfigOffset, 0, kSaveConfigBytes);
    std::memset(save + kSavePlayTimeOffset, 0, 4);
    if (!NetplayMutated("save_sums_cursor")) {
        std::memset(save + kSaveMenuCursorOffset, 0, kSaveMenuCursorBytes);
    }
}

// The first tick waits a minute for the other copy to start and load; later ones give up after five
// seconds and leave the link, each copy then playing on alone.
constexpr int kFirstWaitMs = 60000;

// The first wait, or DC_NETPLAY_FIRST_WAIT_MS (tests that would rather not wait a minute).
int FirstWaitMs() {
    static const int ms = TestEnvInt("DC_NETPLAY_FIRST_WAIT_MS", kFirstWaitMs);

    return ms;
}
constexpr int kWaitMs = 5000;

// A copy whose ticks stop for kBusyAfterMs (a slow load: a town, a floor, a save) says so every
// kBusyEveryMs from a thread of its own (Keepalive), and the other copy then waits for it up to kLoadWaitMs
// instead of kWaitMs.
constexpr int kBusyAfterMs = 1000;
constexpr int kBusyEveryMs = 500;
constexpr int kLoadWaitMs = 120000;

// A resync starts this many ticks (beyond the input delay) after the host sees the copies part, so
// the joiner hears of it first; its state goes over in pieces of kChunkBytes, at most kChunks.
constexpr int kResyncLeadTicks = 120;
constexpr int kChunkBytes = 1024;
constexpr int kChunks = 2048; // 2 MB: the SaveData, the host's save slots (about 80 KB each), the bag
constexpr int kResyncWaitMs = 10000;
// coop-mergetime: at most a window of pieces go unacknowledged, and a piece goes again only once its acknowledgement
// is overdue (the round trip and
// a margin). Every missing piece every 30 ms, as before, put hundreds of datagrams on the wire at once: past a
// 64 KB receive buffer (the other copy's, a relay's) most were lost, and the next 30 ms sent the same ones again
// before any acknowledgement could come back. DC_NETPLAY_MUTATE=no_retransmit: a piece never goes again, from the second
// blob on (the first link's own resync goes over as usual, so the twin fails on the merge's);
// DC_NETPLAY_MUTATE=burst_pieces: the old way (the A/B run).
// The window starts at kPieceWindowStart, grows by one for each piece acknowledged (it doubles each round trip while
// the link keeps up) up to kChunks, and halves (once a round trip) when a piece's acknowledgement is overdue: the link
// is full. A fixed pace of 2 pieces a ms (2 MB/s, train 2) was far too slow for a catch-up's 2 MB snapshot parts
// (i-bag-screen -Case p2: 2 of 9 parts before the joiner's frames ran out); DC_NETPLAY_MUTATE=fixed_pace keeps it
// (that test's twin).
constexpr int kPieceWindowStart = 32;
// coop-joinbudget (Jep, 2026-11-03: her join at 100 ms of lag took 3.4 s against 1.0 s with none): a link's first blob starts at
// kPieceWindowFirst, its window's floor (a halving's) stays kPieceWindowStart. From 32 the checkpoint's 79 pieces took two round
// trips and the first 2 MB part five more of doubling (1.1 s of the 3.4); 128 KB at once is half a home router's queue (the
// 2 MB/s relay's 256 KB, mp-direct-join-bottleneck). DC_NETPLAY_MUTATE=window_start_small (twin): 32, as before.
constexpr int kPieceWindowFirst = 128;
int PieceWindowFirst() {
    static const bool small = NetplayMutated("window_start_small");
    return small ? kPieceWindowStart : kPieceWindowFirst;
}
// coop-steamjoin: over Steam the pieces wait in Steam's own send queue, paced at its send rate, before they reach the wire,
// and the window above never sees that wait. Jep's 2026-10-27 Steam join: Steam paced at 256 KB/s, the window let up to
// 105 pieces out a round trip (400 ms of Steam's queue), each went again once 110 ms passed without its acknowledgement
// while it still sat in the queue, and two in three pieces sent were again (2 MB parts in 27 s, 8 MB in 2 minutes behind
// the splash). So over Steam the host hands it pieces only while its queue holds under kSteamQueueMs of sending, and a
// piece is overdue only that much later. DC_NETPLAY_MUTATE=steam_queue_blind (the twin): the queue is not looked at.
constexpr int kSteamQueueMs = 40;
constexpr int kFixedPerMs = 2;  // fixed_pace
constexpr int kFixedBurst = 32;
constexpr int kFixedWindow = 128;

// Round-trip samples that crossed a stall are not the network's: a packet that waited in the socket
// while this copy loaded (a resync, a town, a floor), or one the other copy sent from before its own
// load, comes back seconds late. A gap of kRttStallMs between two reads of the socket on this copy, or
// between two of the other copy's sends, starts kRttQuietMs in which samples are not taken.
constexpr int kRttStallMs = 250;
constexpr int kRttQuietMs = 500;

// WS5 leftovers (coop-ws5left, 2026-11-07): one pass of Receive reads the socket for kReceivePassMs at most, then gives
// the tick back (the rest is read on the next pass). A pass read until the socket was empty: while the other copy sent
// faster than this one could take each datagram, the pass never ended, so the pads it had already taken were never
// looked at and the tick waited on them, while the other copy, waiting on this one's pads, resent every 20 ms. Seen once
// (WS5's frame watch, l-local-play-houseout under a 12-core hog): his copy 18.5 s inside one pass taking her endless
// resent catch-up pieces (fixed in 3c3ab464), each "a piece dropped" line some 45 ms there; both copies waited 18.5 s.
constexpr int kReceivePassMs = 50;

#pragma pack(push, 1)
struct Entry {
    std::uint32_t frame;
    std::uint16_t buttons;
    std::uint8_t  lx, ly, rx, ry;
    NetCommand    command; // coop-localmenu: the player's command on this frame (kind 0: none)
};

struct Packet {
    std::uint32_t magic;
    std::uint32_t player;
    std::uint32_t session;
    std::uint32_t checksum_frame;
    std::uint32_t checksum;
    std::uint32_t resync_tick; // the host's: the tick both copies take its state at, or kNone
    std::uint32_t send_ms;     // this copy's clock as it sent, and the other's last as it came,
    std::uint32_t echo_ms;     // with how long ago it came: the round trip, for the diagnostics
    std::uint16_t echo_hold_ms;
    Entry         entries[kResend];
};

struct Chunk {
    std::uint32_t magic;
    std::uint32_t player;
    std::uint32_t session;
    std::uint32_t tick;
    std::uint32_t total;
    std::uint16_t index;
    std::uint16_t count;
    std::uint8_t  data[kChunkBytes];
};

// coop-steamrate: over Steam the host paces the pieces itself, by what reaches the joiner, with Steam's send rate (40 MB/s)
// as the ceiling only. Steam's rate is a fixed pace, not a limit (SendRateMin = SendRateMax, no bandwidth estimate), so at
// 40 MB/s it pushed each burst into a home upload of 1 to 5 MB/s and the router dropped the rest; the window halving on the
// losses was the only brake. A delivery-rate pacer, BBR-like: once a round trip (at least kPaceSampleMs) the bytes the
// joiner newly holds give the delivery rate; while filling, the pace is twice the best of the last kPaceSamples, then
// 1.25 times it (probing); a round with more than 2% of its pieces sent again backs it off to that round's delivery.
// Filling ends on such a round or after three rounds that did not grow the delivery by a quarter. The pace starts at
// kPaceStart (the 1 MB/s that joined cleanly over Steam, coop-steamjoin) and a catch-up's blobs keep it, as the window.
// game.netplay_steam_rate a number: a fixed rate, no pacer (Steam's pace alone). DC_NETPLAY_MUTATE=steam_pace_off (twin):
// no pacer, the 40 MB/s ceiling the pace.
constexpr int kPaceStart = 1024 * 1024;
constexpr int kPaceMin = 64 * 1024;
constexpr int kPaceSampleMs = 50;
constexpr int kPaceSamples = 4;
constexpr int kPaceBurstPieces = 32;
// coop-pacedirect: the pacer on the direct path too (UDP: join by address, a NAT code, Tailscale, the LAN, this PC). Jep's
// 2026-10-28c NAT-code joins (direct, round trip 33 ms, both lines 1 Gbps) moved 2 MB parts at 2-3 MB/s with a fifth of the
// pieces lost and a fifth sent twice. On one PC (mp-direct-join) the window alone moves 8 MB in 0.3 s with no relay and in
// 0.6 s through a 16 ms relay, nothing lost: on a path that takes its bursts it is not the limit (each 2 MB part goes in a
// round trip and a frame or two, some 30 MB/s at 33 ms). Through a 2 MB/s relay with a 256 KB queue (a home upload) it sent
// half its pieces twice: each frame's whole-window burst overflowed the queue, the acknowledgements that queued behind it came
// after the overdue time, and the window halved and grew back, round after round. So over UDP the window sends until a round
// loses pieces; from that round the pacer (by delivery samples, kDirectGains) holds the pieces back instead, at most
// kDirectBurstMs of its pace in one frame's call (8 MB in 4.3 s through that relay, 3.9 s its floor, 8.5% sent twice). Its
// ceiling, kDirectCeiling, is a gigabit line: no home path is faster. DC_NETPLAY_MUTATE=direct_pace_off (twin): the window
// alone, as before.
constexpr int kDirectCeiling = 125 * 1024 * 1024;
constexpr int kDirectBurstMs = 17;
// Once paced, the pace over the best delivery round by round (BBR's cycle): a round a quarter over it to find more room,
// one a quarter under to empty the queue that made, six at it. A quarter over every round (Steam's) kept a 2 MB/s path's
// queue full and lost an eighth of the pieces sent.
constexpr double kDirectGains[8] = {1.25, 0.75, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0};

struct PiecePacer {
    int                                   pace = 0; // bytes a second (0: not started)
    double                                tokens = 0.0;
    std::chrono::steady_clock::time_point token_at{};
    std::chrono::steady_clock::time_point sample_at{};
    std::uint32_t                         sample_held = 0, sample_sends = 0, sample_again = 0, sample_tail_again = 0;
    int                                   best[kPaceSamples] = {};
    int                                   best_next = 0;
    bool                                  filling = true;
    int                                   fill_best = 0;
    int                                   fill_flat = 0;
    int                                   pace_peak = 0, delivered_peak = 0; // the log's
    int                                   overdue_ms = -1; // coop-fx51b: the overdue bound its last direct send went by (the log's)
    std::uint32_t                         backoffs = 0;
    bool                                  direct = false; // coop-pacedirect: paced over UDP
    int                                   round_max = 0;  // and the round's best delivery sample (SendPieces)
    std::chrono::steady_clock::time_point cut_at{};       // and its last back-off
    bool                                  ran_out = false; // a call in the round had pace to spare and nothing to send
    int                                   cycle = 0;       // the round's place in kDirectGains
    bool                                  silent_round = false; // coop-mpdjflake: the receiver went silent this round (no loss signal)
    double                                ack_ms = -1.0;        // coop-pacerdark: the acknowledgement latency seen (first sends, averaged)
    double                                ack_dev_ms = 0.0;     // and how far single ones strayed from it (averaged, as a round-trip timer's deviation)
};

// The host's pieces of one blob in flight (a resync's state or a catch-up blob): when each went last, the pace's
// last send, the first send, how many went and how many of those again.
struct PieceSender {
    std::chrono::steady_clock::time_point sent_at[kChunks]{};
    std::chrono::steady_clock::time_point paced{};
    std::chrono::steady_clock::time_point began{};
    std::uint32_t                         sends = 0;
    std::uint32_t                         again = 0;
    std::uint32_t                         tail_again = 0; // coop-fx51b: of them, a part's lost tail (no piece sent after it held)
    std::uint32_t                         tail_seen = 0;  // and the same pieces whatever the twin (its first-loss rule's count)
    std::uint32_t                         rounds = 0; // coop-replayspeed: the calls that sent any
    int                                   window = 0; // the pieces that may go unacknowledged (0: PieceWindowFirst, at the first send)
    std::uint32_t                         held_seen = 0;              // held at the last call
    std::chrono::steady_clock::time_point cut_at{};                   // the window's last halving
    int                                   queue_rate = 0;             // coop-steamjoin: Steam's send rate (0: UDP)
    int                                   queue_peak = 0;             // and the most its queue held
    std::uint32_t                         refused = 0;                // coop-steamrate: sends Steam refused (its buffer full)
    PiecePacer                            pacer;                      // coop-steamrate: over Steam, the pace by arrivals
    std::uint16_t                         held_at_send[kChunks]{};    // coop-pacedirect: the pieces held as each went
    std::chrono::steady_clock::time_point held_since[kChunks]{};      // and since when that many were
    std::chrono::steady_clock::time_point held_at{};                  // when the last newly held piece was seen
    std::chrono::steady_clock::time_point probe_at{};                 // coop-mpdjflake: the last probe piece into a silent receiver
    std::uint32_t                         sampled[kChunks / 32]{};    // its acknowledgement sampled
    std::uint32_t                         resent[kChunks / 32]{};     // it went more than once (no sample)
    std::size_t                           bytes = 0;                  // coop-steamrate: the blob's size (the log's rate)
    std::chrono::steady_clock::time_point acked_sent{};               // coop-replayload: the newest send of a piece held (sent once)
    std::chrono::steady_clock::time_point resent_held_sent{};         // and the newest send of a piece sent again held
    std::chrono::steady_clock::time_point resent_held_at{};           // and when it was seen held
    std::chrono::steady_clock::time_point resent_held_first{};        // coop-joinbudget: when the first such was held, while pieces are late
    std::uint32_t                         sends_here = 0;             // coop-offthread: pieces sent on the game thread
    std::uint32_t                         sends_off = 0;              // and handed to the send thread (PieceSends)
    double                                call_ms_max = 0.0;          // the game thread's longest SendPieces call
};

// coop-offthread (shared-state review 2026-11-09, section 4; option 2): over UDP each piece was one sendto on the game thread,
// a 2 MB part 2048 of them: the stayer's frames waited 112-276 ms a tick on them while her state set went (catch-up host in
// the merge's "wait between frames" lines). SendPieces still decides on the game thread what goes (the window, the pace,
// the acknowledgements, as before); the pieces it picks go to this thread, which sends them in order. While it still sends
// one call's pieces, the next call picks none (before, the game thread itself was busy that long), so the pace is unchanged.
// Over Steam the sends stay on the game thread: Steam's send only queues (no system call), and a refused send must be known
// at once (coop-steamrate). DC_NETPLAY_MUTATE=send_on_tick (twin): every piece sent on the game thread, as before.
// coop-cand58 (train 57's landing: mp-nat-lobbywait's seed copy ended 0xC0000005 at its --frames exit, before the exit's Leave
// line, no dump; not reproduced in 20 tries): netplay's own threads ran on, detached, through the game's teardown (the renderer,
// the audio, the window), the atexit handlers and the static destructors, touching what they tore down. NetplayStopThreads (main,
// as RunGame returns; an atexit fallback) stops and joins each of them first, in one order. DC_NETPLAY_MUTATE=exit_threads_run
// (twin): they run on, as before. DC_NETPLAY_TEST_EXIT_HOLD_MS (tests): the exit waits that long after it (the race window held
// open: Keepalive's Busy is due 1 s after the last tick), and the atexit says how many sends those threads made after it.
std::atomic<bool> g_exit_stop{false}; // the threads end their loops
std::atomic<bool> g_exit_past{false}; // NetplayStopThreads ran (the count runs in the twin too)
std::atomic<int>  g_exit_sends{0};    // sends from netplay's threads after it
std::atomic<int>  g_snap_threads{0};  // SnapJob threads still packing

void ExitCountSend() {
    if (g_exit_past.load()) {
        g_exit_sends++;
    }
}

class PieceSendThread {
  public:
    bool Busy() const { return pending_.load() != 0; }
    void Push(std::vector<Chunk> &&chunks) {
        if (chunks.empty()) {
            return;
        }
        {
            std::lock_guard<std::mutex> hold(lock_);

            if (!started_) {
                started_ = true;
                thread_ = std::thread([this] { Run(); }); // (coop-cand58: joined by Stop as the game ends)
            }
            jobs_.push_back(std::move(chunks));
            pending_++;
        }
        wake_.notify_one();
    }

    // coop-cand58: the thread ends (its unsent pieces dropped: the game is ending, its Leave tells the other) and is joined.
    // True when it ran.
    bool Stop() {
        {
            std::lock_guard<std::mutex> hold(lock_);

            if (!started_) {
                return false;
            }
            stop_ = true;
        }
        wake_.notify_all();
        if (thread_.joinable()) {
            thread_.join();
        }
        return true;
    }

  private:
    void Run() {
        for (;;) {
            std::vector<Chunk> chunks;
            {
                std::unique_lock<std::mutex> hold(lock_);

                wake_.wait(hold, [this] { return !jobs_.empty() || stop_; });
                if (stop_) {
                    return;
                }
                chunks = std::move(jobs_.front());
                jobs_.pop_front();
            }
            for (const Chunk &chunk : chunks) {
                ExitCountSend();
                NetSocketSend(&chunk, sizeof chunk); // (UDP: always true; Steam's sends never come here)
            }
            pending_--;
        }
    }
    std::mutex                     lock_;
    std::condition_variable        wake_;
    std::deque<std::vector<Chunk>> jobs_;
    std::atomic<int>               pending_{0};
    bool                           started_ = false;
    bool                           stop_ = false; // coop-cand58
    std::thread                    thread_;
};

// (never destroyed: its thread may wait on it as the process ends)
PieceSendThread &PieceSends() {
    static auto *sends = new PieceSendThread();
    return *sends;
}

struct Busy {
    std::uint32_t magic;
    std::uint32_t player;
    std::uint32_t session;
    std::uint32_t unused;
};

struct Ack {
    std::uint32_t magic;
    std::uint32_t player;
    std::uint32_t session;
    std::uint32_t tick;
    std::uint32_t held[kChunks / 32];
};

// A follow camera's state, as a resync carries it.
struct CameraState {
    float        pos[4]; // the eye and the point it looks at, which a script's camera starts from
    float        ref[4];
    float        follow[4];
    float        distance;
    float        height;
    float        next_angle;
    float        angle;
    std::int32_t follow_on;
};
// What the host sends ahead of its SaveData (which holds the party status) when the copies resync.
struct ResyncHeader {
    std::uint32_t rand;
    std::int32_t  mode;
    std::int32_t  map_no;
    std::int32_t  local_map_no;
    std::int32_t  select_map_no;
    std::int32_t  partner_choice;
    std::int32_t  debug_mode; // a joiner started with a jump has the game's debug mode on
    std::int32_t  game_clear;
    // The dungeon's two follow cameras (MainCamera__4 for play, SubCamera for scripts such as the floor
    // select), which outlive the dungeon's restart, and which of them is the current one.
    CameraState   cameras[2];
    BT_EVENT_INFO event; // the event script state (where the floor's opening plays), pointers aside
    std::int32_t  sub_camera;
    // Where the host came from and which town's map it shows, which a town's start reads (arrival,
    // villagers), so the joiner arrives as the host did.
    std::int32_t  menu_no;
    char          town[64];
    // The view and clip matrices the host's last draw left, which the first tick after the jump reads
    // (MGClipBox: a town's water and ground roll rand only when in view); each copy would keep its own.
    // As bytes: the header is packed, and a RenderInfo member there sits off the 16-byte boundary its constructor and
    // copies assume (SSE stores), which crashed as soon as the header landed elsewhere on the stack (catchup's checkpoint).
    unsigned char render[sizeof(RenderInfo)];
    std::int32_t  has_render; // 0 under the own_view mutation (set on the host): each copy keeps its own
    // What a town event leaves stopped (object, character and texture animations, the camera), which
    // outlives the mode a resync cuts off: a copy that comes from a dungeon would arrive with them as
    // that left them, and a stopped object animation rolls no rand.
    std::int32_t  stops[4];
    std::int32_t  has_stops; // 0 under the own_stops mutation (set on the host)
    // The tick the host loaded save 0 at (DC_NETPLAY_LOAD_AT), -1 for a resync that loads nothing.
    std::int32_t  loaded_tick;
    // Option B's checkpoint, taken as the host's own mode ended: where that mode was going (NextMapNo, StartEventNo),
    // which a copy taking the state then goes to as well (the mode, the map numbers as the main loop then reads them,
    // and leave_map, the map it leaves its own mode for). has_next 0 for a resync.
    std::int32_t  has_next;
    std::int32_t  next_map_no;
    std::int32_t  start_event_no;
    std::int32_t  next_mode;
    std::int32_t  old_map_no;
    std::int32_t  leave_map;
    // coop-spawn: where player 2 is to stand after a town merge (the host's SpawnStand), so both copies keep her there.
    std::int32_t  stand_set;
    std::int32_t  stand_map;
    std::int32_t  stand_arriver;
    float         stand_pos[3];
    float         stand_yaw;
    // coop-mergestand: where player 1 (Toan) stood on the host at the resync tick, in a town outdoors, so a town resync
    // puts both copies' Toan back there instead of the town's arrival (0 for a checkpoint, a dungeon, indoors, or the
    // spawn_no_stands twin). Her stand above is filled from the host's partner too when no merge set one (a desync's resync).
    std::int32_t  p1_set;
    float         p1_pos[3];
    float         p1_yaw;
    // coop-snapshot (the floor-start checkpoint): the floor (0 up) this checkpoint was taken at the start of, as the
    // gate's script put the party on it ("go dungeon", NetplayFloorStarted). A copy taking the state goes to the
    // dungeon's entrance (leave_map) and from there straight onto this floor (GameTakeJumpFloor), and its catch-up's
    // tick 0 is that floor's start. -1: a mode end's checkpoint, or a resync.
    std::int32_t  floor;
    // coop-floorsnap: the random state the host built that floor from (NetplayFloorBuilding, the script's _SET_RANDOM_MAP).
    // The floor's layout, its monsters and its chests all roll from there, and the host builds the floor before this
    // checkpoint is taken, so a copy taking the state sets this state as it builds the floor, whatever its own way to the
    // dungeon rolled (a joiner from the title or a town: n-debug-start-floor, q-p2-quickslot-p1menu). 0 with floor -1.
    std::uint32_t floor_rand;
    // coop-saveload: 1 when player 1 loaded a save from his menu (NetplayMenuLoadResync): this resync is the load's.
    std::int32_t  menu_load;
    // coop-housemerge (the building-start checkpoint): the building (its InteriorId) the host had just gone into alone as
    // it took this checkpoint (NetplayInteriorStarted), and the door's arguments it went in by (GotoInterior's, and the
    // jump id its door set). A copy taking the state goes to the town (leave_map), in by that door on its first walk
    // (NetplayCatchupEnterBuilding), and its catch-up's tick 0 is the interior's start. 0 for any other.
    std::int32_t  building;
    char          building_name[32];
    std::int32_t  building_entrance;
    std::int32_t  building_jump;
    std::int32_t  building_start_event;
    float         building_time; // the town's clock (NowTime, which each town frame writes to the save's now_time)
    // coop-stable1: why the host took this state (the resync's cause, ResyncCause), so the copy taking it logs the same
    // cause in its "zone:" line (ZoneLeaveLog). "checkpoint" for a checkpoint (a join's or a replay's catch-up).
    char          cause[160];
    // coop-carried: where the resync's carried parts begin in the state (CarriedTake), 0 when there are none.
    std::uint32_t carried_at;
};
#pragma pack(pop)

struct Slot {
    std::int64_t  frame = -1;
    InputPadState pad;
    NetCommand    command; // coop-localmenu
};

struct Link {
    bool          started = false;
    bool          up = false;
    std::chrono::steady_clock::time_point peer_busy_at{}; // the other copy's last Busy
    std::chrono::steady_clock::time_point peer_linking_at{}; // coop-worlds: the other copy's last link datagram heard while listening
    std::uint32_t peer_linking_session = 0; // coop-linkrace: ... and the session it came under
    bool          link_heard = false;       // coop-linkrace: a datagram of the other copy's link came while this one was up
    bool          peer_left = false; // the other copy said it is quitting
    bool          peer_left_done = false; // ... on its --frames budget
    bool          peer_gave_up = false;   // coop-catchupdrop: ... as it gave the link up during a catch-up (kLeaveGaveUp)
    bool          peer_ended = false;     // coop-quittitle: ... as the host, whose Quit Game ended the session (kLeaveEnded)
    std::uint32_t told_session = 0;       // coop-catchupdrop: this copy's session a Leave last went under (SayLeaving)
    std::int64_t  peer_apart_at = -1;     // ... as the copies went apart, on this tick (its own split's): this copy plays to it
    bool          apart_heard = false;    // the joiner logged that Leave (once)
    std::int64_t  loaded_tick = -1;  // the host loaded save 0 on this tick (DC_NETPLAY_LOAD_AT); its next resync says so
    bool          menu_load = false; // coop-saveload: player 1 loaded a save from his menu; the next resync is its
    int           player = 0;
    // coop-localfix2: which player's copy sends its state for this link's catch-up (the source), the other catching up: 1,
    // the host, but the copy that stayed in a town for a merge there (CoopWorldsSendsMerge, both Hellos). Desync resyncs,
    // saves, settings and the input delay stay the host's.
    int           source = 1;
    int           delay = 3;
    std::int64_t  tick = 0;
    bool          stayer_merge = false; // coop-localfix2: this link is her merge (source 2), until the copies are together
    bool          floor_arrival = false; // coop-dungeonjoin: this copy arrived on the other's floor; it holds black to the lockstep
    bool          arriving = false;     // coop-houseexit: this copy arrived where the other stayed and catches up, until the lockstep
    int           arrival_walk = 0;     // coop-arrivalfull: this link is a floor join: the arriver (the one taking the state) whose
                                        // arrival both copies play at the lockstep (coop::ArrivalWalkStart), 0 none
    Slot          local[kRing];
    Slot          remote[kRing];
    InputPadState synced[2];
    InputPadState previous[2];
    InputPadState routed[2];
    // Who steers the menus (port 1): 1 or 2; see netplay.hpp. menu_tick is the tick player 2 took them,
    // menu_seen whether the game has left free play since, held the buttons the new owner of port 1
    // still held as it changed hands, which port 1 keeps released until they are let go.
    int           menu_owner = 1;
    std::int64_t  menu_tick = -1;
    bool          menu_seen = false;
    std::uint16_t held = 0;
    std::int64_t  sums_frame[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
    std::uint32_t sums[8] = {};
    std::int64_t  last_checksum_frame = -1;
    std::uint32_t last_checksum = 0;
    std::int64_t  desync_tick = -1;
    std::int64_t  checked = 0;
    char          status[160] = "";

    // The handshake: the other copy's Hello once it has come, and whether its tick packets have
    // (it has finished its handshake, so ours need not be sent again).
    NetplayHello peer_hello{};
    bool         peer_hello_got = false;
    bool         peer_ticking = false;

    // Sessions: this copy's (new for each link), whether it plays alone listening for the other copy
    // (after the first minute, or after the link went down), the other copy's session it last linked
    // with, whether the link was refused for good (another build), and whether the copies await the
    // host's state before their checksums mean anything.
    std::uint32_t session = 0;
    bool          listening = false;
    std::uint32_t last_peer_session = 0;
    bool          dead = false;
    bool          awaiting_sync = false;
    std::chrono::steady_clock::time_point hello_due;
    NetplayHello                          my_hello{}; // the Hello this copy linked with, sent again
    NetplayHello                          sent_hello{}; // coop-linkdir: the last Hello sent while listening (HeardHello)
    bool                                  sent_hello_set = false;
    // While listening alone: what came from the other player (datagrams, Hellos) since the last report, logged
    // every 10 s, so a join that never links says which side heard nothing.
    long long                             heard_datagrams = 0;
    long long                             heard_hellos = 0;
    std::chrono::steady_clock::time_point heard_report;

    // Diagnostics (netplay-diag.jsonl): the clock they run on, the other copy's last send time and
    // when it came, the round trip, ticks that waited for the other copy and for how long, packets
    // sent again while waiting, and where the next line is due.
    std::chrono::steady_clock::time_point epoch;
    std::uint32_t                         peer_send_ms = 0;
    std::chrono::steady_clock::time_point peer_send_at;
    bool                                  peer_send_got = false;
    double                                rtt_ms = -1.0;
    double                                rtt_avg_ms = -1.0;
    std::chrono::steady_clock::time_point last_receive_at{}; // this copy's last read of the socket
    std::chrono::steady_clock::time_point rtt_quiet_until{}; // no round-trip samples before this (a stall)
    long long                             stalls = 0;
    double                                stall_ms = 0.0;
    double                                stall_max_ms = 0.0;
    long long                             resends = 0;
    long long                             received = 0;
    std::chrono::steady_clock::time_point diag_due;
    std::FILE                            *diag = nullptr;

    // Desync recovery: the tick the copies take the host's state at (-1 none due), the last one they
    // did (checksums from before it are not compared), the state as it travels, which pieces the
    // joiner holds, the map both copies then go to (-1 none), and how many resyncs there have been.
    std::int64_t               resync_tick = -1;
    std::int64_t               resynced_at = -1;
    std::int64_t               joined_at = -1; // coop-pilotfix: a catch-up's first lockstep tick J (its sum is not compared)
    std::chrono::steady_clock::time_point resynced_clock{}; // coop-mergetime: when the last resync's state was held
    std::vector<unsigned char> resync_state;
    std::uint32_t              resync_held[kChunks / 32] = {};
    std::int64_t               restart_map = -1;
    int                        restart_floor = -1; // coop-snapshot: a floor-start checkpoint's floor, for GameTakeJumpFloor (once)
    int                        resync_target = -1; // the map the last resync sent both copies to
    bool                       state_due = false;  // TakeState waits for the mode to end
    // coop-stable1: the unified zone-load log ("zone:" lines, both copies). resync_cause: why the host set resync_tick
    // (ResyncCause: the code path that asked, in words, and where); it travels in ResyncHeader::cause. leave_cause and
    // leave_site: what LeaveForState logged, for the mode's end to name (ZoneModeEnded), cleared there.
    char                       resync_cause[160] = "";
    char                       leave_cause[160] = "";
    char                       leave_site[48] = "";
    bool                       leave_reset = false;
    // coop-stable1: after a resync both copies hold the lockstep through their loads (NetplayTick returns without a tick until
    // the new mode's first frame, NetplayFrameStart): the joiner's load from the title took 14 s against the host's 0.2 s
    // reload, the host's town ran 9 ticks on her inputs from the loading screen, and the clocks parted (OUT OF SYNC at tick 60,
    // a second RESET; w-town-apart p2cave 2026-10-26). The keepalive's Busy keeps the other copy waiting (kLoadWaitMs).
    bool                       load_hold = false;

    // Input delay: the session's first (inputs before it are released pads on both copies), the
    // least it may shrink to, whether it adapts to the round trip, the newest frame this copy has
    // given an input, the last input given, and how long the round trip has wanted less delay.
    int           first_delay = 3;
    int           least_delay = 3;
    bool          adaptive = true;
    std::int64_t  newest = -1;
    InputPadState last_input;
    int           shrink_wait = 0;
    int           script_delay = 3; // coop-loadcause: the delay the link started with (ScriptInputFor)
    std::int64_t  drop_tick = -1; // DC_NETPLAY_DROP_GROWTH: the tick whose packets are not sent
    // DC_NETPLAY_LATENCY: presses (a button down that was up) read for a frame, logged as the game gets
    // them: the tick read, the frame they are for, and when.
    struct Press {
        std::int64_t                          read_tick = -1;
        std::int64_t                          frame = -1;
        std::chrono::steady_clock::time_point at;
    };
    Press presses[16];
    // Ticks a second, between diagnostics lines.
    std::int64_t                          rate_tick = 0;
    std::chrono::steady_clock::time_point rate_at;
    double                                tick_rate = 0.0;
    int                        resyncs = 0;
    std::int64_t               resynced_status_until = -1;
    long long                  receive_cuts = 0; // passes of Receive cut at kReceivePassMs (coop-ws5left)
    std::chrono::steady_clock::time_point receive_cut_said; // the last "receive pass cut" line
};

Link g_link;
int  g_game_pad_player = 1; // coop-scriptmenus: whose agreed pad port 0 carries (NetplayGamePadPlayer)

// coop-localfix2: this copy sends the catch-up's state (and plays on), or takes it (and catches up).
bool SendsState() {
    return g_link.player == g_link.source;
}

bool TakesState() {
    return g_link.player != 0 && g_link.player != g_link.source;
}

// coop-p2skin: NetplayHoldsJoinFrame's state: the tick the first state of the host was taken at (-1 not yet), and the
// ticks held and shown before it, logged once.
struct JoinHold {
    std::int64_t state_tick = -1;
    int          held = 0;
    int          shown = 0;
    bool         said = false;
};

JoinHold g_join_hold;

void JoinStateTaken(std::int64_t tick) {
    if (TakesState() && g_join_hold.state_tick < 0) {
        g_join_hold.state_tick = tick;
    }
}


// Option B (game.netplay_catchup): the host playing alone takes a checkpoint each time one of its own modes ends for a
// town or a dungeon (its state then, inside the loading screen it shows anyway, so nothing on screen changes), and from
// the first tick in the new place routes its pads through the link's path, recording them and a checksum each second,
// so a joiner can later take the checkpoint and replay the pads to the host's tick instead of both reloading. Step 1:
// the recorder, the checkpoint, and DC_NETPLAY_REPLAY=<file>, a copy that takes a recording's checkpoint and pads alone
// and says whether its checksums match (catchup.ps1). Pad 2 is neutral while alone.
struct Solo {
    bool                       on = false;   // pads routed (recorded or replayed) from tick 0
    bool                       due = false;  // a checkpoint taken: tick 0 is the first tick in the new place
    bool                       left = false; // replay: this copy left for the recording's checkpoint
    bool                       replay = false;
    bool                       replay_done = false;
    std::vector<unsigned char> checkpoint;
    std::vector<InputPadState> pads;  // two a tick, from the checkpoint's tick on
    std::vector<std::int64_t>  sum_ticks;
    std::vector<std::uint32_t> sums;
    std::size_t                next_sum = 0; // replay: the recorded checksum to compare next
    int                        matched = 0;
    std::int64_t               parted = -1;
    // coop-worlds: player 2's progress from apart the host applied while it recorded, each with the tick it came before
    // (NetplayCatchupDelta); the joiner's replay applies each on its tick, so both copies hold it from there on.
    struct Delta {
        std::int64_t tick;
        std::int32_t value[5];
    };
    std::vector<Delta> deltas;
    std::size_t        next_delta = 0;
    bool               place = false; // coop-bldgsnap: the checkpoint is a place checkpoint (Checkpoint's place): snapshot only
};

Solo g_solo;

// coop-doublereset: a copy that takes the other's state drops what it kept of its own (its last catch-up's checkpoint); the
// line is what o-opening-resync house-stay's taker check reads (its twin taker_keeps_solo says it keeps it instead).
void TakerDropsSolo() {
    if (g_solo.on || !g_solo.checkpoint.empty()) {
        std::fprintf(stderr, "catchup: this copy takes the other's state and drops its own %scheckpoint\n", g_solo.place ? "place " : "");
    }
    g_solo = Solo{};
}

// Steps 2 and 3, a join by catch-up: a host linked while it records (see Solo) plays on alone and sends the joiner its
// checkpoint and its pads since, in blobs; the joiner leaves for the checkpoint and replays them as fast as it can,
// saying how far it is (Caught). Each time it has replayed all it holds the host sends the pads since; once it is
// within kJoinGapTicks, that blob is the last and names tick J, where the host starts the lockstep and the joiner,
// replayed to J, does too. The host is never reloaded; until it has a checkpoint a join keeps the quiet resync.
constexpr std::uint32_t kCatchBit = 0x80000000u;   // a Chunk's or Ack's tick with it: a catch-up blob's id
constexpr std::uint32_t kCaughtMagic = 0x44435543; // "CUCD": the joiner replayed to a tick (a Busy, unused the tick)
constexpr int           kJoinGapTicks = 90;
// coop-lockgap: her merge's tight gap (coop-localfix2, below). At 2 he trailed her by ~4 ticks through many 2-tick blobs,
// 180-400 ms of his black; at 8 her copy waits at most on his last 8 replay ticks (~10 ms). Roll back: 2.
// DC_NETPLAY_TEST_STAYER_GAP=N tries another (2: the old way). DC_NETPLAY_MUTATE=stayer_gap_0 (twin): no gap and no
// floor of the link's delay, so her merge closes only by the 8 s fallback (the old 2 missed only a word 4 behind, 2 runs in 3).
constexpr int           kStayerGapTicks = 8;
// coop-inlinetwin: in her merge his word also goes as he plays her newest pads (within this many ticks of the last he
// holds: the last one, the tick before he would wait), not only out of pads: caught up within a frame or two of the
// apply, he played her 2-tick blobs as they came at frame pace and never ran out, so no word came and her close waited 30.7 s (l-local-play-houseout-seamless~inlineshots
// alone, 2026-11-02: "his word -1" 922 times). This closes every replay that reaches her newest pads: one 2+ ticks behind
// them plays unthrottled (NetplayCatchingUp) and gains, and only one caught up (1 behind) plays at frame pace; a PC that
// cannot replay faster than her pace never closes cheaply in any scheme (her wide gap would wait on its last 90 ticks). A
// keep-alive back after kStayerTightMs (coop-inlinetwin, dropped in coop-sendstop) covered no other case: under -Jobs 2 it
// went 180-250 behind from a slow replay that was gaining, closed her at the wide gap on his last 90 ticks (a 408 ms frame on
// her screen) and counted 108 blobs in lockgap's check, where this word closed it at the tight gap a moment later.
constexpr int           kEdgeWordTicks = 1;
// coop-reds2 (the replay that never closes: Jep 11-10, option A of reds2's report): a joiner whose replay is not gaining on
// the sender (a slow PC, or replayed ticks dearer than live ones: l-local-play-houseout-hertalk's log lines on PC-02, 39 ticks
// a second against her 60) never reached her newest pads, so no word came and the close never came: black for good. He now
// measures his lag (her newest pads he holds less his replay tick) over kFallbackWindowMs; when at that window's gain it would
// not be within kFallbackGapTicks in kFallbackCloseMs, he asks for a fresh state set (kRestartMagic) and from then on says his
// tick every 50 ms. The sender takes a whole fresh set at its tick S and sends it on the same blobs; he replays on through the
// transfer, puts it back at a frame start, and goes on from S. Once his word reaches S the sender closes within
// kFallbackGapTicks, asking again if he falls behind again.
// KNOWN EXCEPTION to Jep's merge-no-wait rule, his choice for this release: in that close the sender waits once on his last
// ticks (at most kFallbackGapTicks of his replay, some 0.3-0.5 s on a slow PC), and only in the fallback. The planned removal
// of that wait (his first pads Neutral through his arrival's black, his input delay stepping down after) is in
// debug\scratch\reds2\report.md. Per joiner: the state lives in the catch-up (Catch), one per link.
// DC_NETPLAY_MUTATE=no_replay_fallback (twin): no ask, no fresh set, as before (he stays black).
constexpr std::uint32_t kRestartMagic = 0x44435352; // "RSCD": the joiner asks for a fresh state set (a Busy, unused his tick)
constexpr int           kFallbackWindowMs = 1000;
constexpr int           kFallbackCloseMs = 4000;
constexpr int           kFallbackGapTicks = 30;
constexpr int           kStayerTightMs = 8000; // her close's tight gap lasts this long, then the usual gap

struct Catch {
    bool                                  active = false; // under way, until the lockstep starts
    bool                                  walk_wait = false; // coop-pt1106-lockup: in the checkpoint's place, plays on to its landing
    std::chrono::steady_clock::time_point walk_wait_said{};  // coop-pt1106-lockup: the wait's last "still" line
    std::int64_t                          join_tick = -1; // J, once the last blob names it
    std::int64_t                          test_close_at = -1; // coop-catchuprace (tests): close on this tick (a delta's)
    std::chrono::steady_clock::time_point heard{};        // the other copy's last packet
    std::chrono::steady_clock::time_point said{};         // the joiner's last Caught
    std::chrono::steady_clock::time_point began{};        // coop-catchupdrop: as it began on this copy (the give-up hook's)
    std::chrono::steady_clock::time_point piece_at{};     // coop-catchupdrop: the joiner's last piece of it (NetplayCatchMoving)
    std::chrono::steady_clock::time_point replay_began{}; // the joiner's tick 0
    std::chrono::steady_clock::time_point closing_from{}; // coop-localfix2: the sender's first blob of pads after the state set
    std::int64_t                          replay_frame = -1; // GameFrameCount() as last given back (ReplayFree)
    std::int64_t                          replay_free = 0;   // the frames given back
    // coop-shotthread (the load-side lockgap): where the joiner's replay time goes, said at the lockstep: the wait for her
    // next pads (and how many waits) and the rest of each frame outside the netplay tick (the game's own tick, any drawing).
    double                                replay_wait_ms = 0.0, replay_rest_ms = 0.0;
    long                                  replay_waits = 0, replay_ticks = 0;
    // coop-replaywork (therefirst, 2026-11-03: the replay's 5 ms a tick went red at 5.4 and 6.7 with 18 copies on the PC, the work
    // unchanged): the work the replay's cost check stands for, counted, not timed: the shot lines said on the game thread while it
    // replays (each a write to the harness's pipe) and the frames read back from the screen while it replays (each a render and a
    // readback). Both are 0 and at most 1 with the replay's holds (coop-shotthread, coop-joinlate); DC_NETPLAY_MUTATE=shots_inline
    // brings them back.
    long                                  replay_lines_said = 0, replay_readbacks = 0;
    std::chrono::steady_clock::time_point replay_left{};
    std::chrono::steady_clock::time_point edge_at{}; // coop-sendstop: when he first played her newest pads (kEdgeWordTicks)
    // The host: the blob in flight and the pieces the joiner holds, the pads sent so far, the joiner's last report.
    std::uint32_t                         out_id = 0;
    std::vector<unsigned char>            out;
    std::uint32_t                         out_held[kChunks / 32] = {};
    // coop-pagediff: when the blob in flight was all held (its "went in" time ends there, not when the next blob begins: the
    // page list's next blob waits on the taker's landing)
    std::chrono::steady_clock::time_point out_held_at{};
    PieceSender                           out_pieces;
    std::int64_t                          sent_to = 0;
    std::size_t                           deltas_sent = 0; // player 2's changes from apart the blobs so far carried
    std::vector<unsigned char>            join_save;       // the joiner: the host's town Georama at J, from the last blob
    int                                   join_town = -1;  // ... and its town
    // coop-joinlobby: the joiner's pick (member, profile, its bag), carried by the last blob; both copies give it at J
    bool                                  pick_set = false;
    int                                   pick_member = -1;
    std::string                           pick_name;
    std::vector<unsigned char>            pick_bag;
    std::int64_t                          caught = -1;
    // The joiner: the blob arriving, the last whole one (ids from 1), the pads held so far.
    std::uint32_t                         in_id = 0;
    std::uint32_t                         in_done = 0;
    std::vector<unsigned char>            in;
    std::uint32_t                         in_held[kChunks / 32] = {};
    std::uint32_t                         in_pieces = 0; // coop-steamceiling: the pieces of the blob arriving that came (coop-ackbatch counts them too),
    std::uint32_t                         in_again = 0;  // ... and of those, ones already held (late, not lost)
    std::uint32_t                         in_acks = 0;
    std::uint32_t                         acks_told = 0; // ... last told for this blob
    std::int64_t                          have_to = 0;
    bool                                  building_door = false; // coop-housemerge: in by the host's door (once)
    bool                                  land_here = false;     // coop-snapjoin: in the checkpoint's place already (InCheckpointZone)
    // coop-reds2 (kRestartMagic): the joiner's lag window and asks; the sender's fresh sets and the last one's tick.
    std::chrono::steady_clock::time_point lag_from{};
    std::int64_t                          lag_at_from = -1;
    int                                   restarts_asked = 0;
    bool                                  restart_waiting = false;
    std::chrono::steady_clock::time_point restart_said{};
    int                                   restarts = 0;
    std::int64_t                          restart_tick = -1;
};

// coop-reds2: the sender's waits on the other copy's pads over the first ticks after its catch-up's close (its one wait in
// a fallback close, kFallbackGapTicks), said once.
struct CloseWatch {
    int    left = 0;
    bool   fallback = false;
    double longest = 0.0, total = 0.0;
};
CloseWatch g_close_watch;

Catch g_catch;
int   g_catches_begun = 0; // coop-catchupdrop: catch-ups begun on this copy in this game (the give-up hook's N)

// coop-snapshot stage 4 (DC_NETPLAY_SNAPSHOT=1 on the host): a catch-up join sends the
// checkpoint alone, then the host's state set as taken on a quiet frame start at tick S (Snap::tick), in parts of at most
// one blob each (from kSnapMark, to the part, join the count), then the pads from S on. The joiner takes the checkpoint,
// lands as today, puts the state back at a frame start, sets its tick to S and replays from there. A refused apply asks
// the host for the pads from tick 0 instead (kSnapRefusedMagic), the catch-up as before.
constexpr std::uint32_t kSnapMark = 0xFFFFFFFEu;
constexpr std::uint32_t kSnapRefusedMagic = 0x44435253 ^ 0x00FF0000; // "SRCD" with a bit flipped: a Busy from the joiner
// coop-floorjoin2: the first blob's join word when the host's state set follows the checkpoint (CatchHostTick), so the joiner
// knows before it lands that what it plays until tick S is thrown away (NetplayFloorStarted skips the floor's opening scene).
constexpr std::uint32_t kSnapFollows = 0xFFFFFFFDu;
constexpr std::size_t   kSnapPartBytes = (std::size_t) kChunks * kChunkBytes - 64;
// coop-pagediff (Jep, 2026-10-28: only the pages the arriving copy lacks): a blob's from word
// for a part of the sender's page list (SnapshotPageList) and for a part of the pages the taker needs (the whole state set's
// parts keep kSnapMark). The taker's word of the pages it needs (PageNeed, kPageNeedWords * 32 pages a packet, sent again
// every kPageAgainMs until the pages come) and its ask for the whole set (a Busy, likewise) go the other way.
// DC_NETPLAY_MUTATE=no_pagediff: the whole state set, as before.
constexpr std::uint32_t kPageListMark = 0xFFFFFFFCu;
constexpr std::uint32_t kPageDiffMark = 0xFFFFFFFBu;
constexpr std::uint32_t kPageNeedMagic = 0x4443444E;  // "NDCD"
constexpr std::uint32_t kPageWholeMagic = 0x44434857; // "WHCD"
constexpr std::uint32_t kPageNeedWords = 128;
constexpr int           kPageAgainMs = 200;

struct PageNeed {
    std::uint32_t magic;
    std::uint32_t player;
    std::uint32_t session;
    std::uint32_t first;  // the first word's index
    std::uint32_t pages;  // the list's pages (all packets)
    std::uint32_t words[kPageNeedWords];
};
// coop-offthread (shared-state review 2026-11-09, section 4; option 2): the stayer's take held its game thread 179-399 ms (the
// copy, then the packing and the page list) and its frames then waited up to 276 ms a tick on the parts' sends: a join
// affected the player already there (merge-no-wait). Only the copy reads the game's memory, so only the copy stays on the
// tick; the packing, the page list and the pages' diff run on a thread of their own (SnapJob), and the game thread picks up
// what they made at a later frame (SnapJobPoll). DC_NETPLAY_MUTATE=pack_on_tick (twin): all of it on the game thread, as before.
struct SnapJob {
    int                                         kind = 0; // 1 the take's pack and page list, 2 the pages the taker needs
    std::atomic<bool>                           done{false};
    std::chrono::steady_clock::time_point       began{}; // the take's start (kind 1) or the job's (kind 2)
    double                                      tick_ms = 0.0; // what the game thread spent (kind 1: the copy)
    double                                      work_ms = 0.0; // what the job's thread spent
    // Kind 1: in, the state set as taken; out, it packed (or raw) and, by pages, the set kept whole and its list packed.
    bool                                        by_pages = false;
    bool                                        raw_send = false; // snap_raw
    std::vector<unsigned char>                  bytes;
    std::shared_ptr<std::vector<unsigned char>> raw;
    std::size_t                                 taken_size = 0;
    long long                                   pack_ms = 0, list_ms = 0;
    bool                                        listed = false;
    std::size_t                                 list_pages = 0, list_size = 0;
    std::vector<unsigned char>                  list_packed;
    // Kind 2: in, the set as taken and the taker's need; out, the pages packed.
    std::vector<std::uint32_t>                  need;
    bool                                        diff_ok = false;
    std::size_t                                 diff_pages = 0, diff_size = 0;
};

constexpr int           kSnapMenuWaitMs = 3000; // coop-snapjoin: the host's take waits this long for a frame with no menu
constexpr int           kSnapSceneWaitMs = 180000; // coop-stable1: and this long for a scene to end (was 8 s: the fallback reloaded her town), then replays instead

struct Snap {
    // The host.
    bool                       want = false;    // coop-floorsnap: the link's start found a floor join (Begin), whatever presence says later
    bool                       on = false;      // this join goes by snapshot
    bool                       take_due = false; // take at the next quiet frame start
    std::chrono::steady_clock::time_point due_at{}; // when take_due was set (coop-snapjoin: a menu open past kSnapMenuWaitMs)
    long long                  due_drawn = -1;  // coop-ws5left: g_frames_drawn when take_due was set (the take waits for a drawn frame)
    bool                       taken = false;
    bool                       refused = false; // the joiner could not apply it: the pads from 0
    std::int64_t               tick = -1;       // S
    std::vector<unsigned char> bytes;
    std::size_t                parts = 0;
    std::size_t                sent = 0; // parts sent (the one in flight included)
    bool                       pads_begun = false;
    // The joiner.
    std::vector<unsigned char> in;
    std::size_t                in_parts = 0;
    std::size_t                in_total = 0;
    std::int64_t               in_tick = -1;
    bool                       apply_due = false;
    bool                       applied = false;
    bool                       said_refused = false;
    bool                       expected = false; // coop-floorjoin2: the host said its state set follows the checkpoint (kSnapFollows)
    // coop-pagediff, the host: what its parts carry (mark), the step (0 the whole set or done, 1 the list going, 2 waiting
    // for the taker's need, 3 the pages going), its state set as taken (raw) and the taker's need.
    std::uint32_t              mark = kSnapMark;
    int                        page_step = 0;
    std::shared_ptr<std::vector<unsigned char>> raw; // (coop-offthread: shared with a SnapJob reading it)
    std::vector<std::uint32_t> need;
    std::shared_ptr<SnapJob>   job; // coop-offthread: the pack or the pages' diff under way off the game thread

    std::vector<bool>          need_got;
    bool                       whole_asked = false;
    // coop-pagediff, the taker: what the parts coming carry, the sender's list, its own state set and need, the pages in.
    std::uint32_t              in_mark = kSnapMark;
    std::vector<unsigned char> list;
    bool                       list_in = false;
    std::vector<unsigned char> own;
    std::vector<std::uint32_t> in_need;
    std::size_t                list_pages = 0;
    bool                       need_sent = false;
    bool                       diff_due = false;
    bool                       answered = false; // pages or the whole set began to come: the need goes no more
    bool                       asked_whole = false;
    std::chrono::steady_clock::time_point said_at{};
};

Snap g_snap;
NetplayCostTotals g_costs; // coop-ws5left: where this copy's time went (NetplayCosts)

// coop-ws5left: adds the scope's time to a NetplayCostTotals field.
struct CostScope {
    double                               &into;
    std::chrono::steady_clock::time_point at = std::chrono::steady_clock::now();
    ~CostScope() { into += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - at).count(); }
};
long long g_frames_drawn = 0; // coop-ws5left: frames this copy drew (NetplayTestShotAfterFrame), its own

int CheckpointFloor(const std::vector<unsigned char> &state);
void ResyncCause(const char *cause, const char *site);                                 // coop-stable1: the zone log
extern int g_test_epoch, g_test_epoch_resyncs;                                         // coop-stable1: the menu presses' clock (below)
void ZoneLeaveLog(int target, const char *cause, const char *site, bool no_load);      // coop-stable1

// game.netplay_snapshot (the Online launchers set DC_NETPLAY_SNAPSHOT=1), a floor join, the link's start that found one, or
// a floor-start checkpoint (the host on a floor, whoever joins: a joiner from the title or a town builds the floor its own
// way, through the game's dungeon entry, and however much of the host's save it takes first the floor's scripts, monsters
// and random table come out unlike the host's, train 9, 2026-10-24; the snapshot carries all of them); only on a pinned
// layout.
bool SnapshotJoinOn() {
    const bool asked = dc::platform::ConfigGet().netplay_snapshot || TestEnvOn("DC_NETPLAY_SNAPSHOT") || g_snap.want || CoopWorldsFloorJoin() ||
                       CheckpointFloor(g_solo.checkpoint) >= 0;

    return asked && SnapshotLayoutPinned() && !NetplayMutated("no_snapshot");
}

// coop-samedoorfix (w-flagsync -Case samedoor, red with game.coop_story_gather off): whether a merge goes by the state of
// the copy that was there first (the Hellos' stayer). It asked for the snapshot as the session's setting, which only the
// Online launchers set (DC_NETPLAY_SNAPSHOT), so from the title's Multi-player or a test without it the host sent always:
// she in the mayor's house first and he following, her copy caught up to his walk-in and played the mayor's scene that
// both copies held for her merge. A merge by her state takes the snapshot by itself now (g_snap.want, Begin).
// DC_NETPLAY_MUTATE=stayer_needs_snapshot (twin): by the session's setting, as before.
bool StayerMergeOn() {
    return SnapshotJoinOn() || (SnapshotLayoutPinned() && !NetplayMutated("no_snapshot") && !NetplayMutated("stayer_needs_snapshot"));
}

} // namespace
namespace dc::dialog {
bool SessionsRebindable(std::string &missing); // dialog/framework.cpp (coop-pt1111e: SnapshotQuietTick)
void WriteSessions(std::vector<uint8_t> &out); // (coop-pt1111e: the state set's dialog extra)
bool ReadSessions(const uint8_t *data, size_t size, std::string &why);
bool IsSessionActive() noexcept;
void DialogNpcWriteWindows(std::string &out);   // dialog/npc.cpp
bool DialogNpcReadWindows(const char *data, std::size_t size);
} // namespace dc::dialog
namespace dc::quest {
void CustomShopWrite(std::string &out);     // quest/custom_shop.cpp (coop-pt1111e: the state set's dialog extra)
bool CustomShopRead(std::string_view data);
} // namespace dc::quest
namespace {

// A tick the host's state may be taken on: on a floor under control, no script under way (so the script VM's heap holds
// nothing the snapshot would need), no menu over the floor. coop-snapjoin: or in a town or a building, walking (no scene,
// no menu, no door under way).
// menu_ok (coop-snapjoin: the host held a menu open and never took it, opening resync log 2026-10-25): a menu open is
// allowed, a scene still is not (the menu's state is the game's globals, in the set; a scene's script is on the heap).
// DC_NETPLAY_TEST_SNAP_MENU=1 (tests): every tick counts as a menu open (the strict test never passes).
bool SnapshotQuietTick(bool menu_ok = false) {
    static const bool menu_for_test = TestEnvOn("DC_NETPLAY_TEST_SNAP_MENU");

    // coop-pt1111e (Jep's 11-11 playtest item 6: OUT OF SYNC in the mayor's house, P2 following P1 in as his talk went on): a talk
    // with a villager a quest adds choices to (the mayor's "Do you have any supplies?") opens Chronicle's dialog framework, whose
    // session was the process's heap alone: a set taken in it gave the taker the talk with no session (her talk ended on her first
    // replayed tick, or her copy crashed in its window). The sessions now ride with the set as data (kExtraDialog, each choice bound
    // again by its id: dialog::ReadSessions), so the take goes mid-conversation, no wait. Only a choice nothing can bind again (a
    // quest's Scene::Choice not declared by Builder::Choice; its quest says so as it is made, "quests: LINT") holds the take, said
    // loudly: with today's quests none (x-talk-follow -Unbound proves the guard).
    if (SendsState() && g_snap.take_due) {
        std::string missing;

        if (!dc::dialog::SessionsRebindable(missing)) {
            static std::string said;

            if (said != missing) {
                said = missing;
                std::fprintf(stderr, "catchup: LINT: the take waits: dialog choice '%s' binds to nothing on another copy (declare it: Builder::Choice or a resolver)\n",
                             missing.c_str());
            }
            return false;
        }
    }

    if (mode == GAME_MODE_EDIT) {
        // coop-scenejoin: or a room's scene under way (its script's state is in the set: the VM's pinned arena), so her
        // merge goes into his scene (CoopWorldsSceneTakeOk). A talk still waits. coop-exitscene: a town's event outdoors too
        // (CoopWorldsTownSceneTakeOk: his scene as he came back from the dungeon).
        // (coop-stage4 4e part 1: his talk too, indoors and out: CoopWorldsTalkTakeOk)
        // coop-soakfix3: or his pause (Start in his walk: the town's ED_MODE_RETURN_MENU_WALK, a room's ED_IN_MODE_PAUSE, 5), which
        // he may sit in for as long as he likes: its state is the town's globals (the mode, the pause's dim), no script, no menu
        // pack, and linked both copies step his retail pause as they would after the take. DC_NETPLAY_MUTATE=pause_holds_join
        // (twin): it waits, as before (a place checkpoint's join for good: no fallback).
        const bool paused = !NetplayMutated("pause_holds_join") && (EdInteriorFlag == 0 ? ::GameMode == ED_MODE_RETURN_MENU_WALK : CoopTownIndoorMode() == 5);
        // coop-soakfix3: or his fishing (ED_MODE_FISHING, from the fishing board's script, Norune's event 11), which he may sit in for
        // good: a place checkpoint's join (no fallback) waited till he quit. Its state is the decomp's (the line, the fish, the
        // bobber: editloop3's and fish.cpp's globals, in the set); his pad steps it on every copy as it would linked.
        // DC_NETPLAY_MUTATE=fish_holds_join (twin): it waits, as before.
        const bool fishes = EdInteriorFlag == 0 && ::GameMode == ED_MODE_FISHING && !NetplayMutated("fish_holds_join");
        const bool walks = paused || fishes ||
                           (EdInteriorFlag == 0 ? ::GameMode == ED_MODE_WALK || (::GameMode == ED_MODE_EVENT && CoopWorldsTownSceneTakeOk()) ||
                                                     (::GameMode == ED_MODE_TALK && CoopWorldsTalkTakeOk())
                                               : CoopTownIndoorWalks() || (CoopTownIndoorMode() == 4 && CoopWorldsSceneTakeOk()) ||
                                                     (CoopTownIndoorMode() == 3 && CoopWorldsTalkTakeOk()));
        // (coop-stage4 4d: his Georama too: its editor is the game's globals, in the set, and both copies step it once linked)
        const bool menu = EdInteriorFlag == 0 ? ::GameMode == ED_MODE_MENU_INIT || ::GameMode == ED_MODE_MENU || CoopGeoramaCarries() : CoopTownIndoorP1Menu();

        // coop-houseblack (Jep's 2026-11-04 pack 8 playtest: he came out of a house to her in town and his screen stayed black
        // 4+ minutes, her copy's "the place checkpoint's join waits for a quiet frame (mode 2, task 401)" every 10 s): gameTask
        // is the dungeon's; no town code sets or reads it, so in a town it is whatever the dungeon left. She had gone back to
        // town by Circle at the floor menu (coop-gateflow), mid the gate's script: GAME_TASK_SCRIPT_RUN for good, never quiet,
        // and a place checkpoint's take has no fallback. A town goes by its own mode alone. DC_NETPLAY_MUTATE=town_quiet_task
        // (twin, l-local-play-houseout-towntask~twin): the dungeon's task must be play here too, as before.
        static const bool task_counts = NetplayMutated("town_quiet_task");

        return (!task_counts || gameTask == GAME_TASK_PLAY) && ((walks && !menu_for_test) || (menu_ok && (walks || menu)));
    }
    // coop-localfix: after the menu's wait, a knocked-out host's change screen too (its state is the game's globals, as a
    // menu's): the replay from tick 0 it fell back to parted the copies (P1 knocked out alone as she arrived, 2026-10-26).
    // DC_NETPLAY_MUTATE=ko_not_quiet (twin): never, as before.
    if (menu_ok && mode == GAME_MODE_DUNGEON && (gameTask == GAME_TASK_DEAD || gameTask == GAME_TASK_DEAD_CHANGE_CHARA || gameTask == GAME_TASK_CHARA_SELECT_LOOP) && BtEventMode == 0 &&
        !NetplayMutated("ko_not_quiet")) {
        return true;
    }
    // coop-b1lesson: or the floor's opening scene she joins (CoopWorldsSceneJoining: its script's state is in the set, the
    // VM's arena, since coop-scenejoin), so her copy takes it mid-scene and plays the rest with this one.
    if (mode == GAME_MODE_DUNGEON && BtEventMode != 0 && (gameTask == GAME_TASK_PLAY || gameTask == GAME_TASK_SCRIPT_RUN) && CoopWorldsSceneJoining() == 1) {
        return true;
    }
    // coop-joinfix (LAN playtest 2026-10-30, item 37a: she joined his B4 during its skeleton scene and stayed black through all
    // of it, the take waiting for its end): after the menu's wait, any scene under way on a floor (a system script, not the
    // floor's arrival walk-in) too, for a join from the title as for the opening scene's join above: her copy takes it mid-scene
    // and plays the rest with his (Jep's rule: a player joining the other's scene sees it with them). DC_NETPLAY_MUTATE=
    // scene_holds_join (twin): the take waits for the scene's end, as before.
    // coop-soakfix3: or the event's item choice (SCRIPT_ITEM_SELECT: "use an item" at a door or an altar), which he may sit in: the
    // script's own state, as the scene's. DC_NETPLAY_MUTATE=message_holds_join (twin): it waits, as before.
    const bool scene_task = gameTask == GAME_TASK_PLAY || gameTask == GAME_TASK_SCRIPT_RUN ||
                            (gameTask == GAME_TASK_SCRIPT_ITEM_SELECT && !NetplayMutated("message_holds_join"));

    if (menu_ok && mode == GAME_MODE_DUNGEON && BtEventMode != 0 && scene_task &&
        (BtEventInfo.running_script_no != 100 || CoopWorldsArrivalPlaysScene()) && !NetplayMutated("scene_holds_join")) {
        return true;
    }
    // coop-soakfix3 (soak seed 26111134: the host's R2 took him into the first-person view, task 10, and he stood in it with no
    // input left; the take waited past 60 s for a quiet frame, the joiner black throughout, against Jep's rule that a join never
    // waits): the first-person view is play under his control, its state the dungeon's globals (viewMode__2, viewAngleH__2 and
    // the camera, all in the set), no script on the heap, so it is as quiet as GAME_TASK_PLAY. DC_NETPLAY_MUTATE=eye_holds_join
    // (twin): only GAME_TASK_PLAY, as before.
    // So too retail's pause (GAME_TASK_PAUSE, Start; coop_pause's own per-player pause, when on, leaves the task at play): its
    // holds (driveStepHold, CMonUnitHold, CEffectHold) are the dungeon's globals. DC_NETPLAY_MUTATE=pause_holds_join (twin).
    // And the floor's message waits he may sit in until his Cross: a system message (SYSMES_WAIT), his gate key's or attachment's
    // pickup (GATE_KEY_LOOP, ATTACH_LOOP: retail's world tasks, opt7 not yet local), Atla refused (ATRA_REFUSE_WAIT), the
    // revival's (REVIVE_WAIT, REVIVE_DONE). Linked play runs each of them on every copy in lockstep, so a set taken in one gives
    // the joiner what a pair already linked steps; their state is the dungeon's globals and the game's arenas. (Party defeated,
    // DEAD_GAMEOVER, ends the floor: not here.) DC_NETPLAY_MUTATE=message_holds_join (twin): they wait, as before (3 minutes).
    const bool message = (gameTask == GAME_TASK_SYSMES_WAIT || gameTask == GAME_TASK_GATE_KEY_LOOP || gameTask == GAME_TASK_ATTACH_LOOP ||
                          gameTask == GAME_TASK_ATRA_REFUSE_WAIT || gameTask == GAME_TASK_REVIVE_WAIT || gameTask == GAME_TASK_REVIVE_DONE) &&
                         !NetplayMutated("message_holds_join");
    const bool plays = gameTask == GAME_TASK_PLAY || (gameTask == GAME_TASK_EYE_CAMERA && !NetplayMutated("eye_holds_join")) ||
                       (gameTask == GAME_TASK_PAUSE && !NetplayMutated("pause_holds_join")) || message;

    return mode == GAME_MODE_DUNGEON && plays && BtEventMode == 0 &&
           ((BtGameModeFlag == BT_GAME_MODE_PLAY && !menu_for_test) || menu_ok);
}

// coop-snapjoin (Jep, 2026-10-25: "just request a snapshot be sent when the second player changes zones"): a join or a
// merge in a town or a building goes by the host's state set too, as on a floor, not by a replay from the checkpoint.
// DC_NETPLAY_MUTATE=no_town_snapshot (twin): the replay, as before.
// coop-bldgsnap: inside a building too. The joiner's fault after an apply in the mayor's house (EditInLoop's talk step,
// CObject::GetDistance on a null talker, o-opening-resync -Step house-after) was the port's interior loop's own statics
// (port/src/edit_in.cpp: its phase, character and camera pointers, the room's parts) left out of the state set: in it now
// (snapshot-allow.txt). DC_NETPLAY_MUTATE=no_building_snapshot (twin): buildings keep coop-housemerge's replay.
bool SnapshotJoinHere() {
    return mode == GAME_MODE_DUNGEON ||
           (mode == GAME_MODE_EDIT && (EdInteriorFlag == 0 || !NetplayMutated("no_building_snapshot")) && !NetplayMutated("no_town_snapshot"));
}

bool Catchup();

// coop-joinfix test hook (DC_COOP_TEST_SCENE_AT_JOIN=N, n-debug-start -Kind floorscene; item 37a): the host on a floor, as a join
// from the title begins (the catch-up's start), runs the floor's system script N by the game's own debug-event way
// (GAME_TASK_DEBUG_RUN_EVENT), so the join's state set comes while a scene is under way. Logs the scene's start and end.
void TestSceneAtJoin(bool join_began) {
    static const int script = TestEnvInt("DC_COOP_TEST_SCENE_AT_JOIN", -1);
    static int       state = 0; // 0 waiting, 1 asked, 2 running, 3 done
    static long long ran = 0;

    if (script < 0 || state == 3) {
        return;
    }
    if (state == 0 && join_began && mode == GAME_MODE_DUNGEON && gameTask == GAME_TASK_PLAY && BtEventMode == 0) {
        DebugStatus[11] = script;
        DebugStatus[12] = 0;
        gameTask = GAME_TASK_DEBUG_RUN_EVENT;
        state = 1;
        std::fprintf(stderr, "netplay: test: floor system script %d starts as the join begins (tick %lld)\n", script, (long long) g_link.tick);
    } else if (state == 1 && BtEventMode != 0) {
        state = 2;
    } else if (state == 2) {
        ran++;
        if (BtEventMode == 0 && gameTask == GAME_TASK_PLAY) {
            state = 3;
            std::fprintf(stderr, "netplay: test: floor system script %d ended after %lld ticks (tick %lld)\n", script, ran, (long long) g_link.tick);
        }
    }
}

// What Keepalive reads from its own thread: whether the link is up, as whom, and when this copy last ticked.
std::atomic<bool>          g_keepalive_on{false};
std::atomic<std::uint32_t> g_keepalive_player{0};
std::atomic<std::uint32_t> g_keepalive_session{0};
std::atomic<std::int64_t>  g_last_tick_ms{0};

std::int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// coop-offthread (shared-state review 2026-11-09, section 4: herevent's "catching up" read 0 ms in every run): the arrival
// black's split came from NetplayArrivalHold's own calls, and none comes while the catch-up runs (its frames hold), so its
// "linked" was never seen and the whole black counted as before the link. The arriving copy's clock at each phase's edge,
// stamped where it happens: the link, the state set's apply (from, to) and the lockstep (NetplayArrivalHold's phases line).
struct ArrivalStamps {
    std::int64_t link = -1, apply_from = -1, apply_to = -1, lockstep = -1;
};
ArrivalStamps g_arrival;

// coop-replayspeed: the "[t N]" stamp on a catch-up's lines: the machine's steady clock in ms (two copies on one PC read
// the same clock), so a merge's stages can be timed across both logs.
long long LogMs() {
    return (long long) (NowMs() % 10000000);
}

// While the link is up and this copy has not ticked for kBusyAfterMs (it is loading), a Busy every
// kBusyEveryMs keeps the other copy waiting for it.
// coop-cand58: its wait ends at once as the game ends (NetplayStopThreads), and the thread is joined.
std::mutex              g_keepalive_lock;
std::condition_variable g_keepalive_wake;
std::thread             g_keepalive_thread;

void Keepalive() {
    const bool off = NetplayMutated("no_keepalive");

    for (;;) {
        {
            std::unique_lock<std::mutex> hold(g_keepalive_lock);

            if (g_keepalive_wake.wait_for(hold, std::chrono::milliseconds(kBusyEveryMs), [] { return g_exit_stop.load(); })) {
                return;
            }
        }

        if (off || !g_keepalive_on.load() || NowMs() - g_last_tick_ms.load() < kBusyAfterMs) {
            continue;
        }

        Busy busy{kBusyMagic, g_keepalive_player.load(), g_keepalive_session.load(), 0};

        ExitCountSend();
        NetSocketSend(&busy, sizeof busy);
    }
}

// As the game quits while linked, a few Leaves tell the other copy at once (it would else wait out kWaitMs).
// apart_at: the copies go apart (a split), this copy's link tick as it does; the other copy plays on to that tick and
// splits there itself (coop-leaverace), instead of stopping wherever the Leave found it.
// coop-quittitle: ended, the host's Quit Game ended the session for every player (kLeaveEnded).
void SayLeaving(std::int64_t apart_at = -1, bool ended = false) {
    if (!g_link.up || NetplayMutated("no_leave")) {
        return;
    }

    // The last word: 1 when the run ended on its --frames budget (a test's end), so the other copy can tell a test
    // that ran out from a player who quit; kCatchBit with the tick for a split.
    const std::uint32_t word = ended ? kLeaveEnded : dc::app::GameFrameBudgetSpent() ? 1u : apart_at >= 0 ? kCatchBit | (std::uint32_t) apart_at : 0u;
    Busy                leave{kLeaveMagic, (std::uint32_t) g_link.player, g_link.session, word};

    for (int i = 0; i < 3; i++) {
        NetSocketSend(&leave, sizeof leave);
    }
    g_link.told_session = g_link.session;

    std::fprintf(stderr, "netplay: told player %d this copy is leaving\n", 3 - g_link.player);
}

// coop-catchupdrop (Jep's 2026-11-04 pack 8, third try: his copy gave up her house-out catch-up after kLoadWaitMs with
// nothing heard and closed, telling her nothing; his "not walking yet" Busy had kept her catch-up's quiet clock fresh till
// then, so she sat out kLoadWaitMs more, his Toan at the dungeon's door on her screen): a copy that closes the link during a
// catch-up for its own reasons (the other's silence, its window closed, the test's stop) says so under the link's session
// before it takes a new one, a Leave with kLeaveGaveUp; the other copy closes at once (CatchLost). Not after a SayLeaving of
// this link's (a split's word carries its tick, coop-leaverace). DC_NETPLAY_MUTATE=catch_drop_untold (twin): untold, as before.
void SayGaveUp() {
    if (!g_link.up || !g_catch.active || g_link.told_session == g_link.session || NetplayMutated("no_leave") || NetplayMutated("catch_drop_untold")) {
        return;
    }

    Busy leave{kLeaveMagic, (std::uint32_t) g_link.player, g_link.session, kLeaveGaveUp};

    for (int i = 0; i < 3; i++) {
        NetSocketSend(&leave, sizeof leave);
    }
    g_link.told_session = g_link.session;
    std::fprintf(stderr, "netplay: told player %d this copy gave up the catch-up\n", 3 - g_link.player);
}

// While linked with game.netplay_controller_only, keyboard, text and mouse events never reach the
// queue Kazarov's input reads (an SDL event filter runs before it), so neither copy's game sees them.
// Window events, closing the window among them, still pass.
// With game.netplay_keyboard (userdata non-null) the keyboard's events pass: its keys fold into this copy's pad,
// which the link sends, so both copies see them; the mouse, which also turns this copy's camera alone, stays out.
bool SDLCALL DropKeyboardAndMouse(void *keyboard, SDL_Event *event) {
    switch (event->type) {
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
        case SDL_EVENT_TEXT_INPUT:
        case SDL_EVENT_TEXT_EDITING:
            return keyboard != nullptr;
        case SDL_EVENT_MOUSE_MOTION:
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
        case SDL_EVENT_MOUSE_WHEEL:
            return false;
        default:
            return true;
    }
}

// From the first link on (or the start of Option B's recording), with game.netplay_controller_only.
void ControllersOnly() {
    static bool filtered = false;

    if (!filtered && Settings().controller_only && !NetplayMutated("no_filter")) {
        filtered = true;
        static int keyboard = 1;
        const bool keys = Settings().keyboard && !NetplayMutated("no_keyboard");

        SDL_SetEventFilter(DropKeyboardAndMouse, keys ? &keyboard : nullptr);
        std::fprintf(stderr, keys ? "netplay: mouse ignored while linked; the keyboard plays this copy's pad (game.netplay_keyboard)\n"
                                  : "netplay: keyboard and mouse ignored while linked (controllers only)\n");
    }
}

InputPadState Neutral();

InputPadState OwnInput(int player) {
    const int     pad = OwnPad(player);
    InputPadState state = pad >= 0 ? InputGetPad(pad) : Neutral();

    CamTestOwnPad(player, state.buttons); // coop-lockpress: the lockpress check's Circle, sent as her controller's
    if (CoopGeoramaParkedPress(player)) {
        state.buttons |= kInputSelect; // coop-stage4 4d: a Georama parked for a merge opens again as it was opened
    }
    return state;
}

InputPadState Neutral() {
    InputPadState pad;
    pad.connected = true;
    return pad;
}

std::uint32_t NewSession() {
    static std::mt19937 random((unsigned) std::chrono::steady_clock::now().time_since_epoch().count() ^ (unsigned) std::random_device{}());
    std::uint32_t       session;

    do {
        session = (std::uint32_t) random();
    } while (session == 0);

    return session;
}

// The link went down: this copy plays on alone and listens for the other copy (a restarted game, or
// the same one back), which then links again from the host's state.
// The join notice (Jep, 2026-10-21): "<name> has joined" in the game's own lettering at the top left for a few seconds,
// the other player's Steam name or address; "<name> has left" when the link goes. Local and draw-only (a wall clock,
// never the game's tick or random generator), so the two copies may show it at different moments.
struct Notice {
    std::string                           text;
    std::chrono::steady_clock::time_point until{};
    bool                                  drawn = false;
};

Notice g_notice;

// The other player as a person reads it: a Steam friend's name, else the address without its port.
std::string PeerName() {
    std::string peer = NetSocketLearnsPeer() ? std::string(NetSocketPeerText()) : Settings().peer;

    if (peer.rfind("steam:", 0) == 0) {
        const char *name = NetSteamName(std::strtoull(peer.c_str() + 6, nullptr, 10));

        return name[0] != '\0' ? std::string(name) : std::string("A Steam friend");
    }

    std::size_t colon = peer.find(':');

    return colon == std::string::npos ? peer : peer.substr(0, colon);
}

// coop-uinotice (Jep's 2026-10-30 playtest, item 35): a split or a merge is not a connect or a disconnect: "P2 (Xiao) has left
// the area" / "has entered the area", the other player by number and the character they play now; "<address> has
// joined" / "has left" only when the link itself comes or goes. DC_NETPLAY_MUTATE=notice_area_as_link (twin): the
// address for those too, as before.
// coop-pt1106-batch (Jep's 2026-11-06 playtest, item 1: "when P1 leaves, P1's screen says P2 left the area", and the other
// way round): the notice names the player who moved, the same on both screens: the leaver at a split (NetplayGoApart's
// mover), the arriver at a merge (the copy that was not there first: the merge's source 2 or 3). A split or merge with no
// mover known names the other player, as before. DC_NETPLAY_MUTATE=notice_names_other (twin): always the other player.
int g_apart_mover = 0; // the player who left at this link's split (NetplayGoApart), 0 none known
int g_merge_word = 0;  // this link's merge source as Begin decided it (MergeSourceWord: 2 she stayed, 3 he did)
bool g_handed_title_pressed = false; // coop-pt1106-batch: the handed window's controller pressed on its title before its first link

std::string AreaNotice(bool entered, int mover) {
    static const char *const names[6] = {"Toan", "Xiao", "Goro", "Ruby", "Ungaga", "Osmond"};
    const int                who = mover >= 1 && mover <= 2 && !NetplayMutated("notice_names_other") ? mover : 3 - g_link.player;
    int                      chara = -1;

    // Player 1's character is the retail one on every copy, player 2's the partner's (both read by player index).
    if (who == 1) {
        chara = mode == GAME_MODE_DUNGEON && UserStatus != nullptr ? (int) UserStatus->cur_chara : 0;
    } else {
        chara = mode == GAME_MODE_DUNGEON ? coop::g_partner.loaded_chara : CoopTownPartnerMember();
    }

    return "P" + std::to_string(who) + " (" + (chara >= 0 && chara < 6 ? names[chara] : "?") + ") has " + (entered ? "entered" : "left") +
           " the area";
}

std::string g_adopt_note; // coop-pt1111o: the host's settings this joiner took, for the joined notice
bool g_last_close_apart = false; // the link last went down by a split (its next coming up is a merge)

void SetNotice(const std::string &text) {
    g_notice.text = text;
    g_notice.until = std::chrono::steady_clock::now() + std::chrono::seconds(6);
    g_notice.drawn = false;
    std::fprintf(stderr, "netplay: notice: %s\n", text.c_str());
}

// coop-natcodemsg (Jep, 2026-10-28: a NAT > Code join that never gets through sat on "joining" for good): 20 s of
// waiting unlinked by a code (the joiner's Hellos, the host's punches), and the screen says so while both go on: the
// joiner's title line becomes the note, the host gets it as a notice (again each minute). Logged once as a "netplay:
// punch:" line and a netplay-diag.jsonl event. No fallback: NAT > Steam is the way that has one. Once per join (a link
// up, or NAT left, ends it). DC_NETPLAY_MUTATE=nat_no_wait_note (twin): never shown or logged.
constexpr const char *kNatWaitNote = "No answer yet. If this keeps up, use Steam instead"; // (coop-mainmenu: the menu's Steam row)
void NatCodeWaitTick() {
    static std::chrono::steady_clock::time_point since{}, again{};
    static bool                                  said = false, done = false;

    const auto now = std::chrono::steady_clock::now();
    const bool waiting = (g_nat == 2 || (g_nat == 1 && !g_nat_friend.empty())) && g_link.listening && !g_link.up && !g_link.dead;

    if (g_nat != 1 && g_nat != 2) {
        since = {};
        said = done = false;
        return;
    }
    if (g_link.up) {
        done = true;
    }
    if (done || !waiting) {
        return;
    }
    // coop-joinbudget (Jep's 11-03 internet join: "use Steam instead" shown while P2 sat in his lobby): an open lobby is the
    // host answering over this path; the 20 s count starts at its close. DC_NETPLAY_MUTATE=nat_note_in_lobby (twin): counted
    // through the lobby, as before.
    if (coop::lobby::Active() && !NetplayMutated("nat_note_in_lobby")) {
        since = {};
        return;
    }
    if (since == std::chrono::steady_clock::time_point{}) {
        since = now;
        return;
    }

    const double waited = std::chrono::duration<double>(now - since).count();

    if (waited < 20.0) {
        return;
    }
    // coop-fx51f: the twin's 20 s said too (a test line, nothing shown), so a test can start the late copy on this moment
    // instead of a fixed wait, the twin's run as the row's.
    if (NetplayMutated("nat_no_wait_note")) {
        if (!said) {
            said = true;
            std::fprintf(stderr, "netplay: punch: test: %.0f s unanswered on NAT > Code (%s), the note not shown (nat_no_wait_note)\n", waited,
                         g_nat == 1 ? "hosting" : "joining");
        }
        return;
    }
    if (g_nat == 2 && std::strncmp(g_link.status, "Joining player 1 at", 19) == 0) {
        std::snprintf(g_link.status, sizeof g_link.status, "%s", kNatWaitNote);
    }
    if (g_nat == 1 && (!said || now >= again)) {
        again = now + std::chrono::seconds(60);
        SetNotice(kNatWaitNote);
    }
    if (said) {
        return;
    }
    said = true;
    std::fprintf(stderr, "netplay: punch: no answer after %.0f s on NAT > Code (%s): shown \"%s\"; still punching\n", waited,
                 g_nat == 1 ? "hosting" : "joining", kNatWaitNote);
    if (g_link.diag != nullptr) {
        std::fprintf(g_link.diag,
                     "{\"t\":%.1f,\"event\":\"punch\",\"text\":\"no answer after %.0f s on NAT > Code; still punching\",\"side\":\"%s\","
                     "\"shown\":\"%s\"}\n",
                     std::chrono::duration<double>(now - g_link.epoch).count(), waited, g_nat == 1 ? "host" : "joiner", kNatWaitNote);
        std::fflush(g_link.diag);
    }
}

// coop-floorsnap: Close() takes a checkpoint on a floor (both halves of this unnamed namespace are one).
void Checkpoint(int floor = -1, bool building = false, bool place = false);
int  RestartTarget(int at_mode, int map_no, int select_map_no);
bool CatchupArmed();
extern bool g_floor_started;
std::uint32_t g_floor_build_rand = 0; // coop-floorsnap: the random state the floor on this copy was built from (NetplayFloorBuilding)
int           g_floor_builds = 0;     // coop-cutscenejoin: floors built so far (NetplayFloorBuilds)
int           g_floor_build_key = -1; // coop-localfix: selectMapNo * 256 + cur_floor of that build (NetplayFloorBuilt)

int  g_links_up = 0;      // links come up so far (DC_NETPLAY_TEST_LATE_JOIN)
bool g_went_down = false; // coop-quitown: the link went down and has not come up again (DC_NETPLAY_TEST_ALONE_LOG)

// coop-quittitle (ApplyQuit, NetplayTakeQuitToTitle): this copy leaves its session for the title at its frame's end; whether
// it is the host's quit (the session over for every player); the title's line for it.
bool        g_quit_title = false;
bool        g_quit_ends = false;
std::string g_quit_note;
std::int64_t g_quit_tick = -1; // the link tick the quit applied on (the take runs at that frame's end, the link's tick already the next)

void Close(const char *why) {
    SayGaveUp(); // coop-catchupdrop: under this link's session, before the new one below
    g_went_down = true;
    // coop-quittitle: the host's Leave said its Quit Game ended the session (a copy that missed the quit's tick: still in its
    // catch-up, or the host's last pads lost on the way): this copy goes to the title too, as the quit's tick would take it.
    if (g_link.peer_left && g_link.peer_ended && !g_quit_title) {
        g_quit_title = true;
        g_quit_ends = true;
        g_quit_tick = -1; // (no quit tick here: the Leave's)
        g_quit_note = "The host quit: the session has ended";
        std::fprintf(stderr, "netplay: quit: the host's Leave says it ended the session (heard at tick %lld): this copy (player %d) goes to the title\n",
                     (long long) g_link.tick, g_link.player);
    }
    g_last_close_apart = std::strncmp(why, "went apart", 10) == 0 && !NetplayMutated("notice_area_as_link");
    SetNotice(g_last_close_apart ? AreaNotice(false, g_apart_mover) : PeerName() + " has left");
    g_apart_mover = 0;
    std::fprintf(stderr, "netplay: link down at tick %lld: %s; this copy plays on alone and waits for player %d\n", (long long) g_link.tick,
                 why, 3 - g_link.player);
    g_link.up = false;
    g_keepalive_on = false;
    g_link.listening = true;
    g_link.session = NewSession();
    if (NetSessionNow() == NetSession::Linked || NetSessionNow() == NetSession::Rejoining) { // coop-session (a split said Apart first)
        NetSessionTo(NetSession::Dropped, why);
    }
    // The next link (its Hello carries the delay) starts from the least delay and a fresh round trip,
    // not from what this one grew to.
    if (!NetplayMutated("stale_rtt")) {
        g_link.delay = g_link.least_delay;
        g_link.rtt_avg_ms = -1.0;
    }
    g_link.hello_due = std::chrono::steady_clock::now();
    std::snprintf(g_link.status, sizeof g_link.status, "Link lost: waiting for player %d", 3 - g_link.player);

    // coop-floorsnap: the host left on a dungeon floor under control (player 2 went apart or dropped) takes a checkpoint
    // there and records on, so player 2 can come back onto this floor by its snapshot (the floor's own start was linked,
    // so it took none). DC_NETPLAY_MUTATE=no_leave_checkpoint (twin): as before.
    if (g_link.player == 1 && mode == GAME_MODE_DUNGEON && gameTask == GAME_TASK_PLAY && UserStatus != nullptr && CatchupArmed() &&
        !NetplayMutated("no_leave_checkpoint")) {
        g_floor_started = true;
        Checkpoint(UserStatus->cur_floor);
    }
}

void Receive();
void CatchReceiverTick(); // coop-joinbudget (below, with Receive)
void TakePrelinkPieces(); // coop-small1b (below, with the receiver)

// coop-replayload (train 48's red house-out merges: his copy read 31 of her checkpoint's first 32 pieces while still
// listening, her link already up, and dropped them; she sent them again only once overdue, 1000 ms, and the "loss" backed
// her pace off for the state set's parts after): a catch-up's pieces under the session of the Hello this copy is about to
// link with are kept until it links, then taken. DC_NETPLAY_MUTATE=pieces_before_link_dropped (twin,
// l-local-play-houseout-stall~prelink): dropped, as before.
std::vector<Chunk> g_prelink_pieces;
std::uint32_t      g_prelink_session = 0;
int                g_prelink_seen = 0; // pieces of the coming link read before it (the test hook's)
bool               g_hello_asked = false; // coop-areacard: a Hello asked for now (NetplayHelloNow), not on the clock

// The way to the other copy, from its address: this PC, Tailscale (100.64.0.0/10), a home network, or
// the internet.
const char *PathTo(const std::string &peer) {
    unsigned a = 0, b = 0;

    if (peer.rfind("steam:", 0) == 0) {
        return "steam";
    }

    std::sscanf(peer.c_str(), "%u.%u", &a, &b);

    if (a == 127) {
        return "loopback";
    }

    if (a == 100 && b >= 64 && b < 128) {
        return "tailscale";
    }

    if (a == 10 || (a == 192 && b == 168) || (a == 172 && b >= 16 && b < 32)) {
        return "lan";
    }

    return "internet";
}

// Twice a second, a line of netplay-diag.jsonl in the save folder: the link's health since the last.
void Diagnose() {
    auto now = std::chrono::steady_clock::now();

    if (g_link.diag == nullptr || now < g_link.diag_due) {
        return;
    }

    g_link.diag_due = now + std::chrono::milliseconds(500);

    double seconds = std::chrono::duration<double>(now - g_link.rate_at).count();

    if (g_link.rate_tick > 0 && seconds > 0.0) {
        g_link.tick_rate = (double) (g_link.tick - g_link.rate_tick) / seconds;
    }

    g_link.rate_tick = g_link.tick;
    g_link.rate_at = now;

    long long sent = 0;
    long long dropped = 0;

    NetSocketCounts(sent, dropped);

    // Inputs take a one-way trip (half the round trip) and must be there by their tick.
    double tick_ms = 1000.0 / dc::platform::ConfigGet().tick_rate;
    int    suggest = g_link.rtt_avg_ms < 0.0 ? -1 : (int) std::ceil(g_link.rtt_avg_ms / 2.0 / tick_ms) + 1;

    std::fprintf(g_link.diag,
                 "{\"t\":%.1f,\"tick\":%lld,\"player\":%d,\"path\":\"%s\",\"delay\":%d,\"suggest_delay\":%d,\"rtt_ms\":%.1f,"
                 "\"rtt_avg_ms\":%.1f,\"stalls\":%lld,\"stall_ms\":%.1f,\"stall_max_ms\":%.1f,\"resends\":%lld,\"sent\":%lld,"
                 "\"received\":%lld,\"dropped_injected\":%lld,\"resyncs\":%d,\"desync\":%s,\"ticks_per_s\":%.1f,\"adaptive\":%s}\n",
                 std::chrono::duration<double>(now - g_link.epoch).count(), (long long) g_link.tick, g_link.player,
                 PathTo(NetSocketLearnsPeer() ? std::string(NetSocketPeerText()) : Settings().peer), g_link.delay, suggest, g_link.rtt_ms, g_link.rtt_avg_ms, g_link.stalls,
                 g_link.stall_ms, g_link.stall_max_ms, g_link.resends, sent, g_link.received, dropped, g_link.resyncs,
                 g_link.desync_tick >= 0 ? "true" : "false", g_link.tick_rate, g_link.adaptive ? "true" : "false");
    std::fflush(g_link.diag);
    g_link.stall_max_ms = 0.0;
}

// coop-georamaload: her join by her state, his Toan where his Hello says he walks in this town's outside: his copy may have
// walked about the town since its arrival (the join waited for her Georama). DC_NETPLAY_MUTATE=join_toan_at_arrival (twin):
// at his town's arrival spot (CoopTownPreJoinHere), as before.
bool HelloStand(float *toan, float *yaw) {
    if (g_link.peer_hello.has_stand == 0 || g_link.peer_hello.stand_map != MapNo || EdInteriorFlag != 0 || NetplayMutated("join_toan_at_arrival")) {
        return false;
    }
    for (int i = 0; i < 3; i++) {
        toan[i] = g_link.peer_hello.stand_pos[i];
    }
    *yaw = g_link.peer_hello.stand_yaw;
    std::fprintf(stderr, "catchup: her merge: Toan where his copy last said he walks (%.0f %.0f %.0f)\n", toan[0], toan[1], toan[2]);
    return true;
}

NetplayHello HelloMake(bool fresh); // (below; coop-linkdir's line in Begin)

// A new link with the other copy, whose Hello has come: the two must agree (netplay_hello.hpp); the
// joiner takes the host's input delay; the link starts from tick 0, and unless both copies are fresh
// at one state the host sends its own first (a resync on tick delay + 30, NetplayTakeRestart).
// coop-stayerhello (x-floor-join-visuals, x-floor-join-reverse-latefall): the merge's source as one copy decides it: 1 the
// host's state, 2 player 2 stayed (her state), 3 player 1 stayed (his, player 2 arriving). Each copy decided alone, by its
// own bit as its last Hello gave it and the other's last Hello: a bit that changed with a Hello in flight left them on two
// sources (the host's said 1 and now 0, hers 1: a tie on her copy, hers on his). Now the host decides, by its bit as it is
// at the link and her last Hello, and says so in the Hello it links with (link_source); the joiner links only on that Hello
// (Listen) and takes its word. DC_NETPLAY_MUTATE=joiner_decides (twin): each by its own view, as before.
int StayerNow(); // (below)

int MergeSourceWord(const NetplayHello &mine) {
    const bool old = NetplayMutated("joiner_decides");

    // coop-loadpull: the merge after player 1's load apart goes by his state wherever either copy was first, whatever stayer
    // bit a Hello carries (both copies know of the load before either is ready for it: CoopWorldsLoadPull).
    if (CoopWorldsLoadPull()) {
        return 1;
    }

    if (g_link.player == 2 && !old && g_link.peer_hello.link_source >= 1 && g_link.peer_hello.link_source <= 3) {
        return g_link.peer_hello.link_source;
    }

    const int stay = g_link.player == 1 && !old ? StayerNow() : (int) mine.stayer;

    if (stay == (int) g_link.peer_hello.stayer || !StayerMergeOn()) {
        return 1;
    }
    // (this copy stayed: its own player's state; else the other's)
    const int source = stay != 0 ? g_link.player : 3 - g_link.player;

    return source == 2 ? 2 : 3;
}

bool Begin(const NetplayHello &mine) {
    // A host told no address answers whoever said Hello.
    if (NetSocketLearnsPeer()) {
        std::fprintf(stderr, "netplay: player %d is at %s\n", 3 - g_link.player, NetSocketAnswerHello());
    }
    // A NAT code's host found on the LAN (Listen): play goes to where its Hello came from.
    if (const char *lan = NetSocketTakeLanSender()) {
        g_punch.lan = true;
        std::fprintf(stderr, "netplay: nat: linked over the LAN: player %d is at %s\n", 3 - g_link.player, lan);
    }

    // coop-linkdir: who stayed, as each copy decides the merge's source by it (below): this copy's bit as it links with it, as
    // it is now (a Hello made anew) and as its last Hello from listening said it (-1: none), and the other's.
    NetplayTiming("the merge's direction decided");
    std::fprintf(stderr, "netplay: merge direction: this copy (player %d) links saying stayer %d (now %d, its last Hello %d), player %d's Hello says %d, "
                         "the host's word %d, this copy goes by %d (1 the host's state, 2 hers, 3 his as the stayer)\n",
                 g_link.player, (int) mine.stayer, (int) HelloMake(false).stayer, g_link.sent_hello_set ? (int) g_link.sent_hello.stayer : -1,
                 3 - g_link.player, (int) g_link.peer_hello.stayer, g_link.player == 1 ? MergeSourceWord(mine) : (int) g_link.peer_hello.link_source,
                 MergeSourceWord(mine));

    std::string refusal = NetplayHelloCompare(mine, g_link.peer_hello);
    bool        adopted = false;
    // coop-stayerhello: the merge's source, as the host decides it and tells the joiner in the Hello it links with.
    const int   word = MergeSourceWord(mine);

    // Settings alone apart: the joiner takes the host's (both copies see the same Hellos, so both know),
    // and the session starts from the host's state, so the game takes them up as the host's place loads.
    if (!refusal.empty() && NetplayHelloSettingsOnly(mine, g_link.peer_hello)) {
        if (g_link.player == 2) {
            NetplayHelloAdopt(g_link.peer_hello);
            g_adopt_note = NetplayHelloAdoptNote(mine, g_link.peer_hello); // coop-pt1111o: said in the joined notice
            std::fprintf(stderr, "netplay: took the host's settings (%s)\n", refusal.c_str());
        } else {
            std::fprintf(stderr, "netplay: player 2 takes this copy's settings (%s)\n", refusal.c_str());
        }

        refusal.clear();
        adopted = true;
    }

    // Ours goes out a few more times, so the other copy can say why too, and again with each tick
    // until the other copy's ticks come.
    g_link.my_hello = mine;
    g_link.my_hello.link_source = g_link.player == 1 ? (std::uint8_t) word : 0;

    // Test hook (DC_NETPLAY_TEST_LINK_HELLO_LOST=1, the host; coop-lossylink): a link after the first sends none of these five,
    // as when the loss took them all while her copy held back (x-pagediff-floor-lossy, train 50): only CatchHostTick's again.
    static const bool link_hello_lost = TestEnvOn("DC_NETPLAY_TEST_LINK_HELLO_LOST");

    if (link_hello_lost && g_link.player == 1 && g_links_up >= 1) {
        std::fprintf(stderr, "netplay: test: this link's first Hellos lost (DC_NETPLAY_TEST_LINK_HELLO_LOST)\n");
    } else {
        for (int i = 0; i < 5; i++) {
            NetSocketSend(&g_link.my_hello, sizeof g_link.my_hello);
        }
    }

    if (!refusal.empty()) {
        std::fprintf(stderr, "netplay: link refused: %s\n", refusal.c_str());
        std::snprintf(g_link.status, sizeof g_link.status, "Link refused: %s", refusal.c_str());
        g_link.last_peer_session = g_link.peer_hello.session;
        g_link.dead = !NetplayHelloSameBuild(mine, g_link.peer_hello) || g_link.peer_hello.protocol != mine.protocol || // coop-buildid
                      !NetplayHelloRegionsLink(mine, g_link.peer_hello);                                                 // release2
        return false;
    }

    if (g_link.player == 2 && g_link.peer_hello.delay != g_link.delay) {
        std::fprintf(stderr, "netplay: input delay %d ticks, the host's\n", g_link.peer_hello.delay);
        g_link.delay = g_link.peer_hello.delay;
    }

    // A host recording alone (game.netplay_catchup) keeps its tick and its pads' routing and plays on; the joiner catches
    // up to it (Catch). The no_catchup mutation keeps the quiet resync instead. A coop-worlds merge at a dungeon's entrance
    // keeps the resync too: the host's floor play while apart turned on what it heard of the other copy (its exit gate held
    // for it, a hold given up), which its recorded pads cannot replay (w-world-split -Case next-floor -Online; twin
    // DC_WORLDS_MUTATE=catchup_merge). A town merge catches up (w-world-merge-catchup).
    // coop-floorsnap: a floor join (both apart on one floor) catches up by the host's snapshot instead (CoopWorldsFloorJoin,
    // SnapshotJoinOn), so player 2 lands in his floor as he has it; DC_WORLDS_MUTATE=floor_join_resync (twin) keeps the reload.
    const bool          floor_join = CoopWorldsFloorJoin() && !CoopWorldsMutated("floor_join_resync");

    // coop-localfix2 (Jep's 2026-10-26: "if P2 is in the area before P1, wouldnt P1 have to catch up?"): a merge in a town
    // where one copy stayed and the other arrives goes by the stayer's state, the arriving copy catching up behind its own
    // door's fade; both Hellos say who stayed, so both copies decide alike. Else the host's (DC_NETPLAY_MUTATE=host_always_sends).
    g_link.source = word == 2 ? 2 : 1;
    g_merge_word = word;
    if (CoopWorldsLoadPull()) {
        std::fprintf(stderr, "catchup: player 1 loaded a save apart: the merge goes by his state, player 2 takes it where he is (Hellos say stayer %d, %d)\n",
                     (int) mine.stayer, (int) g_link.peer_hello.stayer);
    }
    g_link.stayer_merge = false;
    g_link.arriving = false;
    if (word >= 2) {
        g_link.stayer_merge = g_link.source == 2;
        g_link.arriving = g_link.source != g_link.player;
        g_snap.want = g_snap.want || g_link.stayer_merge; // coop-samedoorfix: her place checkpoint (below) goes by snapshot
        // coop-dungeonjoin: on a floor, the one that was on it first (CoopWorldsFloorFirst); coop-houseenter: or was there
        // first, a building too.
        std::fprintf(stderr, "catchup: player %d stayed in the %s: the merge goes by its state, player %d catches up\n", g_link.source,
                     mode == GAME_MODE_DUNGEON ? "floor (it was there first)" : EdInteriorFlag != 0 ? "building" : "town", 3 - g_link.source);

        // Player 2's copy records from here (a place checkpoint, below): what it kept from its own last catch-up (a replay,
        // spent) is not a recording.
        // coop-stage4 4c: this copy's retail town menu through the merge: the sender's goes on local, before her roles swap; the taker's parks.
        if (mode == GAME_MODE_EDIT) {
            if (SendsState()) {
                CoopScriptMenuRetailToLocal(); // coop-small2: a script's menu opened apart, likewise
                CoopTownMenuRetailToLocal(g_link.player);
                if (g_link.player == 2) {
                    CoopGeoramaHerToOwn(); // (coop-stage4 r4: hers goes on as her own, carried; his as the stayer goes on in the set)
                }
            } else if (TakesState()) {
                CoopTownMenuPark(g_link.player);
                CoopGeoramaPark(g_link.player);
            }
        }
        if (SendsState() && g_link.player == 2) {
            g_solo = Solo{};

            // Her skin (the game's player walking as her) becomes the partner where she stands, Toan where his copy has him:
            // the state the merge goes by is then the pair's, as a merge on the host's makes it.
            float toan[3];
            float yaw = 0.0f;

            // coop-dungeonjoin: on a floor, her member becomes the partner where she stands, his where his copy has him.
            if (mode == GAME_MODE_DUNGEON) {
                if (!CoopWorldsFloorBecomePartner()) {
                    std::fprintf(stderr, "catchup: her floor merge: her member could not become the partner\n");
                }
            } else if (CoopWorldsHerEventCarries() && (CoopWorldsPeerStand(toan, &yaw) || HelloStand(toan, &yaw) || CoopTownPreJoinHere(toan, &yaw))) {
                CoopTownSwapDefer(toan, yaw, g_link.player); // coop-stage4 4e part 2: her own talk or event goes on on her skin; the swap at its end
            } else if (!(CoopWorldsPeerStand(toan, &yaw) || HelloStand(toan, &yaw) || CoopTownPreJoinHere(toan, &yaw)) || !CoopTownBecomePartner(toan, yaw)) {
                std::fprintf(stderr, "catchup: her merge: player 1's place unknown or no skin here; Toan stays where her skin walked\n");
            }
        }
        // His copy takes hers: its own recording (a checkpoint of its town's arrival) is not what it catches up from.
        // (Her copy taking his too: coop-doublereset's drop below, which this one now makes first; taker_keeps_solo keeps both.)
        if (TakesState() && !NetplayMutated("taker_keeps_solo")) {
            TakerDropsSolo();
        }
        // coop-cutscenejoin: her arrival on his floor: she is to stand where she came onto it (the floor's entrance), not
        // beside him; his state set, taken after this, carries it to her copy.
        if (SendsState() && g_link.player == 1 && floor_join) {
            CoopWorldsArriverStand();
        }
    }
    // coop-dungeonjoin: this copy arrived on the other's floor and takes its state: its window stays black until its lockstep
    // (NetplayArrivalHold).
    g_link.floor_arrival = floor_join && TakesState() && CoopWorldsArrivingOnFloor(10 * 60 * 1000);
    // coop-doublereset: any copy that takes the other's state drops what it kept of its own: her copy, the sender at their
    // last (stayer) merge, still held that merge's place checkpoint, and at her next merge (she followed him into the
    // mayor's house) CatchJoinTick left for it before his checkpoint came: out to Norune from the room (zone RESET), then in
    // again by his door (a second). DC_NETPLAY_MUTATE=taker_keeps_solo (twin): kept, as before.
    if (TakesState() && !NetplayMutated("taker_keeps_solo")) {
        TakerDropsSolo();
    } else if (TakesState() && (g_solo.on || !g_solo.checkpoint.empty())) {
        std::fprintf(stderr, "catchup: this copy takes the other's state and keeps its own %scheckpoint (taker_keeps_solo)\n", g_solo.place ? "place " : "");
    }
    const bool          merge = CoopWorldsApart() && mode == GAME_MODE_DUNGEON && !CoopWorldsMutated("catchup_merge") && !floor_join;

    // coop-bldgsnap: a town or building merge with no recording here (the last link spent it, and player 2 alone went into a
    // building or out of one while this copy stayed: p2bags, outback) went by the resync, which reloaded the zone on both
    // copies. With snapshots it catches up from a place checkpoint taken now instead, the host's state set going over hers
    // where she stands (if the set is refused, the pads replay from it: the lockstep's checksum then resyncs, as before).
    // DC_NETPLAY_MUTATE=no_place_checkpoint (twin): the resync, as before.
    // coop-dungeonjoin: her floor merge (she was on the floor first) records from a place checkpoint on her floor likewise.
    const bool her_floor = g_link.source == 2 && mode == GAME_MODE_DUNGEON && floor_join && UserStatus != nullptr;
    // coop-b2join (x-floor-scene2: he went down a gate alone, then she followed into his B2 scene): a join in this copy's floor
    // scene sends from a place checkpoint on that floor too when its own recording is not of it. Off a gate (no mode end)
    // there was none at all, and the link fell back to the resync (her copy reloaded the dungeon, out of the scene).
    // DC_NETPLAY_MUTATE=scene_join_resync (twin): as before.
    const bool scene_send = g_link.source == g_link.player && mode == GAME_MODE_DUNGEON && floor_join && UserStatus != nullptr && CoopWorldsSceneJoining() == 1 &&
                            (!g_solo.on || CheckpointFloor(g_solo.checkpoint) >= 0) && !NetplayMutated("scene_join_resync");

    if (scene_send && g_solo.on) {
        g_solo = Solo{}; // (a checkpoint of an earlier floor: not where the scene is)
    }
    if (SendsState() && !g_solo.on && !g_solo.replay && (CoopWorldsApart() || g_link.source == 2) && (mode == GAME_MODE_EDIT || her_floor || scene_send) &&
        (gameTask == GAME_TASK_PLAY || g_link.source != 1 || scene_send) && Catchup() && SnapshotJoinOn() && SnapshotJoinHere() && !NetplayMutated("no_place_checkpoint") &&
        !NetplayMutated("no_catchup")) {
        Checkpoint(her_floor || scene_send ? UserStatus->cur_floor : -1, false, true);
        if (g_solo.due) {
            g_solo.on = true;
            g_link.tick = 0;
            g_link.previous[0] = g_link.previous[1] = Neutral();
            g_link.menu_owner = 1;
            g_link.held = 0;
        }
    }
    const bool          catch_join = SendsState() && g_solo.on && !g_solo.replay && !NetplayMutated("no_catchup") && !merge;
    const std::int64_t  solo_tick = g_link.tick;
    const InputPadState solo_previous[2] = {g_link.previous[0], g_link.previous[1]};
    const int           solo_owner = g_link.menu_owner;
    const std::uint16_t solo_held = g_link.held;

    g_catch = Catch{};
    g_snap = Snap{}; // coop-snapshot stage 4
    g_snap.want = floor_join || g_link.stayer_merge; // coop-samedoorfix: her merge's state set
    // coop-arrivalfull: both copies know a floor join and who takes the state (the source's other), so both play its
    // arrival from the lockstep's first tick.
    // (coop-b1lesson: none for a join in the floor's opening scene: her arrival played before she waited, and the scene goes on.)
    g_link.arrival_walk = floor_join && mode == GAME_MODE_DUNGEON && CoopWorldsSceneJoining() == 0 ? 3 - g_link.source : 0;

    // A fresh session on tick 0.
    for (Slot &slot : g_link.local) {
        slot.frame = -1;
    }

    for (Slot &slot : g_link.remote) {
        slot.frame = -1;
    }

    for (std::int64_t &frame : g_link.sums_frame) {
        frame = -1;
    }

    g_link.tick = 0;
    g_link.first_delay = g_link.delay;
    g_link.script_delay = g_link.delay;
    g_link.newest = g_link.delay - 1;
    g_link.last_input = Neutral();
    g_link.shrink_wait = 0;
    g_link.synced[0] = g_link.synced[1] = g_link.previous[0] = g_link.previous[1] = Neutral();
    g_link.routed[0] = g_link.routed[1] = Neutral();
    g_link.menu_owner = 1;
    g_link.held = 0;
    g_link.last_checksum_frame = -1;
    g_link.desync_tick = -1;
    g_link.resync_tick = -1;
    g_link.resynced_at = -1;
    g_link.joined_at = -1;
    g_link.restart_map = -1;
    g_link.load_hold = false;
    g_link.peer_ticking = false;
    g_link.peer_send_got = false;
    g_link.link_heard = false;
    g_link.last_peer_session = g_link.peer_hello.session;
    g_link.listening = false;
    g_link.up = true;
    g_links_up++;
    g_went_down = false;
    NetSessionTo(NetSession::Linked, g_links_up == 1 ? "the first link" : "linked again"); // coop-session
    if (g_links_up == 1) {
        NetplayTestInputMark("linked"); // (a test's held --input script goes on: DC_TEST_INPUT_HOLD)
    }
    if (g_link.player == 1) {
        coop::lobby::LateJoin(g_link.peer_hello.player_id); // coop-lobby: the joiner's seat and member (plan 4.5)
    }
    g_link.peer_busy_at = {};
    g_link.peer_left = false;
    g_link.peer_left_done = false;
    g_link.peer_gave_up = false;
    g_link.peer_ended = false; // coop-quittitle
    g_link.peer_apart_at = -1;
    g_link.apart_heard = false;
    // coop-exitghost (Jep, 2026-10-27: "leaving the mayor's house for P2 caused a dark outline to stay imprinted on P2's
    // screen for like 3-4 seconds"): the join hold is this link's. It kept the first link's state tick (291 in the test),
    // and the next link's ticks count from 0, so her merge out of the house held her frame (the town's first frame after
    // her door, dark, the sky and the far ground alone) until the new link passed that tick: 4 s after her catch-up.
    // DC_NETPLAY_MUTATE=join_hold_kept (twin): kept, as before.
    if (!NetplayMutated("join_hold_kept")) {
        g_join_hold = JoinHold{};
    }
    g_keepalive_player = (std::uint32_t) g_link.player;
    g_keepalive_session = g_link.session;
    g_last_tick_ms = NowMs();
    g_keepalive_on = true;

    // From the first link on, the keyboard and the mouse reach neither copy (they would steer one alone).
    ControllersOnly();

    // The player's poison bites on a clock that runs from boot, so two copies that played apart bite on different ticks
    // (Jep, 2026-10-21: OUT OF SYNC after a bat's poison). A fresh session starts it at 0 on both; a catch-up join keeps
    // the host's, which the joiner's checkpoint gives it (TakeState). DC_NETPLAY_TEST_POISON_SKEW=N (one copy, tests): N
    // more ticks on its clock, as a host that played before the link. DC_NETPLAY_MUTATE=poison_phase: no reset (twin).
    poison_counter += (int) TestEnvInt("DC_NETPLAY_TEST_POISON_SKEW", 0);

    if (!catch_join && !NetplayMutated("poison_phase")) {
        poison_counter = 0;
    }

    if (!catch_join) {
        if (merge && g_link.player == 1 && g_solo.on) {
            std::fprintf(stderr, "catchup: a merge at a dungeon's entrance: the host sends its state instead (no catch-up)\n");
        }

        g_solo.on = false;

        if (g_link.player == 1 && !NetplayMutated("no_building_checkpoint")) {
            g_solo.due = false; // coop-housemerge: the recording is spent (EnterLockstep)
        }
    }

    // (coop-mergestand: a join from the title gets player 2's stand from the resync's state, TakeState: spot 2 of Toan's, or
    // the host's partner's spot. A host-only call here, before the link's first tick, desynced the pair after a catch-up
    // join on a floor 5 times of 6 although it returned at once in a dungeon; without it, 0 of 9. Not understood; kept out.)

    // Test hook (DC_NETPLAY_TEST_HOST_SENDS=1, both copies; coop-interp199): never alike, so the host sends its state at the
    // first link. Two copies started from one --jump matched here about 1 run in 4 (a boot-time race: equal save data and
    // rand at their Hellos) and then kept floors of their own; x-atla-open's places are the host's floor's.
    static const bool host_sends = TestEnvOn("DC_NETPLAY_TEST_HOST_SENDS");
    bool              alike = !adopted && mine.fresh && g_link.peer_hello.fresh && mine.digest == g_link.peer_hello.digest && !host_sends;

    g_link.awaiting_sync = !alike;

    // coop-stable1 (tests): copies alike from the link's first tick (both --jump to one place, no resync, no catch-up) are an
    // epoch of their own for DC_COOP_MENUPRESS, which counted from a resync's or a catch-up's start alone (w-world-floors
    // -P1Paused: his Start never came).
    if (alike && !catch_join) {
        g_test_epoch++;
        g_test_epoch_resyncs = g_link.resyncs;
    }

    if (!alike && SendsState()) {
        char why[160];

        std::snprintf(why, sizeof why, "link: the copies are not alike (%s%s%s%s%s), no catch-up: the host's state at tick %lld",
                      adopted ? "adopted session" : "", !mine.fresh ? " this copy not fresh" : "", !g_link.peer_hello.fresh ? " peer not fresh" : "",
                      mine.digest != g_link.peer_hello.digest ? " saves differ" : "", host_sends ? " test: host sends" : "",
                      (long long) (g_link.delay + 30));
        ResyncCause(merge ? "merge at a dungeon's entrance: the host's floor play cannot be replayed" : why, "Begin");
        g_link.resync_tick = g_link.delay + 30;
    }

    if (catch_join) {
        g_link.tick = solo_tick;
        g_link.previous[0] = solo_previous[0];
        g_link.previous[1] = solo_previous[1];
        g_link.menu_owner = solo_owner;
        g_link.held = solo_held;
        g_link.resync_tick = -1;
        g_link.awaiting_sync = false;
        g_catch.active = true;
        g_catch.heard = std::chrono::steady_clock::now();
        g_catch.began = g_catch.heard;
        g_catches_begun++;
        std::fprintf(stderr, "catchup: player %d joins by catching up to tick %lld; this copy plays on\n", 3 - g_link.player, (long long) solo_tick);
        TestSceneAtJoin(true);
    }

    NetplayTiming("linked");
    g_arrival.link = NowMs(); // coop-offthread
    std::fprintf(stderr, "netplay: linked with session %08x%s\n", g_link.peer_hello.session,
                 alike ? "" : g_link.source == 1 ? "; the copies start apart, so the host sends its state first" : "; the copies start apart, so player 2 sends its state first");
    CatchReceiverTick(); // coop-joinbudget: the copy that takes the state reads it beside its game thread from here
    std::snprintf(g_link.status, sizeof g_link.status, "Linked: player %d", g_link.player);
    // coop-pt1111o: the menus settings this game plays by (one per game: the host's), on both copies, for the tests and the log reader
    std::fprintf(stderr, "netplay: menus in effect: game.coop_menus %s, game.coop_menu_panel %s\n", coop::MenusSetting() ? "on" : "off", coop::MenuPanelSetting() ? "on" : "off");
    if (g_last_close_apart) { // coop-uinotice: a merge
        // the arriver: player 1 when player 2 stayed (source 2), player 2 when he did (3); the host's state (1) says neither
        SetNotice(AreaNotice(true, g_merge_word == 2 ? 1 : g_merge_word == 3 ? 2 : 0));
    } else {
        SetNotice(g_link.player == 1 ? PeerName() + " has joined" : "Joined " + PeerName() + (g_adopt_note.empty() ? "" : ". The host's settings are used: " + g_adopt_note));
        g_adopt_note.clear();
    }
    g_last_close_apart = false;
    return true;
}

// Test hook (DC_NETPLAY_TEST_LATE_JOIN=N, both copies; coop-exitghost): the first link as when player 2's copy links while
// its town still loads (2 runs in 5 of l-local-play -HerHouseOut, and Jep's): her Hello says she has not stayed anywhere,
// so the host sends, and the host's state set is taken no sooner than link tick N (some 250-340 when it happens by itself).
// coop-repinfix2 test hook (DC_NETPLAY_TEST_HOST_MENU=pretake, TestHostMenuFrame): the take waits for the host's menu to be open.
bool g_test_take_held = false;

int LateJoinTestTick() {
    static const int at = TestEnvInt("DC_NETPLAY_TEST_LATE_JOIN", 0);

    return g_links_up <= 1 ? at : 0;
}

// coop-replayload test hook (DC_NETPLAY_TEST_JOIN_BY_HOST=1, both copies): the first link goes by the host's state, as
// -HostFirst's late join does, but with no tick of its own: her first Hellos say she has not stayed, so the host sends its
// state at its link (the opening a l-local-play row's checks expect, beside Toan) every run.
bool JoinByHostTest() {
    static const bool on = TestEnvOn("DC_NETPLAY_TEST_JOIN_BY_HOST");

    return on;
}

NetplayHello HelloMake(bool fresh) {
    NetplayHello hello = NetplayHelloMake(g_link.player, g_link.delay, g_link.session, fresh);

    if ((LateJoinTestTick() > 0 || JoinByHostTest()) && g_links_up == 0) {
        hello.stayer = 0;
    }
    // Test hook (DC_NETPLAY_TEST_STALE_STAYER=1, player 1; coop-stayerhello, x-floor-join-visuals-stale): apart, this copy's
    // Hellos say it stayed whatever its bit (StayerNow), as a Hello sent a moment before the bit changed: the joiner's view of
    // it stale at every merge, every run.
    if (static const bool stale = TestEnvOn("DC_NETPLAY_TEST_STALE_STAYER"); stale && g_link.player == 1 && g_links_up > 0 && !g_link.up) {
        hello.stayer = 1;
    }
    // coop-p2toan (Jep's LAN pack 7 playtest, 2026-11-02: both bags empty once he linked): player 2's copy at its first link
    // holds no world of the session's, only what it made alone (a Local copy's town from an empty save of its own, before
    // his state ever reached it), so the first link always goes by player 1's state, his loaded save; later links (a merge
    // after a split or a drop) still go by whoever was there first. DC_NETPLAY_MUTATE=first_link_hers (twin): her town as the
    // merge's source at a first link too, as before.
    if (g_link.player == 2 && g_links_up == 0 && hello.stayer != 0 && !NetplayMutated("first_link_hers")) {
        static bool said = false;

        if (!said) {
            said = true;
            std::fprintf(stderr, "netplay: first link: this copy's town is its own (player 1's state never reached it): the link goes by player 1's\n");
        }
        hello.stayer = 0;
    }
    hello.first_link = g_links_up == 0 ? 1 : 0; // coop-session
    return hello;
}

// coop-stayerhello: this copy's stayer bit as it is now (a Hello's, without the stale-stayer test hook), the host's at its link.
int StayerNow() {
    NetplayHello hello = NetplayHelloMake(g_link.player, g_link.delay, g_link.session, false);

    if ((LateJoinTestTick() > 0 || JoinByHostTest()) && g_links_up == 0) {
        hello.stayer = 0;
    }
    return hello.stayer;
}

// coop-linkdir (mp-title-local-hold, 1 run in 3): a link started from listening goes by the stayer bit the other copy last
// heard from this one (netplay_hello.hpp's promise that both copies decide alike). Her Hello said she had not stayed (her
// town still loading); the host linked on it. Her town's arrival then made her the stayer (CoopTownPreJoinHere) before she
// read the host's answer, and a Hello made anew there said she stayed: each copy took itself as the merge's source (the
// host "sends its state first", hers "player 2 sends"), and her join hold never ran. DC_NETPLAY_MUTATE=begin_fresh_hello
// (twin): made anew at the link, as before.
NetplayHello HeardHello() {
    NetplayHello hello = HelloMake(false);

    if (g_link.sent_hello_set && g_link.sent_hello.session == hello.session && !NetplayMutated("begin_fresh_hello")) {
        hello.stayer = g_link.sent_hello.stayer;
    }
    return hello;
}

// coop-upnp: once the router has answered (the port forwarded, found forwarded already, or refused), the waiting line says
// so (once), whether that came before the first tick (Handshake) or while playing alone (Listen).
void ShowUpnpWaiting() {
    static bool shown = false;
    const int   state = NetUpnpState();

    if (!shown && (state == kUpnpMapped || state == kUpnpForwarded || state == kUpnpFailed) &&
        std::strncmp(g_link.status, "Waiting for player", 18) == 0) {
        shown = true;
        SetWaiting(g_link.status, sizeof g_link.status, g_link.player);
        std::fprintf(stderr, "netplay: status: %s\n", g_link.status);
    }
}

// coop-qastop (tests; Jep, 2026-10-29: "every check ends on its log lines"): DC_QA_STOP_FILE=PATH: once that file exists
// the game stops as its frame budget would (GameRequestStop: a clean exit, stdout flushed), so a test ends a run as soon
// as its checks have their lines (CoopTest.psm1 Wait-ChecksThenStop) instead of at a fixed --frames. Looked for every
// 100 ms of the monotonic clock. True once the stop is asked.
// coop-stophang (Jep, l-lobby-local, 2026-11-01: a stop as the lobby's link opened hung past 30 s): it was looked for only
// at a frame's start, and the link's waits inside one frame (the first tick's handshake and lockstep wait, a minute each;
// a catch-up's wait) never let a frame start, so a copy waiting there for a partner that had already stopped waited out
// the minute. Those waits look too, and end as a closed window ends them. DC_NETPLAY_MUTATE=stop_blind_wait (twin): the
// waits do not look, as before.
bool QaStopSeen() {
    static const char *stop_file = TestEnv("DC_QA_STOP_FILE");
    static auto        looked = std::chrono::steady_clock::time_point{};

    if (stop_file == nullptr) {
        return false;
    }
    if (dc::app::GameStopRequested()) {
        return true;
    }
    const auto now = std::chrono::steady_clock::now();

    if (now - looked < std::chrono::milliseconds(100)) {
        return false;
    }
    looked = now;
    if (!std::ifstream(PathsFromUtf8(stop_file)).good()) {
        return false;
    }
    std::fprintf(stderr, "coop-test: stop file %s seen: stopping\n", stop_file);
    dc::app::GameRequestStop();
    return true;
}

// The link's waits inside a frame (coop-stophang): the stop file ends them.
bool QaStopInWait() {
    return !NetplayMutated("stop_blind_wait") && QaStopSeen();
}

// The first ready tick: waits up to a minute for the other copy (both launched together start on
// the same tick), keeping the window alive. False when it never came or the two cannot link.
bool Handshake() {
    NetplayHello mine = HelloMake(true);
    auto         start = std::chrono::steady_clock::now();
    auto         sent = start - std::chrono::seconds(1);

    SetWaiting(g_link.status, sizeof g_link.status, g_link.player);
    std::fprintf(stderr, "netplay: waiting for player %d\n", 3 - g_link.player);

    // Test hook (DC_NETPLAY_TEST_LINK_SECOND=1, coop-linkrace): this copy links once the other copy's link traffic has come
    // too (it Begins first, on this copy's Hello), so that traffic is stamped while this copy listens (w-world-split -LinkSecond).
    static const bool second = TestEnvOn("DC_NETPLAY_TEST_LINK_SECOND");

    if (second) {
        std::fprintf(stderr, "netplay: test: the first link waits for player %d's link traffic\n", 3 - g_link.player);
    }

    while (!g_link.peer_hello_got || (second && !NetplayPeerLinking())) {
        auto now = std::chrono::steady_clock::now();

        if (const auto waited = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count(); waited > FirstWaitMs()) {
            std::fprintf(stderr, "netplay: player %d has not come; this copy plays alone and keeps waiting\n", 3 - g_link.player);
            // coop-slowrows (WS6): how long the first wait held this copy; a test whose host plays alone first reads it
            // (a host left on the minute's default cost every fixture join 60 s before its load).
            std::fprintf(stderr, "netplay: the first wait gave up after %lld ms\n", (long long) waited);
            return false;
        }

        if (now - sent > std::chrono::milliseconds(100)) {
            NetSocketSend(&mine, sizeof mine);
            sent = now;
        }

        SDL_PumpEvents();

        if (SDL_HasEvent(SDL_EVENT_QUIT)) {
            return false;
        }
        if (QaStopInWait()) {
            std::fprintf(stderr, "netplay: the stop file ended the wait for player %d\n", 3 - g_link.player);
            return false;
        }

        Receive();
        ShowUpnpWaiting();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    return Begin(mine);
}

// Playing alone: twice a second a Hello goes to the other copy, and one from a session not yet tried
// starts a link.
void Listen() {
    auto now = std::chrono::steady_clock::now();

    if (g_link.dead) {
        return;
    }

    // The internet address arrived (STUN): the waiting line shows it.
    static bool wan_shown = false;
    int         wan_port = 0;

    if (!wan_shown && NetSocketWan(wan_port)[0] != '\0' && std::strncmp(g_link.status, "Waiting for player", 18) == 0) {
        wan_shown = true;
        SetWaiting(g_link.status, sizeof g_link.status, g_link.player);
        std::fprintf(stderr, "netplay: status: %s\n", g_link.status);
    }
    ShowUpnpWaiting();

    // Joining by a NAT code that carries this PC's own internet address: the host is on this network, and a router
    // that does not loop its own address back drops the punch (two PCs on one LAN, playtest note 24), so the Hellos go
    // to the host's port on the LAN by broadcast as well (the host answers whoever said Hello).
    static bool lan_checked = false;
    int         own_port = 0;
    const char *own_wan = NetSocketWan(own_port);

    if (!lan_checked && g_link.player == 2 && (g_nat == 2 || g_nat == 4) && own_wan[0] != '\0') {
        const std::string host = Settings().peer.substr(0, Settings().peer.rfind(':'));

        lan_checked = true;
        if (host == own_wan && !NetplayMutated("nat_no_lan")) {
            NetSocketLanSearch(Settings().port + 1);
            std::fprintf(stderr, "netplay: nat: the host's code has this PC's internet address (%s): looking on the LAN too, at port %d\n",
                         host.c_str(), Settings().port + 1);
        }
    }

    // Test hook (DC_NETPLAY_TEST_LINK_AFTER_SAID=1, Local's player 2; coop-localrace -HerFirst): no Hellos and nothing read
    // until her copy has said, unlinked, whom it draws and where she stands (CoopWorldsPrelinkSaid; a minute at most), the
    // lines the l-local-play rows' pre-link checks read: on a busy PC she linked within her first second in town, before
    // either (trains 25-36). Gated on those lines, not on a tick count. coop-p2toan: never at the title (a copy joining from
    // there has no town to say anything of; l-lobby -Case handed sets it so that her copy, as before, is first in town).
    static const bool after_said = TestEnvInt("DC_NETPLAY_TEST_LINK_AFTER_SAID", 0) != 0;
    static auto       said_wait = now;

    // coop-slowrows (WS6, Jep): her first link only. After a split her copy is unlinked in town again and has said nothing
    // pre-link (she joined from her title), so the hook kept her Hellos back at his way out until its minute ran out: the
    // merge came 28 s after it (l-local-play-houseout). DC_NETPLAY_MUTATE=said_wait_every_link (twin): at every link, as before.
    if (after_said && g_link.player == 2 && (g_links_up == 0 || NetplayMutated("said_wait_every_link")) && mode != GAME_MODE_TITLE &&
        !CoopWorldsPrelinkSaid() && now - said_wait < std::chrono::seconds(60)) {
        static bool told = false;

        if (!told) {
            told = true;
            std::fprintf(stderr, "netplay: test: her link waits for her copy's identity and spot lines\n");
        }
        return;
    }

    // Test hook (DC_NETPLAY_TEST_LINK_AFTER_TITLE_PRESS=1, the lobby's handed window; coop-pt1106-batch, l-lobby -Case handed):
    // no Hellos and nothing read until its controller has pressed on its title (a minute at most), so the test's presses
    // come before its first link however fast the link is (one run in three linked first). Gated on the press, not a tick.
    static const bool after_press = TestEnvOn("DC_NETPLAY_TEST_LINK_AFTER_TITLE_PRESS");
    static auto       press_wait = now;

    if (after_press && g_link.player == 2 && g_links_up == 0 && !g_handed_title_pressed && now - press_wait < std::chrono::seconds(60)) {
        static bool told = false;

        if (!told) {
            told = true;
            std::fprintf(stderr, "netplay: test: her link waits for a press on her title\n");
        }
        return;
    }

    // coop-joinlobby: a join picked at the title says no Hello while its card is open (the host's game has not accepted its
    // pick yet): the joiner waits on itself only, the host plays on. What comes in is read (the host's table among it), but
    // no Hello of the host's links it meanwhile (below).
    const bool card_holds = g_link.player == 2 && coop::lobby::JoinHoldsHello();

    // coop-areacard (option 5: the arriver's 0.2-0.7 s before its merge started): a test hook, DC_NETPLAY_TEST_NO_TIMED_HELLO=1:
    // apart, no Hello on the half-second clock, only those a change asks for (NetplayHelloNow, the stayer bit's), so a merge
    // that links only on a timed Hello never links (x-merge-hello~timed).
    static const bool no_timed_hello = TestEnvOn("DC_NETPLAY_TEST_NO_TIMED_HELLO");
    const bool        timed_off = no_timed_hello && CoopWorldsApart() && !g_hello_asked;

    if (now >= g_link.hello_due && !card_holds && !timed_off) {
        NetplayHello hello = HelloMake(false);

        // coop-stayerhello's twin (DC_NETPLAY_MUTATE=merge_before_stayer_hello): apart, a bit that became 1 (there first as the
        // other arrived) stays out of the Hellos, so the other copy links on the 0 gone stale, every run.
        if (NetplayMutated("merge_before_stayer_hello") && CoopWorldsApart() && g_link.sent_hello_set && g_link.sent_hello.session == hello.session &&
            hello.stayer > g_link.sent_hello.stayer) {
            hello.stayer = g_link.sent_hello.stayer;
        }
        NetSocketSend(&hello, sizeof hello);
        NetSocketLanBroadcast(&hello, sizeof hello);
        g_link.hello_due = now + std::chrono::milliseconds(500);
        g_link.sent_hello = hello; // coop-linkdir
        g_link.sent_hello_set = true;
        g_hello_asked = false;
    }

    g_link.peer_hello_got = false;
    Receive();

    if (card_holds) {
        static bool told = false;

        if (!told) {
            told = true;
            std::fprintf(stderr, "netplay: the join's Hellos wait for its card (the host's lobby table)\n");
        }
        g_link.peer_hello_got = false;
        return;
    }

    if (now >= g_link.heard_report) {
        if (g_link.heard_report != std::chrono::steady_clock::time_point{}) {
            std::fprintf(stderr, "netplay: listening (player %d, mode %d): heard %lld datagrams, %lld Hellos in 10 s%s\n", g_link.player,
                         (int) mode, g_link.heard_datagrams, g_link.heard_hellos,
                         g_link.peer_hello_got && g_link.peer_hello.session == g_link.last_peer_session ? " (the last one's session is the last link's)" : "");
        }
        g_link.heard_datagrams = g_link.heard_hellos = 0;
        g_link.heard_report = now + std::chrono::seconds(10);
    }

    // coop-housemerge: a host that has taken its checkpoint but is not yet at its tick 0 (the new place still loading)
    // links once it is there, so the join catches up from tick 0 instead of a resync that reloads the host's town too
    // (o-opening-resync, 2 joins of 9 right after the opening). Its Hellos keep the joiner waiting meanwhile.
    // DC_NETPLAY_MUTATE=join_while_loading (twin): links at once, as before.
    // So too a host that has no place yet (the title, a new game's opening: its first checkpoint comes as it arrives in its
    // first town): linked there, the join was a resync postponed to the town's arrival, which reloaded it on both copies
    // (o-opening-resync paced, 2026-10-25).
    const bool loading = g_solo.due && !g_solo.on;
    const bool no_place = !g_solo.due && RestartTarget(mode, MapNo, selectMapNo) < 0; // (the link may have opened already)
    // coop-houseenter: not for a merge by her state (she was there first, his checkpoint is not what the merge goes by): her
    // copy links on his Hello at once, and his waiting for his own building's checkpoint left that link one-sided (her copy
    // linked, his never did, l-local-play -HouseEnter 2 runs of 5). DC_NETPLAY_MUTATE=host_waits_her_merge (twin): waits.
    const bool her_merge = g_link.peer_hello_got && g_link.peer_hello.stayer != 0 && !CoopWorldsSendsMerge() && StayerMergeOn() &&
                           !NetplayMutated("host_waits_her_merge");

    if (g_link.player == 1 && g_link.peer_hello_got && (loading || no_place) && !her_merge && !g_solo.replay && Catchup() &&
        !NetplayMutated("join_while_loading")) {
        static std::int64_t said_for = -1;

        if (said_for != (std::int64_t) g_link.peer_hello.session) {
            said_for = (std::int64_t) g_link.peer_hello.session;
            std::fprintf(stderr, loading ? "netplay: player 2 waits: the checkpoint's place is still loading (the join catches up from its tick 0)\n"
                                         : "netplay: player 2 waits: this copy is in no town or dungeon yet (the join catches up from its first checkpoint)\n");
        }
        return;
    }

    // coop-georamaload (Jep, 2026-10-27: Xiao in the Georama before Toan's first load-in was thrown out of it and his copy never
    // finished loading): her copy waiting to join builds in the Georama (her Hello says so): neither copy starts the link until
    // she closes it, his copy playing on alone meanwhile. Linked there, her state's place checkpoint waited for a walking frame
    // for good (SnapshotQuietTick), behind his black. Unless the other copy has started the link already (a Hello that said
    // otherwise crossed her opening it): one copy linked alone would wait it out. DC_NETPLAY_MUTATE=link_in_georama (twin):
    // the link at once, as before.
    const bool her_builds = g_link.player == 2 ? CoopWorldsPreJoinBuilds() : g_link.peer_hello.builds != 0; // (her last Hello's word)

    if (static bool said = false; her_builds != said) {
        said = her_builds;
        std::fprintf(stderr, her_builds ? "netplay: player 2 builds in the Georama: the join waits until she closes it (this copy plays on; frame %lld)\n"
                                        : "netplay: player 2's Georama is closed: the join may go on (frame %lld)\n",
                     (long long) dc::app::GameFrameCount());
    }
    if (her_builds && g_link.peer_hello_got && !NetplayPeerLinking() && !NetplayMutated("link_in_georama")) {
        return;
    }

    // coop-stayerhello: the joiner links on the host's link alone (the Hello it links with carries the merge's source); its own
    // Begin first decided the source by its view. DC_NETPLAY_MUTATE=joiner_decides (twin): it links on any Hello, as before.
    if (g_link.player == 2 && g_link.peer_hello_got && g_link.peer_hello.link_source == 0 && !NetplayMutated("joiner_decides")) {
        if (static std::uint32_t said = 0; said != g_link.peer_hello.session) {
            said = g_link.peer_hello.session;
            std::fprintf(stderr, "netplay: player 1's Hello (session %08x) is no link of its yet: this copy waits for the host's link\n",
                         (unsigned) g_link.peer_hello.session);
        }
        return;
    }

    // Test hook (DC_NETPLAY_TEST_LINK_AFTER_BUILDS=1, both copies; coop-georamafix): the first link waits until her copy has
    // built in the Georama once (her copy: her own word; his: her Hello's), as when player 1 is still on the title or the
    // load screen while she waits in town. Since coop-mainmenu her copy starts at his Local pick and links within a few of
    // its frames, before her pad is claimed, so l-local-play -HerGeoramaFirst never had her in the Georama first.
    static const bool after_builds = TestEnvInt("DC_NETPLAY_TEST_LINK_AFTER_BUILDS", 0) != 0;
    static bool       built_once = false;

    if (after_builds && !built_once) {
        if (!her_builds) {
            return;
        }

        built_once = true;
        std::fprintf(stderr, "netplay: test: player 2 has built in the Georama: the first link may come once she closes it\n");
    }

    // coop-replayload test hook (DC_NETPLAY_TEST_LINK_AFTER_PIECE=1, both copies): the copy that is to take the state links
    // only once it has read a catch-up piece of the coming link, so the sender's first pieces always come before its link is
    // up (the race the red merges lost by chance). Since coop-stayerhello the joiner links only on the host's linking Hello,
    // whose link_source names the sender, and only it can wait: the host links first, so a host waiting on her pieces never
    // linked (train 49: her copy waited for his link, his for her piece). No clock: the host's own sending ends the wait. The
    // linking Hello it waited past is the one it links on later (the host's stop once it has linked: -HostFirst never linked).
    static const bool after_piece = TestEnvOn("DC_NETPLAY_TEST_LINK_AFTER_PIECE");
    static bool       held_hello = false;
    const bool        takes_next = g_link.player == 2 && (g_link.peer_hello.link_source == 1 || g_link.peer_hello.link_source == 3);
    const bool        hello_now = g_link.peer_hello_got || (after_piece && held_hello);

    if (after_piece && takes_next && hello_now && g_link.peer_hello.session != g_link.last_peer_session && g_prelink_seen == 0) {
        held_hello = true;
        return;
    }

    if (hello_now && g_link.peer_hello.session != g_link.last_peer_session && CoopWorldsMayLink()) { // coop-worlds
        if (after_piece && takes_next) {
            std::fprintf(stderr, "netplay: test: this copy links after %d of the host's pieces came [t %lld]\n", g_prelink_seen, LogMs());
        }
        held_hello = false;
        g_link.peer_hello_got = true; // (linked on the held Hello: the link's traffic filter reads it)
        g_prelink_seen = 0;
        Begin(HeardHello()); // coop-linkdir
    }
}

// Test hook (DC_NETPLAY_TEST_JOIN_WAIT_S=N): a host listening for its first join keeps its --frames budget for up to
// N s of wall clock, so the frames it runs alone (unpaced when headless, about 200 a second) while the joiner boots
// or its test waits (Jep playing, a slow PC) do not count. Without it a title-join test's host could spend its whole
// budget before the joiner picked Join and end cleanly, and the joiner logged "joining player 1" and never linked
// (2026-10-21, about 1 in 3 under load: n-debug-start, t-p2-town-pause, the Triangle menu thread's fast tier). The
// held frames count again from the first link, or after N s. Mutation no_join_hold: the old budget (netplay-joinwait's twin).
void HoldBudgetForJoin() {
    static const int wait_s = TestEnvInt("DC_NETPLAY_TEST_JOIN_WAIT_S", 0);
    static bool      done = false;
    static bool      said = false;
    static std::int64_t                          frames = 0;
    static std::int64_t                          held = 0;
    static std::chrono::steady_clock::time_point until;

    if (done || wait_s <= 0 || g_link.player != 1) {
        return;
    }

    auto         now = std::chrono::steady_clock::now();
    std::int64_t count = dc::app::GameFrameCount();

    if (!said) {
        said = true;
        frames = count;
        until = now + std::chrono::seconds(wait_s);
        std::fprintf(stderr, "netplay: test: the frame budget waits up to %d s for the first join\n", wait_s);
        return;
    }

    if (g_link.up || now >= until) {
        done = true;
        std::fprintf(stderr, "netplay: test: the frame budget held %lld frames for the first join (%s)\n", (long long) held,
                     g_link.up ? "linked" : "no join in time");
        return;
    }

    if (!NetplayMutated("no_join_hold")) {
        dc::app::GameExtendFrameBudget(count - frames);
        held += count - frames;
    }

    frames = count;
}

} // namespace

// Test hook (DC_NETPLAY_TEST_INPUT_FROM_LINK=1): this copy's --input script counts frames from the first link, not from
// boot; it stays at its frame 0 until then. A host runs unpaced while alone (about 200 frames a second), so a press at
// a fixed host frame landed before or after a title join depending on when the joiner came: n-debug-start-ingame's
// L3+R3 at host frame 3600 opened the picker on the host alone whenever the joiner was late (a RAM wait, a loaded PC;
// red on the coop tip on 2026-10-21, the host had held 8710 frames for the join). Mutation no_input_link: boot frames.
namespace {
std::int64_t g_serve_input_base = -1; // coop-serve: the running job's start frame (its input script counts from it), or -1
}

// Test hook (DC_TEST_INPUT_HOLD="F:event[,F:event...]"): the --input script runs to its frame F, then holds there until the
// game marks that event (NetplayTestInputMark), and goes on from F after it: a press that must meet a screen waits for the
// screen, not for a tick count that races it (coop-lobby, 2026-11-01: l-lobby -Case local's load screen came after its
// Crosses). Several holds in frame order, each on its own event (coop-lobbyslots, 2026-11-03: l-local-play -HostPauseAt held
// its Crosses for the load screen and its Start for the link: a fixed-frame Start had cut off the load screen's Crosses).
// Events: "load" (the title's load screen opened by Continue), "lobby" (the host's lobby shown), "linked" (the first link),
// "peer-busy" (apart, the other copy's player in a talk indoors: coop_worlds.cpp's presence), "warp" (the linked test warp,
// DC_WORLDS_TEST_GATE_WARP).
namespace {
struct InputHold {
    std::int64_t at;
    std::string  event;
    bool         marked = false;
};
std::vector<InputHold> g_input_holds;
bool                   g_input_holds_read = false;
} // namespace

void NetplayTestInputMark(const char *event) {
    for (InputHold &hold : g_input_holds) {
        if (!hold.marked && hold.event == event) {
            hold.marked = true;
            std::fprintf(stderr, "netplay: test: input hold: %s marked\n", event);
            return;
        }
    }
}

static std::int64_t TestInputFrameBelowAnchors(std::int64_t frame) {
    static const bool    on = TestEnvOn("DC_NETPLAY_TEST_INPUT_FROM_LINK");
    static std::int64_t linked_at = -1;
    static std::size_t   next = 0;      // the hold the script meets next
    static std::int64_t  offset = 0;    // boot frames spent held so far
    static std::int64_t  held_from = -1; // the boot frame the current hold began

    if (g_serve_input_base >= 0) {
        return frame - g_serve_input_base;
    }
    if (!g_input_holds_read) {
        g_input_holds_read = true;
        for (const char *at = TestEnv("DC_TEST_INPUT_HOLD"); at != nullptr && *at != '\0';) {
            const char *colon = std::strchr(at, ':');
            const char *comma = std::strchr(at, ',');

            if (colon == nullptr || (comma != nullptr && comma < colon)) {
                break;
            }
            g_input_holds.push_back({std::atoll(at), comma != nullptr ? std::string(colon + 1, comma) : std::string(colon + 1)});
            at = comma != nullptr ? comma + 1 : nullptr;
        }
    }
    while (next < g_input_holds.size() && frame - offset >= g_input_holds[next].at) {
        InputHold &hold = g_input_holds[next];

        if (!hold.marked) {
            if (held_from < 0) {
                held_from = frame;
            }
            return hold.at;
        }
        // (marked: on from F, the frames held left out)
        offset = frame - hold.at;
        held_from = -1;
        next++;
        std::fprintf(stderr, "netplay: test: input hold: on from script frame %lld at boot frame %lld\n", (long long) hold.at, (long long) frame);
    }
    if (next > 0) {
        return frame - offset; // (after a hold: as the hold left it; before the first, as before)
    }    if (!on || NetplayMutated("no_input_link")) {
        return frame;
    }
    if (linked_at < 0) {
        if (!g_link.up) {
            return 0;
        }
        linked_at = frame;
        std::fprintf(stderr, "netplay: test: the input script counts from the link (boot frame %lld)\n", (long long) frame);
    }
    return frame - linked_at;
}

// The script frame the pump applied this frame (ScriptInputFor reads the script round it); -1 before the first.
std::int64_t g_script_frame = -1;

// coop-anchors: the script's own @tick and @event anchors hold it on top of the clocks above (input_anchors.hpp).
std::int64_t NetplayTestInputFrame(std::int64_t frame) {
    static int released = 0;

    g_script_frame = CoopInputAnchorsFrame(TestInputFrameBelowAnchors(frame));
    if (CoopInputAnchorsReleases() != released) {
        released = CoopInputAnchorsReleases();
        g_link.script_delay = g_link.delay; // (the lines after it from the delay of now: ScriptInputFor)
    }
    return g_script_frame;
}

namespace {

void CatchReceiverStopAtExit(); // (coop-quitcrash: below, beside the receiver)

// The socket, the keyboard filter and the diagnostics file; NetplayTick then shakes hands.
bool Open() {
    const LinkSettings config = Settings();

    g_link.player = config.netplay;
    g_link.delay = config.delay;
    g_link.least_delay = config.delay;
    // A delay given to the launcher (DC_NETPLAY_DELAY) holds; else it grows with the round trip. The
    // delay_env_adapts mutation lets it grow anyway, for net-fixed-delay's twin.
    g_link.adaptive = (std::getenv("DC_NETPLAY_DELAY") == nullptr || NetplayMutated("delay_env_adapts")) && !NetplayMutated("fixed_delay");
    g_link.session = NewSession();

    if (!NetSocketOpen(config.port + g_link.player, config.peer.c_str(), config.port + 3 - g_link.player)) {
        return false;
    }

    // coop-upnp: a host asks the router to forward its port (in the background; NetplayTitleNat may have asked already).
    if (g_link.player == 1 && UpnpOn()) {
        NetUpnpStart(config.port + 1);
    }

    // One client hosting: this PC's internet address, asked in the background from the link's own port.
    if (config.one_client && (g_link.player == 1 || g_nat == 2 || g_nat == 4) && dc::platform::ConfigGet().netplay_wan_lookup) {
        const char *stun = TestEnv("DC_NETPLAY_STUN");

        NetSocketStunStart(stun != nullptr ? stun : "stun.l.google.com:19302", config.port + g_link.player);
    }

    g_link.epoch = std::chrono::steady_clock::now();

    static bool keepalive = false;

    if (!keepalive) {
        keepalive = true;
        g_keepalive_thread = std::thread(Keepalive); // (coop-cand58: joined by NetplayStopThreads)
        std::atexit([] {
            NetplayStopThreads(); // coop-cand58: (done in main as RunGame returns; this for an exit that skips it)
            NetplayExitSaySends();
            SayLeaving();
            CatchReceiverStopAtExit(); // coop-quitcrash
        });
    }

    std::filesystem::path diag = PathsSaveRoot() / "netplay-diag.jsonl";

    g_link.diag = std::fopen(PathsDisplay(diag).c_str(), "w");
    g_link.diag_due = std::chrono::steady_clock::now();

    std::fprintf(stderr, "netplay: floor-builder guards %s\n", PortBuildMapGuardsPlaced() ? "in place" : "NOT in place");
    NetplayHelloLogVersion(); // coop-buildid
    // coop-pt1108s (Jep's 11-08 playtest: "peer 1.2.3.4:47201:47201"): a peer that names its own port (a typed "ip:port", a NAT
    // code's address) shows it once; the socket always took that port (NetSocketOpen), so this was the line only.
    // DC_NETPLAY_MUTATE=peer_port_twice (the twin): the usual port appended again, as before.
    {
        const std::size_t colon = config.peer.rfind(':');
        const bool        named = colon != std::string::npos && config.peer.rfind("steam:", 0) != 0 && !NetplayMutated("peer_port_twice");
        const std::string usual = named ? std::string() : ":" + std::to_string(config.port + 3 - g_link.player);

        std::fprintf(stderr, "netplay: player %d, listening on %d, peer %s%s, input delay %d ticks\n", g_link.player, config.port + g_link.player,
                     config.peer.c_str(), usual.c_str(), g_link.delay);
    }

    if (Catchup()) {
        std::fprintf(stderr, "catchup: on: this host records its play, so a join catches up instead of reloading it\n");
    }

    // The co-op features this copy plays with (its config and the launcher's environment), for the launcher audit
    // (l-launcher-features). Catch-up is the host's only.
    std::fprintf(stderr, "netplay: features: worlds %s, menus %s, cameras %s, own bags %s, catch-up %s\n",
                 TestEnvIs("DC_COOP_WORLDS", "0") ? "off" : "on", coop::MenusOn() ? "on" : "off",
                 coop::IndependentCamerasSetting(dc::platform::ConfigGet()) ? "on" : "off", coop::OwnBags() ? "on" : "off",
                 Catchup() ? "on" : "off");
    return true;
}

// FNV-1a over the state the two copies must agree on: the player's and the partner's positions, the
// party's life, the task, the event mode, the random generator, and each monster's state, life and
// position.
struct Hash {
    std::uint32_t value = 2166136261u;

    void Add(const void *data, std::size_t size) {
        const auto *bytes = static_cast<const unsigned char *>(data);

        for (std::size_t i = 0; i < size; i++) {
            value = (value ^ bytes[i]) * 16777619u;
        }
    }

    template <typename T>
    void Add(const T &item) {
        Add(&item, sizeof item);
    }
};

// The parts of the checksum, logged when the copies part so the two logs show which differed first.
struct Parts {
    std::uint32_t rand = 0;
    int           task = 0;
    float         camera = 0.0f;
    float         camera2 = 0.0f; // coop: cameras
    std::uint32_t view = 0;       // coop: cameras
    float         player[3] = {};
    float         partner[3] = {};
    float         town[3] = {};
    std::uint32_t monsters = 0;
    std::uint32_t hits = 0; // coop-p2absent: the live hits (NowColData)
    float         partner_out[4] = {}; // coop-pt1108b: whether she is shown outdoors, and where; said with a desync
    int           partner_left = 0, partner_revive = 0;
    std::uint32_t stages[3] = {}; // the checksum as it stood after the settings, the partner, pad 2
    // repin (Jep, 2026-11-10): what goes in between the first two stages besides the status, said with a desync (t-triangle-menu-menudown:
    // stage 2 alone parted): the event script's fields, motionDrive's counter, the weapon fx slot, the melt timer.
    std::uint32_t event_sum = 0;
    int           heal = 0, weapon_fx = 0, melt_frame = 0, melt_pending = 0, melt_on = 0;
    std::uint32_t status = 0;
    int           menu_owner = 1;
    std::uint32_t in_menu = 0; // coop-localmenu
    std::uint32_t present = 0; // coop-opt7
    int           partner_chara = -1;
    int           partner_down = 0;
    int           partner_choice = -1;
    bool          georama_two = false;
    std::uint32_t p2_bag = 0; // coop-inventory
    bool          combat_panel = false;
    int           combat_lock = -1;
    std::uint32_t menus = 0;  // coop-menus
    std::uint32_t floor = 0;  // coop: gaps2 (the floor's chests, events, trap circles and map)
    std::uint32_t shots = 0; // pad 2's shots in the air
    std::uint32_t save = 0;  // the SaveData
    std::uint32_t town_modes = 0; // coop-town: CoopTownModeSum
    float         town_cam[6] = {}; // coop-localfix: the town camera's eye and look (logged only, not summed)
    float         view_m[16] = {};  // coop-localfix: the view matrix summed as "view" (logged only)
};

Parts g_parts;
Parts g_parts_at[8];

// The party status as each checksum saw it, written out beside the parts when the copies part
// (netplay-desync-p<player>-<tick>.bin in the save folder) so the two files show which bytes differ.
std::vector<unsigned char> g_status_at[8];

// The floor's slots as each checksum saw them, as text (chests, events, trap circles: every slot, in use or free), written
// beside the status dump when the copies part (netplay-desync-floor-p<player>-<tick>.txt), so a floor-part desync
// shows which slot differs.
std::string g_floor_text;
std::string g_floor_at[8];
// coop-floorsnap: the SaveData as each checksum saw it (SaveSum's bytes, config and play time blanked), written out when
// the copies part on the save part alone (netplay-desync-save-p<player>-<tick>.bin), so cmp names the field.
std::vector<unsigned char> g_save_bytes;
std::vector<unsigned char> g_save_at[8];
// coop-floorsnap: the monsters as each checksum saw them (state, hp, where), appended to the floor text, so a desync in
// the monsters part alone (w-world-split next-floor, 2026-10-24, one run in eight) names the monster that differs.
std::string g_monsters_text;
// coop-joindesync: the live hits (every active slot, as summed) and the settings stage's inputs at full precision, appended to
// the floor text, so a hits-only or stage-0 desync on a floor (Jep's 2026-10-28e B1 ticks 23100, 25980) names its field.
std::string g_hits_text;
std::vector<unsigned char> g_status;

// The SaveData's hash but for its config, play time and menu cursor (Checksum, and the save and load test hooks' logs).
std::uint32_t SaveSum(const unsigned char *bytes = nullptr) {
    static std::vector<unsigned char> save;
    Hash                              sum;
    const unsigned char              *from = bytes != nullptr ? bytes : (const unsigned char *) SaveData;

    save.assign(from, from + sizeof(CSaveData));
    BlankOwnSaveParts(save.data());
    sum.Add(save.data(), save.size());
    return sum.value;
}

std::uint32_t CarriedHash(); // coop-carried (below, with the list)

std::uint32_t Checksum() {
    CoopPerfScope perf(kCoopPerfChecksum); // coop: perf
    Hash  hash;
    float partner[4] = {0.0f, 0.0f, 0.0f, 0.0f};

    g_parts = Parts{};
    g_parts.rand = PortRandState();
    g_parts.task = gameTask;
    hash.Add(PortRandState());

    // The camera's angle: the stick is read against it, so a camera turned on one copy alone (by the
    // mouse) sends that copy's players another way.
    // The dungeon's state counts only in a dungeon: a copy that was on a floor before keeps it after it
    // has followed the host to a town where the host never fought.
    const bool dungeon = mode == GAME_MODE_DUNGEON;

    if (dungeon && NowCamera__3 != nullptr && CharaFrame != nullptr) {
        g_parts.camera = NowCamera__3->angle;
        hash.Add(NowCamera__3->angle);
    }
    // coop: cameras
    // The partner's own camera, while the cameras are independent: its stick is read against it.
    float partner_camera[4] = {0.0f, 0.0f, 0.0f, 0.0f};

    if (CoopPartnerCamera(&partner_camera[3], partner_camera)) {
        g_parts.camera2 = partner_camera[3];
        hash.Add(partner_camera);
    }

    // The view the last draw left, which the tick's clip tests read (MGClipBox): a copy that drew from
    // its own player's camera puts the game's back (CoopViewEnd).
    Hash view;
    float town_view[4][4];

    // coop-localfix: in a town, the game's camera's view, which the tick reads now (CoopTownTickView), not the last draw's.
    if (mode == GAME_MODE_EDIT && CoopTownCameraView(town_view)) {
        view.Add(town_view);
    } else {
        view.Add(mgRenderInfo.view);
    }
    g_parts.view = view.value;
    std::memcpy(g_parts.view_m, mgRenderInfo.view, sizeof g_parts.view_m);
    if (mode == GAME_MODE_EDIT) {
        CoopTownCameraNow(g_parts.town_cam, g_parts.town_cam + 3);
    }
    hash.Add(view.value);
    if (dungeon) {
        hash.Add(gameTask);
        hash.Add(BtEventMode);
    }

    // coop-carried: the parts a resync carries beside the save (CarriedParts: the settings, the place). The checksum reads
    // them through the list alone, so nothing it checks there can be left behind by a resync.
    hash.Add(CarriedHash());
    g_parts.stages[0] = hash.value; // after the settings

    // The event script under way and what it asked for (BtEventInfo's pointers would differ between
    // two PCs, so field by field).
    if (dungeon) {
        hash.Add(BtEventInfo.script_no);
        hash.Add(BtEventInfo.running_script_no);
        hash.Add(BtEventInfo.chained_script_no);
        hash.Add(BtEventInfo.request);
        hash.Add(BtEventInfo.action_mode);
        hash.Add(BtEventInfo.treasure_box);
        hash.Add(BtEventInfo.script_main_chr);
        Hash event;
        event.Add(BtEventInfo.script_no);
        event.Add(BtEventInfo.running_script_no);
        event.Add(BtEventInfo.chained_script_no);
        event.Add(BtEventInfo.request);
        event.Add(BtEventInfo.action_mode);
        event.Add(BtEventInfo.treasure_box);
        event.Add(BtEventInfo.script_main_chr);
        g_parts.event_sum = event.value;
    }

    // All of the party status: life, water, items, weapons, gold, the floor's state.
    g_status.clear();
    g_floor_text.clear();
    g_monsters_text.clear();
    g_hits_text.clear();
    if (dungeon && UserStatus != nullptr) {
        Hash status;

        status.Add(UserStatus, sizeof *UserStatus);
        hash.Add(status.value);
        g_parts.status = status.value;
        g_status.assign((const unsigned char *) UserStatus, (const unsigned char *) UserStatus + sizeof *UserStatus);

        // coop-p2absent: motionDrive's regeneration counter, a function static on the PS2 that no state
        // carries. Each copy's had counted its own ticks before a join, so the healing weapon's +1 HP
        // came on different ticks (Toan's life 41 against 43). After the life, in the dump.
        hash.Add(MotionDriveHealCounter);
        const unsigned char *heal = (const unsigned char *) &MotionDriveHealCounter;
        g_status.insert(g_status.end(), heal, heal + sizeof MotionDriveHealCounter);

        // coop-kocatchup: the slot the next elemental hit lights (CWeaponElFx), retail's function static: a catch-up join
        // that left it each copy's own parted random at her first wind hit, a few hundred ticks on. After the counter.
        const int weapon_fx = PortWeaponFxSlot();
        hash.Add(weapon_fx);
        const unsigned char *fx = (const unsigned char *) &weapon_fx;
        g_status.insert(g_status.end(), fx, fx + sizeof weapon_fx);

        // coop-guard: the bag's melt timer (ItemVolumeStep: the floor's step ticks it on every copy, a menu's melt and close reach
        // every copy by command, coop_fullmenu.cpp MeltTimer). Parted, ice and fish melt on different ticks. After the slot.
        hash.Add(ItemVolumeStep.frame);
        hash.Add(ItemVolumeStep.pending_volume);
        hash.Add(ItemVolumeStep.enabled);
        g_parts.heal = (int) MotionDriveHealCounter;
        g_parts.weapon_fx = weapon_fx;
        g_parts.melt_frame = (int) ItemVolumeStep.frame;
        g_parts.melt_pending = (int) ItemVolumeStep.pending_volume;
        g_parts.melt_on = (int) ItemVolumeStep.enabled;
        const unsigned char *melt = (const unsigned char *) &ItemVolumeStep;
        g_status.insert(g_status.end(), melt, melt + sizeof ItemVolumeStep);
    }

    // coop-inventory: player 2's bag and gilda, which the party status does not hold; in the dump after
    // the status.
    if (coop::OwnBags()) {
        g_parts.p2_bag = coop::InventoryHash();
        hash.Add(g_parts.p2_bag);
        coop::InventoryBytes(g_status);
    }

    // coop-menus: both players' overlays, only with game.coop_menus on (the checksum is unchanged off).
    if (coop::MenusOn()) {
        g_parts.menus = coop::MenuHash();
        hash.Add(g_parts.menus);
    }

    // In a town, where the dungeon's player and monsters stand still, the town's player.
    if (mode == GAME_MODE_EDIT && MainChara.frame != nullptr) {
        float position[4];

        MainChara.GetPosition(position);
        std::memcpy(g_parts.town, position, sizeof g_parts.town);
        hash.Add(position[0]);
        hash.Add(position[1]);
        hash.Add(position[2]);
        // coop-soakfix5: the walk's camera swing behind Toan (EdMoveChara's rot_count, ebattle.cpp), which the step turns the
        // town camera by: a copy left with its own showed only once the camera had turned, if it turned at all.
        hash.Add(EdMoveCharaRotCount);

        // coop-inbuild: inside a building, player 2's character too (both copies step her walk from the shared pad).
        if (float partner[3]; CoopTownIndoorPose(partner)) {
            hash.Add(partner[0]);
            hash.Add(partner[1]);
            hash.Add(partner[2]);
        }

        // coop-joindesync: outdoors too, whether she is shown and where (both copies step her walk from pad 2 there as well).
        // Unsummed, a join that placed her apart on her copy showed only once Toan's walk met her body on one copy (Jep's
        // 2026-10-28e Steam session, 5 to 90 units apart). DC_NETPLAY_MUTATE=town_partner_unsummed: not summed, as before.
        if (EdInteriorFlag == 0 && !NetplayMutated("town_partner_unsummed")) {
            float      outdoors[3] = {0.0f, 0.0f, 0.0f};
            const bool shown = coop::TownPartnerShown(outdoors, nullptr);

            g_parts.partner_out[0] = shown ? 1.0f : 0.0f;
            std::memcpy(g_parts.partner_out + 1, outdoors, sizeof outdoors);
            hash.Add(shown ? 1 : 0);
            hash.Add(outdoors[0]);
            hash.Add(outdoors[1]);
            hash.Add(outdoors[2]);
        }
    }

    // coop-town: the town's mode and each player's own pause, Georama and menus (CoopTownModeSum). The town's player
    // stands still through them, so a copy that alone paused or built (a press one copy lost) kept the rest in sync.
    if (mode == GAME_MODE_EDIT) {
        g_parts.town_modes = CoopTownModeSum();
        hash.Add(g_parts.town_modes);
    }

    g_parts.menu_owner = g_link.menu_owner;
    hash.Add(g_link.menu_owner);
    // coop-localmenu: every player's in-menu bits (the step reads them for standing still, the pause, the stand-aside).
    g_parts.in_menu = coop::ActorMenuBits();
    hash.Add(g_parts.in_menu);
    // coop-opt7: every player's own presentation (the step reads it: the opener stands still, out of harm, counts as paused).
    g_parts.present = coop::PresentHash();
    hash.Add(g_parts.present);

    // The partner's member, pad 2's pick and its time lying down (each member's), and who has the
    // Georama editor: state the positions and life above miss until it moves someone.
    const coop::Partner &partner_state = coop::g_partner;

    g_parts.partner_chara = partner_state.loaded_chara;
    g_parts.partner_down = partner_state.down;
    g_parts.partner_choice = partner_state.choice;
    g_parts.georama_two = CoopGeoramaPartnerHolds();
    hash.Add(partner_state.loaded_chara);
    hash.Add(partner_state.down);
    hash.Add(partner_state.choice);
    g_parts.partner_left = (int) partner_state.down_left;
    g_parts.partner_revive = (int) partner_state.revive;
    hash.Add(partner_state.down_left);
    hash.Add(partner_state.revive);
    g_parts.stages[1] = hash.value; // after the partner
    hash.Add(g_parts.georama_two);

    // Pad 2's fighting: its charge, lock-on, aim, item panel and throws (coop::g_combat), field by field.
    const coop::Combat &combat = coop::g_combat;

    g_parts.combat_panel = combat.panel;
    g_parts.combat_lock = combat.lock;
    hash.Add(combat.buttons);
    hash.Add(combat.cross_down);
    hash.Add(combat.held);
    hash.Add(combat.charging);
    hash.Add(combat.charge_ticks);
    hash.Add(combat.charge_level);
    hash.Add(combat.charged_blow);
    hash.Add(combat.scale);
    hash.Add(combat.charges);
    hash.Add(combat.lock);
    hash.Add(combat.circle_ticks);
    hash.Add(combat.aiming);
    hash.Add(combat.aim_yaw);
    hash.Add(combat.aim_pitch);
    hash.Add(combat.item);
    hash.Add(combat.item_wait);
    hash.Add(combat.panel);
    hash.Add(combat.page);
    hash.Add(combat.row);
    hash.Add(combat.shot_scale);
    hash.Add(combat.chara);
    hash.Add(combat.hurt); // coop-p2hurt
    hash.Add(combat.blow);

    for (const coop::Throw &thrown : combat.throws) {
        hash.Add(thrown.pos);
        hash.Add(thrown.vector);
        hash.Add(thrown.item);
        hash.Add(thrown.life);
    }
    // Pad 2's own shots in the air (coop::Partner::shots; Ruby's ring magic flies in the game's own shots).
    Hash shots;

    if (!NetplayMutated("no_shot_sum")) {
        for (const coop::Shot &shot : partner_state.shots) {
            shots.Add(shot.pos);
            shots.Add(shot.vector);
            shots.Add(shot.damage);
            shots.Add(shot.life);
        }
    }

    g_parts.shots = shots.value;
    hash.Add(shots.value);

    // The SaveData (the story, the towns, the Georama, the stock, the clock), but for each copy's own
    // settings (config, which the settings hash covers as both copies share them) and the play time, which
    // counts on through loading screens as long as each PC takes, and the menu cursor, each player's own menu's.
    g_save_bytes.clear();
    if (SaveData != nullptr && !NetplayMutated("no_save_sum")) {
        g_parts.save = SaveSum();
        hash.Add(g_parts.save);
        g_save_bytes.assign((const unsigned char *) SaveData, (const unsigned char *) SaveData + sizeof(CSaveData));
        BlankOwnSaveParts(g_save_bytes.data());
    }

    g_parts.stages[2] = hash.value; // after pad 2's fighting, its shots and the SaveData

    if (dungeon && CharaFrame != nullptr) {
        std::memcpy(g_parts.player, CharaMain.pos, sizeof g_parts.player);
        hash.Add(CharaMain.pos[0]);
        hash.Add(CharaMain.pos[1]);
        hash.Add(CharaMain.pos[2]);
    }

    if (CoopPartnerTarget(partner, nullptr, nullptr, nullptr)) {
        std::memcpy(g_parts.partner, partner, sizeof g_parts.partner);
        hash.Add(partner[0]);
        hash.Add(partner[1]);
        hash.Add(partner[2]);
    }

    Hash monsters;

    if (dungeon && NowMonstorUnit != nullptr && BtAllDrawFlag != 0) {
        for (int i = 0; i < 16; i++) {
            const MONSTOR &monster = NowMonstorUnit->monster[i];
            float          position[4];

            hash.Add(monster.state);
            hash.Add(monster.hp);
            monsters.Add(monster.state);
            monsters.Add(monster.hp);

            char mline[160];

            if (monster.state == 2) {
                NowMonstorUnit->chara[i][0].GetPosition(position);
                hash.Add(position[0]);
                hash.Add(position[2]);
                monsters.Add(position[0]);
                monsters.Add(position[2]);
                std::snprintf(mline, sizeof mline, "monster %d state %d hp %d at (%.3f %.3f %.3f)\n", i, (int) monster.state, (int) monster.hp,
                              position[0], position[1], position[2]);
            } else {
                std::snprintf(mline, sizeof mline, "monster %d state %d hp %d\n", i, (int) monster.state, (int) monster.hp);
            }

            g_monsters_text += mline;
        }
    }

    g_parts.monsters = monsters.value;

    // Test hook (DC_NETPLAY_TEST_MONSTER_DUMP=N, T24 desync A): the monsters' records, whole but for their two pointers, at
    // each of the first N checksums on a floor while linked (monsters-p<player>-<tick>.bin in the save folder), so a test
    // compares the two copies' records byte for byte even on a run whose monsters stay in step (w-world-split next-floor).
    if (static const int dumps = (int) TestEnvInt("DC_NETPLAY_TEST_MONSTER_DUMP", 0); dumps > 0 && dungeon && g_link.up && NowMonstorUnit != nullptr &&
                                                                                      UserStatus != nullptr && UserStatus->cur_floor >= 0 && BtAllDrawFlag != 0) {
        static int dumped = 0;

        if (dumped < dumps) {
            dumped++;
            char name[64];
            std::snprintf(name, sizeof name, "monsters-p%d-%lld.bin", g_link.player, (long long) g_link.tick);
            std::ofstream out(PathsSaveRoot() / name, std::ios::binary);

            for (const MONSTOR &monster : NowMonstorUnit->monster) {
                MONSTOR copy;

                std::memcpy((void *) &copy, (const void *) &monster, sizeof copy); // padding and all
                copy.collision_poly = nullptr;
                copy.lockon_frame = nullptr;
                out.write((const char *) &copy, (std::streamsize) sizeof copy);
            }
        }
    }

    // coop-p2absent: the live hits (NowColData), which decide whose blows land. Their fields but the
    // monster-table pointer, which differs between processes.
    Hash hits;

    if (dungeon && NowColData != nullptr) {
        for (int i = 0; i < 96; i++) {
            if (NowColData->active[i] == 0) {
                continue;
            }

            const COLLISION_HIT &hit = NowColData->hit[i];

            hits.Add(i);
            hits.Add(NowColData->active[i]);
            hits.Add(hit.pos[0]);
            hits.Add(hit.pos[1]);
            hits.Add(hit.pos[2]);
            hits.Add(hit.damage);
            hits.Add(hit.radius);
            hits.Add(hit.life);
            hits.Add(hit.target_mask);
            hits.Add(hit.kind);
            hits.Add(hit.flags);
            hits.Add(hit.owner);
            hits.Add(hit.monster_no);
            hits.Add(hit.attack_no);
            hits.Add(hit.phase);
            hits.Add(hit.ready_phase);

            char hline[256];

            std::snprintf(hline, sizeof hline,
                          "hit %d active %d at (%.4f %.4f %.4f) damage %d radius %.4f life %d mask %d kind %d flags %d owner %d monster %d attack %d phase %d/%d\n",
                          i, (int) NowColData->active[i], hit.pos[0], hit.pos[1], hit.pos[2], (int) hit.damage, hit.radius, (int) hit.life,
                          (int) hit.target_mask, (int) hit.kind, (int) hit.flags, (int) hit.owner, (int) hit.monster_no, (int) hit.attack_no,
                          (int) hit.phase, (int) hit.ready_phase);
            g_hits_text += hline;
        }

        hash.Add(hits.value);
    }

    g_parts.hits = hits.value;

    // coop: gaps2
    // The floor's own state, which the positions and the party status miss until it moves someone:
    // each chest (there, shut, its item, kind and trap), each map event's kind (an opened chest, a
    // taken Atla, a woken mimic) and the trap circles. A copy that opened a chest or sprang a circle
    // the other did not shows here first. Not the mini-map's cells: they are drawn only, and the
    // player's reveal runs on a count of its own (DunMoveChara's static cnt) that a resync does not
    // put back. DC_NETPLAY_MUTATE=no_floor_sum leaves it out, for the floor-sum test's twin.
    if (mode == GAME_MODE_DUNGEON && NowDngMap != nullptr && BtAllDrawFlag != 0 && !NetplayMutated("no_floor_sum")) {
        Hash floor;
        // Only the slots in use: a floor's start marks the chests and circles free but leaves the rest of each
        // slot as the last floor had it, so a copy that played on before a resync keeps other leftovers than
        // one that did not (DC_NETPLAY_MUTATE=stale_floor_sum hashes them all, for the twin).
        const bool all = NetplayMutated("stale_floor_sum");

        for (const TREASURE_BOX &box : NowDngMap->boxes) {
            floor.Add(box.used);

            if (box.used == 0 && !all) {
                continue;
            }

            floor.Add(box.closed);
            floor.Add(box.item_no);
            floor.Add(box.kind);
            floor.Add(box.trap_no);
        }

        for (const DUNGEON_EVENT &event : NowDngMap->events) {
            floor.Add(event.kind);
        }

        for (const MAP_TRAP_CIRCLE &circle : NowDngMap->trap_circle) {
            floor.Add(circle.state);

            if (circle.state == 0 && !all) {
                continue;
            }

            floor.Add(circle.kind);
        }

        g_parts.floor = floor.value;
        hash.Add(floor.value);

        // Every slot, in use or free, for the desync dump (DumpFloor).
        char line[256];

        std::snprintf(line, sizeof line, "floor sum %08x (free slots %s)\n", floor.value, all ? "hashed" : "not hashed");
        g_floor_text = line;

        for (int i = 0; i < (int) std::size(NowDngMap->boxes); i++) {
            const TREASURE_BOX &box = NowDngMap->boxes[i];

            std::snprintf(line, sizeof line, "chest %d used %d closed %d item %d kind %d trap %d\n", i, (int) box.used, (int) box.closed,
                          (int) box.item_no, (int) box.kind, (int) box.trap_no);
            g_floor_text += line;
        }

        for (int i = 0; i < (int) std::size(NowDngMap->events); i++) {
            std::snprintf(line, sizeof line, "event %d kind %d\n", i, (int) NowDngMap->events[i].kind);
            g_floor_text += line;
        }

        for (int i = 0; i < (int) std::size(NowDngMap->trap_circle); i++) {
            const MAP_TRAP_CIRCLE &circle = NowDngMap->trap_circle[i];

            std::snprintf(line, sizeof line, "circle %d state %d kind %d\n", i, (int) circle.state, (int) circle.kind);
            g_floor_text += line;
        }

        g_floor_text += g_monsters_text; // coop-floorsnap: the monsters, for a monsters-part desync

        // coop-joindesync: the settings stage's inputs and the live hits.
        float pcam[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        float pangle = 0.0f;
        bool  has_pcam = CoopPartnerCamera(&pangle, pcam);

        std::snprintf(line, sizeof line, "stage0 task %d event mode %d camera %.6f partner camera %d %.6f (%.4f %.4f %.4f)\n", (int) gameTask,
                      (int) BtEventMode, g_parts.camera, has_pcam ? 1 : 0, pangle, pcam[0], pcam[1], pcam[2]);
        g_floor_text += line;
        std::snprintf(line, sizeof line, "player (%.4f %.4f %.4f) partner (%.4f %.4f %.4f)\n", g_parts.player[0], g_parts.player[1],
                      g_parts.player[2], g_parts.partner[0], g_parts.partner[1], g_parts.partner[2]);
        g_floor_text += line;
        g_floor_text += g_hits_text.empty() ? std::string("hits none\n") : g_hits_text;
    }

    return hash.value;
}

// newest: the last input the packet carries (the kResend before it with it); this copy's newest by default.
void Send(std::int64_t newest = -1) {
    // DC_NETPLAY_DROP_GROWTH: the packets of the tick the input delay first grew on are lost.
    if (g_link.drop_tick >= 0 && g_link.tick == g_link.drop_tick) {
        return;
    }

    Packet packet{};

    packet.magic = kMagic;
    packet.player = (std::uint32_t) g_link.player;
    packet.session = g_link.session;
    packet.checksum_frame = (std::uint32_t) g_link.last_checksum_frame;
    packet.checksum = g_link.last_checksum;
    packet.resync_tick = g_link.player == 1 && g_link.resync_tick >= 0 ? (std::uint32_t) g_link.resync_tick : kNone;

    auto now = std::chrono::steady_clock::now();

    packet.send_ms = (std::uint32_t) std::chrono::duration_cast<std::chrono::milliseconds>(now - g_link.epoch).count();
    // coop-steamjoin: an echo held past what echo_hold_ms carries is no echo. A Steam joiner sent no tick for its 112 s
    // catch-up, the hold stopped at 65535, and its first round trip on the lockstep read 50887 ms (Jep's 2026-10-27 join):
    // its input delay went to 30 ticks and took 10 s to come down. DC_NETPLAY_MUTATE=echo_hold_cap (the twin): echoed so.
    long long hold = g_link.peer_send_got ? std::chrono::duration_cast<std::chrono::milliseconds>(now - g_link.peer_send_at).count() : 0;
    bool      echo = g_link.peer_send_got && (hold <= 65535 || NetplayMutated("echo_hold_cap"));

    packet.echo_ms = echo ? g_link.peer_send_ms : kNone;
    packet.echo_hold_ms = echo ? (std::uint16_t) std::min<long long>(65535, hold) : 0;

    if (newest < 0) {
        newest = g_link.newest;
    }

    // Each packet carries this copy's last kResend inputs, so a lost one only delays them.
    // (The no_resend twin breaks that from tick 600, once the link is up and the game under way.)
    const bool lossy = NetplayMutated("no_resend") && g_link.tick >= 600;
    const int  resend = lossy ? 1 : kResend;

    for (int i = 0; i < kResend; i++) {
        std::int64_t frame = newest - i;
        Entry       &entry = packet.entries[i];

        if (i >= resend || frame < 0 || g_link.local[frame % kRing].frame != frame) {
            entry.frame = 0xFFFFFFFFu;
            continue;
        }

        const InputPadState &pad = g_link.local[frame % kRing].pad;
        entry.frame = (std::uint32_t) frame;
        entry.buttons = pad.buttons;
        entry.lx = pad.left_x;
        entry.ly = pad.left_y;
        entry.rx = pad.right_x;
        entry.ry = pad.right_y;
        entry.command = g_link.local[frame % kRing].command;
    }

    NetSocketSend(&packet, sizeof packet);

    // Until the other copy is past its handshake, it may still wait for this copy's Hello.
    if (!g_link.peer_ticking) {
        NetSocketSend(&g_link.my_hello, sizeof g_link.my_hello);
    }
}

void DumpStatus(std::int64_t frame, const std::vector<unsigned char> &status) {
    if (status.empty()) {
        return;
    }

    char name[64];
    std::snprintf(name, sizeof name, "netplay-desync-p%d-%lld.bin", g_link.player, (long long) frame);
    std::filesystem::path path = PathsSaveRoot() / name;
    std::ofstream         out(path, std::ios::binary);

    out.write((const char *) status.data(), (std::streamsize) status.size());
    std::fprintf(stderr, "netplay: party status at tick %lld written to %s\n", (long long) frame, PathsDisplay(path).c_str());
}

void DumpSave(std::int64_t frame, const std::vector<unsigned char> &save) {
    if (save.empty()) {
        return;
    }

    char name[64];
    std::snprintf(name, sizeof name, "netplay-desync-save-p%d-%lld.bin", g_link.player, (long long) frame);
    std::filesystem::path path = PathsSaveRoot() / name;
    std::ofstream         out(path, std::ios::binary);

    out.write((const char *) save.data(), (std::streamsize) save.size());
    std::fprintf(stderr, "netplay: save data at tick %lld written to %s\n", (long long) frame, PathsDisplay(path).c_str());
}

void DumpFloor(std::int64_t frame, const std::string &floor) {
    if (floor.empty() || NetplayMutated("no_floor_dump")) {
        return;
    }

    char name[64];
    std::snprintf(name, sizeof name, "netplay-desync-floor-p%d-%lld.txt", g_link.player, (long long) frame);
    std::filesystem::path path = PathsSaveRoot() / name;
    std::ofstream         out(path, std::ios::binary);

    out << floor;
    std::fprintf(stderr, "netplay: floor slots at tick %lld written to %s\n", (long long) frame, PathsDisplay(path).c_str());
}

// coop-menulocal: the latest frame whose checksums met (DC_COOP_MENUPRESS_EXIT_CLOSED=compare waits for one past the close).
std::int64_t g_test_compared = -1;

void CompareChecksum(std::int64_t frame, std::uint32_t theirs) {
    g_test_compared = std::max(g_test_compared, frame);
    if (frame <= g_link.resynced_at || g_link.awaiting_sync) {
        return;
    }

    // LOOSENED CHECK (Jep, coop-pilotfix; Jep's rule: named so): two ticks of the sync check are skipped, J and J+1 of a lockstep
    // that ends in a worlds merge, because the copies are apart there by design until the merge places in J+1. Anything that
    // differs only in those two ticks is not reported; a difference that lasts is still caught at the next checksum tick.
    // coop-pilotfix (cand57's town stayer-merge reds, x-town-stayermenu -SaveChooser and -Retail over, OUT OF SYNC at tick 240
    // 1 run in 6 under load): a worlds merge's catch-up replay and its first lockstep ticks are still apart until the merge
    // (PartnerStep: the partner's state parts the copies until then), and the merge's own placing runs in tick J+1 (365 of
    // 365 merges in the logs of 11-09..11-10: "merged at link tick J+1"). A tick's sum is taken as it starts, so the sums of J
    // and J+1 are two apart states, which differ in every catch-up (the partner stage). The host closes the catch-up at its
    // first even tick with her word within the gap, J some 240 to 250 in these rows, so a J or J+1 on a checksum tick (1 in 5
    // there) reported OUT OF SYNC; any other J compared first at the next, merged. Not compared, as a resync's own tick is
    // not. DC_NETPLAY_MUTATE=join_tick_compared (twin): compared, as before.
    if (g_link.joined_at >= 0 && (frame == g_link.joined_at || frame == g_link.joined_at + 1) && !NetplayMutated("join_tick_compared")) {
        std::fprintf(stderr, "netplay: the lockstep's tick %lld is not compared (J+%lld, the merge not placed yet)\n", (long long) frame,
                     (long long) (frame - g_link.joined_at));
        return;
    }

    // coop-saveload: a resync already set (player 1's load from his menu, which each copy took from its own save folder)
    // replaces both copies' state, so a second before it that differs is no news. DC_NETPLAY_MUTATE=compare_pending: as before.
    if (g_link.resync_tick >= 0 && frame < g_link.resync_tick && !NetplayMutated("compare_pending")) {
        return;
    }

    for (int i = 0; i < 8; i++) {
        if (g_link.sums_frame[i] != frame) {
            continue;
        }

        if (g_link.sums[i] == theirs) {
            NetplayTestInSync(frame); // coop-fx51b (tests)
            // coop-qastop (tests): DC_NETPLAY_SYNC_LOG_EVERY=N says "in sync" every N compares (one each 60 ticks; 10, every
            // 600 ticks, by default): with 1 a check that needs both copies alike after its action has it a second later.
            static const int sync_log_every = std::max(1, TestEnvInt("DC_NETPLAY_SYNC_LOG_EVERY", 10));

            if (++g_link.checked % sync_log_every == 0) {
                std::fprintf(stderr, "netplay: in sync at tick %lld\n", (long long) frame);
            }
        } else if (g_link.desync_tick < 0) {
            g_link.desync_tick = frame;
            std::fprintf(stderr, "netplay: OUT OF SYNC at tick %lld (ours %08x, theirs %08x)\n", (long long) frame, g_link.sums[i],
                         theirs);
            std::snprintf(g_link.status, sizeof g_link.status, "OUT OF SYNC at tick %lld", (long long) frame);
            const Parts &parts = g_parts_at[i];
            std::fprintf(stderr,
                         "netplay: this copy at tick %lld: random %08x task %d camera %.4f camera2 %.4f view %08x player (%.2f %.2f %.2f) "
                         "partner (%.2f %.2f %.2f) monsters %08x status %08x menu player %d town (%.2f %.2f %.2f) "
                         "in menu %08x partner member %d down %d choice %d georama player %d p2 bag %08x panel %d lock %d shots %08x save %08x stages %08x %08x %08x menus %08x floor %08x hits %08x town modes %08x\n",
                         (long long) frame, parts.rand, parts.task, parts.camera, parts.camera2, parts.view, parts.player[0], parts.player[1],
                         parts.player[2], parts.partner[0], parts.partner[1], parts.partner[2], parts.monsters, parts.status,
                         parts.menu_owner, parts.town[0], parts.town[1], parts.town[2], parts.in_menu, parts.partner_chara, parts.partner_down,
                         parts.partner_choice, parts.georama_two ? 2 : 1, parts.p2_bag, parts.combat_panel ? 1 : 0, parts.combat_lock,
                         parts.shots, parts.save, parts.stages[0], parts.stages[1], parts.stages[2], parts.menus, parts.floor, parts.hits, parts.town_modes);
            // coop-localfix: the view in full and the town camera it should come from, so a "view" alone apart says which.
            {
                const float *m = parts.view_m;
                const float *c = parts.town_cam;

            // coop-soakfix4: the summed positions exactly (a low bit apart prints alike at two places)
            std::fprintf(stderr, "netplay: this copy's summed positions at tick %lld: player (%a %a %a) partner (%a %a %a)\n", (long long) frame,
                         parts.player[0], parts.player[1], parts.player[2], parts.partner[0], parts.partner[1], parts.partner[2]);
            std::fprintf(stderr, "netplay: this copy's partner at tick %lld: shown %.0f at (%.3f %.3f %.3f), down left %d, revive %d\n", (long long) frame, parts.partner_out[0], parts.partner_out[1], parts.partner_out[2],
                         parts.partner_out[3], parts.partner_left, parts.partner_revive);
            std::fprintf(stderr, "netplay: this copy's stage 2 parts at tick %lld: event %08x heal %d weapon fx %d melt %d %d %d\n", (long long) frame, parts.event_sum,
                         parts.heal, parts.weapon_fx, parts.melt_frame, parts.melt_pending, parts.melt_on);
                std::fprintf(stderr,
                             "netplay: this copy's view at tick %lld: rows (%.4f %.4f %.4f %.4f) (%.4f %.4f %.4f %.4f) (%.4f %.4f %.4f %.4f) (%.3f %.3f %.3f %.4f); "
                             "town camera eye (%.3f %.3f %.3f) look (%.3f %.3f %.3f)\n",
                             (long long) frame, m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8], m[9], m[10], m[11], m[12], m[13], m[14], m[15], c[0],
                             c[1], c[2], c[3], c[4], c[5]);
            }
            DumpStatus(frame, g_status_at[i]);
            DumpFloor(frame, g_floor_at[i]);
            DumpSave(frame, g_save_at[i]);

            // The host sets the tick both copies take its state at; the joiner hears it in the host's
            // packets.
            if (g_link.player == 1 && g_link.resync_tick < 0) {
                char why[160];

                std::snprintf(why, sizeof why, "desync: OUT OF SYNC at tick %lld (ours %08x, theirs %08x)", (long long) frame, g_link.sums[i], theirs);
                ResyncCause(why, "CompareChecksums");
                g_link.resync_tick = g_link.tick + g_link.delay + kResyncLeadTicks;
                std::fprintf(stderr, "netplay: resync at tick %lld\n", (long long) g_link.resync_tick);
            }

            std::snprintf(g_link.status, sizeof g_link.status, "OUT OF SYNC at tick %lld: resyncing", (long long) frame);
        }

        g_link.sums_frame[i] = -1;
    }
}

// coop-ackbatch: the joiner's acknowledgements go once a pass of Receive, not once a piece. Each carries every piece held
// (its bitmap), so the last of a pass tells the host all the others would; once a piece, a real Steam join's joiner sent
// 8,382 of them (2.3 MB, run log 2026-10-28a) for some 8,300 pieces. The pieces of one pass came in together, so none is
// acknowledged later than before. DC_NETPLAY_MUTATE=ack_per_piece (twin): one a piece, as before.
struct AckDue {
    bool          resync = false;
    std::uint32_t resync_tick = 0;
    bool          catchup = false;
    std::uint32_t catch_id = 0;
    bool          catch_all = false;
};

AckDue g_ack_due;

void SendAckNow(std::uint32_t tick) {
    Ack ack{};

    ack.magic = kAckMagic;
    ack.player = (std::uint32_t) g_link.player;
    ack.session = g_link.session;
    ack.tick = tick;
    std::memcpy(ack.held, g_link.resync_held, sizeof ack.held);
    NetSocketSend(&ack, sizeof ack);
}

void SendAck(std::uint32_t tick) {
    if (NetplayMutated("ack_per_piece")) {
        SendAckNow(tick);
        return;
    }

    if (g_ack_due.resync && g_ack_due.resync_tick != tick) {
        SendAckNow(g_ack_due.resync_tick);
    }

    g_ack_due.resync = true;
    g_ack_due.resync_tick = tick;
}

bool AllHeld(std::uint32_t count) {
    for (std::uint32_t i = 0; i < count; i++) {
        if ((g_link.resync_held[i / 32] & (1u << (i % 32))) == 0) {
            return false;
        }
    }

    return true;
}

// DC_NETPLAY_TEST_RTT_SPIKE=from:to:ms[:every] (load-input-drift): the round trip the delay follows (AdaptDelay) reads ms
// longer from link tick from to to, as a busy PC's does (copies stopped hundreds of ms in a tick: 50 to 110 ms on
// 127.0.0.1, train 51), so the delay grows there and shrinks after it on the same ticks every run (on the samples
// themselves it moved the delay on some runs only: samples after a stall are skipped by wall time); with every, again
// each every ticks. RttSpiked: a spike is set at all.
struct RttSpike {
    long long from = -1, to = -1, ms = 0, every = 0;
};

const RttSpike &RttSpikeSet() {
    static const RttSpike spike = [] {
        RttSpike    s;
        const char *at = TestEnv("DC_NETPLAY_TEST_RTT_SPIKE");

        if (at == nullptr || std::sscanf(at, "%lld:%lld:%lld:%lld", &s.from, &s.to, &s.ms, &s.every) < 3) {
            s.from = s.to = -1;
        }
        return s;
    }();
    return spike;
}

bool RttSpiked() {
    return RttSpikeSet().from >= 0;
}

double RttSpikeMs(std::int64_t tick) {
    const RttSpike &s = RttSpikeSet();

    if (s.from < 0 || tick < s.from) {
        return 0.0;
    }
    long long into = s.every > 0 ? (tick - s.from) % s.every : tick - s.from;

    return into < s.to - s.from ? (double) s.ms : 0.0;
}

void ReceiveTick(const Packet &packet) {
    auto now = std::chrono::steady_clock::now();

    g_link.peer_ticking = true;
    g_link.received++;

    if (!g_link.peer_send_got || packet.send_ms > g_link.peer_send_ms) {
        // The other copy sent nothing for kRttStallMs (it was loading): its next echoes may be of packets
        // that waited in its socket meanwhile.
        if (g_link.peer_send_got && packet.send_ms - g_link.peer_send_ms > (std::uint32_t) kRttStallMs) {
            g_link.rtt_quiet_until = now + std::chrono::milliseconds(kRttQuietMs);
        }

        g_link.peer_send_ms = packet.send_ms;
        g_link.peer_send_at = now;
        g_link.peer_send_got = true;
    }

    if (packet.echo_ms != kNone) {
        double sent = (double) packet.echo_ms + packet.echo_hold_ms;
        double rtt = (double) std::chrono::duration_cast<std::chrono::microseconds>(now - g_link.epoch).count() / 1000.0 - sent;
        static const int fake_ms = TestEnvInt("DC_NETPLAY_FAKE_RTT_MS", 0);

        rtt += fake_ms; // a test's round trip on 127.0.0.1

        // DC_NETPLAY_TEST_RTT_FROM=tick (load-input-drift -LateRtt): no sample before link tick tick, as on a full PC
        // (train 53's warm run: the first average at tick 150+, after the fixture load's quiet).
        static const int rtt_from = TestEnvInt("DC_NETPLAY_TEST_RTT_FROM", -1);

        if (rtt >= 0.0 && (now >= g_link.rtt_quiet_until || NetplayMutated("stale_rtt")) && g_link.tick >= rtt_from) {
            if (rtt_from >= 0 && g_link.rtt_avg_ms < 0.0) {
                std::fprintf(stderr, "netplay: test: first round-trip sample at tick %lld (%.0f ms)\n", (long long) g_link.tick, rtt);
            }
            g_link.rtt_ms = rtt;
            g_link.rtt_avg_ms = g_link.rtt_avg_ms < 0.0 ? rtt : g_link.rtt_avg_ms * 0.9 + rtt * 0.1;
        }
    }

    for (const Entry &entry : packet.entries) {
        if (entry.frame == kNone) {
            continue;
        }

        Slot &slot = g_link.remote[entry.frame % kRing];
        slot.frame = entry.frame;
        slot.pad = Neutral();
        slot.pad.buttons = entry.buttons;
        slot.pad.left_x = entry.lx;
        slot.pad.left_y = entry.ly;
        slot.pad.right_x = entry.rx;
        slot.pad.right_y = entry.ry;
        slot.command = entry.command;
    }

    if (packet.checksum_frame != kNone) {
        CompareChecksum(packet.checksum_frame, packet.checksum);
    }

    // The joiner: the host's resync tick, if it is one not yet done.
    if (g_link.player == 2 && packet.resync_tick != kNone && (std::int64_t) packet.resync_tick > g_link.resynced_at &&
        g_link.resync_tick != (std::int64_t) packet.resync_tick) {
        g_link.resync_tick = packet.resync_tick;
        std::memset(g_link.resync_held, 0, sizeof g_link.resync_held);
        g_link.resync_state.clear();
        std::fprintf(stderr, "netplay: the host resyncs at tick %lld\n", (long long) g_link.resync_tick);
    }
}

// A pad as the link carries it (an Entry's buttons and sticks), so a recording plays as a linked copy's would.
InputPadState LinkPad(const InputPadState &pad) {
    InputPadState out = Neutral();

    out.buttons = pad.buttons;
    out.left_x = pad.left_x;
    out.left_y = pad.left_y;
    out.right_x = pad.right_x;
    out.right_y = pad.right_y;
    return out;
}

// coop-steamceiling: Steam's view of the link beside a blob's log line, on both copies (Jep's 2026-10-28 Steam join: two in
// three pieces sent never reached the joiner, which acknowledged almost exactly one copy of each; this says whether Steam
// saw them lost on the way, and whether the way is a Valve relay).
void SteamLinkLine(const char *what, std::uint32_t id) {
    char line[256];

    if (NetSocketSteamLine(line, sizeof line)) {
        std::fprintf(stderr, "%s %u, Steam: %s\n", what, id, line);
    }
}

// The host: a new blob's pieces start with the window the last blob of this catch-up ended with (one link, one catch-up).
// coop-replayspeed: each of the state set's 2 MB parts began again at kPieceWindowStart, so each took some seven rounds of
// the window doubling, one round a frame; with it kept, the parts after the first go in one or two (Jep's house merge:
// her 8 MB state set held his black for ~70 of her ticks). DC_NETPLAY_MUTATE=window_per_blob (twin): a fresh window each.
// The last blob's sending is logged as it is replaced: the rounds (calls that sent pieces) it took, its l-local-play check.
void NextCatchPieces() {
    const PieceSender &last = g_catch.out_pieces;
    const int          window = NetplayMutated("window_per_blob") ? kPieceWindowStart : last.window;

    if (g_catch.out_id > 1 && last.began != std::chrono::steady_clock::time_point{}) {
        // coop-steamrate: and the blob's bytes over that time, the rate it reached the other copy at (Jep's two-PC joins read here).
        // coop-pagediff: the time ends when the blob was all held, not when the next began (the page list's next waits on the
        // taker's landing).
        const auto      ended = g_catch.out_held_at != std::chrono::steady_clock::time_point{} ? g_catch.out_held_at : std::chrono::steady_clock::now();
        const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(ended - last.began).count();

        std::fprintf(stderr, "catchup: blob %u went in %u rounds, %u pieces (%u again), %lld ms; window %d; delivered %.2f MB/s\n", g_catch.out_id - 1,
                     last.rounds, last.sends, last.again, ms, last.window, ms > 0 ? (double) last.bytes / 1048576.0 / ((double) ms / 1000.0) : 0.0);
        if (last.queue_rate > 0) {
            std::fprintf(stderr, "catchup: blob %u over Steam: its rate %d KB/s, its queue at most %d KB; paced at most %d KB/s, delivered at most %d KB/s, "
                                 "%u back-offs, %u refused as full\n",
                         g_catch.out_id - 1, last.queue_rate / 1024, last.queue_peak / 1024, last.pacer.pace_peak / 1024, last.pacer.delivered_peak / 1024,
                         last.pacer.backoffs, last.refused);
        } else if (last.pacer.direct) {
            std::fprintf(stderr, "catchup: blob %u direct: paced at most %d KB/s, delivered at most %d KB/s, %u back-offs, acknowledged in %.0f ms (straying %.0f; the link's round trip %.0f ms), overdue %d ms\n",
                         g_catch.out_id - 1, last.pacer.pace_peak / 1024, last.pacer.delivered_peak / 1024, last.pacer.backoffs, last.pacer.ack_ms, last.pacer.ack_dev_ms, g_link.rtt_avg_ms,
                         last.pacer.overdue_ms);
        }
        // coop-offthread: where its sends ran (x-town-stayermenu reads it)
        std::fprintf(stderr, "catchup: blob %u sends: %u on the game thread, %u on the send thread; the game thread's longest send call %.1f ms\n",
                     g_catch.out_id - 1, last.sends_here, last.sends_off, last.call_ms_max);
        SteamLinkLine("catchup: blob", g_catch.out_id - 1);
    }

    // coop-steamrate: the pace carries on too (its samples start again with the new blob's pieces).
    PiecePacer pacer = last.pacer;

    pacer.sample_at = pacer.token_at = std::chrono::steady_clock::time_point{};
    pacer.pace_peak = pacer.delivered_peak = 0;
    pacer.backoffs = 0;
    g_catch.out_pieces = PieceSender{};
    g_catch.out_pieces.window = window;
    g_catch.out_pieces.pacer = pacer;
    g_catch.out_held_at = {};
}

// The host: the next blob, its pads from..to (runs of one pad: count, buttons, sticks), the checkpoint first if asked,
// and J if it is the last.
void CatchBlob(std::int64_t from, std::int64_t to, bool checkpoint, std::int64_t join, bool snap_follows = false) {
    std::vector<unsigned char> &out = g_catch.out;
    auto put = [&](std::uint32_t value, int bytes) {
        for (int i = 0; i < bytes; i++) {
            out.push_back((unsigned char) (value >> (8 * i)));
        }
    };

    out.clear();
    put((std::uint32_t) from, 4);
    put((std::uint32_t) to, 4);
    put(snap_follows ? kSnapFollows : join < 0 ? kNone : (std::uint32_t) join, 4); // coop-floorjoin2: kSnapFollows
    put(checkpoint ? (std::uint32_t) g_solo.checkpoint.size() : 0, 4);

    if (checkpoint) {
        out.insert(out.end(), g_solo.checkpoint.begin(), g_solo.checkpoint.end());
        g_catch.deltas_sent = 0;
    }

    // coop-worlds: player 2's progress the recording holds and no blob has carried yet: all of it with the checkpoint, then
    // what came in since. Player 2's copy goes on sending her changes from apart until the lockstep, and one the host applied
    // after the checkpoint had gone was in its play and not in her replay (T24 desync B, w-world-split -Case merge-catchup
    // about one run in three on a loaded PC: SaveData 0x12A0, Matataki's plot she had built, at the first checksum).
    // DC_NETPLAY_MUTATE=checkpoint_deltas_only (twin): only the checkpoint's blob carries them, as before.
    const std::size_t first = g_catch.deltas_sent;
    const std::size_t count = checkpoint || !NetplayMutated("checkpoint_deltas_only") ? g_solo.deltas.size() - first : 0;

    put((std::uint32_t) count, 4);

    for (std::size_t i = first; i < first + count; i++) {
        put((std::uint32_t) g_solo.deltas[i].tick, 4);

        for (std::int32_t value : g_solo.deltas[i].value) {
            put((std::uint32_t) value, 4);
        }
    }

    g_catch.deltas_sent = first + count;

    // The last blob, in a town: the town's Georama in the host's SaveData as it is at J, its live Georama written in first,
    // for the joiner to take there (EnterLockstep). That alone: the rest of the SaveData is the replay's, and on a floor
    // (or with her own bag swapped in) the host's whole SaveData put her copy's dungeon state out from under its floor
    // (bf806f0: P2's copy crashed in CFrame::GetLWMatrix at a floor-1 catch-up's lockstep). DC_NETPLAY_MUTATE=no_join_save
    // (twin): none.
    // (coop-weaktwins: the floor case's twin, join_whole_save, sent the whole SaveData again; it is gone. A floor join takes
    // the host's state set since coop-floorsnap, so the host's whole SaveData at J was what her copy held already and the
    // twin passed: o-opening-resync -CrashOnly has no twin now.)
    SV_GEORAMA_DATA *town = mode == GAME_MODE_EDIT && SaveData != nullptr && MapNo >= 0 && MapNo < 200 ? SaveData->GetGrdData(MapNo) : nullptr;
    const bool       with_save = join >= 0 && town != nullptr && !NetplayMutated("no_join_save");

    if (with_save) {
        CoopTownGeoramaToSave();
    }

    put(with_save ? (std::uint32_t) MapNo : kNone, 4);
    put(with_save ? (std::uint32_t) sizeof *town : 0, 4);

    if (with_save) {
        out.insert(out.end(), (const unsigned char *) town, (const unsigned char *) town + sizeof *town);
    }

    // coop-joinlobby: the last blob of a join from the host: the joiner's pick (its card's, LateJoin): its member and its
    // profile's bag, which both copies give at J (EnterLockstep). Not a merge by her state (her copy sends then).
    g_catch.pick_set = false;
    g_catch.pick_bag.clear();
    if (join >= 0 && g_link.player == 1) {
        g_catch.pick_set = coop::lobby::JoinPickTake(g_catch.pick_member, g_catch.pick_name);
        if (g_catch.pick_set) {
            coop::ProfileBagsJoinBag(g_catch.pick_name, g_catch.pick_bag);
        }
    }
    put(g_catch.pick_set ? (std::uint32_t) g_catch.pick_member : kNone, 4);
    {
        char name[16] = {};

        std::snprintf(name, sizeof name, "%s", g_catch.pick_set ? g_catch.pick_name.c_str() : "");
        out.insert(out.end(), (const unsigned char *) name, (const unsigned char *) name + sizeof name);
    }
    put((std::uint32_t) g_catch.pick_bag.size(), 4);
    out.insert(out.end(), g_catch.pick_bag.begin(), g_catch.pick_bag.end());
    if (g_catch.pick_set) {
        std::fprintf(stderr, "catchup: the last blob carries the joiner's pick: %d (%s), its bag %s\n", g_catch.pick_member,
                     g_catch.pick_name.empty() ? "no profile" : g_catch.pick_name.c_str(), g_catch.pick_bag.empty() ? "not changed" : "given");
    }

    // coop-localfix2: the sender's own pad (player 2's for her merge; the other's is at rest in a recording).
    const std::size_t lane = g_link.source == 2 ? 1 : 0;

    for (std::int64_t tick = from; tick < to;) {
        const InputPadState &pad = g_solo.pads[(std::size_t) tick * 2 + lane];
        std::int64_t         run = 1;

        while (tick + run < to && std::memcmp(&g_solo.pads[(std::size_t) (tick + run) * 2 + lane], &pad, sizeof pad) == 0) {
            run++;
        }

        put((std::uint32_t) run, 4);
        put(pad.buttons, 2);
        put(pad.left_x, 1);
        put(pad.left_y, 1);
        put(pad.right_x, 1);
        put(pad.right_y, 1);
        tick += run;
    }

    g_catch.out_id++;
    std::memset(g_catch.out_held, 0, sizeof g_catch.out_held);
    NextCatchPieces();
    g_catch.sent_to = to;
    g_catch.join_tick = join;
    std::fprintf(stderr, "catchup: blob %u: ticks %lld to %lld%s, %zu bytes%s [t %lld]\n", g_catch.out_id, (long long) from, (long long) to,
                 checkpoint ? " with the checkpoint" : "", out.size(), join >= 0 ? ", the last: the lockstep starts there" : "", LogMs());
}

// The host: part `part` of `parts` of the snapshot as a blob (CatchBlob's header with kSnapMark).
void SnapBlob(std::size_t part, std::size_t parts, const unsigned char *data, std::size_t length, std::uint32_t mark = kSnapMark) {
    std::vector<unsigned char> &out = g_catch.out;
    auto put = [&](std::uint32_t value) {
        for (int i = 0; i < 4; i++) {
            out.push_back((unsigned char) (value >> (8 * i)));
        }
    };

    out.clear();
    put(mark);
    put((std::uint32_t) part);
    put((std::uint32_t) parts);
    put((std::uint32_t) length);
    put((std::uint32_t) g_snap.tick);
    out.insert(out.end(), data, data + length);
    g_catch.out_id++;
    std::memset(g_catch.out_held, 0, sizeof g_catch.out_held);
    NextCatchPieces();
    std::fprintf(stderr, "catchup: blob %u: snapshot part %zu of %zu, %zu bytes (tick %lld) [t %lld]%s\n", g_catch.out_id, part + 1, parts, out.size(),
                 (long long) g_snap.tick, LogMs(), mark == kPageListMark ? " (coop-pagediff: the page list)" : mark == kPageDiffMark ? " (coop-pagediff: the pages)" : "");
}

// How long the host waits for a piece's acknowledgement before it sends the piece again: the round trip and a
// margin, 250 ms before the link has measured one.
int PieceOverdueMs() {
    return g_link.rtt_avg_ms < 0.0 ? 250 : std::clamp((int) (g_link.rtt_avg_ms * 1.25) + 40, 60, 1000);
}

// The host: the pieces of `data` the joiner does not hold (`held`, from its acknowledgements), as many as the window
// allows unacknowledged, each again only once its acknowledgement is overdue. Call it as often as you like: it sends
// what the acknowledgements since the last call make room for.
void SendPieces(PieceSender &out, const std::vector<unsigned char> &data, const std::uint32_t *held, std::uint32_t tick) {
    using Clock = std::chrono::steady_clock;

    // coop-offthread: the game thread's time in this call (the blob's line says its longest)
    struct CallClock {
        PieceSender      &out;
        Clock::time_point at = Clock::now();
        ~CallClock() { out.call_ms_max = std::max(out.call_ms_max, std::chrono::duration<double, std::milli>(Clock::now() - at).count()); }
    } call_clock{out};

    static const bool burst = NetplayMutated("burst_pieces");
    static const bool fixed = NetplayMutated("fixed_pace");
    static int        blobs = 0;
    const bool        once = blobs > 1 && NetplayMutated("no_retransmit");
    auto              now = Clock::now();
    auto              overdue = std::chrono::milliseconds(burst ? 30 : PieceOverdueMs());
    std::uint32_t     count = (std::uint32_t) ((data.size() + kChunkBytes - 1) / kChunkBytes);

    if (out.window == 0) {
        out.window = PieceWindowFirst();
    }
    long long         since = out.paced == Clock::time_point{} ? kFixedBurst
                                                               : std::chrono::duration_cast<std::chrono::milliseconds>(now - out.paced).count() * kFixedPerMs;
    int               budget = burst ? (now - out.paced >= overdue ? kChunks : 0) : fixed ? (int) std::min<long long>(since, kFixedBurst) : kChunks;
    int               queued = 0, rate = 0;
    int               in_flight = 0;
    std::uint32_t     held_count = 0;
    bool              late = false;
    Clock::time_point late_sent{}; // coop-replayload: when the oldest overdue piece went
    std::uint32_t     late_lo = 0, late_hi = 0; // and the first and last overdue (the log's)
    bool              paced = false;
    bool              direct = false;
    const bool        steam = NetSocketQueue(queued, rate) && rate > 0;
    // coop-offthread: over UDP the pieces go on PieceSends' thread
    static const bool on_tick = NetplayMutated("send_on_tick");
    const bool        off_thread = !steam && !on_tick && !NetSocketQueue(queued, rate);

    // Over Steam: no more than kSteamQueueMs of sending waits in its queue, and that wait counts in the overdue time.
    if (!burst && steam && !NetplayMutated("steam_queue_blind")) {
        int room = (int) ((long long) rate * kSteamQueueMs / 1000) - queued;

        budget = std::min(budget, std::max(0, room) / (int) sizeof(Chunk));
        overdue += std::chrono::milliseconds(kSteamQueueMs);
        out.queue_rate = rate;
        out.queue_peak = std::max(out.queue_peak, queued);
        paced = !fixed && NetSteamPaced();
    } else if (!burst && !steam && !fixed && !NetplayMutated("direct_pace_off")) {
        // coop-pacedirect: over UDP the pacer, kDirectCeiling its ceiling.
        rate = kDirectCeiling;
        paced = direct = true;
    }

    // coop-steamrate: the pacer's tokens (PiecePacer), whatever this call sends.
    PiecePacer &pacer = out.pacer;

    if (paced) {
        if (pacer.pace == 0) {
            pacer.pace = direct ? rate : std::min(rate, kPaceStart);
        }
        pacer.pace = std::clamp(pacer.pace, std::min(rate, kPaceMin), rate);
        pacer.direct = direct;

        // (Direct: the time since the last call's worth, at least kDirectBurstMs: the calls come once a frame, and frames
        // of 30-50 ms are common, so a cap of one 17 ms frame threw the rest of the pace away and the pacer sent half of it,
        // its samples "held back", the pace never grew. Nothing saved up past that: after a pause, one burst's worth. A
        // blob's first call too, not kPaceBurstPieces: each part would lose a frame.)
        const double since = pacer.token_at == Clock::time_point{} ? 0.0 : std::chrono::duration<double>(now - pacer.token_at).count();
        const double cap = direct ? (double) pacer.pace * std::max(since, kDirectBurstMs / 1000.0)
                                  : std::max<double>((double) pacer.pace * kSteamQueueMs / 1000.0, (double) kPaceBurstPieces * sizeof(Chunk));

        if (pacer.token_at == Clock::time_point{}) {
            pacer.tokens = direct ? cap : (double) kPaceBurstPieces * sizeof(Chunk);
        } else {
            pacer.tokens = std::min(cap, pacer.tokens + pacer.pace * since);
        }
        pacer.token_at = now;
        pacer.pace_peak = std::max(pacer.pace_peak, pacer.pace);
    }

    // coop-pacerdark (Jep, 2026-11-01; x-floor-join-late-seamless under 100 ms of lag crawled: blob 3 in 820 rounds, 637 of 2685
    // again, 24 s, 11 back-offs down to 373 KB/s): a piece's acknowledgement comes a round trip after it, but the overdue
    // bound came from the link's measured round trip, 250 ms before the link has one, so under lag every piece went again
    // before its acknowledgement could arrive, every round read as lossy and the pace backed off and off. The bound now
    // follows the acknowledgement latency the pacer sees itself (first sends, averaged), a quarter and 40 ms over it.
    // DC_NETPLAY_MUTATE=overdue_link_rtt (twin, mp-direct-join-lag150~twin): the link's bound alone, as before.
    if (direct && !NetplayMutated("overdue_link_rtt")) {
        // (no acknowledgement seen yet, and no round trip from the link: a title join's first part goes out at the ceiling in
        // under a round trip, so by the old 250 ms every piece of it was overdue before its acknowledgement could arrive and
        // went again once (989 of 8789 again under a 150 ms relay, with the bound following the latency from the second part on);
        // until the first acknowledgement the bound is a second)
        // (the average plus four deviations plus 40 ms, a round-trip timer's bound: the acknowledgements of a part stray past
        // the average as the receiver works through it)
        // (the deviation never under an eighth of the average: a part's acknowledgements come near-identical, so the averaged
        // deviation fell to 0 within one part and the bound sat 40 ms over the average; then under a 150 ms relay with the PC
        // loaded the acknowledgements came 312 to 371 ms and a whole 1024-piece window went again (1477 of 9279, 15.9%))
        const double dev = std::max(pacer.ack_dev_ms, pacer.ack_ms / 8.0);
        const int bound = pacer.ack_ms > 0.0 ? (int) (pacer.ack_ms + 4.0 * dev) + 40 : g_link.rtt_avg_ms < 0.0 ? 1000 : 0;

        overdue = std::max(overdue, std::chrono::milliseconds(std::clamp(bound, 60, 2000)));
        // coop-loadreds (test-time plan WS5, mp-direct-join-outage under load): on a busy PC the joiner's map load strays its
        // acknowledgements (278-444 ms, straying 300), this bound climbs to 1.5-2 s and a real outage then costs two overdue
        // times more (the probe, then the lost tail: 5.2 s against 3.1 s), no loss and no desync. Test hook
        // DC_NETPLAY_TEST_OVERDUE_MS=N: that bound on a quiet PC.
        static const int overdue_floor = TestEnvInt("DC_NETPLAY_TEST_OVERDUE_MS", 0);

        overdue = std::max(overdue, std::chrono::milliseconds(overdue_floor));
    }
    if (direct) {
        pacer.overdue_ms = (int) overdue.count(); // (coop-fx51b: mp-direct-join-lag150 sets it against the acknowledgements)
    }

    if (budget <= 0 || count == 0) {
        return;
    }

    if (out.began == Clock::time_point{}) {
        out.began = now;
        blobs++;
    }
    out.bytes = data.size();

    out.paced = now;

    std::int64_t held_last = -1; // coop-fx51b: the highest piece held (a lost tail lies past it)

    for (std::uint32_t i = 0; i < count && !burst; i++) {
        if ((held[i / 32] & (1u << (i % 32))) != 0) {
            held_count++;
            held_last = i;

            // coop-replayload: the newest send acknowledged (a piece sent once: a resent one's acknowledgement may be its first's)
            if ((out.resent[i / 32] & (1u << (i % 32))) == 0 && out.acked_sent < out.sent_at[i]) {
                out.acked_sent = out.sent_at[i];
            } else if ((out.resent[i / 32] & (1u << (i % 32))) != 0 && out.resent_held_sent < out.sent_at[i]) {
                out.resent_held_sent = out.sent_at[i];
                out.resent_held_at = now;
                out.resent_held_first = out.resent_held_first == Clock::time_point{} ? now : out.resent_held_first;
            }
        } else if (out.sent_at[i] != Clock::time_point{}) {
            if (now - out.sent_at[i] < overdue) {
                in_flight++;
            } else {
                late_sent = late && late_sent < out.sent_at[i] ? late_sent : out.sent_at[i];
                late_lo = late ? late_lo : i;
                late_hi = i;
                late = true;
            }
        }
    }

    // The window: one more for each piece acknowledged since the last call, half when one is overdue (once a round trip).
    if (held_count > out.held_seen) {
        out.window = std::min<int>(kChunks, out.window + (int) (held_count - out.held_seen));
        out.held_seen = held_count;
        out.held_at = now;
    }

    // coop-mpdjflake (Jep, 2026-10-31; the mp-direct-join flakes under load): a receiver that has gone silent, no new
    // acknowledgement for an overdue time while pieces are overdue (the joiner loading its map as the snapshot streams, a
    // copy the PC did not schedule), is not a lossy path. Into silence one overdue piece goes again per overdue time (a
    // probe), not the whole window, the window is not halved and the pacer takes no loss from such a round. Before, the
    // whole window went again every overdue time (128 duplicates in a 2 MB part), the round read as lossy, the pace fell to
    // next to nothing and climbed back a quarter a round: 8 MB in 3.3 s instead of 0.7. DC_NETPLAY_MUTATE=resend_into_silence
    // (twin, mp-direct-join-stall~twin): as before.
    // coop-replayload (train 48's runs under load: blob 1 of every red house-out merge "111 pieces (32 again)", the pace
    // backed off to the stall's delivery, 26 to 65 MB/s, and held every 2 MB part after to 2 to 5 rounds): his copy came out
    // of its 850-930 ms load wait at the merge and acknowledged part of a burst (32 of the checkpoint's pieces, or the first
    // half of a part while the second half was still on its way), then went quiet again; the rest of that burst, overdue,
    // went again as lost (31 again 16 ms after his acknowledgement, the pieces 1032 ms old). They were only behind: an
    // acknowledgement shows a piece lost only once a piece sent after it is held (RACK's rule, by send). Until then the late
    // pieces wait on his reading, a probe each overdue time as into silence. A path that drops pieces still shows it: the
    // next round's pieces come to be held, or (a part's last pieces lost, none sent after them) an overdue time passes after
    // a piece sent again is held and they are still not. DC_NETPLAY_MUTATE=silence_from_last_ack (twin,
    // l-local-play-houseout-stall~silence): silence only an overdue time after his last acknowledgement, as before.
    // coop-joinbudget (mp-direct-join-bottleneck with a 128-piece first window: part 1's last 650 pieces went 3 a second for
    // 130 s): with every piece of a part sent once, a lost tail shows only by a resent piece held and an overdue time after
    // it, but each probe into the "silence" was held a round trip later and moved that time on, so the overdue time never
    // passed: one probe each overdue time, for good. It counts from the first resent piece held while pieces are late.
    // DC_NETPLAY_MUTATE=probe_wait_from_last (twin, mp-direct-join-tailloss~twin): from the last, as before.
    static const bool probe_wait_last = NetplayMutated("probe_wait_from_last");

    if (!late) {
        out.resent_held_first = Clock::time_point{};
    }
    const Clock::time_point heard = out.held_at == Clock::time_point{} ? out.began : out.held_at;
    const bool              silent = direct && late && !NetplayMutated("resend_into_silence") &&
                        (now - heard >= overdue || (!NetplayMutated("silence_from_last_ack") && !(late_sent < out.acked_sent) &&
                         !(late_sent < out.resent_held_sent && now - (probe_wait_last ? out.resent_held_at : out.resent_held_first) >= overdue)));
    int        probes = silent ? (now - out.probe_at >= overdue ? 1 : 0) : 1 << 30;

    if (silent) {
        pacer.silent_round = true;
    }

    if (late && !silent && (!direct || pacer.cut_at == Clock::time_point{}) && now - out.cut_at >= overdue) {
        out.window = std::max(kPieceWindowStart, out.window / 2);
        out.cut_at = now;
    }

    // coop-pacedirect: a delivery sample for each piece newly held (BBR's): the pieces that came to be held since the count
    // it went at was reached (not since it went: after the joiner stalled, its acknowledgements come at once and a piece
    // sent just before said 4.7 MB/s through a 2 MB/s relay), over that time; a lossy round backs the pace off to the best
    // of them. A round's count of pieces newly held (Steam's sample) also counts the time each 2 MB part waits for its last
    // acknowledgements, which is not the path's. A piece sent more than once gives none (its acknowledgement may be its
    // first send's).
    if (direct) {
        double ack_lat_max = -1.0; // coop-pacerdark: the slowest first-send piece this call sees acknowledged (one sample a call)

        for (std::uint32_t i = 0; i < count; i++) {
            const std::uint32_t bit = 1u << (i % 32);

            if ((held[i / 32] & bit) == 0 || (out.sampled[i / 32] & bit) != 0) {
                continue;
            }
            out.sampled[i / 32] |= bit;

            const double secs = std::chrono::duration<double>(now - out.held_since[i]).count();

            if (out.sent_at[i] != Clock::time_point{} && (out.resent[i / 32] & bit) == 0 && secs > 0.001 && held_count > out.held_at_send[i]) {
                pacer.round_max = std::max(pacer.round_max, (int) std::min<double>(2e9, (double) (held_count - out.held_at_send[i]) * kChunkBytes / secs));
            }
            // coop-pacerdark: the acknowledgement latency of a piece sent once (its first acknowledgement is its own).
            if (out.sent_at[i] != Clock::time_point{} && (out.resent[i / 32] & bit) == 0) {
                ack_lat_max = std::max(ack_lat_max, std::chrono::duration<double, std::milli>(now - out.sent_at[i]).count());
            }
        }
        // One sample a call, the slowest piece the call's acknowledgements cover (the bound must cover it): a per-piece update
        // took an acknowledgement of 157 pieces as 157 observations and the deviation decayed to its floor within one, so on a
        // loaded PC the first blob's later acknowledgements (98 ms average, straying 13) fell past the bound and 32 pieces went
        // again, one back-off, and the pace held every part after at 2 MB/s (mp-direct-join-outage beside 13 other copies).
        if (ack_lat_max >= 0.0) {
            if (pacer.ack_ms < 0.0) {
                pacer.ack_ms = ack_lat_max;
                pacer.ack_dev_ms = ack_lat_max / 2.0; // (a round-trip timer's first deviation: half the first sample)
            } else {
                // (the deviation first, from the old average: a round-trip timer's way; half the part's acknowledgements
                // came past the average plus a quarter under a 150 ms relay, 989 pieces sent again for nothing)
                pacer.ack_dev_ms = pacer.ack_dev_ms * 0.75 + std::fabs(ack_lat_max - pacer.ack_ms) * 0.25;
                pacer.ack_ms = pacer.ack_ms * 0.875 + ack_lat_max * 0.125;
            }
        }
    }

    // coop-steamrate: once a round trip, the delivery rate (what the joiner newly holds) sets the pace; then the tokens
    // bound this call's pieces.
    if (paced) {
        const auto round = std::chrono::milliseconds(std::max(kPaceSampleMs, (int) g_link.rtt_avg_ms));

        if (pacer.sample_at == Clock::time_point{} || held_count < pacer.sample_held) {
            pacer.sample_at = now;
            pacer.sample_held = held_count;
            pacer.sample_sends = out.sends;
            pacer.sample_again = out.again;
            pacer.sample_tail_again = out.tail_again;
            pacer.round_max = 0;
            pacer.ran_out = false;
            pacer.silent_round = false;
        } else if (now - pacer.sample_at >= round || (held_count >= count && out.sends != pacer.sample_sends && !NetplayMutated("last_round_unread"))) {
            // coop-fx51b (Jep, 2026-11-05: mp-direct-join-firstloss~twin passed one run in two on a fixture): a round is read on
            // the call after its clock runs out, so a part all held before that call (a host that did not call in time: a PC
            // that did not schedule it, the 157-piece checkpoint in one window since joinbudget) never had its last round read,
            // and its loss counted or not by that race. CatchSend calls once more as the part ends, and the round is read then.
            // DC_NETPLAY_MUTATE=last_round_unread (twin, mp-direct-join-firstloss~lastround): only on its clock, as before.
            const double        secs = std::chrono::duration<double>(now - pacer.sample_at).count();
            // (Direct: the best of the round's per-piece samples, above.)
            const int           delivered = direct ? pacer.round_max : (int) std::min<double>(2e9, (double) (held_count - pacer.sample_held) * kChunkBytes / secs);
            // coop-fx51b (Jep, 2026-11-05: mp-direct-join-tailloss on a fixture, 4480 and 5752 ms of its 4000): a part's lost tail sent
            // again is no sign of a full path (its pieces were the part's last, none sent after them held: the loss came at the end,
            // not from the pace), so its pieces count as neither loss nor a lossy round; else one back-off held every part after it
            // to that part's delivery (3.5 MB/s against 5.9). DC_NETPLAY_MUTATE=tail_counts_loss (twin): counted, as before.
            const std::uint32_t sent = out.sends - pacer.sample_sends,
                                again = out.again - pacer.sample_again - (out.tail_again - pacer.sample_tail_again);

            // More than 2% of the round's pieces sent again: the path is full. Sent under half the pace: the window or
            // the blob's end held it, so its delivery says nothing of the path.
            const bool lossy = sent >= 20 && again * 50 > sent && !pacer.silent_round; // (coop-mpdjflake: silence is not loss)
            const bool limited = (double) sent * sizeof(Chunk) < 0.5 * pacer.pace * secs;

            if (delivered > 0) {
                pacer.best[pacer.best_next] = delivered;
                pacer.best_next = (pacer.best_next + 1) % kPaceSamples;
                pacer.delivered_peak = std::max(pacer.delivered_peak, delivered);
            }

            const int best = *std::max_element(pacer.best, pacer.best + kPaceSamples);

            // coop-pacedirect: direct, the first lossy round starts the pace, at the best delivery of the last kPaceSamples
            // rounds (the lossy round's own came out under the path's, a slow climb back: 2 MB parts at 1.1 MB/s on the
            // 2 MB/s relay); until then the window alone sends, as before (kDirectCeiling is no brake). A later lossy round
            // backs it off so again, once:
            // the pieces lost before it go again only once overdue, in the rounds after, so a round stays lossy for that
            // long whatever the new pace (the 2 MB/s relay: five back-offs in a row down to 165 KB/s, 8 MB in 47 s).
            // Between, kDirectGains' cycle over the best delivery; a round that ran out of pieces keeps the pace (its
            // samples were its part's, not the path's).
            // coop-joinlate (train 49's x-floor-join-late-seamless under 100 ms of lag: the first part, the 80 KB checkpoint, had
            // a lossy round; the pace backed off to that part's own delivery, 145 KB/s, and every 2 MB part after it crawled at
            // 263 KB/s, 12.9 s each): a part under a quarter of a full one goes out in a few windows, so its delivery is its
            // window's, not the path's; a lossy round of it takes no pace (its pieces go again by the window's rule), and the
            // pace starts from a full part's rounds. (Not "a round that ran out": a 2 MB part's last rounds run out too, and
            // through a 2 MB/s drop-tail queue they are the lossy ones: mp-direct-join-bottleneck never backed off.)
            // DC_NETPLAY_MUTATE=backoff_small_part (twin): it backs off, as before.
            const bool part_bound = count < (std::uint32_t) kChunks / 4 && !NetplayMutated("backoff_small_part");

            if (direct) {
                if (lossy && !part_bound) {
                    if (pacer.cut_at == Clock::time_point{} || now - pacer.cut_at >= overdue + round) {
                        pacer.pace = best > 0 ? best : pacer.pace / 2;
                        std::fill(pacer.best, pacer.best + kPaceSamples, best);
                        pacer.backoffs++;
                        pacer.cut_at = now;
                        pacer.cycle = 0;
                    }
                } else if (pacer.cut_at != Clock::time_point{} && !pacer.ran_out && best > 0) {
                    pacer.pace = (int) std::min<double>(2e9, kDirectGains[pacer.cycle] * best);
                    pacer.cycle = (pacer.cycle + 1) % 8;
                }
            } else if (lossy && delivered > 0) {
                pacer.pace = delivered;
                std::fill(pacer.best, pacer.best + kPaceSamples, delivered);
                pacer.filling = false;
                pacer.backoffs++;
            } else if (!limited && best > 0) {
                if (pacer.filling) {
                    if (best >= pacer.fill_best + pacer.fill_best / 4) {
                        pacer.fill_best = best;
                        pacer.fill_flat = 0;
                    } else if (++pacer.fill_flat >= 3) {
                        pacer.filling = false;
                    }
                }
                pacer.pace = (int) std::min<double>(2e9, (pacer.filling ? 2.0 : 1.25) * best);
            }
            pacer.pace = std::clamp(pacer.pace, std::min(rate, kPaceMin), rate);
            pacer.pace_peak = std::max(pacer.pace_peak, pacer.pace);
            pacer.sample_at = now;
            pacer.sample_held = held_count;
            pacer.sample_sends = out.sends;
            pacer.sample_again = out.again;
            pacer.sample_tail_again = out.tail_again;
            pacer.round_max = 0;
            pacer.ran_out = false;
            pacer.silent_round = false;
        }

        budget = std::min(budget, (int) (pacer.tokens / sizeof(Chunk)));

        if (budget <= 0) {
            return;
        }
    }

    // (coop-pacedirect: paced over UDP, once its pace has started, the pace holds the pieces back, not the window.)
    const int window = fixed ? kFixedWindow : direct && pacer.cut_at != Clock::time_point{} ? kChunks : out.window;

    Chunk               chunk{};
    const std::uint32_t sends_before = out.sends;
    const std::uint32_t again_before = out.again;
    const std::uint32_t tail_before = out.tail_again;
    // coop-fx51b: the late pieces are the part's lost tail when every one of them lies past the last piece held (the part's
    // end lost, nothing after it to show the loss); they go again by the resent-held rule. Not by send time: a part's first
    // window goes in one call, so pieces lost at its front were sent no earlier than the held ones after them (firstloss).
    // Only a part's first loss: a part that already lost pieces before its end is on a full path, and its end's loss is the
    // same (mp-direct-join-bottleneck's 2 MB/s drop-tail queue loses most at a part's end). DC_NETPLAY_MUTATE=tail_counts_loss (twin).
    static const bool tail_loss = NetplayMutated("tail_counts_loss");
    const bool        tail = direct && late && held_last < (std::int64_t) late_lo && out.again == out.tail_seen;

    chunk.magic = kChunkMagic;
    chunk.player = (std::uint32_t) g_link.player; // coop-localfix2: the sender (player 2 for her merge)
    chunk.session = g_link.session;
    chunk.tick = tick;
    chunk.total = (std::uint32_t) data.size();
    chunk.count = (std::uint16_t) count;

    // coop-offthread: the send thread still sends an earlier call's pieces: none picked now (the game thread waited them out before)
    if (off_thread && PieceSends().Busy()) {
        budget = 0;
    }
    std::vector<Chunk> off;

    for (std::uint32_t i = 0; i < count && budget > 0 && (burst || in_flight < window); i++) {
        bool again = out.sent_at[i] != Clock::time_point{};

        if ((held[i / 32] & (1u << (i % 32))) != 0 || (again && (once || (!burst && now - out.sent_at[i] < overdue)))) {
            continue;
        }
        if (again && silent) { // coop-mpdjflake: one probe into a silent receiver
            if (probes <= 0) {
                continue;
            }
            probes--;
            out.probe_at = now;
        }

        std::size_t at = (std::size_t) i * kChunkBytes;

        chunk.index = (std::uint16_t) i;
        std::memset(chunk.data, 0, sizeof chunk.data);
        std::memcpy(chunk.data, data.data() + at, std::min<std::size_t>(kChunkBytes, data.size() - at));
        // coop-steamrate: a piece Steam refused (its send buffer full) did not go: it goes at the next call, as KeeperFX
        // retries such a send, not once it is overdue. DC_NETPLAY_MUTATE=steam_full_lost (twin): counted as sent, as before.
        if (off_thread) {
            off.push_back(chunk);
            out.sends_off++;
        } else if (!NetSocketSend(&chunk, sizeof chunk)) {
            out.refused++;

            if (!NetplayMutated("steam_full_lost")) {
                break;
            }
        } else {
            out.sends_here++;
        }
        out.sent_at[i] = now;
        out.held_at_send[i] = (std::uint16_t) held_count;
        out.held_since[i] = out.held_at == Clock::time_point{} ? now : out.held_at;
        out.resent[i / 32] |= again ? 1u << (i % 32) : 0;
        out.sends++;
        out.again += again ? 1 : 0;
        out.tail_again += again && tail && !tail_loss ? 1 : 0;
        out.tail_seen += again && tail ? 1 : 0;
        budget--;
        in_flight++;

        if (paced) {
            pacer.tokens -= (double) sizeof(Chunk);
        }
    }
    out.rounds += out.sends != sends_before ? 1 : 0;
    PieceSends().Push(std::move(off));

    // coop-fx51b: a call that sent a part's lost tail again says how the pacer reads it (mp-direct-join-tailloss reads it).
    if (tail && out.again - again_before >= 4) {
        std::fprintf(stderr, "pieces: %u again as the part's lost tail (pieces %u-%u): %s\n", out.again - again_before, late_lo, late_hi,
                     out.tail_again - tail_before > 0 ? "not loss, the pace kept" : "read as loss");
    }
    // coop-replayload: a call that sends 4 or more pieces again says why (the l-local-play stall rows read it).
    if (out.again - again_before >= 4) {
        std::fprintf(stderr, "pieces: %u again in one call of %u: late %d silent %d, heard %lld ms ago, oldest late sent %lld ms ago, overdue %lld ms, held %u of %u, window %d; late pieces %u-%u, newest held send %lld ms ago, newest resend held %lld ms ago [t %lld]\n",
                     out.again - again_before, out.sends - sends_before, late ? 1 : 0, silent ? 1 : 0,
                     (long long) std::chrono::duration_cast<std::chrono::milliseconds>(now - heard).count(),
                     (long long) std::chrono::duration_cast<std::chrono::milliseconds>(now - late_sent).count(), (long long) overdue.count(), held_count, count,
                     window, late_lo, late_hi, (long long) std::chrono::duration_cast<std::chrono::milliseconds>(now - out.acked_sent).count(),
                     (long long) std::chrono::duration_cast<std::chrono::milliseconds>(now - out.resent_held_sent).count(), LogMs());
    }

    // (coop-pacedirect: pace to spare and none left to send this call: the round ran out, SendPieces' samples.)
    if (direct && budget > 0) {
        pacer.ran_out = true;
    }
}

std::uint32_t CatchPieces() {
    return (std::uint32_t) ((g_catch.out.size() + kChunkBytes - 1) / kChunkBytes);
}

bool CatchOutHeld() {
    for (std::uint32_t i = 0; i < CatchPieces(); i++) {
        if ((g_catch.out_held[i / 32] & (1u << (i % 32))) == 0) {
            return false;
        }
    }

    if (g_catch.out_held_at == std::chrono::steady_clock::time_point{}) {
        g_catch.out_held_at = std::chrono::steady_clock::now();

        // coop-replayload (train 48's red house-out merges: part 1 ended with the window at 1012 to 1838, so part 2 took 2 to 5
        // rounds): SendPieces grows the window by the pieces acknowledged since its last call, but the acknowledgement that makes
        // the blob whole is read here, in the same pass that starts the next blob (NextCatchPieces), and no SendPieces call
        // sees it. Under load his acknowledgements come in bunches, so that last one held many. They count now, as the blob
        // ends. DC_NETPLAY_MUTATE=window_held_late (twin, l-local-play-houseout-stall~heldlate): not counted, as before.
        PieceSender        &out = g_catch.out_pieces;
        const std::uint32_t pieces = CatchPieces();

        if (pieces > out.held_seen && !NetplayMutated("window_held_late")) {
            out.window = std::min<int>(kChunks, out.window + (int) (pieces - out.held_seen));
            out.held_seen = pieces;
        }
        // coop-fx51b: and its last round is read here (SendPieces' rounds: all held, it sends nothing), the same way.
        // (window_held_late leaves this read out too: SendPieces here counts the pieces just held into the window, which would undo the twin.)
        if (!g_catch.out.empty() && !NetplayMutated("last_round_unread") && !NetplayMutated("window_held_late")) {
            const std::uint32_t unread = out.sends - out.pacer.sample_sends;

            SendPieces(out, g_catch.out, g_catch.out_held, kCatchBit | g_catch.out_id);
            std::fprintf(stderr, "catchup: blob %u all held: its last round read as it ended (%u sends not read before)\n", g_catch.out_id, unread);
        }
    }
    return true;
}

// The host: the pieces of the blob in flight the joiner lacks (SendPieces).
void CatchSend() {
    if (!g_catch.out.empty() && !CatchOutHeld()) {
        SendPieces(g_catch.out_pieces, g_catch.out, g_catch.out_held, kCatchBit | g_catch.out_id);
    }
}

// The joiner: a whole blob, its checkpoint (the first) and its pads (pad 2 neutral) appended.
// WS5: the blob being taken came whole in one piece, the first time (his replay's pads).
bool CatchOnePiece() {
    return g_catch.in.size() <= kChunkBytes && g_catch.in_pieces == 1 && g_catch.in_again == 0;
}

void CatchTake() {
    const std::vector<unsigned char> &in = g_catch.in;
    std::size_t                       at = 0;
    auto get = [&](int bytes) {
        std::uint32_t value = 0;

        for (int i = 0; i < bytes && at < in.size(); i++, at++) {
            value |= (std::uint32_t) in[at] << (8 * i);
        }

        return value;
    };

    std::int64_t  from = get(4);
    std::int64_t  to = get(4);
    std::uint32_t join = get(4);
    std::uint32_t checkpoint = get(4);

    // coop-floorjoin2: the host's first blob says its state set follows (SnapshotJoinOn): the floor this copy lands on for it
    // skips its opening scene (NetplayFloorStarted), the host being past it, or soon past (its take waits for a quiet tick).
    if (join == kSnapFollows) {
        g_snap.expected = true;
        join = kNone;
        std::fprintf(stderr, "catchup: the host's state set follows this checkpoint\n");
    }

    // coop-snapshot: a part of the host's state set.
    const std::uint32_t mark = (std::uint32_t) from;

    if (mark == kSnapMark || mark == kPageListMark || mark == kPageDiffMark) {
        std::size_t  part = (std::size_t) to, parts = join, length = checkpoint;
        std::int64_t snap_tick = get(4);

        if (part == 0) {
            if (g_catch.restart_waiting) { // coop-reds2: the fresh set asked for is coming
                g_catch.restart_waiting = false;
                std::fprintf(stderr, "catchup: the fresh state set (tick %lld) is coming: this copy replays on meanwhile [t %lld]\n", (long long) snap_tick, LogMs());
            }
            g_snap.in.clear();
            g_snap.in.reserve(std::min<std::size_t>(parts, 16) * kSnapPartBytes); // coop-small1: one buffer for its parts, not one a part
            g_snap.in_parts = 0;
            g_snap.in_total = parts;
            g_snap.in_tick = snap_tick;
            g_snap.in_mark = mark;
            g_snap.answered = g_snap.answered || mark != kPageListMark;
        }
        if (part == g_snap.in_parts && at + length <= in.size()) {
            g_snap.in.insert(g_snap.in.end(), in.begin() + (std::ptrdiff_t) at, in.begin() + (std::ptrdiff_t) (at + length));
            g_snap.in_parts++;
        }
        std::fprintf(stderr, "catchup: took blob %u: snapshot part %zu of %zu (%zu bytes so far) [t %lld]\n", g_catch.in_id, part + 1, parts, g_snap.in.size(),
                     LogMs());

        if (g_snap.in_parts == g_snap.in_total && g_snap.in_mark == kPageListMark) {
            g_snap.list_in = SnapshotUnpack(g_snap.in, g_snap.list);
            g_snap.in.clear();
            if (!g_snap.list_in) { // (never seen) the whole set instead
                g_snap.asked_whole = true;
                std::fprintf(stderr, "catchup: the page list did not unpack: asking for the whole state set\n");
            }
        } else if (g_snap.in_parts == g_snap.in_total && g_snap.in_mark == kPageDiffMark) {
            g_snap.diff_due = true;
        } else if (g_snap.in_parts == g_snap.in_total) {
            g_snap.apply_due = true;
        }
        return;
    }

    if (checkpoint > 0) {
        g_solo = Solo{};
        g_solo.replay = true;
        g_solo.checkpoint.assign(in.begin() + (std::ptrdiff_t) at, in.begin() + (std::ptrdiff_t) (at + checkpoint));
        at += checkpoint;
    }

    // Player 2's changes from apart this blob carries (all so far with the checkpoint, then the newer), on their ticks.
    const std::uint32_t added = get(4);

    for (std::uint32_t i = 0; i < added; i++) {
        Solo::Delta delta;

        delta.tick = get(4);

        for (std::int32_t &value : delta.value) {
            value = (std::int32_t) get(4);
        }

        g_solo.deltas.push_back(delta);
    }

    if (added > 0) {
        std::fprintf(stderr, checkpoint > 0 ? "catchup: the checkpoint comes with %u of player 2's changes from apart\n"
                                            : "catchup: blob %u brings %u more of player 2's changes from apart (from tick %lld)\n",
                     checkpoint > 0 ? added : g_catch.in_id, added, (long long) g_solo.deltas.back().tick);
    }

    // The host's town Georama at J, with the last blob in a town (EnterLockstep takes it).
    const std::uint32_t save_town = get(4);
    const std::uint32_t save_bytes = get(4);

    g_catch.join_town = save_town == kNone ? -1 : (int) save_town;

    if (save_bytes > 0 && at + save_bytes <= in.size()) {
        g_catch.join_save.assign(in.begin() + (std::ptrdiff_t) at, in.begin() + (std::ptrdiff_t) (at + save_bytes));
        at += save_bytes;
    }

    // coop-joinlobby: the joiner's pick, with the last blob (EnterLockstep gives it).
    const std::uint32_t pick_member = get(4);

    if (at + 16 <= in.size()) {
        char name[17] = {};

        std::memcpy(name, &in[at], 16);
        at += 16;
        g_catch.pick_name = name;
    }
    const std::uint32_t pick_bytes = get(4);

    g_catch.pick_bag.clear();
    if (pick_bytes > 0 && at + pick_bytes <= in.size()) {
        g_catch.pick_bag.assign(in.begin() + (std::ptrdiff_t) at, in.begin() + (std::ptrdiff_t) (at + pick_bytes));
        at += pick_bytes;
    }
    g_catch.pick_set = pick_member != kNone;
    g_catch.pick_member = g_catch.pick_set ? (int) pick_member : -1;

    if (from != g_catch.have_to && g_snap.in_total == 0) { // (after a snapshot the pads start at S, as they should)
        std::fprintf(stderr, "catchup: blob %u starts at tick %lld, not %lld\n", g_catch.in_id, (long long) from, (long long) g_catch.have_to);
    }

    g_solo.pads.resize((std::size_t) from * 2);

    while (at < in.size()) {
        std::uint32_t run = get(4);
        InputPadState pad = Neutral();

        pad.buttons = (std::uint16_t) get(2);
        pad.left_x = (std::uint8_t) get(1);
        pad.left_y = (std::uint8_t) get(1);
        pad.right_x = (std::uint8_t) get(1);
        pad.right_y = (std::uint8_t) get(1);

        for (std::uint32_t i = 0; i < run; i++) {
            g_solo.pads.push_back(g_link.source == 2 ? Neutral() : pad); // coop-localfix2: her merge's pads are player 2's
            g_solo.pads.push_back(g_link.source == 2 ? pad : Neutral());
        }
    }

    g_catch.have_to = (std::int64_t) g_solo.pads.size() / 2;
    g_catch.join_tick = join == kNone ? -1 : (std::int64_t) join;
    if (join != kNone) {
        CoopWorldsTestSlotOnLastBlob(); // coop-catchuprace (tests): DC_WORLDS_TEST_SLOT_AT_MERGE_WHEN=lastblob
    }
    // WS5: a one-piece pad blob (30 a second in his replay) says so once a second, and the last one always (CatchReceiveChunk).
    if (!CatchOnePiece() || join != kNone || g_catch.in_id % 30 == 0 || NetplayMutated("pad_blob_lines")) {
        std::fprintf(stderr, "catchup: took blob %u: pads to tick %lld%s [t %lld]\n", g_catch.in_id, (long long) g_catch.have_to,
                     join == kNone ? "" : ", the last", LogMs());
    }

    if (g_catch.have_to != to) {
        std::fprintf(stderr, "catchup: blob %u ends at tick %lld, not %lld\n", g_catch.in_id, (long long) g_catch.have_to, (long long) to);
    }
}

void CatchAckNow(std::uint32_t id, bool all) {
    Ack ack{};

    ack.magic = kAckMagic;
    ack.player = (std::uint32_t) g_link.player;
    ack.session = g_link.session;
    ack.tick = kCatchBit | id;

    if (all) {
        std::memset(ack.held, 0xFF, sizeof ack.held);
    } else {
        std::memcpy(ack.held, g_catch.in_held, sizeof ack.held);
    }

    NetSocketSend(&ack, sizeof ack);

    // coop-ackbatch: the acknowledgements a blob took, told once it is whole.
    if (id == g_catch.in_id) {
        g_catch.in_acks++;
    }

    if (all && id == g_catch.in_done && g_catch.acks_told != id) {
        g_catch.acks_told = id;
        if (CatchOnePiece() && g_catch.in_acks == 1 && !NetplayMutated("pad_blob_lines")) {
            return; // (WS5: a one-piece blob's one acknowledgement goes unsaid, CatchReceiveChunk)
        }
        std::fprintf(stderr, "catchup: blob %u: %u acknowledgements for %u pieces\n", id, g_catch.in_acks, g_catch.in_pieces);
    }
}

void CatchAck(std::uint32_t id, bool all) {
    if (NetplayMutated("ack_per_piece")) {
        CatchAckNow(id, all);
        return;
    }

    if (g_ack_due.catchup && g_ack_due.catch_id != id) {
        CatchAckNow(g_ack_due.catch_id, g_ack_due.catch_all);
        g_ack_due.catch_all = false;
    }

    g_ack_due.catch_all = (g_ack_due.catchup && g_ack_due.catch_id == id && g_ack_due.catch_all) || all;
    g_ack_due.catchup = true;
    g_ack_due.catch_id = id;
}

// coop-ackbatch: the acknowledgements due from this pass of Receive.
void SendAcksDue() {
    if (g_ack_due.resync) {
        SendAckNow(g_ack_due.resync_tick);
    }

    if (g_ack_due.catchup) {
        CatchAckNow(g_ack_due.catch_id, g_ack_due.catch_all);
    }

    g_ack_due = AckDue{};
}

void CatchReceiveChunk(const Chunk &chunk);
bool      CatchReceiverOn();
long long CatchReceiverWholeAgo(std::uint32_t id); // coop-small1 (below)
long long CatchReceiverPieceMs();                   // coop-small1 (below)
void CatchReceiverStopAtExit();

// coop-replayload test hook (DC_NETPLAY_TEST_ACK_STALL=L:B:K[,L:B:K...], the copy that takes the state): once blob B of
// link L's catch-up (1: the first link, 2: the next, a merge) holds K pieces, its further pieces wait here unread and
// unacknowledged, as in a stall of this copy's (a zone load, a loaded PC) with them queued in the socket, until one comes
// that already waits (the host sending again into the quiet). Then all are taken at once and acknowledged in one, the
// bunch a stall ends with. No clock: the host's own resend ends it.
bool CatchStallHolds(const Chunk &chunk, std::uint32_t id) {
    if (CatchReceiverOn()) { // (coop-joinbudget: the receiver thread reads the socket then: the stall is its, RxStallHolds)
        return false;
    }
    struct Stall {
        std::uint32_t link, blob, at;
        bool          over;
    };
    static std::vector<Stall> stalls = [] {
        std::vector<Stall> list;
        const char        *spec = TestEnv("DC_NETPLAY_TEST_ACK_STALL");

        for (const char *at = spec; at != nullptr && *at != '\0';) {
            unsigned link = 0, blob = 0, pieces = 0;

            if (std::sscanf(at, "%u:%u:%u", &link, &blob, &pieces) == 3) {
                list.push_back({link, blob, pieces, false});
            }
            at = std::strchr(at, ',');
            at = at != nullptr ? at + 1 : nullptr;
        }

        return list;
    }();
    static std::vector<Chunk> waiting;

    for (Stall &stall : stalls) {
        if (stall.over || stall.link != (std::uint32_t) g_links_up || stall.blob != id || g_catch.in_pieces - g_catch.in_again < stall.at) {
            continue;
        }

        const bool again = std::any_of(waiting.begin(), waiting.end(), [&](const Chunk &w) { return w.index == chunk.index; });

        if (!again) {
            if (waiting.empty()) {
                std::fprintf(stderr, "catchup: test: blob %u stalls at %u pieces held [t %lld]\n", id, g_catch.in_pieces - g_catch.in_again, LogMs());
            }
            waiting.push_back(chunk);
            return true;
        }

        std::vector<Chunk> take;

        take.swap(waiting);
        stall.over = true;
        std::fprintf(stderr, "catchup: test: blob %u's stall ends on piece %u sent again: %zu pieces waited [t %lld]\n", id, (unsigned) chunk.index, take.size(),
                     LogMs());
        for (const Chunk &w : take) {
            CatchReceiveChunk(w);
        }
        CatchReceiveChunk(chunk);
        return true;
    }

    return false;
}

// The joiner: a piece of a blob. The host's first piece starts the catch-up here.
std::vector<unsigned char> *g_catch_whole = nullptr; // coop-small1: CatchReceiveWhole's blob, while CatchReceiveChunk takes it (by swap)

void CatchReceiveChunk(const Chunk &chunk) {
    std::uint32_t id = chunk.tick & ~kCatchBit;

    if (!g_catch.active && g_catch.in_done == 0) {
        g_catch = Catch{};
        g_catch.active = true;
        g_catch.heard = std::chrono::steady_clock::now();
        g_catch.began = g_catch.heard;
        g_catches_begun++;
        std::fprintf(stderr, "catchup: the host sends its checkpoint: this copy catches up\n");
        CoopWorldsTestSlotOnCatch(); // coop-fx51c (tests): DC_WORLDS_TEST_SLOT_AT_MERGE_WHEN=catchup
        CatchReceiverTick(); // coop-joinbudget: before this copy's next frame, which may be its floor's load

        // coop-joinbudget (a test's, DC_NETPLAY_TEST_CATCH_STALL_MS=N): this copy's game thread stops N ms here, as her floor's
        // load held it (some 1.3 s) as the checkpoint came: mp-direct-join -CatchStallMs.
        if (static const int stall = TestEnvInt("DC_NETPLAY_TEST_CATCH_STALL_MS", 0); stall > 0) {
            std::fprintf(stderr, "catchup: test: this copy stops %d ms as the catch-up begins\n", stall);
            std::this_thread::sleep_for(std::chrono::milliseconds(stall));
        }
    }

    if (id <= g_catch.in_done) {
        CatchAck(id, true);
        return;
    }

    if (id != g_catch.in_id || g_catch.in.size() != chunk.total) {
        // coop-ackbatch: one due for the blob before goes now, while the pieces held are still that blob's.
        if (g_ack_due.catchup) {
            CatchAckNow(g_ack_due.catch_id, g_ack_due.catch_all);
            g_ack_due.catchup = g_ack_due.catch_all = false;
        }

        g_catch.in_id = id;
        if (g_catch_whole == nullptr) {
            g_catch.in.assign(chunk.total, 0);
        }
        std::memset(g_catch.in_held, 0, sizeof g_catch.in_held);
        g_catch.in_pieces = g_catch.in_again = g_catch.in_acks = 0;

        // coop-steamrate (a test's, DC_NETPLAY_TEST_JOIN_HITCH_MS=N): once, at the first piece of the third big blob (a
        // snapshot part), this copy stops N ms, as a zone load or the apply stops it while pieces still arrive. The third:
        // by then the host's window (2048 pieces) and pace are up, so more than Steam's default 1000 messages can arrive
        // in the stop; at the first part the window (some 150) held what was in flight below it.
        static int  hitch = TestEnvInt("DC_NETPLAY_TEST_JOIN_HITCH_MS", 0);
        static int  big = 0;
        static bool hitched = false;

        if (hitch > 0 && !hitched && chunk.count >= 1024 && ++big == 3) {
            hitched = true;
            std::fprintf(stderr, "catchup: test: blob %u: this copy stops %d ms\n", id, hitch);
            std::this_thread::sleep_for(std::chrono::milliseconds(hitch));
        }
    }

    if (g_catch_whole != nullptr) { // coop-small1: the whole blob at once (CatchReceiveWhole)
        g_catch.in.swap(*g_catch_whole); // (its bytes, any the game thread held before the thread began among them)
        for (std::uint32_t i = 0; i < chunk.count; i++) {
            g_catch.in_pieces += (g_catch.in_held[i / 32] & (1u << (i % 32))) == 0 ? 1 : 0;
            g_catch.in_held[i / 32] |= 1u << (i % 32);
        }
    } else {
        if (CatchStallHolds(chunk, id)) {
            return;
        }

        std::size_t at = (std::size_t) chunk.index * kChunkBytes;

        g_catch.in_pieces++;
        g_catch.in_again += (g_catch.in_held[chunk.index / 32] & (1u << (chunk.index % 32))) != 0 ? 1 : 0;
        std::memcpy(g_catch.in.data() + at, chunk.data, std::min<std::size_t>(kChunkBytes, chunk.total - at));
        g_catch.in_held[chunk.index / 32] |= 1u << (chunk.index % 32);
    }

    bool whole = true;

    for (std::uint32_t i = 0; i < chunk.count && whole; i++) {
        whole = (g_catch.in_held[i / 32] & (1u << (i % 32))) != 0;
    }

    if (whole) {
        // WS5: a one-piece blob (his replay's pads, 30 a second) writes no "came as 1 pieces of 1" and no "1 acknowledgements
        // for 1 pieces", and its "took blob" once a second (CatchTake): three lines a blob, some 10 ms each beside a pinned PC,
        // held his first Receive behind her blobs for seconds (l-local-play-houseout-slowreplay, 4-10 s stalls).
        // DC_NETPLAY_MUTATE=pad_blob_lines (l-local-play-houseout-slowlog~twin): all three, as before.
        if (!CatchOnePiece() || NetplayMutated("pad_blob_lines")) {
            char waited[64] = "";

            if (const long long ago = CatchReceiverWholeAgo(id); ago >= 0) {
                std::snprintf(waited, sizeof waited, "; whole on the receiver %lld ms before", ago);
            }
            std::fprintf(stderr, "catchup: blob %u came as %u pieces of %u (%u held already%s) [t %lld]\n", id, g_catch.in_pieces, (unsigned) chunk.count,
                         g_catch.in_again, waited, LogMs());
        }
        SteamLinkLine("catchup: took blob", id);
        CatchTake();
        g_catch.in_done = id;
    }

    CatchAck(id, whole);
}

// The joiner: a piece of the host's state, kept and acknowledged. A piece of a resync already done
// is acknowledged again, in case the host missed the last acknowledgement.
void ReceiveChunk(const Chunk &chunk) {
    if (chunk.count == 0 || chunk.count > kChunks || chunk.index >= chunk.count || chunk.total > (std::uint32_t) kChunks * kChunkBytes) {
        return;
    }

    // coop-localfix2: a catch-up's pieces go to the copy that takes the state (player 1's for a stayer's merge); a
    // resync's to player 2 alone.
    if ((chunk.tick & kCatchBit) != 0) {
        if (TakesState()) {
            CatchReceiveChunk(chunk);
            g_catch.piece_at = std::chrono::steady_clock::now(); // (after: its first piece starts g_catch afresh)
        } else if ((chunk.tick & ~kCatchBit) != 0 && (chunk.tick & ~kCatchBit) <= g_catch.in_done && !NetplayMutated("last_blob_unheld")) {
            CatchAck(chunk.tick & ~kCatchBit, true); // (WS5: a blob this copy took, past its lockstep: its acknowledgement was lost; ReceiveAck)
        } else {
            std::fprintf(stderr, "catchup: a piece dropped: this copy does not take the state (player %d, source %d) [t %lld]\n", g_link.player,
                         (int) g_link.source, LogMs());
        }
        return;
    }

    if (g_link.player != 2) {
        return;
    }

    if ((std::int64_t) chunk.tick == g_link.resynced_at) {
        SendAck(chunk.tick);
        return;
    }

    if ((std::int64_t) chunk.tick != g_link.resync_tick) {
        return;
    }

    if (g_link.resync_state.size() != chunk.total) {
        g_link.resync_state.assign(chunk.total, 0);
        std::memset(g_link.resync_held, 0, sizeof g_link.resync_held);
    }

    std::size_t at = (std::size_t) chunk.index * kChunkBytes;
    std::size_t size = std::min<std::size_t>(kChunkBytes, chunk.total - at);

    std::memcpy(g_link.resync_state.data() + at, chunk.data, size);
    g_link.resync_held[chunk.index / 32] |= 1u << (chunk.index % 32);
    SendAck(chunk.tick);
}

// The host: which pieces the joiner holds.
// WS5 (l-local-play-houseout's and -slowreplay's FLAKE on her screen through his merge, beside a pinned PC): in a stayer's
// merge her copy sends the blobs, and its lockstep starts as its last blob goes out; EnterLockstep gives the source back to
// the host there (player 1), so SendsState() was false on her copy when his acknowledgement of that last blob came, and it was
// read as nothing. CatchSend sent the blob again on every pass for the rest of the link (62 to 367 times a run), and his copy,
// no longer the one that takes the state, dropped each piece unacknowledged with a log line on its game thread, in the ticks
// after the merge whose pads her copy waited on. Now the copy that sent the blob takes its acknowledgement, whoever is the
// source, and a copy past its lockstep acknowledges again a piece of a blob it took (one lost acknowledgement).
// DC_NETPLAY_MUTATE=last_blob_unheld (twin, l-local-play-houseout~unheld): both as before.
void ReceiveAck(const Ack &ack) {
    if ((ack.tick & kCatchBit) != 0) {
        const bool sent = NetplayMutated("last_blob_unheld") ? SendsState() : !g_catch.out.empty();

        if (sent && (ack.tick & ~kCatchBit) == g_catch.out_id) {
            for (int i = 0; i < kChunks / 32; i++) {
                g_catch.out_held[i] |= ack.held[i];
            }
        }

        return;
    }

    if (g_link.player == 1 && (std::int64_t) ack.tick == g_link.resync_tick) {
        for (int i = 0; i < kChunks / 32; i++) {
            g_link.resync_held[i] |= ack.held[i];
        }
    }
}

// coop-joinbudget (her late floor join under 100 ms of lag, paced: 6.7 s black against Jep's 4.9 s budget): the host's
// checkpoint came as her copy loaded its floor, one frame of some 1.3 s, and the game thread read nothing in it (3
// acknowledgements for 79 pieces, all read at once after), so the host's window could not grow and the first 2 MB part
// started its climb only after. While a catch-up runs on the copy that takes the state, a thread of its own reads the
// socket: each datagram goes, in order, to a queue the game thread's Receive reads as it read the socket, and the pieces of a
// catch-up blob are acknowledged from here at once (a piece queued is held: it is in this copy and cannot be lost). The thread
// touches only the socket, the queue and its own record of the blob's pieces; the game thread does all the rest, the pads
// and the game state included, so the lockstep is the same. It stops once the catch-up is over; the game thread reads the
// queue to its end before the socket again, so nothing is lost or reordered at either end. Direct links only (Steam's own
// queue holds its messages). DC_NETPLAY_MUTATE=catch_receive_inline (twin): no thread, as before.
// coop-small1 (coop-offthread's open: "her parts all go and are acknowledged within ~170 ms, but his game thread takes them
// 400-600 ms later"): the game thread read every piece the thread had read and acknowledged again, one datagram at a time
// (13-26 us each quiet, some 400 us beside a busy PC), in Receive's passes of kReceivePassMs once a frame, so a whole part
// waited 96-130 ms quiet, and up to some 900 ms under load, for the frames to get through its 2048 pieces. The thread puts a
// catch-up blob together itself and queues it whole (one item, in its place among the datagrams: where its last piece came);
// the game thread takes it in one (CatchReceiveWhole). DC_NETPLAY_MUTATE=rx_pieces_to_game (twin): each piece queued, as before.
struct RxItem {
    std::vector<unsigned char> bytes; // a datagram as read; or, whole, a blob's bytes (the thread's own, moved: no copy)
    bool                       whole = false;
    std::uint32_t              session = 0, tick = 0, total = 0, count = 0; // (whole: its pieces' head)
};

struct CatchReceiver {
    std::mutex                             lock;
    std::deque<RxItem>                     queue;
    std::thread                            thread;
    std::atomic<bool>                      stop{false};
    bool                                   running = false;
    // The thread's own (set before it starts, read back after it stops): the blob whose pieces it has seen, as the host's
    // acknowledgement wants them.
    std::uint32_t player = 0, session = 0, peer_session = 0;
    std::uint32_t id = 0, done = 0, count = 0;
    std::uint32_t held[kChunks / 32] = {};
    std::uint32_t acks = 0, datagrams = 0;
    std::uint32_t link = 0; // g_links_up as it started (the stall hook's link)
    // coop-small1: when each blob was whole on this thread (acknowledged to the host), by id % 64, so the game thread's take
    // says how long a whole blob waited for it ("whole on the receiver N ms before").
    std::atomic<std::uint32_t> whole_id[64] = {};
    std::atomic<long long>     whole_ms[64] = {};
    // coop-small1: the blob it puts together (id's): its bytes and its Chunk head; the last piece's time (NetplayCatchMoving);
    // the blobs it handed over whole and the catch-up pieces it queued one by one (the twin's, or a partial blob at its stop).
    std::vector<unsigned char> blob;
    Chunk                      head{};
    std::atomic<long long>     piece_ms{0};
    std::uint32_t              wholes = 0, pieces_out = 0, part_out = 0;

    // coop-towntask51 (train 51, l-local-play-houseout-towntask~twin: his copy quit while still taking a catch-up that never
    // ended, "libc++abi: terminating", exception 0xC0000409): the game left with the thread running, and a joinable
    // std::thread's destructor ends the program. Stopped and joined here as the program ends.
    ~CatchReceiver() {
        stop = true;
        if (thread.joinable()) {
            thread.join();
        }
    }
};

CatchReceiver g_rx;

// coop-quitcrash (x-fixture-join-quit): the game ending while this copy still takes a catch-up stops the receiver with the
// other exit work (netplay's atexit, before the statics go), and says so. The destructor above stays the backstop.
// DC_NETPLAY_MUTATE=rx_exit_unstopped (twin): not here, the destructor's join alone, unsaid.
void CatchReceiverStopAtExit() {
    if (!g_rx.thread.joinable() || NetplayMutated("rx_exit_unstopped")) {
        return;
    }
    g_rx.stop = true;
    g_rx.thread.join();
    g_rx.running = false;
    g_rx.blob.clear();
    std::fprintf(stderr, "catchup: receiver thread stopped as the game ends (catch-up %s) [t %lld]\n", g_catch.active ? "under way" : "over", LogMs());
    std::fflush(stderr);
}

bool CatchReceiverOn() {
    return g_rx.running;
}

// coop-small1: the receiver thread's last catch-up piece (NowMs), 0 none.
long long CatchReceiverPieceMs() {
    return g_rx.piece_ms.load(std::memory_order_relaxed);
}

// coop-small1: how long ago the receiver thread found blob id whole (and acknowledged it), -1 if it did not.
long long CatchReceiverWholeAgo(std::uint32_t id) {
    if (g_rx.whole_id[id % 64].load(std::memory_order_acquire) != id) {
        return -1;
    }
    return (long long) (NowMs() - g_rx.whole_ms[id % 64].load(std::memory_order_relaxed));
}

void CatchReceiverAck(std::uint32_t id, bool all) {
    Ack ack{};

    ack.magic = kAckMagic;
    ack.player = g_rx.player;
    ack.session = g_rx.session;
    ack.tick = kCatchBit | id;
    if (all) {
        std::memset(ack.held, 0xFF, sizeof ack.held);
    } else {
        std::memcpy(ack.held, g_rx.held, sizeof ack.held);
    }
    NetSocketSend(&ack, sizeof ack);
    g_rx.acks++;
}

// The receiver's record of a datagram it has: a catch-up piece is held (acknowledged at once when its blob is whole or an
// older one's, else with the pass's acknowledgement: ack_due). coop-small1: and put in its blob; true when the datagram is
// such a piece, which then reaches the game thread only with its blob, whole (or not at all: an older blob's, sent again).
// Under the lock (a whole blob goes on the queue here).
bool CatchReceiverTake(const unsigned char *data, int got, bool &ack_due) {
    Chunk chunk;

    if (got != (int) sizeof(Chunk)) {
        return false;
    }
    std::memcpy(&chunk, data, sizeof chunk);
    if (chunk.magic != kChunkMagic || chunk.player != 3 - g_rx.player || chunk.session != g_rx.peer_session || (chunk.tick & kCatchBit) == 0 ||
        chunk.index >= chunk.count || chunk.count == 0 || chunk.count > kChunks || chunk.total > (std::uint32_t) kChunks * kChunkBytes ||
        (std::size_t) chunk.index * kChunkBytes >= chunk.total) {
        return false;
    }
    const std::uint32_t id = chunk.tick & ~kCatchBit;
    const bool          assemble = !NetplayMutated("rx_pieces_to_game");

    g_rx.piece_ms.store(NowMs(), std::memory_order_relaxed);
    if (id <= g_rx.done) {
        CatchReceiverAck(id, true);
        return assemble;
    }
    if (id != g_rx.id || (assemble && g_rx.blob.size() != chunk.total)) {
        if (ack_due && id != g_rx.id) {
            CatchReceiverAck(g_rx.id, false);
        }
        if (id != g_rx.id) {
            std::memset(g_rx.held, 0, sizeof g_rx.held);
        }
        g_rx.id = id;
        g_rx.count = chunk.count;
        g_rx.blob.assign(assemble ? chunk.total : 0, 0);
    }
    g_rx.held[chunk.index / 32] |= 1u << (chunk.index % 32);
    ack_due = true;
    if (assemble) {
        const std::size_t at = (std::size_t) chunk.index * kChunkBytes;

        std::memcpy(g_rx.blob.data() + at, chunk.data, std::min<std::size_t>(kChunkBytes, chunk.total - at));
        g_rx.head = chunk;
    } else {
        g_rx.pieces_out++;
    }

    bool whole = true;

    for (std::uint32_t i = 0; i < g_rx.count && whole; i++) {
        whole = (g_rx.held[i / 32] & (1u << (i % 32))) != 0;
    }
    if (whole) {
        g_rx.done = id;
        g_rx.whole_ms[id % 64].store(NowMs(), std::memory_order_relaxed);
        g_rx.whole_id[id % 64].store(id, std::memory_order_release);
        CatchReceiverAck(id, true);
        ack_due = false;
        if (assemble) {
            RxItem item;

            item.whole = true;
            item.session = g_rx.head.session;
            item.tick = g_rx.head.tick;
            item.total = g_rx.head.total;
            item.count = g_rx.head.count;
            item.bytes.swap(g_rx.blob);
            g_rx.queue.push_back(std::move(item));
            g_rx.wholes++;
            g_rx.blob.clear();
        }
    }
    return assemble;
}

// coop-small1: a blob the thread had only part of as it stops goes to the game thread as its pieces, after what is queued, so
// the game thread holds what the host was told is held. Under the lock, the thread stopped.
void CatchReceiverPartOut() {
    if (g_rx.blob.empty() || g_rx.id <= g_rx.done) {
        g_rx.blob.clear();
        return;
    }
    for (std::uint32_t i = 0; i < g_rx.head.count; i++) {
        if ((g_rx.held[i / 32] & (1u << (i % 32))) == 0) {
            continue;
        }
        Chunk             piece = g_rx.head;
        const std::size_t at = (std::size_t) i * kChunkBytes;
        RxItem            item;

        piece.index = (std::uint16_t) i;
        std::memset(piece.data, 0, sizeof piece.data);
        std::memcpy(piece.data, g_rx.blob.data() + at, std::min<std::size_t>(kChunkBytes, g_rx.blob.size() - at));
        item.bytes.assign((const unsigned char *) &piece, (const unsigned char *) &piece + sizeof piece);
        g_rx.queue.push_back(std::move(item));
        g_rx.part_out++;
    }
    g_rx.blob.clear();
}

// coop-joinbudget (l-local-play-houseout-stall red with the receiver thread: blob 2's first 128 pieces came, the game thread's hook
// held the rest unacknowledged, but the thread had acknowledged them all, so the host never sent one again to end the stall,
// moved on to part 2, and part 1 was lost): DC_NETPLAY_TEST_ACK_STALL's stall is the reading side's, so with the thread on it
// is the thread's: once blob B of link L holds K pieces, its further datagrams are not queued or acknowledged until one comes
// that already waits; then all go on as read. The same lines as CatchStallHolds'.
bool RxStallHolds(const unsigned char *data, int got, std::vector<std::vector<unsigned char>> &release) {
    struct Stall {
        std::uint32_t link, blob, at;
        bool          over;
    };
    static std::vector<Stall> stalls = [] {
        std::vector<Stall> list;
        const char        *spec = TestEnv("DC_NETPLAY_TEST_ACK_STALL");

        for (const char *at = spec; at != nullptr && *at != '\0';) {
            unsigned link = 0, blob = 0, pieces = 0;

            if (std::sscanf(at, "%u:%u:%u", &link, &blob, &pieces) == 3) {
                list.push_back({link, blob, pieces, false});
            }
            at = std::strchr(at, ',');
            at = at != nullptr ? at + 1 : nullptr;
        }

        return list;
    }();
    static std::vector<std::vector<unsigned char>> waiting;
    static std::vector<std::uint16_t>              waiting_index;
    Chunk                                          chunk;

    if (stalls.empty() || got != (int) sizeof(Chunk)) {
        return false;
    }
    std::memcpy(&chunk, data, sizeof chunk);
    if (chunk.magic != kChunkMagic || (chunk.tick & kCatchBit) == 0) {
        return false;
    }
    const std::uint32_t id = chunk.tick & ~kCatchBit;
    std::uint32_t       held = 0;

    for (std::uint32_t i = 0; id == g_rx.id && i < g_rx.count; i++) {
        held += (g_rx.held[i / 32] >> (i % 32)) & 1u;
    }
    for (Stall &stall : stalls) {
        if (stall.over || stall.link != g_rx.link || stall.blob != id || (held < stall.at && waiting.empty())) {
            continue;
        }
        if (std::find(waiting_index.begin(), waiting_index.end(), chunk.index) == waiting_index.end()) {
            if (waiting.empty()) {
                std::fprintf(stderr, "catchup: test: blob %u stalls at %u pieces held [t %lld]\n", id, held, LogMs());
            }
            waiting.emplace_back(data, data + got);
            waiting_index.push_back(chunk.index);
            return true;
        }
        stall.over = true;
        std::fprintf(stderr, "catchup: test: blob %u's stall ends on piece %u sent again: %zu pieces waited [t %lld]\n", id, (unsigned) chunk.index,
                     waiting.size(), LogMs());
        release.swap(waiting);
        release.emplace_back(data, data + got);
        waiting.clear();
        waiting_index.clear();
        return true;
    }
    return false;
}

// coop-houseblack test hook (DC_NETPLAY_TEST_TAIL_DROP=L:B:N, the copy that takes the state, with the receiver thread): the
// first arrival of each of the last N pieces of blob B of link L's catch-up is dropped unread, a part's lost tail. Jep's
// 2026-11-04 pack 8 playtest: player 2 out of a house to him in town, the state set's pieces 1800 over the internet, the
// first burst's losses resent but a tail of them then went one probe each overdue time: her screen black 117 s (the lost
// tail's livelock coop-joinbudget fixed). The relay's --tail-drop, on one PC with no relay.
bool RxTailDrops(const unsigned char *data, int got) {
    static unsigned link = 0, blob = 0, tail = 0;
    static bool     parsed = false;
    static std::vector<std::uint16_t> dropped;
    Chunk           chunk;

    if (!parsed) {
        parsed = true;
        const char *spec = TestEnv("DC_NETPLAY_TEST_TAIL_DROP");

        if (spec == nullptr || std::sscanf(spec, "%u:%u:%u", &link, &blob, &tail) != 3) {
            tail = 0;
        }
    }
    if (tail == 0 || got != (int) sizeof(Chunk)) {
        return false;
    }
    std::memcpy(&chunk, data, sizeof chunk);
    if (chunk.magic != kChunkMagic || (chunk.tick & kCatchBit) == 0 || g_rx.link != link || (chunk.tick & ~kCatchBit) != blob ||
        chunk.count == 0 || chunk.index + tail < chunk.count) {
        return false;
    }
    if (std::find(dropped.begin(), dropped.end(), chunk.index) != dropped.end()) {
        return false;
    }
    dropped.push_back(chunk.index);
    // (the line whole in one write: this is the receiver thread, and the game thread's printf lines went into the middle of
    // a fprintf here, so x-pagediff-floor-tailloss once missed its mode line, train 51)
    if (dropped.size() == 1 || dropped.size() == std::min<std::size_t>(tail, chunk.count)) {
        char line[160];
        const int length = std::snprintf(line, sizeof line, "catchup: test: blob %u's lost tail: %zu of its last %u pieces dropped (of %u) [t %lld]\n", blob,
                                         dropped.size(), tail, (unsigned) chunk.count, LogMs());

        std::fwrite(line, 1, (std::size_t) std::max(0, std::min(length, (int) sizeof line - 1)), stderr);
    }
    return true;
}

void CatchReceiverRun() {
    alignas(8) unsigned char buffer[sizeof(Chunk) + 64];

    while (!g_rx.stop.load()) {
        // A pass: what the socket holds, then one acknowledgement for the blob (the game thread's batching, coop-ackbatch).
        bool ack_due = false;
        int  got;

        while ((got = NetSocketReceive(buffer, sizeof buffer)) > 0) {
            std::vector<std::vector<unsigned char>> release;

            g_rx.datagrams++;
            if (RxTailDrops(buffer, got)) {
                continue;
            }
            if (RxStallHolds(buffer, got, release)) {
                for (const std::vector<unsigned char> &kept : release) {
                    std::lock_guard<std::mutex> hold(g_rx.lock);

                    if (!CatchReceiverTake(kept.data(), (int) kept.size(), ack_due)) {
                        g_rx.queue.push_back(RxItem{kept, false});
                    }
                }
                continue;
            }
            std::lock_guard<std::mutex> hold(g_rx.lock);

            if (!CatchReceiverTake(buffer, got, ack_due)) {
                g_rx.queue.push_back(RxItem{std::vector<unsigned char>(buffer, buffer + got), false});
            }
        }
        if (ack_due) {
            CatchReceiverAck(g_rx.id, false);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}


// coop-replayload: the pieces kept from before this copy linked, now it has, if they are this link's.
// coop-small1b (l-local-play-houseout and -stall on bin-m4: her copy read 128 of the checkpoint's 157 pieces before it
// linked, and blob 1 never came whole; she gave up the catch-up after 240 s): CatchReceiverTick starts the receiver thread
// with the game thread's part of the blob, and the first of these pieces started it: CatchReceiveChunk begins the catch-up
// and calls CatchReceiverTick before it places the piece, so the receiver began on blob 0 with nothing, and the rest of
// these went into g_catch.in after the receiver had its copy; the receiver held only the other 29, and each side
// acknowledged its own share, so his copy counted the blob held. Now no receiver starts while these go in (g_prelink_taking);
// it starts after, with the part of blob 1 they make (the receiver's start takes a part the game thread holds).
// DC_NETPLAY_MUTATE=prelink_after_receiver (l-local-play-houseout-stall~prelate): the receiver starts inside, as before.
bool g_prelink_taking = false;

void TakePrelinkPieces() {
    if (g_link.up && !g_prelink_pieces.empty()) {
        std::vector<Chunk> kept;

        kept.swap(g_prelink_pieces);
        if (g_prelink_session == g_link.peer_hello.session) {
            const bool before = g_rx.running;

            std::fprintf(stderr, "catchup: %zu pieces read before this copy linked, taken now [t %lld]\n", kept.size(), LogMs());
            g_prelink_taking = !NetplayMutated("prelink_after_receiver");
            for (const Chunk &chunk : kept) {
                ReceiveChunk(chunk);
            }
            g_prelink_taking = false;
            if (g_rx.running) {
                // (the twin's stop line: no receiver runs here unless the mutation let one start, inside or before)
                std::fprintf(stderr, "catchup: the receiver thread started without the pieces read before the link (%s)\n", before ? "before" : "inside");
            }
        }
    }
}

// The game thread, each frame and as a catch-up begins: the receiver on while this copy takes the host's catch-up.
void CatchReceiverTick() {
    // (from the link on, the state still to come: her floor's load came between the link and the checkpoint's first piece, so
    // a receiver started at that piece started after the load, paced-b5)
    const bool want = TakesState() && g_link.up && (g_catch.active || g_link.awaiting_sync) && NetSteamSelf() == 0 && !NetplayMutated("catch_receive_inline");

    if (g_prelink_taking) {
        return; // coop-small1b: the pieces read before the link go in first (TakePrelinkPieces), then the receiver starts
    }
    if (want && !g_rx.running) {
        // (coop-small1b: those pieces first, so the receiver starts with their part of the blob; the twin's: ReceivePass, after)
        if (!NetplayMutated("prelink_after_receiver")) {
            TakePrelinkPieces();
        }
        g_rx.player = (std::uint32_t) g_link.player;
        g_rx.session = g_link.session;
        g_rx.peer_session = g_link.peer_hello.session;
        g_rx.id = g_catch.in_id;
        g_rx.done = g_catch.in_done;
        g_rx.count = (std::uint32_t) ((g_catch.in.size() + kChunkBytes - 1) / kChunkBytes);
        std::memcpy(g_rx.held, g_catch.in_held, sizeof g_rx.held);
        g_rx.acks = g_rx.datagrams = g_rx.wholes = g_rx.pieces_out = g_rx.part_out = 0;
        g_rx.link = (std::uint32_t) g_links_up;
        // coop-small1: the blob the game thread has part of goes on here, with its bytes (the held bits above say which).
        g_rx.blob = g_catch.in_id > g_catch.in_done ? g_catch.in : std::vector<unsigned char>{};
        g_rx.head = Chunk{};
        g_rx.head.magic = kChunkMagic;
        g_rx.head.player = 3 - g_rx.player;
        g_rx.head.session = g_rx.peer_session;
        g_rx.head.tick = kCatchBit | g_catch.in_id;
        g_rx.head.total = (std::uint32_t) g_catch.in.size();
        g_rx.head.count = (std::uint16_t) g_rx.count;
        {
            // (the datagrams queued already, the pieces kept from before the link among them: held, and said so at once; a
            // catch-up piece goes into its blob, the rest stay queued in order)
            std::lock_guard<std::mutex> hold(g_rx.lock);
            bool                        ack_due = false;
            std::deque<RxItem>          kept;

            kept.swap(g_rx.queue);
            for (RxItem &item : kept) {
                if (item.whole || !CatchReceiverTake(item.bytes.data(), (int) item.bytes.size(), ack_due)) {
                    g_rx.queue.push_back(std::move(item));
                }
            }
            if (ack_due) {
                CatchReceiverAck(g_rx.id, false);
            }
        }
        g_rx.stop = false;
        g_rx.running = true;
        g_rx.thread = std::thread(CatchReceiverRun);
        std::fprintf(stderr, "catchup: receiver thread on (blob %u) [t %lld]\n", g_rx.id, LogMs());
    } else if (!want && g_rx.running) {
        g_rx.stop = true;
        g_rx.thread.join();
        g_rx.running = false;
        {
            std::lock_guard<std::mutex> hold(g_rx.lock);

            CatchReceiverPartOut();
        }
        std::fprintf(stderr, "catchup: receiver thread off: %u datagrams read, %u acknowledgements sent; %u blobs handed over whole, %u pieces one by one (and %u of a blob held in part) [t %lld]\n",
                     g_rx.datagrams, g_rx.acks, g_rx.wholes, g_rx.pieces_out, g_rx.part_out, LogMs());
    }
}

// Receive's next datagram: the receiver's queue first (while it runs, or until the game thread has read what it left), then
// the socket.
// coop-small1: a catch-up blob the receiver thread put together (its pieces' head, its bytes), taken as its pieces were,
// in one: on the link it came on, by the copy that takes the state (else dropped, as its pieces were in Receive).
void CatchReceiveWhole(RxItem &item) {
    if (!g_link.up || !g_link.peer_hello_got || item.session != g_link.peer_hello.session || !TakesState() || item.bytes.size() != item.total ||
        item.count == 0 || item.count > kChunks || item.total > (std::uint32_t) kChunks * kChunkBytes) {
        std::fprintf(stderr, "catchup: a whole blob dropped: link %s, its session %08x, the Hello's %08x [t %lld]\n", g_link.up ? "up" : "down",
                     item.session, g_link.peer_hello.session, LogMs());
        return;
    }
    Chunk head{};

    head.magic = kChunkMagic;
    head.player = (std::uint32_t) (3 - g_link.player);
    head.session = item.session;
    head.tick = item.tick;
    head.total = item.total;
    head.count = (std::uint16_t) item.count;
    g_catch_whole = &item.bytes;
    CatchReceiveChunk(head);
    g_catch_whole = nullptr;
    g_catch.piece_at = std::chrono::steady_clock::now();
}

// coop-small1: a whole blob from the thread is not a datagram: it goes to g_rx_whole and ReceiveNext says kRxWholeGot.
constexpr int kRxWholeGot = -2;
RxItem        g_rx_whole;

int ReceiveNext(void *data, std::size_t size) {
    {
        std::lock_guard<std::mutex> hold(g_rx.lock);

        if (!g_rx.queue.empty()) {
            RxItem &front = g_rx.queue.front();

            if (front.whole) {
                g_rx_whole = std::move(front);
                g_rx.queue.pop_front();
                return kRxWholeGot;
            }
            const int got = (int) std::min(size, front.bytes.size());

            std::memcpy(data, front.bytes.data(), (std::size_t) got);
            g_rx.queue.pop_front();
            return got;
        }
        if (g_rx.running) {
            return 0;
        }
    }
    return NetSocketReceive(data, size);
}

// DC_NETPLAY_TEST_RECEIVE_COST=from:window:ms (load-receive-pass): from link tick from, for window ms of wall time, each
// datagram this copy's Receive takes costs it ms more, as a dear log line per datagram did beside a full PC (WS5's
// 18.5 s wait; kReceivePassMs). At the window's end one line: the datagrams taken, the longest pass, the passes cut.
struct ReceiveCost {
    long long                             from = -1, window = 0, ms = 0;
    bool                                  read = false, on = false, said = false;
    std::chrono::steady_clock::time_point began;
    long long                             taken = 0, cuts_from = 0;
    double                                longest = 0.0;
};
ReceiveCost g_receive_cost;

void ReceiveCostOver() {
    ReceiveCost &c = g_receive_cost;

    c.said = true;
    std::fprintf(stderr, "netplay: test: receive cost over at tick %lld: %lld datagrams taken, longest pass %.0f ms, %lld passes cut [t %lld]\n",
                 (long long) g_link.tick, c.taken, c.longest, g_link.receive_cuts - c.cuts_from, LogMs());
}

bool ReceiveCostOn() {
    ReceiveCost &c = g_receive_cost;

    if (!c.read) {
        c.read = true;
        const char *at = TestEnv("DC_NETPLAY_TEST_RECEIVE_COST");

        if (at == nullptr || std::sscanf(at, "%lld:%lld:%lld", &c.from, &c.window, &c.ms) < 3) {
            c.from = -1;
        }
    }
    if (c.from < 0 || c.said || !g_link.up || g_link.tick < c.from) {
        return false;
    }
    const auto now = std::chrono::steady_clock::now();

    if (!c.on) {
        c.on = true;
        c.began = now;
        c.cuts_from = g_link.receive_cuts;
        std::fprintf(stderr, "netplay: test: receive cost %lld ms a datagram from tick %lld for %lld ms [t %lld]\n", c.ms, (long long) g_link.tick,
                     c.window, LogMs());
    }
    if (now - c.began > std::chrono::milliseconds(c.window)) {
        ReceiveCostOver();
        return false;
    }
    return true;
}

void ReceivePass(std::chrono::steady_clock::time_point pass_from);

void Receive() {
    const auto from = std::chrono::steady_clock::now();
    const bool cost = ReceiveCostOn();

    ReceivePass(from);
    if (cost) {
        g_receive_cost.longest = std::max(g_receive_cost.longest, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - from).count());
    }
}

void ReceivePass(std::chrono::steady_clock::time_point pass_from) {
    alignas(8) unsigned char buffer[sizeof(Chunk) + 64];
    auto                     now = pass_from;

    // Whatever waited in the socket since a stall of this copy's (no read for kRttStallMs) measures the
    // stall, not the round trip.
    if (now - g_link.last_receive_at > std::chrono::milliseconds(kRttStallMs)) {
        g_link.rtt_quiet_until = now + std::chrono::milliseconds(kRttQuietMs);
    }

    g_link.last_receive_at = now;

    TakePrelinkPieces();

    for (long long taken = 0;; taken++) {
        // coop-ws5left: kReceivePassMs a pass at most (one datagram at least), then the tick looks at what came.
        // DC_NETPLAY_MUTATE=receive_drains_all (load-receive-pass~twin): until the socket is empty, as before.
        if (taken > 0 && !NetplayMutated("receive_drains_all") &&
            std::chrono::steady_clock::now() - pass_from > std::chrono::milliseconds(kReceivePassMs)) {
            g_link.receive_cuts++;
            const auto at = std::chrono::steady_clock::now();

            if (at - g_link.receive_cut_said > std::chrono::seconds(1)) {
                g_link.receive_cut_said = at;
                std::fprintf(stderr, "netplay: a receive pass cut after %lld datagrams, %.0f ms (the rest on the next; %lld cut this link) [t %lld]\n",
                             taken, std::chrono::duration<double, std::milli>(at - pass_from).count(), g_link.receive_cuts, LogMs());
            }
            SendAcksDue();
            return;
        }

        int got = ReceiveNext(buffer, sizeof buffer); // (coop-joinbudget: the catch-up receiver's queue first)

        if (got == kRxWholeGot) { // coop-small1: a catch-up blob the receiver thread put together
            CatchReceiveWhole(g_rx_whole);
            continue;
        }
        if (got < 8) {
            SendAcksDue();
            return;
        }
        if (g_receive_cost.on && !g_receive_cost.said) {
            g_receive_cost.taken++;
            std::this_thread::sleep_for(std::chrono::milliseconds(g_receive_cost.ms));

            // The window ends inside a pass too, so a pass that never returns (the twin's) still says its length.
            const auto at = std::chrono::steady_clock::now();

            if (at - g_receive_cost.began > std::chrono::milliseconds(g_receive_cost.window)) {
                g_receive_cost.longest = std::max(g_receive_cost.longest, std::chrono::duration<double, std::milli>(at - pass_from).count());
                ReceiveCostOver();
            }
        }

        std::uint32_t magic;
        std::uint32_t player;

        std::memcpy(&magic, buffer, 4);
        std::memcpy(&player, buffer + 4, 4);

        // coop-lobby: the lobby's own messages (a joiner waiting in the host's lobby hears its table here).
        if (coop::lobby::Receive(magic, buffer, got)) {
            continue;
        }

        if ((int) player != 3 - g_link.player) {
            continue;
        }

        g_link.heard_datagrams++;
        for (int i = 0; i < 8 && StallWatchOn(); i++) {
            if (g_stall_watch.magics[i] == magic || g_stall_watch.magics[i] == 0) {
                g_stall_watch.magics[i] = magic;
                g_stall_watch.magic_count[i]++;
                break;
            }
        }

        // coop-worlds: its own messages ("WWCD"), linked or apart.
        if (CoopWorldsReceive(magic, buffer, got)) {
            continue;
        }

        // coop-partnerlog: the other copy's log lines and its acknowledgements of this one's, linked or apart.
        const auto log_from = std::chrono::steady_clock::now();

        if (PartnerLogReceive(magic, buffer, got)) {
            if (!StallWatchOn()) {
                continue;
            }
            const double log_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - log_from).count();

            g_stall_watch.log_ms += log_ms;
            g_stall_watch.log_most = std::max(g_stall_watch.log_most, log_ms);
            g_stall_watch.log_pieces++;
            continue;
        }

        // coop-hellosize51 (train 51's x-protocol-refuse red): a Hello of another size is another protocol's (buildid's
        // protocol 16 grew it), so it is kept as far as it reaches, its protocol read, and the link refused as a different
        // version of the link, never dropped unheard (both copies listened on, 0 Hellos). A size that differs with the same
        // protocol says protocol 0 (refused the same way). DC_NETPLAY_MUTATE=hello_size_exact (x-protocol-refuse~hellosize):
        // only an exact size is a Hello, as before.
        const int helloHead = (int) (offsetof(NetplayHello, protocol) + sizeof(std::uint32_t));
        if (magic == kNetplayHelloMagic && got != (int) sizeof(NetplayHello) && got >= helloHead && !NetplayMutated("hello_size_exact")) {
            std::memset(&g_link.peer_hello, 0, sizeof g_link.peer_hello);
            std::memcpy(&g_link.peer_hello, buffer, std::min((std::size_t) got, sizeof g_link.peer_hello));
            if (g_link.peer_hello.protocol == NetplayHelloProtocol()) {
                g_link.peer_hello.protocol = 0;
            }
            static int said = 0;
            if (said != got) {
                said = got;
                std::fprintf(stderr, "netplay: a Hello of %d bytes (this protocol's is %d), protocol %u\n", got, (int) sizeof(NetplayHello),
                             (unsigned) g_link.peer_hello.protocol);
            }
            g_link.peer_hello_got = true;
            g_link.heard_hellos++;
            NetSocketKeepSender();
            continue;
        }
        if (magic == kNetplayHelloMagic && got == (int) sizeof(NetplayHello)) {
            std::memcpy(&g_link.peer_hello, buffer, sizeof g_link.peer_hello);
            g_link.peer_hello_got = true;
            g_link.heard_hellos++;
            NetSocketKeepSender();
            continue;
        }

        // Anything else must come from the session this copy linked with.
        std::uint32_t session;

        std::memcpy(&session, buffer + 8, 4);

        // coop-worlds: the other copy's link traffic (its pads, its state) under the session its Hellos give while this copy
        // still listens: it has started the merge's link (NetplayPeerLinking).
        // (coop-catchupdrop: not its Leave: a copy that gave up a catch-up says so under that link's session, its last Hello's)
        if (!g_link.up && session == g_link.peer_hello.session && session != g_link.last_peer_session && magic != kLeaveMagic) {
            // coop-natsplitflake: which of its messages said so, once per session of its, for the logs.
            static std::uint32_t said = 0;

            if (said != session) {
                said = session;
                std::fprintf(stderr, "netplay: the other copy's link traffic heard while listening (magic %08x, %d bytes, session %08x)\n",
                             (unsigned) magic, got, (unsigned) session);
            }
            g_link.peer_linking_at = std::chrono::steady_clock::now();
            g_link.peer_linking_session = session;
        }

        // (the last Hello heard, not one in this pass: Listen clears peer_hello_got each pass, and the host's linking Hellos
        // stop once it has linked, so most of its first pieces come in passes without one)
        if (!g_link.up && g_link.peer_hello.session != 0 && session == g_link.peer_hello.session && session != g_link.last_peer_session &&
            magic == kChunkMagic && got == (int) sizeof(Chunk)) {
            g_prelink_seen++;

            if (!NetplayMutated("pieces_before_link_dropped") && g_prelink_pieces.size() < (std::size_t) kChunks * 2) {
                if (g_prelink_session != session) {
                    g_prelink_pieces.clear();
                    g_prelink_session = session;
                }
                g_prelink_pieces.emplace_back();
                std::memcpy(&g_prelink_pieces.back(), buffer, sizeof(Chunk));
                continue;
            }
        }

        if (!g_link.up || !g_link.peer_hello_got || session != g_link.peer_hello.session) {
            // coop-replayload: a catch-up piece read and dropped says why (his merge's checkpoint lost 31 of its first 32).
            if (magic == kChunkMagic) {
                std::fprintf(stderr, "catchup: a piece dropped unread: link %s, Hello %s, its session %08x, the Hello's %08x [t %lld]\n",
                             g_link.up ? "up" : "down", g_link.peer_hello_got ? "heard" : "not heard", session, g_link.peer_hello.session, LogMs());
            }
            continue;
        }

        g_link.link_heard = true;
        g_catch.heard = std::chrono::steady_clock::now();

        if (magic == kMagic && got == (int) sizeof(Packet)) {
            Packet packet;
            std::memcpy(&packet, buffer, sizeof packet);
            ReceiveTick(packet);
        } else if (magic == kChunkMagic && got == (int) sizeof(Chunk)) {
            Chunk chunk;
            std::memcpy(&chunk, buffer, sizeof chunk);
            ReceiveChunk(chunk);
        } else if (magic == kBusyMagic && got == (int) sizeof(Busy)) {
            g_link.peer_busy_at = std::chrono::steady_clock::now();
        } else if (magic == kLeaveMagic && got == (int) sizeof(Busy)) {
            Busy leave;
            std::memcpy(&leave, buffer, sizeof leave);
            g_link.peer_left = true;
            g_link.peer_left_done = leave.unused == 1;
            g_link.peer_gave_up = leave.unused == kLeaveGaveUp;
            g_link.peer_ended = leave.unused == kLeaveEnded && leave.player == 1; // coop-quittitle: only the host's ends it
            g_link.peer_apart_at = (leave.unused & kCatchBit) != 0 ? (std::int64_t) (leave.unused & ~kCatchBit) : -1;
        } else if (magic == kPageNeedMagic && got == (int) sizeof(PageNeed)) {
            PageNeed need;
            std::memcpy(&need, buffer, sizeof need);
            const std::uint32_t words = (need.pages + 31) / 32;

            if (SendsState() && g_snap.on && g_snap.page_step == 2 && need.first < words && need.first % kPageNeedWords == 0) {
                if (g_snap.need.size() != words) {
                    g_snap.need.assign(words, 0);
                    g_snap.need_got.assign((words + kPageNeedWords - 1) / kPageNeedWords, false);
                }
                std::memcpy(g_snap.need.data() + need.first, need.words, std::min<std::uint32_t>(kPageNeedWords, words - need.first) * 4);
                g_snap.need_got[need.first / kPageNeedWords] = true;
            }
        } else if (magic == kPageWholeMagic && got == (int) sizeof(Busy)) {
            if (SendsState() && g_snap.on && !g_snap.whole_asked) {
                g_snap.whole_asked = true;
                std::fprintf(stderr, "pagediff: the taker asks for the whole state set\n");
            }
        } else if (magic == kSnapRefusedMagic && got == (int) sizeof(Busy)) {
            if (SendsState() && g_snap.on && !g_snap.refused) {
                g_snap.refused = true;
                std::fprintf(stderr, "catchup: the joiner refused the snapshot\n");
            }
        } else if (magic == kRestartMagic && got == (int) sizeof(Busy)) {
            Busy ask;
            std::memcpy(&ask, buffer, sizeof ask);
            // (an ask from before the last fresh set's apply, still in flight, is that set's: his tick is under its S)
            if (SendsState() && g_catch.active && g_snap.on && g_snap.pads_begun && g_catch.join_tick < 0 && !g_snap.take_due &&
                g_snap.sent >= g_snap.parts && (std::int64_t) ask.unused >= g_catch.restart_tick && !NetplayMutated("no_replay_fallback")) {
                g_catch.restarts++;
                g_snap.take_due = true;
                g_snap.taken = false;
                g_snap.due_at = std::chrono::steady_clock::now();
                g_snap.due_drawn = g_frames_drawn;
                g_snap.parts = g_snap.sent = 0;
                g_snap.mark = kSnapMark;
                g_snap.page_step = 0;
                g_snap.raw.reset(); // (coop-offthread: a shared set; this copy lets go of it)
                g_snap.need.clear();
                g_snap.need_got.clear();
                g_snap.whole_asked = false;
                g_snap.job.reset(); // (cand57: a page job of the last set still on its thread lets go; offthread's SnapJob)
                std::fprintf(stderr, "catchup: player %u's replay is not gaining (his tick %u, this copy's %lld): a fresh state set (fallback %d) [t %lld]\n",
                             ask.player, ask.unused, (long long) g_link.tick, g_catch.restarts, LogMs());
            }
        } else if (magic == kCaughtMagic && got == (int) sizeof(Busy)) {
            Busy caught;
            std::memcpy(&caught, buffer, sizeof caught);
            g_catch.caught = std::max(g_catch.caught, (std::int64_t) caught.unused);
        } else if (magic == kAckMagic && got == (int) sizeof(Ack)) {
            Ack ack;
            std::memcpy(&ack, buffer, sizeof ack);
            ReceiveAck(ack);
        }
    }
}

void TakeCamera(CameraState &out, CCameraFollow &camera) {
    camera.GetPos(out.pos);
    camera.GetRef(out.ref);
    std::memcpy(out.follow, camera.follow, sizeof out.follow);
    out.distance = camera.distance;
    out.height = camera.height;
    out.next_angle = camera.next_angle;
    out.angle = camera.angle;
    out.follow_on = camera.follow_on;
}

void PutCamera(CameraState in, CCameraFollow &camera) {
    camera.SetPos(in.pos);
    camera.SetRef(in.ref);
    std::memcpy(camera.follow, in.follow, sizeof camera.follow);
    camera.distance = in.distance;
    camera.height = in.height;
    camera.next_angle = in.next_angle;
    camera.angle = in.angle;
    camera.follow_on = in.follow_on;
}

// The host's state for a resync: the random generator, where it is, pad 2's member and its SaveData,
// which holds the party status, the inventory, the towns and the story.
// coop-carried (root-cause analysis 2026-10-30, cause 6; Jep approved): state the lockstep's checksum reads that the save does
// not hold, declared once. Each part has its hash (what the checksum adds), its take (its bytes, appended to a resync's state
// by the host) and its put (what each copy does with the host's bytes after the resync's load). Items 2 (a setting changed
// mid-link looped resyncs: settings checked, never resent) and 38 (a resync inside a building reloaded the town's gate: the
// place not carried) were each one part missing; a part added here is checked and carried together.
// DC_NETPLAY_MUTATE=carried_off (the class check's fix-off): no part carried, as before the list.
struct CarriedPart {
    const char *name;
    std::uint32_t (*hash)();
    void (*take)(std::vector<unsigned char> &out);
    void (*put)(const unsigned char *data, std::size_t size);
};

// The settings both copies must share (config.hpp): the joiner takes the host's into its config.json, as at a join.
std::uint32_t SettingsPartHash() {
    return NetplaySettingsHash();
}
void SettingsPartTake(std::vector<unsigned char> &out) {
    NetplayHello hello{};

    NetplayHelloSettings(hello);
    const auto *bytes = reinterpret_cast<const unsigned char *>(&hello);
    out.insert(out.end(), bytes, bytes + sizeof hello);
}
void SettingsPartPut(const unsigned char *data, std::size_t size) {
    NetplayHello host{}, mine{};

    if (g_link.player != 2 || size != sizeof host) {
        return;
    }
    std::memcpy(&host, data, sizeof host);
    NetplayHelloSettings(mine);
    if (NetplayHelloSettingsDiffer(mine, host)) {
        NetplayHelloAdopt(host);
        std::fprintf(stderr, "netplay: carried: the host's settings taken (settings hash %08x now)\n", NetplaySettingsHash());
    }
}

// The place inside a town: the building both stand in (the save's load is the town's), gone back into on the first walk.
std::uint32_t PlacePartHash() {
    return mode == GAME_MODE_EDIT && EdInteriorFlag != 0 ? (std::uint32_t) CoopWorldsInteriorId(EdInteriorName) : 0u;
}
void PlacePartTake(std::vector<unsigned char> &out) {
    char               name[32] = {};
    const std::int32_t id = (std::int32_t) PlacePartHash();

    if (id != 0) {
        std::snprintf(name, sizeof name, "%s", EdInteriorName);
    }
    const auto *bytes = reinterpret_cast<const unsigned char *>(&id);
    out.insert(out.end(), bytes, bytes + sizeof id);
    out.insert(out.end(), name, name + sizeof name);
}
void PlacePartPut(const unsigned char *data, std::size_t size) {
    std::int32_t id = 0;
    char         name[32] = {};

    if (size != sizeof id + sizeof name) {
        return;
    }
    std::memcpy(&id, data, sizeof id);
    std::memcpy(name, data + sizeof id, sizeof name);
    name[sizeof name - 1] = '\0';
    if (id > 0) {
        CoopWorldsResyncReenter(id, name);
    }
}

// coop-soakfix2 (the soak's apart seed 26111101, red 4 of 6: OUT OF SYNC at tick 60 after her house-exit merge, parts view and
// stages): Toan's last step (MainChara.move_info: landed, the ground under him) outlives a town's load, and EdMoveChara's next
// step reads it (a landed step's speed is scaled by that ground's normal). The merge's resync reloads the town on both copies,
// so each kept its own: her copy's Toan had last stood on the mayor's doorstep (normal y 0.868), his copy's on flat ground
// (1.0); Toan's first walk step after the load (link tick 39, his stick held) went 1.15 times as far on his copy. The stand
// warp then put Toan on one spot on both, but the town camera had already followed the two steps (its follow -771.40 against
// -772.54 at tick 40) and stayed apart. Whether a replay goes red is whether her copy's Toan last stood on the slope. The
// host's MainChara (static, never reloaded) is carried whole; the hash reads what the step reads, in a town.
// DC_NETPLAY_MUTATE=toan_step_own (twin): each copy keeps its own, as before.
std::uint32_t ToanStepPartHash() {
    if (mode != GAME_MODE_EDIT) {
        return 0u;
    }
    const MoveCheckInfo &mi = MainChara.move_info;
    std::uint32_t        hash = 2166136261u;
    const std::int32_t   landed = mi.landed;
    const float         *normal = mi.ground_poly.normal;

    for (const auto *p = reinterpret_cast<const unsigned char *>(&landed); p != reinterpret_cast<const unsigned char *>(&landed + 1); p++) {
        hash = (hash ^ *p) * 16777619u;
    }
    for (const auto *p = reinterpret_cast<const unsigned char *>(normal); p != reinterpret_cast<const unsigned char *>(normal + 3); p++) {
        hash = (hash ^ *p) * 16777619u;
    }
    return hash;
}
void ToanStepPartTake(std::vector<unsigned char> &out) {
    const auto *bytes = reinterpret_cast<const unsigned char *>(&MainChara.move_info);
    out.insert(out.end(), bytes, bytes + sizeof(MoveCheckInfo));
}
void ToanStepPartPut(const unsigned char *data, std::size_t size) {
    MoveCheckInfo host;

    if (size != sizeof host) {
        return;
    }
    std::memcpy(&host, data, sizeof host);
    const MoveCheckInfo &mine = MainChara.move_info;
    const bool           own = NetplayMutated("toan_step_own");

    if (mine.landed != host.landed || std::memcmp(mine.ground_poly.normal, host.ground_poly.normal, 3 * sizeof(float)) != 0) {
        std::fprintf(stderr, "netplay: carried: Toan's last step differs here (landed %d, ground normal y %.5f; the host's landed %d, %.5f): %s\n",
                     (int) mine.landed, mine.ground_poly.normal[1], (int) host.landed, host.ground_poly.normal[1],
                     own ? "kept its own (toan_step_own)" : "the host's taken");
    }
    if (!own) {
        std::memcpy(&MainChara.move_info, &host, sizeof host);
    }
}

// coop-repinfix1 (the soak's apart 1341 and leave 1047, PC-02 job 17 and PC-01: OUT OF SYNC at tick 60 after the merge's or
// the rejoin's resync, view then stages): the town's camera (MainCamera) outlives a town's load as each copy left it, and
// apart (or on a dropped player's new copy, fresh) the copies' cameras are not alike. EditInit only re-aims it (SetFollow
// 0 0 0, a step, SetAngleSoon), so it keeps each copy's own angle round the map's origin (eye 43.4 5 54.9 on his copy,
// 32.7 5 -61.9 on hers, both looking at 0 0 0), and the load's town event (128) copies it into EventCamera, which is the
// view until the event's _RESET_CAMERA puts the camera behind Toan alike on both. That is the event's first tick, so the
// view was apart only for ticks no checksum read, unless the event outlives a checksum tick: in the seeds, his random Start
// (a --jump start's DebugMode: EdEventMode's debug pause) held the event from link tick 33 to 66, over tick 60. The host's
// MainCamera is carried and put before the load (as the dungeon's MainCamera__4 in the header), so the load starts from
// one camera on every copy; the hash reads its eye and look-at in a town (what the view reads).
// DC_NETPLAY_MUTATE=town_camera_own (twin): each copy keeps its own, as before.
std::uint32_t TownCameraPartHash() {
    if (mode != GAME_MODE_EDIT) {
        return 0u;
    }
    sceVu0FVECTOR eye_ref[2];
    std::uint32_t hash = 2166136261u;

    MainCamera.GetPos(eye_ref[0]);
    MainCamera.GetRef(eye_ref[1]);
    for (int v = 0; v < 2; v++) {
        for (const auto *p = reinterpret_cast<const unsigned char *>(eye_ref[v]); p != reinterpret_cast<const unsigned char *>(eye_ref[v] + 3); p++) {
            hash = (hash ^ *p) * 16777619u;
        }
    }
    return hash;
}
void TownCameraPartTake(std::vector<unsigned char> &out) {
    CameraState state{};

    TakeCamera(state, MainCamera);
    const auto *bytes = reinterpret_cast<const unsigned char *>(&state);
    out.insert(out.end(), bytes, bytes + sizeof state);
}
void TownCameraPartPut(const unsigned char *data, std::size_t size) {
    CameraState host;
    CameraState mine{};

    if (size != sizeof host) {
        return;
    }
    std::memcpy(&host, data, sizeof host);
    TakeCamera(mine, MainCamera);
    const bool own = NetplayMutated("town_camera_own");

    if (std::memcmp(&mine, &host, sizeof host) != 0) {
        std::fprintf(stderr, "netplay: carried: the town camera differs here (angle %.4f eye %.2f %.2f %.2f; the host's angle %.4f eye %.2f %.2f %.2f): %s\n",
                     mine.angle, mine.pos[0], mine.pos[1], mine.pos[2], host.angle, host.pos[0], host.pos[1], host.pos[2],
                     own ? "kept its own (town_camera_own)" : "the host's taken");
    }
    if (!own) {
        PutCamera(host, MainCamera);
    }
}

} // namespace
namespace dc::quest {
std::string SaveJson();                 // quest/framework.cpp (coop-pt1111e: the quests' progress, carried)
bool        LoadJson(std::string_view json);
void        QuestHostSetup();           // quest/host.cpp
} // namespace dc::quest
namespace {
// The quests' progress as text, the quests registered first: the host registers them at a game's start or load or a copy's first
// talk, and a copy without them wrote its saved progress alone (no quest at its fresh stage), so two copies holding the same
// progress would read apart.
std::string QuestsText() {
    dc::quest::QuestHostSetup();
    return dc::quest::SaveJson();
}

// coop-pt1111e (coordinator, 11-11, after Jep's playtest item 6): the quests' progress (quest/framework.cpp: each quest's stage and
// variables, saved beside the save as its own JSON) is in no state set and was read by no checksum: a quest moved on one copy alone
// (a talk's choice that ran on one copy, a resync) parted the copies with no OUT OF SYNC to say so. Its text (SaveJson) is the part:
// hashed into the checksum, carried with a resync (LoadJson on every copy) and, with the state set, by its extra (SnapExtras).
// DC_NETPLAY_MUTATE=quests_unsummed (twin): not read by the checksum, as before.
std::uint32_t QuestsPartHash() {
    // (the moved progress alone: a quest at its first stage with no variables is as no progress, whether the copy registered it or not)
    std::string text = CoopWorldsQuestsMoved();

    // (the twin reads the progress as it first was, blind to every later move: a state file's saved checksum still matches)
    if (NetplayMutated("quests_unsummed")) {
        static const std::string first = text;
        text = first;
    }
    std::uint32_t     hash = 2166136261u;

    for (unsigned char c : text) {
        hash = (hash ^ c) * 16777619u;
    }
    return hash;
}
void QuestsPartTake(std::vector<unsigned char> &out) {
    const std::string text = QuestsText();
    out.insert(out.end(), text.begin(), text.end());
}
void QuestsPartPut(const unsigned char *data, std::size_t size) {
    const std::string host(reinterpret_cast<const char *>(data), size);

    if (host != QuestsText()) {
        std::fprintf(stderr, "netplay: carried: the quests' progress differs here: the host's taken\n");
    }
    if (!dc::quest::LoadJson(host)) {
        std::fprintf(stderr, "netplay: carried: the host's quests' progress does not read (%zu bytes)\n", size);
    }
}

const CarriedPart kCarried[] = {
    {"settings", SettingsPartHash, SettingsPartTake, SettingsPartPut},
    {"place", PlacePartHash, PlacePartTake, PlacePartPut},
    {"toan step", ToanStepPartHash, ToanStepPartTake, ToanStepPartPut},
    {"town camera", TownCameraPartHash, TownCameraPartTake, TownCameraPartPut},
    {"quests", QuestsPartHash, QuestsPartTake, QuestsPartPut}, // coop-pt1111e
};

std::uint32_t CarriedHash() {
    std::uint32_t hash = 2166136261u;

    for (const CarriedPart &part : kCarried) {
        hash = (hash ^ part.hash()) * 16777619u;
    }
    return hash;
}

// Appended to a resync's state: per part its name (16 bytes), its size (4) and its bytes; the header says where they begin.
void CarriedTake(std::vector<unsigned char> &state) {
    if (NetplayMutated("carried_off") || state.size() < sizeof(ResyncHeader)) {
        return;
    }
    const std::uint32_t at = (std::uint32_t) state.size();
    std::string         said;

    for (const CarriedPart &part : kCarried) {
        std::vector<unsigned char> bytes;
        char                       name[16] = {};

        part.take(bytes);
        std::snprintf(name, sizeof name, "%s", part.name);
        state.insert(state.end(), name, name + sizeof name);
        for (int i = 0; i < 4; i++) {
            state.push_back((unsigned char) (bytes.size() >> (8 * i)));
        }
        state.insert(state.end(), bytes.begin(), bytes.end());
        said += std::string(said.empty() ? "" : ", ") + part.name + " " + std::to_string(bytes.size());
    }
    std::memcpy(state.data() + offsetof(ResyncHeader, carried_at), &at, sizeof at);
    std::fprintf(stderr, "netplay: carried with the resync: %s bytes\n", said.c_str());
}

// Each part of a resync's state put back by its name, on both copies.
void CarriedPut(const std::vector<unsigned char> &state) {
    ResyncHeader header;

    if (state.size() < sizeof header) {
        return;
    }
    std::memcpy(&header, state.data(), sizeof header);

    std::size_t at = header.carried_at;

    while (at != 0 && at + 20 <= state.size()) {
        char          name[17] = {};
        std::uint32_t size = 0;

        std::memcpy(name, state.data() + at, 16);
        for (int i = 0; i < 4; i++) {
            size |= (std::uint32_t) state[at + 16 + i] << (8 * i);
        }
        at += 20;
        if (at + size > state.size()) {
            break;
        }
        for (const CarriedPart &part : kCarried) {
            if (std::strcmp(part.name, name) == 0) {
                part.put(state.data() + at, size);
            }
        }
        at += size;
    }
}

std::vector<unsigned char> TakeState(bool keep_stands = true) {
    ResyncHeader header{};

    header.rand = PortRandState();
    header.mode = mode;
    header.map_no = MapNo;
    header.local_map_no = LocalMapNo;
    header.select_map_no = selectMapNo;
    header.partner_choice = coop::g_partner.choice;
    header.debug_mode = DebugMode;
    std::snprintf(header.cause, sizeof header.cause, "%s", keep_stands ? g_link.resync_cause : "checkpoint"); // coop-stable1
    // coop-stable1: the town's clock in every state, not the building checkpoint's alone: a town resync (a join with no
    // checkpoint, a desync's) left each copy its own NowTime, the town's next frame wrote it into the save's now_time, and the
    // pair parted on the save and on rand 27 ticks after the resync (w-town-apart p2cave: OUT OF SYNC at tick 60, a second
    // RESET at 187, 2026-10-25). DC_NETPLAY_MUTATE=own_town_clock (twin): each copy keeps its own, as before.
    header.building_time = NowTime;

    if (const coop::SpawnStand &stand = coop::SpawnStandNow(); stand.set) {
        header.stand_set = 1;
        header.stand_map = stand.map;
        header.stand_arriver = stand.arriver;
        std::memcpy(header.stand_pos, stand.pos, sizeof header.stand_pos);
        header.stand_yaw = stand.yaw;
    }

    // coop-mergestand: a resync in a town outdoors carries where both stand on the host now, so the town's reload puts both
    // back: Toan always; her spot when the host shows her (a desync's resync, a join while a stand-in walks with him), over
    // any stand, which is the merge's word when the host is apart and shows nobody. A checkpoint (keep_stands false) carries
    // no spots: the host plays on from it, so a joiner's replay brings Toan where he is. DC_COOP_MUTATE=spawn_no_stands
    // (twin): none, so both copies come back at the town's arrival, as before.
    if (keep_stands && !coop::Mutated("spawn_no_stands")) {
        float toan[3], toan_yaw, partner[3], partner_yaw;
        bool  partner_set = false;

        if (mode == GAME_MODE_EDIT && CoopTownStandsForResync(toan, &toan_yaw, &partner_set, partner, &partner_yaw)) {
            header.p1_set = 1;
            std::memcpy(header.p1_pos, toan, sizeof header.p1_pos);
            header.p1_yaw = toan_yaw;
            // coop-fx51c (w-world-split -Case merge-town on dun1-f1: her copy came into Matataki as his still played his arrival
            // from Wise Owl Forest, town mode 14; "the merge goes into it, both play the rest of it", the event's rest ended on
            // both in 5 ticks, then this stand put Toan back on the gate where the arrival had him mid-way, its ground event
            // fired, "town event 1 goes into a dungeon", and he left for the forest 10 ticks after the merge): a take inside a
            // town event carries no stand for Toan, so the event's own end places him on both copies, as the game's arrival
            // does. Hers goes as before. DC_NETPLAY_MUTATE=stand_in_event (twin): his stand from inside the event, as before.
            if (::GameMode == ED_MODE_EVENT && !NetplayMutated("stand_in_event")) {
                header.p1_set = 0;
                std::fprintf(stderr, "spawn: the state set is taken in a town event: no stand for Toan (the event's end places him)\n");
            }

            if (partner_set) {
                header.stand_set = 1;
                header.stand_map = MapNo;
                header.stand_arriver = 2;
                std::memcpy(header.stand_pos, partner, sizeof header.stand_pos);
                header.stand_yaw = partner_yaw;
            } else if (!header.stand_set && !CoopWorldsApart()) {
                // A join with nobody shown and no stand yet (the host's town was still loading at the link's start): spot 2 of
                // Toan's as he stands now, on both copies. coop-townstand: the town's pick (CoopTownSecondSpot: her ground, short
                // of walls, off any town event's ground: Queens' and Matataki's dungeon entries lay 100 from Toan at a debug
                // start, and her first step there took her copy into the dungeon alone).
                header.stand_set = 1;
                header.stand_map = MapNo;
                header.stand_arriver = 2;
                CoopTownSecondSpot(toan, toan_yaw, toan, header.stand_pos);
                header.stand_yaw = toan_yaw;
                // coop-soakfix3 (a join as he walked out of Wise Owl Forest into Matataki, his arrival a town event: her spot 2 was of his
                // place mid-way, on the forest gate's ground, wider there than her spot's tries; she stood on it through the event and
                // her first landing after it started town event 1, her copy off to the forest alone at link tick 40): inside a town event
                // her stand waits, as his does, for the event's end: spot 2 of Toan's then, on every copy (CoopTownStandAfterEvent).
                // DC_NETPLAY_MUTATE=her_stand_in_event (twin): spot 2 of his place mid-event, as before.
                // (DC_COOP_TEST_GATE_SPOT=1, test_join_in_arrival: the search from Toan's place mid-way, on the gate: where it lands)
                if (TestEnvOn("DC_COOP_TEST_GATE_SPOT")) {
                    std::fprintf(stderr, "coop: test: gate spot in town %d: from Toan's (%.0f %.0f %.0f, ground event %d) to (%.0f %.0f %.0f), ground event %d\n", (int) MapNo,
                                 toan[0], toan[1], toan[2], CoopTownGroundEventAt(toan), header.stand_pos[0], header.stand_pos[1], header.stand_pos[2],
                                 CoopTownGroundEventAt(header.stand_pos));
                }
                if (::GameMode == ED_MODE_EVENT && !NetplayMutated("her_stand_in_event")) {
                    header.stand_arriver = coop::kSpawnAfterEvent;
                    std::fprintf(stderr, "spawn: a join in a town event: player 2's stand waits for its end (spot 2 of Toan's then)\n");
                }
                std::fprintf(stderr, "spawn: a join with no stand: player 2 is to stand at spot 2 of Toan's (%.0f %.0f %.0f)\n", header.stand_pos[0],
                             header.stand_pos[1], header.stand_pos[2]);
            }
        }
    }
    header.game_clear = GameClearFlag;
    TakeCamera(header.cameras[0], MainCamera__4);
    std::memcpy(&header.event, &BtEventInfo, sizeof header.event);
    TakeCamera(header.cameras[1], SubCamera);
    header.sub_camera = NowCamera__3 == &SubCamera;
    header.menu_no = main_select_menu_no;
    std::snprintf(header.town, sizeof header.town, "%s", main_select_param);
    std::memcpy(header.render, (const void *) &mgRenderInfo, sizeof header.render);
    header.has_render = !NetplayMutated("own_view");
    header.stops[0] = all_stop;
    header.stops[1] = CCharacter::MotionStopFlag;
    header.stops[2] = CTextureAnime::stop_anime;
    header.stops[3] = CCamera::StopCamera;
    header.has_stops = !NetplayMutated("own_stops");
    header.loaded_tick = -1;
    header.menu_load = g_link.menu_load ? 1 : 0;
    g_link.menu_load = false;
    header.floor = -1; // coop-snapshot: Checkpoint(floor) sets a floor-start checkpoint's
    header.building = 0; // coop-housemerge: Checkpoint(-1, true) sets a building-start checkpoint's

    if (g_link.loaded_tick >= 0) {
        if (SaveSlotReadNumbered(0)) {
            header.loaded_tick = (std::int32_t) g_link.loaded_tick;
        } else {
            std::fprintf(stderr, "netplay: could not load slot 0 for tick %lld\n", (long long) g_link.loaded_tick);
        }

        g_link.loaded_tick = -1;
    }

    CoopWorldsBagsBack(); // coop-dungeonp2: each bag in its own place before the state is taken
    std::vector<unsigned char> state(sizeof header + sizeof(CSaveData));

    std::memcpy(state.data(), &header, sizeof header);
    std::memcpy(state.data() + sizeof header, (const void *) SaveData, sizeof(CSaveData));
    // Then the host's save slots, with their length, and pad 2's own bag, which the session reset keeps.
    std::vector<unsigned char> slots = NetplaySlotsTake();

    for (int i = 0; i < 4; i++) {
        state.push_back((unsigned char) (slots.size() >> (8 * i)));
    }

    state.insert(state.end(), slots.begin(), slots.end());
    coop::InventoryWrite(state);
    return state;
}

// The map a resync sends both copies to from where the host is (NextMapNo's numbering): its dungeon's
// entrance, or its town; -1 while it is anywhere else (the title, the opening, a loading screen).
int RestartTarget(int at_mode, int map_no, int select_map_no) {
    if (at_mode == GAME_MODE_DUNGEON && select_map_no >= 0 && select_map_no <= 6) {
        return select_map_no + 200;
    }

    if (at_mode == GAME_MODE_EDIT && map_no >= 0 && map_no < 200) {
        return map_no;
    }

    return -1;
}

// coop-stable1: the unified zone-load log. Jep (2026-10-25): a stable build, no new features, and from the first scene to
// both players on floor 1 "the zone must never reset"; invisible catch-ups on a zone change are fine, zone RESETS are not.
// Every load either copy is made to take (a resync's, a catch-up's, a replay's) and every mode end is one "zone:" line on
// stderr (the tests slice on stderr), with the cause and the code that asked for it:
//   zone: RESET at tick T player P: mode M map X (local L, floor F, interior I) loads again as map Y: cause "...", by <site>
//   zone: load at tick T player P: mode M map X (...) leaves for map Y: cause "...", by <site>
//   zone: no load at tick T player P: ... (a snapshot join that lands where it stands)
//   zone: mode M map X (...) ended -> next map Y, event E: <forced by netplay: cause, by site | the game's own change
//         [, the SAME zone again]>
// A RESET is a forced load whose target is the zone the copy is in already (RestartTarget alike); the game's own change to
// the same map (a language change, a save loaded from the battle menu) is marked too. The host's cause rides in its state
// (ResyncHeader::cause), so the joiner names the same one.
int PlaceNow(char *out, std::size_t size) {
    const int floor = mode == GAME_MODE_DUNGEON && UserStatus != nullptr ? (int) UserStatus->cur_floor : -1;
    const int interior = mode == GAME_MODE_EDIT && EdInteriorFlag != 0 ? CoopWorldsInteriorId(EdInteriorName) : 0;

    std::snprintf(out, size, "mode %d map %d (local %d, floor %d, interior %d)", (int) mode, (int) MapNo, (int) LocalMapNo, floor, interior);
    return RestartTarget(mode, MapNo, selectMapNo);
}

// The host: why it is about to set (or has set) resync_tick, for its state's header and the zone line. The first cause
// of a pending resync stays; a later one (a postponement) is appended.
void ResyncCause(const char *cause, const char *site) {
    if (g_link.resync_tick >= 0 && g_link.resync_cause[0] != '\0') {
        std::size_t len = std::strlen(g_link.resync_cause);

        std::snprintf(g_link.resync_cause + len, sizeof g_link.resync_cause - len, "; %s (%s)", cause, site);
        return;
    }
    std::snprintf(g_link.resync_cause, sizeof g_link.resync_cause, "%s (%s)", cause, site);
}

// A forced load (or a snapshot landing with none): the zone line, and the mode end's words.
void ZoneLeaveLog(int target, const char *cause, const char *site, bool no_load) {
    char      place[128];
    const int here = PlaceNow(place, sizeof place);
    const bool reset = !no_load && here >= 0 && here == target;

    std::snprintf(g_link.leave_cause, sizeof g_link.leave_cause, "%s", cause);
    std::snprintf(g_link.leave_site, sizeof g_link.leave_site, "%s", site);
    g_link.leave_reset = reset;

    if (no_load) {
        std::fprintf(stderr, "zone: no load at tick %lld player %d: %s stays (the checkpoint's map %d lands here): cause \"%s\", by %s\n",
                     (long long) g_link.tick, g_link.player, place, target, cause, site);
        g_link.leave_cause[0] = '\0'; // no mode end follows it
    } else if (reset) {
        std::fprintf(stderr, "zone: RESET at tick %lld player %d: %s loads again as map %d: cause \"%s\", by %s\n", (long long) g_link.tick,
                     g_link.player, place, target, cause, site);
    } else {
        std::fprintf(stderr, "zone: load at tick %lld player %d: %s leaves for map %d: cause \"%s\", by %s\n", (long long) g_link.tick,
                     g_link.player, place, target, cause, site);
    }
    std::fflush(stderr);
}

// Every mode end (NetplayModeEnded), before the main loop follows NextMapNo: whose change it was.
void ZoneModeEnded() {
    char      place[128];
    const int here = PlaceNow(place, sizeof place);
    const int next = (int) NextMapNo;

    if (g_link.leave_cause[0] != '\0') {
        std::fprintf(stderr, "zone: %s ended -> next map %d, event %d: forced by netplay%s: %s, by %s\n", place, next, (int) StartEventNo,
                     g_link.leave_reset ? " (RESET)" : "", g_link.leave_cause, g_link.leave_site);
        g_link.leave_cause[0] = '\0';
        g_link.leave_site[0] = '\0';
        g_link.leave_reset = false;
    } else {
        const bool same = here >= 0 && next == here;

        std::fprintf(stderr, "zone: %s ended -> next map %d, event %d: the game's own change%s\n", place, next, (int) StartEventNo,
                     same ? ", the SAME zone again (RESET: a language change or a save loaded from the menu)" : "");
    }
    std::fflush(stderr);
}

// Both copies, once the mode the resync left has ended: the host's state, every co-op module as a
// fresh session has it, pad 2's pick as the host has it. Taken here, not on the resync tick, because a
// dungeon left through its own exit plays one more frame of its floor first, which differs between
// the copies (one may be at a floor's entrance, the other fighting on it).
// coop-floorscene: own is a checkpoint's self-take (Checkpoint): this copy's own state again, not the other's.
void TakeState(const std::vector<unsigned char> &state, bool own = false) {
    ResyncHeader header;

    NetplayTestResyncLeftovers();
    CoopWorldsBagsBack(); // coop-dungeonp2

    if (!NetplayMutated("poison_phase")) {
        poison_counter = 0; // as a fresh session (Begin): both copies bite on one clock
    }

    std::memcpy(&header, state.data(), sizeof header);

    // The own_load twin (on the joiner): a load the host made leaves this copy's SaveData as it was.
    if (!(header.loaded_tick >= 0 && g_link.player == 2 && NetplayMutated("own_load"))) {
        std::memcpy((void *) SaveData, state.data() + sizeof header, sizeof(CSaveData));
    }

    if (header.loaded_tick >= 0) {
        std::fprintf(stderr, "netplay: loaded slot 0 at tick %d (checksum %08x)\n", (int) header.loaded_tick, SaveSum());
    }

    // coop-saveload: player 1's load from his menu; her menu (open on both copies) closes with a notice on hers.
    if (header.menu_load != 0) {
        std::fprintf(stderr, "netplay: resynced to player 1's loaded save (checksum %08x)\n", SaveSum());
        CoopTownMenuClosedByLoad();
    }

    std::size_t   at = sizeof header + sizeof(CSaveData);
    std::uint32_t slots = 0;

    for (int i = 0; i < 4 && at + i < state.size(); i++) {
        slots |= (std::uint32_t) state[at + i] << (8 * i);
    }

    at += 4;

    // The joiner plays on the host's saves from now on, in a folder of its own.
    if (g_link.player == 2 && at + slots <= state.size() && !NetplayMutated("own_slots")) {
        NetplaySlotsPut(state.data() + at, slots);
    }

    std::fprintf(stderr, "netplay: save slots now: %s\n", NetplaySlotsList().c_str());

    std::size_t bag = at + slots;

    if (state.size() > bag && !NetplayMutated("no_bag")) {
        coop::InventoryRead(state.data() + bag, (header.carried_at > bag ? header.carried_at : state.size()) - bag); // coop-carried
    }
    PortRandSetState(header.rand);
    selectMapNo = header.select_map_no;
    DebugMode = header.debug_mode;
    GameClearFlag = header.game_clear;

    PutCamera(header.cameras[0], MainCamera__4);

    // The event state but for its pointers into this copy's script stack, which stay this copy's.
    RS_STACKDATA *entrance = BtEventInfo.entrance_result;
    RS_STACKDATA *escape = BtEventInfo.escape_result;
    s32           item_select = BtEventInfo.item_select_result;

    std::memcpy(&BtEventInfo, &header.event, sizeof BtEventInfo);
    BtEventInfo.entrance_result = entrance;
    BtEventInfo.escape_result = escape;
    BtEventInfo.item_select_result = item_select;
    PutCamera(header.cameras[1], SubCamera);
    NowCamera__3 = header.sub_camera ? &SubCamera : &MainCamera__4;

    // The map numbers and town name as the host has them; the jump that follows makes MapNo the old
    // map, as it was on the host.
    MapNo = header.map_no;
    LocalMapNo = header.local_map_no;
    main_select_menu_no = header.menu_no;
    std::snprintf(main_select_param, sizeof main_select_param, "%s", header.town);

    if (header.has_render) {
        std::memcpy((void *) &mgRenderInfo, header.render, sizeof header.render);
    }

    // coop-stable1: the host's town clock (see TakeState()); the building checkpoint's take (NetplayCatchupEnterBuilding) sets
    // it again after its own TakeState, as before.
    // As the town's own load reads it back (editloop.cpp EditLoad: 0.01 * ((int) (100 * now_time) % 1200)), in the save as
    // well: a copy whose load reads it (the joiner's) and one whose does not (the host's reload) then hold the same value;
    // the host's live NowTime restored as it was differed from her read-back by its hundredths (OUT OF SYNC at tick 60 on
    // now_time 0.0422466 against 0.04169, 2026-10-26, once the loads no longer ran the ticks).
    if (!NetplayMutated("own_town_clock")) {
        const float town_clock = 0.01f * (float) ((int) (100.0f * header.building_time) % 1200);

        NowTime = town_clock;
        if (SaveData != nullptr) {
            SaveData->SetNowTime(town_clock);
        }
    }

    // A town's own draw-time state (port/src/editloop3.cpp) as a copy new to the town has it.
    if (!NetplayMutated("own_town_draw")) {
        PortEditLoop3ResetForResync();
    }

    // The floor builder's stray reads land in guards a resync clears on both copies (port/src/dungeonmap.cpp).
    if (!NetplayMutated("keep_map_guard")) {
        PortBuildMapGuardsClear();
    }

    // The towns' effect clock (effectmacro.cpp's effect_count, stepped each town frame), whose phase picks the frames
    // a waterfall's spray or a chimney's smoke rolls rand on: a copy that has been in a town a while has it elsewhere
    // than one that has not. Both copies start it over.
    if (!NetplayMutated("keep_effect_count")) {
        effect_count = 0;
    }

    // coop-soakfix5 (the soak's leave seeds 1031 and 1247): the town walk's swing of the camera behind Toan (EdMoveChara's
    // rot_count, retail's function static, ebattle.cpp), which a rejoin's ticks before its resync had started on one copy alone
    // (his pad on hers): that copy went on swinging the reloaded town's camera, view apart from tick 40. Both copies start with
    // none. DC_NETPLAY_MUTATE=keep_town_swing (twin): each copy keeps its own, as before.
    if (EdMoveCharaRotCount != 0) {
        std::fprintf(stderr, "netplay: the town walk's camera swing behind Toan (%d ticks left) %s at the resync\n", EdMoveCharaRotCount,
                     NetplayMutated("keep_town_swing") ? "kept (keep_town_swing)" : "starts over");
    }
    if (!NetplayMutated("keep_town_swing")) {
        EdMoveCharaRotCount = 0;
    }

    // The slot the next elemental hit lights in CWeaponElFx (a function static in CheckDmg), which a copy that has
    // fought a while has elsewhere than one that has not: two sparks at once then step in another order and roll
    // rand apart. The dungeon's start puts the slots themselves out; this starts the rotation over on both copies.
    if (!NetplayMutated("keep_weapon_fx")) {
        PortWeaponFxSlotReset();
    }

    // The town's message windows: a window one copy made before the join (a Georama part's name) and the other never
    // did is made again on both, alike (Norune, Jep 2026-10-20). DC_NETPLAY_MUTATE=keep_message_cache keeps each copy's.
    if (!NetplayMutated("keep_message_cache")) {
        PortMessageCachesReset();
    }

    if (header.has_stops) {
        all_stop = header.stops[0];
        CCharacter::MotionStopFlag = header.stops[1];
        CTextureAnime::stop_anime = header.stops[2];
        CCamera::StopCamera = header.stops[3];
    }

    // Every co-op module back to a fresh session's state (coop.hpp), then pad 2's pick as the host has it. A resync loads the
    // town again on every copy (not the sender's own checkpoint take): a menu carried through a merge parks first (coop-soakfix5,
    // seed 1269), and the in-menu bits go with the closed engines (seed 1167).
    if (!own) {
        CoopTownMenuParkCarriedForResync();
    }
    if (!NetplayMutated("no_session_reset")) {
        CoopResetForSession();
    }
    if (!own) {
        coop::ActorMenusResetForResync();
    }
    if (header.menu_load != 0) {
        CoopTownAfterLoad(); // coop-saveload: what she is left in after his load's resync
    }

    // coop-worlds: a floor change held for the merge ends here, on both copies alike (the host's state takes them off the floor).
    CoopWorldsStateTaken(own);

    // coop-p2absent: the healing weapon's counter from the same tick on both (the twin keeps each copy's).
    if (!NetplayMutated("heal_counter")) {
        MotionDriveHealCounter = 0;
    }

    // coop-p2absent: the live hits (attacks, shots, bombs) go too. No state carries them, so a hit the
    // host had going from its solo play stayed in its table alone, the next hits took other slots there
    // than on the joiner, and one blow hurt Toan on one copy only (life 41 against 43, 2026-10-20). The
    // twin (DC_NETPLAY_MUTATE=keep_hits) leaves each copy's table as it was.
    if (NowColData != nullptr && !NetplayMutated("keep_hits")) {
        std::memset((void *) NowColData, 0, sizeof *NowColData);
    }

    coop::g_partner.choice = header.partner_choice;

    // coop-spawn: the host's stand for player 2 after the merge, or none (both copies alike).
    if (header.stand_set) {
        coop::SpawnStand stand;

        stand.set = true;
        stand.map = header.stand_map;
        stand.arriver = header.stand_arriver;
        std::memcpy(stand.pos, header.stand_pos, sizeof stand.pos);
        stand.yaw = header.stand_yaw;
        coop::SpawnSetStand(stand, "the host's state");
    } else {
        coop::SpawnClearStand();
    }

    // coop-mergestand: and Toan's spot, or none (both copies alike).
    if (header.p1_set) {
        coop::SpawnToan toan;

        toan.set = true;
        toan.map = header.map_no;
        std::memcpy(toan.pos, header.p1_pos, sizeof toan.pos);
        toan.yaw = header.p1_yaw;
        coop::SpawnSetToan(toan, "the host's state");
    } else {
        coop::SpawnClearToan();
    }

    if (header.has_next) {
        NextMapNo = header.next_map_no;
        StartEventNo = header.start_event_no;
        mode = header.next_mode;
        OldMapNo = header.old_map_no;
    }
}

// Both copies, at the resync tick: leave for the host's dungeon (at its entrance) or town, where both
// start alike once TakeState has run.
// coop-stable1: cause and site name the code path in the zone line (ZoneLeaveLog); a null cause takes the host's from the state.
void LeaveForState(const std::vector<unsigned char> &state, const char *cause, const char *site) {
    ResyncHeader header;

    std::memcpy(&header, state.data(), sizeof header);
    g_link.menu_owner = 1;
    g_link.held = 0;
    g_link.state_due = true;
    int target = header.has_next ? header.leave_map : RestartTarget(header.mode, header.map_no, header.select_map_no);

    header.cause[sizeof header.cause - 1] = '\0';
    ZoneLeaveLog(target, cause != nullptr ? cause : header.cause[0] != '\0' ? header.cause : "(the host gave no cause)", site, false);

    // Both copies leave their mode on this tick, mid-frame, as the developer menu leaves it
    // (NetplayTakeRestart), wherever each is: a dungeon's own exit would play one more frame of its
    // floor first, and a copy in a town none, which would part them by a frame.
    g_link.resync_target = target;
    g_link.restart_map = target;
}

// The resync tick, before its inputs: the host sends its state until the joiner holds all of it, the
// joiner waits for it; both then take it alike. False when the other copy stops answering.
bool Resync() {
    std::int64_t tick = g_link.resync_tick;
    auto         start = std::chrono::steady_clock::now();
    auto         sent = start - std::chrono::seconds(1);
    static PieceSender pieces; // (16 KB: not on the stack)

    pieces = PieceSender{};

    if (g_link.player == 1) {
        g_link.resync_state = TakeState();
        std::memset(g_link.resync_held, 0, sizeof g_link.resync_held);

        CarriedTake(g_link.resync_state); // coop-carried: every checked part the save does not hold

        // coop-p2absent: test hook (DC_NETPLAY_STALE_HIT=1): a hit of the host's own going into the resync,
        // as a monster's blow from its solo play was (far off, harmless, long-lived).
        static bool stale_hit = TestEnvOn("DC_NETPLAY_STALE_HIT");
        // (coop-warmreset, WS7: once a job, not once a process, on a warm pair; DC_COOP_MUTATE=job_hook_kept: once a process)
        COOP_TEST_JOB_RESET(stale_hit, TestEnvOn("DC_NETPLAY_STALE_HIT"), "job_hook_kept");

        if (stale_hit && NowColData != nullptr) {
            sceVu0FVECTOR away = {-50000.0f, 0.0f, -50000.0f, 1.0f};
            int           slot = NowColData->Set(away, 1, 100000, 1.0f, 1.0f, 1, 2, 0, 0);

            stale_hit = false;
            std::fprintf(stderr, "netplay: test: a stale hit in slot %d goes into the resync\n", slot);
        }
    }

    std::fprintf(stderr, "netplay: resyncing at tick %lld (host in mode %d, map %d, local map %d, town %s)\n", (long long) tick, (int) mode,
                 (int) MapNo, (int) LocalMapNo, main_select_param);

    for (;;) {
        Receive();

        std::uint32_t count = (std::uint32_t) ((g_link.resync_state.size() + kChunkBytes - 1) / kChunkBytes);

        if (!g_link.resync_state.empty() && AllHeld(count)) {
            break;
        }

        auto now = std::chrono::steady_clock::now();

        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count() > kResyncWaitMs) {
            Close("the resync did not complete");
            return false;
        }
        if (QaStopInWait()) { // coop-stophang
            Close("this copy is stopping (the test's stop file)");
            return false;
        }

        // The host sends the pieces the joiner lacks, paced (SendPieces); both keep the link's inputs going every
        // 30 ms meanwhile.
        if (g_link.player == 1) {
            SendPieces(pieces, g_link.resync_state, g_link.resync_held, (std::uint32_t) tick);
        }

        if (now - sent > std::chrono::milliseconds(30)) {
            sent = now;
            Send();
        }

        std::this_thread::sleep_for(std::chrono::microseconds(500));
    }

    // coop-mergetime: how long the state took to go over, and how (net-resync-time reads it).
    long long took = (long long) std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    std::uint32_t pieces_count = (std::uint32_t) ((g_link.resync_state.size() + kChunkBytes - 1) / kChunkBytes);

    g_link.resynced_clock = std::chrono::steady_clock::now();

    if (g_link.player == 1) {
        std::fprintf(stderr, "netplay: resync transfer: %u pieces in %lld ms, %u sent (%u again), round trip %.0f ms, again after %d ms, window %d\n",
                     pieces_count, took, pieces.sends, pieces.again, g_link.rtt_avg_ms, PieceOverdueMs(), pieces.window);
    } else {
        std::fprintf(stderr, "netplay: resync transfer: %u pieces held in %lld ms\n", pieces_count, took);
    }

    if (!NetplayMutated("no_resync")) {
        LeaveForState(g_link.resync_state, g_link.player == 1 ? g_link.resync_cause : nullptr, "Resync");
        g_link.load_hold = !NetplayMutated("no_load_hold"); // coop-stable1 (twin no_load_hold: the ticks run on through the loads, as before)

        CarriedPut(g_link.resync_state); // coop-carried
    }
    g_link.resync_cause[0] = '\0'; // coop-stable1: the cause went into the state (the joiner reads it there)

    for (std::int64_t &frame : g_link.sums_frame) {
        frame = -1;
    }

    g_link.last_checksum_frame = -1;
    g_link.desync_tick = -1;
    g_link.awaiting_sync = false;
    g_link.resynced_at = tick;
    JoinStateTaken(tick); // coop-p2skin
    g_link.resync_tick = -1;
    g_link.resyncs++;
    g_link.resynced_status_until = tick + 600;
    std::snprintf(g_link.status, sizeof g_link.status, "Resynced at tick %lld", (long long) tick);
    std::fprintf(stderr, "netplay: resynced at tick %lld (%zu bytes of the host's state), leaving for map %lld\n", (long long) tick,
                 g_link.resync_state.size(), (long long) g_link.resync_target);
    return true;
}

// Test hook (DC_COOP_MENUPRESS=P:BUTTON:T,..., the menu-triangle tests): player P's agreed pad (1 or 2) holds
// BUTTON (triangle, start, circle, cross, square, up, down, left, right, select) for 4 ticks from T ticks after the pair's first tick of free play (or a town's walk) on a
// floor (counted again after a resync), and player 1's Cross (the script's taps for the floor select) is taken off.
// On the agreed pads, so both copies see the same presses.
// The menu presses' clock: both copies alike from a resync's state taken at the mode's end, or a catch-up's
// lockstep start. g_test_epoch counts those starts; g_test_epoch_resyncs is the resync count at the last one.
int g_test_epoch = 0;
int g_test_epoch_resyncs = -1;

std::int64_t g_test_press_since = -1; // the clock's ticks so far (-1 before it starts), for coop_pause's test hit

std::string g_test_shot; // coop-itemsend: DC_COOP_MENUSHOTS's next frame to write
long        g_test_series_frame = 0;   // coop-herexit2: this copy's frames so far (DC_COOP_TEST_SHOT_SERIES)
long        g_test_series_mark = -1;   // and the frame NetplayTestSeriesMark noted
bool        g_test_series_moved = false;
long        g_test_series_count = 0;   // the series' own frame count and its first frame (-1: its spec not read yet)
long        g_test_series_from = -1;
// coop-floorjoin2 (tests): DC_COOP_SHOT_AFTER_JOIN=N with DC_COOP_SHOT_PATH=<png>: the tick, N after the lockstep began from a
// catch-up (EnterLockstep), whose frame goes to the path (NetplayFrameStart hands it to g_test_shot); -1 for none.
std::int64_t g_shot_due = -1;
int          g_shot_walk_after = -1; // coop-joinlate: DC_COOP_SHOT_AFTER_WALK: the shot's N, due once the arrival walk has played out
std::int64_t g_test_lockstep_from = -1; // coop-keydoor (tests): the tick the last catch-up's lockstep began on, -1 before one

bool StateFileLoadPending(); // coop-fixtures (below, with the state files)

// coop-twinfix test hook (DC_COOP_TEST_DOOR_ON_EASE=T:G, both copies; w-town-apart -Case houseout -SplitCam): from link tick T,
// player 1's Cross for 4 ticks on the first tick her town camera's eye stands G or more off its target (CoopTownCamEaseGap),
// once. His door then opens while her eye still eases in, the state the split's camera handover is for, on a state of the
// game, not a tick a test tuned (its -CrossDelay 150 had let her eye settle: eye 34.0 = target 34.0, and the handover's twin
// split_cam_targets passed, nightly 2026-11-01).
void TestDoorOnEase() {
    static const char *spec = TestEnv("DC_COOP_TEST_DOOR_ON_EASE");
    static std::int64_t pressed_at = -1;

    if (spec == nullptr || !g_link.up) {
        return;
    }

    long  from = 0;
    float gap_min = 0.0f;

    if (std::sscanf(spec, "%ld:%f", &from, &gap_min) != 2 || g_link.tick < from) {
        return;
    }
    if (pressed_at < 0) {
        const float gap = CoopTownCamEaseGap();

        if (gap < gap_min) {
            return;
        }
        pressed_at = g_link.tick;
        std::fprintf(stderr, "netplay: test: player 1 presses cross at tick %lld, her camera's eye %.1f off its target\n", (long long) g_link.tick, gap);
    }
    if (g_link.tick < pressed_at + 4) {
        g_link.synced[0].buttons |= kInputCross;
    }
}

void TestMenuPresses() {
    TestDoorOnEase();
    // coop-pt1106-lockup test hook (DC_NETPLAY_TEST_HOST_MENU_CLOSE=1 on both copies): in the lockstep, player 1's local menu takes a
    // Circle on his agreed pad each 16 ticks till it closes (a page's own Circle first, then the ring's).
    // =N: from N ticks after the lockstep (a hit's row keeps the menu up till it came).
    static const int host_menu_close = TestEnvInt("DC_NETPLAY_TEST_HOST_MENU_CLOSE", -1);

    // DC_NETPLAY_TEST_MENU_PLAYER=P: player P's menu and pad, not player 1's (x-floor-hermenu).
    static const int host_menu_player = TestEnvInt("DC_NETPLAY_TEST_MENU_PLAYER", 1);

    // (coop-pt1109d: with DC_NETPLAY_TEST_HOST_MENU_AGAIN the next menu stays up: these Circles are for the first)
    static const bool host_menu_once = TestEnvOn("DC_NETPLAY_TEST_HOST_MENU_AGAIN");
    static int        host_menu_stage = 0;

    if (host_menu_once && g_link.up && !g_catch.active && g_test_lockstep_from >= 0) {
        const bool in = coop::PlayerInMenu(host_menu_player, coop::kActorMenuLocal);

        host_menu_stage = host_menu_stage == 0 && in ? 1 : host_menu_stage == 1 && !in ? 2 : host_menu_stage;
    }

    if (host_menu_close >= 0 && host_menu_stage < 2 && g_link.up && !g_catch.active && g_test_lockstep_from >= 0 && g_link.tick >= g_test_lockstep_from + host_menu_close &&
        (coop::PlayerInMenu(host_menu_player, coop::kActorMenuLocal) || (mode == GAME_MODE_EDIT && EdInteriorFlag == 0 && ::GameMode == ED_MODE_GEORAMA_MENU)) && g_link.tick % 16 < 4) { // (coop-p1parts: or his parts menu)
        g_link.synced[host_menu_player - 1].buttons |= kInputCircle;
    }
    static const char *spec = TestEnv("DC_COOP_MENUPRESS");
    static std::int64_t start = -1;
    static int          generation = TestJobGeneration();
    static bool         job_reset = false; // coop-serve: the exit's latch, cleared per job (below)

    // coop-serve: a persistent client's next job brings its own presses: read again, their clock from the job's first tick
    if (generation != TestJobGeneration()) {
        generation = TestJobGeneration();
        spec = TestEnv("DC_COOP_MENUPRESS");
        start = g_link.up ? g_link.tick : -1;
        job_reset = true;
    }

    // coop-usebands (x-floor-join -GateLock -HerMenus N): N times before the hook's lock (DC_COOP_BONEDOOR), on the link's ticks
    // since the merge's lockstep, her Triangle opens her game menu and her Circle 120 ticks later shuts it, as Jep's partner
    // did six times between the merge and his picker.
    static const int her_menus = TestEnvInt("DC_COOP_BONEDOOR_HERMENUS", 0);
    static const bool lock_test = TestEnvOn("DC_COOP_BONEDOOR_LOCK");

    // (and his picker at the lock is left open for the frames: his input script's Crosses, on its own frame clock, are kept from
    // it from the lock's tick to 60 after)
    if (lock_test && g_test_lockstep_from >= 0 && !CoopWorldsApart() && g_link.tick >= g_test_lockstep_from) {
        static const int   lock_tick = TestEnvInt("DC_COOP_BONEDOOR", -1);
        const std::int64_t clock = g_link.tick - g_test_lockstep_from;

        if (clock >= lock_tick && clock <= lock_tick + 60) {
            g_link.synced[0].buttons &= ~kInputCross;
        }
    }

    if (her_menus > 0 && g_test_lockstep_from >= 0 && !CoopWorldsApart() && g_link.tick >= g_test_lockstep_from) {
        static const int   lock_at = TestEnvInt("DC_COOP_BONEDOOR", -1);
        const std::int64_t clock = g_link.tick - g_test_lockstep_from;

        for (int i = 0; i < her_menus; i++) {
            const std::int64_t open = lock_at - 400 - 250 * (her_menus - i);

            if (clock >= open && clock < open + 4) {
                g_link.synced[1].buttons |= kInputTriangle;
            } else if (clock >= open + 120 && clock < open + 124) {
                g_link.synced[1].buttons |= kInputCircle;
            }
            if (clock == open || clock == open + 120) {
                std::fprintf(stderr, "netplay: test: her menu %s (%d of %d) at clock %lld\n", clock == open ? "opens" : "shuts", i + 1, her_menus,
                             (long long) clock);
            }
        }
    }

    g_test_press_since = -1;
    static int          epoch = -1;
    // coop-stophang (found by coop-suiteaudit on t-triangle-menu-town with -Mutate menubit_here, 2026-11-01): the clock counts
    // again from the epoch after a desync's resync, so a twin whose presses desync the pair pressed them all again after the
    // resync, desynced again, and looped (77 RESETs) until its 285 s timeout. A resync puts the host's state on both copies,
    // which already holds what a press before it did: a press is spent once its first tick came, and after a resync it does
    // not come again. A merge with no resync between (a catch-up's new epoch) presses as before. Bit = the press's place in
    // the spec (the first 64); a persistent client's next job starts with none spent. DC_NETPLAY_MUTATE=press_replay (twin):
    // pressed again, as before.
    static std::uint64_t spent = 0;
    static int           spent_resyncs[64] = {};

    if (job_reset) {
        spent = 0;
        // coop-fx51a: a job's presses count from its own free play (a warm pair's next job kept the last one's start).
        // DC_NETPLAY_MUTATE=press_start_kept (warm-repeat-selftest's twin): kept, as before.
        if (!NetplayMutated("press_start_kept")) {
            start = -1;
        }
    }

    if (spec == nullptr) {
        return;
    }

    float partner[4];

    if (epoch != g_test_epoch) {
        epoch = g_test_epoch;
        start = -1;
    }
    // Before the copies are alike (a resync not yet taken, a catch-up still replaying) nothing counts; nor before a state
    // file the copy is to load has been put back (coop-fixtures: in Norune free play came before the load at tick 60, so
    // a press at clock 10 opened her menu before it and the loaded game was not the saved one: town-both, 2026-11-01).
    if (g_test_epoch == 0 || g_test_epoch_resyncs != g_link.resyncs || g_catch.active || StateFileLoadPending()) {
        start = -1;
        return;
    }
    // coop-fixtures: DC_COOP_MENUPRESS_FROM_DOOR=N: the clock stands at N on the tick the cave door test armed
    // (DC_COOP_DOORPROMPT: a player put on the door and landed there, the key given), the step the presses depend on, not
    // free play: a row's press times stay as they were (its first press just past N), each now that far past the door.
    static const int from_door = TestEnvInt("DC_COOP_MENUPRESS_FROM_DOOR", -1);

    if (start < 0 && from_door >= 0) {
        if (CoopTownDoorPromptArmed() < 0) {
            return;
        }
        start = CoopTownDoorPromptArmed(); // (N is added to since below: a start before tick 0 would read as none)
        std::fprintf(stderr, "netplay: test: menu presses count from tick %lld, at %d there (the cave door test armed)\n", (long long) start, from_door);
    }
    // coop-cand1111fix: DC_COOP_MENUPRESS_FROM_DOOR_READY=P: the clock from the link tick player P stood at Norune's opened cave door
    // with its event ready (CaveDoorSquares' own test for P's Square), the state x-cave-square's Square depends on, not a count of
    // ticks after the load. Both copies read it alike (the walker is Toan on every copy, her partner's pose is the shared one).
    static const int from_door_ready = TestEnvInt("DC_COOP_MENUPRESS_FROM_DOOR_READY", 0);

    if (start < 0 && from_door_ready > 0) {
        if (CoopTownCaveDoorReadyTick(from_door_ready) < 0) {
            return;
        }
        start = CoopTownCaveDoorReadyTick(from_door_ready);
        std::fprintf(stderr, "netplay: test: menu presses count from tick %lld (player %d at the opened cave door, its event ready)\n", (long long) start, from_door_ready);
    }
    // coop-pt1106-lockup: DC_COOP_MENUPRESS_FROM_LOCKSTEP=1: the clock from the lockstep, a menu still up (free play is its close).
    static const bool from_lockstep = TestEnvOn("DC_COOP_MENUPRESS_FROM_LOCKSTEP");

    if (start < 0 && from_lockstep && g_test_lockstep_from >= 0) {
        start = g_test_lockstep_from;
        std::fprintf(stderr, "netplay: test: menu presses count from tick %lld (the lockstep)\n", (long long) start);
    }
    if (start < 0 && ((gameTask == GAME_TASK_PLAY && CoopPartnerTarget(partner, nullptr, nullptr, nullptr)) || coop::MenuTownWalk())) {
        start = g_link.tick;
        std::fprintf(stderr, "netplay: test: menu presses count from tick %lld\n", (long long) start);
        coop::InventoryTestSeed(); // coop-itemsend: DC_COOP_BAGSEED
    }
    if (start < 0) {
        return;
    }

    // The test's own script taps player 1's Cross for the floor select (after the join's resync too); from
    // free play on it is not his. coop-loadpull: DC_COOP_MENUPRESS_APART_CROSS=1 leaves his own Cross apart alone (his
    // copy steps its pads as linked player 1 then: w-town-apart -Case saveloadin's menu presses apart, DC_WORLDS_TEST_APART_PRESS).
    static const bool apart_cross = TestEnvOn("DC_COOP_MENUPRESS_APART_CROSS");

    if (!(apart_cross && CoopWorldsApart())) {
        g_link.synced[0].buttons &= ~kInputCross;
    }

    std::int64_t since = g_link.tick - start + (from_door > 0 ? from_door : 0);

    g_test_press_since = since;

    // coop-menuflake: DC_COOP_MENUPRESS_EXIT=N: N ticks of the same clock on, this copy quits as a closed window does (exit 0, its
    // logs flushed). The test's window after the presses is then the same ticks whatever the join cost, where a frame budget ran
    // out inside a slow join's catch-up on a loaded PC.
    static std::int64_t quit_after = TestEnvInt("DC_COOP_MENUPRESS_EXIT", -1);
    static bool         quit_sent = false;

    if (job_reset) {
        job_reset = false;
        quit_after = TestEnvInt("DC_COOP_MENUPRESS_EXIT", -1);
        quit_sent = false;
    }
    // (a persistent client's job: the exit's tick ends the job's presses, logged, and the copy runs on for the next job)
    if (quit_after >= 0 && since >= quit_after && !quit_sent && TestEnv("DC_QA_SERVE") != nullptr) {
        quit_sent = true;
        std::fprintf(stderr, "netplay: test: menu presses clock at %lld ticks: the job's presses are over\n", (long long) since);
        std::fflush(stdout);
    }
    if (quit_after >= 0 && since >= quit_after && !quit_sent) {
        SDL_Event quit{};

        quit_sent = true;
        quit.type = SDL_EVENT_QUIT;
        SDL_PushEvent(&quit);
        std::fprintf(stderr, "netplay: test: menu presses clock at %lld ticks: this copy quits\n", (long long) since);
        std::fflush(stdout);
    }
    // coop-menulocal: DC_COOP_MENUPRESS_EXIT_CLOSED=1: the quit on the presses' last event instead of a tick: once the clock is
    // past the last press and its player is in no menu (that menu's close has run). The in-menu bits are alike on every copy,
    // so both copies quit on one link tick, where a test's poll of its stop files let a fast copy run on into a resync.
    // =compare: and then on the first checksum compare of a frame after that close (a check that the copies stayed alike
    // through the menu needs one; its twin parts them there, before the resync it sets).
    static const char *quit_closed_mode = TestEnv("DC_COOP_MENUPRESS_EXIT_CLOSED");
    static const bool  quit_closed = quit_closed_mode != nullptr && *quit_closed_mode != '\0' && std::strcmp(quit_closed_mode, "0") != 0;
    static const bool  quit_compare = quit_closed && std::strcmp(quit_closed_mode, "compare") == 0;
    static std::int64_t closed_tick = -1;

    if (quit_closed && !quit_sent && spec != nullptr) {
        long last_at = -1;
        int  last_player = 0;

        // (each press is P:BUTTON:T)
        for (const char *at = spec; at != nullptr && *at != '\0';) {
            const char *next = std::strchr(at, ',');
            const char *first = std::strchr(at, ':');
            const char *second = first != nullptr ? std::strchr(first + 1, ':') : nullptr;

            if (second != nullptr && (next == nullptr || second < next) && std::atol(second + 1) > last_at) {
                last_at = std::atol(second + 1);
                last_player = std::atoi(at);
            }
            at = next != nullptr ? next + 1 : nullptr;
        }
        if (last_at < 0 || since <= last_at) {
            closed_tick = -1;
        } else if (closed_tick < 0 && !coop::PlayerInMenu(last_player, coop::kActorMenuFull | coop::kActorMenuTown | coop::kActorMenuLocal)) {
            closed_tick = g_link.tick;
        }
        if (closed_tick >= 0 && (!quit_compare || g_test_compared > closed_tick)) {
            SDL_Event quit{};

            quit_sent = true;
            quit.type = SDL_EVENT_QUIT;
            SDL_PushEvent(&quit);
            std::fprintf(stderr, "netplay: test: menu presses clock at %lld ticks: player %d's last menu closed%s, this copy quits\n",
                         (long long) since, last_player, quit_compare ? " and a compare after it ran" : "");
            std::fflush(stdout);
        }
    }

    // coop-itemsend: DC_COOP_MENUSHOTS=DIR:T,T,... writes this copy's frame to DIR/p<player>-<T>.png at those ticks of
    // the same clock, so a test sees what a press left on the screen (the game's own frame, not the desktop). The
    // capture waits for the frame's end (NetplayTestShotAfterFrame): a frame can only be read outside one.
    static const char *shots = TestEnv("DC_COOP_MENUSHOTS");
    static int         shots_generation = TestJobGeneration();

    if (shots_generation != TestJobGeneration()) { // coop-serve: a job's own frames
        shots_generation = TestJobGeneration();
        shots = TestEnv("DC_COOP_MENUSHOTS");
    }

    if (shots != nullptr) {
        const char *ticks = std::strrchr(shots, ':');

        for (const char *at = ticks != nullptr ? ticks + 1 : nullptr; at != nullptr && *at != '\0';) {
            if (std::atoll(at) == since) {
                char path[512];

                std::snprintf(path, sizeof path, "%.*s/p%d-%lld.png", (int) (ticks - shots), shots, NetplayLocalPlayer(), (long long) since);
                g_test_shot = path;
            }
            at = std::strchr(at, ',');
            at = at != nullptr ? at + 1 : nullptr;
        }
    }

    int index = 0;

    for (const char *at = spec; at != nullptr && *at != '\0'; index++) {
        int  player = std::atoi(at);
        char name[16] = {};
        long when = -1;
        const bool          parsed = std::sscanf(at, "%*d:%15[a-z0-9]:%ld", name, &when) == 2;
        const std::uint64_t bit = index < 64 ? (std::uint64_t) 1 << index : 0;
        const bool          replay = bit != 0 && (spent & bit) != 0 && spent_resyncs[index] != g_link.resyncs && !NetplayMutated("press_replay");

        if (parsed && since == when && bit != 0 && !replay) {
            spent |= bit;
            spent_resyncs[index] = g_link.resyncs;
        }
        if (parsed && since == when && replay) {
            std::fprintf(stderr, "netplay: test: player %d's %s at clock %ld came before the resync: not pressed again\n", player, name, when);
        }

        // coop-feathersync: <button>hold (squarehold): that button held 60 ticks, as a stick is.
        const std::size_t length = std::strlen(name);
        const bool        hold = length > 4 && std::strcmp(name + length - 4, "hold") == 0;
        char              base[16] = {};

        std::memcpy(base, name, hold ? length - 4 : length);
        if (parsed && !replay && (player == 1 || player == 2) && since >= when && since < when + (std::strncmp(name, "stick", 5) == 0 || hold ? 60 : 4)) {
            std::uint16_t button = std::strcmp(base, "triangle") == 0 ? kInputTriangle
                                   : std::strcmp(base, "start") == 0  ? kInputStart
                                   : std::strcmp(base, "circle") == 0 ? kInputCircle
                                   : std::strcmp(base, "cross") == 0  ? kInputCross
                                   : std::strcmp(base, "square") == 0 ? kInputSquare
                                   : std::strcmp(base, "up") == 0     ? kInputUp
                                   : std::strcmp(base, "down") == 0   ? kInputDown
                                   : std::strcmp(base, "left") == 0   ? kInputLeft
                                   : std::strcmp(base, "right") == 0  ? kInputRight
                                   : std::strcmp(base, "select") == 0 ? kInputSelect // coop-itemsend
                                   : std::strcmp(base, "r1") == 0     ? kInputR1     // coop-keytab: the board's pages
                                   : std::strcmp(base, "l1") == 0     ? kInputL1
                                   : std::strcmp(base, "r2") == 0     ? kInputR2 // coop-dranscheat: the cheat's chord
                                   : std::strcmp(base, "l2") == 0     ? kInputL2
                                                                      : 0;

            g_link.synced[player - 1].buttons |= button;
            // stickleft / stickright / stickup / stickdown: the left stick held over for 60 ticks (a walk), not a button.
            if (std::strncmp(base, "stick", 5) == 0 && since < when + 60) {
                if (std::strcmp(base, "stickup") == 0 || std::strcmp(base, "stickdown") == 0) {
                    g_link.synced[player - 1].left_y = std::strcmp(base, "stickup") == 0 ? 0 : 255;
                } else {
                    g_link.synced[player - 1].left_x = std::strcmp(base, "stickleft") == 0 ? 0 : 255;
                }
            }
            if (since == when) {
                std::fprintf(stderr, "netplay: test: player %d presses %s at tick %lld\n", player, name, (long long) g_link.tick);
                std::printf("coop-test: player %d presses %s at tick %lld\n", player, name, (long long) g_link.tick); // the game's own lines' stream
            }
        }
        at = std::strchr(at, ',');
        at = at != nullptr ? at + 1 : nullptr;
    }
}

} // namespace

// coop-framecap: every test frame this copy writes goes through here, under one cap a copy: DC_COOP_TEST_SHOT_MAX frames
// (1500 unset) and DC_COOP_TEST_SHOT_MAX_MB megabytes (6144 unset), whichever comes first. A frame is 3.5 MB at 1080p:
// test captures filled 700 GB of Jep's disk in a day (2026-10-28; a merge that never locked in wrote 46 GB a run). Past the
// cap nothing more is written and the copy says so once, "coop-test: frame cap hit", which CoopTest's Complete-Test turns
// into a failed check (the run's frames stop, so what it checks from them cannot be trusted).
bool NetplayTestWritePng(const char *path, const std::uint8_t *rgba, std::uint32_t width, std::uint32_t height, const char *same_as) {
    static std::mutex          lock; // (coop-shotthread: the shot writer's thread and coop_worlds' game-thread hooks both write)
    std::lock_guard            hold(lock);
    static const long          max_frames = TestEnvInt("DC_COOP_TEST_SHOT_MAX", 1500);
    static const std::uint64_t max_bytes = (std::uint64_t) TestEnvInt("DC_COOP_TEST_SHOT_MAX_MB", 6144) << 20;
    static long                frames = 0;
    static std::uint64_t       bytes = 0;
    static bool                said = false;
    // coop-warmreset (WS7 step 1): the cap is a job's, not a process's: a warm pair's jobs together reached it, so one job failed on
    // a cap it did not earn and every later one wrote no frames and said nothing (said already). DC_COOP_MUTATE=job_shots_kept
    // (twin): counted over the process, as before.
    COOP_TEST_JOB_RESET(frames, 0L, "job_shots_kept");
    COOP_TEST_JOB_RESET(bytes, (std::uint64_t) 0, "job_shots_kept");
    COOP_TEST_JOB_RESET(said, false, "job_shots_kept");

    if (frames >= max_frames || bytes >= max_bytes) {
        if (!said) {
            said = true;
            std::printf("coop-test: frame cap hit: %ld frames, %llu MB written (cap %ld frames, %llu MB); %s and later not written\n", frames,
                        (unsigned long long) (bytes >> 20), max_frames, (unsigned long long) (max_bytes >> 20), path);
            std::fprintf(stderr, "coop-test: frame cap hit: %ld frames, %llu MB written\n", frames, (unsigned long long) (bytes >> 20));
            std::fflush(stdout);
        }
        return false;
    }
    std::error_code linked;

    if (same_as != nullptr) {
        std::filesystem::remove(PathsFromUtf8(path), linked);
        std::filesystem::create_hard_link(PathsFromUtf8(same_as), PathsFromUtf8(path), linked);
        if (linked) {
            std::filesystem::copy_file(PathsFromUtf8(same_as), PathsFromUtf8(path), std::filesystem::copy_options::overwrite_existing, linked);
        }
    }
    // (coop-shotthread: written beside it and renamed, so a test that waits for the file never opens half of it: l-local-play
    // -HerSeries stops both copies once her last frame's file is there)
    if (same_as == nullptr || linked) {
        const std::string part = std::string(path) + ".part";
        std::error_code   renamed;

        if (!gfx::WritePng(PathsFromUtf8(part.c_str()), rgba, width, height)) {
            return false;
        }
        std::filesystem::rename(PathsFromUtf8(part.c_str()), PathsFromUtf8(path), renamed);
        if (renamed) {
            return false;
        }
    }
    frames++;
    if (std::FILE *f = std::fopen(path, "rb"); f != nullptr) {
        std::fseek(f, 0, SEEK_END);
        bytes += (std::uint64_t) std::max(0L, std::ftell(f));
        std::fclose(f);
    } else {
        bytes += (std::uint64_t) width * height * 4; // (unreadable: count it as raw)
    }
    return true;
}

// coop-framecap: a frame's mean level (0-255) and FNV-1a hash of its pixels on a 16-pixel grid from (4, 4), x outer, the
// numbers x-floor-join's $readSeries took from the PNG, so a series check needs no file.
static void TestFrameLook(const std::vector<std::uint8_t> &rgba, std::uint32_t width, std::uint32_t height, double &level, std::uint32_t &hash) {
    double sum = 0.0;
    long   n = 0;

    hash = 2166136261u;
    for (std::uint32_t x = 4; x < width; x += 16) {
        for (std::uint32_t y = 4; y < height; y += 16) {
            const std::uint8_t *p = &rgba[((std::size_t) y * width + x) * 4];

            sum += p[0] + p[1] + p[2];
            n++;
            hash = (std::uint32_t) (((std::uint64_t) (hash ^ ((std::uint32_t) p[0] << 16 | (std::uint32_t) p[1] << 8 | p[2])) * 16777619u) % 4294967296ull);
        }
    }
    level = sum / (3.0 * std::max(1L, n));
}

// coop-shotthread (l-local-play-houseout-seamless: the host series' 601 frames cost 29 ms a frame of PNG on his game thread,
// so his house-out replay ran at 61 ticks a second for 59-83 s and never gained on her 60): a taken frame's PNG is encoded and
// written on one writer thread. The readback stays on the game thread (it is the frame); the writer prints "frame written"
// once the file is on disk, so a test waiting on that line can open it. At most kShotQueueMax frames wait (the game thread
// blocks past that and says so once); the queue drains at exit. DC_NETPLAY_MUTATE=shots_inline (twin): written on the game
// thread, as before.
namespace {

struct TakenShot {
    std::string                           path;
    std::vector<std::uint8_t>             rgba;
    std::uint32_t                         width = 0, height = 0;
    double                                readback_ms = 0.0;
    std::string                           same_as; // (the arrival hold's first frame: these pixels again, linked to it)
    std::chrono::steady_clock::time_point taken;
};

constexpr std::size_t kShotQueueMax = 32; // (4.9 MB a frame at 1280x960: 157 MB at most)

struct ShotWriter {
    std::mutex              lock;
    std::condition_variable changed;
    std::deque<TakenShot>   queue;
    bool                    busy = false, stopping = false, started = false, said_full = false;
    std::thread             thread;
    // The writer's "frame written" lines, said by the game thread (SayShotsDone): two threads printing to the one log
    // interleaved the game's own lines mid-line (run 4: "ecatchadback up: lockstep ..."), and the writer's flush held the
    // stream's lock against the game thread's next line.
    std::vector<std::string> done;
} g_shots;

std::string ShotWrittenLine(const TakenShot &shot, bool ok, std::chrono::steady_clock::time_point write_from) {
    const auto now = std::chrono::steady_clock::now();
    char       line[1024];

    std::snprintf(line, sizeof line, "coop-test: frame written to %s: %s (%ux%u) in %.1f ms (readback %.1f, png %.1f)\n", shot.path.c_str(), ok ? "ok" : "failed", shot.width,
                  shot.height, shot.readback_ms + std::chrono::duration<double, std::milli>(now - write_from).count(), shot.readback_ms,
                  std::chrono::duration<double, std::milli>(now - write_from).count());
    return line;
}

void SayShotWritten(const TakenShot &shot, bool ok, std::chrono::steady_clock::time_point write_from) {
    std::fputs(ShotWrittenLine(shot, ok, write_from).c_str(), stdout);
    std::fflush(stdout);
}

// The game thread's own shot lines ("series frame", "frame taken") while it replays: a test copy's stdout is unbuffered
// (DC_QA_ONE_STREAM), so each line is a write to the harness's pipe, and through his catch-up they cost a tick about 7 ms
// (run 5: 9.5 ms a tick outside the netplay tick with the captures at 1.4 ms, 2.8 with no series: 60 ticks a second). Held
// here and said, in order, at the first frame after the replay.
std::string g_replay_lines;

void SayShotLine(const char *line) {
    if (NetplayCatchingUp() && !NetplayMutated("shots_inline")) { // (the twin: said at once, as before)
        g_replay_lines += line;
        return;
    }
    if (!g_replay_lines.empty()) {
        std::fputs(g_replay_lines.c_str(), stdout);
        g_replay_lines.clear();
    }
    if (NetplayCatchingUp()) {
        g_catch.replay_lines_said++;
    }
    std::fputs(line, stdout);
}

// The game thread, each frame outside a replay (a flush there costs his catch-up a timer quantum) and at exit: the files
// the writer has put on disk since, said in the order written, after the replay's held lines.
void SayShotsDone() {
    std::vector<std::string> lines;

    {
        std::lock_guard hold(g_shots.lock);

        lines.swap(g_shots.done);
    }
    if (!g_replay_lines.empty()) {
        std::fputs(g_replay_lines.c_str(), stdout);
        g_replay_lines.clear();
    }
    for (const std::string &line : lines) {
        std::fputs(line.c_str(), stdout);
    }
    if (!lines.empty()) {
        std::fflush(stdout);
    }
}

void ShotWriterRun() {
    std::unique_lock held(g_shots.lock);

    for (;;) {
        g_shots.changed.wait(held, [] { return !g_shots.queue.empty() || g_shots.stopping; });
        if (g_shots.queue.empty()) {
            return;
        }
        TakenShot shot = std::move(g_shots.queue.front());

        g_shots.queue.pop_front();
        g_shots.busy = true;
        held.unlock();
        g_shots.changed.notify_all(); // (a game thread waiting on a full queue)

        const auto from = std::chrono::steady_clock::now();
        const bool ok = NetplayTestWritePng(shot.path.c_str(), shot.rgba.data(), shot.width, shot.height, shot.same_as.empty() ? nullptr : shot.same_as.c_str());

        std::string line = ShotWrittenLine(shot, ok, from);

        held.lock();
        g_shots.done.push_back(std::move(line));
        g_shots.busy = false;
        g_shots.changed.notify_all();
    }
}

// At exit (registered at the writer's start): every frame taken is written before the copy goes.
void ShotWriterDrain() {
    {
        std::lock_guard hold(g_shots.lock);

        if (!g_shots.started) {
            return;
        }
        g_shots.stopping = true;
    }
    g_shots.changed.notify_all();
    if (g_shots.thread.joinable()) {
        g_shots.thread.join();
    }
    SayShotsDone();
}

void QueueShot(TakenShot &&shot) {
    std::unique_lock held(g_shots.lock);

    if (!g_shots.started) {
        g_shots.started = true;
        g_shots.thread = std::thread(ShotWriterRun);
        std::atexit(ShotWriterDrain);
    }
    bool &said_full = g_shots.said_full; // (coop-warmreset, WS7: said once a job)
    COOP_TEST_TRACE_RESET(said_full, false);
    if (g_shots.queue.size() >= kShotQueueMax) {
        if (!g_shots.said_full) {
            g_shots.said_full = true;
            std::fprintf(stderr, "coop-test: frame writer behind: %zu frames wait; the game waits for it (said once)\n", g_shots.queue.size());
        }
        g_shots.changed.wait(held, [] { return g_shots.queue.size() < kShotQueueMax; });
    }
    g_shots.queue.push_back(std::move(shot));
    held.unlock();
    g_shots.changed.notify_all();
}

} // namespace

// From CoopWorldsAfterFrame, outside a frame: DC_COOP_MENUSHOTS's pending capture.
void NetplayTestShotAfterFrame() {
    g_frames_drawn++; // coop-ws5left: a frame drawn (the state set's take waits for one after its checkpoint)
    if (!NetplayCatchingUp()) {
        SayShotsDone(); // (coop-shotthread)
    }

    // coop-localfix test hook (DC_COOP_TEST_CATCHUP_SHOTS=DIR, the joiner): every 6th frame while its catch-up is under way (a
    // merge's: the link up, his state not yet in or its replay running), what it shows, to DIR/catch-NNN.png (Jep's 2026-10-26
    // retest: "if Toan leaves the house, Xiao's screen shows the skybox for a second").
    if (static const char *const dir = TestEnv("DC_COOP_TEST_CATCHUP_SHOTS"); dir != nullptr && g_test_shot.empty()) {
        static int frames = 0, taken = 0;
        static char path[512];

        if (TakesState() && g_catch.active && taken < 60) {
            if (frames++ % 6 == 0) {
                std::snprintf(path, sizeof path, "%s/catch-%03d.png", dir, taken++);
                g_test_shot = path;
            }
        } else {
            frames = 0;
        }
    }

    // coop-dungeonjoin test hook (DC_COOP_TEST_MERGE_SERIES=DIR:EVERY:AFTER): a floor merge as this copy shows it, every
    // EVERY-th frame to DIR/merge-NNNNN.png: from the first frame apart in a dungeon with the other copy heard on a floor of
    // it and this copy off the entrance's floor menu (the arriver from its pick, the one already there from the other's
    // floor start) to AFTER frames into the lockstep that follows (x-floor-join -Seamless).
    if (static const char *const merge = TestEnv("DC_COOP_TEST_MERGE_SERIES"); merge != nullptr && g_test_shot.empty()) {
        static long        frame = 0, after = -1, every = 1, lock_frames = -1;
        static bool        started = false, done = false, linked = false;
        static std::string dir;
        static char        path[512];

        if (after < 0) {
            const std::string spec = merge;
            const std::size_t c2 = spec.rfind(':');
            const std::size_t c1 = spec.rfind(':', c2 - 1);

            dir = spec.substr(0, c1);
            every = std::max(1L, std::strtol(spec.c_str() + c1 + 1, nullptr, 10));
            after = std::max(0L, std::strtol(spec.c_str() + c2 + 1, nullptr, 10));
        }

        const int here = CoopWorldsHereFloor();
        // (coop-cutscenejoin: or on its way onto one, its opening scene playing, with this copy arriving there: x-floor-join
        // -Case scene's wait. Each line says whether this copy shows its own arrival, joining from it: -Case pick.)
        const int  peer = selectMapNo >= 0 && mode == GAME_MODE_DUNGEON ? CoopWorldsPeerFloorIn(selectMapNo) : -1;
        const bool opening = peer < 0 && mode == GAME_MODE_DUNGEON && CoopWorldsPeerOpeningFloor() >= 0 && CoopWorldsArrivingApart();
        const bool own_arrival = CoopWorldsPickJoining() && !(g_link.up && g_snap.applied);

        if (done) {
            std::fflush(stdout); // (the lines after it reach the log before the test stops this copy)
        }
        if (!started && !done && CoopWorldsApart() && (peer >= 0 || opening) && here != -1 && here != -1000) {
            started = true;
            std::fprintf(stderr, "coop-test: merge series starts (here floor %d, the other's %d)\n", here, peer);
        }
        // coop-joinlate (train 49's x-floor-join-late-seamless red, "0 frames ... to its lockstep (frame none)"): a replayed tick of
        // a catch-up is never shown (GameRenderTick skips it, the window holds), but each counted as a frame here, so a replay of
        // 1214 ticks behind a host that ran on unbounded through a 100 ms-lag transfer ate the series' 2400 frames before the
        // lockstep. The series counts and takes the frames shown. DC_NETPLAY_MUTATE=series_counts_replay (twin): each, as before.
        if (started && !done && (!NetplayCatchingUp() || NetplayMutated("series_counts_replay"))) {
            linked = linked || g_link.up;
            if (linked && g_link.up && !g_catch.active && !CoopWorldsApart()) {
                lock_frames++;
            }
            if (frame % every == 0) {
                // coop-framecap: the frame's level and hash are taken here and put on the line (a check reads them, no file);
                // DC_COOP_TEST_MERGE_SERIES_PNG=K writes the PNG of every K-th capture only (1 unset: all, 0: none).
                static const long         png_every = TestEnvInt("DC_COOP_TEST_MERGE_SERIES_PNG", 1);
                static long               captures = 0;
                std::vector<std::uint8_t> rgba;
                std::uint32_t             width = 0, height = 0;
                double                    level = -1.0;
                std::uint32_t             hash = 0;

                // (coop-joinlate: no capture in a catch-up's replay any more, above, so none of its 6.5 ms a tick of read-backs)
                if (dc::app::GameScreenshot(rgba, width, height) && width >= 8 && height >= 8) {
                    TestFrameLook(rgba, width, height, level, hash);
                }
                if (png_every > 0 && captures % png_every == 0) {
                    std::snprintf(path, sizeof path, "%s/merge-%05ld.png", dir.c_str(), frame);
                    g_test_shot = path;
                }
                captures++;
                // (coop-cutsceneview: and the other's view of its scene this copy shows, by the other's count of it, 0 none.)
                // (coop-joinlate: said through SayShotLine, held while the replay runs: each line a pipe write on a test copy)
                char frame_line[384];

                std::snprintf(frame_line, sizeof frame_line,
                              "coop-test: merge frame %ld: link %d catch-up %d apart %d here %d other %d task %d lockstep %ld arrival %d hold %d scene %u walk %d level %.3f hash %u replay %d\n",
                              frame, (int) g_link.up, g_catch.active ? 1 : 0, CoopWorldsApart() ? 1 : 0, here, peer, (int) gameTask, lock_frames,
                              own_arrival ? 1 : 0, NetplayArrivalHold() ? 1 : 0, CoopWorldsSceneViewSeq(), coop::ArrivalWalkPlayer(), level, hash, NetplayCatchingUp() ? 1 : 0);
                SayShotLine(frame_line);
            }
            frame++;
            // (coop-latefall: at most DC_COOP_TEST_MERGE_SERIES_MAX frames, 20000 unset: a merge that never came wrote 13334 PNGs, 46 GB)
            static const long max_frames = TestEnvInt("DC_COOP_TEST_MERGE_SERIES_MAX", 20000);

            if (lock_frames >= after || frame > max_frames) {
                done = true;
                std::fprintf(stderr, "coop-test: merge series done (%ld frames, %ld in the lockstep)\n", frame, lock_frames + 1);
            }
        }
    }

    // coop-localfix test hook (DC_COOP_TEST_SHOT_SERIES=DIR:FROM:TO:EVERY): this copy's frames FROM to TO (counted from its
    // start), every EVERY-th, to DIR/series-FRAME.png, whatever the link is doing (a windowed run's skybox second).
    // coop-warmconvert (WS7 round 4, t-georama-bar and v-hud-face as warm jobs): the series is a job's own on a warm pair (its
    // folder the row's, its frames and mark counted from the job's begin, as a fresh copy's from its start).
    // DC_COOP_MUTATE=switch_stale (warm-switch-selftest's twin): read once, as before.
    // (its counts put back in ServeResetForJob, before the job's fixture load can mark it)
    COOP_TEST_SWITCH(const char *, series, TestEnv("DC_COOP_TEST_SHOT_SERIES"));
    if (series != nullptr) {
        long              &frame = g_test_series_count;
        static std::string dir;
        long              &from = g_test_series_from;
        static long        to = -1, every = 1;
        static char        path[512];

        if (from < 0) {
            const std::string spec = series;
            const std::size_t c3 = spec.rfind(':');
            const std::size_t c2 = spec.rfind(':', c3 - 1);
            const std::size_t c1 = spec.rfind(':', c2 - 1);

            dir = spec.substr(0, c1);
            from = std::strtol(spec.c_str() + c1 + 1, nullptr, 10);
            to = std::strtol(spec.c_str() + c2 + 1, nullptr, 10);
            every = std::max(1L, std::strtol(spec.c_str() + c3 + 1, nullptr, 10));
        }
        // coop-herexit2: DC_COOP_TEST_SHOT_SERIES_MARK=1: FROM and TO count from the frame a test hook marks (her own exit
        // out of the house, NetplayTestSeriesMark), none before it.
        COOP_TEST_SWITCH(bool, marked_only, TestEnvOn("DC_COOP_TEST_SHOT_SERIES_MARK"));

        if (marked_only && g_test_series_mark >= 0 && from >= 0 && !g_test_series_moved) {
            g_test_series_moved = true;
            from += g_test_series_mark;
            to += g_test_series_mark;
        }
        if (g_test_shot.empty() && (!marked_only || g_test_series_moved) && frame >= from && frame <= to && (frame - from) % every == 0) {
            std::snprintf(path, sizeof path, "%s/series-%05ld.png", dir.c_str(), frame);
            g_test_shot = path;
            char line[128];

            std::snprintf(line, sizeof line, "coop-test: series frame %ld at link tick %lld, catch-up %d\n", frame, (long long) g_link.tick, g_catch.active ? 1 : 0);
            SayShotLine(line); // (coop-shotthread)
        }
        frame++;
        g_test_series_frame = frame;
    }

    if (g_test_shot.empty()) {
        return;
    }

    // coop-shotthread: through his arrival hold (his replay draws nothing) every capture renders the same black list again
    // and reads it back (10 ms a frame, the host series' 601 frames held his replay at 61 ticks a second): the hold's first
    // capture is kept and handed out again until the hold ends, and its file is linked, not encoded again (run 3: the
    // writer's encodes beside a loaded PC's 18 copies still held his replay at 62). (Not with shots_inline, the twin.)
    static std::vector<std::uint8_t> held_rgba;
    static std::string               held_path;
    static std::uint32_t             held_width = 0, held_height = 0;
    const bool                       inline_shots = NetplayMutated("shots_inline");
    const bool                       hold = NetplayArrivalHold() && !inline_shots;
    TakenShot                        shot;
    const auto                       began = std::chrono::steady_clock::now();
    bool                             read = false;

    if (hold && !held_rgba.empty()) {
        shot.rgba = held_rgba;
        shot.width = held_width;
        shot.height = held_height;
        shot.same_as = held_path;
        read = true;
    } else {
        read = dc::app::GameScreenshot(shot.rgba, shot.width, shot.height) && shot.width * shot.height > 0;
        if (NetplayCatchingUp()) {
            g_catch.replay_readbacks++;
        }
    }
    if (!hold) {
        held_rgba.clear();
    } else if (read && held_rgba.empty()) {
        held_rgba = shot.rgba;
        held_width = shot.width;
        held_height = shot.height;
        held_path = g_test_shot;
    }

    shot.taken = std::chrono::steady_clock::now();
    shot.readback_ms = std::chrono::duration<double, std::milli>(shot.taken - began).count();
    shot.path = g_test_shot;
    // (the line at the frame itself, in order with the game's own lines: t-menuleak's arrow and l-local-play's wait window
    // read what came before it; "frame written" comes later, from the writer)
    char taken_line[640];

    std::snprintf(taken_line, sizeof taken_line, "coop-test: frame taken for %s (%ux%u) in %.1f ms\n", shot.path.c_str(), shot.width, shot.height, shot.readback_ms);
    SayShotLine(taken_line);
    const bool replaying = NetplayCatchingUp(); // (no flush in a replay: a timer quantum each)
    if (!read) {
        SayShotWritten(shot, false, shot.taken);
    } else if (inline_shots) {
        // (coop-pacerdark: the frame's cost on the game thread, the readback and the PNG apart)
        SayShotWritten(shot, NetplayTestWritePng(shot.path.c_str(), shot.rgba.data(), shot.width, shot.height), shot.taken);
    } else {
        if (!replaying) {
            std::fflush(stdout);
        }
        QueueShot(std::move(shot));
    }
    // coop-inlinetwin: the frame's whole cost on the game thread (readback, and the PNG with shots_inline, or the queue's wait
    // when full), what the replay pays for it: l-local-play -HostSeries checks it inside his catch-up.
    char held_line[640];

    std::snprintf(held_line, sizeof held_line, "coop-test: frame for %s held the game thread %.1f ms%s\n", g_test_shot.c_str(),
                  std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - began).count(), replaying ? " (in a catch-up)" : "");
    SayShotLine(held_line);
    g_test_shot.clear();
}

// coop-padswap test hook (DC_COOP_TEST_PAD_PROBE=K, w-indoor -Case closetpads): K link ticks after the room's event player 2
// started (NetplayPadProbeArm, the same tick on both copies), each player's agreed pad holds its stick down alone in turn,
// no buttons: player 1's for ticks 0-119 of the probe, then none to 179, then player 2's for 180-299, none to 359. Whose
// character walks shows who drives whom. NetplayPadProbePhase: 1 his stick, 2 hers, 3 a pause, 0 no probe.
static std::int64_t g_pad_probe_from = -1;

void NetplayPadProbeArm() {
    static const char *const spec = TestEnv("DC_COOP_TEST_PAD_PROBE");

    if (spec != nullptr && g_pad_probe_from < 0) {
        g_pad_probe_from = g_link.tick + std::atoll(spec);
        std::fprintf(stderr, "netplay: test: pad probe from link tick %lld\n", (long long) g_pad_probe_from);
    }
}

int NetplayPadProbePhase() {
    const std::int64_t at = g_pad_probe_from < 0 ? -1 : g_link.tick - g_pad_probe_from;

    return at < 0 || at >= 360 ? 0 : at < 120 ? 1 : at >= 180 && at < 300 ? 2 : 3;
}

static void PadProbe() {
    const int phase = NetplayPadProbePhase();

    if (phase == 0) {
        return;
    }

    for (int i = 0; i < 2; ++i) {
        g_link.synced[i] = Neutral();
        if (phase == i + 1) {
            g_link.synced[i].left_y = 0xff;
        }
    }
}

// coop-atlacross: each player's buttons pressed this tick on the agreed pads (down now, up on the tick before), taken in Route
// before the tick's pads become the previous ones; the same on both copies (NetplayPlayerPressed).
static std::uint16_t g_player_pressed[2] = {};

// coop-closetitem test hook (DC_COOP_TEST_MES_PRESSES=1, w-indoor -Case closetmes): from the tick after a system message that
// waits for a button is set for player 2 (NetplayMesPressArm, the same tick on both copies), the agreed pads' Crosses are the
// test's, sticks kept: for ticks 0-239 player 1's Cross down 4 ticks in every 20 and hers up; 240-299 both up; 300-599 hers
// down 4 ticks in every 40 and his up. NetplayMesPressPhase: 1 his presses, 2 a pause, 3 hers, 0 none.
// coop-closetglow (DC_COOP_TEST_MES_PRESSES=walk, w-indoor -Case closetwalk): his stick in place of his Crosses, a second each
// way in turn for ticks 0-299 while her box is up (his walk through it), then centred; her Crosses as above.
static std::int64_t g_mes_press_from = -1;

void NetplayMesPressArm() {
    static const bool on = TestEnv("DC_COOP_TEST_MES_PRESSES") != nullptr;

    if (on && g_mes_press_from < 0) {
        g_mes_press_from = g_link.tick + 1;
        std::fprintf(stderr, "netplay: test: message presses from link tick %lld\n", (long long) g_mes_press_from);
    }
}

int NetplayMesPressPhase() {
    const std::int64_t at = g_mes_press_from < 0 ? -1 : g_link.tick - g_mes_press_from;

    return at < 0 || at >= 600 ? 0 : at < 240 ? 1 : at < 300 ? 2 : 3;
}

static void MesPresses() {
    const int phase = NetplayMesPressPhase();

    if (phase == 0) {
        return;
    }

    const std::int64_t at = g_link.tick - g_mes_press_from;

    static const bool walk = [] {
        const char *v = TestEnv("DC_COOP_TEST_MES_PRESSES");
        return v != nullptr && std::strcmp(v, "walk") == 0;
    }();

    for (int i = 0; i < 2; ++i) {
        g_link.synced[i].buttons &= ~kInputCross;
    }
    if (walk) {
        // (a second each way in turn: up, right, down, left, so a wall ahead stops only one of them)
        static const std::uint8_t ways[4][2] = {{128, 0}, {255, 128}, {128, 255}, {0, 128}};
        const int                 way = (int) (at / 60 % 4);

        g_link.synced[0].left_x = phase <= 2 ? ways[way][0] : 128;
        g_link.synced[0].left_y = phase <= 2 ? ways[way][1] : 128;
    } else if (phase == 1 && at % 20 < 4) {
        g_link.synced[0].buttons |= kInputCross;
    }
    if (phase == 3 && (at - 300) % 40 < 4) {
        g_link.synced[1].buttons |= kInputCross;
    }
}

// coop-dranscheat (coop_inventory.hpp): each player's L1+R1 held, then Select+Triangle, on the agreed pads gives that player a Dran's
// Feather once per press of the whole chord; while L1 and R1 are both held, and after until Select, Triangle and Start are all up,
// that player's Select, Triangle and Start are taken off the agreed pad, so no ring, menu, menu claim or pause opens for the press.
// Before Route reads the pads, so every reader and a catch-up's replay (whose recorded pads come before this) see the same. The two
// flags follow the agreed pads alone. The chord keeps clear of the game's own pad-1 debug chords, which read the copy's raw pad and
// so cannot be masked here: Select+Start (the developer menu while DebugMode is on, which a --jump turns on), R3 with all four
// shoulders (the debug mode toggle), Select+L2 (the debug pad's modifier), L3+R3 (the debug start picker) and L3 (the town's debug
// overlay, her member swap). DC_COOP_MUTATE=dranscheat_mask: Select, Triangle and Start go on to the game as before (twin).
static bool g_cheat_chord[2] = {};
static bool g_cheat_mask[2] = {};

static void DransCheatPads() {
    if (!coop::DransCheatOn()) {
        return;
    }

    constexpr std::uint16_t kHeld = kInputL1 | kInputR1;
    constexpr std::uint16_t kChord = kHeld | kInputSelect | kInputTriangle;
    constexpr std::uint16_t kMasked = kInputSelect | kInputTriangle | kInputStart;

    for (int i = 0; i < 2; ++i) {
        auto      &buttons = g_link.synced[i].buttons;
        const bool chord = (buttons & kChord) == kChord;

        if (chord && !g_cheat_chord[i]) {
            coop::DransCheatGive(i);
        }
        g_cheat_chord[i] = chord;

        if ((buttons & kHeld) == kHeld) {
            g_cheat_mask[i] = true;
        } else if ((buttons & kMasked) == 0) {
            g_cheat_mask[i] = false;
        }
        if (g_cheat_mask[i] && !coop::Mutated("dranscheat_mask")) {
            buttons &= ~kMasked;
        }
    }
}

namespace {

// Hands port 1 (the menus) to player 2 or back; see netplay.hpp. Runs on the agreed pads and the
// game's state as the tick starts, the same in both copies.
// ---- coop-localmenu: commands (netplay_cmd.hpp) ----------------------------------------------------------------------------
std::vector<NetCommand> g_command_queue;           // this copy's own player's, not yet on a frame
NetCommandHandler       g_command_handlers[256] = {};

// coop-stabfix1 (soak seed 1342, join: OUT OF SYNC at 660 on random and stages): the sender of a catch-up records a command
// of its own on g_link.tick, which SoloTick has already moved on to the next tick as the game's step runs, and the joiner's
// replay applies it as that next tick starts (SoloTick, before its step). Applied at once here, it took effect in the step
// before: his local Georama parts menu, shut by his Circle mid catch-up (CoopP1PartsMenuStep: the close applied in his own
// step returns 1 there), closed the town's menu one tick sooner on his copy than on hers, and the close's drawing (random
// draws a tick, from the town's mode 4 on) ran a tick apart; his next Triangle cut his run a draw longer: random 48bba50c
// against 12205ac8. A recorded command now waits, on the sender too, for the start of the tick it is recorded on, as it
// does on the replay and as a linked command waits for its frame. DC_NETPLAY_MUTATE=recorded_command_at_once (twin): at once.
struct RecordedCommand {
    int          player;
    NetCommand   command;
    std::int64_t tick;
};
std::vector<RecordedCommand> g_recorded_commands;

NetCommand TakeQueuedCommand() {
    if (g_command_queue.empty()) {
        return NetCommand{};
    }

    NetCommand command = g_command_queue.front();

    g_command_queue.erase(g_command_queue.begin());
    return command;
}

void ApplyCommand(int player, const NetCommand &command, std::int64_t tick) {
    if (command.kind == kCmdNone) {
        return;
    }

    std::fprintf(stderr, "netplay: command %d (a %d b %d c %u) from player %d applied at tick %lld\n", (int) command.kind, (int) command.a,
                 (int) command.b, (unsigned) command.c, player, (long long) tick);

    if (NetCommandHandler handler = g_command_handlers[command.kind]; handler != nullptr) {
        handler(player, command, tick);
    }
}

void Route() {
    TestMenuPresses();

    // Start alone: pad 2's Triangle is the partner's own item panel (coop_combat.cpp), which does not
    // pause. Player 1's Triangle, which opens the game's item menu, keeps player 2 from taking the port
    // on that tick.
    // coop-menus: the overlays' test twin (DC_COOP_MUTATE=menuclaim) makes Triangle a claim again. With the
    // overlay off (game.coop_menu_panel) her Triangle is a claim too: it opens her game menu beside the world,
    // her Start the pause. DC_COOP_MUTATE=triangleclaim (menu-triangle's twin): Start only.
    // coop-pause: with each player's own pause (coop::PauseOn) her Start is her pause, no claim: Triangle alone.
    const bool              triangle_claims = coop::Mutated("menuclaim") || (!coop::MenuPanelOn() && !coop::Mutated("triangleclaim"));
    const std::uint16_t     kClaimButtons = !triangle_claims                                 ? kInputStart
                                            : coop::PauseOn() && !coop::Mutated("menuclaim") ? kInputTriangle
                                                                                             : kInputStart | kInputTriangle;
    constexpr std::uint16_t kMenuButtons = kInputStart | kInputTriangle;

    // coop-arrivalfull: the arriver's pad, while its arrival plays again (coop::ArrivalWalkPlayer), is neutral on both copies.
    if (const int walker = coop::ArrivalWalkPlayer(); walker == 1 || walker == 2) {
        g_link.synced[walker - 1] = Neutral();
    }
    PadProbe(); // coop-padswap test hook
    MesPresses(); // coop-closetitem test hook
    DransCheatPads(); // coop-dranscheat
    const InputPadState    &one = g_link.synced[0];
    const InputPadState    &two = g_link.synced[1];
    std::uint16_t           pressed_one = one.buttons & ~g_link.previous[0].buttons;
    std::uint16_t           pressed_two = two.buttons & ~g_link.previous[1].buttons;
    float                   partner[4];
    bool                    free_play = gameTask == GAME_TASK_PLAY && !coop::FullMenuLive() && CoopPartnerTarget(partner, nullptr, nullptr, nullptr);
    if (g_link.menu_owner == 1) {
        // coop-localmenu 3b: with local menus her Triangle opens her own menu on her copy (coop_fullmenu.cpp), no claim of port 1.
        // coop-menulocal: her Start too with game.coop_menu_panel on (the menu's button then; FullMenuLocalButton). Her Start
        // claimed port 1 beside his local menu (FullMenuLive is false for local menus): her pad walked the world under his menu,
        // the game's menu opened as a second one and the copies parted. DC_NETPLAY_MUTATE=claim_beside_local: Start claims, as
        // before (the twin).
        const std::uint16_t local_button = NetplayMutated("claim_beside_local") ? kInputTriangle : coop::FullMenuLocalButton();
        const std::uint16_t claims = kClaimButtons & (coop::FullMenuLocalOn() ? (std::uint16_t) ~local_button : (std::uint16_t) 0xFFFF);

        if (free_play && (pressed_two & claims) != 0 && (pressed_one & kMenuButtons) == 0 && !NetplayMutated("menu_claim")) {
            g_link.menu_owner = 2;
            g_link.menu_tick = g_link.tick;
            g_link.menu_seen = false;
            g_link.held = two.buttons & ~kClaimButtons;
            std::fprintf(stderr, "netplay: player 2 takes the menu at tick %lld\n", (long long) g_link.tick);
        }
    } else if (coop::FullMenuLive() && !NetplayMutated("fullmenu_owner")) {
        // coop-fullmenu: her game menu is open beside the running world (the task is play): still hers.
        g_link.menu_seen = true;
    } else if (gameTask == GAME_TASK_PAUSE || (gameTask >= GAME_TASK_MENU_OPEN && gameTask <= GAME_TASK_MENU_CLOSE)) {
        g_link.menu_seen = true;
    } else if (gameTask != GAME_TASK_PLAY || g_link.menu_seen || g_link.tick > g_link.menu_tick + 2) {
        // Back on the floor; or the game never opened the menu (Triangle while moving), or something
        // else started on the same tick (an event, a chest), which player 1 answers.
        g_link.menu_owner = 1;
        g_link.held = one.buttons;
        std::fprintf(stderr, "netplay: menu back to player 1 at tick %lld\n", (long long) g_link.tick);
    }

    const InputPadState &owner = g_link.menu_owner == 1 ? one : two;

    g_link.held &= owner.buttons;
    g_link.routed[0] = owner;
    g_game_pad_player = g_link.menu_owner == 1 ? 1 : 2; // coop-scriptmenus
    g_link.routed[0].buttons &= ~g_link.held;
    if (g_link.menu_owner == 1) {
        g_link.routed[0].buttons &= ~coop::PlayerWheelMask(); // coop-wheelcrash: his ring's buttons are the ring's
    }
    // coop-fullmenu: her pad reaches the world while she has no menu of her own (the first menu shut while his second
    // stays open gives it back; her second menu beside his first keeps it).
    g_link.routed[1] = (g_link.menu_owner == 1 || coop::FullMenuFirstClosed()) && !coop::FullMenuRunsFor(2) ? two : Neutral();
    // coop-sceneowner: her floor's opening scene, joined, is hers to the end: the game's pad (port 1, the scene's text and its
    // player) is hers, and his reaches nothing while he watches (there is no partner of his on the floor yet).
    // (The handover is in the state set: the same on both copies, a catch-up's replay included.)
    // (coop-stage4 4e part 2: so too her own talk or event carried through her merge, till its swap)
    if ((CoopWorldsSceneOwner() == 2 || CoopTownSwapDeferred()) && g_link.menu_owner == 1) {
        g_link.routed[0] = two;
        g_game_pad_player = 2; // coop-scriptmenus
        g_link.routed[1] = Neutral();
        // (test hook DC_COOP_TEST_DEFER_CROSS, both copies: her Cross reads her carried talk through)
        static const bool defer_cross = TestEnvOn("DC_COOP_TEST_DEFER_CROSS");
        if (defer_cross && CoopTownSwapDeferred() && !CoopWorldsApart() && g_link.tick % 30 < 4) { // (read through once together: apart it would only start her talk again)
            g_link.routed[0].buttons |= kInputCross;
        }
    }
    // coop-localmenu stage 3a: a player in a menu of their own copy (the bit, the same on every copy) reaches the world with
    // nothing; the menu's engine reads their agreed pad itself.
    for (int p = 0; p < 2; p++) {
        // (coop-stage4: twins DC_COOP_MUTATE=townmenu_walks / p1townmenu_walks, both copies: her / his pad reaches the town under
        // the town menu, as before)
        // (coop-opt7: so too a player in a presentation of their own; its engine reads their agreed pad)
        if ((coop::PlayerPresenting(p + 1) || (coop::PlayerInMenu(p + 1, coop::kActorMenuLocal) && (p == 1 || g_link.menu_owner == 1) &&
            !(coop::PlayerInMenu(p + 1, coop::kActorMenuTown) && coop::Mutated(p == 1 ? "townmenu_walks" : "p1townmenu_walks"))))) {
            g_link.routed[p] = Neutral();
            (p == 0 ? pressed_one : pressed_two) = 0;
        }
    }
    NetplayTestPad(g_link.tick, g_link.routed[0]);
    g_player_pressed[0] = pressed_one; // coop-atlacross
    g_player_pressed[1] = pressed_two;
    g_link.previous[0] = one;
    g_link.previous[1] = two;

    if (g_link.desync_tick < 0 && g_link.tick >= g_link.resynced_status_until) {
        std::snprintf(g_link.status, sizeof g_link.status,
                      g_link.menu_owner == 1 ? "Linked: player %d" : "Linked: player %d (player 2 has the menu)", g_link.player);
    }
}

// DC_NETPLAY_LATENCY=1: log each press's way from the pad to the game ("netplay-latency: ...").
bool Latency() {
    static const bool on = TestEnvOn("DC_NETPLAY_LATENCY");

    return on;
}

// The input delay follows the round trip: an input must cross to the other copy (half the round
// trip) before its tick, with two ticks to spare. It grows a tick at a time, each tick it wants more:
// a tick that grows gives two frames an input, so each input still goes out in four of the packets'
// last eight (it grew by six, then by three on the next tick, which left the frames of the first
// growth in one packet only: that one lost at link start, both copies waited for each other until the
// link went down, net-lag-300ms-5pct, 2026-10-20). It shrinks a tick after a second of wanting less,
// or after a quarter second while it is more than four ticks over (a shrink only skips reading the pad
// once). The stale_rtt mutation keeps the old five seconds, fast_growth the old six ticks at once.
void AdaptDelay() {
    // coop-ws5left (train 53's warm red of load-input-drift): a test's RTT spike (DC_NETPLAY_TEST_RTT_SPIKE) lands on its
    // ticks whether or not a round trip was read yet: with none, it is the spike alone (127.0.0.1's own round trip is
    // under a ms). Under load the fixture load's quiet held the first sample past the spike's ticks and the delay never
    // moved. DC_NETPLAY_MUTATE=spike_waits_average (load-input-drift-lateavg~twin): no delay change before a sample, as
    // before. Players set no spike: unchanged for them.
    const bool spike_alone = g_link.rtt_avg_ms < 0.0 && RttSpiked() && !NetplayMutated("spike_waits_average");

    if (!g_link.adaptive || (g_link.rtt_avg_ms < 0.0 && !spike_alone)) {
        return;
    }

    double tick_ms = 1000.0 / dc::platform::ConfigGet().tick_rate;
    double rtt = (spike_alone ? 0.0 : g_link.rtt_avg_ms) + RttSpikeMs(g_link.tick);
    int    want = std::clamp((int) std::ceil(rtt / 2.0 / tick_ms) + 2, g_link.least_delay, 30);

    if (want > g_link.delay) {
        g_link.delay = std::min(want, g_link.delay + (NetplayMutated("fast_growth") ? 6 : 1));
        g_link.shrink_wait = 0;

        static const bool drop = TestEnvOn("DC_NETPLAY_DROP_GROWTH");

        if (drop && g_link.drop_tick < 0) {
            g_link.drop_tick = g_link.tick;
            std::fprintf(stderr, "netplay: test drop of tick %lld's packets (the first delay growth)\n", (long long) g_link.tick);
        }
        std::fprintf(stderr, "netplay: input delay %d ticks at tick %lld (round trip %.0f ms)\n", g_link.delay, (long long) g_link.tick,
                     g_link.rtt_avg_ms);
    } else if (want < g_link.delay && ++g_link.shrink_wait >= (NetplayMutated("stale_rtt") ? 300 : g_link.delay - want > 4 ? 15 : 60)) {
        g_link.delay--;
        g_link.shrink_wait = 0;
        std::fprintf(stderr, "netplay: input delay %d ticks at tick %lld (round trip %.0f ms)\n", g_link.delay, (long long) g_link.tick,
                     g_link.rtt_avg_ms);
    } else if (want >= g_link.delay) {
        g_link.shrink_wait = 0;
    }
}

// coop-loadcause (2026-11-04): a test's input script reaches the game on the frames a quiet PC gives it. The pad is read
// once a tick, for frame tick + delay, and the delay follows the round trip, which a busy PC sends up (a copy stopped
// 300 ms in a tick reads as a 50 ms round trip on 127.0.0.1). Each growth moved every later press a frame later (the
// frames it passes over keep the last input) and each shrink a frame earlier, its tick reading nothing (a one-frame
// press there was lost). x-p2-lock-cam's floor comes from the Cross at its floor select: delay 3 built the floor its
// check passes on, 4 another, and which one a run got followed the PC's load (train 51 land 8 and 9). So with a script
// installed, frame at gets the script's input from the frame it holds at the link's first delay, read for each frame
// given (two on a growth); on a quiet PC that is the frame the pump applied, as before. Players have no script.
// DC_NETPLAY_MUTATE=script_follows_reads (load-input-drift's twin): the reads as before.
bool ScriptReads() {
    return g_script_frame >= 0 && InputScriptActive() && !NetplayMutated("script_follows_reads");
}

// DC_NETPLAY_TEST_PRESS_FRAMES=1 (load-input-drift): each button this copy's pad starts to press, with the frame it is
// given to; a script's presses keep their spacing in frames whatever the delay did.
// coop-fx51a (coordinator 2026-11-05, b2: what a fixture load leaves behind): DC_QA_GLOBALS_DUMP=<regions>|<frames>|<dir> hashes
// each global <regions> lists (debug\build\globals-outside.py: the game's writable globals outside the state set) <frames>
// frames after each fixture load or save and writes "<name> <hash> [<bytes, for one of 4 KB or less>]" lines to
// <dir>\globals-p<player>-<n>.txt, n counting
// them (x-fixture-fidelity -Globals: the maker pair at its save + frames against the loader at its load + frames).
// warm-repeat-selftest's globals case diffs them by name: a fresh pair's load against a warm pair's after another row.
namespace {
struct GlobalsDump {
    struct Region {
        std::uintptr_t offset;
        std::size_t    size;
        std::string    name;
    };
    std::vector<Region> regions;
    std::string         dir;
    int                 frames = 0;
    int                 loads = 0;
    long long           due = -1;
    bool                read = false;
};
GlobalsDump g_globals;

void GlobalsDumpRead() {
    if (g_globals.read) {
        return;
    }
    g_globals.read = true;
    const char *spec = TestEnv("DC_QA_GLOBALS_DUMP");

    if (spec == nullptr) {
        return;
    }
    const std::string s = spec;
    const std::size_t a = s.find('|'), b = a == std::string::npos ? a : s.find('|', a + 1);

    if (b == std::string::npos) {
        std::fprintf(stderr, "qa-globals: DC_QA_GLOBALS_DUMP is <regions>|<frames>|<dir>, not %s\n", spec);
        return;
    }
    g_globals.frames = std::atoi(s.substr(a + 1, b - a - 1).c_str());
    g_globals.dir = s.substr(b + 1);
    std::ifstream in(PathsFromUtf8(s.substr(0, a).c_str()));
    std::string   line;

    while (std::getline(in, line)) {
        unsigned long long offset = 0, size = 0;
        char               name[512] = {};

        if (line.empty() || line[0] == '#' || std::sscanf(line.c_str(), "%llx %llx %511s", &offset, &size, name) != 3) {
            continue;
        }
        g_globals.regions.push_back({(std::uintptr_t) offset, (std::size_t) size, name});
    }
    std::fprintf(stderr, "qa-globals: %zu globals to hash %d frames after each load\n", g_globals.regions.size(), g_globals.frames);
}

#ifdef _WIN32
extern "C" char __ImageBase; // (lld's, at the exe's base: the region file's offsets are from it)
#endif

void GlobalsDumpWrite() {
#ifdef _WIN32
    const auto        base = (std::uintptr_t) &__ImageBase;
#else
    const std::uintptr_t base = 0; // (the Linux port has no region file: snapregions is the Windows link map's)
    if (base == 0) {
        return;
    }
#endif
    const std::string path = g_globals.dir + "/globals-p" + std::to_string(g_link.player) + "-" + std::to_string(g_globals.loads) + ".txt";
    std::ofstream  out(PathsFromUtf8(path.c_str()));

    for (const auto &r : g_globals.regions) {
        std::uint64_t hash = 1469598103934665603ull;
        const auto   *at = (const unsigned char *) (base + r.offset);

        for (std::size_t i = 0; i < r.size; i++) {
            hash = (hash ^ at[i]) * 1099511628211ull;
        }
        out << r.name << ' ' << std::hex << hash;
        // (a global of 4 KB or fewer also as its bytes, so a difference is read field by field against its type)
        if (r.size <= 4096) {
            out << ' ';
            for (std::size_t i = 0; i < r.size; i++) {
                static const char kHex[] = "0123456789abcdef";
                out << kHex[at[i] >> 4] << kHex[at[i] & 15];
            }
        }
        out << std::dec << '\n';
    }
    std::fprintf(stderr, "qa-globals: load %d hashed %zu globals at frame %lld to %s\n", g_globals.loads, g_globals.regions.size(),
                 (long long) dc::app::GameFrameCount(), path.c_str());
}
} // namespace

void GlobalsDumpAfterLoad() {
    GlobalsDumpRead();
    if (g_globals.regions.empty()) {
        return;
    }
    g_globals.loads++;
    g_globals.due = (long long) dc::app::GameFrameCount() + g_globals.frames;
}

// coop-fx51a (d-solo-town -Fixture, coordinator 2026-11-05: its twin's missing town pack must break the row past the load):
// DC_COOP_TEST_TOWN_AGAIN=<map>:<file>: once a state file is loaded and <file> exists, this copy goes to town <map> again
// (NetplayGoToTown, as a serve job's go-there-first does), its textures (gedit/system/esys_cmn.pak) and message files read
// anew. Once. The test writes <file> when it is ready (its twin first takes its own copy of the pack away).
void TownAgainFrame() {
    static const char *spec = TestEnv("DC_COOP_TEST_TOWN_AGAIN");
    static bool        done = false;
    static int         poll = 0;

    if (spec == nullptr || done || !NetplayStateFileLoaded() || ++poll % 30 != 0) {
        return;
    }
    const char *colon = std::strchr(spec, ':');

    if (colon == nullptr || !std::ifstream(PathsFromUtf8(colon + 1)).good()) {
        return;
    }
    done = true;
    const int map = std::atoi(spec);

    std::fprintf(stderr, "coop-test: town %d again after the state file (DC_COOP_TEST_TOWN_AGAIN)\n", map);
    NetplayGoToTown(map);
}

void GlobalsDumpFrame() {
    if (g_globals.due >= 0 && (long long) dc::app::GameFrameCount() >= g_globals.due) {
        g_globals.due = -1;
        GlobalsDumpWrite();
    }
}

// coop-fx51a (coordinator 2026-11-05, shortest in ticks: a run's waste is read from its copies' logs, lib\Waste.ps1):
// DC_QA_TICK_MARK=1 stamps every line with the copy's game frame (coop.hpp TestLogFrame) and says each scripted press and
// stick move with the frame it applies on.
bool QaTickMark() {
    COOP_TEST_SWITCH(bool, on, TestEnvOn("DC_QA_TICK_MARK"));
    return on;
}

void ScriptPressTrace(const InputPadState &pad, std::int64_t at, std::int64_t tick) {
    static const bool    on = TestEnvOn("DC_NETPLAY_TEST_PRESS_FRAMES");
    static std::uint16_t last = 0;
    static std::uint32_t last_stick = 0x80808080u;

    if (!on && !QaTickMark()) {
        return;
    }
    if ((pad.buttons & ~last) != 0) {
        std::fprintf(stderr, "netplay: test: press %04x on frame %lld (read at tick %lld, delay %d)\n", pad.buttons & ~last,
                     (long long) at, (long long) tick, g_link.delay);
    }
    last = pad.buttons;
    const std::uint32_t stick = (std::uint32_t) pad.left_x << 24 | (std::uint32_t) pad.left_y << 16 | (std::uint32_t) pad.right_x << 8 | pad.right_y;

    if (stick != last_stick && QaTickMark()) {
        std::fprintf(stderr, "netplay: test: stick %08x on frame %lld (read at tick %lld, delay %d)\n", stick, (long long) at,
                     (long long) tick, g_link.delay);
    }
    last_stick = stick;
}

InputPadState ScriptInputFor(std::int64_t at, std::int64_t tick) {
    // (never past an anchor not yet happened: the lines after it wait for it, coop-anchors)
    InputScriptApply(std::min(g_script_frame + (at - tick) - g_link.script_delay, CoopInputAnchorsLimit()));

    InputPadState input = OwnInput(g_link.player);

    input.connected = true;
    return input;
}

// A trace of each copy's inputs and state checksums, with DC_COOP_NETTRACE set, to diff when they part.
void Trace(const InputPadState &one, const InputPadState &two, std::uint32_t sum) {
    static const bool trace = TestEnv("DC_COOP_NETTRACE") != nullptr;

    if (trace) {
        float angle = NowCamera__3 != nullptr ? NowCamera__3->angle : 0.0f;
        float next = NowCamera__3 != nullptr ? NowCamera__3->next_angle : 0.0f;

        // (coop-fx51a: "here", the floor this copy's worlds take it to be on (CoopWorldsHereFloor), outside the checksum: the
        // fixture fidelity self-test compares it between the copy that saved a state and one that loaded it)
        std::fprintf(stderr, "nettrace %lld p1 %04x %02x%02x p2 %04x %02x%02x state %08x task %d mode %d camera %.4f>%.4f random %08x here %d\n",
                     (long long) g_link.tick, one.buttons, one.left_x, one.left_y, two.buttons, two.left_x, two.left_y, sum, (int) gameTask,
                     (int) mode, angle, next, PortRandState(), CoopWorldsHereFloor());
    }
}

// DC_NETPLAY_SAVE_AT=N (the host): on tick N the host writes save 0 as the save screen does. DC_NETPLAY_LOAD_AT=M (the
// host): on tick M the host calls a resync whose state is save 0 read back, so both copies go on from it. Test save
// folders only: these are test variables, which the launchers never set.
// coop-fx51a (Jep, 2026-11-04: no fixed tick waits): DC_NETPLAY_SAVE_AT=loaded: the save on the first link tick after a
// state fixture was put back (StateFileApply), so a test that starts on a fixture saves at its moment, not at a tick count.
std::int64_t g_state_file_applied = -1; // the link tick a state file was last put back on (-1: none)

void SaveLoadHooks(std::int64_t tick) {
    static const char *const  save_spec = TestEnv("DC_NETPLAY_SAVE_AT");
    static const bool         save_on_load = save_spec != nullptr && std::strcmp(save_spec, "loaded") == 0;
    static const std::int64_t save_at = save_on_load ? -1 : TestEnvInt("DC_NETPLAY_SAVE_AT", -1);
    // coop-fx51b (no fixed tick waits): DC_NETPLAY_LOAD_AT=saved: the load once both copies compared in sync after the save
    // and the save data has moved on from it (its checksum no longer the save's), so what the load brings back differs from
    // what both copies have (a joiner that kept its own would show it).
    static const char *const  load_spec = TestEnv("DC_NETPLAY_LOAD_AT");
    static const bool         load_on_save = load_spec != nullptr && std::strcmp(load_spec, "saved") == 0;
    static const std::int64_t load_at = load_on_save ? -1 : TestEnvInt("DC_NETPLAY_LOAD_AT", -1);
    static bool               saved_on_load = false;
    static std::int64_t       checked_at_save = -1; // g_link.checked as the save was written (-1: no save yet)
    static std::uint32_t      saved_sum = 0;        // the save's checksum as written

    if (g_link.player != 1 || SaveData == nullptr) {
        return;
    }

    const bool load_save = save_on_load && !saved_on_load && g_state_file_applied >= 0 && tick > g_state_file_applied;

    if (load_save) {
        saved_on_load = true;
        std::fprintf(stderr, "netplay: test: the fixture was put back at tick %lld: saving on tick %lld\n", (long long) g_state_file_applied,
                     (long long) tick);
    }
    if (tick == save_at || load_save) {
        std::vector<unsigned char> written;
        bool                       saved = SaveSlotWriteNumbered(0, &written);

        // The checksum of the save as written, which a load gives back.
        std::fprintf(stderr, saved ? "netplay: saved at tick %lld to slot 0 (checksum %08x)\n" : "netplay: could not save at tick %lld (checksum %08x)\n",
                     (long long) tick, saved ? SaveSum(written.data()) : SaveSum());
        if (saved) {
            checked_at_save = g_link.checked;
            saved_sum = SaveSum(written.data());
        }
    }

    const bool load_saved = load_on_save && checked_at_save >= 0 && g_link.checked > checked_at_save && SaveSum() != saved_sum;

    // The load itself waits for the resync's state (TakeState), so what both copies take is the save as written.
    if (load_saved) {
        checked_at_save = -1; // (once)
        std::fprintf(stderr, "netplay: test: in sync since the save and its data moved on (checksum %08x, the save's %08x): loading on tick %lld\n",
                     SaveSum(), saved_sum, (long long) tick);
    }
    if (tick == load_at || load_saved) {
        g_link.loaded_tick = tick;
        ResyncCause("test: the host loaded slot 0 (DC_NETPLAY_LOAD_AT)", "SaveLoadTick");
        g_link.resync_tick = tick + g_link.delay + 30;
        std::fprintf(stderr, "netplay: the host loaded slot 0 at tick %lld; both take it at tick %lld\n", (long long) tick,
                     (long long) g_link.resync_tick);
    }
}

bool Catchup() {
    return NetplayCatchupSetting() && SendsState(); // coop-localfix2: the merge's source (the host but for a stayer's merge)
}

// DC_NETPLAY_RECORD=<file> (tests): the checkpoint, the pads and the checksums so far, rewritten each second.
void WriteRecording() {
    static const char *path = TestEnv("DC_NETPLAY_RECORD");

    if (path == nullptr || g_solo.replay) {
        return;
    }

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    auto          put = [&](const void *data, std::size_t size) { out.write((const char *) data, (std::streamsize) size); };
    std::uint32_t sizes[3] = {(std::uint32_t) g_solo.checkpoint.size(), (std::uint32_t) g_solo.pads.size(), (std::uint32_t) g_solo.sums.size()};

    put(sizes, sizeof sizes);
    put(g_solo.checkpoint.data(), g_solo.checkpoint.size());
    put(g_solo.pads.data(), g_solo.pads.size() * sizeof(InputPadState));
    put(g_solo.sum_ticks.data(), g_solo.sums.size() * sizeof(std::int64_t));
    put(g_solo.sums.data(), g_solo.sums.size() * sizeof(std::uint32_t));
}

bool ReadRecording(const char *path) {
    std::ifstream in(path, std::ios::binary);
    auto          get = [&](void *data, std::size_t size) { return (bool) in.read((char *) data, (std::streamsize) size); };
    std::uint32_t sizes[3] = {};

    if (!get(sizes, sizeof sizes) || sizes[0] == 0) {
        return false;
    }

    g_solo.checkpoint.resize(sizes[0]);
    g_solo.pads.resize(sizes[1]);
    g_solo.sum_ticks.resize(sizes[2]);
    g_solo.sums.resize(sizes[2]);
    return get(g_solo.checkpoint.data(), sizes[0]) && get(g_solo.pads.data(), sizes[1] * sizeof(InputPadState)) &&
           get(g_solo.sum_ticks.data(), sizes[2] * sizeof(std::int64_t)) && get(g_solo.sums.data(), sizes[2] * sizeof(std::uint32_t));
}

// The host alone, armed for Option B: not linked, listening, not a replaying copy.
// Before the link starts (the title, the opening) the host is the copy set as player 1: its arrival in its first town or
// dungeon from the title (Continue, a New Game's opening) is its first checkpoint, so a join soon after needs no reload.
bool CatchupArmed() {
    static const bool replaying = TestEnv("DC_NETPLAY_REPLAY") != nullptr;
    const bool        on = dc::platform::ConfigGet().netplay_catchup || TestEnvOn("DC_NETPLAY_CATCHUP");

    if (!g_link.started) {
        return on && !replaying && Settings().netplay == 1 && !NetplayMutated("no_first_checkpoint");
    }

    // coop-snapshot: a copy just gone apart (coop-worlds: it took a floor's gate alone) is not listening again until its
    // new place, yet the floor it lands on must be a checkpoint, so player 2 can join him there (the parked
    // coop-dungeonp2-follow found this). DC_NETPLAY_MUTATE=no_apart_checkpoint (twin) keeps the old rule.
    const bool apart = CoopWorldsApart() && !NetplayMutated("no_apart_checkpoint");

    return Catchup() && !replaying && !g_link.up && (g_link.listening || apart) && !g_link.dead;
}

// The host, as one of its modes ends for a town or a dungeon on its own (NetplayModeEnded): its state now and where the
// mode was going is the checkpoint, which it takes itself as well (the resets a copy taking it gets), all inside the
// loading screen the change of place shows anyway. Recording starts over from the first tick in the new place.
// coop-snapshot: floor >= 0 is the floor-start checkpoint, taken as a floor's "go dungeon" puts the party on floor
// `floor` of the dungeon it is in (NetplayFloorStarted): the state now, going to this dungeon's entrance (leave_map) and
// from there straight onto the floor (ResyncHeader::floor, GameTakeJumpFloor). The mode goes on, so the self-take keeps
// the main loop's map numbers as they are. Recording starts over from the floor's first tick.
// coop-housemerge: a building's interior set up on this copy (NetplayInteriorStarted), not yet taken as a catch-up's tick 0:
// the host as it took its building-start checkpoint, a joiner as it took that checkpoint's state again inside.
bool g_building_started = false;

// coop-housemerge: building is the building-start checkpoint, taken as the host alone inside a town's building has its
// interior set up (NetplayInteriorStarted): the state now, going to this town (leave_map, no event) and from there in by
// the door it took (ResyncHeader::building). The mode goes on, as for a floor's.
// coop-bldgsnap: place is a place checkpoint, taken as a link starts in a town or a building with no recording on this
// copy (PlaceCheckpoint): where the host stands now (the town, and the building it is in if any), for the joiner to land
// in before the host's state set goes over hers. No self-take (the snapshot carries the state; the host plays on as it is).
void Checkpoint(int floor, bool building, bool place) {
    // Where the mode is going: NextMapNo, or for the title's Continue (which sets the town's mode itself) the town.
    int next = floor >= 0 ? (mode == GAME_MODE_DUNGEON && selectMapNo >= 0 && selectMapNo <= 6 ? selectMapNo + 200 : -1)
               : building || place ? (mode == GAME_MODE_EDIT && MapNo >= 0 && MapNo < 200 ? MapNo : -1)
               : NextMapNo >= 0 ? NextMapNo : mode == GAME_MODE_EDIT && MapNo >= 0 && MapNo < 200 ? MapNo : -1;
    const bool goes_on = floor >= 0 || building || place; // the mode plays on: the self-take keeps the main loop's map numbers

    if (next < 0 || next >= 300) {
        return;
    }

    std::vector<unsigned char> state = TakeState(false); // coop-mergestand: no stands in a checkpoint
    ResyncHeader               header;

    std::memcpy(&header, state.data(), sizeof header);
    header.has_next = 1;
    header.next_map_no = goes_on ? next : NextMapNo;
    header.start_event_no = goes_on ? -1 : StartEventNo;
    header.next_mode = mode;
    header.old_map_no = OldMapNo;
    header.leave_map = next;
    header.floor = floor;
    header.floor_rand = floor >= 0 ? g_floor_build_rand : 0;

    if (building || (place && EdInteriorFlag != 0)) {
        header.building = CoopWorldsInteriorId(EdInteriorName);
        std::snprintf(header.building_name, sizeof header.building_name, "%s", EdInteriorName);
        header.building_entrance = EdInteriorPartsNo;
        header.building_jump = EdInteriorJumpID;
        header.building_start_event = EdInteriorStartEvent;
        header.building_time = NowTime;
    }
    std::memcpy(state.data(), &header, sizeof header);

    if (!place && !NetplayMutated("own_checkpoint")) {
        const int next_map_no = NextMapNo, start_event_no = StartEventNo, old_map_no = OldMapNo, at_mode = mode;

        TakeState(state, true);

        if (goes_on) {
            // The floor (the building) plays on: the jump a copy taking the state makes is not this copy's.
            NextMapNo = next_map_no;
            StartEventNo = start_event_no;
            OldMapNo = old_map_no;
            mode = at_mode;
            g_link.restart_floor = -1;
        }
    }

    g_solo = Solo{};
    g_solo.checkpoint = std::move(state);
    g_solo.due = true;
    g_solo.place = place;

    if (place) {
        std::fprintf(stderr, "catchup: place checkpoint in map %d%s%s (no recording: the join goes by snapshot from here, %zu bytes)\n", (int) header.map_no,
                     header.building > 0 ? ", inside building " : "", header.building > 0 ? header.building_name : "", g_solo.checkpoint.size());
    } else if (floor >= 0) {
        std::fprintf(stderr, "catchup: checkpoint at the start of floor %d in map %d (for map %d, %zu bytes)\n", floor + 1, (int) header.map_no,
                     (int) header.leave_map, g_solo.checkpoint.size());
    } else if (building) {
        g_building_started = true; // this copy is where the catch-up's tick 0 is
        std::fprintf(stderr, "catchup: checkpoint at the start of building %s in map %d (entrance %d, jump %d, event %d, %zu bytes)\n",
                     header.building_name, (int) header.map_no, (int) header.building_entrance, (int) header.building_jump,
                     (int) header.building_start_event, g_solo.checkpoint.size());
    } else {
        std::fprintf(stderr, "catchup: checkpoint as map %d ends, for map %d (event %d, %zu bytes)\n", (int) header.map_no, (int) header.leave_map,
                     (int) header.start_event_no, g_solo.checkpoint.size());
    }
}

// coop-snapshot: the floor the checkpoint was taken at the start of (ResyncHeader::floor), -1 for a mode end's.
int CheckpointFloor(const std::vector<unsigned char> &state) {
    ResyncHeader header;

    if (state.size() < sizeof header) {
        return -1;
    }

    std::memcpy(&header, state.data(), sizeof header);
    return header.floor;
}

// coop-floorsnap: the random state the checkpoint's floor was built from (ResyncHeader::floor_rand), 0 for none.
std::uint32_t CheckpointFloorRand(const std::vector<unsigned char> &state) {
    ResyncHeader header;

    if (state.size() < sizeof header) {
        return 0;
    }

    std::memcpy(&header, state.data(), sizeof header);
    return header.floor_rand;
}

// coop-housemerge: the building (its InteriorId) a building-start checkpoint was taken in, 0 for any other.
int CheckpointBuilding(const std::vector<unsigned char> &state) {
    ResyncHeader header;

    if (state.size() < sizeof header) {
        return 0;
    }

    std::memcpy(&header, state.data(), sizeof header);
    return header.building;
}

// coop-snapshot: a floor started on this copy ("go dungeon": the party is on the floor, under control), not yet taken as
// a catch-up's tick 0.
bool g_floor_started;

// Whether this copy is where the checkpoint's catch-up starts (tick 0): in the checkpoint's town or dungeon, and for a
// floor-start checkpoint on its floor as it started (the host as it took it; a copy that took the state once its
// entrance has put it on that floor).
bool AtCheckpointPlace() {
    if (RestartTarget(mode, MapNo, selectMapNo) < 0) {
        return false;
    }

    const int floor = CheckpointFloor(g_solo.checkpoint);

    // coop-housemerge: a building-start checkpoint's tick 0 is the interior's start (the town's load is not it).
    if (CheckpointBuilding(g_solo.checkpoint) > 0) {
        return mode == GAME_MODE_EDIT && g_building_started && EdInteriorFlag != 0;
    }

    if (floor < 0) {
        return true;
    }

    return mode == GAME_MODE_DUNGEON && g_floor_started && UserStatus != nullptr && UserStatus->cur_floor == floor;
}

// coop-snapjoin: whether this copy, before it leaves for the checkpoint, is in the checkpoint's zone already, under control:
// on its floor of its dungeon, in its building, or outdoors in its town. Only for a snapshot join (the apply puts the
// host's whole state over this copy's, so how this copy came here does not matter).
bool InCheckpointZone() {
    ResyncHeader header;

    // coop-localfix (path block 2026-10-26: P1 knocked out alone on his floor as she arrived, her copy reloaded hers): on a
    // floor, a knocked-out player's change screen is under control too: the host's state set lands over it.
    // DC_NETPLAY_MUTATE=floor_place_reload (twin): play only, as before.
    const bool knocked_out = mode == GAME_MODE_DUNGEON && (gameTask == GAME_TASK_DEAD || gameTask == GAME_TASK_DEAD_CHANGE_CHARA || gameTask == GAME_TASK_CHARA_SELECT_LOOP) &&
                             !NetplayMutated("floor_place_reload");

    // coop-cutscenejoin: and her own arrival on his floor, under way (CoopWorldsPickJoining): his state lands over it.
    // coop-b1lesson: or her floor's start held for his opening scene of it, which she joins (CoopWorldsSceneJoining).
    const bool scene_join = mode == GAME_MODE_DUNGEON && CoopWorldsSceneJoining() == 2;
    const bool arriving = mode == GAME_MODE_DUNGEON && (CoopWorldsPickJoining() || scene_join);

    // coop-houseblack (Jep's 2026-11-04 pack 8, second try: he back in town by Circle at the floor menu, she in town, his
    // copy "in the checkpoint's place, not walking yet" for good): in a town gameTask is the dungeon's leftover, as in
    // SnapshotQuietTick. DC_NETPLAY_MUTATE=town_quiet_task (twin): it counts here too, as before.
    static const bool town_task_counts = NetplayMutated("town_quiet_task");
    const bool        town = mode == GAME_MODE_EDIT && !town_task_counts;

    if (g_solo.checkpoint.size() < sizeof header || (gameTask != GAME_TASK_PLAY && !town && !knocked_out && !arriving)) {
        return false;
    }
    std::memcpy(&header, g_solo.checkpoint.data(), sizeof header);
    // The place LeaveForState would leave for (a mode-end checkpoint's, or a floor start's: its next map, the dungeon) is
    // where this copy is.
    const int target = header.has_next ? header.leave_map : RestartTarget(header.mode, header.map_no, header.select_map_no);

    if (target < 0 || target != RestartTarget(mode, MapNo, selectMapNo)) {
        return false;
    }

    const int floor = CheckpointFloor(g_solo.checkpoint);
    const int building = CheckpointBuilding(g_solo.checkpoint);

    if (mode == GAME_MODE_DUNGEON) {
        // coop-b1lesson: his floor not started yet (its start comes after the scene), his checkpoint is the dungeon's: her
        // floor is his (built from his random state, CoopWorldsSceneJoining), and his state set goes over it here.
        if (floor < 0 && scene_join && !NetplayMutated("scene_join_reload")) {
            return true;
        }
        return floor >= 0 && UserStatus != nullptr && UserStatus->cur_floor == floor && (BtEventMode == 0 || knocked_out || arriving);
    }
    if (mode == GAME_MODE_EDIT) {
        return building > 0 ? EdInteriorFlag != 0 && CoopWorldsInteriorId(EdInteriorName) == building && CoopTownIndoorWalks()
                            : EdInteriorFlag == 0 && ::GameMode == ED_MODE_WALK;
    }
    return false;
}

// coop-bldgsnap: whether this copy is in the checkpoint's town, and in its building (or outdoors) as the checkpoint is,
// walking or not: InCheckpointZone's place without its "under control".
bool InCheckpointPlace() {
    ResyncHeader header;

    if (g_solo.checkpoint.size() < sizeof header || (mode != GAME_MODE_EDIT && mode != GAME_MODE_DUNGEON)) {
        return false;
    }
    std::memcpy(&header, g_solo.checkpoint.data(), sizeof header);
    const int target = header.has_next ? header.leave_map : RestartTarget(header.mode, header.map_no, header.select_map_no);
    const int building = CheckpointBuilding(g_solo.checkpoint);

    if (target < 0 || target != RestartTarget(mode, MapNo, selectMapNo)) {
        return false;
    }
    // coop-localfix (path block 2026-10-26: she was in an event on his floor as his merge came, and her copy reloaded the
    // floor, "not in its zone"): on the checkpoint's floor, not under control yet, she plays on there until she is, then
    // lands. DC_NETPLAY_MUTATE=floor_place_reload (twin): leaves for the checkpoint's map, as before.
    if (mode == GAME_MODE_DUNGEON) {
        const int floor = CheckpointFloor(g_solo.checkpoint);

        return floor >= 0 && UserStatus != nullptr && UserStatus->cur_floor == floor && !NetplayMutated("floor_place_reload");
    }
    // (Its door into the building opening counts, as for coop-worlds' zones: the merge links her there, outdoors still; and
    // the map jump that follows it, before the interior starts.)
    return building > 0 ? (EdInteriorFlag != 0 || ::GameMode == ED_MODE_DOOR_OPEN || ::GameMode == ED_MODE_MAP_JUMP) &&
                                 CoopWorldsInteriorId(EdInteriorName) == building
                        : EdInteriorFlag == 0;
}

// DC_NETPLAY_TEST_EXIT_AT=N (tests): N ticks after the host starts listening, on a dungeon floor, it takes the
// dungeon's way out (as its exit fade starts), a change of place that ends its mode.
void TestExit() {
    static const std::int64_t exit_at = TestEnvInt("DC_NETPLAY_TEST_EXIT_AT", -1);
    static std::int64_t       alone = 0;

    if (exit_at >= 0 && alone++ == exit_at) {
        if (mode == GAME_MODE_DUNGEON && gameTask == GAME_TASK_PLAY) {
            gameTask = GAME_TASK_EXIT_FADE_START;
            std::fprintf(stderr, "catchup: test: the host takes the dungeon's way out\n");
        } else {
            std::fprintf(stderr, "catchup: test: no way out (mode %d, task %d)\n", (int) mode, (int) gameTask);
        }
    }

    // coop-snapshot (tests): with DC_NETPLAY_NEXT_FLOOR=N the host alone takes the floor's gate N ticks after it starts
    // listening, as netplay_test's floor hook drives it linked: the next floor ends its mode, so it records there
    // (snaptrip.ps1 -Place floor).
    static const bool   next_floor = TestEnv("DC_NETPLAY_NEXT_FLOOR") != nullptr;
    static std::int64_t alone_floor = 0;

    if (next_floor) {
        NetplayTestFloorApart(alone_floor++);
    }
}

// coop-snapshot (tests): the round trip's checksum, Checksum() itself; with DC_SNAPSHOT_TRIP_PARTS=1 one line of what it
// holds each call (the take, each tick of both passes, the restore's check), so the two passes' lines at one tick name
// the part that reads state outside the snapshot's set.
std::uint32_t TripChecksum() {
    static const bool   parts = TestEnvOn("DC_SNAPSHOT_TRIP_PARTS");
    const std::uint32_t sum = Checksum();

    if (parts) {
        const Parts &p = g_parts;

        // The floor trip's one-tick parting in ref_off (2026-10-24): the move's inputs at the tick.
        std::fprintf(stderr, "trip-inputs tick %lld refoff (%.3f %.3f %.3f) lx %.3f pad on %04x down %04x routed %04x/%04x stick %d %d prev %04x\n",
                     (long long) g_link.tick, ref_off[0], ref_off[1], ref_off[2], inputH1, GamePad.GetPadOn(), GamePad.GetPadDown(),
                     g_link.synced[0].buttons, g_link.synced[1].buttons, (int) g_link.synced[0].left_x, (int) g_link.synced[0].left_y,
                     g_link.previous[0].buttons);

        std::fprintf(stderr,
                     "trip-parts tick %lld sum %08x rand %08x task %d cam %.4f cam2 %.4f view %08x player (%.3f %.3f %.3f) partner (%.3f %.3f %.3f) "
                     "monsters %08x hits %08x stages %08x %08x %08x status %08x owner %d pchara %d pdown %d pchoice %d geo2 %d bag %08x panel %d lock %d "
                     "menus %08x floor %08x shots %08x save %08x\n",
                     (long long) g_link.tick, sum, p.rand, p.task, p.camera, p.camera2, p.view, p.player[0], p.player[1], p.player[2], p.partner[0],
                     p.partner[1], p.partner[2], p.monsters, p.hits, p.stages[0], p.stages[1], p.stages[2], p.status, p.menu_owner, p.partner_chara,
                     p.partner_down, p.partner_choice, (int) p.georama_two, p.p2_bag, (int) p.combat_panel, p.combat_lock, p.menus, p.floor, p.shots,
                     p.save);
    }
    return sum;
}

// coop-p2absent: test hook (DC_NETPLAY_TICK_TRACE=N): one line a tick of what the checksum holds, so two copies' logs
// diff to the first tick they part. `kind` marks a catch-up's recorded or replayed tick (coop-p1follows).
static void TickTrace(std::int64_t tick, const char *kind) {
    Checksum();
    std::printf("netplay-tick %lld:%s rand %08x player (%.3f %.3f) partner (%.3f %.3f) monsters %08x status %08x floor %08x shots %08x hp %d next %d save %08x town mode %d clock %.4f view %08x swing %d\n",
                (long long) tick, kind, g_parts.rand, g_parts.player[0], g_parts.player[2], g_parts.partner[0], g_parts.partner[2], g_parts.monsters,
                g_parts.status, g_parts.floor, g_parts.shots, UserStatus != nullptr ? (int) UserStatus->hp[0] : -1,
                UserStatus != nullptr ? (int) UserStatus->next_hp[0] : -1, g_parts.save, mode == GAME_MODE_EDIT ? (int) ::GameMode : -1,
                SaveData != nullptr ? SaveData->GetNowTime() : -1.0f, // coop-stable1: the town's clock too (the post-resync desync)
                g_parts.view, // coop-repinfix1: the view (a resync's town event draws from its own camera on its first ticks)
                EdMoveCharaRotCount); // coop-repinfix1: the town walk's camera swing (test_leave_1031_town_swing reads it a tick)
    // What the player's hit check reads beside the hits (BtCheckDamageProc): recoil, invincibility, action, member.
    std::printf("netplay-tick %lld:   act recoil %d inv %d action %d chara %d task %d mode %d pads %04x %d %d / %04x %d %d\n", (long long) tick,
                (int) BtActStatus.recoil_frames, (int) BtActStatus.invincible_frames, (int) BtActStatus.action_on,
                UserStatus != nullptr ? (int) UserStatus->cur_chara : -1, (int) gameTask, (int) BtGameModeFlag, g_link.routed[0].buttons,
                g_link.routed[0].left_x, g_link.routed[0].left_y, g_link.routed[1].buttons, g_link.routed[1].left_x, g_link.routed[1].left_y);
    if (NowColData != nullptr) {
        for (int i = 0; i < 96; i++) {
            if (NowColData->active[i] != 0) {
                const COLLISION_HIT &hit = NowColData->hit[i];

                std::printf("netplay-tick %lld:   hit%d active %d (%.3f %.3f %.3f) dmg %d r %.2f life %d mask %d kind %d mon %d own %d atk %d phase %d\n",
                            (long long) tick, i, (int) NowColData->active[i], hit.pos[0], hit.pos[1], hit.pos[2], (int) hit.damage, hit.radius,
                            (int) hit.life, (int) hit.target_mask, (int) hit.kind, (int) hit.monster_no, (int) hit.owner, (int) hit.attack_no,
                            (int) hit.phase);
            }
        }
    }
    if (NowMonstorUnit != nullptr && BtAllDrawFlag != 0) {
        for (int i = 0; i < 16; i++) {
            const MONSTOR &monster = NowMonstorUnit->monster[i];
            float          position[4] = {};

            if (monster.state != 0) {
                NowMonstorUnit->chara[i][0].GetPosition(position);
                std::printf("netplay-tick %lld:   m%d state %d hp %d (%.3f %.3f) target %d\n", (long long) tick, i, (int) monster.state,
                            (int) monster.hp, position[0], position[2], CoopMonsterTarget(i));
            }
        }
    }
    std::fflush(stdout);
}

// The host alone with game.netplay_catchup (see Solo): a tick as the link would run it with no delay, pad 2 neutral.
// coop-houseexit test hook (DC_NETPLAY_TEST_TICK_DUMP=T[,T...]:PREFIX): on each of these link ticks, as it starts (a catch-up's
// recorded or replayed tick, or a lockstep's, whichever this copy plays it as), the writable image and arenas (snapdiff.py)
// to PREFIX-l<link>-p<player>-t<T>[-r<n>].bin, and her town camera's state on one line: the two copies' bytes at one tick, to
// diff, where a per-tick trace changed the timing the desync needs (the house-exit merge's OUT OF SYNC under load). Each link
// counts from 1 (a merge's link starts its ticks again: the first run dumped her title join only); DC_NETPLAY_TEST_TICK_DUMP_LINK=N
// dumps link N alone (each dump is ~350 MB). -r<n>: a file of that name already there (the row's rerun alone) is kept.
void TestTickDump(std::int64_t tick, const char *how) {
    // DC_NETPLAY_TEST_TICK_LINE=T[,T...]: her town line alone (no dump) on those ticks of every link, and at the state set's
    // take (tick -1 in the line): where her copy had her when it sent, against where each copy has her as the tick starts.
    if (static const char *lines = TestEnv("DC_NETPLAY_TEST_TICK_LINE"); lines != nullptr && tick > 0) {
        for (const char *at = lines; *at != '\0';) {
            char *end = nullptr;
            if (std::strtoll(at, &end, 10) == tick) {
                const std::uint32_t sum = Checksum();
                std::fprintf(stderr, "tickline: tick %lld as %s, checksum %08x view %08x stages %08x %08x town %.5f %.5f %.5f\n", (long long) tick, how,
                             sum, g_parts.view, g_parts.stages[0], g_parts.stages[1], g_parts.town[0], g_parts.town[1], g_parts.town[2]);
                CoopTownCamTestLine(tick);
                break;
            }
            if (end == at) {
                break;
            }
            at = *end == ',' ? end + 1 : end;
        }
    }
    static const char *spec = TestEnv("DC_NETPLAY_TEST_TICK_DUMP");
    static const int    only_link = TestEnvInt("DC_NETPLAY_TEST_TICK_DUMP_LINK", 0);
    static std::int64_t done[16] = {};
    static int          done_count = 0;
    static int          link = 1;
    static std::int64_t last = 0;

    const char *colon = spec != nullptr ? std::strchr(spec, ':') : nullptr;

    if (colon == nullptr || tick <= 0) {
        return;
    }
    if (tick < last) { // a new link's ticks
        link++;
        done_count = 0;
    }
    last = tick;
    if (only_link > 0 && link != only_link) {
        return;
    }
    for (int i = 0; i < done_count; i++) {
        if (done[i] == tick) {
            return;
        }
    }
    for (const char *at = spec; at < colon;) {
        char *end = nullptr;
        const long long want = std::strtoll(at, &end, 10);

        if (end == at) {
            break;
        }
        if (want == tick && done_count < 16) {
            done[done_count++] = tick;
            const std::string stem = std::string(colon + 1) + "-l" + std::to_string(link) + "-p" + std::to_string(g_link.player) + "-t" + std::to_string((long long) tick);
            std::string       path = stem + ".bin";

            for (int n = 1; std::filesystem::exists(PathsFromUtf8(path.c_str())) && n < 100; n++) {
                path = stem + "-r" + std::to_string(n) + ".bin";
            }
            std::fprintf(stderr, "tickdump: link %d tick %lld as %s, checksum %08x\n", link, (long long) tick, how, Checksum());
            CoopTownCamTestLine(tick);
            SnapshotDumpTo(path, tick, mode, MapNo, g_solo.replay);
            return;
        }
        at = *end == ',' ? end + 1 : end;
    }
}

// coop-stabfix1: the sender's recorded commands, as the tick they are recorded on starts (see g_recorded_commands).
static void ApplyRecordedCommands(const char *where) {
    if (g_recorded_commands.empty()) {
        return;
    }
    const std::vector<RecordedCommand> due = std::move(g_recorded_commands);

    g_recorded_commands.clear();
    for (const RecordedCommand &c : due) {
        if (g_link.up) { // (alone, quiet: every command of a host's own play)
            std::fprintf(stderr, "catchup: command %d for player %d applied on the catch-up's recorded tick %lld, %s (link tick %lld)\n", (int) c.command.kind,
                         c.player, (long long) c.tick, where, (long long) g_link.tick);
        }
        ApplyCommand(c.player, c.command, g_link.tick);
    }
}

void SoloTick() {
    ApplyRecordedCommands("as its tick starts");
    if (!g_solo.on) {
        static const char *replay = TestEnv("DC_NETPLAY_REPLAY");
        static int         settled = 0;

        // A replaying copy, a second into a town or a dungeon, leaves for the recording's checkpoint as a joiner would.
        if (replay != nullptr && !g_solo.left) {
            settled = RestartTarget(mode, MapNo, selectMapNo) < 0 ? 0 : settled + 1;

            if (settled < kChecksumTicks) {
                return;
            }

            g_solo = Solo{};
            g_solo.left = true;
            g_solo.replay = ReadRecording(replay);
            std::size_t pressed = 0;

            for (std::size_t i = 0; i < g_solo.pads.size(); i += 2) {
                pressed += g_solo.pads[i].buttons != 0 ? 1 : 0;
            }

            std::fprintf(stderr, g_solo.replay ? "catchup: replaying %s (%zu ticks, %zu with a button held, %zu checksums)\n" : "catchup: could not read %s\n",
                         replay, g_solo.pads.size() / 2, pressed, g_solo.sums.size());

            if (!g_solo.replay) {
                return;
            }

            g_link.resync_state = g_solo.checkpoint;
            LeaveForState(g_solo.checkpoint, "replay: DC_NETPLAY_REPLAY leaves for the recording's checkpoint", "SoloTick");
            g_solo.due = true;
            std::fprintf(stderr, "catchup: leaving for the checkpoint's map %lld\n", (long long) g_link.resync_target);
            return;
        }

        if (!g_solo.due || !AtCheckpointPlace()) {
            return;
        }

        // Tick 0: the first tick in the checkpoint's new place (coop-snapshot: a floor-start checkpoint's floor).
        g_solo.on = true;
        g_floor_started = false;
        ControllersOnly();
        g_link.tick = 0;
        g_link.synced[0] = g_link.synced[1] = g_link.previous[0] = g_link.previous[1] = Neutral();
        g_link.menu_owner = 1;
        g_link.held = 0;
        std::fprintf(stderr, g_solo.replay ? "catchup: replay from tick 0 in map %d%s\n" : "catchup: recording from tick 0 in map %d%s\n", (int) MapNo,
                     CheckpointFloor(g_solo.checkpoint) >= 0 ? " (the floor's start)" : "");

        // coop-localfix2 (the room merge's OUT OF SYNC, w-town-apart -Case samebuilding, on main too): a building-start
        // checkpoint is taken as the room starts, and the room's own start rolls the random state before the recording's tick
        // 0 (a6735655 there, f1a5bd0c in the checkpoint); the joiner took the checkpoint's and replayed from it, parted from
        // tick 0 on. The recording's tick 0 state goes in the checkpoint (sent at the link, later) and the joiner sets it at
        // its own tick 0 (CatchJoinTick). DC_NETPLAY_MUTATE=tick0_rand_own (twin): as before.
        if (!g_solo.replay && g_solo.checkpoint.size() >= sizeof(ResyncHeader) && !NetplayMutated("tick0_rand_own")) {
            ResyncHeader header;

            std::memcpy(&header, g_solo.checkpoint.data(), sizeof header);
            header.rand = PortRandState();
            std::memcpy(g_solo.checkpoint.data(), &header, sizeof header);
        }
    }

    // coop-bldgsnap: the host alone out of the building its recording started in (a building-start checkpoint): a merge out
    // here must not catch up from inside (w-town-apart -Case bags -Opening: her replay of his way out by the door did not
    // take it, she merged inside the house while he stood in Norune, then a resync). The recording ends; the merge goes
    // by a place checkpoint and the host's snapshot where snapshots are on, else by the resync.
    // DC_NETPLAY_MUTATE=keep_building_recording (twin): kept, as before.
    if (g_link.player == 1 && !g_solo.replay && !g_catch.active && mode == GAME_MODE_EDIT && EdInteriorFlag == 0 && gameTask == GAME_TASK_PLAY &&
        CheckpointBuilding(g_solo.checkpoint) > 0 && !NetplayMutated("keep_building_recording")) {
        std::fprintf(stderr, "catchup: out of the building the recording started in (at tick %lld): the recording ends\n", (long long) g_link.tick);
        g_solo = Solo{};
        g_building_started = false;
        return;
    }

    std::int64_t tick = g_link.tick;

    // coop-p1follows: DC_NETPLAY_TICK_TRACE_LINK=N: a catch-up's recorded and replayed ticks too, to diff with the lockstep's.
    if (static const int link_trace = TestEnvInt("DC_NETPLAY_TICK_TRACE_LINK", 0); link_trace > 0 && tick > 0 && tick <= link_trace) {
        TickTrace(tick, g_solo.replay ? " replayed" : " recorded");
    }

    // coop-worlds: the replay applies player 2's progress on the tick the host applied it, before the tick's checksum.
    while (g_solo.replay && g_solo.next_delta < g_solo.deltas.size() && g_solo.deltas[g_solo.next_delta].tick <= tick) {
        CoopWorldsApplyProgress(g_solo.deltas[g_solo.next_delta++].value);
    }

    SnapshotDumpTick(tick, mode, MapNo, g_solo.replay); // coop-snapshot: stage 0's dumps (tests)
    TestTickDump(tick, g_solo.replay ? "replayed" : "recorded"); // coop-houseexit (tests)

    // coop-snapshot: stage 1's round trip (tests), recording only: each tick's hashes, and while ticks T to T + K play
    // again the pads recorded the first time (NetplayFrameStart takes and puts back the state).
    // DC_SNAPSHOT_TRIP_WHERE=town|dungeon|floor:N (tests): the trip runs in that place only (floor N, 1 up, of a
    // dungeon); every floor's start is a checkpoint now, so a trip meant for the town or for floor 2 would otherwise
    // run on the floor the host started on.
    const char *place = mode == GAME_MODE_DUNGEON ? "dungeon" : mode == GAME_MODE_EDIT ? "town" : "other";
    static const std::string trip_where = TestEnvOr("DC_SNAPSHOT_TRIP_WHERE", "");
    const bool  trip_here = trip_where.empty() || trip_where == place ||
                           (trip_where.rfind("floor:", 0) == 0 && mode == GAME_MODE_DUNGEON && UserStatus != nullptr &&
                            UserStatus->cur_floor + 1 == std::atoi(trip_where.c_str() + 6));
    const bool  trip_again = !g_solo.replay && trip_here && SnapshotTripTick(tick, place, MapNo, TripChecksum) == SnapshotStep::Again;

    if (tick > 0 && tick % kChecksumTicks == 0 && !trip_again) {
        std::uint32_t sum = Checksum();

        // coop-housemerge (tests, DC_NETPLAY_TEST_SOLO_SUMS=1): each recorded or replayed checksum and its main parts, so the
        // host's recording and a joiner's replay of it can be diffed tick by tick.
        static const bool solo_sums = TestEnvOn("DC_NETPLAY_TEST_SOLO_SUMS");

        if (solo_sums) {
            std::fprintf(stderr, "catchup: sum %s tick %lld %08x random %08x save %08x town (%.2f %.2f %.2f) modes %08x menus %08x stages %08x %08x\n",
                         g_solo.replay ? "replayed" : "recorded", (long long) tick, sum, g_parts.rand, g_parts.save, g_parts.town[0],
                         g_parts.town[1], g_parts.town[2], g_parts.town_modes, g_parts.menus, g_parts.stages[0], g_parts.stages[1]);

            // The save itself at the first two checksums, beside the log (solo-save-<recorded|replayed>-<tick>.bin).
            if (tick <= 2 * kChecksumTicks && SaveData != nullptr) {
                char path[64];

                std::snprintf(path, sizeof path, "solo-save-%s-%lld.bin", g_solo.replay ? "replayed" : "recorded", (long long) tick);
                std::ofstream((std::string(TestEnvOr("DC_NETPLAY_TEST_SOLO_DIR", ".")) + "/" + path).c_str(), std::ios::binary)
                    .write((const char *) SaveData, sizeof(CSaveData));
            }
        }

        if (!g_solo.replay) {
            g_solo.sum_ticks.push_back(tick);
            g_solo.sums.push_back(sum);
            WriteRecording();
        } else if (g_solo.next_sum < g_solo.sums.size() && g_solo.sum_ticks[g_solo.next_sum] == tick) {
            if (g_solo.sums[g_solo.next_sum] == sum) {
                g_solo.matched++;
            } else if (g_solo.parted < 0) {
                g_solo.parted = tick;
                std::fprintf(stderr, "catchup: replay parted at tick %lld (%08x, recorded %08x)\n", (long long) tick, sum,
                             g_solo.sums[g_solo.next_sum]);
            }

            g_solo.next_sum++;
        }
    }

    if (trip_again) {
        g_link.synced[0] = g_solo.pads[(std::size_t) tick * 2];
        g_link.synced[1] = g_solo.pads[(std::size_t) tick * 2 + 1];
    } else if (!g_solo.replay) {
        // coop-localfix2: the recording copy's own pad as its player's (player 2's for a stayer's merge), the other's at rest.
        const int own = g_link.source == 2 && g_link.player == 2 ? 1 : 0;

        g_link.synced[own] = LinkPad(OwnInput(own + 1));
        g_link.synced[1 - own] = Neutral();
        g_solo.pads.push_back(g_link.synced[0]);
        g_solo.pads.push_back(g_link.synced[1]);
    } else {
        // The replay_skip_tick twin plays each tick the pads recorded for the one after it.
        std::size_t at = (std::size_t) (tick + (NetplayMutated("replay_skip_tick") ? 1 : 0)) * 2;

        if (at + 1 < g_solo.pads.size()) {
            g_link.synced[0] = g_solo.pads[at];
            g_link.synced[1] = g_solo.pads[at + 1];
            // coop-p2toan: DC_NETPLAY_MUTATE=pause_not_replayed (l-local-play-pausejoin~twin): his Start (his pause) left out of
            // the pads her catch-up replays, so his pause inside her catch-up never reaches her copy.
            if (NetplayMutated("pause_not_replayed") && (g_link.synced[0].buttons & kInputStart) != 0) {
                g_link.synced[0].buttons &= ~kInputStart;
                static bool said = false;

                if (!said) {
                    said = true;
                    std::fprintf(stderr, "catchup: test: his Start left out of the replay at tick %lld (DC_NETPLAY_MUTATE=pause_not_replayed)\n", (long long) tick);
                }
            }
        } else {
            g_link.synced[0] = g_link.synced[1] = Neutral();

            if (!g_solo.replay_done) {
                g_solo.replay_done = true;
                std::fprintf(stderr, "catchup: replay done at tick %lld: %d of %zu checksums matched%s\n", (long long) tick, g_solo.matched,
                             g_solo.sums.size(), g_solo.parted < 0 ? "" : ", parted");
            }
        }
    }

    Route();
    g_link.tick++;
}

// coop-snapshot: stage 1's round trip (tests), at the main loop's frame start. The pad routing is the link's, outside
// the state set, so it is kept with the state and put back with it, and the tick goes back to T.
void TripFrameStart() {
    static InputPadState trip_previous[2];
    static int           trip_owner = 1;
    static std::uint16_t trip_held = 0;

    switch (SnapshotTripFrame()) {
    case SnapshotStep::Taken:
        trip_previous[0] = g_link.previous[0];
        trip_previous[1] = g_link.previous[1];
        trip_owner = g_link.menu_owner;
        trip_held = g_link.held;
        break;
    case SnapshotStep::Restored:
        g_link.previous[0] = trip_previous[0];
        g_link.previous[1] = trip_previous[1];
        g_link.menu_owner = trip_owner;
        g_link.held = trip_held;
        g_link.tick = SnapshotTripFrom();
        break;
    default:
        break;
    }
}

// Both copies, at J: the lockstep starts on this tick, the inputs for J to J + delay - 1 neutral on both (Begin's
// start, moved to J); the pads' routing (previous, the menu's owner) goes on as the catch-up left it, alike on both.
// WS5: the tick and the time the held log lines of his lockstep go out (EnterLockstep); -1 none held.
std::int64_t g_log_hold_tick = -1, g_log_hold_by = 0;

void LogHoldEnd() {
    if (g_log_hold_tick >= 0) {
        g_log_hold_tick = -1;
        QaLogHold(false);
    }
}

void EnterLockstep(std::int64_t join) {
    g_link.joined_at = CoopWorldsApart() ? join : -1; // coop-pilotfix (CompareChecksum): a catch-up that ends in a worlds merge
    // WS5: his dozen lines here to the merge go out as one write, two ticks on: a test copy's log file beside a full PC costs
    // some 10 ms a line on this thread, in the first ticks whose pads she waits on. DC_NETPLAY_MUTATE=lockstep_lines (twin): line by line.
    if (TakesState() && !NetplayMutated("lockstep_lines")) {
        QaLogHold(true);
        g_log_hold_tick = join + 2;
        g_log_hold_by = NowMs() + 500;
    }

    for (Slot &slot : g_link.local) {
        slot.frame = -1;
    }

    // The other copy's inputs from J on, already here, stay (this link's own: Begin cleared the ring): a joiner still
    // replaying as the host's first lockstep ticks came, the host's last ones before a split among them (coop-leaverace).
    for (Slot &slot : g_link.remote) {
        if (slot.frame < join) {
            slot.frame = -1;
        }
    }

    for (std::int64_t &frame : g_link.sums_frame) {
        frame = -1;
    }

    g_link.tick = join;
    g_test_lockstep_from = join; // coop-keydoor (NetplayTestLockstepFrom)
    g_link.floor_arrival = false; // coop-dungeonjoin: caught up (NetplayArrivalHold)
    // coop-arrivalfull: the arriver's arrival on the floor, from its start, on both copies (the one there first plays on).
    if (g_link.arrival_walk != 0) {
        coop::ArrivalWalkStart(g_link.arrival_walk);
        g_link.arrival_walk = 0;
    }
    JoinStateTaken(join); // coop-p2skin: a catch-up join's replay showed nothing (NetplayCatchingUp)
    coop::FullMenuFullToLocal(); // coop-soakfix4: his menu beside the world, opened during the catch-up, as a local menu
    // coop-joinlobby: the joiner's card's pick, on both copies at J: player 2's member (the partner's own load follows
    // the choice, as the wheel's pick does) and her profile's bag (the host files the one she had: never dropped).
    if (g_catch.pick_set) {
        g_catch.pick_set = false;
        if (g_catch.pick_member >= 0 && g_catch.pick_member < 6) {
            coop::g_partner.choice = g_catch.pick_member;
        }
        if (!g_catch.pick_bag.empty()) {
            coop::ProfileBagsJoinTake(g_catch.pick_name, g_catch.pick_bag, !TakesState());
        }
        std::fprintf(stderr, "catchup: the joiner's pick at tick %lld: player 2 plays %d (%s)\n", (long long) join, coop::g_partner.choice,
                     g_catch.pick_name.empty() ? "no profile" : g_catch.pick_name.c_str());
        g_catch.pick_bag.clear();
    }

    // coop-floorjoin2 (tests): the frame DC_COOP_SHOT_AFTER_JOIN ticks from here, to DC_COOP_SHOT_PATH.
    static const int shot_after = TestEnvInt("DC_COOP_SHOT_AFTER_JOIN", -1);

    if (shot_after >= 0 && TestEnv("DC_COOP_SHOT_PATH") != nullptr) {
        // coop-joinlate: DC_COOP_SHOT_AFTER_WALK=1: N after the arriver's replayed walk has played out instead (coop-arrivalonce:
        // its own screen is black through it), on both copies on one tick (the walk ends on the same link tick); no walk: from here.
        static const bool after_walk = TestEnv("DC_COOP_SHOT_AFTER_WALK") != nullptr;

        if (after_walk && coop::ArrivalWalkPlayer() != 0) {
            g_shot_walk_after = shot_after;
        } else {
            g_shot_due = join + shot_after;
        }
    }
    g_link.first_delay = join + g_link.delay;
    g_link.script_delay = g_link.delay;
    g_link.newest = join + g_link.delay - 1;
    g_link.last_input = Neutral();
    g_link.last_checksum_frame = -1;
    g_link.desync_tick = -1;
    g_link.resync_tick = -1;
    g_link.awaiting_sync = false;
    g_solo.on = false;

    // coop-housemerge: the host's recording is spent once the pair is linked: a later split in the same place (a building
    // in this town) must not record on from tick 0 over this checkpoint's pads (a catch-up then replayed them from the
    // old checkpoint). The next checkpoint (a mode end, a floor's or a building's start) starts it again.
    // DC_NETPLAY_MUTATE=no_building_checkpoint (twin): kept, as before.
    if (g_link.player == 1 && !NetplayMutated("no_building_checkpoint")) {
        g_solo.due = false;
    }

    // coop-houseexit: the host's replay of her state (a merge where she stayed) is spent here too. Left marked a replay, his
    // copy never recorded or took a place checkpoint again, so her next merge on his state (she out of the mayor's house to
    // him outside, l-local-play -HerHouseOut) was a resync that reloaded both copies. DC_NETPLAY_MUTATE=host_keeps_replay
    // (twin): kept, as before.
    if (g_link.player == 1 && g_solo.replay && !NetplayMutated("host_keeps_replay")) {
        g_solo = Solo{};
        std::fprintf(stderr, "catchup: the host's replay of her state is spent at tick %lld\n", (long long) join);
    }

    g_catch.active = false;
    g_catch.join_tick = join;
    g_test_epoch++;
    g_test_epoch_resyncs = g_link.resyncs;

    // T24 desync B: in a town, the joiner takes the town's Georama in the host's SaveData as it was at J (the last blob
    // carries it), and its town's live plots from it. While apart the host's live sync writes the town's live Georama into its SaveData by the wall clock, twice a
    // second (CoopTownGeoramaToSave in coop_worlds' slotsync), and takes player 2's changes as they come: neither is on a
    // tick its recording marks, so the joiner's replay rebuilt the town without them (w-world-split -Case merge-catchup,
    // about one run in three: Matataki's plots, their obtained and stock, at the first checksum after the lockstep).
    // DC_NETPLAY_MUTATE=no_join_save (twin, on the host): no save in the last blob, each copy's own as the replay left it.
    SV_GEORAMA_DATA *town = TakesState() && SaveData != nullptr && mode == GAME_MODE_EDIT && g_catch.join_town == MapNo && MapNo >= 0 && MapNo < 200
                                ? SaveData->GetGrdData(MapNo)
                                : nullptr;

    if (town != nullptr && g_catch.join_save.size() == sizeof *town) {
        std::size_t differed = 0;

        for (std::size_t i = 0; i < sizeof *town; i++) {
            differed += ((const unsigned char *) town)[i] != g_catch.join_save[i];
        }

        std::memcpy((void *) town, g_catch.join_save.data(), sizeof *town);

        for (int plot = 0; plot < 24; plot++) {
            CoopTownGeoramaPlotFromSave(MapNo, plot);
        }

        std::fprintf(stderr, "catchup: the host's save at tick %lld taken (town %d's Georama, %zu bytes differed)\n", (long long) join, MapNo, differed);
    }

    g_catch.join_save.clear();
    g_catch.join_town = -1;

    // coop-catchuprace (train 53's cold red, in sync only; w-world-merge-catchup-latemid): a change of hers the host took on
    // its tick J itself (as its catch-up closed) is in his state at J, and recorded at J, but her replay ends with tick J-1
    // and applies a tick's changes as it replays it (SoloTick): it was never applied here, OUT OF SYNC at the next checksum.
    // Applied now, before tick J, as the host had it. DC_NETPLAY_MUTATE=join_tick_delta_left (twin): left, as before.
    ApplyRecordedCommands("as the lockstep starts"); // coop-stabfix1 (the sender's)
    if (TakesState() && !NetplayMutated("join_tick_delta_left")) {
        while (g_solo.next_delta < g_solo.deltas.size() && g_solo.deltas[g_solo.next_delta].tick <= join) {
            std::fprintf(stderr, "catchup: player 2's change recorded at tick %lld applied as the lockstep starts (J %lld)\n",
                         (long long) g_solo.deltas[g_solo.next_delta].tick, (long long) join);
            CoopWorldsApplyProgress(g_solo.deltas[g_solo.next_delta++].value);
        }
    }

    double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - g_catch.replay_began).count();

    if (TakesState()) {
        g_arrival.lockstep = NowMs(); // coop-offthread
        std::fprintf(stderr, "catchup: lockstep from tick %lld, %.1f s after the replay began (%.0f ticks a second) [t %lld]\n", (long long) join, seconds,
                     seconds > 0.0 ? (double) join / seconds : 0.0, LogMs());
        std::fprintf(stderr, "catchup: the replay's time: %ld ticks, %.0f ms outside the netplay tick (%.2f a tick), %.0f ms waiting for pads in %ld waits\n",
                     g_catch.replay_ticks, g_catch.replay_rest_ms, g_catch.replay_ticks > 0 ? g_catch.replay_rest_ms / g_catch.replay_ticks : 0.0, g_catch.replay_wait_ms,
                     g_catch.replay_waits);
        g_stall_watch.replaying = false;
        if (TestEnvInt("DC_NETPLAY_TEST_STALL_WATCH", 0) > 0) {
            std::fprintf(stderr, "catchup: the replay's parts: %lld calls for %ld ticks; first Receive %.0f ms (most %.1f), pace hook %.0f ms (most %.1f), SoloTick %.0f ms (most %.1f); %lld datagrams (most %lld a pass), partner log %lld of all this copy's so far, %.0f ms (most %.1f)\n",
                         g_stall_watch.calls.load(), g_catch.replay_ticks, g_stall_watch.receive_ms, g_stall_watch.receive_most, g_stall_watch.hook_ms, g_stall_watch.hook_most,
                         g_stall_watch.solo_ms, g_stall_watch.solo_most, g_stall_watch.datagrams, g_stall_watch.pass_most, g_stall_watch.log_pieces, g_stall_watch.log_ms,
                         g_stall_watch.log_most);
        }
        if (TestEnvInt("DC_NETPLAY_TEST_STALL_WATCH", 0) > 0) {
            for (int i = 0; i < 8 && g_stall_watch.magics[i] != 0; i++) {
                std::fprintf(stderr, "catchup: the replay's datagrams: magic %08x: %lld\n", g_stall_watch.magics[i], g_stall_watch.magic_count[i]);
            }
        }
        std::fprintf(stderr, "catchup: the replay's work: %ld shot lines said on the game thread, %ld frames read back from the screen\n",
                     g_catch.replay_lines_said, g_catch.replay_readbacks);
        // (coop-sendstop: from his first tick at her newest pads to the lockstep, what his word there costs: l-local-play
        // -EdgeCloseMs)
        if (g_link.stayer_merge && g_catch.edge_at != std::chrono::steady_clock::time_point{}) {
            std::fprintf(stderr, "catchup: at her newest pads %.0f ms before the lockstep\n",
                         std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - g_catch.edge_at).count());
        }
        if (g_catch.replay_free > 0) {
            std::fprintf(stderr, "netplay: test: the frame budget gave back the replay's %lld frames\n", (long long) g_catch.replay_free);
        }
        g_catch.replay_frame = -1;
    } else {
        g_arrival.lockstep = NowMs(); // coop-offthread
        std::fprintf(stderr, "catchup: lockstep from tick %lld [t %lld]\n", (long long) join, LogMs());
    }
    CoopWorldsBagDump("lockstep"); // coop-p2toan: whose bags the join left (DC_WORLDS_TEST_BAGDUMP)
    NetplayTestLockstep(join);     // coop-fx51b (tests)
    std::snprintf(g_link.status, sizeof g_link.status, "Linked: player %d", g_link.player);
    g_link.source = 1; // coop-localfix2: the catch-up is done; the host's again (both copies alike, on this tick)
    g_link.arriving = false;
}

// coop-leaverace: the other copy went apart (a split) on a tick this copy has not played yet: it plays on to that tick,
// where its own game splits the same way (the split is in both copies' play), and the Leave alone closes nothing. A
// joiner still replaying the catch-up when the host split just after the lockstep began (o-opening-resync house runs,
// 2026-10-25) closed the link mid-replay and never split. Only with the pads to get there: the joiner's last blob held
// (J known; the ticks from J to the split are the lockstep's first, neutral up to J + delay) or, in the lockstep, the
// host's last inputs as they come. DC_NETPLAY_MUTATE=close_on_leave (twin): the Leave closes at once, as before.
bool PlaysOnToSplit() {
    if (!g_link.peer_left || g_link.peer_apart_at < 0 || g_link.tick >= g_link.peer_apart_at) {
        return false;
    }

    if (g_catch.active && (!TakesState() || g_catch.join_tick < 0)) {
        return false;
    }

    const bool on = !NetplayMutated("close_on_leave");

    if (!g_link.apart_heard) {
        g_link.apart_heard = true;
        std::fprintf(stderr, "netplay: player %d went apart at tick %lld; this copy, at tick %lld%s, %s\n", 3 - g_link.player,
                     (long long) g_link.peer_apart_at, (long long) g_link.tick, g_catch.active ? " (still replaying the catch-up)" : "",
                     on ? "plays on to it" : "stops here (close_on_leave)");
    }

    return on;
}

// coop-catchupdrop test hook (DC_NETPLAY_TEST_CATCH_GIVE_UP=P:MS[:N]): player P's copy gives its Nth catch-up (1: the
// first) up MS ms after it began there (closes, as its quiet clock would after kLoadWaitMs), once a run; the copy that
// takes the state holds its catch-up still till then (CatchGiveUpHolds: a stuck one, his "not walking yet" of Jep's 11-04
// third try). Its line says how much of the other copy's log came meanwhile (the partner log beside a waiting catch-up).
struct CatchGiveUp {
    int           player = -1, ms = 0, nth = 1;
    bool          done = false;
    std::uint64_t log_from = 0;
    bool          log_from_set = false;
};

CatchGiveUp &GiveUpTest() {
    static CatchGiveUp test = [] {
        CatchGiveUp out;

        if (const char *v = TestEnv("DC_NETPLAY_TEST_CATCH_GIVE_UP"); v != nullptr && std::sscanf(v, "%d:%d:%d", &out.player, &out.ms, &out.nth) < 2) {
            out.player = -1;
        }
        return out;
    }();
    return test;
}

bool CatchGiveUpHolds() {
    CatchGiveUp &test = GiveUpTest();

    if (test.player != g_link.player || test.done || !g_catch.active || g_catches_begun != test.nth) {
        return false;
    }
    if (!test.log_from_set) {
        test.log_from_set = true;
        test.log_from = PartnerLogTakenBytes();
        std::fprintf(stderr, "netplay: test: this copy gives its catch-up %d up in %d ms (%s)%s [t %lld]\n", test.nth, test.ms,
                     TakesState() ? "it takes the state" : "it sends its state", TakesState() ? ", held still till then" : "", LogMs());
    }
    return TakesState();
}

bool CatchGiveUpDue() {
    CatchGiveUp &test = GiveUpTest();

    if (test.player != g_link.player || test.done || !g_catch.active || g_catches_begun != test.nth) {
        return false;
    }
    CatchGiveUpHolds(); // (its first line and log mark, if the hold has not run)

    if (std::chrono::steady_clock::now() - g_catch.began < std::chrono::milliseconds(test.ms)) {
        return false;
    }
    test.done = true;
    std::fprintf(stderr, "netplay: test: this copy gives its catch-up up (%llu bytes of the other copy's log came during it) [t %lld]\n",
                 (unsigned long long) (PartnerLogTakenBytes() - test.log_from), LogMs());
    Close("this copy gave up the catch-up (the test's)");
    g_catch.active = false;
    return true;
}

// The other copy gone during the catch-up: as the lockstep's wait would decide it.
bool CatchLost() {
    if (CatchGiveUpDue()) {
        return true;
    }

    auto now = std::chrono::steady_clock::now();
    auto quiet = std::chrono::duration_cast<std::chrono::milliseconds>(now - std::max(g_catch.heard, g_link.peer_busy_at)).count();

    if ((!g_link.peer_left || PlaysOnToSplit()) && quiet <= kLoadWaitMs) {
        return false;
    }

    Close(g_link.peer_gave_up ? "the other copy gave up the catch-up" : g_link.peer_left ? "the other copy left during the catch-up"
                                                                                        : "the other copy stopped answering during the catch-up");
    g_catch.active = false;
    return true;
}

// The host, each tick of a catch-up: the next blob once the last is held and the joiner has replayed it, then a tick
// alone as before. True when the lockstep starts on this tick instead.
// coop-pagediff: the pages alone pay off on a slow link: Steam, or the internet (Tailscale, a public address). On this PC or a
// LAN the whole set goes in about 100 ms, under the cost of the page exchange (its round trip, the arriver's own take, and
// the pages no longer going over during the arriver's load): l-local-play's house-out merge caught up in 709 ms whole and in
// 772-1047 ms by pages on 127.0.0.1. So there it goes whole, as before. The peer as the link knows it (learnt, or the
// configured one, as the diagnostics name it); none: whole; a name, not an address (Tailscale's): by pages.
// DC_NETPLAY_MUTATE=pagediff_always: by pages on any link (the tests on this PC); no_pagediff: whole on any.
bool PageDiffLink() {
    if (NetplayMutated("no_pagediff")) {
        return false;
    }
    if (NetplayMutated("pagediff_always")) {
        return true;
    }
    int queued = 0, rate = 0;

    if (NetSocketQueue(queued, rate)) {
        return true; // Steam
    }
    const std::string peer = NetSocketLearnsPeer() ? std::string(NetSocketPeerText()) : Settings().peer;
    unsigned          a = 0, b = 0, c = 0, d = 0;

    if (peer.empty() || peer.rfind("localhost", 0) == 0) {
        return false;
    }
    if (std::sscanf(peer.c_str(), "%u.%u.%u.%u", &a, &b, &c, &d) != 4) {
        return true;
    }
    const bool near = a == 127 || a == 10 || (a == 172 && b >= 16 && b <= 31) || (a == 192 && b == 168) || (a == 169 && b == 254);

    return !near;
}

// coop-pagediff, the host: from the page list or pages to the whole state set (packed as SnapFrameStart packs it).
void PageWhole() {
    std::vector<unsigned char> packed;

    if (NetplayMutated("snap_raw")) {
        packed = *g_snap.raw;
    } else {
        SnapshotPack(*g_snap.raw, packed); // (a failed rebuild, never seen: on the game thread still)
    }
    g_snap.bytes.swap(packed);
    g_snap.parts = (g_snap.bytes.size() + kSnapPartBytes - 1) / kSnapPartBytes;
    g_snap.sent = 0;
    g_snap.mark = kSnapMark;
    g_snap.page_step = 0;
    std::fprintf(stderr, "pagediff: the taker asked for the whole state set: %zu bytes in %zu parts [t %lld]\n", g_snap.bytes.size(), g_snap.parts, LogMs());
}

// coop-offthread: a SnapJob's work (its own thread; the game thread's under pack_on_tick). It reads only what it holds.
void SnapJobRun(SnapJob &job) {
    using Clock = std::chrono::steady_clock;
    const auto start = Clock::now();
    auto       ms_since = [](Clock::time_point at) { return (long long) std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - at).count(); };

    if (job.kind == 1) {
        job.taken_size = job.bytes.size();
        if (job.by_pages) {
            job.raw = std::make_shared<std::vector<unsigned char>>(job.bytes);
        }
        // coop-localfix2 (Jep, 2026-10-26: the arriving copy's black while it catches up): the state set packed for the
        // link (about a third of its size). DC_NETPLAY_MUTATE=snap_raw (twin): sent as it is.
        if (!job.raw_send) {
            const auto                 pack_start = Clock::now();
            std::vector<unsigned char> packed;

            SnapshotPack(job.bytes, packed);
            job.bytes.swap(packed);
            job.pack_ms = ms_since(pack_start);
        }
        // coop-localfix2 test hook (DC_NETPLAY_TEST_SNAP_DUMP=PATH): the whole state set as it would be sent, to measure its compression.
        if (const char *dump = TestEnv("DC_NETPLAY_TEST_SNAP_DUMP"); dump != nullptr) {
            std::ofstream(PathsFromUtf8(dump), std::ios::binary).write(reinterpret_cast<const char *>(job.bytes.data()), (std::streamsize) job.bytes.size());
        }
        // coop-pagediff: the page list goes first (the whole set kept as taken for the pages the taker needs, or its ask).
        if (job.by_pages) {
            const auto                 list_start = Clock::now();
            std::vector<unsigned char> list;

            if (SnapshotPageList(*job.raw, list, &job.list_pages)) {
                SnapshotPack(list, job.list_packed);
                job.list_size = list.size();
                job.listed = true;
            }
            job.list_ms = ms_since(list_start);
        }
    } else if (job.kind == 2) {
        std::vector<unsigned char> diff;

        job.diff_ok = SnapshotPageDiff(*job.raw, job.need, diff, &job.diff_pages);
        if (job.diff_ok) {
            job.diff_size = diff.size();
            SnapshotPack(diff, job.bytes);
        }
    }
    job.work_ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

void SnapJobPoll();

// coop-offthread: starts a SnapJob on a thread of its own; g_snap holds it until SnapJobPoll takes what it made (a new
// session's g_snap = Snap{} lets it go: the thread ends on its own copy of the inputs).
void SnapJobStart(const std::shared_ptr<SnapJob> &job) {
    g_snap.job = job;
    if (NetplayMutated("pack_on_tick")) {
        const auto at = std::chrono::steady_clock::now();

        SnapJobRun(*job);
        job->tick_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - at).count();
        job->done.store(true);
        SnapJobPoll();
        return;
    }
    g_snap_threads++; // coop-cand58: NetplayStopThreads waits for it
    std::thread([job] {
        SnapJobRun(*job);
        job->done.store(true);
        g_snap_threads--;
    }).detach();
}

// coop-offthread: the game thread, each frame start and catch-up tick: a SnapJob done is taken up (the logs as before).
void SnapJobPoll() {
    if (!g_snap.job || !g_snap.job->done.load()) {
        return;
    }
    const std::shared_ptr<SnapJob> job = std::move(g_snap.job);
    const auto                     at = std::chrono::steady_clock::now();

    g_snap.job.reset();
    if (job->kind == 1) {
        g_snap.bytes.swap(job->bytes);
        if (!job->raw_send) {
            std::fprintf(stderr, "catchup: state set packed: %zu of %zu bytes (%lld ms)\n", g_snap.bytes.size(), job->taken_size, job->pack_ms);
        }
        g_snap.parts = (g_snap.bytes.size() + kSnapPartBytes - 1) / kSnapPartBytes;
        if (job->by_pages && job->listed) {
            g_snap.raw = job->raw;
            std::fprintf(stderr, "pagediff: page list of %zu pages: %zu bytes, %zu packed (%lld ms); the whole set packed was %zu\n", job->list_pages, job->list_size,
                         job->list_packed.size(), job->list_ms, g_snap.bytes.size());
            g_snap.bytes.swap(job->list_packed);
            g_snap.parts = (g_snap.bytes.size() + kSnapPartBytes - 1) / kSnapPartBytes;
            g_snap.mark = kPageListMark;
            g_snap.page_step = 1;
        } else if (job->by_pages) {
            g_snap.raw.reset();
            std::fprintf(stderr, "pagediff: no page list: the whole state set\n");
        }
        g_snap.taken = true;
        std::fprintf(stderr, "catchup: snapshot taken at tick %lld: %zu bytes in %zu parts (%lld ms) [t %lld]\n", (long long) g_snap.tick, g_snap.bytes.size(),
                     g_snap.parts, (long long) std::chrono::duration_cast<std::chrono::milliseconds>(at - job->began).count(), LogMs());
        // (x-town-stayermenu reads it: the game thread's share of the take)
        const bool on_tick = NetplayMutated("pack_on_tick");

        std::fprintf(stderr, "catchup: the state set's take held the game thread %.0f ms (its copy %.0f ms); its pack and page list %.0f ms %s\n",
                     job->tick_ms, job->tick_ms - (on_tick ? job->work_ms : 0.0), job->work_ms, on_tick ? "on the game thread" : "on a thread of their own");
        return;
    }
    if (job->kind == 2 && g_snap.page_step == 4) { // (not a whole set asked for meanwhile)
        if (!job->diff_ok) { // (never seen) the whole set instead
            g_snap.whole_asked = true;
            PageWhole();
            return;
        }
        g_snap.bytes.swap(job->bytes);
        g_snap.parts = std::max<std::size_t>(1, (g_snap.bytes.size() + kSnapPartBytes - 1) / kSnapPartBytes);
        g_snap.sent = 0;
        g_snap.mark = kPageDiffMark;
        g_snap.page_step = 3;
        std::fprintf(stderr, "pagediff: sending %zu pages (%zu bytes, %zu packed) of the state set's %zu bytes, in %zu parts (%lld ms) [t %lld]\n", job->diff_pages,
                     job->diff_size, g_snap.bytes.size(), g_snap.raw ? g_snap.raw->size() : 0, g_snap.parts,
                     (long long) std::chrono::duration_cast<std::chrono::milliseconds>(at - job->began).count(), LogMs());
    }
}

// coop-pagediff, the host, its parts all held: the next step (true), or none (false: the pads from S go).
bool PageStep() {
    if (g_snap.page_step == 1) {
        g_snap.page_step = 2;
        std::fprintf(stderr, "pagediff: the page list is over: waiting for the taker's need [t %lld]\n", LogMs());
        return true;
    }
    if (g_snap.page_step == 2) {
        if (g_snap.need_got.empty() || std::find(g_snap.need_got.begin(), g_snap.need_got.end(), false) != g_snap.need_got.end()) {
            return true;
        }
        // coop-offthread: the pages' diff and pack off the game thread (SnapJob kind 2; step 4 while it runs, SnapJobPoll ends it)
        auto job = std::make_shared<SnapJob>();

        job->kind = 2;
        job->began = std::chrono::steady_clock::now();
        job->raw = g_snap.raw;
        job->need = g_snap.need;
        g_snap.page_step = 4;
        SnapJobStart(job);
        return true;
    }
    if (g_snap.page_step == 4) {
        return true; // (the pages' diff under way)
    }
    if (g_snap.page_step == 3) {
        g_snap.page_step = 0;
    }
    return false;
}

bool CatchHostTick() {
    CostScope cost{g_costs.catch_ms};
    Receive();

    if (CatchLost()) {
        return false;
    }

    // coop-lossylink (x-pagediff-floor-lossy, train 50: "A goes by 3, B by 1", no apply, no lockstep): her copy links only on
    // the Hello this copy linked with (coop-stayerhello's link_source), which Begin sent five times and the tick path again
    // with each tick. A host catching her up sends no ticks, so under loss, with her copy holding back as those five came
    // (its slot sync busy), she waited for a link Hello that never came again and his catch-up for her answer, till the
    // link dropped. Twice a second until she answers (a piece of the catch-up acknowledged, or her ticks).
    // DC_NETPLAY_MUTATE=link_hello_once (twin): Begin's five alone, as before.
    static std::uint32_t answered_in = 0; // (the link session she has answered in: once, for the whole catch-up)

    if (!g_link.peer_ticking && answered_in != g_link.my_hello.session && !NetplayMutated("link_hello_once")) {
        for (std::uint32_t word : g_catch.out_held) {
            answered_in = word != 0 ? g_link.my_hello.session : answered_in;
        }
        static auto next = std::chrono::steady_clock::now();
        const auto  now = std::chrono::steady_clock::now();

        if (answered_in != g_link.my_hello.session && now >= next) {
            next = now + std::chrono::milliseconds(500);
            NetSocketSend(&g_link.my_hello, sizeof g_link.my_hello);
            static std::uint32_t said = 0;

            if (said != g_link.my_hello.session) {
                said = g_link.my_hello.session;
                std::fprintf(stderr, "catchup: player 2 has not answered yet: this copy's link Hello again, twice a second, till she does\n");
            }
        }
    }

    SnapJobPoll(); // coop-offthread
    if (g_snap.on && g_snap.whole_asked && g_snap.mark != kSnapMark && g_snap.raw && !g_snap.raw->empty()) {
        PageWhole(); // coop-pagediff: the taker's rebuild failed
    }

    std::int64_t tick = g_link.tick;

    // coop-joinbudget (her late floor join under 100 ms of lag, paced: 6.7 s black, 4.9 s budget): the host's own join's pads go on
    // as they come too (a blob each 2 ticks once the last is held), not only once she has replayed the last and said so: that
    // held each blob a round trip and her replay behind it (blobs 0.4 to 0.9 s apart, 1.65 s from her apply to the lockstep), as
    // her merge's were before coop-localfix2. DC_NETPLAY_MUTATE=pads_wait_replay (twin): only once she has replayed the last.
    const bool host_streams = g_link.source != 2 && g_catch.join_tick < 0 && CatchOutHeld() && (!g_snap.on || g_snap.pads_begun) &&
                              tick - g_catch.sent_to >= 2 && !NetplayMutated("pads_wait_replay");

    if (g_catch.out_id == 0) {
        g_snap.on = SnapshotJoinOn() && SnapshotJoinHere();

        if (g_snap.on) {
            CatchBlob(0, 0, true, -1, true); // the checkpoint alone: the place to land (coop-floorjoin2: the state set follows)
            g_snap.take_due = true;
            g_snap.due_at = std::chrono::steady_clock::now();
            g_snap.due_drawn = g_frames_drawn;
            std::fprintf(stderr, "catchup: the join goes by snapshot: the checkpoint first, the state set once a quiet frame comes\n");
        } else {
            CatchBlob(0, tick, true, -1);
        }
    } else if (g_snap.on && !g_snap.refused && (!g_snap.pads_begun || g_snap.sent < g_snap.parts)) {
        // The parts, one blob each, once the last blob is held; then the pads from S.
        if (CatchOutHeld() && g_snap.taken && g_snap.sent < g_snap.parts) {
            std::size_t at = g_snap.sent * kSnapPartBytes;
            std::size_t length = std::min(kSnapPartBytes, g_snap.bytes.size() - at);

            SnapBlob(g_snap.sent, g_snap.parts, g_snap.bytes.data() + at, length, g_snap.mark);
            g_snap.sent++;
        } else if (CatchOutHeld() && g_snap.taken && g_snap.sent == g_snap.parts && PageStep()) {
            // coop-pagediff: the list is over, the need not yet in, or the pages begin
        } else if (CatchOutHeld() && g_snap.taken && g_snap.sent == g_snap.parts && !g_snap.pads_begun) {
            g_snap.pads_begun = true;
            g_catch.sent_to = g_snap.tick;
            CatchBlob(g_snap.tick, tick, false, -1);
        }
    } else if (g_snap.on && g_snap.refused && !g_snap.pads_begun) {
        if (CatchOutHeld()) {
            g_snap.pads_begun = true;
            g_catch.sent_to = 0;
            CatchBlob(0, tick, false, -1);
            std::fprintf(stderr, "catchup: the joiner refused the snapshot: the pads from tick 0 instead\n");
        }
    } else if (host_streams || (g_catch.join_tick < 0 && CatchOutHeld() && g_catch.test_close_at == tick) || g_catch.join_tick < 0 && CatchOutHeld() &&
               (g_catch.caught >= g_catch.sent_to || (g_link.source == 2 && !NetplayMutated("stayer_wide_gap") && tick - g_catch.sent_to >= 2))) {
        // coop-localfix2: her merge's pads go on as they come (a blob each 2 ticks once the last is held, his word of how far he is each 8 ms while he waits), not only once he
        // has replayed the last: waiting on his reply each time held his replay to her own pace (61 ticks a second, 9.8 s).
        // coop-localfix2: for her merge (she stayed, he arrives behind his door's fade) the lockstep starts only once he is
        // within the link's delay (or kStayerGapTicks) of her tick, so her copy never waits long on his replay (a 90-tick gap held her frame 0.75 s,
        // l-local-play -HerSeries); after 8 s of closing (a copy too slow to close) the usual gap. DC_NETPLAY_MUTATE=
        // stayer_wide_gap (twin): the usual gap at once.
        if (g_catch.closing_from == std::chrono::steady_clock::time_point{}) {
            g_catch.closing_from = std::chrono::steady_clock::now();
        }
        const bool tight = g_link.source == 2 && !NetplayMutated("stayer_wide_gap") &&
                           std::chrono::steady_clock::now() - g_catch.closing_from < std::chrono::milliseconds(kStayerTightMs);
        static const bool  no_gap = NetplayMutated("stayer_gap_0");
        static const int   stayer_gap = no_gap ? 0 : TestEnvInt("DC_NETPLAY_TEST_STAYER_GAP", kStayerGapTicks);
        // coop-fx51c test hook (DC_NETPLAY_TEST_JOIN_GAP=N, the host): the lockstep's J named once she is within N ticks, not
        // kJoinGapTicks, so with DC_NETPLAY_TEST_SLOW_LAST_BLOB her last blob holds that much play at the game's pace: the host's
        // first lockstep ticks, and a split among them, come while she still replays every run (w-world-split -SplitAfterJoin on
        // a fixture: its catch-up of some 90 ticks closed in blobs of 2 and the race was never hit).
        static const int   join_gap = TestEnvInt("DC_NETPLAY_TEST_JOIN_GAP", kJoinGapTicks);
        // coop-reds2: after a fresh set (its parts all sent, his word at or past its tick), the close within kFallbackGapTicks:
        // the fallback's one wait (a known exception to merge-no-wait, kRestartMagic).
        const bool         fallback = g_catch.restarts > 0 && g_snap.taken && g_snap.sent >= g_snap.parts && g_catch.caught >= g_catch.restart_tick;
        const std::int64_t gap = fallback ? kFallbackGapTicks : tight ? (no_gap ? 0 : std::max<std::int64_t>(stayer_gap, g_link.delay)) : join_gap;
        // coop-soakfix4 (soak seeds 1079, 1135, 1183, 1303, 1367: a rejoin under 90 ticks after the host's recording began
        // again): no word from the joiner yet (caught -1, she has not begun her replay: still in her floor's start event) is
        // no tick of hers, so never "within the gap". Read as tick -1, the host's tick 12 was 13 behind: the catch-up closed
        // at once, the host's first lockstep tick waited for a copy 5 s from landing, and the link dropped (a rejoin's
        // OUT OF SYNC, B2's crash, a hang). DC_NETPLAY_MUTATE=close_on_no_word (twin): as before.
        const bool         heard_word = g_catch.caught >= 0 || NetplayMutated("close_on_no_word");
        // (a fresh set under way: no close by the usual gaps on his 50 ms words of the replay it replaces)
        bool               last = (heard_word && tick - g_catch.caught <= gap && (fallback || g_catch.restarts == 0)) || g_catch.test_close_at == tick;

        // (coop-joinbudget: a blob of pads sent before her word reached the last one's end, x-floor-join -Seamless's check of
        // the stream; with pads_wait_replay none is)
        if (g_link.source != 2 && g_catch.caught < g_catch.sent_to) {
            std::fprintf(stderr, "catchup: pads streamed: ticks %lld to %lld before her word reached %lld (her word %lld)\n", (long long) g_catch.sent_to,
                         (long long) tick, (long long) g_catch.sent_to, (long long) g_catch.caught);
        }
        if (g_link.source != 2 && CoopDebug()) { // (coop-joinbudget: the host's own join's close, under DC_COOP_DEBUG)
            std::fprintf(stderr, "catchup: closing at the host's tick %lld, her word %lld (%lld behind, the gap %lld) [t %lld]\n", (long long) tick, (long long) g_catch.caught,
                         (long long) (tick - g_catch.caught), (long long) gap, LogMs());
        }
        if (g_link.source == 2) {
            std::fprintf(stderr, "catchup: closing at her tick %lld, his word %lld (%lld behind, the gap %lld) [t %lld]\n", (long long) tick, (long long) g_catch.caught,
                         (long long) (tick - g_catch.caught), (long long) gap, LogMs());
        }

        CatchBlob(g_catch.sent_to, tick, false, last ? tick : -1);

        if (last) {
            g_close_watch = CloseWatch{120, fallback};
            if (fallback) {
                std::fprintf(stderr, "catchup: the fallback's close at tick %lld, his word %lld (%lld behind): this copy waits on his last ticks once\n",
                             (long long) tick, (long long) g_catch.caught, (long long) (tick - g_catch.caught));
            }
            EnterLockstep(tick);
            CatchSend();
            return true;
        }
    }

    CatchSend();
    std::snprintf(g_link.status, sizeof g_link.status, "Player 2 is joining");
    SoloTick();
    g_last_tick_ms = NowMs();
    return false;
}

void SayCaught(std::int64_t tick) {
    Busy caught{};

    caught.magic = kCaughtMagic;
    caught.player = (std::uint32_t) g_link.player;
    caught.session = g_link.session;
    caught.unused = (std::uint32_t) tick;
    NetSocketSend(&caught, sizeof caught);
    g_catch.said = std::chrono::steady_clock::now();
}

// The joiner, each tick of a catch-up: leave for the checkpoint once it is whole; from the first tick there, the host's
// pads, as fast as the game runs them (NetplayCatchingUp); out of pads, say so and wait for the next blob. True when the
// lockstep starts on this tick.
// Test hook (DC_NETPLAY_TEST_REPLAY_FREE=1): the joiner's replay frames do not count against its --frames budget,
// given back as they run. The replay is as long as the host's lead, which grows the later the joiner gets to Join (its
// boot under load), and a title-join test's joiner spent its whole budget catching up and never synced
// (n-debug-start-pick-town: the host 2227+ ticks ahead, 2026-10-24). Mutation replay_counts: the old budget.
void ReplayFree() {
    static const bool on = TestEnvOn("DC_NETPLAY_TEST_REPLAY_FREE");

    if (!on || g_catch.replay_frame < 0 || NetplayMutated("replay_counts")) {
        return;
    }

    std::int64_t now = dc::app::GameFrameCount();
    dc::app::GameExtendFrameBudget(now - g_catch.replay_frame);
    g_catch.replay_free += now - g_catch.replay_frame;
    g_catch.replay_frame = now;
}

// WS5 (test hook DC_NETPLAY_TEST_STALL_WATCH=<ms>): his replay's time by part, and what the game thread is doing whenever no
// replay tick has played for <ms> (l-local-play-houseout-slowreplay's stalls of 6-10 s, whose cause no log named).
void StallWatchRun(int limit_ms) {
    long seen = -1;
    auto seen_at = std::chrono::steady_clock::now();
    auto said_at = seen_at;

    for (;;) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        const long ticks = g_stall_watch.ticks.load();
        const auto now = std::chrono::steady_clock::now();
        static int inside_said = 0;

        // (a long first Receive, whatever the limit: the part that held the replay)
        if (const long long from = g_stall_watch.receive_from.load(); g_stall_watch.replaying.load() && from != 0 && NowMs() - from >= 100 && inside_said < 40) {
            inside_said++;
            CoopStallSample("the first Receive", true, NowMs() - from, ticks, g_stall_watch.calls.load());
        }

        if (ticks != seen || !g_stall_watch.replaying.load()) {
            seen = ticks;
            seen_at = said_at = now;
            continue;
        }
        const auto quiet = std::chrono::duration_cast<std::chrono::milliseconds>(now - seen_at).count();

        if (quiet >= limit_ms && now - said_at >= std::chrono::milliseconds(limit_ms)) {
            said_at = now;
            CoopStallSample(g_stall_watch.where.load(), g_stall_watch.inside.load(), (long long) quiet, ticks, g_stall_watch.calls.load());
        }
    }
}

void StallWatchStart() {
    static const int limit_ms = TestEnvInt("DC_NETPLAY_TEST_STALL_WATCH", 0);
    static bool      started = false;

    if (limit_ms > 0 && !started) {
        started = true;
        CoopStallAdopt();
        std::thread(StallWatchRun, limit_ms).detach();
    }
}

struct StallWatchScope {
    StallWatchScope() {
        g_stall_watch.inside = true;
        g_stall_watch.calls++;
    }
    ~StallWatchScope() { g_stall_watch.inside = false; }
};

double SinceMs(std::chrono::steady_clock::time_point from) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - from).count();
}

bool CatchJoinTick() {
    StallWatchStart();
    StallWatchScope watch_scope;
    const auto      receive_from = std::chrono::steady_clock::now();

    const long long datagrams_from = g_link.heard_datagrams;

    StallWhere("the first Receive");
    g_stall_watch.receive_from = NowMs();
    Receive();
    g_stall_watch.receive_from = 0;
    g_stall_watch.receive_ms += SinceMs(receive_from);
    g_stall_watch.receive_most = std::max(g_stall_watch.receive_most, SinceMs(receive_from));
    g_stall_watch.datagrams += g_link.heard_datagrams - datagrams_from;
    g_stall_watch.pass_most = std::max(g_stall_watch.pass_most, (long long) g_link.heard_datagrams - datagrams_from);
    g_catch.walk_wait = false;
    StallWhere("CatchJoinTick's start");

    if (CatchLost()) {
        return false;
    }
    if (CatchGiveUpHolds()) { // (coop-catchupdrop's test hook: a stuck catch-up; Busy as the "not walking yet" wait's)
        static auto busy_at = std::chrono::steady_clock::now();

        if (std::chrono::steady_clock::now() - busy_at > std::chrono::seconds(1)) {
            Busy busy{kBusyMagic, (std::uint32_t) g_link.player, g_link.session, 0};

            NetSocketSend(&busy, sizeof busy);
            busy_at = std::chrono::steady_clock::now();
        }
        return false;
    }

    std::snprintf(g_link.status, sizeof g_link.status, "Joining player 1: catching up");

    if (!g_solo.left) {
        if (g_solo.checkpoint.empty()) {
            return false;
        }

        g_solo.left = true;
        g_solo.due = true;
        g_link.resync_state = g_solo.checkpoint;
        // coop-snapjoin: the host's state set follows and this copy is in the checkpoint's place already (her own way onto
        // his floor, into his building or town): it lands where it stands, and the apply puts his state over it, with no
        // load of the zone she is in (Jep's 2026-10-25: from the first scene to both on floor 1 "the zone must never
        // reset"). DC_NETPLAY_MUTATE=snap_land_reload (twin): she leaves for the checkpoint's map and loads it again.
        // Until the first part of the state set comes, this copy plays on where it is: the host may yet send the pads from
        // tick 0 instead (a scene under way on its side), and those replay from the checkpoint's load.
        const bool land_here = g_snap.expected && InCheckpointZone() && !NetplayMutated("snap_land_reload");

        if (land_here && g_snap.in_total == 0 && g_catch.have_to <= 0) {
            g_solo.left = false;
            g_solo.due = false;
            return false;
        }
        if (land_here && g_snap.in_total > 0) {
            g_catch.land_here = true;
            ZoneLeaveLog(RestartTarget(mode, MapNo, selectMapNo), "join: the host's snapshot lands over this copy where it stands", "CatchJoinTick", true);
            std::fprintf(stderr, "catchup: this copy is in the checkpoint's place already (mode %d, map %d, floor %d, building %d): it lands here\n", (int) mode,
                         (int) MapNo, UserStatus != nullptr ? (int) UserStatus->cur_floor : -1, CheckpointBuilding(g_solo.checkpoint));
            return false;
        }
        // coop-bldgsnap: in the checkpoint's town and building but not walking yet (her own door into it still opening, the
        // room's start event, a talk): she plays on until she walks there, then lands (w-town-apart -Case outback: linked as
        // she went back in, she left for the town and came in again by his door). DC_NETPLAY_MUTATE=snap_no_settle (twin).
        // (Not once the host sends the pads from tick 0 instead: those replay from the checkpoint's own load.)
        if (g_snap.expected && (g_catch.have_to <= 0 || g_snap.in_total > 0) && InCheckpointPlace() && !NetplayMutated("snap_no_settle") && !NetplayMutated("snap_land_reload")) {
            static std::int64_t said = -1;

            if (said != (std::int64_t) g_link.session) {
                said = (std::int64_t) g_link.session;
                std::fprintf(stderr, "catchup: this copy is in the checkpoint's place, not walking yet: it lands once it walks\n");
                g_catch.walk_wait_said = std::chrono::steady_clock::now();
            }
            // coop-pt1106-lockup (Jep's 2026-11-06 playtest, P1's menu open as his merge by her state linked: "locked up", no
            // dump): the link's up, so the game reads g_link.routed, Neutral from the link's start until the lockstep. What
            // ends by itself (a door, a start event) came to its walk; his menu waits for a press that never reached it, so
            // he never walked, never landed, and her Busy-fed catch-up ran on till he closed the game. He plays on with his
            // own pad till he lands. DC_NETPLAY_MUTATE=wait_pads_neutral (twin): Neutral, as before.
            g_catch.walk_wait = !NetplayMutated("wait_pads_neutral");
            if (std::chrono::steady_clock::now() - g_catch.walk_wait_said > std::chrono::seconds(10)) {
                g_catch.walk_wait_said = std::chrono::steady_clock::now();
                std::fprintf(stderr, "catchup: still not walking: mode %d, town mode %d, task %d, interior %d, event %d, have to tick %lld\n", (int) mode, (int) ::GameMode,
                             (int) gameTask, (int) EdInteriorFlag, (int) BtEventMode, (long long) g_catch.have_to);
            }
            // coop-localfix (a floor's long event or a knockout as he arrived, 2026-10-26: the host closed the link after
            // kLoadWaitMs of nothing heard): a Busy each second keeps the host waiting while this copy plays on to its landing.
            static auto busy_at = std::chrono::steady_clock::now();

            if (std::chrono::steady_clock::now() - busy_at > std::chrono::seconds(1) && !NetplayMutated("wait_no_busy")) {
                Busy busy{kBusyMagic, (std::uint32_t) g_link.player, g_link.session, 0};

                NetSocketSend(&busy, sizeof busy);
                busy_at = std::chrono::steady_clock::now();
            }
            g_solo.left = false;
            g_solo.due = false;
            return false;
        }
        LeaveForState(g_solo.checkpoint, g_snap.expected ? "join: catching up to the host's checkpoint (snapshot expected, this copy not in its zone)"
                                                        : "join: catching up to the host's checkpoint (its pads replay from there)", "CatchJoinTick");
        std::fprintf(stderr, "catchup: leaving for the checkpoint's map %lld\n", (long long) g_link.resync_target);
        return false;
    }

    if (!g_solo.on) {
        if (!g_solo.due || !(g_catch.land_here || AtCheckpointPlace())) {
            return false;
        }

        g_solo.on = true;
        g_floor_started = false;
        g_link.tick = 0;
        g_link.synced[0] = g_link.synced[1] = g_link.previous[0] = g_link.previous[1] = Neutral();
        g_link.menu_owner = 1;
        g_link.held = 0;
        std::fprintf(stderr, "catchup: replay from tick 0 in map %d%s\n", (int) MapNo, CheckpointFloor(g_solo.checkpoint) >= 0 ? " (the floor's start)" : "");

        // coop-localfix2: the random state the host's recording had at its tick 0 (SoloTick puts it in the checkpoint).
        if (g_solo.checkpoint.size() >= sizeof(ResyncHeader) && !NetplayMutated("tick0_rand_own")) {
            ResyncHeader header;

            std::memcpy(&header, g_solo.checkpoint.data(), sizeof header);
            if (header.rand != PortRandState()) {
                std::fprintf(stderr, "catchup: tick 0's random state is the host's %08x (this copy's was %08x)\n", header.rand, PortRandState());
                PortRandSetState(header.rand);
            }
        }

        // coop-housemerge: the bags home for tick 0 itself: the frames before it (apart, not yet replaying) had her bag in
        // the game's pack, and the worlds' tick puts it back only after this tick's play, so a gift on the host's first
        // tick (the mayor's, at his house's start) went into her bag here (house-after, 2026-10-25).
        // DC_NETPLAY_MUTATE=swapped_tick0 (twin): as before.
        if (!NetplayMutated("swapped_tick0")) {
            CoopWorldsBagsBack();
        }
        g_catch.replay_began = std::chrono::steady_clock::now();
        g_catch.replay_frame = dc::app::GameFrameCount();
        g_catch.replay_free = 0;
        g_catch.replay_wait_ms = g_catch.replay_rest_ms = 0.0;
        g_catch.replay_waits = g_catch.replay_ticks = 0;
        g_catch.replay_lines_said = g_catch.replay_readbacks = 0;
        g_catch.edge_at = {};
        g_catch.replay_left = {};
        g_stall_watch.replaying = true;
    }
    ReplayFree();
    StallWhere("the state set's wait");

    // coop-snapshot stage 4: while the host's state set is arriving or waits for the frame start that puts it back, no
    // tick plays (the pads from S would otherwise play from tick 0).
    auto snap_pending = [] {
        return g_snap.apply_due || (g_snap.in_total > 0 && g_snap.in_parts < g_snap.in_total && !g_snap.applied) ||
               ((g_snap.list_in || g_snap.asked_whole) && !g_snap.applied && !g_snap.said_refused); // coop-pagediff
    };

    if (snap_pending()) {
        return false;
    }

    StallWhere("the wait for pads");
    std::int64_t tick = g_link.tick;
    auto         start = std::chrono::steady_clock::now();
    auto         pumped = start;
    bool         waited = false;

    for (;;) {
        if (tick == g_catch.join_tick) {
            EnterLockstep(tick);
            return true;
        }

        if (snap_pending()) {
            return false;
        }

        if (tick < g_catch.have_to) {
            break;
        }

        auto now = std::chrono::steady_clock::now();

        waited = true;

        // (coop-inlinetwin test hook DC_NETPLAY_TEST_NO_PADS_WORD=1: in her merges after the first link no word out of pads,
        // as when he never runs out. Not the first link's: its lockstep is the test's mode.)
        static const bool no_pads_word = TestEnvOn("DC_NETPLAY_TEST_NO_PADS_WORD");

        if (now - g_catch.said > std::chrono::milliseconds(g_link.stayer_merge && !NetplayMutated("stayer_wide_gap") ? 8 : 100)) { // coop-localfix2: her merge waits on word of each tick he has
            if (!(g_link.stayer_merge && no_pads_word && g_links_up >= 2)) {
                SayCaught(tick);
            }
        }

        if (now - pumped > std::chrono::milliseconds(100)) {
            SDL_PumpEvents();
            pumped = now;

            if (SDL_HasEvent(SDL_EVENT_QUIT)) {
                Close("this copy is closing");
                return false;
            }
        }
        if (QaStopInWait()) {
            Close("this copy is stopping (the test's stop file)");
            return false;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        Receive();

        if (CatchLost()) {
            return false;
        }
    }

    if (waited) {
        g_catch.replay_waits++;
        g_catch.replay_wait_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    }
    // A word each 500 ms while replaying keeps the host hearing of this copy. Not in her merge (coop-lockgap, 2026-11-01): her
    // blobs' acknowledgements keep the link heard there, and a word from mid-replay (60-130 behind under load, his replay at
    // 64-100 ticks a second) was his first word past the state set's tick, so her close counted a run of 2-tick blobs before
    // the word that ends it (l-local-play -HouseOutCross under 15 other copies). His first word is now the one he says out of
    // pads. DC_NETPLAY_MUTATE=keepalive_word (twin): the word goes on in her merge, each 8 ms (the wait's own) so it lands
    // mid-replay at any load, not only when the replay outlasts 500 ms.
    // coop-inlinetwin (kEdgeWordTicks): in her merge, his word as he plays her newest pads. DC_NETPLAY_MUTATE=edge_word_late
    // (twin, coop-sendstop): only 5 s after he first plays them, so the merge still closes (with the test hook, nothing else
    // would) and the test's mode holds, but late.
    static const bool keepalive_word = NetplayMutated("keepalive_word");
    static const bool edge_word_late = NetplayMutated("edge_word_late");
    const auto        now = std::chrono::steady_clock::now();
    const bool        at_edge = g_link.stayer_merge && g_catch.have_to - tick <= kEdgeWordTicks;

    if (at_edge && g_catch.edge_at == std::chrono::steady_clock::time_point{}) {
        g_catch.edge_at = now;
    }
    const bool keepalive = !g_link.stayer_merge || keepalive_word || g_catch.restarts_asked > 0; // (coop-reds2: after an ask, each 50 ms)
    const bool edge = at_edge && (!edge_word_late || now - g_catch.edge_at >= std::chrono::seconds(5));

    if ((keepalive || edge) && now - g_catch.said > std::chrono::milliseconds(g_catch.restarts_asked > 0 ? 50 : g_link.stayer_merge ? 8 : 500)) {
        SayCaught(tick);
    }

    // coop-reds2 (kRestartMagic): his lag over the window; not gaining enough to close in kFallbackCloseMs, a fresh state set.
    static const bool fallback_off = NetplayMutated("no_replay_fallback");

    if (!fallback_off && g_snap.applied && g_catch.join_tick < 0 && !g_catch.restart_waiting) {
        const std::int64_t lag = g_catch.have_to - tick;

        if (g_catch.lag_at_from < 0) {
            g_catch.lag_from = now;
            g_catch.lag_at_from = lag;
        } else if (now - g_catch.lag_from >= std::chrono::milliseconds(kFallbackWindowMs)) {
            const double seconds = std::chrono::duration<double>(now - g_catch.lag_from).count();
            const double gain = (double) (g_catch.lag_at_from - lag) / seconds; // ticks a second
            const bool   closes = lag <= kFallbackGapTicks || (gain > 0.0 && (double) (lag - kFallbackGapTicks) / gain * 1000.0 <= kFallbackCloseMs);

            if (!closes) {
                g_catch.restart_waiting = true;
                g_catch.restarts_asked++;
                g_catch.restart_said = {};
                std::fprintf(stderr, "catchup: this copy's replay is not gaining on the sender: %lld ticks behind (%lld %.1f s ago, %.1f a second): asking for a fresh state set (fallback %d) [t %lld]\n",
                             (long long) lag, (long long) g_catch.lag_at_from, seconds, gain, g_catch.restarts_asked, LogMs());
            }
            g_catch.lag_from = now;
            g_catch.lag_at_from = lag;
        }
    }
    if (g_catch.restart_waiting && now - g_catch.restart_said > std::chrono::milliseconds(200)) {
        Busy ask{kRestartMagic, (std::uint32_t) g_link.player, g_link.session, (std::uint32_t) tick};

        NetSocketSend(&ask, sizeof ask);
        g_catch.restart_said = now;
    }

    // coop-loadreds (test-time plan WS5, train 52: l-local-play-houseout~keepalive passed under load, its mutation unseen): test
    // hook DC_NETPLAY_TEST_REPLAY_TPS=N holds his replay to N ticks a second on a fixed clock, the pace of a busy PC (64-100 a
    // second, the comment above). Unheld, his replay of a fixture's merge is some 12 ticks in 0.1-0.2 s, and whether a word from
    // mid-replay costs her a third blob went by the PC's timing: 3 alone (the twin's FAIL), 2 beside other copies. N stays over
    // her 60 a second, or his replay never reaches her.
    static const int replay_tps = TestEnvInt("DC_NETPLAY_TEST_REPLAY_TPS", 0);

    StallWhere("the replay pace hook");
    const auto hook_from = std::chrono::steady_clock::now();

    if (replay_tps > 0) {
        static std::chrono::steady_clock::time_point due{};
        const auto                                   at = std::chrono::steady_clock::now();

        if (at - due > std::chrono::seconds(1)) { // (a new replay)
            due = at;
        }
        std::this_thread::sleep_until(due);
        due += std::chrono::microseconds(1000000 / replay_tps);
    }

    g_stall_watch.hook_ms += SinceMs(hook_from);
    g_stall_watch.hook_most = std::max(g_stall_watch.hook_most, SinceMs(hook_from));
    StallWhere("SoloTick");
    const auto solo_from = std::chrono::steady_clock::now();

    g_catch.replay_ticks++;
    SoloTick();
    g_stall_watch.solo_ms += SinceMs(solo_from);
    g_stall_watch.solo_most = std::max(g_stall_watch.solo_most, SinceMs(solo_from));
    g_stall_watch.ticks++;
    StallWhere("past the replay tick");
    g_last_tick_ms = NowMs();
    g_catch.replay_left = std::chrono::steady_clock::now();
    return false;
}

} // namespace

// The joiner replaying the host's pads with more held: its ticks skip the wait for the next display boundary.
bool NetplayCatchUnderWay() {
    return g_catch.active;
}

// coop-pt1106-lockup: in the catch-up's place, playing on till it walks and lands (the test hook's clock; its twin's too).
bool NetplayCatchWaitsToWalk() {
    return g_catch.active && !g_solo.on && g_catch.walk_wait_said != std::chrono::steady_clock::time_point{};
}

// DC_NETPLAY_MUTATE=catch_log_paused (twin): moving for the whole catch-up (the partner log waits it out), as before.
bool NetplayCatchMoving() {
    constexpr int kCatchMovingMs = 500;

    if (!g_catch.active) {
        return false;
    }
    if (NetplayMutated("catch_log_paused")) {
        return true;
    }
    if (SendsState()) {
        return !g_catch.out.empty() && !CatchOutHeld();
    }
    // (coop-small1: a blob's pieces reach the game thread whole, so the receiver thread's last piece counts too)
    return std::chrono::steady_clock::now() - g_catch.piece_at < std::chrono::milliseconds(kCatchMovingMs) ||
           (CatchReceiverOn() && NowMs() - CatchReceiverPieceMs() < kCatchMovingMs);
}

bool NetplaySnapshotApplied() {
    return TakesState() && g_catch.active && g_snap.applied;
}

bool NetplaySnapshotAvailable() {
    return SnapshotLayoutPinned() && !NetplayMutated("no_snapshot");
}

// coop-linkrace: the session the other copy's last Hello gave (0: none heard yet), for coop-worlds' test hooks.
std::uint32_t NetplayPeerHelloSession() {
    return g_link.peer_hello.session;
}

// coop-stayerhello (train 49, x-floor-join-reverse-latefall every run): whether the stayer bit this copy's Hellos give is
// the one it has now. Her copy, alone on the floor first, became the stayer only as his presence said he arrived (there
// first), and the merge started in that frame: her last Hello still said 0, both copies went by it (HeardHello), and his
// state reloaded hers. Not said yet: a Hello saying it goes now, ahead of the ready presence that MergeReady's caller sends
// (the same socket, so the other copy holds it before it can link on that presence). No frame is held: a merge held back
// let his copy play on past its arrival into its next floor event, and x-floor-join-reverse-start parted (OUT OF SYNC).
// DC_NETPLAY_MUTATE=merge_before_stayer_hello (twin): none, and apart the half-second Hellos keep a bit that became 1 out (Listen).
// coop-areacard (option 5, Jep 2026-11-09): the arriver waited 0.2-0.7 s with both copies ready before its merge began. The
// cause: a copy starts the merge's link only in a frame in which a Hello of the other's came (peer_hello_got is that frame's
// alone), and Hellos go on a half-second clock; the ready word goes by presence, at once. So both copies ready, the link
// waited for the other's next timed Hello: 0-500 ms, and a second wait of up to 500 ms when the host was ready last (her
// copy links only on the host's link, the host only on her Hello). Now a copy whose ready word changes, or which hears the
// other's turn ready while it is, sends a Hello at once (on the next listening pass), so the other links on it.
// DC_NETPLAY_MUTATE=ready_hello_timed (twin): the timed Hellos alone, as before.
void NetplayHelloNow() {
    if (g_link.up || NetplayMutated("ready_hello_timed")) {
        return;
    }
    if (!g_hello_asked && CoopDebug()) {
        NetplayTiming("a Hello goes now (the merge's ready word changed)");
    }
    g_hello_asked = true;
    g_link.hello_due = std::chrono::steady_clock::now();
}

void NetplayHelloSaysStayer() {
    if (g_link.up || NetplayMutated("merge_before_stayer_hello")) {
        return;
    }

    NetplayHello hello = HelloMake(false);

    if (g_link.sent_hello_set && g_link.sent_hello.session == hello.session && g_link.sent_hello.stayer == hello.stayer) {
        return;
    }

    std::fprintf(stderr, "netplay: this copy's stayer bit is now %d, its last Hello said %d: a Hello says so before the merge\n", (int) hello.stayer,
                 g_link.sent_hello_set ? (int) g_link.sent_hello.stayer : -1);
    NetSocketSend(&hello, sizeof hello);
    g_link.sent_hello = hello;
    g_link.sent_hello_set = true;
    g_link.hello_due = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
}

bool NetplayPeerFirstLink() {
    return !g_link.up && g_link.peer_hello_got && g_link.peer_hello.first_link != 0;
}

bool NetplayPeerLinking() {
    // Test hook (DC_NETPLAY_TEST_PEER_LINKING_WINDOW_S=N): the window is N s, not 2 (w-world-split -LinkSecond: a split
    // however long after the link is one inside the window, so a stamp the split kept is always still in it).
    static const int window_s = TestEnvInt("DC_NETPLAY_TEST_PEER_LINKING_WINDOW_S", 2);

    if (g_link.up || g_link.peer_linking_at == std::chrono::steady_clock::time_point{} ||
        std::chrono::steady_clock::now() - g_link.peer_linking_at >= std::chrono::seconds(window_s)) {
        return false;
    }

    // coop-linkrace (natsplitflake's open bug 1): the stamp is the traffic that came before this copy's own Begin, under
    // the session this copy then linked with; a split under 2 s after that link read it as the other copy starting the
    // merge's link, and this copy linked alone. Traffic of the session last linked with is that link's, never a new one.
    // DC_NETPLAY_MUTATE=stale_peer_linking (twin): it counts, as before.
    const bool stale = g_link.peer_linking_session == g_link.last_peer_session;
    static std::uint32_t said_session = 0;
    static bool          said_stale = false;

    if (said_session != g_link.peer_linking_session || said_stale != stale) {
        said_session = g_link.peer_linking_session;
        said_stale = stale;
        std::fprintf(stderr, stale && !NetplayMutated("stale_peer_linking")
                                 ? "netplay: the other copy's link traffic of session %08x is the last link's: not a new link of its\n"
                                 : "netplay: the other copy counts as linking: its link traffic of session %08x%s\n",
                     (unsigned) g_link.peer_linking_session, stale ? " (the last link's)" : "");
    }

    return !stale || NetplayMutated("stale_peer_linking");
}

bool NetplayCatchingUp() {
    // Test hook (DC_NETPLAY_TEST_SLOW_LAST_BLOB=1): the last blob (J known) replays at the game's own pace, as a slow PC's
    // would, so the host's first lockstep ticks, and a split among them, come while this copy is still replaying
    // (w-world-split -Case merge-catchup -SplitAfterJoin, coop-leaverace).
    static const bool slow_last = TestEnvOn("DC_NETPLAY_TEST_SLOW_LAST_BLOB");

    return g_catch.active && TakesState() && g_solo.on && g_link.tick + 1 < g_catch.have_to && !NetplayMutated("slow_catchup") &&
           !(slow_last && g_catch.join_tick >= 0);
}

bool NetplayHoldsJoinFrame() {
    if (!TakesState() || !g_link.up) {
        return false;
    }

    const bool before = g_link.awaiting_sync && g_join_hold.state_tick < 0;
    const bool leaving = g_join_hold.state_tick >= 0 && g_link.tick <= g_join_hold.state_tick + 1;

    if (!before && !leaving) {
        if (g_join_hold.state_tick >= 0 && !g_join_hold.said) {
            g_join_hold.said = true;
            std::fprintf(stderr, "netplay: before the host's first state (taken at tick %lld) this copy held its frame %d ticks, showed %d\n",
                         (long long) g_join_hold.state_tick, g_join_hold.held, g_join_hold.shown);
        }

        return false;
    }

    const bool hold = !NetplayMutated("show_join");

    (hold ? g_join_hold.held : g_join_hold.shown)++;
    return hold;
}

bool NetplayReplaying() {
    return g_catch.active && TakesState() && g_solo.on;
}

// coop-atlamenu (Jep's 2026-11-04 pack 8 playtest: "P2 can't open menu while P1 is in long loading screen" after he came back
// to her town): the catch-up's ticks that both copies step: the sender's from its state set's take on (all of them when the
// pads go from tick 0), the taker's as it replays them after the set (or from tick 0). The pair is still apart in them
// (CoopWorldsTick waits for the lockstep), so what goes by the pair's link alone (her own town menu and pause) may run there
// too, the same on both copies. The sender's ticks before the take are in no replay: the set carries their end alone.
bool NetplayCatchupStepsAlike() {
    if (!g_link.up || !g_catch.active || !g_solo.on) {
        return false;
    }
    if (SendsState()) {
        return !g_solo.replay && (!g_snap.on || g_snap.taken || g_snap.refused);
    }
    return g_snap.applied || !g_snap.expected || g_snap.said_refused;
}

// The controllers SDL offers (vendor:product and name), logged when they change: a Steam virtual pad (28de:11ff) means
// Steam Input has taken the real one (Jep's 2026-10-21 Steam playtest: neither pad played).
// Test hook (DC_NETPLAY_STEAM_PAD=FROM,TO): an SDL virtual gamepad posing as Steam's virtual pad (28de:11ff), holding Cross
// from link tick FROM to TO, so a test sees whether SDL lets a Steam virtual pad play (the launcher's
// SDL_GAMECONTROLLER_ALLOW_STEAM_VIRTUAL_GAMEPAD).
void FakeSteamPad() {
    static const char    *spec = TestEnv("DC_NETPLAY_STEAM_PAD");
    static SDL_Joystick *pad = nullptr;
    static bool          tried = false;

    if (spec == nullptr) {
        return;
    }

    if (!tried) {
        tried = true;

        // DC_NETPLAY_TWO_PADS=1: an idle pad (1234:5678) first, as on Jep's host PC, so the pressed pad
        // is the second.
        // (DC_NETPLAY_TWO_PADS=N: N idle pads first.)
        for (int idle_no = 0; idle_no < TestEnvInt("DC_NETPLAY_TWO_PADS", 0); idle_no++) {
            SDL_VirtualJoystickDesc idle;

            SDL_INIT_INTERFACE(&idle);
            idle.type = SDL_JOYSTICK_TYPE_GAMEPAD;
            idle.vendor_id = 0x1234; // (not 045e:02ff: the test lets only its own fake pads through, never this PC's real ones)
            idle.product_id = 0x5678;
            idle.naxes = SDL_GAMEPAD_AXIS_COUNT;
            idle.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
            idle.name = "Xbox One Controller (test, idle)";
            SDL_JoystickID first = SDL_AttachVirtualJoystick(&idle);
            SDL_Joystick *held_open = first != 0 ? SDL_OpenJoystick(first) : nullptr; // (held open for the run)
            (void) held_open;
            std::fprintf(stderr, "netplay: test: idle first pad %s\n", first != 0 ? "attached" : SDL_GetError());
        }

        SDL_VirtualJoystickDesc desc;

        SDL_INIT_INTERFACE(&desc);
        desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
        desc.vendor_id = 0x28DE;
        desc.product_id = 0x11FF;
        desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
        desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
        desc.name = "Steam Virtual Gamepad (test)";
        SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
        pad = id != 0 ? SDL_OpenJoystick(id) : nullptr;
        std::fprintf(stderr, "netplay: test: fake Steam virtual pad %s\n", pad != nullptr ? "attached" : SDL_GetError());
    }

    // DC_NETPLAY_STEAM_PAD_ALONE=1: from..to counts this copy's frames, linked or not, and the pad also holds its left
    // stick right (a host waiting alone in a town walks with it).
    static const bool alone = TestEnvOn("DC_NETPLAY_STEAM_PAD_ALONE");
    static long long  frames = 0;

    frames++;

    if (pad != nullptr && (g_link.up || alone)) {
        static const long long from = std::atoll(spec);
        static const long long to = std::strchr(spec, ',') != nullptr ? std::atoll(std::strchr(spec, ',') + 1) : from + 600;
        const long long        at = alone ? frames : g_link.tick;

        // (Alone: Cross for the window's first 10 frames, a press to claim a pad with, then the stick only.)
        SDL_SetJoystickVirtualButton(pad, SDL_GAMEPAD_BUTTON_SOUTH, at >= from && at < (alone ? std::min(to, from + 10) : to));
        if (alone) {
            SDL_SetJoystickVirtualAxis(pad, SDL_GAMEPAD_AXIS_LEFTX, at >= from && at < to ? SDL_JOYSTICK_AXIS_MAX : 0);
        }
    }
}

void LogPads() {
    FakeSteamPad();

    static int         every = 0;
    static std::string last = "(not yet)"; // so the first look is logged, pads or none

    if (every-- > 0) {
        return;
    }
    every = 120;

    int             count = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&count);
    std::string     now;

    for (int i = 0; ids != nullptr && i < count; i++) {
        char        one[160];
        const char *name = SDL_GetGamepadNameForID(ids[i]);

        std::snprintf(one, sizeof one, "%s%04x:%04x %s", i ? "; " : "", SDL_GetGamepadVendorForID(ids[i]), SDL_GetGamepadProductForID(ids[i]),
                      name != nullptr ? name : "?");
        now += one;
    }
    SDL_free(ids);

    if (now != last) {
        last = now;
        std::fprintf(stderr, "netplay: pads: %s (raw HID: hidapi_xbox=%s rawinput=%s)\n", now.empty() ? "none" : now.c_str(),
                     SDL_GetHint(SDL_HINT_JOYSTICK_HIDAPI_XBOX) != nullptr ? SDL_GetHint(SDL_HINT_JOYSTICK_HIDAPI_XBOX) : "default",
                     SDL_GetHint(SDL_HINT_JOYSTICK_RAWINPUT) != nullptr ? SDL_GetHint(SDL_HINT_JOYSTICK_RAWINPUT) : "default");
    }
}

// Steam Input: while Steam owns the controllers (this copy runs as Spacewar), the pad Steam reads for us plays as the
// first pad whenever something on it is pressed; otherwise SDL's pads and the keyboard do, as always.
void SteamPadOverride() {
    static bool          set = false;
    static bool          said = false;
    static InputPadState state;
    NetSteamPadState     pad;

    if (NetSteamPad(pad) && pad.input) {
        state = InputPadState{};
        state.connected = true;
        state.buttons = pad.buttons;
        state.left_x = pad.left_x;
        state.left_y = pad.left_y;
        state.right_x = pad.right_x;
        state.right_y = pad.right_y;
        InputSetOverride(0, &state);
        set = true;

        if (!said) {
            said = true;
            std::fprintf(stderr, "netplay: steam input plays the first pad (buttons %04x)\n", pad.buttons);
        }
    } else if (set) {
        InputSetOverride(0, nullptr);
        set = false;
    }
}

void NatSteamTick();
void PunchLogTick();

// coop-snapshot stage 4: the co-op modules' heap objects the snapshot carries beside the state set, by id: the partner's
// camera (coop_camera.cpp; the lockstep's checksum reads its angle).
constexpr std::uint64_t kExtraPartnerCamera = 0xC0000001u;
constexpr std::uint64_t kExtraTownPlace = 0xC0000002u; // coop-localfix: her town place (CoopTownSyncBytes)
constexpr std::uint64_t kExtraLinkPads = 0xC0000003u;  // coop-splashmove: the link's pads as the set was taken (SnapPads)
constexpr std::uint64_t kExtraOpenGates = 0xC0000004u; // coop-gateopen: the floors whose gate is open (coop::OpenGatesBytes)
constexpr std::uint64_t kExtraTownPause = 0xC0000005u; // coop-mergecam: her town pause (CoopP2TownPauseBytes)
constexpr std::uint64_t kExtraHerGeorama = 0xC0000006u; // coop-stage4 r4: her own Georama (CoopGeoramaSyncBytes)
constexpr std::uint64_t kExtraSwapDefer = 0xC0000007u;  // coop-stage4 4e part 2: her event's due swap (CoopTownSwapDeferBytes)
constexpr std::uint64_t kExtraTalks = 0xC0000008u;      // opt8: every player's talk slot (CoopTownTalksBytes)
constexpr std::uint64_t kExtraScriptMenu = 0xC0000009u; // coop-small2 (9 in train 56: opt8 has 8): a script's menu open, carried local (CoopScriptMenuSyncBytes)
constexpr std::uint64_t kExtraQuests = 0xC000000Au;     // coop-pt1111e: the quests' progress (QuestsPartTake's text, its length first)
constexpr std::uint64_t kExtraDialog = 0xC000000Bu;     // coop-pt1111e: the dialog framework's sessions and windows (SnapDialogFill)

// coop-splashmove (Jep's 2026-10-26 case: Xiao walking about Norune while his copy showed its splash): the pads the link
// routed on the tick before the state set's. Her town step reads the routed pads at the frame's start, before this tick's
// are routed, so the first tick after the set steps her by the sender's last pad; the taker had them at rest and put her
// a step short, on his copy only, for good (the checksum does not see her place). DC_NETPLAY_MUTATE=snap_pads_rest (twin):
// at rest, as before.
struct SnapPads {
    InputPadState synced[2];
    InputPadState previous[2];
    InputPadState routed[2];
} g_snap_pads;

// coop-pt1111e: the quests' progress with the state set (a merge's take, a state file): its text, its length in the first 4 bytes.
// Filled from this copy's own each time the list is made, so a set without it (an older state file) puts back what this copy had.
// DC_NETPLAY_MUTATE=snap_quests_lost (twin, x-quest-apart -AtTake): not read back after the apply (the taker keeps its own).
unsigned char g_snap_quests[16384];

void SnapQuestsFill() {
    std::vector<unsigned char> text;
    QuestsPartTake(text);
    std::uint32_t size = text.size() + 4 <= sizeof g_snap_quests ? (std::uint32_t) text.size() : 0u;

    if (size == 0 && !text.empty()) {
        std::fprintf(stderr, "catchup: the quests' progress (%zu bytes) is more than the set's room for it (%zu): not carried\n", text.size(),
                     sizeof g_snap_quests - 4);
    }
    std::memset(g_snap_quests, 0, sizeof g_snap_quests);
    std::memcpy(g_snap_quests, &size, 4);
    std::memcpy(g_snap_quests + 4, text.data(), size);
}

void SnapQuestsApply() {
    std::uint32_t size = 0;

    std::memcpy(&size, g_snap_quests, 4);
    if (size == 0 || size + 4 > sizeof g_snap_quests || NetplayMutated("snap_quests_lost")) {
        return;
    }
    QuestsPartPut(g_snap_quests + 4, size);
    std::fprintf(stderr, "catchup: the state set carries the quests' progress (%u bytes)\n", size);
}

// coop-pt1111e: the dialog framework's sessions (the open one and each talker's slot) and their windows with the state set, as data
// (dialog::WriteSessions, DialogNpcWriteWindows): the sessions' size and bytes, then the windows' size and bytes. Filled from this
// copy's own each time the list is made, as the quests'. DC_NETPLAY_MUTATE=dialog_session_not_carried (twin): not read back after
// the apply (the taker keeps its own, as before: her talk's dialog open with no session).
unsigned char g_snap_dialog[65536];

void SnapDialogFill() {
    std::vector<uint8_t> sessions;
    std::string          windows;
    std::string          shop; // (coop-pt1111e: a quest's shop open, custom_shop's g_open, mid-visit)

    dc::dialog::WriteSessions(sessions);
    dc::dialog::DialogNpcWriteWindows(windows);
    dc::quest::CustomShopWrite(shop);
    std::memset(g_snap_dialog, 0, sizeof g_snap_dialog);
    if (12 + sessions.size() + windows.size() + shop.size() > sizeof g_snap_dialog) {
        std::fprintf(stderr, "catchup: the dialog sessions (%zu + %zu + %zu bytes) are more than the set's room for them: not carried\n", sessions.size(),
                     windows.size(), shop.size());
        return;
    }
    std::size_t at = 0;
    for (const std::string_view part : {std::string_view(reinterpret_cast<const char *>(sessions.data()), sessions.size()), std::string_view(windows),
                                        std::string_view(shop)}) {
        const std::uint32_t size = (std::uint32_t) part.size();

        std::memcpy(g_snap_dialog + at, &size, 4);
        std::memcpy(g_snap_dialog + at + 4, part.data(), part.size());
        at += 4 + part.size();
    }
}

void SnapDialogApply() {
    std::string_view parts[3];
    std::size_t      at = 0;

    for (std::string_view &part : parts) {
        std::uint32_t size = 0;

        if (at + 4 > sizeof g_snap_dialog) {
            return;
        }
        std::memcpy(&size, g_snap_dialog + at, 4);
        if (at + 4 + (std::size_t) size > sizeof g_snap_dialog) {
            return;
        }
        part = std::string_view(reinterpret_cast<const char *>(g_snap_dialog + at + 4), size);
        at += 4 + size;
    }
    if (parts[0].empty() || NetplayMutated("dialog_session_not_carried")) {
        return;
    }
    std::string why;

    if (!dc::dialog::ReadSessions(reinterpret_cast<const uint8_t *>(parts[0].data()), parts[0].size(), why)) {
        std::fprintf(stderr, "catchup: LINT: the state set's dialog sessions do not read here (%s): closed\n", why.c_str());
        return;
    }
    if (!dc::dialog::DialogNpcReadWindows(parts[1].data(), parts[1].size())) {
        std::fprintf(stderr, "catchup: the state set's dialog windows do not read here\n");
        return;
    }
    if (dc::dialog::IsSessionActive()) {
        std::fprintf(stderr, "catchup: the state set carries the open dialog session (%zu bytes, its choices bound again here)\n", parts[0].size());
    }
    // coop-pt1111e: a quest's shop open as the set was taken (its goods, keeper, lines and the prices it laid over), so its close
    // puts retail's prices back here too. DC_NETPLAY_MUTATE=quest_shop_not_carried (twin): not read back (the taker has none).
    if (!NetplayMutated("quest_shop_not_carried")) {
        if (!dc::quest::CustomShopRead(parts[2])) {
            std::fprintf(stderr, "catchup: the state set's quest shop does not read here\n");
        } else if (!parts[2].empty()) {
            std::fprintf(stderr, "catchup: the state set carries the open quest shop (%zu bytes)\n", parts[2].size());
        }
    }
}

std::vector<SnapshotExtra> SnapExtras() {
    std::vector<SnapshotExtra> extras;

    SnapQuestsFill(); // coop-pt1111e
    extras.push_back({kExtraQuests, g_snap_quests, sizeof g_snap_quests});
    SnapDialogFill(); // coop-pt1111e
    extras.push_back({kExtraDialog, g_snap_dialog, sizeof g_snap_dialog});
    CCameraFollow             *camera = coop::CoopCameraObject();

    extras.push_back({kExtraPartnerCamera, reinterpret_cast<unsigned char *>(camera), sizeof *camera});
    std::size_t    town_size = 0;
    unsigned char *town = CoopTownSyncBytes(&town_size);

    extras.push_back({kExtraTownPlace, town, town_size});
    extras.push_back({kExtraLinkPads, reinterpret_cast<unsigned char *>(&g_snap_pads), sizeof g_snap_pads});
    std::size_t    pause_size = 0;
    unsigned char *pause = CoopP2TownPauseBytes(&pause_size);

    extras.push_back({kExtraTownPause, pause, pause_size});
    std::size_t    georama_size = 0;
    unsigned char *georama = CoopGeoramaSyncBytes(&georama_size);

    extras.push_back({kExtraHerGeorama, georama, georama_size});
    std::size_t    defer_size = 0;
    unsigned char *defer = CoopTownSwapDeferBytes(&defer_size);

    extras.push_back({kExtraSwapDefer, defer, defer_size});
    // opt8: every player's talk slot (a state set taken mid-talk: a merge's, a state file's). DC_NETPLAY_MUTATE=snap_talks_lost (twin): the
    // slots emptied once the set is put back (StateSetApplyCore), as a set without them: the talk under way is lost there.
    {
        std::size_t    talks_size = 0;
        unsigned char *talks = CoopTownTalksBytes(&talks_size);

        extras.push_back({kExtraTalks, talks, talks_size});
    }
    std::size_t    script_size = 0;
    unsigned char *script = CoopScriptMenuSyncBytes(&script_size);

    extras.push_back({kExtraScriptMenu, script, script_size});
    // coop-gateopen: DC_NETPLAY_MUTATE=snap_no_gates (twin): not carried, the taker keeps its own.
    if (!NetplayMutated("snap_no_gates")) {
        std::size_t    gates_size = 0;
        unsigned char *gates = coop::OpenGatesBytes(&gates_size);

        extras.push_back({kExtraOpenGates, gates, gates_size});
    }
    return extras;
}

// coop-snapshot stage 4: the state set put back over this copy (the catch-up's apply; coop-stateload's file load too).
// The co-op partner (coop::g_partner, in the state set for its fields) owns objects on this copy's C heap (its character
// and weapon are new'd, its packs and models loaded here): those pointers stay this copy's. The host had no partner loaded
// while it recorded alone, so the objects' contents carry nothing (the lockstep's start loads her anew).
bool g_state_file_applying = false; // coop-fx51c: a state file (a fixture) is being put back (StateFileApply)

bool StateSetApplyCore(const std::vector<unsigned char> &unpacked, std::string &why, std::size_t *regions, std::size_t *bytes) {
    coop::Partner &partner = coop::g_partner;
    CCharacter    *own_chara = partner.chara, *own_weapon = partner.weapon;
    CTexAnimeData *own_tex_anime = partner.tex_anime;
    u_int         *own_pack = partner.pack, *own_ex_pack = partner.ex_pack;
    u_char        *own_model = partner.model, *own_ex_model = partner.ex_model;

    coop::CoopCameraSnapshotBefore();
    coop::AudioSnapshotBefore(); // coop-pt1109c: the sound driver's track, before the set's words land
    PortTextureRebuildBefore(); // coop-floorjoin2
    CoopGeoramaSyncClear(); // coop-stage4 r4: a set without her Georama leaves this copy's as it is
    std::size_t   menu_held = 0, menu_total = 0; // coop-offthread: this copy's own menu across the apply
    std::uint32_t menu_before = 0, menu_after = 0;

    coop::MenuWorkCensus(&menu_held, &menu_total, &menu_before);
    CoopScriptMenuSyncClear(); // coop-small2: a set without a script's menu open leaves this copy's as it is
    // coop-pt1111m: DC_COOP_MUTATE=menusoff_menu_out (twin): a link's apply leaves this copy's own menu working globals, as since e57647e7
    std::vector<unsigned char> menu_own;
    const bool                 menu_kept = !g_state_file_applying && coop::MenuWorkInSet() && coop::MenuWorkKeptMutated();

    if (menu_kept) {
        coop::MenuWorkSave(menu_own);
    }
    const bool applied = SnapshotApplyBytes(unpacked.data(), unpacked.size(), why, regions, bytes, SnapExtras());
    if (menu_kept) {
        coop::MenuWorkRestore(menu_own);
    }
    coop::MenuWorkCensus(&menu_held, &menu_total, &menu_after);
    // coop-pt1111m (x-town-stayermenu-*-menusoff reads it against the sender's take line): with the menus in the lockstep the taker
    // steps the sender's menus from here, on the sender's menu state
    if (!g_state_file_applying && coop::MenuWorkInSet()) {
        std::fprintf(stderr, "catchup: the menus are in the lockstep (game.coop_menus off): the menu's working globals after the apply %08x%s\n", coop::MenuWorkHash(),
                     menu_kept ? " (test: this copy's own kept)" : "");
    }
    // (x-town-stayermenu reads it: his menu open as she takes his set, her menu's globals stay hers)
    std::fprintf(stderr, "catchup: the state set holds %zu of the menu's %zu working globals; this copy's others across the apply: %08x before, %08x after\n",
                 menu_held, menu_total, menu_before, menu_after);
    if (applied) {
        SnapQuestsApply(); // coop-pt1111e
        SnapDialogApply();
    }
    if (applied && NetplayMutated("snap_talks_lost")) { // opt8 (twin, SnapExtras)
        std::size_t talks_size = 0;
        std::memset(CoopTownTalksBytes(&talks_size), 0, talks_size);
    }
    if (applied) { // opt8: the talks the set carried (x-town-talk-saved's line)
        for (int k = 0; k < coop::kMaxPlayers; k++) {
            if (CoopTalkOn(k)) {
                std::fprintf(stderr, "coop-talk: the state set carries player %d's talk\n", k + 1);
            }
        }
    }
    coop::CoopCameraSnapshotAfter();
    // coop-fx51c: DC_NETPLAY_MUTATE=file_town_unplaced (twin of fixture-recipe-proof-towndoor): a state file's town place, camera
    // and pause for her left out, as no file took them before (StateFileTakeParts): she is put away and placed beside Toan.
    if (g_state_file_applying && NetplayMutated("file_town_unplaced")) {
        std::size_t          size = 0;
        unsigned char *const sync = CoopTownSyncBytes(&size);

        std::memset(sync, 0, size);
    }
    CoopTownSyncApply(); // coop-localfix: her town place, the host's
    CoopP2TownPauseApply(); // coop-mergecam: her pause, the sender's
    CoopGeoramaSyncApply(); // coop-stage4 r4: her own Georama, the sender's
    CoopScriptMenuSyncApply(); // coop-small2: the sender's script menu, open and local: this copy waits for its end
    if (TestEnv("DC_NETPLAY_TEST_TICK_LINE") != nullptr) { // coop-houseexit (tests): as the take's line, on the copy that took it
        std::fprintf(stderr, "tickline: the state set's apply at tick %lld\n", (long long) g_link.tick);
        CoopTownCamTestLine(-2);
    }

    partner.chara = own_chara;
    partner.weapon = own_weapon;
    partner.tex_anime = own_tex_anime;
    partner.pack = own_pack;
    partner.ex_pack = own_ex_pack;
    partner.model = own_model;
    partner.ex_model = own_ex_model;
    return applied;
}

// After a state set's apply: this copy's own textures, draw blocks and partner body made again over it.
// coop-floorjoin2 (Jep's 2026-10-25 playtest, note 36: "after that just had a black screen"): the game's texture table came
// with the state set, naming the sender's TBP0 and CBP keys, while the port's registry (not in the set) still held this
// copy's own: every draw missed. The registry is made again from the table and the staged pixels
// (PortTextureRebuildAfterApply). DC_NETPLAY_MUTATE=stale_texture_keys (x-floor-join -Case late's second twin): the
// registry as it was. coop-localfix (Jep's 2026-10-26: "Xiao is still invisible on P2s screen but visible on P1s"): the
// sender's partner textures out of the table before the registry is made again, and her town model loaded again after it.
void StateSetAfterApply() {
    if (mode == GAME_MODE_EDIT) {
        CoopTownSnapshotTexturesOut();
    } else if (mode == GAME_MODE_DUNGEON) {
        coop::PartnerSnapshotTexturesOut(); // coop-joinvisuals: the floor's, likewise
    }
    // repin (Chronicle 9418a550's Enhanced cloth): what this copy's cloths settled before is keyed by addresses the state set
    // has just filled with the sender's cloths; each settles again (draw-only: cloth feeds no tick, no checksum).
    ClothForgetSettled();

    if (!NetplayMutated("stale_texture_keys")) {
        // (coop-provenance: the rebuild decodes only pixels the state set carried, by its own rule: texture.cpp's carried;
        // DC_NETPLAY_MUTATE=carried_near, the class check's fix-off, puts its old 1 GB guess back, said here)
        const PortTextureRebuildReport r = PortTextureRebuildAfterApply();

        if (NetplayMutated("carried_near")) {
            std::fprintf(stderr, "catchup: test: the texture rebuild took pixels within 1 GB of the manager as carried (carried_near)\n");
        }

        std::fprintf(stderr, "catchup: textures re-keyed after the apply: %d images (%d palettes), %d placeholders, %d moved, %d skipped\n",
                     r.images, r.palettes, r.targets, r.moved, r.skipped);
        // coop-joinatla (diagnostic, DC_COOP_TEXLOG=1): every texture slot the table holds now, and where its pixels are.
        if (TestEnv("DC_COOP_TEXLOG") != nullptr) {
            for (int i = 0; i < 196; i++) {
                const CTexture &tex = TexManager.textures[i];

                if (tex.name[0] == 0) {
                    continue;
                }
                const auto at = (std::uintptr_t) tex.image[0], base = (std::uintptr_t) read_buffer;

                std::fprintf(stderr, "texlog: slot %d \"%s\" block %d %dx%d bpp %d image %p clut %p arena %d read+%lld\n", i, tex.name,
                             (int) tex.block, (int) tex.width, (int) tex.height, (int) tex.bpp, (void *) tex.image[0], (void *) tex.clut,
                             ArenaContains(tex.image[0]) ? 1 : 0, at >= base ? (long long) (at - base) : -1LL);
            }
        }
    }
    dc::render::Draw3DStateSetApplied(); // coop-pond: each draw record checked once against its model's bytes (a town's dug ground)
    coop::DrawBlocksSnapshotApplied(); // coop-keydoor: the mod's own draw blocks load again from this copy's files
    if (mode == GAME_MODE_EDIT) {
        CoopTownSnapshotApplied();
    } else if (mode == GAME_MODE_DUNGEON) {
        coop::PartnerSnapshotApplied();
    }
    coop::AudioSnapshotApplied(); // coop-pt1109c: the set's music on this copy's sound driver
}

// coop-joinfix test hook (DC_NETPLAY_TEST_FOREIGN_CLUT=1, x-floor-join -ForeignClut; item 37b): the taking copy, on a floor,
// moves one block texture's palette into a buffer of its own heap within 1 GB of the texture manager, as Xiao's slingshot's
// was in Jep's B4 join, before it takes the state set. The table then names an address the taker never sent: the rebuild
// after the apply must make that texture a placeholder, not decode it (its twin carried_near faulted there).
static void ForeignClutTestTick() {
    static const bool want = TestEnv("DC_NETPLAY_TEST_FOREIGN_CLUT") != nullptr;
    static bool       done = false;

    if (!want || done || mode != GAME_MODE_DUNGEON) {
        return;
    }
    done = true;
    for (int i = 0; i < 196; i++) {
        CTexture &tex = TexManager.textures[i];

        if (tex.name[0] == 0 || tex.block < 0 || tex.block >= 72 || tex.clut == nullptr || !SnapshotStateHolds(tex.clut)) {
            continue;
        }
        const std::size_t bytes = tex.bpp == 0 ? 16 * 4 : 256 * 4;
        void             *buffer = SnapshotTestForeignBuffer(&TexManager, 0x10000);

        if (buffer == nullptr) {
            std::fprintf(stderr, "netplay: test: foreign palette: no free range within 1 GB of the texture manager\n");
            return;
        }
        std::memcpy(buffer, tex.clut, bytes);
        tex.clut = (decltype(tex.clut)) buffer;
        std::fprintf(stderr, "netplay: test: foreign palette: slot %d \"%s\" palette moved to %p (%lld MB from the texture manager, outside the state set)\n",
                     i, tex.name, buffer, (long long) (((std::uintptr_t) &TexManager - (std::uintptr_t) buffer) >> 20));
        return;
    }
    std::fprintf(stderr, "netplay: test: foreign palette: no block texture with a palette in the state set\n");
}

// coop-snapshot stage 4, at the main loop's frame start (no game frame on the stack): the host takes its state set on a
// quiet tick; the joiner, landed (tick 0 reached), puts the whole set back and goes on from the host's tick S.
void SnapFrameStart() {
    CostScope cost{g_costs.take_ms}; // (the take's frame start: all of it, the take with the town and Atla takes)
    SnapJobPoll(); // coop-offthread: a pack done off the game thread
    coop::DunTestAloneFrame(); // coop-soakfix3: DC_COOP_DUNTEST_ALONE (a host alone, before a join)
    // coop-soakfix3 test line (DC_NETPLAY_TEST_PLACE_LINE=1, tests/test_fixture_join.py::test_host_sits): this copy's place each time
    // it changes (game mode, the dungeon's task, the town's mode, the room's mode), so a row sees him go into a state a player
    // can sit in, and out of it on both copies.
    if (static const bool place_line = TestEnvOn("DC_NETPLAY_TEST_PLACE_LINE"); place_line) {
        static int was[4] = {-2, -2, -2, -2};
        const int  now[4] = {(int) mode, mode == GAME_MODE_DUNGEON ? (int) gameTask : -1, mode == GAME_MODE_EDIT ? (int) ::GameMode : -1,
                             mode == GAME_MODE_EDIT ? CoopTownIndoorMode() : -1};

        if (std::memcmp(was, now, sizeof now) != 0) {
            std::memcpy(was, now, sizeof now);
            std::fprintf(stderr, "netplay: test: place mode %d task %d town mode %d room mode %d (tick %lld)\n", now[0], now[1], now[2], now[3],
                         (long long) g_link.tick);
        }
    }
    // coop-houseblack test hook (DC_NETPLAY_TEST_TOWN_TASK=V[:R]): player 2's copy (player R's), the first frame it is apart in a town, takes
    // gameTask V (0x191, GAME_TASK_SCRIPT_RUN: what Circle at the floor menu back to town leaves, coop-gateflow), for the
    // house-out merge it then sends its state set from (l-local-play -HouseOutCross).
    if (static const char *task = TestEnv("DC_NETPLAY_TEST_TOWN_TASK"); task != nullptr) {
        static bool done = false;
        char       *rest = nullptr;
        const long  value = std::strtol(task, &rest, 0);
        const int   role = rest != nullptr && *rest == ':' ? std::atoi(rest + 1) : 2; // (V:R: the copy of role R, 2 unless said)

        if (!done && mode == GAME_MODE_EDIT && CoopWorldsApart() && NetplayRole() == role) {
            done = true;
            gameTask = (decltype(gameTask)) value;
            std::fprintf(stderr, "netplay: test: apart in town, the dungeon's task is now %#x\n", (unsigned) gameTask);
        }
    }
    // coop-snapjoin: a menu held open past kSnapMenuWaitMs does not hold the join off (the take waited for good once, opening
    // resync log 2026-10-25). DC_NETPLAY_MUTATE=snap_wait_quiet (twin): it waits for a frame with no menu, as before.
    if (SendsState() && g_snap.on && g_snap.take_due && mode == GAME_MODE_DUNGEON) {
        coop::FullMenuRetailToLocal();
        coop::FullMenuFullAtTake(); // coop-repinfix2: his menu beside the world, opened before the take (soak leave 2239)
    }
    if (SendsState() && g_snap.on && g_snap.take_due) {
        CoopScriptMenuRetailToLocal(); // coop-small2: a script's menu opened apart goes on local on its opener's copy
    }
    if (SendsState() && g_snap.on && g_snap.take_due && mode == GAME_MODE_EDIT) {
        CoopTownMenuRetailToLocal(g_link.player); // coop-stage4 4c: one opened since the link started
        if (g_link.player == 2) {
            CoopGeoramaHerToOwn(); // coop-stage4 r4: so too her Georama
        }
    }
    // coop-pt1106-lockup (Jep, 2026-11-06: a player joining a zone is not made to wait): in a town the wait only kept the joiner
    // black 3 s beside a menu that may stay open for minutes. Once the menu is up and its pack read (nothing of its own still
    // arriving) the set goes at once. DC_NETPLAY_MUTATE=snap_menu_full_wait (twin): the whole wait, as before.
    const bool menu_settled = mode == GAME_MODE_EDIT && ((BtlMenuReadEndFlag != 0 && (EdInteriorFlag == 0 ? ::GameMode == ED_MODE_MENU : CoopTownIndoorP1Menu())) ||
                                                         (CoopGeoramaCarries() && ::GameMode != ED_MODE_GEORAMA_MENU_INIT)) && // (coop-stage4 4d)
                              !NetplayMutated("snap_menu_full_wait");
    const int  menu_waited_ms = (int) std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - g_snap.due_at).count();
    // coop-soakfix3 (Jep: a join never waits): a scene on a floor or his knock-out screens need no menu's wait (3 s of the joiner's
    // black): SnapshotQuietTick takes them under menu_ok, so menu_ok at once there. DC_NETPLAY_MUTATE=scene_waits_menu (twin): the
    // 3 s, as before.
    const bool floor_scene = mode == GAME_MODE_DUNGEON && (BtEventMode != 0 || gameTask == GAME_TASK_DEAD || gameTask == GAME_TASK_DEAD_CHANGE_CHARA ||
                                                           gameTask == GAME_TASK_CHARA_SELECT_LOOP) &&
                             !NetplayMutated("scene_waits_menu");
    const bool menu_ok = g_snap.take_due && (menu_settled || floor_scene || menu_waited_ms > kSnapMenuWaitMs) &&
                         !NetplayMutated("snap_wait_quiet");

    // coop-snapjoin: a scene under way past kSnapSceneWaitMs (the mayor's, with player 2 following him in: the take waited
    // until the joiner gave up, w-town-apart -Case samebuilding -Snapshot, 2026-10-25): the catch-up's replay from the
    // checkpoint instead, as before snapshots, which plays the scene on her copy too. DC_NETPLAY_MUTATE=snap_wait_scene (twin):
    // it waits.
    // coop-bldgsnap: not from a place checkpoint (PlaceCheckpoint): this copy never took that state itself, so its pads from
    // tick 0 would replay unlike its play (w-town-apart -Case outback: OUT OF SYNC, then a resync's reload). The take waits
    // for the scene (a talk, the room's event) to end, a Busy each second keeping the joiner from giving up; she plays on
    // where she stands meanwhile.
    // coop-stable1: the Busy and the wait for every checkpoint, not the place checkpoint's alone: a building-start checkpoint
    // (she follows him into the mayor's house during his scene) fell back to the pads after 8 s, and the pads made her copy
    // reload the town to go in by his door again (o-opening-resync house-during: "zone: RESET ... by CatchJoinTick"). Now the
    // take waits out the scene (kSnapSceneWaitMs, 3 minutes) while she plays on in his building. DC_NETPLAY_MUTATE=
    // snap_scene_short (twin): the 8 s fallback, as before.
    const int scene_wait_ms = NetplayMutated("snap_scene_short") ? 8000 : kSnapSceneWaitMs;

    if (SendsState() && g_snap.on && g_snap.take_due && !SnapshotQuietTick(menu_ok)) {
        static auto busy_at = std::chrono::steady_clock::now();

        if (std::chrono::steady_clock::now() - busy_at > std::chrono::seconds(1)) {
            Busy busy{kBusyMagic, (std::uint32_t) g_link.player, g_link.session, 0};

            NetSocketSend(&busy, sizeof busy);
            busy_at = std::chrono::steady_clock::now();
        }
        if (static auto said_at = std::chrono::steady_clock::now(); std::chrono::steady_clock::now() - said_at > std::chrono::seconds(10)) {
            said_at = std::chrono::steady_clock::now();
            std::fprintf(stderr, "catchup: the %s join waits for a quiet frame (mode %d, task %d, room mode %d, town mode %d)\n",
                         g_solo.place ? "place checkpoint's" : "checkpoint's", (int) mode, (int) gameTask, CoopTownIndoorMode(), (int) ::GameMode);
        }
    }
    if (SendsState() && g_snap.on && g_snap.take_due && !g_solo.place && !SnapshotQuietTick(menu_ok) && !NetplayMutated("snap_wait_scene") &&
        std::chrono::steady_clock::now() - g_snap.due_at > std::chrono::milliseconds(scene_wait_ms)) {
        g_snap.take_due = false;
        g_snap.refused = true;
        std::fprintf(stderr, "catchup: no quiet frame for %d ms (a scene under way: mode %d, task %d): the pads from tick 0 instead\n", scene_wait_ms,
                     (int) mode, (int) gameTask);
    }

    // coop-ws5left (t-p2-houseout-view's waits, 182-238 ms unloaded, 289-353 beside a CPU hog): the stayer's merge, its place
    // checkpoint and link and then the state set's take (pack and parts, 80-100 ms), all ran between two of her drawn frames:
    // a 0.2 s freeze on the screen of the player who was there (Jep's rule: a join never makes her wait). The take now waits
    // for a frame drawn after the checkpoint, so the two halves fall in two frames. DC_NETPLAY_MUTATE=snap_same_frame (twin):
    // at once, as before.
    const bool drawn_since = g_frames_drawn >= g_snap.due_drawn + 2 || NetplayMutated("snap_same_frame");

    if (SendsState() && g_snap.on && g_snap.take_due && drawn_since && SnapshotQuietTick(menu_ok) && (LateJoinTestTick() == 0 || g_link.tick >= LateJoinTestTick()) &&
        !g_test_take_held) {
        std::string why;
        auto        start = std::chrono::steady_clock::now();

        if (!SnapshotQuietTick()) {
            std::fprintf(stderr, "catchup: no frame without a menu for %d ms: the state set is taken with the menu open (%s)\n", menu_waited_ms,
                         menu_settled ? "at once, the menu has settled" : "after the wait");
        }
        // (coop-stage4 4e part 1: said when the set is taken mid-talk, his talk going on through the merge; x-town-stayermenu -Talk)
        if (mode == GAME_MODE_EDIT && (EdInteriorFlag != 0 ? CoopTownIndoorMode() == 3 : ::GameMode == ED_MODE_TALK)) {
            std::fprintf(stderr, "catchup: the state set is taken mid-talk (%s, room mode %d, town mode %d): the talk goes on through the merge\n",
                         EdInteriorFlag != 0 ? "a room" : "the town", CoopTownIndoorMode(), (int) ::GameMode);
        }
        g_snap.take_due = false;
        CoopTownSyncTake(); // coop-localfix: her town place with the state set
        CoopP2TownPauseTake(); // coop-mergecam: her pause with it
        CoopGeoramaSyncTake(); // coop-stage4 r4: her own Georama with it
        CoopScriptMenuSyncTake(); // coop-small2: a script's menu open, carried local
        CoopWorldsTestQuestAtTake(); // coop-pt1111e (a test hook)
        if (TestEnv("DC_NETPLAY_TEST_TICK_LINE") != nullptr) { // coop-houseexit (tests)
            std::fprintf(stderr, "tickline: the state set's take at tick %lld\n", (long long) g_link.tick);
            CoopTownCamTestLine(-1);
        }
        if (mode == GAME_MODE_DUNGEON) {
            coop::PartnerMotionTake(); // coop-p1follows: the partner body's motion clock with it
        }
        std::memcpy(g_snap_pads.synced, g_link.synced, sizeof g_snap_pads.synced);
        std::memcpy(g_snap_pads.previous, g_link.previous, sizeof g_snap_pads.previous);
        std::memcpy(g_snap_pads.routed, g_link.routed, sizeof g_snap_pads.routed);
        ForeignClutTestTick();
        // coop-soakfix3: where the set was taken (a row's check that the join did not wait for him to leave a state he sits in)
        std::fprintf(stderr, "catchup: the state set's take: mode %d task %d town mode %d room mode %d at tick %lld\n", (int) mode,
                     mode == GAME_MODE_DUNGEON ? (int) gameTask : -1, mode == GAME_MODE_EDIT ? (int) ::GameMode : -1,
                     mode == GAME_MODE_EDIT ? CoopTownIndoorMode() : -1, (long long) g_link.tick);
        if (mode == GAME_MODE_DUNGEON && BtEventMode != 0) { // coop-joinfix: the join goes into his scene (37a)
            std::fprintf(stderr, "catchup: the state set is taken mid-scene (script %d, task %d, tick %lld)\n", (int) BtEventInfo.running_script_no,
                         (int) gameTask, (long long) g_link.tick);
        }
        if (coop::MenuWorkInSet()) { // coop-pt1111m (the rows' line, against the taker's after its apply)
            std::fprintf(stderr, "catchup: the menus are in the lockstep (game.coop_menus off): the menu's working globals at the take %08x\n", coop::MenuWorkHash());
        }
        if (SnapshotTakeBytes(g_snap.bytes, why, SnapExtras())) {
            g_snap.tick = g_link.tick;
            // coop-offthread: the copy alone on the game thread; the pack and the page list on a SnapJob's (taken up by SnapJobPoll)
            // (coop-reds2's fresh set, a fallback's, comes through here too: off the game thread like the first)
            auto job = std::make_shared<SnapJob>();

            if (g_catch.restarts > 0) {
                g_catch.restart_tick = g_snap.tick;
            }

            job->kind = 1;
            job->began = start;
            job->tick_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            if (const SnapshotTakeStats taken = SnapshotLastTake(); true) {
                std::fprintf(stderr, "catchup: the state set's copy: %zu regions, %zu MB scanned, %zu MB kept; listing %.0f ms, copying %.0f ms\n", taken.regions,
                             taken.scanned >> 20, taken.kept >> 20, taken.list_ms, taken.copy_ms);
            }
            job->by_pages = g_catch.restarts == 0 && PageDiffLink(); // coop-pagediff (coop-reds2: a fresh set goes whole)
            job->raw_send = NetplayMutated("snap_raw");
            job->bytes.swap(g_snap.bytes);
            std::fprintf(stderr, "pagediff: the state set goes %s (peer %s)\n", job->by_pages ? "by pages" : "whole",
                         NetSocketLearnsPeer() ? NetSocketPeerText() : Settings().peer.c_str());
            std::fprintf(stderr, "catchup: the state set's take came %lld drawn frames after its checkpoint\n", g_frames_drawn - g_snap.due_drawn);
            SnapJobStart(job);
        } else {
            g_snap.refused = true;
            std::fprintf(stderr, "catchup: no snapshot (%s): the pads from tick 0 instead\n", why.c_str());
        }
    }

    // coop-pagediff, the taker landed (tick 0 reached) with the sender's list in: its own state set, and the pages it needs.
    if (TakesState() && g_snap.list_in && !g_snap.need_sent && !g_snap.asked_whole && g_solo.on && g_catch.active) {
        const auto  start = std::chrono::steady_clock::now();
        std::string why;
        std::size_t needed = 0;

        if (!SnapshotTakeBytes(g_snap.own, why, SnapExtras())) {
            g_snap.own.clear(); // every page needed
        }
        if (SnapshotPageNeed(g_snap.list, g_snap.own, g_snap.in_need, &g_snap.list_pages, &needed)) {
            g_snap.need_sent = true;
            std::fprintf(stderr, "pagediff: this copy needs %zu of the list's %zu pages (%lld ms) [t %lld]\n", needed, g_snap.list_pages,
                         (long long) std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count(), LogMs());
        } else {
            g_snap.asked_whole = true;
            std::fprintf(stderr, "pagediff: the page list did not parse: asking for the whole state set\n");
        }
    }
    if (TakesState() && g_snap.need_sent && !g_snap.answered && !g_snap.asked_whole &&
        std::chrono::steady_clock::now() - g_snap.said_at > std::chrono::milliseconds(kPageAgainMs)) {
        PageNeed need{};

        need.magic = kPageNeedMagic;
        need.player = (std::uint32_t) g_link.player;
        need.session = g_link.session;
        need.pages = (std::uint32_t) g_snap.list_pages;
        for (std::uint32_t first = 0; first < g_snap.in_need.size(); first += kPageNeedWords) {
            need.first = first;
            std::memset(need.words, 0, sizeof need.words);
            std::memcpy(need.words, g_snap.in_need.data() + first, std::min<std::size_t>(kPageNeedWords, g_snap.in_need.size() - first) * 4);
            NetSocketSend(&need, sizeof need);
        }
        g_snap.said_at = std::chrono::steady_clock::now();
    }
    if (TakesState() && g_snap.diff_due && !g_snap.asked_whole) {
        const auto                 start = std::chrono::steady_clock::now();
        std::vector<unsigned char> diff, rebuilt;
        std::string                why = "the pages did not unpack";

        g_snap.diff_due = false;
        if (SnapshotUnpack(g_snap.in, diff) && SnapshotPageRebuild(g_snap.list, g_snap.own, g_snap.in_need, diff, rebuilt, why)) {
            std::fprintf(stderr, "pagediff: state set rebuilt from %zu bytes of pages sent (%zu came packed) and this copy's own: %zu bytes, the whole hash matches (%lld ms)\n",
                         diff.size(), g_snap.in.size(), rebuilt.size(),
                         (long long) std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
            g_snap.in.swap(rebuilt); // unpacked: SnapshotUnpack below takes it as it is
            g_snap.apply_due = true;
        } else {
            g_snap.asked_whole = true;
            g_snap.answered = false;
            g_snap.in.clear();
            std::fprintf(stderr, "pagediff: the rebuild failed (%s): asking for the whole state set\n", why.c_str());
        }
        g_snap.own.clear();
        g_snap.own.shrink_to_fit();
    }
    if (TakesState() && g_snap.asked_whole && !g_snap.answered && std::chrono::steady_clock::now() - g_snap.said_at > std::chrono::milliseconds(kPageAgainMs)) {
        Busy whole{};

        whole.magic = kPageWholeMagic;
        whole.player = (std::uint32_t) g_link.player;
        whole.session = g_link.session;
        NetSocketSend(&whole, sizeof whole);
        g_snap.said_at = std::chrono::steady_clock::now();
    }

    if (TakesState() && g_snap.apply_due && g_solo.on && g_catch.active) {
        std::string why;
        std::size_t regions = 0, bytes = 0;
        auto        start = std::chrono::steady_clock::now();

        g_snap.apply_due = false;
        // coop-floorjoin2 (tests): what this copy was in when the state set landed: a floor-start checkpoint's scene flag
        // (the cave's floor 1: 103) set means the landing skipped the floor's opening scene.
        std::fprintf(stderr, "catchup: before the apply: task %d, event mode %d, script %d, scene flags 103 %d 100 %d\n", (int) gameTask,
                     (int) BtEventMode, (int) BtEventInfo.running_script_no, SaveData != nullptr ? (int) SaveData->GetGameFlag(103) : -1,
                     SaveData != nullptr ? (int) SaveData->GetGameFlag(100) : -1);
        // coop-localfix2: the state set came packed (SnapshotPack); unpacked here, or refused as broken.
        std::vector<unsigned char> unpacked;
        const bool                 whole = SnapshotUnpack(g_snap.in, unpacked);

        if (!whole) {
            why = "the packed state set did not unpack";
        }
        const bool applied = whole && StateSetApplyCore(unpacked, why, &regions, &bytes);

        if (applied) {
            g_snap.applied = true;
            g_link.tick = g_snap.in_tick;
            // coop-fx51b: DC_NETPLAY_MUTATE=snap_own_rand (twin of the catch-up joins from a fixture): the taker's generator one
            // draw on from the host's after the apply, as a joiner that kept part of its own state: the first checksum parts them.
            if (NetplayMutated("snap_own_rand")) {
                rand();
                std::fprintf(stderr, "catchup: test: one draw of this copy's own after the apply (snap_own_rand)\n");
            }
            if (NetplayMutated("snap_pads_rest")) {
                g_link.synced[0] = g_link.synced[1] = g_link.previous[0] = g_link.previous[1] = Neutral();
            } else {
                std::memcpy(g_link.synced, g_snap_pads.synced, sizeof g_link.synced);
                std::memcpy(g_link.previous, g_snap_pads.previous, sizeof g_link.previous);
                std::memcpy(g_link.routed, g_snap_pads.routed, sizeof g_link.routed);
            }
            g_link.menu_owner = 1;
            g_link.held = 0;
            if (g_solo.pads.size() < (std::size_t) g_snap.in_tick * 2) {
                g_solo.pads.resize((std::size_t) g_snap.in_tick * 2, Neutral());
            }
            g_catch.have_to = std::max<std::int64_t>(g_catch.have_to, g_snap.in_tick);
            g_catch.lag_at_from = -1; // coop-reds2: the lag window starts again from here
            while (g_solo.next_delta < g_solo.deltas.size() && g_solo.deltas[g_solo.next_delta].tick <= g_snap.in_tick) {
                g_solo.next_delta++; // coop-worlds: progress the host applied before S is in the state set already
            }
            CoopWorldsFloorLayoutAgain(); // the tests' layout line: the host's floor is here now
            CoopWorldsMergeStateIn();     // coop-localfix: from here this copy's player is player 1's

            // coop-floorjoin2 (Jep's 2026-10-25 playtest, note 36: "after that just had a black screen"): the game's texture
            // table came with the state set, naming the host's TBP0 and CBP keys, while the port's registry (not in the set)
            // still held this copy's own: every draw missed. The registry is made again from the table and the staged
            // pixels (PortTextureRebuildAfterApply). DC_NETPLAY_MUTATE=stale_texture_keys (x-floor-join -Case late's second
            // twin): the registry as it was.
            // coop-localfix (Jep's 2026-10-26: "Xiao is still invisible on P2s screen but visible on P1s"): the host's partner
            // textures out of the table before the registry is made again (they named this copy's pixels at the host's addresses:
            // only her shadow drew), and her town model loaded again after it.
            StateSetAfterApply();
            g_arrival.apply_to = NowMs(); // coop-offthread
            g_arrival.apply_from = g_arrival.apply_to - (std::int64_t) std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
            std::fprintf(stderr, "catchup: snapshot of tick %lld applied: %zu regions, %zu bytes (%lld ms); replaying from tick %lld [t %lld]\n",
                         (long long) g_snap.in_tick, regions, bytes,
                         (long long) std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count(),
                         (long long) g_snap.in_tick, LogMs());
            // coop-fringefix: both clocks as the set leaves them (NowTime, the save's now_time), for the clock desync after a merge
            std::fprintf(stderr, "catchup: clock after the state set: NowTime %.3f, the save's %.3f\n", NowTime,
                         SaveData != nullptr ? SaveData->GetNowTime() : -1.0f);
        } else {
            std::fprintf(stderr, "catchup: the snapshot could not be applied (%s): asking for the pads from tick 0\n", why.c_str());
            g_snap.said_refused = true;
        }
        g_snap.in.clear();
        g_snap.in.shrink_to_fit();
    }

    if (TakesState() && g_snap.said_refused && g_catch.active && g_catch.have_to <= 0) {
        static auto last = std::chrono::steady_clock::now() - std::chrono::seconds(1);
        auto        now = std::chrono::steady_clock::now();

        if (now - last > std::chrono::milliseconds(200)) {
            Busy refused{};

            refused.magic = kSnapRefusedMagic;
            refused.player = (std::uint32_t) g_link.player;
            refused.session = g_link.session;
            NetSocketSend(&refused, sizeof refused);
            last = now;
        }
    }
}

namespace {
void NatCodecCheck(); // coop-natcode (tests)
}

// coop-stateload (tests; Jep's "do both", 2026-10-28): state files, the catch-up's state set on disk, so a test starts at a
// place and moment without playing its way there.
//   DC_STATE_SAVE_AT=T:PATH  on the first quiet tick (SnapshotQuietTick) at T or later (the link's tick when linked, else
//                            the frame count), this copy's state set (SnapshotTakeBytes, packed) goes to PATH after a
//                            header line naming where it was taken and the game's checksum there.
//   DC_STATE_LOAD_AT=T:PATH  on the first frame at T or later in the header's place (PlaceNow: mode, map, floor,
//                            building; the test's --jump puts the copy there), PATH's state set is put back as a catch-up
//                            puts the host's (StateSetApplyCore, StateSetAfterApply). Both copies of a linked pair load it
//                            on one lockstep tick and go on alike. Logged with the checksum the file names and the one after.
// The header: "dcstate 1 tick T place <PlaceNow> sum XXXXXXXX bytes N\n", then N packed bytes.
namespace {
struct StateFileSpec {
    std::int64_t at = -1;
    std::string  path;
    bool         done = false;
};

StateFileSpec ParseStateSpec(const char *env) {
    StateFileSpec spec;
    const char   *text = TestEnv(env);
    const char   *colon = text != nullptr ? std::strchr(text, ':') : nullptr;

    if (colon != nullptr) {
        spec.at = std::atoll(text);
        spec.path = colon + 1;
    }
    return spec;
}

std::int64_t StateTick() {
    return g_link.up ? g_link.tick : (std::int64_t) dc::app::GameFrameCount();
}


// coop-fixtures (tests): the checksum's parts as the last Checksum() left them, one line, so a state file's save and load
// can be compared part by part when the load does not give the saved checksum back (the town-both fixture, 2026-11-01).
void StatePartsLine(const char *what) {
    const Parts &p = g_parts;
    float        outdoors[3] = {0.0f, 0.0f, 0.0f};
    const bool   shown = mode == GAME_MODE_EDIT && coop::TownPartnerShown(outdoors, nullptr);

    std::fprintf(stderr,
                 "coop-state: %s parts: rand %08x task %d camera %.4f camera2 %.4f view %08x player (%.2f %.2f %.2f) partner (%.2f %.2f %.2f) "
                 "town (%.2f %.2f %.2f) town_modes %08x status %08x save %08x p2_bag %08x menus %08x floor %08x monsters %08x stages %08x %08x %08x "
                 "town_cam (%.1f %.1f %.1f / %.1f %.1f %.1f) partner_chara %d down %d choice %d menu_owner %d town_partner %d (%.2f %.2f %.2f) party %d\n",
                 what, p.rand, p.task, p.camera, p.camera2, p.view, p.player[0], p.player[1], p.player[2], p.partner[0], p.partner[1], p.partner[2],
                 p.town[0], p.town[1], p.town[2], p.town_modes, p.status, p.save, p.p2_bag, p.menus, p.floor, p.monsters, p.stages[0], p.stages[1],
                 p.stages[2], p.town_cam[0], p.town_cam[1], p.town_cam[2], p.town_cam[3], p.town_cam[4], p.town_cam[5], p.partner_chara, p.partner_down,
                 p.partner_choice, p.menu_owner, shown ? 1 : 0, outdoors[0], outdoors[1], outdoors[2],
                 UserStatus != nullptr ? (int) UserStatus->party_size : -1); // (coop-fixtures: the party a recipe made, fixture-recipe-proof)
}

// coop-twinfix (atlasync's leftover, 2026-11-01): a state file is the catch-up's state set on disk, so what the join's take
// copies into the set's own statics before it is taken goes with a file too: on a floor the partner body's motion clock and
// facing (PartnerMotionTake), else a fixture carried the body_kept of the copy's last join (or none: the partner's motion
// started again at every load, StateFileApply's PartnerSnapshotApplied). (coop-opt7 step 3: her Atla's presentation, which
// coop-atlasync carried here too, is her copy's engine now, never in the set.) DC_NETPLAY_MUTATE=file_motion_stale (twin): none.
void StateFileTakeParts() {
    std::fprintf(stderr, "coop-state: the file's take at tick %lld (mode %d)\n", (long long) StateTick(), (int) mode);
    if (mode == GAME_MODE_DUNGEON && !NetplayMutated("file_motion_stale")) {
        coop::PartnerMotionTake();
    }
    // coop-fx51c (housecam, 2026-11-04: on the town-door fixture her eye jumped 749 in a frame through his house-exit merge): in a
    // town her place, camera and pause go with the state set as a merge's take has them (CoopTownSyncTake, CoopP2TownPauseTake).
    // A file took none, so every town fixture's load put her away ("it had none") and placed her again beside Toan (town load:
    // first), on the town-door fixture under the porch he stands on (y 50 against his 71), not where the file's copy had her.
    // (Its twin, DC_NETPLAY_MUTATE=file_town_unplaced, drops them at the load: StateFileApply.)
    if (mode == GAME_MODE_EDIT) {
        CoopTownSyncTake();
        CoopP2TownPauseTake();
        CoopGeoramaSyncTake(); // coop-stage4 r4
    }
    CoopScriptMenuSyncTake(); // coop-small2 (a file takes none open: its record cleared)
}

void StateFileSave(StateFileSpec &spec) {
    std::vector<unsigned char> raw, packed;
    std::string                why;
    char                       place[160];

    spec.done = true;
    PlaceNow(place, sizeof place);
    StateFileTakeParts();
    if (!SnapshotTakeBytes(raw, why, SnapExtras())) {
        std::fprintf(stderr, "coop-state: no state set to save (%s)\n", why.c_str());
        return;
    }
    SnapshotPack(raw, packed);
    const std::uint32_t taken = Checksum();
    StatePartsLine("taken");
    // coop-fixtures (2026-11-01, town-both): the checksum the file names is this copy's after putting the state set it took
    // back on itself, as a loading copy will (StateSetApplyCore, StateSetAfterApply). The apply's after-steps redo what no
    // state set carries (in a town the partner's model is hidden until it is placed again), so a load is compared with a
    // load, not with the tick before one: the town partner part differed every time. The invariant: a file loaded anywhere
    // gives the checksum it gave on the copy that wrote it.
    std::uint32_t sum = taken;
    {
        std::string again_why;
        if (StateSetApplyCore(raw, again_why, nullptr, nullptr)) {
            StateSetAfterApply();
            sum = Checksum();
            StatePartsLine("saved");
        } else {
            std::fprintf(stderr, "coop-state: the taken state set did not go back on this copy (%s): the file names the taken checksum\n", again_why.c_str());
        }
    }
    std::ofstream       out(PathsFromUtf8(spec.path.c_str()), std::ios::binary);

    out << "dcstate 1 tick " << StateTick() << " place " << place << " sum " << std::hex << sum << " settings " << NetplaySettingsHash() << std::dec << " bytes "
        << packed.size() << "\n";
    out.write(reinterpret_cast<const char *>(packed.data()), (std::streamsize) packed.size());
    std::fprintf(stderr, "coop-state: taken checksum %08x, %08x once put back on this copy\n", taken, sum);
    std::fprintf(stderr, "coop-state: saved at tick %lld in %s: %zu bytes (%zu packed), checksum %08x, to %s%s\n", (long long) StateTick(), place,
                 raw.size(), packed.size(), sum, spec.path.c_str(), out.good() ? "" : " (WRITE FAILED)");
    GlobalsDumpAfterLoad(); // (a save counts as a load for the dump: the maker's frames past its save against a loader's past its load)
}

// The file's header and packed bytes, or false (said why).
bool          g_state_file_loaded = false; // coop-gatekey52: a state file was put back (NetplayStateFileLoaded)
std::int64_t  g_state_file_tick = -1;      // coop-gatekey52: and the tick it was
int           g_state_file_loads = 0;      // coop-warmconvert (WS7 round 4): state files put back so far
int           g_state_file_loads_at_job = 0; // and how many before this job's (ServeResetForJob)
std::uint32_t g_state_read_settings = 0; // coop-fixtures: the settings hash the last read state file names (0: an older file)

bool StateFileRead(const std::string &path, std::string &place, std::uint32_t &sum, std::vector<unsigned char> &packed) {
    std::ifstream in(PathsFromUtf8(path.c_str()), std::ios::binary);
    std::string   line;

    if (!in || !std::getline(in, line)) {
        std::fprintf(stderr, "coop-state: cannot read %s\n", path.c_str());
        return false;
    }
    const auto at_place = line.find(" place "), at_sum = line.find(" sum "), at_bytes = line.find(" bytes ");

    if (line.rfind("dcstate 1 ", 0) != 0 || at_place == std::string::npos || at_sum == std::string::npos || at_bytes == std::string::npos) {
        std::fprintf(stderr, "coop-state: %s is not a state file\n", path.c_str());
        return false;
    }
    place = line.substr(at_place + 7, at_sum - at_place - 7);
    sum = (std::uint32_t) std::strtoul(line.c_str() + at_sum + 5, nullptr, 16);
    const auto at_settings = line.find(" settings ");
    g_state_read_settings = at_settings != std::string::npos ? (std::uint32_t) std::strtoul(line.c_str() + at_settings + 10, nullptr, 16) : 0u;
    packed.resize((std::size_t) std::strtoull(line.c_str() + at_bytes + 7, nullptr, 10));
    in.read(reinterpret_cast<char *>(packed.data()), (std::streamsize) packed.size());
    if ((std::size_t) in.gcount() != packed.size()) {
        std::fprintf(stderr, "coop-state: %s is short (%lld of %zu bytes)\n", path.c_str(), (long long) in.gcount(), packed.size());
        return false;
    }
    return true;
}

// coop-cand57 (x-arena-pins -Case moved, tests only): DC_STATE_EXIT_AFTER_LOAD=1 ends the copy straight after a state file's
// apply or refusal, before the world steps, so a row on the apply alone (its twin: arena_base_unchecked, the set loaded with
// its pointers into nothing) ends on its line, not on the crash a step would make.
static void StateFileExitAfterLoad() {
    if (TestEnvOn("DC_STATE_EXIT_AFTER_LOAD")) {
        std::fprintf(stderr, "coop-state: test: exit after the load (DC_STATE_EXIT_AFTER_LOAD)\n");
        std::fflush(nullptr);
        std::_Exit(0);
    }
}

// The state set of a read state file put back now, as a catch-up puts the host's (StateSetApplyCore, StateSetAfterApply),
// logged with the checksum before, after and the one the file names. coop-serve: also a job's fixture.
bool StateFileApply(const std::string &path, const char *place, const std::vector<unsigned char> &packed, std::uint32_t saved_sum) {
    std::vector<unsigned char> unpacked;
    std::string                why;
    std::size_t                regions = 0, bytes = 0;
    const auto                 start = std::chrono::steady_clock::now();
    const std::uint32_t        before = Checksum();

    if (!SnapshotUnpack(packed, unpacked)) {
        std::fprintf(stderr, "coop-state: %s did not unpack\n", path.c_str());
        return false;
    }
    g_state_file_applying = true; // coop-fx51c (the file_town_unplaced twin, StateSetApplyCore)
    const bool applied = StateSetApplyCore(unpacked, why, &regions, &bytes);

    g_state_file_applying = false;
    if (!applied) {
        std::fprintf(stderr, "coop-state: %s could not be applied (%s)\n", path.c_str(), why.c_str());
        StateFileExitAfterLoad();
        return false;
    }
    StateSetAfterApply();
    CoopWorldsStateFileLoaded(); // coop-fx51c: the floor it was saved on counts as started here too
    g_state_file_loaded = true;
    g_state_file_tick = StateTick();
    g_state_file_loads++;
    const std::uint32_t after = Checksum();
    StatePartsLine("loaded"); // (coop-fixtures: compared with the save's line when the sums differ)

    std::fprintf(stderr, "coop-state: loaded %s at tick %lld in %s: %zu regions, %zu bytes (%lld ms); checksum %08x before, %08x after, %08x saved: %s\n",
                 path.c_str(), (long long) StateTick(), place, regions, bytes,
                 (long long) std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count(), before, after, saved_sum,
                 after == saved_sum ? "the same" : "DIFFERENT");
    if (after == saved_sum) {
        g_state_file_applied = g_link.tick; // coop-fx51a: DC_NETPLAY_SAVE_AT=loaded saves on the tick after it
    }
    // (coop-fx51a, b2: the floor counts as started here, as on the copy that saved it: a catch-up's checkpoint readiness,
    // StandsAtCheckpointStart, reads it; the load kept the jump's own, the loader against the maker)
    if (mode == GAME_MODE_DUNGEON) {
        g_floor_started = true;
    }
    GlobalsDumpAfterLoad();
    StateFileExitAfterLoad();
    return after == saved_sum;
}

// coop-serve (tests; Jep, 2026-10-31: "open 3-5 clients and iterate through 30-50 tests on each of them without having to
// close and open new clients"): DC_QA_SERVE=DIR keeps a copy running from test to test. Every 30 frames it looks for
// DIR/job-N.txt (N from 1), one item a line, the file complete once its last line is "end":
//   at T            the job starts at link tick T (the frame count when not linked); a linked pair's two copies get the
//                   same T from the harness, so both start, load and press on one lockstep tick
//   fixture PATH    a state file (DC_STATE_SAVE_AT's) put back at T; it must have been taken in the place the copy is in
//   input PATH      an --input script installed at T, its frames counted from T (NetplayTestInputFrame)
//   env NAME=VALUE  a test switch for this job (TestEnvSetJob), set at T before its fixture; cleared at its end. Only the
//                   switches that re-read per job (TestJobGeneration: the dungeon driver, the menu presses) take it mid-run
// At T it logs "qa-job: begin N" (then the load's coop-state line); once DIR/end-N.txt exists it logs "qa-job: end N",
// drops the job's input and looks for job N+1. DC_QA_STOP_FILE still ends the copy. Nothing here runs without the switch.
// That one job leaves nothing behind for the next is the serve-isolation row's to prove (a row cold and as a later job).
struct ServeJob {
    int          number = 0;
    std::int64_t at = -1;
    std::string  fixture, input;
    std::vector<std::pair<std::string, std::string>> env;
    bool         begun = false;
    bool         agreed = false; // coop-loadreds: a linked pair's job, begun on the tick both copies' reads agree on
    bool         end_seen = false;   // train53: its end file seen on this copy
    bool         end_agreed = false; // and said to the other copy: it ends on the tick both words agree on
    // coop-fx51a: a fixture taken in another place than this copy's, gone to first (ServeGoTo) and put back once there
    bool                       pending = false;
    std::string                pending_place;
    std::uint32_t              pending_sum = 0;
    std::vector<unsigned char> pending_packed;
};

// Job N's file, read once it is complete; false while it is missing or still being written.
bool ServeReadJob(const std::string &base, int number, ServeJob &job) {
    std::ifstream in(PathsFromUtf8((base + "job-" + std::to_string(number) + ".txt").c_str()));
    std::string   line;
    bool          complete = false;

    job = ServeJob{};
    while (in && std::getline(in, line)) {
        if (!line.empty() && line.back() == 13) {
            line.pop_back();
        }
        if (line == "end") {
            complete = true;
        } else if (line.rfind("at ", 0) == 0) {
            job.at = std::atoll(line.c_str() + 3);
        } else if (line.rfind("fixture ", 0) == 0) {
            job.fixture = line.substr(8);
        } else if (line.rfind("input ", 0) == 0) {
            job.input = line.substr(6);
        } else if (line.rfind("env ", 0) == 0 && line.find('=') != std::string::npos) {
            const std::size_t eq = line.find('=');
            job.env.emplace_back(line.substr(4, eq - 4), line.substr(eq + 1));
        }
    }
    job.number = complete ? number : 0;
    return complete;
}

// coop-fx51a (coordinator 2026-11-04: a warm pair on floor 2 or in town must take a dun1-f1 fixture next, as retail's own
// area change gets there): this copy leaves for a fixture's place as a resync leaves for the host's (LeaveForState): out
// of its mode on this tick, the lockstep held (load_hold) until it stands in the place, both copies of a pair alike. A
// dungeon floor goes the way a floor-start checkpoint's copy goes (NetplayGoToFloor: the entrance, then straight onto the
// floor), a town as a resync's town. False (said) for a place it has no way to: a building's interior, the title.
bool ServeGoTo(const std::string &place) {
    int at_mode = -1, map_no = -1, local = -1, floor = -1, interior = -1;

    if (std::sscanf(place.c_str(), "mode %d map %d (local %d, floor %d, interior %d)", &at_mode, &map_no, &local, &floor, &interior) != 5 ||
        interior != 0 || NetplayMutated("serve_no_goto")) {
        return false;
    }
    if (at_mode == GAME_MODE_DUNGEON && map_no >= 200 && map_no <= 206 && floor >= 0) {
        NetplayGoToFloor(map_no - 200, floor);
        g_link.resync_target = map_no;
    } else if (at_mode == GAME_MODE_EDIT && map_no >= 0 && map_no < 200) {
        NetplayGoToTown(map_no);
        g_link.resync_target = map_no;
    } else {
        return false;
    }
    g_link.load_hold = !NetplayMutated("no_load_hold");
    return true;
}

// The job's input script installed and counted from now (its fixture put back).
void ServeInstallInput(const ServeJob &job) {
    if (job.input.empty()) {
        return;
    }
    InputScript script;
    std::string error;

    if (!CoopInputScriptLoad(PathsFromUtf8(job.input.c_str()), script, error)) {
        std::fprintf(stderr, "qa-job: %d: bad input script: %s\n", job.number, error.c_str());
        return;
    }
    InputScriptInstall(std::move(script));
    // (counted from this frame, the load's: with ScriptInputFor (coop-loadcause) a job's presses land on the same ticks past
    // its load as a cold row's anchored at the load tick + 1: x-fixture-fidelity -Case dirty, a press at script frame 130 135
    // ticks past the load on both pairs)
    g_serve_input_base = (std::int64_t) dc::app::GameFrameCount();
}

// coop-warmreset (test-time plan WS7 step 1): just before a job's fixture goes back, on both copies of the pair on one tick: a new job
// generation (the per-job test counters, COOP_TEST_TRACE_RESET / COOP_TEST_JOB_RESET, start from here, not from the last job's end:
// the pair plays on idle between jobs) and every co-op module's state outside the state set as a fresh session has it
// (CoopResetForJob: before, only a resync reset it, so a job took the last one's wheels, monster targets, shot ring, door vote ...).
// What of it the state set holds the fixture then puts back, so the load's checksum is the saved one as before. Not on the
// serve_no_reload twin's later jobs (no fixture put back there).
void ServeResetForJob(const ServeJob &job) {
    TestJobBegin();
    CoopResetForJob();
    g_state_file_loads_at_job = g_state_file_loads; // (NetplayJobStateFileLoaded: from this job's own load on)
    if (!TestTraceKept()) {
        dc::render::Draw3DResetForJob(); // (the draw3d stale-block counts and their said lines: from none, as on a fresh copy)
        // coop-warmconvert (WS7 round 4): the frame series (DC_COOP_TEST_SHOT_SERIES, a job switch) counts from the job, its mark
        // from none: the job's fixture load (or a hook after it) marks it as a fresh copy's does
        g_test_series_count = 0;
        g_test_series_from = -1;
        g_test_series_mark = -1;
        g_test_series_moved = false;
        g_test_series_frame = 0;
    }
    std::fprintf(stderr, "qa-job: %d: reset for the job\n", job.number);
}

// After the fixture: what this job found outside the state set (warm-repeat-selftest compares it with the pair's first job's).
void ServeLeftoversLine(const ServeJob &job) {
    std::fprintf(stderr, "qa-job: %d leftovers: %s, draw3d stale %d rebuilt %d\n", job.number, CoopJobLeftovers().c_str(), dc::render::Draw3DStaleVisuals(), dc::render::Draw3DRebuiltVisuals());
}

// The job's start: its fixture put back (in its place, gone to first when it is another), its input script installed
// and counted from then.
void ServeBegin(ServeJob &job) {
    // coop-warmconvert (WS7 round 3): "NAME@1=value" / "NAME@2=value" is a switch of that player's copy alone (its name without the
    // suffix), as a cold row gives its two copies different env (the host's and the joiner's frame folder, a case on one side;
    // lib\Serve.ps1 Get-ServeJobLines). (No per-copy input script: a script's lines name their pad, "pad2", and each copy
    // plays its own player's, so one script serves both.) DC_NETPLAY_MUTATE=serve_env_both (warm-repeat-selftest -EnvBothTwin):
    // a @-switch set on both copies.
    for (const auto &kv : job.env) {
        const std::size_t at = kv.first.size() >= 3 ? kv.first.size() - 2 : std::string::npos;

        if (at != std::string::npos && kv.first[at] == '@' && (kv.first[at + 1] == '1' || kv.first[at + 1] == '2')) {
            if (kv.first[at + 1] - '0' == g_link.player || NetplayMutated("serve_env_both")) {
                TestEnvSetJob(kv.first.substr(0, at).c_str(), kv.second.c_str());
            }
            continue;
        }
        TestEnvSetJob(kv.first.c_str(), kv.second.c_str());
    }
    std::fprintf(stderr, "qa-job: begin %d at tick %lld\n", job.number, (long long) StateTick());
    if (!job.fixture.empty()) {
        coop::FullMenuEndForJob(); // coop-fx51a: the last job's menu, if it left one open, before the fixture goes back
    }
    // (DC_NETPLAY_MUTATE=serve_no_reload, serve-isolation's twin: a later job's fixture is not put back, so what the jobs before it
    // left behind stays)
    if (!job.fixture.empty() && !(job.number > 1 && NetplayMutated("serve_no_reload"))) {
        std::string                fixture_place;
        std::uint32_t              sum = 0;
        std::vector<unsigned char> packed;
        char                       place[160];

        PlaceNow(place, sizeof place);
        if (!StateFileRead(job.fixture, fixture_place, sum, packed)) {
            std::fprintf(stderr, "qa-job: %d: its fixture could not be read\n", job.number);
        } else if (fixture_place != place) {
            // (coop-fx51a: to its place first, then put back there, ServeFrameStart; the input once it is)
            if (ServeGoTo(fixture_place)) {
                std::fprintf(stderr, "qa-job: %d: its fixture was taken in %s, this copy is in %s: going there first\n", job.number,
                             fixture_place.c_str(), place);
                job.pending = true;
                job.pending_place = fixture_place;
                job.pending_sum = sum;
                job.pending_packed = std::move(packed);
                return;
            }
            std::fprintf(stderr, "qa-job: %d: its fixture was taken in %s, this copy is in %s: not loaded\n", job.number, fixture_place.c_str(), place);
        } else {
            ServeResetForJob(job);
            StateFileApply(job.fixture, place, packed, sum);
            ServeLeftoversLine(job);
        }
    }
    ServeInstallInput(job);
}

// coop-fx51a: a job gone to its fixture's place first puts it back on the first frame there, once the lockstep goes on
// again (load_hold lifted, no catch-up), both copies of a pair on one tick; then its input script. True while it waits.
bool ServePendingLoad(ServeJob &job) {
    if (!job.pending) {
        return false;
    }
    char place[160];

    PlaceNow(place, sizeof place);
    if (job.pending_place != place || g_link.load_hold || g_catch.active || g_link.restart_map >= 0) {
        return true;
    }
    job.pending = false;
    std::fprintf(stderr, "qa-job: %d: in its fixture's place at tick %lld: putting it back\n", job.number, (long long) StateTick());
    ServeResetForJob(job);
    StateFileApply(job.fixture, place, job.pending_packed, job.pending_sum);
    ServeLeftoversLine(job);
    job.pending_packed.clear();
    ServeInstallInput(job);
    return false;
}
// coop-loadreds (test-time plan WS5, 2026-11-06: train 52's warm-repeat-selftest red under load): a linked pair's copies each
// read a job file on their own frames, so with the harness's "at" the begin was each copy's own call: copy B read job 1 at
// tick 240 and began it there, copy A read it at 241 and refused it, and the pair was parted for good (B in job 1, A on to
// job 2, OUT OF SYNC from job 2's fixture on). A busy PC moves the reads apart; no "at" the harness picks can rule it out.
// So a linked pair agrees on the begin through the lockstep itself: a copy that has read job N says so as a command
// (kCmdServe, c = N), and the tick both copies' words are applied on (alike on both: the lockstep applies every command on
// its frame's tick) gives the begin: the next tick or the job's at, the later; no copy of a pair refuses a job. A copy alone
// keeps the at and its refusal. DC_NETPLAY_MUTATE=serve_no_agree (twin): each copy of a pair by the at alone, as before.
// train53 (2026-11-07): the end too. Each copy saw its job's end file on its own frame, so the end's switches and input went
// a tick or a few apart (end 2 at tick 640 on A, 639 on B); a job left mid-play (a twin's: x-gate-flow's gatekeyno lock never
// opened, x-his-chest's chestpausep1) parted the pair 20-60 ticks later and its resync landed in the next job (warm-repeat
// -selftest-mutations-keys/misc red in the candidate's warm run). A copy that sees the end file says so (kCmdServe, c = N with
// kServeEndBit); both end on the tick after both words. DC_NETPLAY_MUTATE=serve_end_own (warm-repeat-selftest -EndOwnTwin):
// each copy on its own look, as before.
constexpr std::uint32_t kServeEndBit = 0x40000000u;

struct ServeAgree {
    std::uint32_t ready[3] = {0, 0, 0};     // the job each player's copy last said it read
    std::uint32_t number = 0;               // the job both have read
    std::int64_t  from = -1;                // its first tick
    std::uint32_t end_ready[3] = {0, 0, 0}; // the job each player's copy last said it saw end
    std::uint32_t end_number = 0;           // the job both have seen end
    std::int64_t  end_from = -1;            // the tick it ends on
};
ServeAgree g_serve_agree;

void ApplyServeRead(int player, const NetCommand &command, std::int64_t tick) {
    if (player < 1 || player > 2) {
        return;
    }
    if ((command.c & kServeEndBit) != 0) {
        const std::uint32_t number = command.c & ~kServeEndBit;

        g_serve_agree.end_ready[player] = number;
        if (g_serve_agree.end_ready[1] == number && g_serve_agree.end_ready[2] == number && g_serve_agree.end_number != number) {
            g_serve_agree.end_number = number;
            g_serve_agree.end_from = tick + 1;
            std::fprintf(stderr, "qa-job: end %u agreed at tick %lld: both copies have seen it\n", (unsigned) number, (long long) tick);
        }
        return;
    }
    g_serve_agree.ready[player] = command.c;
    if (g_serve_agree.ready[1] == command.c && g_serve_agree.ready[2] == command.c && g_serve_agree.number != command.c) {
        g_serve_agree.number = command.c;
        g_serve_agree.from = tick + 1;
        std::fprintf(stderr, "qa-job: %u agreed at tick %lld: both copies have read it\n", (unsigned) command.c, (long long) tick);
    }
}

void ServeFrameStart() {
    static const char *const dir = TestEnv("DC_QA_SERVE");
    static int               next = 1, every = 0;
    static ServeJob          job;

    if (dir == nullptr) {
        return;
    }
    // (from the first frame: the other copy's word may be applied here before this copy has read the job, and a word with no
    // handler is lost, the pair then waiting on it for good)
    NetplaySetCommandHandler(kCmdServe, ApplyServeRead);
    const std::string base = std::string(dir) + "/";

    // coop-fx51a: the job's begin is looked for every frame, on its link tick (both copies of a pair the same tick: the
    // lockstep holds them alike, as it holds the load and the catch-up below); only the files are polled every 30 frames.
    // A job a copy reads after its at has passed is refused, said, not begun late: one copy of a pair beginning on a later
    // tick than the other parts them (x-fixture-fidelity: "read 2 (at 1050)", begun at 1079). The harness gives it a later at.
    // coop-fx51a (coordinator 2026-11-05: the harness picks a job's at from the tick the copies stand on now, with no
    // waiting on logs): a running copy writes the link tick it stands on (idle or in a job: a job's end is looked for every 30 frames, so the next job's at must come from now) to DIR/tick-p<player>.txt every kTickFileFrames
    // frames, and looks for the next job file every frame, so a job's read takes one frame, its at the file's tick plus
    // the two windows (lib\Serve.ps1 Get-ServeJobAt). DC_NETPLAY_MUTATE=serve_no_tickfile: no file (the harness then
    // falls back to the logs' pace).
    constexpr int kTickFileFrames = 10;
    static int    tick_every = 0;

    if (++tick_every >= kTickFileFrames && !NetplayMutated("serve_no_tickfile")) {
        tick_every = 0;
        std::ofstream tick_file(PathsFromUtf8((base + "tick-p" + std::to_string(g_link.player) + ".txt").c_str()), std::ios::trunc);
        // (with the wall clock's milliseconds it was written at and this copy's pace over the last second, so the harness
        // carries the tick forward by the file's age: a copy unpaced runs some 400 ticks a second)
        static std::int64_t pace_tick = -1;
        static auto         pace_at = std::chrono::steady_clock::now();
        static double       pace = 0.0;
        const auto          now = std::chrono::steady_clock::now();
        const double        span = std::chrono::duration<double>(now - pace_at).count();

        if (pace_tick < 0 || span >= 1.0) {
            if (pace_tick >= 0 && span > 0.0) {
                pace = (double) (StateTick() - pace_tick) / span;
            }
            pace_tick = StateTick();
            pace_at = now;
        }
        const long long wall_ms = (long long) std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

        tick_file << "tick " << StateTick() << " next " << next << " every " << kTickFileFrames << " ms " << wall_ms << " pace " << (long long) pace << "\n";
    }
    if (job.number == 0) {
        if (!ServeReadJob(base, next, job)) {
            return;
        }
        std::fprintf(stderr, "qa-job: read %d (at %lld)\n", job.number, (long long) job.at);
        if (g_link.up && !NetplayMutated("serve_no_agree")) {
            NetCommand command;

            command.kind = kCmdServe;
            command.c = (std::uint32_t) job.number;
            job.agreed = true;
            std::fprintf(stderr, "qa-job: %d read at tick %lld%s: the pair agrees on its begin\n", job.number, (long long) StateTick(),
                         StateTick() > job.at ? ", past its at" : "");
            NetplayQueueCommand(command);
        } else if (StateTick() > job.at) { // (a copy alone; serve_late_begin, its twin, went with coop-loadreds)
            std::fprintf(stderr, "qa-job: refused %d: read at tick %lld, past its at %lld\n", job.number, (long long) StateTick(), (long long) job.at);
            next = job.number + 1;
            job = ServeJob{};
            return;
        }
    }
    if (ServePendingLoad(job)) {
        return;
    }
    if (!job.begun) {
        // (not while a resync's load or a catch-up is under way, as a state file's load; a pair's job once both have read it)
        // (agreed past its at: the at's place against the checksum ticks kept, the next such tick, as x-fixture-fidelity
        // phases a job's load on the fresh pair's)
        if (job.agreed) {
            if (g_serve_agree.number != (std::uint32_t) job.number) {
                return;
            }
            const std::int64_t from = g_serve_agree.from;
            const std::int64_t begin = from <= job.at ? job.at : job.at + (from - job.at + kChecksumTicks - 1) / kChecksumTicks * kChecksumTicks;

            if (StateTick() < begin) {
                return;
            }
        }
        if (StateTick() < job.at || g_link.load_hold || g_catch.active) {
            return;
        }
        job.begun = true;
        ServeBegin(job);
        return;
    }
    // coop-warmconvert (test-time plan WS7 step 3, 2026-11-06: a job's end took up to 30 frames to be seen, 1.2 s on a pair
    // running 25 ticks a second, every row): looked for every frame, as the next job is. DC_NETPLAY_MUTATE=serve_end_poll30,
    // warm-repeat-selftest~endpoll's twin: every 30 frames, as before.
    static const int end_every = NetplayMutated("serve_end_poll30") ? 30 : 1;

    if (!job.end_seen) {
        if (++every < end_every) {
            return;
        }
        every = 0;
        if (!std::ifstream(PathsFromUtf8((base + "end-" + std::to_string(job.number) + ".txt").c_str())).good()) {
            return;
        }
        job.end_seen = true;
        if (job.agreed && g_link.up && !NetplayMutated("serve_end_own")) {
            NetCommand command;

            command.kind = kCmdServe;
            command.c = (std::uint32_t) job.number | kServeEndBit;
            job.end_agreed = true;
            std::fprintf(stderr, "qa-job: %d end seen at tick %lld: the pair agrees on its end\n", job.number, (long long) StateTick());
            NetplayQueueCommand(command);
        }
    }
    if (job.end_agreed && (g_serve_agree.end_number != (std::uint32_t) job.number || StateTick() < g_serve_agree.end_from)) {
        return;
    }
    {
        std::fprintf(stderr, "qa-job: end %d at tick %lld (its file looked for every %d frames)\n", job.number, (long long) StateTick(), end_every);
        if (g_serve_input_base >= 0) {
            InputScriptInstall(InputScript{}); // (the pads back to the copy's own)
            CoopInputAnchorsInstall({});
            g_serve_input_base = -1;
        }
        TestEnvClearJob();
        std::fflush(stderr);
        std::fflush(stdout);
        next = job.number + 1;
        job = ServeJob{};
    }
}

bool g_state_load_pending = TestEnv("DC_STATE_LOAD_AT") != nullptr; // coop-fixtures: a state file still to be put back

bool StateFileLoadPending() {
    return g_state_load_pending;
}

static bool dc_coop_ring_chooser_up() {
    return dc::battlemenu::BattleMenuQuitConfirmActive();
}

// coop-pt1106-lockup test hook (DC_NETPLAY_TEST_HOST_MENU=1, x-fixture-join -Case hostmenu): a copy alone, under control after its
// fixture's load, opens the game's menu on its first pad and leaves it open; the joining copy starts on this hook's line.
// =page: and takes the first page of its ring with Cross (a floor's menu, the Items page) before saying so.
// =apart: a copy apart on a floor, once it is apart (x-floor-hermenu: player 2 stayed where player 1 left).
void TestHostMenuFrame() {
    static const bool  on = TestEnvOn("DC_NETPLAY_TEST_HOST_MENU");
    static const char *how = TestEnv("DC_NETPLAY_TEST_HOST_MENU");
    // =apartpage: both (x-town-stayermenu: an apart menu inside its page, which the merge must leave there).
    static const bool  page = how != nullptr && (std::strcmp(how, "page") == 0 || std::strcmp(how, "apartpage") == 0);
    static int        pressed = 0;
    static bool       done = false;

    // =apartbuild: his Georama apart, by his Select (x-town-stayermenu -Build).
    // =apartbuildmenu (coop-p1parts): and his parts menu open in it, by his Triangle.
    // =buildoption (coop-soakfix4, test_fixture_join.py::test_join_his_options): alone in town, his Georama, its parts menu and
    // there its Option page (the port's Options screen), left open; the joiner starts on this hook's line.
    static const bool  option = how != nullptr && std::strcmp(how, "buildoption") == 0;
    // =partscatchup (coop-stabfix1, test_fixture_join.py::test_parts_close_mid_catchup): alone in town, his Georama and its parts
    // menu open (the joiner starts on this hook's line); once his state set for her is taken, his own Circles shut it mid catch-up.
    static const bool  shut_mid = how != nullptr && std::strcmp(how, "partscatchup") == 0;
    static const bool  parts = how != nullptr && (std::strcmp(how, "apartbuildmenu") == 0 || option || shut_mid);
    static const bool  build = how != nullptr && (std::strcmp(how, "apartbuild") == 0 || parts);
    // =apartchooser (coop-pt1109d, x-town-stayermenu -SaveChooser): apart, his town ring walked to Save and its save-or-load chooser up by his Cross, left so.
    static const bool  chooser = how != nullptr && std::strcmp(how, "apartchooser") == 0;
    static const bool  apart = how != nullptr && (std::strcmp(how, "apart") == 0 || std::strcmp(how, "apartpage") == 0 || (build && !option && !shut_mid) || chooser);
    // =catchup (coop-soakfix4, test_fixture_join.py::test_menu_into_lockstep): the host, once its state set is taken and the
    // joiner still catching up, opens its menu (the menu beside the world, the link not yet its players') and leaves it open.
    static const bool  catchup = how != nullptr && std::strcmp(how, "catchup") == 0;
    // =pretake (coop-repinfix2, test_fixture_join.py::test_menu_before_take; soak leave 2239): the same, but once linked and
    // before its state set is taken: with her body on his copy (a rejoin) it is the menu beside the world, open as the take comes.
    static const bool  pretake = how != nullptr && std::strcmp(how, "pretake") == 0;

    g_test_take_held = pretake && !done;

    // DC_NETPLAY_TEST_HOST_MENU_AGAIN=1 (with =apartchooser): once that menu has gone on through the merge and closed, his Triangle opens the next, and 90 frames on
    // (its ring up, on page 0, the Items page) one Cross.
    static const bool  again = TestEnvOn("DC_NETPLAY_TEST_HOST_MENU_AGAIN");
    // (=buildoption: his own Circle 60 ticks into the lockstep closes the page: the Options screen reads his own pad (InputGetPad);
    // the menu's own close reads the routed pad (the row's DC_COOP_MENUPRESS))
    if (option && done) {
        static int  circles = 0;
        const bool  in = g_link.up && !g_catch.active && g_test_lockstep_from >= 0;
        const auto  at = in ? g_link.tick - g_test_lockstep_from : -1;
        const bool  press = at >= 60 && at < 64;

        if (press) {
            InputPadState pad{};

            pad.connected = true;
            pad.left_x = pad.left_y = pad.right_x = pad.right_y = 0x80;
            pad.buttons = kInputCircle;
            InputSetOverride(0, &pad);
            if (at == 60) {
                std::fprintf(stderr, "netplay: test: the host's own Circle %d at link tick %lld (town mode %d, menu page %d, options open %d step %d)\n", ++circles,
                             (long long) g_link.tick, (int) ::GameMode, EditMenuPage(), dc::options::g_screen.open ? 1 : 0, (int) dc::options::g_screen.step);
            }
        } else {
            InputSetOverride(0, nullptr);
        }
        return;
    }
    if (shut_mid && done) {
        static int circles = 0;
        const bool mid = g_link.up && g_catch.active && SendsState() && g_snap.taken && ::GameMode == ED_MODE_GEORAMA_MENU;

        if (mid) {
            InputPadState pad{};

            pad.connected = true;
            pad.left_x = pad.left_y = pad.right_x = pad.right_y = 0x80;
            if (circles++ % 8 < 4) {
                pad.buttons = kInputCircle;
            }
            if (circles == 1) {
                std::fprintf(stderr, "netplay: test: the host's Circle on his parts menu mid catch-up at link tick %lld\n", (long long) g_link.tick);
            }
            InputSetOverride(0, &pad);
        } else {
            InputSetOverride(0, nullptr);
        }
        return;
    }
    if (again && done) {
        static int stage = 0;
        static int frames = 0;
        const bool in = coop::PlayerInMenu(1);
        InputPadState pad{};

        pad.connected = true;
        pad.left_x = pad.left_y = pad.right_x = pad.right_y = 0x80;
        if (stage == 0 && g_link.up && in) {
            stage = 1;
        } else if (stage == 1 && g_link.up && !in) {
            stage = 2;
        } else if (stage == 2 && in) {
            stage = 3;
        }
        if (stage == 2) {
            if (::GameMode == ED_MODE_WALK && pressed++ % 8 < 4) {
                pad.buttons |= kInputTriangle;
            }
            InputSetOverride(0, &pad);
        } else if (stage == 3) {
            if (++frames == 90) {
                std::printf("netplay: test: the first Cross of his next menu\n");
            }
            if (frames >= 90 && frames < 94) {
                pad.buttons |= kInputCross;
            }
            if (frames < 94) {
                InputSetOverride(0, &pad);
            } else {
                InputSetOverride(0, nullptr);
                stage = 4;
            }
        }
        return;
    }


    if (!on || done ||
        (apart     ? !CoopWorldsApart()
         : catchup ? !(g_link.up && g_catch.active && SendsState() && g_snap.taken)
         : pretake ? !(g_link.up && g_catch.active && SendsState() && g_snap.on && !g_snap.taken)
                   : g_link.up) ||
        g_state_load_pending) {
        return;
    }
    // (=pretake: done once his menu is open by any road (the take's turn to a local menu sets the world's mode back to play))
    if (pretake && coop::PlayerInMenu(1)) {
        InputSetOverride(0, nullptr);
        done = true;
        g_test_take_held = false;
        std::fprintf(stderr, "netplay: test: the host's menu is open before the take (link tick %lld, beside the world %d, mode %d)\n",
                     (long long) g_link.tick, BtGameModeFlag == BT_GAME_MODE_BATTLE_MENU ? 1 : 0, (int) mode);
        return;
    }

    const bool town = mode == GAME_MODE_EDIT && EdInteriorFlag == 0;
    const bool floor = mode == GAME_MODE_DUNGEON;

    const bool room = mode == GAME_MODE_EDIT && EdInteriorFlag != 0; // (coop-stage4 4c: x-town-stayermenu -Indoors)

    if (parts && town && (::GameMode == ED_MODE_GEORAMA || ::GameMode == ED_MODE_GEORAMA_MENU_INIT)) {
        InputPadState pad{};

        pad.connected = true;
        pad.left_x = pad.left_y = pad.right_x = pad.right_y = 0x80;
        if (::GameMode == ED_MODE_GEORAMA && pressed++ % 8 < 4) {
            pad.buttons |= kInputTriangle;
        }
        InputSetOverride(0, &pad);
        return;
    }
    if (option && town && ::GameMode == ED_MODE_GEORAMA_MENU) {
        // (its page cursor on Option, the fifth of the six (EditMenuSelect's next[]), then its Cross; done once the page runs)
        InputPadState pad{};

        pad.connected = true;
        pad.left_x = pad.left_y = pad.right_x = pad.right_y = 0x80;
        if (static int said = -1; said != EditMenuPage()) {
            said = EditMenuPage();
            std::fprintf(stderr, "netplay: test: the host's Georama menu on page %d (cursor %d)\n", EditMenuPage(), EditMenuCursor());
        }
        if (EditMenuOnSelect()) {
            EditMenuSetCursor(4);
            if (pressed++ % 8 < 4) {
                pad.buttons |= kInputCross;
            }
        } else if (EditMenuOnOption() && dc::options::g_screen.open && dc::options::g_screen.step == OPTION_STEP_RUN) {
            InputSetOverride(0, nullptr);
            done = true;
            std::fprintf(stderr, "netplay: test: the host's Georama Option page is open (menu page %d)\n", EditMenuPage());
            return;
        }
        InputSetOverride(0, &pad);
        return;
    }
    if (build && town && ::GameMode == (parts ? ED_MODE_GEORAMA_MENU : ED_MODE_GEORAMA)) {
        InputSetOverride(0, nullptr);
        // (=partscatchup: its opening run first, 60 frames: till then the menu takes no Circle, and her catch-up is ~20 ticks)
        if (static int settling = 0; shut_mid && ++settling < 60) {
            return;
        }
        done = true;
        std::fprintf(stderr, "netplay: test: the host's Georama is open (mode %d)\n", (int) mode);
        return;
    }

    if ((town && ::GameMode == ED_MODE_MENU) || (room && CoopTownRoomModeNow() == 7) || (floor && BtGameModeFlag == BT_GAME_MODE_BATTLE_MENU)) {
        if (BtlMenuReadEndFlag != 0 && page && BattleMenuFlag != BTLMENU_STATE_ITEM) {
            InputPadState pad{};

            pad.connected = true;
            pad.left_x = pad.left_y = pad.right_x = pad.right_y = 0x80;
            if (BattleMenuFlag == BTLMENU_STATE_MAIN && pressed++ % 8 < 4) {
                pad.buttons |= kInputCross;
            }
            InputSetOverride(0, &pad);
            return;
        }
        if (BtlMenuReadEndFlag != 0 && chooser && !dc_coop_ring_chooser_up()) {
            InputPadState pad{};

            pad.connected = true;
            pad.left_x = pad.left_y = pad.right_x = pad.right_y = 0x80;
            if (BattleMenuFlag == BTLMENU_STATE_MAIN && pressed++ % 8 < 4) {
                pad.buttons |= MenuSelect[1] == 5 ? kInputCross : kInputUp;
            }
            InputSetOverride(0, &pad);
            return;
        }
        if (BtlMenuReadEndFlag != 0) {
            InputSetOverride(0, nullptr);
            done = true;
            std::fprintf(stderr, "netplay: test: the host's menu is open (mode %d, task %#x, page state %d)\n", (int) mode, (unsigned) gameTask, (int) BattleMenuFlag);
        }
        return;
    }
    if (!(town && ::GameMode == ED_MODE_WALK) && !(room && CoopTownIndoorWalks()) && !(floor && gameTask == GAME_TASK_PLAY && BtGameModeFlag == BT_GAME_MODE_PLAY)) {
        return;
    }

    InputPadState pad{};

    pad.connected = true;
    pad.left_x = pad.left_y = pad.right_x = pad.right_y = 0x80;
    if (pressed++ % 8 < 4) {
        pad.buttons |= build ? kInputSelect : kInputTriangle;
    }
    InputSetOverride(0, &pad);
}

void StateFileFrameStart() {
    TestHostMenuFrame();
    static StateFileSpec save = ParseStateSpec("DC_STATE_SAVE_AT");
    static StateFileSpec load = ParseStateSpec("DC_STATE_LOAD_AT");
    g_state_load_pending = load.at >= 0 && !load.done;
    static std::string   load_place;
    static std::uint32_t load_sum = 0;
    static std::vector<unsigned char> load_packed;
    static bool          load_read = false;

    static const bool    present_test = TestEnv("DC_COOP_TEST_PRESENT_RELOAD") != nullptr; // coop-opt7 (x-opener-local -Case reload)
    static std::vector<unsigned char> mid_packed;
    static std::uint32_t mid_sum = 0;

    if (save.at >= 0 && !save.done && StateTick() >= save.at && SnapshotQuietTick()) {
        StateFileSave(save);
    }
    // coop-opt7 (tests, DC_COOP_TEST_PRESENT_RELOAD; coop_opener.cpp PresentTestStateDue; coop-atlasync's hook before it): the
    // state set taken to memory midway through a presentation, and loaded again later in it (the one taken midway, else
    // DC_STATE_LOAD_AT's file), on the frame after the tick that asked: alike on both copies of a linked pair.
    if (const int due = present_test ? coop::PresentTestStateDue() : 0; due == 1) {
        std::vector<unsigned char> raw;
        std::string                why;

        StateFileTakeParts();
        if (SnapshotTakeBytes(raw, why, SnapExtras())) {
            SnapshotPack(raw, mid_packed);
            mid_sum = Checksum();
            std::fprintf(stderr, "coop-state: test: the state set taken midway through her Atla at tick %lld, checksum %08x\n", (long long) StateTick(), mid_sum);
        } else {
            std::fprintf(stderr, "coop-state: test: no state set midway through her Atla (%s)\n", why.c_str());
        }
    } else if (due == 2) {
        char place[160];

        PlaceNow(place, sizeof place);
        std::fprintf(stderr, "coop-state: test: loading again as her Atla's name waits (%s)\n", mid_packed.empty() ? "the file's" : "the one taken midway");
        StateFileApply(mid_packed.empty() ? load.path : std::string("her Atla midway"), place, mid_packed.empty() ? load_packed : mid_packed,
                       mid_packed.empty() ? load_sum : mid_sum);
    }
    if (load.at < 0 || load.done) {
        return;
    }
    if (!load_read) {
        load_read = true;
        if (!StateFileRead(load.path, load_place, load_sum, load_packed)) {
            load.done = true;
            return;
        }
        std::fprintf(stderr, "coop-state: %s: %zu packed bytes taken in %s, checksum %08x\n", load.path.c_str(), load_packed.size(), load_place.c_str(), load_sum);
    }
    char place[160];

    PlaceNow(place, sizeof place);
    // (not on a quiet tick: the whole state set, its scene's VM too, goes over whatever runs, as a catch-up's on landing;
    // not while a resync's load or a catch-up is under way: a link's resync loads the place again, undoing a load before it)
    if (StateTick() < load.at || load_place != place || g_link.load_hold || g_catch.active) {
        return;
    }
    load.done = true;
    // (the shared settings, config.json's and the launcher's switches (NetplaySettingsHash), are in the checksum and carried
    // by a resync, not in the state set: a copy whose settings differ from the maker's cannot give the saved checksum back)
    if (g_state_read_settings != 0) {
        std::fprintf(stderr, "coop-state: settings here %08x, the file's %08x: %s\n", NetplaySettingsHash(), g_state_read_settings,
                     NetplaySettingsHash() == g_state_read_settings ? "alike" : "DIFFERENT");
    }
    const bool loaded = StateFileApply(load.path, place, load_packed, load_sum);
    coop::StandInRegrantAfterLoad(); // coop-fx51b (tests: DC_COOP_TEST_STANDIN_REGRANT)
    // coop-quitcrash (x-fixture-join: a host alone on a fixture, a joiner after it): the loaded state is this host's
    // checkpoint, as a floor's start is (NetplayFloorStarted), so a join catches up to it by snapshot instead of the
    // reloading resync a host with no checkpoint gives.
    // (a place checkpoint on its floor, recording from here at once, as a merge's own place checkpoint does: the floor-start
    // kind waits for the floor's start, long past, and the host kept the joiner waiting "the checkpoint's place is still
    // loading")
    // coop-fx51b (p-p2-absent-town, n-join-partner: a town host alone on a fixture): in a town too, a place checkpoint as
    // a town's own is (CheckpointHere's town case: no floor).
    const bool town_alone = mode == GAME_MODE_EDIT;
    if (!g_link.up && CatchupArmed() && ((mode == GAME_MODE_DUNGEON && UserStatus != nullptr) || town_alone)) {
        Checkpoint(town_alone ? -1 : UserStatus->cur_floor, false, true);
        if (g_solo.due) {
            g_solo.on = true;
            g_link.tick = 0;
            g_link.synced[0] = g_link.synced[1] = g_link.previous[0] = g_link.previous[1] = Neutral();
            g_link.menu_owner = 1;
            g_link.held = 0;
            if (town_alone) {
                std::fprintf(stderr, "coop-state: this host alone: the loaded state is its checkpoint (town %d), a join catches up to it\n", (int) MapNo);
            } else {
                std::fprintf(stderr, "coop-state: this host alone: the loaded state is its checkpoint (floor %d), a join catches up to it\n", (int) UserStatus->cur_floor + 1);
            }
        }
    }
    // coop-fx51e (v-floor-title on a fixture): DC_FLOOR_TITLE_ON=loaded starts the floor's title on the load, as the floor's
    // own script starts it on arrival (FloorTitleOn, btsysscript's), and marks the frame series there.
    if (static const char *const title_on = TestEnv("DC_FLOOR_TITLE_ON"); loaded && title_on != nullptr && std::strcmp(title_on, "loaded") == 0) {
        FloorTitleOn();
        NetplayTestSeriesMark();
        std::fprintf(stderr, "floor-title: test: started on the fixture's load\n");
    }
    // coop-pt1111h (x-floor-join-late -Zone): DC_COOP_TEST_LIMIT_ZONE_LOADED=Z puts the loaded floor in limited zone Z and raises its
    // message (CDngMessageMan::LimmitZone, as the floor's start does), on the load itself: in the state a join's snapshot takes
    // (coop_combat's DC_COOP_TEST_LIMIT_ZONE waits for the partner's step, which a host alone only has once linked).
    if (static const int zone = TestEnvInt("DC_COOP_TEST_LIMIT_ZONE_LOADED", -1); g_state_file_loaded && zone >= 0 && UserStatus != nullptr) {
        UserStatus->res_limit_zone_current = zone;
        DngMessMan.LimmitZone(); // present: everyone's (the floor's own rule message, as its start raises it: FloorRuleMessage, owner 0)
        std::fprintf(stderr, "coop-test: limited zone %d: the dungeon's message %d raised (timer %d) on the load, link tick %lld\n", zone,
                     (int) DngMessMan.message, (int) DngMessMan.timer, (long long) g_link.tick);
    }
    load_packed.clear();
    load_packed.shrink_to_fit();
}
} // namespace

bool NetplayStateFileLoaded() { return g_state_file_loaded; }

bool NetplayJobStateFileLoaded() { return g_state_file_loads > g_state_file_loads_at_job; }

bool NetplayStateFileApplying() { return g_state_file_applying; }

bool NetplayStateFileChecked() {
    return g_state_file_loaded && NetplayLinkTick() > (g_state_file_tick / kChecksumTicks + 1) * kChecksumTicks;
}

void NetplayFrameStart() {
    // (the copy's own frames on its lines, linked or apart: the link tick stands still while apart, fx51d 2026-11-05)
    TestLogFrame((long long) dc::app::GameFrameCount());
    GlobalsDumpFrame(); // coop-fx51a: DC_QA_GLOBALS_DUMP
    TownAgainFrame();   // coop-fx51a: DC_COOP_TEST_TOWN_AGAIN
    QaStopSeen();          // coop-qastop (DC_QA_STOP_FILE)
    StateFileFrameStart(); // coop-stateload
    ServeFrameStart();     // coop-serve
    if (static bool checked = false; !checked) {
        checked = true;
        if (TestEnvInt("DC_NETPLAY_NAT_CHECK", 0) != 0) {
            NatCodecCheck();
        }
    }
    // The hold ends at the first frame in the resync's place itself (a joiner from the title passes a mode on the way, whose
    // one frame let tick 34 run before her town: one tick behind the host for good, 2026-10-26), or after kLoadWaitMs.
    if (g_link.load_hold) {
        static auto held_since = std::chrono::steady_clock::time_point{};

        if (held_since == std::chrono::steady_clock::time_point{}) {
            held_since = std::chrono::steady_clock::now();
        }
        const bool there = RestartTarget(mode, MapNo, selectMapNo) == g_link.resync_target;
        const bool late = std::chrono::steady_clock::now() - held_since > std::chrono::milliseconds(kLoadWaitMs);

        if (there || late) {
            g_link.load_hold = false;
            held_since = {};
            std::fprintf(stderr, "netplay: resync load done: the lockstep goes on from tick %lld (player %d, %s)\n", (long long) g_link.tick, g_link.player,
                         there ? "in the resync's place" : "the wait ran out");
        }
    }
    TripFrameStart(); // coop-snapshot
    SnapFrameStart(); // coop-snapshot stage 4

    // coop-floorjoin2 (tests): the frame after the join (NetplayTestShotAfterFrame writes it, outside the frame).
    if (g_shot_walk_after >= 0 && coop::ArrivalWalkPlayer() == 0) {
        g_shot_due = g_link.tick + g_shot_walk_after;
        std::fprintf(stderr, "netplay: test: the arrival walk played out by tick %lld: the frame after the join is due at tick %lld\n",
                     (long long) g_link.tick, (long long) g_shot_due);
        g_shot_walk_after = -1;
    }
    if (g_shot_due >= 0 && g_link.up && !g_catch.active && g_link.tick >= g_shot_due) {
        // coop-onher: "%d" in the path is the shot's number (1 the join's, 2 the next catch-up's ...): a later merge's frame
        // no longer writes over the join's.
        static int shot_no = 0;

        g_test_shot = TestEnv("DC_COOP_SHOT_PATH");
        if (const std::size_t at = g_test_shot.find("%d"); at != std::string::npos) {
            g_test_shot.replace(at, 2, std::to_string(++shot_no));
        }
        std::fprintf(stderr, "netplay: test: the frame at tick %lld, after the join, goes to %s\n", (long long) g_link.tick, g_test_shot.c_str());
        // coop-toanvis: model blocks whose meshes were not this copy's own models any more (a state set over a copy that had
        // changed its player), and how many were built again (w-town-apart -P2Follows -HerShotAt).
        std::fprintf(stderr, "draw3d: by the frame after the join: %d draws of a stale model block, %d blocks built again\n", dc::render::Draw3DStaleVisuals(),
                     dc::render::Draw3DRebuiltVisuals());
        // coop-joinvisuals: where the frame before it put the two (the tests' frame checks: her body, his).
        int spots[8];

        CoopScreenSpots(spots);
        std::fprintf(stderr, "coop-debug: screen spots at the shot: player feet (%d %d) head (%d %d), partner feet (%d %d) head (%d %d)\n", spots[0],
                     spots[1], spots[2], spots[3], spots[4], spots[5], spots[6], spots[7]);
        g_shot_due = -1;
    }
}

// coop-natsplit (Jep, 2026-10-28: "if you hit NAT Join you have no way of cancelling that"): a join from the title not yet
// answered (an address, a NAT code or NAT > Steam, punching or on Steam's way) is given up with Circle or Triangle, as the
// entry's back: no more Hellos, Steam shut down (its lobby left), and the title's menu as before the join.
// DC_NETPLAY_MUTATE=join_no_cancel (twin): no way back, as before.
// The join from the title given up (Circle, or a NAT > Steam punch that never got through): back to the title's menu,
// with a note under its line for a while when one is given.
std::string                           g_title_note;
std::chrono::steady_clock::time_point g_title_note_until{};

void JoinGiveUp(const char *how, const char *note) {
    std::fprintf(stderr, "netplay: join %s (%s) after %.0f s: no more Hellos, back to the title's menu%s%s\n", how,
                 g_nat == 4 ? "NAT > Steam" : g_nat == 2 ? "NAT > Code" : "by address",
                 std::chrono::duration<double>(std::chrono::steady_clock::now() - g_link.epoch).count(), note != nullptr ? ": " : "",
                 note != nullptr ? note : "");
    if (note != nullptr) {
        g_title_note = note;
        g_title_note_until = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    }
    g_link.listening = false;
    g_link.started = false;
    g_join_picked = false;
    g_role = 0;
    NetSessionTo(NetSession::Title, how); // coop-session
    g_link.status[0] = '\0';
    if (g_link.diag != nullptr) {
        std::fclose(g_link.diag);
        g_link.diag = nullptr;
    }
    NetSteamStop("the join given up");
    g_nat = 0;
    g_nat_friend.clear();
    g_nat_steam_host = 0;
    g_nat_times = NatSteamTimes{};
    g_punch = PunchLog{};
    g_join_peer.clear();
}

// coop-quittitle (Jep's rule, 2026-11-10): Quit Game from the in-game menu is a command (kCmdQuit) every linked copy applies on
// one link tick, the sender being the player who quit. The host's (player 1's) ends the session for every player: each copy
// leaves for the title at that frame's end, its link closed (NetplayTakeQuitToTitle). Any other player's takes that player
// alone to the title: its copy says its Leave and closes; every other copy plays on and hears that Leave as it hears a copy
// whose game was closed ("link down ...: player N left", Close), the same path. Alone (not linked: a host waiting, apart,
// dropped) the command applies at once on this copy, which leaves its session for the title. Per player, for any number.
// DC_NETPLAY_MUTATE=quit_host_alone (net-quit-host's twin): the host's quit is any player's (only the host's copy leaves, the
// others are left hanging); quit_ends_all (net-quit-p2's twin): anyone's ends the session for all.
void ApplyQuit(int player, const NetCommand &, std::int64_t tick) {
    const int  local = g_link.player > 0 ? g_link.player : 1;
    const bool ends = (player == 1 && !NetplayMutated("quit_host_alone")) || NetplayMutated("quit_ends_all");

    if (ends) {
        std::fprintf(stderr, "netplay: quit: %s (player %d) quit at link tick %lld: the session ends for every player; this copy (player %d) goes to the title\n",
                     player == 1 ? "the host" : "a guest (quit_ends_all)", player, (long long) tick, local);
        g_quit_title = true;
        g_quit_ends = true;
        g_quit_tick = tick;
        g_quit_note = player == local ? "You quit: the session has ended for every player" : "The host quit: the session has ended";
    } else if (player == local) {
        std::fprintf(stderr, "netplay: quit: player %d quit at link tick %lld: this copy leaves the session for the title; the others play on\n", player,
                     (long long) tick);
        g_quit_title = true;
        g_quit_ends = false;
        g_quit_tick = tick;
        g_quit_note = "You left the session";
    } else {
        std::fprintf(stderr, "netplay: quit: player %d quit at link tick %lld: their copy leaves the session; this copy (player %d) plays on\n", player,
                     (long long) tick, local);
    }
    std::fflush(stderr);
}

// coop-quittitle: set as this copy's session ended (NetplayTakeQuitToTitle); its first tick at the title says so, once.
bool g_quit_at_title_due = false;

void QuitAtTitleTick() {
    if (!g_quit_at_title_due || mode != GAME_MODE_TITLE) {
        return;
    }
    g_quit_at_title_due = false;
    std::fprintf(stderr, "netplay: quit: this copy is at the title, its link closed (session %s, link %s); its line: \"%s\"\n", NetSessionName(NetSessionNow()),
                 g_link.up ? "up" : g_link.listening ? "listening" : "closed", NetplayTitleNote());
    std::fflush(stderr);
}

// coop-joinfix (LAN playtest 2026-10-30, item 9): only a join that never linked. Apart (the link down after a split) her copy is
// still a listening player 2 that picked Join, and her Circle or Triangle in her own town menu gave the join up: her copy then
// hosted alone as player 1 and never merged again. coop-session: the session says so (Joining, never linked).
// Her apart presses in tests (DC_WORLDS_TEST_APART_PRESS_P2) come in as her controller's (CoopWorldsTestPadButtons, 0 otherwise).
// DC_NETPLAY_MUTATE=cancel_apart (twin): any listening join, as before.
// coop-lobbyleave (LAN pack 8, 2026-11-04): nor in a host's lobby (coop_lobby.hpp GuestIn), whose link comes up only at the
// host's start: his Circle there is the lobby's (its Leave? question), and only a Yes there leaves (NetplayLobbyLeaveJoin).
// The friend's Circle gave the join up unseen and the host dropped his seat 10 s later. DC_NETPLAY_MUTATE=lobby_cancel
// (l-lobby -Case guest-pad's twin): cancelled in the lobby too, as before.
static void JoinCancelTick() {
    static std::uint16_t last = 0;
    const std::uint16_t  buttons = (std::uint16_t) (OwnInput(2).buttons | CoopWorldsTestPadButtons());
    const std::uint16_t  down = buttons & ~last;

    last = buttons;
    if (!JoinMenu() || !g_join_picked || !g_link.started || g_link.up || !g_link.listening || g_role != 2 || g_catch.active ||
        (NetSessionNow() != NetSession::Joining && !NetplayMutated("cancel_apart") && !NetSessionOff()) || (down & (kInputCircle | kInputTriangle)) == 0 ||
        NetplayMutated("join_no_cancel") || (coop::lobby::GuestIn() && !NetplayMutated("lobby_cancel"))) {
        return;
    }
    JoinGiveUp("cancelled", nullptr);
}

// coop-natonly: a NAT join's guest (code or Steam) whose direct link dropped and which has heard nothing at all from the
// host for 30 s: the host is gone or the way is shut (a network change). It keeps saying Hello (the host may come back), and
// says so on screen, again each minute, rather than linking by Steam (Jep, 2026-10-29: Steam is the NAT handoff only).
// Test hook DC_NETPLAY_TEST_NAT_CUT=T: every datagram dropped from link tick T on (NetSocketTestCut).
// DC_NETPLAY_MUTATE=nat_lost_silent (twin): no note, as before.
static void NatLostTick() {
    static const int cut_at = TestEnvInt("DC_NETPLAY_TEST_NAT_CUT", -1);
    static bool      cut = false;

    if (cut_at >= 0 && !cut && g_link.up && g_link.tick >= cut_at) {
        cut = true;
        NetSocketTestCut();
        std::fprintf(stderr, "netplay: test: NAT cut at link tick %lld: every datagram dropped from now on\n", (long long) g_link.tick);
    }

    static long long                             heard_was = -1;
    static std::chrono::steady_clock::time_point heard_at{}, next_note{};
    long long                                    sent = 0, heard = 0;
    const char                                  *from = "";
    const auto                                   now = std::chrono::steady_clock::now();

    if ((g_nat != 2 && g_nat != 4) || g_role != 2 || g_links_up == 0 || g_link.up || NetplayMutated("nat_lost_silent")) {
        heard_was = -1;
        return;
    }
    NetSocketUdpCounts(sent, heard, from);
    if (heard != heard_was) {
        heard_was = heard;
        heard_at = now;
        next_note = now + std::chrono::seconds(30);
        return;
    }
    if (now < next_note) {
        return;
    }
    next_note = now + std::chrono::seconds(60);

    static const char *const kNote = "Lost the direct link to the host: nothing heard for a while. Still trying; if they're gone, quit and join again";

    SetNotice(kNote);
    std::fprintf(stderr, "netplay: nat: nothing heard from the host for %.0f s since the link went down: shown \"%s\"; still saying Hello\n",
                 std::chrono::duration<double>(now - heard_at).count(), kNote);
}

void NetplayTick() {
    CostScope      cost{g_costs.tick_ms}; // coop-ws5left
    TickPartsScope parts; // coop-small1b
    if (g_log_hold_tick >= 0 && (g_link.tick >= g_log_hold_tick || NowMs() >= g_log_hold_by)) {
        LogHoldEnd();
    }
    TickPart("the receiver's start");
    CatchReceiverTick(); // coop-joinbudget: the joiner's catch-up receiver on or off
    coop::WriteGuardTick(g_link.tick, g_link.player, g_link.up && !g_catch.active); // coop-review1109 (a test's: DC_QA_WRITE_GUARD)
    TickPart("the hooks");
    // coop-joinbudget (a test's): whether this copy's clock keeps the game's pace, once, under DC_COOP_DEBUG (x-floor-join
    // -Seamless's timings: its copies ran unbounded, 200-480 ticks a second, so a catch-up chased a host no player has).
    if (static bool said = false; !said && CoopDebug()) {
        said = true;
        std::fprintf(stderr, "netplay: test: this copy's clock %s\n", ClockUnbounded() ? "runs unbounded" : "keeps the game's pace");
    }
    TestSceneAtJoin(false); // coop-joinfix (a test hook)
    NetplayHelloLogSha();   // coop-buildid
    coop::ActorMenuSync();     // coop-localmenu: this copy's players' menu opens and closes, as commands
    if (static bool quit_handler = false; !quit_handler) { // coop-quittitle (every copy, before any command can come)
        quit_handler = true;
        NetplaySetCommandHandler(kCmdQuit, ApplyQuit);
    }
    QuitAtTitleTick(); // coop-quittitle: the line once this copy, its session over, stands at the title
    // coop-testspeed2 (Jep, 2026-10-30): DC_HEADLESS_PACED=alone (headless tests): the game's own pace while this copy plays
    // without its partner (a catch-up join needs the host in real time), as fast as the pair can once both play in lockstep
    // (the lockstep keeps them together). 15 of a day's 22 hours of game rows were paced from start to end.
    // DC_NETPLAY_MUTATE=paced_always is the twin.
    static const char *pace = std::getenv("DC_HEADLESS_PACED");
    static const bool   pace_alone = pace != nullptr && std::strcmp(pace, "alone") == 0;
    static int          paced_now = -1;

    if (pace_alone) {
        const int unbounded = NetplayPartnerLinked() && !NetplayMutated("paced_always") ? 1 : 0;

        if (unbounded != paced_now) {
            ClockSetUnbounded(unbounded == 1);
            std::fprintf(stderr, "netplay: test: headless clock %s at tick %lld\n", unbounded ? "unbounded (partner linked)" : "paced (alone)",
                         (long long) g_link.tick);
            paced_now = unbounded;
        }
    }
    // Test hook (DC_NETPLAY_TEST_ALONE_LOG=1, net-quit): after the link went down, a line each 600 ticks this copy plays
    // on alone, so a test sees it still ticking. coop-quittitle: a value over 1 is the line's interval in ticks (net-quit: 60).
    static const bool alone_log = TestEnv("DC_NETPLAY_TEST_ALONE_LOG") != nullptr;
    static const int  alone_every = TestEnvInt("DC_NETPLAY_TEST_ALONE_LOG", 1) > 1 ? TestEnvInt("DC_NETPLAY_TEST_ALONE_LOG", 1) : 600;
    static std::int64_t alone = 0;

    if (alone_log && g_went_down && !g_link.up && ++alone % alone_every == 0) {
        std::fprintf(stderr, "netplay: test: %lld ticks alone since the link went down\n", (long long) alone);
    }

    // Local's player 2: asks for a button until it has a controller.
    static bool asked = false;

    if (asked && OwnPad(2) >= 0) {
        asked = false;
        g_notice.text.clear(); // claimed: the prompt goes at once
        std::fprintf(stderr, "netplay: local: player 2's prompt cleared\n");
    }
    if (TestEnvInt("DC_NETPLAY_LOCAL_CLAIM", -1) >= 0 && OwnPad(2) < 0) {
        asked = true;
        static auto next = std::chrono::steady_clock::now();

        if (std::chrono::steady_clock::now() >= next) {
            next = std::chrono::steady_clock::now() + std::chrono::seconds(4);
            SetNotice("Player 2: press any button on your controller");
        }
    }
    // NAT code, hosting at the title: the socket is open but the link is not reading it yet; read it here, so STUN's
    // answer (the code to show) comes in. (A Hello read here is lost; the joiner sends them again.)
    // coop-lobby: so too the host's lobby, whose guests' messages are read here (coop_lobby.hpp).
    if ((g_nat == 1 || coop::lobby::Listening()) && !g_link.started) {
        alignas(8) unsigned char scratch[2048];
        int                      got;

        while ((got = NetSocketReceive(scratch, sizeof scratch)) > 0) {
            std::uint32_t magic = 0;

            if (got >= 8) {
                std::memcpy(&magic, scratch, 4);
                coop::lobby::Receive(magic, scratch, got);
            }
        }
    }
    coop::lobby::NetTick();

    // NAT code, hosting: towards the friend's mapped address until the link is up, so this router lets their Hello in.
    NatSteamTick();
    NatCodeWaitTick();
    if ((g_nat == 1 || g_nat == 3) && !g_nat_friend.empty() && !g_link.up && !NetplayMutated("no_punch")) {
        static auto next = std::chrono::steady_clock::now();
        static bool said = false;
        auto        now = std::chrono::steady_clock::now();

        if (now >= next) {
            const std::size_t colon = g_nat_friend.rfind(':');

            next = now + std::chrono::milliseconds(250);
            NetSocketPunch(g_nat_friend.substr(0, colon).c_str(), std::atoi(g_nat_friend.c_str() + colon + 1));
            if (!said) {
                said = true;
                std::fprintf(stderr, "netplay: nat: punching towards %s\n", g_nat_friend.c_str());
            }
        }
    }
    PunchLogTick();
    PartnerLogTick(); // coop-partnerlog: this copy's log to the other PC

    NetSteamPump();
    SteamPadOverride();
    LogPads();

    if (!g_link.started) {
        // The link starts on the first tick the game is in a town or a dungeon (a jump, or a save
        // loaded), never at the title or its menus.
        if (Settings().netplay == 0) {
            g_link.started = true;
            return;
        }

        // One client: Join picked (with an address) makes this copy player 2; a town or a dungeon first, player 1.
        if (Settings().one_client && g_role == 0) {
            // Test hook (DC_NETPLAY_TEST_JOIN=address): join at once, as Join with that address typed would (a test
            // that must not drive pad 1 with an input script, whose override hides the live keyboard).
            static const char *test_join = TestEnv("DC_NETPLAY_TEST_JOIN");

            if (test_join != nullptr && !g_join_picked) {
                g_join_peer = test_join;
                g_join_picked = true;
                std::fprintf(stderr, "netplay: test: joining at %s without the menu\n", test_join);
            }

            if (g_join_picked) {
                g_role = 2;
                std::fprintf(stderr, "netplay: one client: joining player 1 at %s\n", g_join_peer.c_str());
            } else if (mode == GAME_MODE_DUNGEON || mode == GAME_MODE_EDIT) {
                g_role = 1;
                // Test hook DC_NETPLAY_TEST_NOTICE=text: that notice now (a frame of the notice's drawing).
                if (const char *notice = TestEnv("DC_NETPLAY_TEST_NOTICE")) {
                    SetNotice(notice);
                }
                std::fprintf(stderr, "netplay: one client: hosting as player 1; this PC's address %s\n",
                             NetSocketLocalAddress()[0] != '\0' ? NetSocketLocalAddress() : "unknown");
                // Over Steam too, when steam_api64.dll and Steam are there: a friends-only lobby a friend joins.
                // Over Steam only when Host > Steam picked it (g_host_via 2); otherwise Steam is never touched. Test hook
                // DC_NETPLAY_TEST_HOST=steam: as if Host > Steam was picked (a host that --jumps past the title).
                if (g_host_via == 0 && TestEnvIs("DC_NETPLAY_TEST_HOST", "steam")) {
                    g_host_via = SteamAsNat(3, "the test's Host > Steam") ? 5 : 2; // coop-natonly
                }
                if (!NetplayMutated("no_steam_lobby") && (g_host_via == 2 || g_host_via == 5 || NetplayMutated("steam_always"))) {
                    NetSteamHost(kSteamTag);
                } else {
                    std::fprintf(stderr, "netplay: one client: hosting by IP only (no Steam lobby)\n");
                }

                // The lobby's Multi screen seats: each one's window now, its controller handed over (Jep, 2026-11-01: no
                // second window until the lobby is settled and calls for it).
                if (!g_handoffs.empty()) {
                    for (const HandOff &handoff : g_handoffs) {
                        char          why[96];
                        unsigned long pid = NetplayMutated("local_no_spawn") ? 0
                                                                              : NetplayLocalSpawnHanded(handoff.host_pad, handoff.pad, handoff.identity, why, sizeof why);

                        if (pid != 0) {
                            std::fprintf(stderr, "netplay: local: seat %d's copy started on controller %d (pid %lu)\n", handoff.seat + 1, handoff.pad + 1, pid);
                        } else {
                            std::fprintf(stderr, "netplay: local: seat %d's copy did not start: %s\n", handoff.seat + 1, NetplayMutated("local_no_spawn") ? "(twin)" : why);
                        }
                    }
                    NetplayLobbyStartedWindow(g_handoffs.front().host_pad);
                    g_handoffs.clear();
                }
                // Local without a lobby (a host that --jumps past the title): player 2's copy, joining this one on
                // 127.0.0.1. Never after a lobby: only its start opens a window, for the seats that asked.
                if (g_host_via == 3 && !g_lobby_spawned && !g_lobby_hosts) {
                    char          why[96];
                    unsigned long pid = NetplayMutated("local_no_spawn") ? 0 : NetplayLocalSpawn(g_local_pad, why, sizeof why);

                    if (pid != 0) {
                        std::fprintf(stderr, "netplay: local: player 2's copy started (pid %lu)\n", pid);
                    } else {
                        std::fprintf(stderr, "netplay: local: player 2's copy did not start: %s\n", NetplayMutated("local_no_spawn") ? "(twin)" : why);
                    }
                }
            } else {
                return;
            }
        }

        // Player 2 with the main menu's Join (DC_NETPLAY_JOIN_MENU): the link starts when Join is
        // picked, and nowhere else.
        const bool join_menu = JoinMenu();

        if (join_menu ? !g_join_picked : mode != GAME_MODE_DUNGEON && mode != GAME_MODE_EDIT && !NetplayMutated("link_at_title")) {
            return;
        }

        g_link.started = true;

        if (!Open()) {
            g_link.dead = true;
            std::snprintf(g_link.status, sizeof g_link.status, "Link failed");
            return;
        }
        // coop-session: player 1 hosts, player 2 joins (from its title's Join, or told the host's address).
        NetSessionTo(g_link.player == 1 ? NetSession::Hosting : NetSession::Joining, join_menu ? "Join picked" : "the link opened");

        // Joined from the menu: it listens at once, the menu still up, until the host answers; the host's
        // state then brings this copy to the host's place. Two copies told each other's address wait a
        // minute for each other here, so copies launched together start on the same tick; a host told no
        // address plays on and listens at once.
        if (join_menu) {
            g_link.listening = true;
            g_link.hello_due = std::chrono::steady_clock::now();
            std::snprintf(g_link.status, sizeof g_link.status, "Joining player 1 at %s...", Settings().peer.c_str());
            std::fprintf(stderr, "netplay: joining player 1 from the main menu (mode %d)\n", (int) mode);
        } else if ((NetSocketLearnsPeer() || !Handshake()) && !g_link.dead) {
            g_link.listening = true;
            g_link.hello_due = std::chrono::steady_clock::now();

            if (std::strncmp(g_link.status, "Link refused", 12) != 0) {
                SetWaiting(g_link.status, sizeof g_link.status, g_link.player);
            }
        }
    }

    TickPart("the link's set-up");
    JoinCancelTick(); // coop-natsplit
    NatLostTick();    // coop-natonly
    TickPart("CoopWorldsTick");

    // coop-worlds (apart from a catch-up, as the host recorded it). coop-floorsnap: nor while this copy still awaits the host's
    // state: a joiner knows its join is a catch-up only from the first chunk, and took the ticks before it as the merge held,
    // then "did not hold" as the catch-up began and went apart again mid-catch-up (w-floor-pick, 2026-10-24).
    CoopWorldsTick(g_link.up && !g_catch.active && !g_link.awaiting_sync, g_link.tick);

    if (!g_link.up && g_link.listening) {
        TickPart("Listen");
        Listen();
    }

    TickPart("HoldBudgetForJoin");
    HoldBudgetForJoin();
    TickPart("apart");

    if (!g_link.up) {
        if (Catchup() && g_link.listening && !g_link.dead) {
            TestExit();
            SoloTick();
        }

        return;
    }

    // (coop-shotthread: the joiner's replay time outside this tick, from the end of the last one)
    if (g_catch.active && !SendsState() && g_solo.on && g_catch.replay_left != std::chrono::steady_clock::time_point{}) {
        g_catch.replay_rest_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - g_catch.replay_left).count();
    }
    TickPart("the catch-up");
    if (g_catch.active && !(SendsState() ? CatchHostTick() : CatchJoinTick())) {
        return;
    }
    TickPart("the lockstep tick");

    // coop-stable1: the resync's load (see Link::load_hold): no tick until this copy's new mode runs its first frame.
    if (g_link.load_hold) {
        Receive();
        return;
    }

    std::int64_t tick = g_link.tick;

    // The host holds a resync back while it is somewhere a resync cannot send both copies (the title,
    // the opening, a loading screen), deciding it early enough that the joiner hears before the tick.
    if (g_link.player == 1 && g_link.resync_tick >= 0 && tick >= g_link.resync_tick - g_link.delay - 10 && tick < g_link.resync_tick &&
        RestartTarget(mode, MapNo, selectMapNo) < 0 && !NetplayMutated("no_postpone")) {
        std::fprintf(stderr, "netplay: resync postponed at tick %lld: the host is in mode %d, map %d\n", (long long) tick, (int) mode,
                     (int) MapNo);
        g_link.resync_tick = tick + g_link.delay + 60;
        ResyncCause("postponed: the host had no town or dungeon", "NetplayTick");
    }

    if (tick == g_link.resync_tick && !Resync()) {
        return;
    }

    // The state as this tick starts, before either copy's input for it (not on a resync tick, which
    // is taken before both copies leave for the same map).
    // coop-p2absent: test hook (DC_NETPLAY_TICK_TRACE=N): the first N ticks after a resync.
    static const int tick_trace = TestEnvInt("DC_NETPLAY_TICK_TRACE", 0);
    // coop-p1follows: DC_NETPLAY_TICK_TRACE_LINK=N: the first N ticks of every link too (a catch-up merge is no resync).
    static const int link_trace = TestEnvInt("DC_NETPLAY_TICK_TRACE_LINK", 0);

    if ((tick_trace > 0 && g_link.resynced_at >= 0 && tick > g_link.resynced_at && tick <= g_link.resynced_at + tick_trace) ||
        (link_trace > 0 && tick > 0 && tick <= link_trace)) {
        TickTrace(tick, "");
    }

    ApplyRecordedCommands("as a lockstep tick starts"); // coop-stabfix1 (none left by now: a fallback)
    TestTickDump(tick, "lockstep"); // coop-houseexit (tests)

    if (tick > 0 && tick % kChecksumTicks == 0 && tick != g_link.resynced_at) {
        std::uint32_t sum = Checksum();
        int           slot = (int) ((tick / kChecksumTicks) % 8);

        g_link.sums_frame[slot] = tick;
        g_link.sums[slot] = sum;
        g_parts_at[slot] = g_parts;
        g_status_at[slot] = g_status;
        g_floor_at[slot] = g_floor_text;
        g_save_at[slot] = g_save_bytes;
        g_link.last_checksum_frame = tick;
        g_link.last_checksum = sum;
    }
    // coop-pilotfix test hook (DC_NETPLAY_TEST_CHECK_JOIN=1, both copies): a worlds merge's lockstep ticks J and J+1 checksummed
    // and sent whatever J is (slots 4 and 5 checks from J's neighbours), so CompareChecksum meets them every run, not only when
    // one falls on a checksum tick (1 in 5 in the town stayer merges): their own states, the catch-up's timing untouched.
    if (static const bool check_join = TestEnvOn("DC_NETPLAY_TEST_CHECK_JOIN"); check_join && g_link.joined_at >= 0 &&
        (tick == g_link.joined_at || tick == g_link.joined_at + 1) && tick % kChecksumTicks != 0) {
        const std::uint32_t sum = Checksum();
        const int           slot = (int) ((tick / kChecksumTicks + 4 + (tick - g_link.joined_at)) % 8);

        g_link.sums_frame[slot] = tick;
        g_link.sums[slot] = sum;
        g_parts_at[slot] = g_parts;
        g_status_at[slot] = g_status;
        g_floor_at[slot] = g_floor_text;
        g_save_at[slot] = g_save_bytes;
        g_link.last_checksum_frame = tick;
        g_link.last_checksum = sum;
        std::fprintf(stderr, "netplay: test: the lockstep's tick %lld checksummed (J+%lld, %08x)\n", (long long) tick, (long long) (tick - g_link.joined_at), sum);
    }

    NetplayTestTick(tick);
    SaveLoadHooks(tick);

    AdaptDelay();

    // The input read now is for frame tick + delay. Each frame gets one input, from the copy that owns
    // it, so the delay can change on one copy alone: when it grows, the frames it passes over keep the
    // last input; when it shrinks, nothing is read until the frames already given are passed.
    std::int64_t frame = tick + g_link.delay;

    if (frame > g_link.newest) {
        const bool    scripted = ScriptReads();
        InputPadState input = scripted ? ScriptInputFor(frame, tick) : OwnInput(g_link.player);

        input.connected = true;

        if (Latency() && (input.buttons & ~g_link.last_input.buttons) != 0) {
            g_link.presses[frame % 16] = Link::Press{tick, frame, std::chrono::steady_clock::now()};
        }

        for (std::int64_t at = g_link.newest + 1; at <= frame; at++) {
            Slot &slot = g_link.local[at % kRing];

            slot.frame = at;
            slot.pad = at == frame ? input : scripted ? ScriptInputFor(at, tick) : g_link.last_input;
            slot.command = TakeQueuedCommand(); // coop-localmenu: one a frame, in the order queued
            ScriptPressTrace(slot.pad, at, tick);
        }

        if (scripted) {
            InputScriptApply(g_script_frame); // (the pad as the pump left it, for the rest of the frame)
        }

        g_link.newest = frame;
        g_link.last_input = input;

        // Once a second while linked: how many ticks this copy's own pad held a button or a stick off centre,
        // and where the partner stands, so a log shows whether this PC's controller (or keyboard) plays.
        static int held = 0, ticks = 0;
        static std::uint16_t last_held = 0;
        bool       stick = std::abs((int) input.left_x - 128) > 40 || std::abs((int) input.left_y - 128) > 40;

        if (input.buttons != 0 || stick) {
            held++;
            last_held = input.buttons;
        }

        if (++ticks >= 60) {
            float town[4] = {};
            int   member = -1;
            bool  in_town = mode == GAME_MODE_EDIT && coop::TownPartnerShown(town, &member);

            std::fprintf(stderr, "netplay: input: player %d's own pad held %d/60 ticks (buttons %04x); partner %d at (%.1f, %.1f)\n",
                         g_link.player, held, last_held, in_town ? member : g_parts.partner_chara, in_town ? town[0] : g_parts.partner[0],
                         in_town ? town[2] : g_parts.partner[2]);
            // coop-localfix (DC_COOP_DEBUG): on a floor, whether the game draws its player (Toan on P2's screen after a join).
            if (mode == GAME_MODE_DUNGEON && CoopDebug()) {
                std::fprintf(stderr, "netplay: floor player draw: visible %d, event draw %d, chara %d\n", (int) BtActStatus.player_visible,
                             (int) EdEventInfo.player_draw, UserStatus != nullptr ? (int) UserStatus->cur_chara : -1);
            }
            held = ticks = 0;
        }
    }

    // coop-apartsync test hook DC_NETPLAY_TEST_BLACKOUT=T:MS: at link tick T the network cuts out for MS (both copies set it,
    // as a router dropping out does): longer than the wait's timeout, the link goes down with both copies playing on.
    static const char *const blackout = TestEnv("DC_NETPLAY_TEST_BLACKOUT");
    static bool              blackout_done = false;
    long long                blackout_at = -1;
    int                      blackout_ms = 0;

    // coop-partnerlog: DC_NETPLAY_TEST_BLACKOUT=file:<go file>:MS: the cut once the test writes the go file (it does when both
    // copies' logs show the lockstep), not at a link tick each copy reaches on its own clock.
    if (blackout != nullptr && !blackout_done && std::strncmp(blackout, "file:", 5) == 0) {
        static const std::string go_file = [] {
            const std::string spec(blackout + 5);
            return spec.substr(0, spec.rfind(':'));
        }();
        std::error_code missing;

        if (std::filesystem::exists(go_file, missing)) {
            blackout_done = true;
            std::fprintf(stderr, "netplay: test: the network cuts out at link tick %lld (go file)\n", (long long) tick);
            NetSocketBlackout(std::atoi(std::strrchr(blackout, ':') + 1));
        }
    } else if (blackout != nullptr && !blackout_done && std::strncmp(blackout, "synced:", 7) == 0) {
        // coop-fx51c (Jep, 2026-11-04: no fixed tick waits): DC_NETPLAY_TEST_BLACKOUT=synced:MS: the cut on the tick the link's
        // first checksum after a state file's load is taken (NetplayStateFileChecked: a row's fixture, alike on both copies).
        if (NetplayStateFileChecked()) {
            blackout_done = true;
            std::fprintf(stderr, "netplay: test: the network cuts out at link tick %lld (the fixture's first compare)\n", (long long) tick);
            NetSocketBlackout(std::atoi(blackout + 7));
        }
    } else if (blackout != nullptr && !blackout_done && std::sscanf(blackout, "%lld:%d", &blackout_at, &blackout_ms) == 2 && tick >= blackout_at) {
        blackout_done = true;
        std::fprintf(stderr, "netplay: test: the network cuts out at link tick %lld\n", (long long) tick);
        NetSocketBlackout(blackout_ms);
    }

    {
        CostScope sent{g_costs.send_ms}; // coop-ws5left
        CatchSend(); // the last blob, until the joiner holds it
        Send();
    }

    InputPadState theirs = Neutral();
    InputPadState ours = Neutral();
    NetCommand    their_command;
    NetCommand    our_command;

    if (tick >= g_link.first_delay) {
        auto start = std::chrono::steady_clock::now();
        auto resent = start;
        auto pumped = start;
        int  limit = tick == g_link.first_delay ? FirstWaitMs() : kWaitMs;

        for (;;) {
            Receive();

            if (g_link.remote[tick % kRing].frame == tick) {
                break;
            }

            // The joiner, waiting for a host that sends a catch-up instead of its ticks.
            if (g_catch.active) {
                return;
            }

            // The other copy quit and said so: no need to wait out the timeout (a split on a tick still ahead: its
            // last inputs may yet come, PlaysOnToSplit).
            if (g_link.peer_left && !PlaysOnToSplit()) {
                char why[48];

                // coop-giveupword (l-local-play-houseout-giveup-p2): the taker's catch-up starts on the sender's first piece
                // (CatchReceiveChunk), so a sender that gave up before any piece finds this copy here, not in CatchLost: its
                // word is still a give-up. DC_NETPLAY_MUTATE=gave_up_as_left (twin): read as a plain leave, as before.
                if (g_link.peer_gave_up && !NetplayMutated("gave_up_as_left")) {
                    Close("the other copy gave up the catch-up");
                    return;
                }
                std::snprintf(why, sizeof why, g_link.peer_left_done ? "player %d left at the end of its run" : "player %d left", 3 - g_link.player);
                Close(why);
                return;
            }

            auto now = std::chrono::steady_clock::now();
            auto ms = [&](auto since) { return std::chrono::duration_cast<std::chrono::milliseconds>(now - since).count(); };

            // Gone once nothing came for limit, a Busy (the other copy loading) counting as word from it, up
            // to kLoadWaitMs in all.
            if (ms(std::max(start, g_link.peer_busy_at)) > limit || ms(start) > std::max(limit, kLoadWaitMs)) {
                // coop-linkrace (natsplitflake's open bug 2): nothing of the other copy's came under its session all this
                // link: it never linked with this one (it was not ready, or it linked a moment after this copy gave up).
                // Its session is no last link's then, so its Hellos and its own link, which still carry it, start the next
                // one. Kept, the other copy's link a moment after this one gave up went unanswered, it gave up in turn, and
                // the pair linked out of phase for good. DC_NETPLAY_MUTATE=keep_lone_session (twin): kept, as before.
                const bool lone = !g_link.link_heard;
                const std::uint32_t was = g_link.peer_hello.session;

                // (Once per session of its: a second lone link under the same one keeps it, so a copy that hears that
                // session's Hellos but never links, whatever holds it, costs this one wait more at most.)
                static std::uint32_t forgot = 0;

                std::fprintf(stderr, "netplay: the wait for player %d's tick %lld gave up after %lld ms (its last Busy %lld ms ago, %lld datagrams heard)\n",
                             3 - g_link.player, (long long) tick, (long long) ms(start),
                             g_link.peer_busy_at == std::chrono::steady_clock::time_point{} ? -1LL : (long long) ms(g_link.peer_busy_at),
                             (long long) g_link.heard_datagrams);
                Close("the other copy stopped answering");
                if (lone && was != forgot && !NetplayMutated("keep_lone_session")) {
                    forgot = was;
                    g_link.last_peer_session = 0;
                    std::fprintf(stderr, "netplay: nothing of player %d's came under its session %08x in that link: a link of this copy's alone; "
                                         "its session may start the next\n", 3 - g_link.player, (unsigned) was);
                }
                return;
            }

            // A long wait keeps the window answering, and closing it ends the wait.
            if (ms(start) > 250 && ms(pumped) > 100) {
                SDL_PumpEvents();
                pumped = now;

                if (SDL_HasEvent(SDL_EVENT_QUIT)) {
                    SayLeaving(); // coop-fx51b: a quit while waiting for the other's pads tells it too (it waited out its 5 s)
                    Close("this copy is closing");
                    return;
                }
            }
            if (QaStopInWait()) {
                SayLeaving(); // coop-fx51b (p-p2-absent-town on a fixture: her stop came in this wait, unsaid)
                Close("this copy is stopping (the test's stop file)");
                return;
            }

            if (now - resent > std::chrono::milliseconds(20) && !(NetplayMutated("no_resend") && g_link.tick >= 600)) {
                CatchSend();
                Send();
                g_link.resends++;
                resent = now;
            }

            std::this_thread::sleep_for(std::chrono::microseconds(500));
        }

        double waited = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();

        g_costs.wait_ms += waited; // coop-ws5left
        if (g_close_watch.left > 0) { // coop-reds2
            g_close_watch.longest = std::max(g_close_watch.longest, waited);
            g_close_watch.total += waited;
            if (--g_close_watch.left == 0) {
                std::fprintf(stderr, "catchup: this copy's waits on the other's pads in the 120 ticks after its close (%s): longest %.0f ms, %.0f ms in all\n",
                             g_close_watch.fallback ? "the fallback's" : "the usual close", g_close_watch.longest, g_close_watch.total);
            }
        }
        if (waited > 2.0) {
            g_link.stalls++;
            g_link.stall_ms += waited;
            g_link.stall_max_ms = std::max(g_link.stall_max_ms, waited);
        }

        theirs = g_link.remote[tick % kRing].pad;
        ours = g_link.local[tick % kRing].pad;
        their_command = g_link.remote[tick % kRing].command;
        our_command = g_link.local[tick % kRing].command;

        if (Link::Press &press = g_link.presses[tick % 16]; Latency() && press.frame == tick) {
            double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - press.at).count();

            std::fprintf(stderr, "netplay-latency: press read at tick %lld for tick %lld, applied at tick %lld: %lld ticks, %.1f ms\n",
                         (long long) press.read_tick, (long long) press.frame, (long long) tick, (long long) (tick - press.read_tick), ms);
            press.frame = -1;
        }
    } else {
        Receive();
    }

    g_link.synced[g_link.player - 1] = ours;
    g_link.synced[2 - g_link.player] = theirs;
    Route();
    // coop-localmenu: the tick's commands, player 1's first, on every copy alike.
    ApplyCommand(1, g_link.player == 1 ? our_command : their_command, tick);
    ApplyCommand(2, g_link.player == 2 ? our_command : their_command, tick);
    Diagnose();
    Trace(g_link.synced[0], g_link.synced[1], tick % kChecksumTicks == 0 ? g_link.last_checksum : 0);
    g_link.tick++;
    g_last_tick_ms = NowMs();
}

// coop-worlds: a notice at the top left (the other player's news while apart, as a join's or leave's).
void NetplayShowNotice(const char *text) {
    SetNotice(text);
}

// coop-saveload: player 1's load from his menu (both copies step it; the host's own save is the one that counts): the
// host takes both copies to its state now, as DC_NETPLAY_LOAD_AT does, the resync marked as the load's.
// DC_COOP_MUTATE=saveload_noresync (twin): no resync, each copy keeps what its own load gave it.
void NetplayTiming(const char *what) {
    if (CoopDebug()) {
        std::fprintf(stderr, "netplay: timing: %s [t %lld]\n", what, LogMs());
    }
}

void NetplayMenuLoadResync() {
    if (!g_link.up || g_link.player != 1 || coop::Mutated("saveload_noresync")) {
        return;
    }

    g_link.menu_load = true;
    ResyncCause("player 1 loaded a save from his menu", "NetplayMenuLoadResync");
    if (g_link.resync_tick < 0 || g_link.resync_tick > g_link.tick + g_link.delay + 30) {
        g_link.resync_tick = g_link.tick + g_link.delay + 30;
    }
    std::fprintf(stderr, "netplay: player 1 loaded a save at tick %lld; both take his state at tick %lld\n", (long long) g_link.tick,
                 (long long) g_link.resync_tick);
}

// coop-loadpull: player 1 loaded a save from his menu while apart (CoopWorldsLoaded, as the load's data is in and before the
// town's reload): what this copy recorded before it is not a catch-up's start (her replay would load her own save 0); the
// reload's mode end takes the next checkpoint (NetplayModeEnded), else the merge's link takes a place checkpoint.
void NetplayLoadPull() {
    if (g_solo.on || !g_solo.checkpoint.empty()) {
        std::fprintf(stderr, "catchup: player 1 loaded a save apart: this copy drops its %scheckpoint from before the load\n", g_solo.place ? "place " : "");
    }
    g_solo = Solo{};
}

bool NetplayPad(int port, InputPadState &out) {
    if (port < 0 || port > 1) {
        return false;
    }

    // coop-pt1106-batch (Jep's 2026-11-06 playtest, item 6: "Multiscreen now starts at the main menu instead of in game, so P2
    // can press things she shouldn't"): the lobby's handed window boots to its title and joins from there (coop-p2toan: never a
    // blank town of its own), so until its first link its title and front end take no presses, its controller's or anyone's:
    // the host's state brings it into the game. A friend's own copy (no handed controller) is as before, and so is this
    // window once it has linked (apart later, it plays on alone). DC_NETPLAY_MUTATE=handed_title_presses (twin): its presses
    // reach its title, as before.
    static const bool handed = TestEnvInt("DC_NETPLAY_LOCAL_PAD", -1) >= 0;

    if (handed && !g_link.up && g_links_up == 0 && InputGetPad(port).buttons != 0) {
        g_handed_title_pressed = true; // (the test's link hook waits for it)
    }
    if (handed && !g_link.up && g_links_up == 0 && !NetplayMutated("handed_title_presses")) {
        const InputPadState held = InputGetPad(port);
        static std::uint16_t said = 0;

        if (held.buttons != 0 && (held.buttons & ~said) != 0) {
            said = (std::uint16_t) (said | held.buttons);
            std::fprintf(stderr, "netplay: local: the handed window's title takes no presses before its link (controller %d held %04x)\n", port + 1,
                         (unsigned) held.buttons);
        }
        out = Neutral();
        out.connected = port == 0;
        return true;
    }

    // Player 2 with the menu's Join plays alone, until it links, with its own controller and no second
    // pad: two copies on one PC would otherwise both read controller 1 at their menus.
    if (!g_link.up && JoinMenu() && !NetplayMutated("join_any_pad")) {
        out = port == 0 ? OwnInput(2) : Neutral();
        out.connected = port == 0;
        return true;
    }

    // One client hosting alone (not linked, no catch-up recording): its own controller, the one last pressed
    // (ActivePad), as at the title. Retail's read took the first pad, which on Jep's PC is not his (2026-10-21: the
    // controller worked on the menu and not in the game). DC_NETPLAY_MUTATE=host_first_pad (the test's twin): as before.
    if (!g_link.up && !g_solo.on && Settings().one_client && g_link.player == 1 && !NetplayMutated("host_first_pad")) {
        out = port == 0 ? OwnInput(1) : Neutral();
        out.connected = port == 0;
        return true;
    }

    if (!g_link.up && !g_solo.on) {
        return false;
    }

    // coop-pt1106-lockup: waiting to land where it stands, this copy still plays alone (CatchJoinTick).
    if (g_link.up && g_catch.active && g_catch.walk_wait) {
        out = port == 0 ? OwnInput(g_link.player) : Neutral();
        out.connected = port == 0;
        return true;
    }

    out = g_link.routed[port];
    return true;
}

// coop: cameras
// coop-party: which player holds the game's menus (1 or 2) while the link is up, else 1.
bool NetplayPlayerPressed(int player, std::uint16_t buttons) {
    return g_link.up && player >= 1 && player <= 2 && (g_player_pressed[player - 1] & buttons) != 0;
}

// coop-stage4 4c: a local town menu's pad: the player's agreed pad, and its own controller through a merge's own ticks (the menu it
// carries, while the lockstep's pads are not up yet): the menu moves on this copy alone, so nothing need agree.
// coop-scriptmenus: the player whose agreed pad is the game's (port 0) this tick, as the routing above set it (every copy alike): what an
// event's script reads, so whose its menus are.
int NetplayGamePadPlayer() {
    return g_link.up ? g_game_pad_player : NetplayHasPlayer() ? NetplayLocalPlayer() : 1;
}

bool NetplayMenuPad(int player, InputPadState &out) {
    if (!g_link.up || player < 1 || player > 2) {
        return false;
    }
    out = g_catch.active && player == g_link.player ? OwnInput(player) : g_link.synced[player - 1];
    return true;
}

bool NetplayPlayerPad(int player, InputPadState &out) {
    if (!g_link.up || player < 1 || player > 2) {
        return false;
    }
    out = g_link.synced[player - 1];
    return true;
}

std::int64_t NetplayLinkTick() {
    return g_link.tick;
}

std::int64_t NetplayTestPressClock() {
    return g_test_press_since;
}

std::int64_t NetplayTestLockstepFrom() {
    return g_test_lockstep_from;
}

bool NetplayTestShotPending() {
    return !g_test_shot.empty();
}

void NetplayTestShot(const char *path) {
    g_test_shot = path;
}

int NetplayMenuOwner() {
    return g_link.up ? g_link.menu_owner : 1;
}

int NetplayLocalPlayer() {
    // A host recording for a catch-up plays as linked player 1 does (independent cameras and the rest), so a joiner
    // replaying it linked, as player 2, steps alike.
    return g_link.up ? g_link.player : g_solo.on ? 1 : 0;
}

bool NetplayHasPlayer() {
    return NetplayLocalPlayer() != 0;
}

// The same on both copies: the host stops recording and the joiner stops replaying at the join tick (EnterLockstep).
bool NetplayPartnerLinked() {
    // coop-floorsnap: nor while a catch-up is under way on either copy: a joiner taking a floor-start checkpoint spends the
    // entrance and the floor's build with the link up but no tick yet, and loaded a partner of its own there (snapjoin run
    // 4: her presence latch then held into the host's state, and she loaded again at the apply while the host loaded her
    // at J). Both load her at J, the lockstep's start.
    return g_link.up && !g_solo.on && !g_catch.active;
}

int NetplayLinkPlayer() {
    return g_link.player;
}

bool NetplayLinkUp() {
    return g_link.up;
}

// coop-cavedoorfix: a state file (DC_STATE_LOAD_AT) still to be put back on this copy: the tests' link-tick hooks wait for it.
bool NetplayStateLoadPending() {
    return StateFileLoadPending();
}

void NetplayModeEnded() {
    ZoneModeEnded(); // coop-stable1: the zone line, before NextMapNo is followed
    g_floor_started = false; // coop-snapshot: whatever floor this mode had is left behind
    g_floor_build_key = -1; // coop-localfix: and its build (NetplayFloorBuilt)

    if (g_link.state_due) {
        g_link.state_due = false;
        TakeState(g_link.resync_state);
        g_test_epoch++;
        g_test_epoch_resyncs = g_link.resyncs;
        std::fprintf(stderr, "netplay: took the host's state as the mode ended\n");

        // coop-snapshot: a floor-start checkpoint: from the entrance this copy goes straight onto its floor
        // (GameTakeJumpFloor). DC_NETPLAY_MUTATE=no_restart_floor (twin): the entrance's floor menu, as before.
        const int floor = CheckpointFloor(g_link.resync_state);

        if (floor >= 0 && !NetplayMutated("no_restart_floor")) {
            g_link.restart_floor = floor;
            std::fprintf(stderr, "catchup: the checkpoint is floor %d's start: this copy goes onto it from the entrance\n", floor + 1);
        }
    } else if (CatchupArmed()) {
        Checkpoint();
    }
}

void NetplayFloorBuilding() {
    CoopWorldsFloorBuilding(); // coop-twinfix: player 2's copy apart hides the floor's Toan from its build

    // T24 desync A: the monsters' records start from zeros on each floor a linked or apart copy builds. The floor's set-up
    // (CleanViewMonstor, SetupViewMonstor) puts back only some of each record's fields, so a copy that had played a floor
    // kept the rest as that floor left them (the last hit, the ground under it, the lock-on point, the knockback, the hit
    // spheres) and a copy that had not held zeros: after a merge at the entrance the next floor's monsters started alike in
    // what the checksum sees and apart in the rest, and one of them walked another way now and then (w-world-split -Case
    // next-floor, about one run in twenty). Here, before the build, and never under a live floor (a resync's TakeState did
    // it first, which could leave a floor that went on a moment with its monsters' frames gone). DC_NETPLAY_MUTATE=
    // keep_monsters (twin): each copy keeps its own.
    if (NowMonstorUnit != nullptr && (g_link.up || CoopWorldsApart()) && !NetplayMutated("keep_monsters")) {
        std::memset((void *) NowMonstorUnit->monster, 0, sizeof NowMonstorUnit->monster);
        std::memset((void *) NowMonstorUnit->effect, 0, sizeof NowMonstorUnit->effect);
        std::memset((void *) NowMonstorUnit->effect2, 0, sizeof NowMonstorUnit->effect2);
        std::memset((void *) NowMonstorUnit->effect3, 0, sizeof NowMonstorUnit->effect3);
        std::memset((void *) NowMonstorUnit->event, 0, sizeof NowMonstorUnit->event);
        std::memset((void *) NowMonstorUnit->event2, 0, sizeof NowMonstorUnit->event2);
        std::memset((void *) NowMonstorUnit->event_flags, 0, sizeof NowMonstorUnit->event_flags);

        for (MONSTOR &monster : NowMonstorUnit->monster) {
            monster.state = -1; // none until the floor's set-up makes them (as CleanViewMonstor leaves them)
        }
    }
    // A joiner catching up to a floor-start checkpoint, building that floor: from the host's random state (see
    // ResyncHeader::floor_rand), so the floor comes out as the host's did. The host built it before its checkpoint, whose
    // own random state is the one after the build; a joiner from the title or a town rolled its own way here, and from the
    // checkpoint's state it built another floor (train 9, 2026-10-24: n-debug-start-floor and q-p2-quickslot-p1menu parted
    // on the first checksum after the lockstep, random, player and monsters alike). DC_NETPLAY_MUTATE=own_floor_rand
    // (twin): the joiner builds from the state it has.
    if (TakesState() && g_catch.active && !g_solo.on && UserStatus != nullptr && CheckpointFloor(g_solo.checkpoint) >= 0 &&
        CheckpointFloor(g_solo.checkpoint) == UserStatus->cur_floor && !NetplayMutated("own_floor_rand")) {
        const std::uint32_t rand = CheckpointFloorRand(g_solo.checkpoint);
        std::size_t         differed = 0;

        // The host's SaveData as well, before the build: a joiner from the title or a town came into the dungeon through the
        // game's own entry, which starts the dungeon's status afresh (UserStatus->Init: the floors' first-visit flags among
        // it) before the jump onto the host's floor, so the floor's opening scene ran on the joiner and not on the host, and
        // the two generators had parted before "go dungeon" (n-debug-start-floor: random alone at the first checksum).
        // The host's floor was built from its own save, which is this one but for what the intro changed.
        if (SaveData != nullptr && g_solo.checkpoint.size() >= sizeof(ResyncHeader) + sizeof(CSaveData) && !NetplayMutated("own_floor_save")) {
            const unsigned char *bytes = g_solo.checkpoint.data() + sizeof(ResyncHeader);

            for (std::size_t i = 0; i < sizeof(CSaveData); i++) {
                differed += ((const unsigned char *) SaveData)[i] != bytes[i];
            }

            std::memcpy((void *) SaveData, bytes, sizeof(CSaveData));
        }

        std::fprintf(stderr, "catchup: floor %d built from the host's random state %08x (this copy's was %08x), its save put back (%zu bytes differed)\n",
                     (int) UserStatus->cur_floor + 1, rand, PortRandState(), differed);
        PortRandSetState(rand);
    } else if (std::uint32_t peer_rand = 0; UserStatus != nullptr && CoopWorldsPeerFloorRand(UserStatus->cur_floor, &peer_rand)) {
        // coop-localfix (Jep's 2026-10-26 note H: "When P2 joins the dungeon after P1, it still causes P2 to join a different
        // version of P1"): apart, onto the floor the other copy plays: built from the state the other built it from, so the
        // floor comes out as the other's and the merge's state set that follows changes nothing that shows. Either player
        // may come second. DC_WORLDS_MUTATE=own_floor_apart (twin): its own roll, as before.
        std::fprintf(stderr, "worlds: floor %d built from the other copy's floor's random state %08x (this copy's was %08x)\n",
                     (int) UserStatus->cur_floor + 1, peer_rand, PortRandState());
        PortRandSetState(peer_rand);
    } else if (static const char *forced = TestEnv("DC_NETPLAY_TEST_FLOOR_RAND"); forced != nullptr) {
        // coop-keydoor (test hook): every floor built from this random state, on both copies alike, so a test plays a layout
        // from a run's log (x-key-door: the B1 of Jep's 2026-10-27 run, d6abbc68, with its Bone Key door).
        PortRandSetState((std::uint32_t) std::strtoul(forced, nullptr, 16));
        std::fprintf(stderr, "netplay: test: floor built from random state %s (DC_NETPLAY_TEST_FLOOR_RAND)\n", forced);
    }

    g_floor_build_rand = PortRandState();
    g_floor_build_key = UserStatus != nullptr ? selectMapNo * 256 + UserStatus->cur_floor : -1;
    g_floor_builds++; // coop-cutscenejoin (NetplayFloorBuilds)

    // coop-localfix: which floor each copy built (the layout follows from it), so two logs show whether they built one floor.
    if (UserStatus != nullptr && (g_link.up || CoopWorldsApart())) {
        std::fprintf(stderr, "netplay: floor %d of dungeon %d built from random state %08x\n", (int) UserStatus->cur_floor + 1, (int) selectMapNo,
                     g_floor_build_rand);
    }
}

std::uint32_t NetplayFloorBuildRand() {
    return g_floor_build_rand;
}

int NetplayFloorBuilds() {
    return g_floor_builds;
}

bool NetplayFloorBuilt(int *map, int *floor) {
    if (mode != GAME_MODE_DUNGEON || UserStatus == nullptr || g_floor_build_key < 0 || g_floor_build_key != selectMapNo * 256 + UserStatus->cur_floor) {
        return false;
    }

    *map = selectMapNo;
    *floor = UserStatus->cur_floor;
    return true;
}

// coop-floorjoin2: the story scenes the floors of dungeon `map` up to floor `up_to` (0 up) play on arrival, marked seen: the
// floors reached (UserStatus) and, in the save, the game flag each sets (GameTakeJumpFloor's list, recipe 0019: Divine Beast Cave's floors 1, 2, 3, 5, 6 and
// 7, Sun and Moon Temple's 4 and 7, Moon Sea's 8; coop-floorgate: and the inter floors 9 of Wise Owl Forest, Shipwreck, Sun and Moon Temple; a debug start skips the same ones). How many were unset. The floor's
// system script reads the flags as it starts, so this must come at the dungeon's entrance (NetplayTakeRestartFloor), not
// at "go dungeon" (NetplayFloorStarted): set there, the flags came too late and the scene ran (x-floor-join -Case late,
// 2026-10-25, set 2).
struct FloorScene {
    int map, floor, flag;
};
// coop-floorgate: and the "inter" (mid-dungeon) floors' scenes, Wise Owl Forest's, Shipwreck's and Sun and Moon Temple's floor 9
// (111, 121, 132).
constexpr FloorScene kFloorScenes[] = {{0, 0, 103}, {0, 1, 100}, {0, 2, 106}, {0, 4, 104}, {0, 5, 101}, {0, 6, 105}, {1, 8, 111},
                                       {2, 8, 121}, {3, 3, 130}, {3, 6, 131}, {3, 8, 132}, {4, 7, 140}};

int MarkFloorScenesSeen(int map, int up_to) {
    const auto &scenes = kFloorScenes;
    int         marked = 0;

    if (SaveData == nullptr) {
        return 0;
    }
    CoopWorldsLocalFlags(true); // coop-floorscene2: this copy's own marks, not progress for the other copy
    for (const FloorScene &scene : scenes) {
        if (scene.map == map && scene.floor <= up_to) {
            marked += SaveData->GetGameFlag(scene.flag) == 0 ? 1 : 0;
            SaveData->SetGameFlag(scene.flag, 1);
        }
    }
    CoopWorldsLocalFlags(false);
    // The floor's first visit is what plays the scene (UserStatus->floor_reached, as GameTakeJumpFloor marks it): with the
    // game flags alone set, the scene still ran (x-floor-join -Case late, 2026-10-25, set 4).
    if (UserStatus != nullptr && map >= 0 && map < 7 && UserStatus->floor_reached[map] < up_to) {
        UserStatus->floor_reached[map] = static_cast<s8>(up_to);
        marked++;
    }
    return marked;
}

int NetplayFloorSceneFlag(int map, int floor) {
    for (const FloorScene &scene : kFloorScenes) {
        if (scene.map == map && scene.floor == floor) {
            return scene.flag;
        }
    }
    return -1;
}

// coop-b4story (Jep's LAN pack 7 playtest, 2026-11-01: "P2 still ran down the stairs 2 times on B4 when joining late. I think B4
// was supposed to still be tagged a story floor since it had a cinematic and miniboss on it"): the floors whose story plays
// after the arrival, not at it, so neither retail's jump list nor kFloorScenes names them. Never marked seen by
// MarkFloorScenesSeen: a join or a debug start onto the floor would skip them.
// pt1111c (Jep's 11-11 playtest, item 18: "Dran's Cave B8: a cutscene plays as soon as you step onto the floor ... but the floor
// is not tagged"): the rule. A floor is a story floor while any game flag one of ITS OWN events sets is unset, whichever way
// the event starts: the floor start's own scene (program 100, kFloorScenes), an event marker of the floor's map that fires as
// the player walks into it (a PT_MARKER of dun\<map>.cfg whose program writes a story flag), or a monster's death
// (_SET_MONSTOR_ID's script). The audit (debug\notes\floor-story-audit: audit.py over data\dun and its README, 2026-11-11: every
// map cfg's markers, every program of the seven event.stb that writes a game flag, and program 100's _SET_DUNGEON_MAP per
// floor) found these besides kFloorScenes and the bosses (kGatherBoss):
// - Divine Beast Cave floor 4 (d01inter2): marker event02 (program 102, radius 40) starts the skeleton scene, its miniboss's
//   win (program 110) sets 102.
// - Divine Beast Cave floor 8 (d01inter): marker event00_2 (program 300, radius 50 around the entrance: it fires on the first
//   step after the entrance, Jep's case) plays Seda's scene and duel; it sets 37 as the scene is seen and 25 as the duel is
//   won, and the floor's start switches the marker off only once 25 is set (program 100: _SGET_GAME_FLAG 25 ->
//   _SET_EVENT_SW 300, 0). So 25, not 37, is when the story is over (a lost duel plays it again).
// - Shipwreck floor 9 (d03inter): after its arrival scene (121) its three monsters' deaths each play one (programs 197, 196,
//   195: 122, 123, 124).
// - Sun and Moon Temple floor 9 (d04inter): after its arrival scene (132) two monsters' deaths (programs 197, 196: 133, 134).
// Not tagged: Wise Owl Forest floor 9's event10_2 (program 300, flag 48) needs an item used on it, as a key does (no walk-in);
// Moon Sea floor 8 (d05inter), Gallery of Time and Demon Shaft have no story but their arrival or boss; Shipwreck floor 17's
// gatekey_3 (400, flag 28) is a key. DC_NETPLAY_MUTATE=midfloor_story_off (twin): none of these; walkin_story_off (twin): all
// but floor 8's walk-in.
struct FloorStory {
    int  map, floor, flag;
    bool walk_in; // started by walking into its marker (the pt1111c twin's)
};
constexpr FloorStory kFloorStories[] = {{0, 3, 102, false}, {0, 7, 25, true},   {2, 8, 122, false},
                                        {2, 8, 123, false}, {2, 8, 124, false}, {3, 8, 133, false}, {3, 8, 134, false}};

int NetplayFloorStoryFlag(int map, int floor) {
    const int scene = NetplayFloorSceneFlag(map, floor);
    int       first = scene;

    // The first of the floor's story flags still unset (its opening scene's first), else its first one.
    if (scene >= 0 && (SaveData == nullptr || SaveData->GetGameFlag(scene) == 0)) {
        return scene;
    }
    for (const FloorStory &story : kFloorStories) {
        if (story.map != map || story.floor != floor || NetplayMutated("midfloor_story_off") || (story.walk_in && NetplayMutated("walkin_story_off"))) {
            continue;
        }
        if (SaveData == nullptr || SaveData->GetGameFlag(story.flag) == 0) {
            return story.flag;
        }
        first = first >= 0 ? first : story.flag;
    }
    return first;
}

void NetplayFloorStarted() {
    g_floor_started = true;
    CoopWorldsFloorStarted(); // coop-worlds: the zone names the floor from here
    coop::LogFloorGateKey(); // coop-gatekeyno

    // coop-chestsync: the party test hooks before the checkpoint, so the host's state carries them (coop_party.cpp).
    if (UserStatus != nullptr) {
        coop::PartyTestHooks(!(TakesState() && g_catch.active));
    }

    // The host alone: the floor's start is the checkpoint (not while a joiner is catching up to the last one, nor under
    // DC_NETPLAY_MUTATE=no_floor_checkpoint, the twin that keeps the mode ends' checkpoints only).
    if (CatchupArmed() && !g_catch.active && UserStatus != nullptr && !NetplayMutated("no_floor_checkpoint")) {
        Checkpoint(UserStatus->cur_floor);
    } else if (TakesState() && g_catch.active && !g_solo.on && UserStatus != nullptr && CheckpointFloor(g_solo.checkpoint) == UserStatus->cur_floor &&
               SaveData != nullptr && g_solo.checkpoint.size() >= sizeof(ResyncHeader) + sizeof(CSaveData) && !NetplayMutated("own_floor_save")) {
        // A joiner catching up to this floor's start, at the very point the host took its checkpoint ("go dungeon", before
        // the floor's first play tick): the host's SaveData and random state again, as the checkpoint holds them. A joiner
        // from the title or a town came onto the floor through the game's own dungeon entry, which counts the entry in the
        // save (quest_dungeon, now_time) on top of the host's state, which already held the host's own entry; a joiner
        // from inside the dungeon did not (w-floor-pick). Not a frame later (CatchJoinTick): a play tick runs between, and
        // putting the state back after it rewound the random generator by that tick (n-debug-start-floor: random, status
        // and save parted at the first checksum). DC_NETPLAY_MUTATE=own_floor_save (twin): the save as the entry left it.
        ResyncHeader         header;
        const unsigned char *bytes = g_solo.checkpoint.data() + sizeof header;
        std::size_t          differed = 0;

        std::memcpy(&header, g_solo.checkpoint.data(), sizeof header);

        for (std::size_t i = 0; i < sizeof(CSaveData); i++) {
            differed += ((const unsigned char *) SaveData)[i] != bytes[i];
        }

        std::memcpy((void *) SaveData, bytes, sizeof(CSaveData));
        std::fprintf(stderr, "catchup: floor %d started: the host's save put back (%zu bytes differed), random state %08x (this copy's was %08x)\n",
                     (int) UserStatus->cur_floor + 1, differed, header.rand, PortRandState());
        PortRandSetState(header.rand);
    } else if (UserStatus != nullptr) {
        std::fprintf(stderr, "catchup: floor %d started, no checkpoint (armed %d: catchup %d up %d listening %d apart %d dead %d; catch active %d)\n",
                     (int) UserStatus->cur_floor + 1, (int) CatchupArmed(), (int) Catchup(), (int) g_link.up, (int) g_link.listening,
                     (int) CoopWorldsApart(), (int) g_link.dead, (int) g_catch.active);
    }
}

void NetplayInteriorStarted() {
    g_building_started = false;

    // The host alone inside a building it went into apart (player 2's copy stayed outside): the interior's start is the
    // checkpoint, so when she follows him in her copy catches up from here, not from the last mode end's checkpoint and
    // the pads recorded after it (the house merge, 2026-10-25: a catch-up from the opening's checkpoint replayed the
    // opening's pads, and the copies parted 50 ticks after the merge). DC_NETPLAY_MUTATE=no_building_checkpoint (twin):
    // no checkpoint, as before.
    if (g_link.player == 1 && mode == GAME_MODE_EDIT && CoopWorldsApart() && CatchupArmed() && !g_catch.active &&
        !NetplayMutated("no_building_checkpoint")) {
        Checkpoint(-1, true);
        return;
    }

    // A joiner catching up to that checkpoint, in by the host's door (NetplayCatchupBuildingDoor): the host's state again
    // here, at the point it took it (the town's load, its walk to the door and the door itself had run on this copy
    // alone), the mode's map numbers kept as the host kept its own. Its tick 0 is the next.
    const int building = CheckpointBuilding(g_solo.checkpoint);

    if (TakesState() && g_catch.active && g_solo.left && !g_solo.on && !g_link.state_due && building > 0 &&
        building == CoopWorldsInteriorId(EdInteriorName)) {
        const int    next_map_no = NextMapNo, start_event_no = StartEventNo, old_map_no = OldMapNo, at_mode = mode;
        const std::uint32_t rand_was = PortRandState();

        ResyncHeader header;

        std::memcpy(&header, g_solo.checkpoint.data(), sizeof header);
        TakeState(g_solo.checkpoint);
        NextMapNo = next_map_no;
        StartEventNo = start_event_no;
        OldMapNo = old_map_no;
        mode = at_mode;
        // The town's clock too: the save holds it, but the town's next frame writes NowTime over it, and this copy's had
        // run on through its walk to the door (OUT OF SYNC on the save's now_time as the pair walked out, 2026-10-25).
        // DC_NETPLAY_MUTATE=own_building_clock (twin): this copy's own.
        if (!NetplayMutated("own_building_clock")) {
            NowTime = header.building_time;
        }
        g_building_started = true;
        std::fprintf(stderr, "catchup: building %s started: the host's state taken again, random state %08x (this copy's was %08x)\n", EdInteriorName,
                     PortRandState(), rand_was);
    }
}

bool NetplayLinkedByCatchup() {
    return g_link.up && !g_catch.active && g_catch.join_tick >= 0;
}

bool NetplayCatchLastBlobGone() {
    return g_link.up && g_link.player == 1 && g_catch.join_tick >= 0;
}

bool NetplayCatchupSetting() {
    return dc::platform::ConfigGet().netplay_catchup || TestEnvOn("DC_NETPLAY_CATCHUP"); // (the session's, MenuOnline)
}

namespace {

bool ArrivalHoldNow() {
    if (NetplayMutated("arrival_shown")) {
        return false;
    }
    // Taking her state for her merge, until the lockstep (EnterLockstep gives the source back to the host).
    if (g_link.up && g_link.stayer_merge && g_link.source == 2 && TakesState()) { // (not after: the source is the host again, she would "take")
        return true;
    }
    // coop-arrivalonce (Jep, 2026-11-01, on "it played the cinematic dungeon floor entrance then it played it a second time ... xiao
    // just walked down the stairs": "only the cinematic"): the arriver's own screen has had its arrival as the game's floor
    // entrance (the stairs down); the arrival walk replayed after the catch-up (coop::ArrivalWalkStart, on both copies) is for the
    // other screen, so the arriver's stays black through it. (coop-joinlate: no walk again since, Jep "i wanted it not to happen";
    // this holds only under the twin arrival_walk_twice, so its walk is not shown either; arrival_walk_shown's twin row is gone.)
    if (g_link.up && coop::ArrivalWalkPlayer() != 0 && coop::ArrivalWalkPlayer() == NetplayLocalPlayer()) {
        return true;
    }
    // coop-dungeonjoin (Jep, 2026-10-27: a floor join's catch-up hidden inside the arriving copy's floor loading): the copy
    // that set out for a floor the other already plays shows its stairs down (the game's loading animation), then black
    // from the floor's first frame to its lockstep: not its own floor's first frame held through the transfer and the
    // replay (some 11 s, x-floor-join -Case late -Seamless). Before the link, at most 20 s (a merge that never came shows
    // the floor). DC_NETPLAY_MUTATE=floor_shown_early (twin): shown, as before.
    const bool floor_on = !NetplayMutated("floor_shown_early");
    const bool floor_arriving = CoopWorldsArrivingOnFloor(20000); // (asked each frame: it notes when this copy sets out)

    // coop-cutscenejoin: a join from her own arrival (CoopWorldsPickJoining): the arrival shows until his state lands over
    // it, black from there to the lockstep. coop-arrivalfull (Jep, 2026-10-27): no more: its arrival plays after the
    // catch-up, from its start, on both copies (coop::ArrivalWalkStart), so the stairs down and then black hide the whole
    // catch-up. DC_NETPLAY_MUTATE=arrival_skipped (twin): shown, as before.
    const bool own_arrival = CoopWorldsPickJoining() && !(g_link.up && g_snap.applied) && NetplayMutated("arrival_skipped");

    if (floor_on && g_link.up && g_link.floor_arrival && !own_arrival) {
        return true;
    }
    // coop-cutscenejoin: its floor's start held while the other plays that floor's opening scene: the black after its
    // arrival (the floor's entrance animation) goes on until the scene is over and it has joined. coop-cutsceneview: but for
    // the other's view of that scene, while it comes in (CoopWorldsSceneView): this copy shows the scene, from its camera.
    if (floor_on && CoopWorldsFloorStartHeld() && !CoopWorldsSceneView(nullptr, nullptr, nullptr)) {
        return true;
    }
    // coop-houseexit: and her copy taking his where he stayed (she out of the mayor's house to him outside), until the
    // lockstep: her house's last frame drawn again was shown through her catch-up (l-local-play -HerHouseOut).
    // (A floor arrival is held here too; floor_shown_early leaves it out as well, or its twin, x-floor-join -Seamless, passed.)
    if (g_link.up && g_link.arriving && TakesState() && (floor_on || !g_link.floor_arrival)) {
        return true;
    }
    // Arrived in her town, the link not up yet: until it comes (at most 8 s, a merge that never came shows the town).
    const bool arrived = CoopWorldsArrivedWhereStayer(8000); // (asked each frame: it notes where this copy is drawing)
    const bool hold = !g_link.up && (arrived || (floor_on && floor_arriving && !own_arrival)); // (her Hello may not have come yet: her presence says she stayed there)

    if (static bool said = false; CoopDebug() && hold != said) {
        said = hold;
        NetplayTiming(hold ? "arrival hold on" : "arrival hold off");
        std::fprintf(stderr, "netplay: arrival hold %s (link %d, hello %d, her stayer %d, arrived %d, on a floor %d)\n", hold ? "on" : "off", (int) g_link.up,
                     (int) g_link.peer_hello_got, (int) g_link.peer_hello.stayer, (int) arrived, (int) floor_arriving);
    }
    return hold;
}

} // namespace

// coop-replayspeed: how long each arrival's black lasted, and how much of it came after the link was up (her state set, its
// apply, the replay): the time the catch-up adds to the arriving copy's transition.
// coop-pt1111h: the arrival's black as the last presented frame had it (NetplayArrivalHold), read only.
static bool g_arrival_black = false;

bool NetplayArrivalHold() {
    static std::int64_t began = -1, linked = -1;
    static double       log_ms_from = 0.0; // coop-small1b: the game thread's log writes at the black's start (QaLogTotals)
    static long long    log_lines_from = 0;
    const bool          hold = ArrivalHoldNow();

    if (hold && began < 0) {
        began = NowMs();
        QaLogTotals(&log_ms_from, &log_lines_from);
    }
    // coop-pt1111l: DC_NETPLAY_MUTATE=black_polled_link (twin, x-town-stayermenu-herevent~polled) reads the link as these calls
    // see it, as before fcda4155; a test copy that draws nothing (DC_QA_NO_DRAW) made none of them in the black then, so the
    // link went unseen ("catching up" 0 ms, no phases line). efa20fb4 (coop-pilot) has its every tick ask the hold as a drawn
    // copy does, so the polled link now came within a tick of the stamped one and the twin passed. Under the twin a no-draw
    // copy's calls see no link, as before that: it fails on its own check again.
    if (hold && linked < 0 && g_link.up && !(NetplayMutated("black_polled_link") && NetplayTestNoDraw())) {
        linked = NowMs();
    }
    if (!hold && began >= 0) {
        const std::int64_t now = NowMs();
        // coop-offthread: the link as stamped where it came (a catch-up's frames call this no more); DC_NETPLAY_MUTATE=black_polled_link
        // (twin): as this call saw it alone, as before (the catching up read 0 ms).
        auto               in_black = [&](std::int64_t at) { return at >= began && at <= now ? at : (std::int64_t) -1; };
        const std::int64_t link = NetplayMutated("black_polled_link") ? linked : in_black(g_arrival.link);

        std::fprintf(stderr, "netplay: arrival black %lld ms (%lld ms before the link, %lld ms catching up) [t %lld]\n", (long long) (now - began),
                     (long long) ((link < 0 ? now : link) - began), (long long) (link < 0 ? 0 : now - link), LogMs());
        // coop-offthread: and its phases, each edge on this copy's clock (-1: none in this black): x-town-stayermenu -HerEvent and
        // w-town-cavereach -Case herevent hold each to its own limit.
        if (link >= 0) {
            const std::int64_t from = in_black(g_arrival.apply_from), to = in_black(g_arrival.apply_to), lock = in_black(g_arrival.lockstep);

            std::fprintf(stderr, "netplay: arrival phases: before the link %lld ms, the state set %lld ms, its apply %lld ms, the catch-up %lld ms, after the lockstep %lld ms\n",
                         (long long) (link - began), (long long) (from >= 0 ? from - link : -1), (long long) (from >= 0 && to >= 0 ? to - from : -1),
                         (long long) (to >= 0 && lock >= 0 ? lock - to : -1), (long long) (lock >= 0 ? now - lock : -1));
        }
        // coop-small1b: what this copy's game thread spent writing its log lines in the black (a test copy's: QaLogTotals)
        double    log_ms = 0.0;
        long long log_lines = 0;

        QaLogTotals(&log_ms, &log_lines);
        if (log_lines > log_lines_from) {
            std::fprintf(stderr, "netplay: arrival log writes: %lld lines, %.1f ms (%.2f ms a line) on the game thread\n", log_lines - log_lines_from,
                         log_ms - log_ms_from, (log_ms - log_ms_from) / (double) (log_lines - log_lines_from));
        }
        began = linked = -1;
    }
    g_arrival_black = hold; // coop-pt1111h
    return hold;
}

bool NetplayArrivalBlack() {
    return g_arrival_black;
}

NetplayCostTotals NetplayCosts() {
    return g_costs;
}

// coop-small1b: the netplay tick's parts since the last call, the dearest first (those of 5 ms or more), and the game
// thread's log writes (QaLogCost: the lines' own write calls, wherever they were made), for the long-frame line.
std::string NetplayTickPartsSay() {
    auto parts = g_tick_parts.ms;

    g_tick_parts.ms.clear();
    std::sort(parts.begin(), parts.end(), [](const auto &a, const auto &b) { return a.second > b.second; });

    std::string out;
    char        piece[128];

    for (const auto &part : parts) {
        if (part.second < 5.0) {
            break;
        }
        std::snprintf(piece, sizeof piece, "%s%s %.0f", out.empty() ? "" : ", ", part.first, part.second);
        out += piece;
    }
    long long    lines = 0;
    const double log_ms = QaLogCost(&lines);

    std::snprintf(piece, sizeof piece, "%slog writes %.0f ms in %lld lines", out.empty() ? "" : "; ", log_ms, lines);
    out += piece;
    return out;
}

bool NetplayStayerMerge() {
    return g_link.up && g_link.stayer_merge && (CoopWorldsApart() || g_catch.active); // (a join from her waiting walk: its catch-up)
}

bool NetplayLinkedBySnapshot() {
    return NetplayLinkedByCatchup() && (g_snap.taken || g_snap.applied);
}

bool NetplayCatchupBuildingDoor(char *name, int name_size, int *entrance, int *jump, int *start_event) {
    if (!TakesState() || !g_catch.active || !g_solo.left || g_solo.on || g_link.state_due || g_building_started || g_catch.building_door ||
        CheckpointBuilding(g_solo.checkpoint) <= 0) {
        return false;
    }

    ResyncHeader header;

    std::memcpy(&header, g_solo.checkpoint.data(), sizeof header);

    if (MapNo != header.map_no) {
        return false;
    }

    g_catch.building_door = true;
    std::snprintf(name, (std::size_t) name_size, "%s", header.building_name);
    *entrance = header.building_entrance;
    *jump = header.building_jump;
    *start_event = header.building_start_event;
    std::fprintf(stderr, "catchup: in by the host's door to building %s (entrance %d, jump %d, event %d)\n", name, *entrance, *jump, *start_event);
    return true;
}

void NetplayGoToFloor(int map, int floor) {
    // As a floor-start checkpoint's copy goes (NetplayModeEnded): out of this mode mid-frame, wherever it is, into the dungeon's
    // entrance and from there straight onto the floor (GameTakeJumpFloor), not its floor menu.
    selectMapNo = map;
    g_link.restart_map = 200 + map;
    g_link.restart_floor = floor;
    std::fprintf(stderr, "netplay: this copy goes onto dungeon %d floor %d (from mode %d map %d)\n", map, floor + 1, (int) mode, (int) MapNo);
    std::fflush(stderr);
}

// coop-gateflow: this copy back to a town from a dungeon's floor menu (its Circle there), out of the mode as a resync leaves.
void NetplayGoToTown(int map) {
    g_link.restart_map = map;
    g_link.restart_floor = -1;
    std::fprintf(stderr, "netplay: this copy goes back to town map %d (from mode %d dungeon %d)\n", map, (int) mode, (int) selectMapNo);
    std::fflush(stderr);
}

int NetplayTakeRestartFloor() {
    const int floor = g_link.restart_floor;

    g_link.restart_floor = -1;
    if (floor >= 0) {
        // coop-floorsnap: the host's SaveData again, here at the floor's entry, before any of the floor's scripts run. A
        // joiner from the title or a town came into the dungeon through the game's own entry, which starts the dungeon's
        // status afresh (UserStatus->Init: the floors' first-visit flags among it); the floor's system script reads those
        // flags as it starts, so with the host's save put back only at the build (_SET_RANDOM_MAP, an opcode of that
        // script) the floor's opening scene still ran on the joiner alone (n-debug-start-floor: at the replay's tick 0
        // the joiner had a script under way, CScript, the message and event managers live, and RandTbl parted).
        // coop-floorjoin2: when the host's state set follows (a snapshot join: everything this copy plays before tick S goes
        // at the apply), the floor's opening scene is marked seen here too, so the landing goes straight to play and the
        // apply finds no scene under way. (x-floor-join -Case late, 2026-10-25: the host's checkpoint already carried the
        // cave's flag, set as its floor's script began, before its "go dungeon"; this guards a scene whose flag comes
        // later.) DC_NETPLAY_MUTATE=replay_floor_scene (the twin) leaves it.
        if (g_snap.expected && selectMapNo >= 0 && !NetplayMutated("replay_floor_scene")) {
            std::fprintf(stderr, "catchup: entering floor %d for a snapshot join: its opening scene is marked seen (%d flags set; the host's state set follows)\n",
                         floor + 1, MarkFloorScenesSeen(selectMapNo, floor));
        }
        return floor;
    }

    // coop-floorjoin2 (Jep's 2026-10-25 playtest, note 36: "P2 joined B1 after Toan did and had to watch the cutscene again
    // even though P1 had finished it"): player 2's copy, apart, at the entrance of the dungeon the host is in. A pick of his
    // floor merges by his snapshot as soon as both copies see each other there, and throws her build of the floor away, yet
    // the floor's own opening scene (the cave's floor 1: game flag 103, set as script 100 begins; the flags are not shared
    // while apart) played first, from start to end, before the merge could form (x-floor-join -Case late's first runs: the
    // scene ran on her build, the merge came after it). The scenes of his floor and the floors before it (he is past them)
    // are marked seen here, before the floor's system script reads the flags: her build goes straight to play, and the
    // merge forms at once. DC_NETPLAY_MUTATE=replay_floor_scene (the twin): the scene, as before.
    // coop-cutscenejoin: or his floor whose opening scene he plays now (her arrival there waits for it: CoopWorldsHoldFloorStart).
    const int peer_on = selectMapNo >= 0 ? CoopWorldsPeerFloorIn(selectMapNo) : -1;
    const int peer_floor = peer_on >= 0 ? peer_on : CoopWorldsPeerOpeningFloor();

    if (peer_floor >= 0 && NetplayRole() == 2 && CoopWorldsApart() && NetplaySnapshotAvailable() && !NetplayMutated("replay_floor_scene")) {
        // coop-floorscene2 test hook (DC_NETPLAY_TEST_UNSEE=F, x-floor-join -Case late): her copy has not seen game flag F's
        // scene (cleared here, on her copy alone), so the marks below set it, as in Jep's run (his floor 3's scene she had not
        // seen): whether that mark reaches his copy is the test.
        if (static const int unsee = TestEnvInt("DC_NETPLAY_TEST_UNSEE", -1); unsee >= 0 && SaveData != nullptr) {
            CoopWorldsLocalFlags(true);
            SaveData->SetGameFlag(unsee, 0);
            CoopWorldsLocalFlags(false);
            std::fprintf(stderr, "catchup: test: game flag %d cleared on this copy alone\n", unsee);
        }
        std::fprintf(stderr, "catchup: at the entrance with the host on floor %d: the opening scenes up to it are marked seen (%d flags set; a pick of his floor merges by his snapshot)\n",
                     peer_floor + 1, MarkFloorScenesSeen(selectMapNo, peer_floor));
    }

    // Test hook (DC_NETPLAY_TEST_PICK_FLOOR=N, 1 up; w-floor-pick): player 2's copy, apart, picks floor N at the entrance
    // window once, as a player's choice would.
    static const int pick = TestEnvInt("DC_NETPLAY_TEST_PICK_FLOOR", 0);
    static bool      picked = false;
    COOP_TEST_JOB_RESET(picked, false, "job_hook_kept"); // (coop-warmreset, WS7: once a job on a warm pair)

    if (pick > 0 && !picked && NetplayRole() == 2 && CoopWorldsApart()) {
        picked = true;
        std::fprintf(stderr, "catchup: test: player 2 picks floor %d at the entrance\n", pick);
        // coop-startjoin (DC_NETPLAY_TEST_PICK_SEEN=1, x-floor-join -StartJoin): as a floor she has been on before, so her
        // floor starts at once with its arrival alone (a first visit's scene comes before the floor starts).
        if (TestEnvInt("DC_NETPLAY_TEST_PICK_SEEN", 0) != 0) {
            std::fprintf(stderr, "catchup: test: floor %d's first visit marked done (%d flags set)\n", pick, MarkFloorScenesSeen(selectMapNo, pick - 1));
        }
        return pick - 1;
    }
    return -1;
}

// coop-mergetime: milliseconds since this copy last held a resync's state, -1 before any (the area card's log line).
long long NetplayResyncAgeMs() {
    if (g_link.resynced_clock == std::chrono::steady_clock::time_point{}) {
        return -1;
    }

    return (long long) std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - g_link.resynced_clock).count();
}

bool NetplayTakeRestart(int &next_map) {
    if (g_link.restart_map < 0) {
        return false;
    }

    next_map = (int) g_link.restart_map;
    g_link.restart_map = -1;
    // coop-houseblack (Jep's 2026-11-04 pack 8 playtest): a restart from a dungeon to a town (coop-gateflow's Circle at the floor
    // menu, mid the gate's script) left gameTask at GAME_TASK_SCRIPT_RUN in the town for good, and every town check that asks
    // for play (the merge's quiet frame, its landing, the place checkpoint) refused: black for good at the next merge. The
    // dungeon's task is play again as the dungeon is left; its next floor sets its own. DC_NETPLAY_MUTATE=town_task_kept
    // (twin, x-gate-flow-back-p1~town_task_kept): it stays, as before.
    if (mode == GAME_MODE_DUNGEON && next_map >= 0 && next_map < 200 && gameTask != GAME_TASK_PLAY && !NetplayMutated("town_task_kept")) {
        std::fprintf(stderr, "netplay: back to town map %d from a dungeon: its task %#x is play again\n", next_map, (unsigned) gameTask);
        gameTask = GAME_TASK_PLAY;
    }
    return true;
}

// coop-cand58: netplay's own threads stopped and joined, in one order, before the game's teardown (main, as RunGame returns):
// Keepalive, the piece sender, a SnapJob still packing (waited for, up to 10 s), the catch-up receiver, the STUN asker and the
// exe hasher. Once; the atexit calls it again for an exit that skipped main's.
void NetplayStopThreads() {
    static bool done = false;

    if (done) {
        return;
    }
    done = true;
    g_exit_past = true;
    if (NetplayMutated("exit_threads_run")) {
        std::fprintf(stderr, "netplay: test: the exit leaves netplay's threads running (exit_threads_run)\n");
    } else {
        g_exit_stop = true;
        std::string stopped;
        const auto  say = [&stopped](const char *name) { stopped += (stopped.empty() ? "" : ", ") + std::string(name); };

        {
            std::lock_guard<std::mutex> hold(g_keepalive_lock); // (no wake lost between its check and its wait)
        }
        g_keepalive_wake.notify_all();
        if (g_keepalive_thread.joinable()) {
            g_keepalive_thread.join();
            say("keepalive");
        }
        if (PieceSends().Stop()) {
            say("piece sender");
        }
        if (g_snap_threads.load() > 0) {
            for (int i = 0; i < 2000 && g_snap_threads.load() > 0; i++) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            say(g_snap_threads.load() > 0 ? "state-set packer (STILL RUNNING after 10 s)" : "state-set packer");
        }
        if (g_rx.thread.joinable()) {
            CatchReceiverStopAtExit();
            say("catch-up receiver");
        }
        if (NetSocketStunStop()) {
            say("STUN asker");
        }
        if (NetplayHelloStop()) {
            say("exe hasher");
        }
        std::fprintf(stderr, "netplay: exit: netplay's threads stopped and joined before the teardown: %s\n", stopped.empty() ? "none ran" : stopped.c_str());
    }
    std::fflush(stderr);
    static const int hold_ms = TestEnvInt("DC_NETPLAY_TEST_EXIT_HOLD_MS", 0);

    if (hold_ms > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(hold_ms));
    }
}

// coop-cand58 (tests, DC_NETPLAY_TEST_EXIT_HOLD_MS): the sends netplay's threads made after NetplayStopThreads.
void NetplayExitSaySends() {
    static const int hold_ms = TestEnvInt("DC_NETPLAY_TEST_EXIT_HOLD_MS", 0);

    if (hold_ms > 0) {
        std::fprintf(stderr, "netplay: exit: sends from netplay's threads after the stop: %d\n", g_exit_sends.load());
        std::fflush(stderr);
    }
}

bool NetplayOwnsPads() {
    return Settings().netplay != 0 && (!g_link.started || g_link.up || g_solo.on);
}

void NetplayPadHints() {
    // Every copy, single player too (Jep, 2026-10-21): the one client may go online from its title.
    if (NetplayMutated("xinput_pads")) {
        return;
    }

    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_XBOX, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_XBOX_ONE, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_RAWINPUT, "1");
}

int NetplayRole() {
    return Settings().netplay;
}

bool NetplayConfigured() {
    return Settings().netplay != 0;
}

namespace {

// One client's address entry on the main menu (SELECT opens it): Up/Down change the last character
// (0-9 and the dot), Right adds one, Left (or Backspace) takes the last off, Cross or Start (Return)
// joins at it, Circle or Triangle (Tab) closes it; a keyboard's digit and dot keys type. It starts
// from the address joined last time (game.netplay_peer), else this PC's own up to its last dot.
// A NAT code: an IPv4 address and port in base 30: 0-9 and the letters but B, I, L, O, S and Z, which read as 8, 1, 1,
// 0, 5 and 2 (coop-natcode, Jep, 2026-10-28: a friend typed 1 for I and joined a wrong port; base 36 since 2026-10-25).
// 8 characters ("K3F9-Q2XY") when the router kept a port near the game's (kNatPortBase up to 151 above it: the address
// times 152 plus that, under 30^8), else 10 ("K3F9Q-2XY7M": the address's 32 bits and the port's 16, under 30^10), so
// every port has a code (STUN gave Jep 11169, 29380, 31392: all 10). A typed look-alike is refused, never read as its twin.
constexpr char     kNatDigits[] = "0123456789ACDEFGHJKMNPQRTUVWXY";
constexpr unsigned kNatBase = sizeof kNatDigits - 1;
constexpr unsigned kNatPortSpan = 152;
constexpr char     kNatLookAlikes[] = "BILOSZ";
static_assert(kNatBase == 30, "the code lengths are worked out for base 30");

// The lowest port an 8-character code holds (the game's 47201 and 47202 are in), or DC_NETPLAY_NAT_PORT_BASE (a test
// lane's ports are elsewhere).
unsigned NatPortBase() {
    static const int base = TestEnvInt("DC_NETPLAY_NAT_PORT_BASE", 47200);

    return (unsigned) base;
}

std::string NatCode(const char *address, int port) {
    std::uint32_t parts[4] = {};

    if (address == nullptr || std::sscanf(address, "%u.%u.%u.%u", &parts[0], &parts[1], &parts[2], &parts[3]) != 4 || port <= 0 || port > 0xFFFF) {
        return "";
    }
    const unsigned long long ip = (parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8) | parts[3];
    const bool               short_code = (unsigned) port >= NatPortBase() && (unsigned) port - NatPortBase() < kNatPortSpan;
    unsigned long long       value = short_code ? ip * kNatPortSpan + ((unsigned) port - NatPortBase()) : (ip << 16) | (unsigned) port;
    const int                length = short_code ? 8 : 10;
    std::string              text(length, '0');

    for (int i = length - 1; i >= 0; i--) {
        text[i] = kNatDigits[value % kNatBase];
        value /= kNatBase;
    }
    return text.substr(0, length / 2) + "-" + text.substr(length / 2);
}

// A code (letters either case; dashes, spaces and other marks ignored) back to "a.b.c.d:port"; false unless 8 or 10
// characters make a sane one. A letter or digit the codes do not use (a look-alike: B, I, L, O, S, Z) refuses the code.
bool NatDecode(const std::string &code, std::string &out) {
    unsigned long long value = 0;
    int                length = 0;

    for (char c : code) {
        char upper = c >= 'a' && c <= 'z' ? (char) (c - 'a' + 'A') : c;

        if (!(upper >= '0' && upper <= '9') && !(upper >= 'A' && upper <= 'Z')) {
            continue;
        }
        if (NetplayMutated("nat_lookalikes")) {
            // (the test's twin: a decoder that forgives look-alikes, the bandaid this code must not have)
            const char *alike = std::strchr(kNatLookAlikes, upper);

            upper = alike != nullptr ? "811052"[alike - kNatLookAlikes] : upper;
        }
        const char *at = std::strchr(kNatDigits, upper);

        if (at == nullptr) {
            return false;
        }
        if (length < 10) {
            value = value * kNatBase + (unsigned long long) (at - kNatDigits);
        }
        length++;
    }
    if (length != 8 && length != 10) {
        return false;
    }
    unsigned long long ip = length == 8 ? value / kNatPortSpan : value >> 16;
    unsigned           port = length == 8 ? NatPortBase() + (unsigned) (value % kNatPortSpan) : (unsigned) (value & 0xFFFF);
    char               text[32];

    if (NetplayMutated("nat_bad_code")) {
        port ^= 1; // (the test's twin: the port read wrong)
    }
    if (port == 0 || port > 0xFFFF || ip == 0 || ip > 0xFFFFFFFFull) {
        return false;
    }
    std::snprintf(text, sizeof text, "%llu.%llu.%llu.%llu:%u", (ip >> 24) & 255, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255, port);
    out = text;
    return true;
}

// coop-natcode (tests, DC_NETPLAY_NAT_CHECK=1 at the first frame): every code made for a spread of addresses and ports
// (Jep's STUN ports among them) is 8 or 10 characters, holds no look-alike and reads back, in either case; each code
// with one character typed as its look-alike (0 as O, 1 as I or L, 5 as S, 8 as B, 2 as Z) or one character short is
// refused (qa\tests\mp-natcode.ps1).
void NatCodecCheck() {
    static const char *const kAddresses[] = {"1.2.3.4", "100.64.0.1", "127.0.0.1", "203.0.113.7", "81.21.150.5", "255.255.255.255"};
    static const int         kPorts[] = {1, 1024, 11169, 29380, 31392, 47199, 47200, 47201, 47202, 47351, 47352, 50000, 65535};
    static const char        kTyped[][3] = {{'0', 'O'}, {'1', 'I'}, {'1', 'L'}, {'5', 'S'}, {'8', 'B'}, {'2', 'Z'}};
    int                      codes = 0, refused = 0, failures = 0;

    auto fail = [&](const std::string &code, const char *why) {
        failures++;
        std::fprintf(stderr, "netplay: nat: codec check FAIL %s: %s\n", code.c_str(), why);
    };
    for (const char *address : kAddresses) {
        for (int port : kPorts) {
            const std::string code = NatCode(address, port);
            const bool        near = (unsigned) port >= NatPortBase() && (unsigned) port - NatPortBase() < kNatPortSpan;
            char              want[32];
            std::string       back, lower = code;

            std::snprintf(want, sizeof want, "%s:%d", address, port);
            codes++;
            if (code.size() != (near ? 9u : 11u) || code[code.size() / 2] != '-') {
                fail(code + " for " + want, near ? "not 8 characters and a dash" : "not 10 characters and a dash");
            }
            if (code.find_first_of(kNatLookAlikes) != std::string::npos) {
                fail(code, "holds a look-alike");
            }
            for (char &c : lower) {
                c = c >= 'A' && c <= 'Z' ? (char) (c - 'A' + 'a') : c;
            }
            if (!NatDecode(code, back) || back != want) {
                fail(code + " for " + want, ("read back as " + back).c_str());
            }
            if (!NatDecode(lower, back) || back != want) {
                fail(lower, "lower case not read back");
            }
            for (std::size_t i = 0; i < code.size(); i++) {
                for (const auto &pair : kTyped) {
                    if (code[i] == pair[0]) {
                        std::string typo = code;

                        typo[i] = pair[1];
                        if (NatDecode(typo, back)) {
                            fail(typo, ("a look-alike typed, read as " + back).c_str());
                        } else {
                            refused++;
                        }
                    }
                }
            }
            if (NatDecode(code.substr(1), back)) {
                fail(code.substr(1), "one character short, read as a code");
            }
        }
    }
    std::fprintf(stderr, "netplay: nat: codec check %s: %d codes (base %u, port base %u), %d look-alike typings refused, %d failures\n",
                 failures == 0 ? "PASS" : "FAIL", codes, kNatBase, NatPortBase(), refused, failures);
}

// This copy's own code, once STUN has answered ("" until then). coop-upnp: a host whose router forwarded its port by
// UPnP gives that outside address and port instead (a forward, where STUN's is only the last mapping it saw).
std::string NatOwnCode() {
    int         port = 0;
    const char *wan = NetSocketWan(port);
    int         upnp_port = 0;
    const char *upnp = NetUpnpOutside(upnp_port);
    const bool  by_upnp = upnp[0] != '\0' && NetUpnpPublic();
    std::string code = by_upnp ? NatCode(upnp, upnp_port) : wan[0] != '\0' ? NatCode(wan, port) : "";
    static std::string said;

    if (code != said && !code.empty()) {
        said = code;
        std::fprintf(stderr, "netplay: nat: this PC's code %s (%s:%d%s)\n", code.c_str(), by_upnp ? upnp : wan, by_upnp ? upnp_port : port,
                     by_upnp ? ", UPnP" : "");
    }
    return code;
}

struct AddressEntry {
    bool          open = false;
    bool          code = false; // a NAT code (kNatDigits), not an address
    bool          swallow = false; // the buttons that closed it, held: the menu must not see them
    std::string   text;
    std::string   note;
    std::uint16_t last = 0;
    bool          keys[37] = {}; // 0-9 and the dot, then A-Z
    bool          steam = false; // looking for a Steam friend hosting (Square)
    bool          port = false;  // coop-upnp: an address may end in ":port" (a host whose router forwarded another port)
    bool          colon_key = false;
};

AddressEntry g_entry;

constexpr char kEntryChars[] = "0123456789.";
constexpr char kEntryPortChars[] = "0123456789.:"; // coop-upnp: with a port

void EntryOpen() {
    g_entry.open = true;
    g_entry.note.clear();
    g_entry.text = dc::platform::ConfigGet().netplay_peer;

    if (!NetSocketValidAddress(g_entry.text.c_str()) || g_entry.text == "127.0.0.1") { // the setting's default
        std::string here = NetSocketLocalAddress();
        std::size_t dot = here.rfind('.');

        g_entry.text = dot == std::string::npos ? std::string() : here.substr(0, dot + 1);
    }

    g_entry.port = true; // coop-upnp
    std::fprintf(stderr, "netplay: address entry opened at \"%s\"\n", g_entry.text.c_str());
}

void EntryStep(std::uint16_t buttons) {
    // coop-natsplit test hook (DC_NETPLAY_TEST_ENTRY_KEY=K, a letter or digit): in a code's entry, K's key held on its
    // frames 30-32 and Select, the button the keyboard's C also plays, held a frame later (31-34), as a hand typing C does;
    // the code's text logged at frame 40.
    static const char *const fake = TestEnv("DC_NETPLAY_TEST_ENTRY_KEY");
    static int               fake_at = 0;
    const bool               fake_held = fake != nullptr && g_entry.code && fake_at >= 30 && fake_at < 33;

    if (fake != nullptr && g_entry.code) {
        if (fake_at >= 31 && fake_at < 35) {
            buttons |= kInputSelect;
        }
        if (fake_at == 40) {
            std::fprintf(stderr, "netplay: test: entry key %c typed: the code's text \"%s\"\n", fake[0], g_entry.text.c_str());
        }
        fake_at++;
    }

    std::uint16_t down = buttons & ~g_entry.last;
    g_entry.last = buttons;

    const bool *state = SDL_GetKeyboardState(nullptr);
    static constexpr SDL_Scancode kDigits[11] = {SDL_SCANCODE_0, SDL_SCANCODE_1, SDL_SCANCODE_2, SDL_SCANCODE_3, SDL_SCANCODE_4, SDL_SCANCODE_5,
                                                 SDL_SCANCODE_6, SDL_SCANCODE_7, SDL_SCANCODE_8, SDL_SCANCODE_9, SDL_SCANCODE_PERIOD};
    static constexpr SDL_Scancode kPad[11] = {SDL_SCANCODE_KP_0, SDL_SCANCODE_KP_1, SDL_SCANCODE_KP_2, SDL_SCANCODE_KP_3, SDL_SCANCODE_KP_4, SDL_SCANCODE_KP_5,
                                              SDL_SCANCODE_KP_6, SDL_SCANCODE_KP_7, SDL_SCANCODE_KP_8, SDL_SCANCODE_KP_9, SDL_SCANCODE_KP_PERIOD};

    // An address takes digits and dots (15 at most; with a port, ":" and 5 more), a code digits and letters (10 at most,
    // the dash shown, not typed); a look-alike letter is not typed at all, the note says what the code's character is.
    const std::size_t most = g_entry.code ? 10 : g_entry.port ? 21 : 15;
    const std::string before = g_entry.text;
    bool              key_typed = false;

    for (int i = 0; i < 37; i++) {
        const bool letter = i >= 11;
        bool       held = state != nullptr && (letter ? (bool) state[SDL_SCANCODE_A + (i - 11)] : (state[kDigits[i]] || state[kPad[i]]));
        const char typed = letter ? (char) ('A' + (i - 11)) : kEntryChars[i];

        held = held || (fake_held && std::toupper((unsigned char) fake[0]) == typed);
        key_typed = key_typed || (held && !g_entry.keys[i] && (letter ? g_entry.code : true));
        if (held && !g_entry.keys[i] && letter && g_entry.code && std::strchr(kNatDigits, typed) == nullptr) {
            g_entry.note = "Codes have no B, I, L, O, S or Z: those are 8, 1, 1, 0, 5 and 2";
        } else if (held && !g_entry.keys[i] && g_entry.text.size() < most && (letter ? g_entry.code : !g_entry.code || i < 10)) {
            g_entry.text += typed;
        }
        g_entry.keys[i] = held;
    }
    if (g_entry.port) { // coop-upnp: the ":" before a port (the key that types it, with or without Shift)
        const bool held = state != nullptr && state[SDL_SCANCODE_SEMICOLON];

        if (held && !g_entry.colon_key && g_entry.text.size() < most && g_entry.text.find(':') == std::string::npos) {
            g_entry.text += ':';
        }
        g_entry.colon_key = held;
    }
    // coop-natsplit (Jep, 2026-10-28: "I couldnt type C during the code entry for NAT Code"): a letter or digit key that also
    // plays a pad button (C is Select, which deletes) types its character alone; the pad's presses that frame are not read.
    // (That frame and the next: the pad may take the key a frame later than the keyboard's state shows it.)
    // DC_NETPLAY_MUTATE=entry_key_buttons (twin): read as before.
    static int key_frames = 0;

    key_frames = key_typed ? 2 : std::max(0, key_frames - 1);
    if (key_frames > 0 && !NetplayMutated("entry_key_buttons")) {
        down = 0;
    }

    if ((down & kInputRight) && g_entry.text.size() < most) {
        g_entry.text += '0';
    }

    if ((down & (kInputLeft | kInputSelect)) && !g_entry.text.empty()) {
        g_entry.text.pop_back();
    }

    if ((down & (kInputUp | kInputDown)) && !g_entry.text.empty()) {
        // Up counts on (a code 0-9 then its letters A-Y, an address 0-9, the dot, then ":" with a port), Down back, each round
        // to the start.
        const char *chars = g_entry.code ? kNatDigits : g_entry.port ? kEntryPortChars : kEntryChars;
        const int   count = (int) std::strlen(chars);
        const char *at = std::strchr(chars, g_entry.text.back());
        int         i = at != nullptr ? (int) (at - chars) : 0;

        i = (i + ((down & kInputUp) ? 1 : count - 1)) % count;
        g_entry.text.back() = chars[i];
    }
    if (g_entry.code && g_entry.text != before) {
        g_entry.note.clear(); // (a look-alike's or a bad code's note goes once the code is typed on: the friend's code shows again)
    }

    if (g_entry.steam) {
        std::uint64_t host = 0;
        char          name[64] = "";
        int           found = NetSteamFindHost(kSteamTag, host, name, sizeof name);
        // coop-lobbysteam-deadpick (2026-11-01): the friend picked from JOIN > STEAM's list would not take this copy (their game
        // closed after the list showed it, or another build): back to the list, which looks again, with the reason as the
        // menu's note. Before, the NAT search below retried the dead lobby every 2 s for 20 s and then opened NAT > Code
        // saying no friend hosts. DC_NETPLAY_MUTATE=steam_dead_pick_retry (the twin): as before.
        if (found < 0 && !NetplayMutated("steam_dead_pick_retry")) {
            const std::string refused = NetSteamPickRefused();

            if (!refused.empty()) {
                g_entry.open = false;
                g_entry.swallow = true;
                g_entry.steam = false;
                g_nat = 0;
                g_nat_steam_host = 0;
                g_nat_times = NatSteamTimes{};
                g_punch = PunchLog{};
                g_title_note = refused + ": pick again";
                g_title_note_until = std::chrono::steady_clock::now() + std::chrono::seconds(30);
                NetSteamJoinPick(0);
                if (g_host_via == 0) {
                    MenuOnline(false);
                }
                std::fprintf(stderr, "netplay: steam: the picked friend's lobby refused this copy (%s): back to the friend list\n", refused.c_str());
                return;
            }
        }

        if (found > 0 && !NetplayMutated("no_join")) {
            char peer[32];

            std::snprintf(peer, sizeof peer, "steam:%llu", (unsigned long long) host);
            // NAT > Steam: straight to the host's code from its lobby; the Steam relay only if it has none (or later, if the
            // punch does not get through: NatSteamTick).
            if (g_nat == 4) {
                std::string direct;

                g_nat_steam_host = host;
                g_nat_times.lobby = NatSince();
                if (NatDecode(NetSteamLobbyGet("nat"), direct)) {
                    std::snprintf(peer, sizeof peer, "%s", direct.c_str());
                    std::fprintf(stderr, "netplay: nat: host's code from the lobby: %s\n", peer);
                } else if (!NetplayMutated("nat_steam_fallback")) {
                    // coop-natsplit (Jep, 2026-10-29: the whole NAT connection settled at the menu, no Steam play by
                    // itself): no code in the lobby (yet: the host puts it there once its STUN answers): look again every
                    // 2 s within the search's 20 s, then NAT > Code's entry with a note. Twin nat_steam_fallback: the relay.
                    static auto next = std::chrono::steady_clock::time_point{};
                    const auto  now = std::chrono::steady_clock::now();

                    if (now < next) {
                        return;
                    }
                    next = now + std::chrono::seconds(2);
                    std::fprintf(stderr, "netplay: nat: no code in the host's lobby yet (%lld ms): looking again\n", NatSince());
                    NetSteamFindReset();
                    if (NatSince() >= kNatLobbySearchMs) {
                        NetSteamStop("NAT > Steam: no code in the host's lobby");
                        g_nat = 2;
                        g_nat_steam_host = 0;
                        g_punch = PunchLog{};
                        g_entry.steam = false;
                        g_entry.code = true;
                        g_entry.port = false;
                        g_entry.text.clear();
                        g_entry.note = "The host's game gave no NAT code: type the code shown on their screen";
                        std::fprintf(stderr, "netplay: nat: no code in the host's lobby in %d s: Join > NAT > Code instead\n", kNatLobbySearchMs / 1000);
                    }
                    return;
                } else {
                    std::fprintf(stderr, "netplay: nat: no code in the host's lobby: the Steam relay\n");
                }
            }
            g_entry.open = false;
            g_entry.swallow = true;
            g_entry.steam = false;
            g_join_peer = peer;
            g_join_picked = true;
            std::fprintf(stderr, "netplay: Steam friend chosen: %s (%s)\n", name, peer);
            std::fprintf(stderr, "netplay: Join picked on the main menu (mode %d)\n", (int) mode);
            return;
        }

        // coop-natsplit (Jep's 2026-10-28 play: his friend's NAT > Steam found no lobby, as it looked once before his game
        // was up, and fell to the address box, whose join by address his router never lets in): NAT > Steam looks again
        // every 2 s for 20 s, then turns to NAT > Code, the way that gets through without Steam.
        // DC_NETPLAY_MUTATE=nat_no_lobby_ip (twin): the address box at once, as before.
        static auto again = std::chrono::steady_clock::now();

        if (found < 0 && g_nat == 4 && !NetplayMutated("nat_no_lobby_ip")) {
            const auto now = std::chrono::steady_clock::now();

            if (NatSince() < kNatLobbySearchMs) {
                if (now >= again) {
                    again = now + std::chrono::seconds(2);
                    NetSteamFindReset();
                }
                found = 0;
            } else {
                NetSteamStop("NAT > Steam found no lobby");
                g_nat = 2;
                g_nat_friend.clear();
                g_punch = PunchLog{};
                g_entry.steam = false;
                g_entry.code = true;
                g_entry.port = false;
                g_entry.text.clear();
                g_entry.note = "No friend hosting on Steam: type the host's NAT code (shown on their screen)";
                std::fprintf(stderr, "netplay: nat: no friend's lobby in %d s: Join > NAT > Code instead\n", kNatLobbySearchMs / 1000);
                return;
            }
        }
        if (found < 0) {
            g_entry.steam = false;
            g_entry.note = NetSteamStatus();
        }

        if ((down & (kInputCircle | kInputTriangle)) == 0) {
            return;
        }
        g_entry.steam = false;
    }

    // coop-lobby: the entry's Square opens JOIN > STEAM's list of friends hosting (coop-lobbysteam: a friend is picked there,
    // never the first found joined); the entry closes for it.
    if ((down & kInputSquare) && !g_entry.code) {
        g_entry.open = false;
        g_entry.swallow = true;
        g_entry_steam_list = true;
        std::fprintf(stderr, "netplay: address entry: Square opens Join > Steam's list\n");
        return;
    }

    if (down & (kInputCircle | kInputTriangle)) {
        g_entry.open = false;
        g_entry.swallow = true;
        g_entry.port = false;
        std::fprintf(stderr, "netplay: address entry closed\n");
        // A Steam search closed without a join: Steam goes (unless Host > Steam still wants it).
        if (g_host_via != 2 && !NetplayMutated("steam_kept")) {
            NetSteamStop("the join closed");
        }
        if (g_entry.code) {
            g_entry.code = false;
            g_nat = 0;
            g_nat_friend.clear();
            if (g_host_via == 4) {
                g_host_via = 0;
            }
        }
        if (g_host_via == 0) {
            MenuOnline(false);
        }
        return;
    }

    if ((down & (kInputCross | kInputStart)) && g_entry.code) {
        std::string friend_address;

        if (!NatDecode(g_entry.text, friend_address)) {
            g_entry.note = "A code is 8 or 10 characters, 0-9 and letters but B, I, L, O, S, Z (the dash optional)";
            return;
        }
        g_entry.open = false;
        g_entry.swallow = true;
        g_entry.code = false;
        if (g_nat == 1) {
            g_nat_friend = friend_address;
            std::fprintf(stderr, "netplay: nat: friend's code taken: %s\n", friend_address.c_str());
        } else {
            g_join_peer = friend_address;
            std::fprintf(stderr, "netplay: nat: host's code taken: %s\n", friend_address.c_str());
            // coop-lobby (Jep, 2026-10-30: no Rejoin button; the entries remember their last value through a close).
            dc::platform::Config config = dc::platform::ConfigGet();

            config.netplay_last_code = g_entry.text;
            ConfigChange(config);
            if (!NetplayMutated("no_join")) {
                g_join_picked = true;
                std::fprintf(stderr, "netplay: Join picked on the main menu (mode %d)\n", (int) mode);
            }
        }
        return;
    }

    if (down & (kInputCross | kInputStart)) {
        // coop-upnp: "address:port" too (the port a host's router forwarded by UPnP, when not the usual one).
        const std::size_t colon = g_entry.port ? g_entry.text.find(':') : std::string::npos;
        const std::string address = g_entry.text.substr(0, colon);
        const int         port = colon == std::string::npos ? 0 : std::atoi(g_entry.text.c_str() + colon + 1);

        if (!NetSocketValidAddress(address.c_str())) {
            g_entry.note = "Not an address: four numbers with dots, like 192.168.1.23";
            return;
        }
        if (colon != std::string::npos && (port <= 0 || port > 65535)) {
            g_entry.note = "The port after the colon is a number from 1 to 65535, like :47201";
            return;
        }

        g_entry.open = false;
        g_entry.swallow = true;
        g_entry.port = false;
        g_join_peer = colon != std::string::npos ? address + ":" + std::to_string(port) : address;

        // Remembered for next time (the address alone: Join > IP starts from it too).
        dc::platform::Config config = dc::platform::ConfigGet();

        config.netplay_peer = address;
        ConfigChange(config);
        std::fprintf(stderr, "netplay: address typed: %s\n", g_join_peer.c_str());

        if (!NetplayMutated("no_join")) {
            g_join_picked = true;
            std::fprintf(stderr, "netplay: Join picked on the main menu (mode %d)\n", (int) mode);
        }
    }
}

} // namespace

bool NetplayTitleEntryOpen() {
    if (!g_entry.open && g_entry.swallow) {
        if ((OwnInput(2).buttons & (kInputCross | kInputStart | kInputCircle | kInputTriangle)) == 0) {
            g_entry.swallow = false;
        }
        return true;
    }

    return g_entry.open;
}

bool NetplayTitleJoin(bool pressed) {
    if (g_link.started || !JoinMenu()) {
        return g_link.started && JoinMenu();
    }

    if (Settings().one_client) {
        // Steam only while it is in use (Jep, 2026-10-21): not from the title on any more; Host > Steam, Join > Steam or
        // the entry's Square start it, and an invite accepted in Steam is taken once Steam is up (Join > Steam).
        if (!g_entry.open && !g_join_picked && NetSteamInvited() && !NetplayMutated("no_invite")) {
            g_entry.open = true;
            g_entry.steam = true;
            g_entry.note.clear();
            g_entry.last = OwnInput(2).buttons;
            std::fprintf(stderr, "netplay: Steam invite: joining from the main menu\n");
            SteamAsNat(4, "a Steam invite"); // coop-natonly
        }

        if (g_entry.open) {
            EntryStep(OwnInput(2).buttons);
        } else if (pressed && !g_join_picked) {
            g_entry.last = OwnInput(2).buttons;
            EntryOpen();
        }

        return g_join_picked || g_entry.open;
    }

    if (pressed && !g_join_picked && !NetplayMutated("no_join")) {
        g_join_picked = true;
        std::fprintf(stderr, "netplay: Join picked on the main menu (mode %d)\n", (int) mode);
    }

    return g_join_picked;
}

const char *NetplayTitleNote() {
    return !g_title_note.empty() && std::chrono::steady_clock::now() < g_title_note_until ? g_title_note.c_str() : "";
}

void NetplayDrawTitleJoin() {
    if (!JoinMenu()) {
        return;
    }

    char line[160];

    char under[160] = "";
    const char *here = NetSocketLocalAddress();

    if (g_link.started) {
        std::snprintf(line, sizeof line, "%s", g_link.status);
        if (g_nat == 2) {
            const std::string own = NatOwnCode();

            std::snprintf(under, sizeof under, "%s", !own.empty() ? ("Your code for the host: " + own).c_str() : "Finding your internet address...");
        }
    } else if (Settings().one_client && g_entry.open && g_entry.code) {
        const std::string own = NatOwnCode();

        if (g_nat == 1) {
            std::snprintf(line, sizeof line, "%s", !own.empty() ? ("Your code: " + own + "  (send it to your friend)").c_str() : "Finding your internet address...");
            std::snprintf(under, sizeof under, "%s", !g_entry.note.empty() ? g_entry.note.c_str() : ("Friend's code: " + g_entry.text + "_   Cross: done  Circle: back").c_str());
        } else {
            std::snprintf(line, sizeof line, "Host's code: %s_", g_entry.text.c_str());
            std::snprintf(under, sizeof under, "%s", !g_entry.note.empty() ? g_entry.note.c_str() : "Up/Down: 0-9, A-Y  Right: add  Left: delete  Cross: join  Circle: back");
        }
    } else if (Settings().one_client && g_entry.open) {
        if (g_entry.steam) {
            std::snprintf(line, sizeof line, "%s", NetSteamStatus());
            std::snprintf(under, sizeof under, "Looking for a Steam friend hosting Dark Cloud co-op  (Circle: back)");
        } else {
            std::snprintf(line, sizeof line, "Join at: %s_", g_entry.text.c_str());
            std::snprintf(under, sizeof under, "%s",
                          !g_entry.note.empty() ? g_entry.note.c_str()
                                                : "Up/Down: digit  Right: add  Left: delete  Cross: join  Circle: back");
        }
    } else if (Settings().one_client) {
        // The launcher's lookup of this PC's internet address (save\wan-address.txt), read once.
        static std::string wan_file;
        static bool        read_wan = false;

        if (!read_wan) {
            read_wan = true;
            std::ifstream in(PathsSaveRoot() / "wan-address.txt");
            std::getline(in, wan_file);
            while (!wan_file.empty() && (wan_file.back() == '\r' || wan_file.back() == ' ')) {
                wan_file.pop_back();
            }
        }

        // coop-consent: the internet address on these lines only as the lobby's R1 left it (game.lobby_show_ip); the twin
        // DC_CONSENT_MUTATE=ip_shown shows it as before.
        const bool wan_shown = dc::platform::ConfigGet().lobby_show_ip || TestEnvIs("DC_CONSENT_MUTATE", "ip_shown");

        // (plain START is single player: online is the Multi-player row's; SELECT still opens the address entry)
        std::snprintf(line, sizeof line, "Online: MULTIPLAYER to host or join");
        // coop-natsplit: why the last join was given up, for a while.
        if (!g_title_note.empty() && std::chrono::steady_clock::now() < g_title_note_until) {
            std::snprintf(under, sizeof under, "%s", g_title_note.c_str());
        }
        // (coop-natonly: Host > Steam is NAT > Steam's host now, g_host_via 5: a Steam that is not up is said for it too.)
        if (g_host_via == 5 && NetSteamSelf() != 0) {
            std::snprintf(line, sizeof line, "NAT by Steam: start or load a game");
            std::snprintf(under, sizeof under, "Steam friends join from Multi-player > Join > Steam; play goes direct");
        } else if (g_host_via == 4) {
            std::snprintf(line, sizeof line, "NAT: your code %s", NatOwnCode().c_str());
            std::snprintf(under, sizeof under, "Friend's code taken (%s): start or load a game", g_nat_friend.c_str());
        } else if ((g_host_via == 2 || g_host_via == 5) && NetSteamSelf() == 0) {
            // Steam is not up for this game (no steam_api64.dll, Steam not running, ...): said, not hosted offline in silence.
            static bool said = false;

            if (!said) {
                said = true;
                std::fprintf(stderr, "netplay: title menu: Host > Steam but Steam is not up: %s\n", NetSteamStatus());
            }
            const char *why = NetSteamStatus();

            std::snprintf(line, sizeof line, "Steam is not available: %s", std::strncmp(why, "Steam: ", 7) == 0 ? why + 7 : why);
            std::snprintf(under, sizeof under, "Start or load still hosts by IP (this PC: %s); for Steam, start Steam and the game again",
                          here[0] != '\0' ? here : "no network");
        } else if (g_host_via == 2) {
            std::snprintf(line, sizeof line, "Host on Steam: start or load a game");
            std::snprintf(under, sizeof under, "Steam friends then join from Multi-player > Join > Steam");
        } else if (g_host_via == 3) {
            std::snprintf(line, sizeof line, "Local: start or load a game");
            std::snprintf(under, sizeof under, "Player 2's copy then opens on the right, joins, and asks for its controller");
        } else if (g_host_via == 1 && !wan_file.empty() && !wan_shown) {
            // coop-consent (Jep, 2026-11-02): the internet address only once its player shows it (R1 in the lobby).
            std::snprintf(line, sizeof line, "Host by IP: start or load a game");
            std::snprintf(under, sizeof under, "Friends join at your internet IP (R1 in the lobby shows it), LAN %s", here[0] != '\0' ? here : "none");
        } else if (g_host_via == 1 && !wan_file.empty()) {
            std::snprintf(line, sizeof line, "Host by IP: start or load a game");
            std::snprintf(under, sizeof under, "Friends join at internet %s (%s), LAN %s", wan_file.c_str(),
                          UpnpOn() ? "the game opens the port by UPnP, or forward it" : "port forwarded", here[0] != '\0' ? here : "none");
        } else if (g_host_via == 1) {
            std::snprintf(line, sizeof line, "Host by IP: start or load a game");
            std::snprintf(under, sizeof under, "Friends join at this PC: %s", here[0] != '\0' ? here : "no network");
        } else if (!wan_file.empty() && wan_shown) {
            std::snprintf(under, sizeof under, "SELECT joins by address (internet %s, LAN %s)", wan_file.c_str(), here[0] != '\0' ? here : "none");
        } else {
            std::snprintf(under, sizeof under, "SELECT joins by address (this PC: %s)", here[0] != '\0' ? here : "no network");
        }
    } else {
        std::snprintf(line, sizeof line, "Online: press SELECT to join player 1 at %s", Settings().peer.c_str());
    }

    dc::text::caption::LineStyle style;

    style.status_font = true;
    style.edge_width = 1.5f;
    // A dark band under the two lines only while they sit over the title's copyright line (Jep, 2026-10-21: hard to
    // read there); under the trademark lines they need none, and the band showed as a black box (Jep, 2026-10-24).
    if (NetplayTitleNoticeBand()) {
        static const spRGBA kBand = {0x00, 0x00, 0x00, 0x68};

        set2DSpriteC4(Vif1Packet, CRect_i_(40, (int) NetplayTitleNoticeY(0) - 10, 560, 36), (spRGBA *) &kBand, (spRGBA *) &kBand, (spRGBA *) &kBand, (spRGBA *) &kBand);
    }
    dc::text::caption::DrawLine(line, 320.0f, NetplayTitleNoticeY(0), 10.0f, 1.0f, 560.0f, style, 1.0f, 1.0f, nullptr); // centred

    if (under[0] != '\0') {
        dc::text::caption::DrawLine(under, 320.0f, NetplayTitleNoticeY(1), 8.0f, 1.0f, 560.0f, style, 1.0f, 1.0f, nullptr);
    }
    static std::string said;
    if (said != line) {
        said = line;
        std::fprintf(stderr, "netplay: title notice at %.0f and %.0f (the trademark lines end at %d): %s | %s\n", NetplayTitleNoticeY(0), NetplayTitleNoticeY(1),
                     kTitleTrademarkBottom, line, under);
        std::fprintf(stderr, "netplay: title notice band: %s\n", NetplayTitleNoticeBand() ? "drawn" : "none");
    }
}

bool NetplayTitleNoticeBand() {
    // DC_NETPLAY_MUTATE=notice_band (a test's twin): the band under the lines in their new place too, as the merge left it.
    return TestEnvIs("DC_NETPLAY_MUTATE", "notice_old_place") || TestEnvIs("DC_NETPLAY_MUTATE", "notice_band");
}

float NetplayTitleNoticeY(int which) {
    // Under the title's trademark and copyright lines (the "start" picture's lower half, drawn at MENU_Y(362): its two
    // lines end 56 texels down it, screen row 434, their soft edge 436), so they stay legible (Jep, 2026-10-24); on the
    // 480-row screen with a margin. DC_NETPLAY_MUTATE=notice_old_place (a test's twin): over them, as before.
    if (TestEnvIs("DC_NETPLAY_MUTATE", "notice_old_place")) {
        return which == 0 ? 410.0f : 426.0f;
    }
    return which == 0 ? 446.0f : 460.0f;
}

// The online launchers' settings, for this session only (config.json keeps its own): co-op, own bags, catch-up, the
// keyboard (MenuOnline).
void OnlineSession(dc::platform::Config &config) {
    config.coop = true;
    config.coop_own_bags = true;
    config.netplay_catchup = true;
    config.netplay_keyboard = true;
}

void OnlineSessionRestore(dc::platform::Config &config, const dc::platform::Config &saved) {
    config.coop = saved.coop;
    config.coop_own_bags = saved.coop_own_bags;
    config.netplay_catchup = saved.netplay_catchup;
    config.netplay_keyboard = saved.netplay_keyboard;
}

// coop-pt1111p: this copy's run-only settings in one session (ConfigSetSession keeps one): the title's online ones (MenuOnline) and,
// on a joiner, the host's it took (NetplayHelloAdopt). Each kept out of config.json by its restore.
namespace {
bool g_adopt_session = false;

void RunSession(dc::platform::Config &config) {
    if (g_menu_online) {
        OnlineSession(config);
    }
    // (the adopted keys need no apply: the running settings hold the host's from the adopt's change on, and a later change of
    // them is this run's too)
}

void RunSessionRestore(dc::platform::Config &config, const dc::platform::Config &saved) {
    if (g_menu_online) {
        OnlineSessionRestore(config, saved);
    }
    if (g_adopt_session) {
        NetplayHelloAdoptedKeys(config, saved);
    }
}

void RunSessionSet() {
    const bool on = g_menu_online || g_adopt_session;

    ConfigSetSession(on ? RunSession : nullptr, on ? RunSessionRestore : nullptr);
}
} // namespace

void NetplayAdoptSession(bool on) {
    if (NetplayMutated("adopt_saved") || on == g_adopt_session) {
        return;
    }
    g_adopt_session = on;
    RunSessionSet();
    std::fprintf(stderr, on ? "netplay: the host's settings are this run's (config.json keeps this copy's own)\n"
                            : "netplay: this copy's own settings again (the host's were this run's)\n");
}

// A plain copy (no online setting) goes online for the title's Multi-player, or back to single player. Only at the
// title: nothing has linked, so the link may start again from there.
namespace {
void MenuOnline(bool on) {
    if (TestEnvInt("DC_NETPLAY", dc::platform::ConfigGet().netplay) != 0 || on == g_menu_online) {
        return;
    }

    g_menu_online = on;
    g_link.started = false;
    g_role = 0;
    g_join_picked = false;
    if (!NetplayMutated("no_session")) {
        RunSessionSet(); // (coop-pt1111p: with the adopted keys, when a joiner's)
    }
    if (on) {
        // As the LAN pack's launcher sets before the game starts: Xbox pads over raw HID (SDL applies these as they change).
        SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_XBOX, "1");
        SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_XBOX_ONE, "1");
    }
    std::fprintf(stderr, on ? "netplay: title menu: online for this session (co-op, own bags, catch-up, keyboard; config.json unchanged)\n"
                            : "netplay: title menu: single player again\n");
}
} // namespace

bool NetplayTitleMenuShown() {
    // A plain copy shows it too: one client on demand.
    if (TestEnvInt("DC_NETPLAY", dc::platform::ConfigGet().netplay) == 0) {
        return true;
    }
    return Settings().one_client && JoinMenu();
}

void NetplayTitleOpenJoin(bool steam) {
    if (!NetplayTitleMenuShown() || g_join_picked || g_entry.open) {
        return;
    }
    MenuOnline(true);
    if (g_link.started) {
        return;
    }

    // What SELECT (and its Square) opens; the buttons held now are not read as presses there.
    g_entry.last = OwnInput(2).buttons;
    EntryOpen();
    std::fprintf(stderr, "netplay: title menu: Join > %s\n", steam ? "Steam" : "IP");

    if (steam) {
        NetSteamFindReset();
        g_entry.steam = true;
        std::fprintf(stderr, "netplay: looking for a Steam friend hosting\n");
        SteamAsNat(4, "Join > Steam"); // coop-natonly (NAT > Steam's own pick sets it again, the same)
    }
}

void NetplayTitleHost(int via) {
    const int  was = g_host_via;
    const bool steam = via == 2;

    // coop-natonly: Host > Steam is NAT > Steam's host (its lobby carries this host's code; play goes direct).
    if (via == 2 && SteamAsNat(3, "Host > Steam")) {
        via = 5;
    }

    if (via == 3) {
        g_local_pad = ActivePad();
        std::fprintf(stderr, "netplay: local: player 1 plays controller %d\n", g_local_pad + 1);
    }

    MenuOnline(via != 0);
    if (via == 0) {
        g_nat = 0;
        g_nat_friend.clear();
    }

    g_host_via = via;
    // Host > Steam starts Steam now (Spacewar shows, the line under the menu says if it cannot); leaving it stops it.
    if (via == 2 || via == 5) {
        NetSteamStart();
    } else if ((was == 2 || was == 5) && !NetplayMutated("steam_kept")) {
        NetSteamStop("Host > Steam left");
    }
    std::fprintf(stderr, "netplay: title menu: Host > %s\n", steam ? "Steam (start or load a game to host)" : via == 1 ? "IP (start or load a game to host)" : via == 3 ? "Local (start or load a game; player 2's copy follows)" : "none (back)");
}

void NetplayTitleNat(int role) {
    if (!NetplayTitleMenuShown() || g_join_picked || g_entry.open) {
        return;
    }
    MenuOnline(true);
    g_nat = role;
    g_nat_friend.clear();
    g_punch = PunchLog{};
    g_nat_times = NatSteamTimes{};
    NatSince();
    if (role == 1) {
        // Hosting: player 1's socket now, so STUN finds the router's mapping the link will keep (NetSocketOpen).
        const LinkSettings settings = Settings();
        const char        *stun = TestEnv("DC_NETPLAY_STUN");

        g_host_via = 4;
        if (NetSocketOpen(settings.port + 1, "any", settings.port + 2)) {
            NetSocketStunStart(stun != nullptr ? stun : "stun.l.google.com:19302", settings.port + 1);
            if (UpnpOn()) { // coop-upnp: the code then carries the forwarded port
                NetUpnpStart(settings.port + 1);
            }
        }
    }
    g_entry.last = OwnInput(2).buttons;
    g_entry.open = true;
    g_entry.code = true;
    g_entry.steam = false;
    // coop-lobby: Join > Code opens with the code joined last (config), so a rejoin is Cross.
    g_entry.text = role == 2 ? dc::platform::ConfigGet().netplay_last_code : std::string();
    g_entry.note.clear();
    std::fprintf(stderr, "netplay: title menu: %s > NAT > Code%s\n", role == 1 ? "Host" : "Join", !g_entry.text.empty() ? " (the last code typed in)" : "");
}

// coop-lobby: the host's lobby listens as Host > IP and Host > Code did together (the code is the outside address in letters):
// player 1's socket opens now, STUN asks the outside address (the code) and UPnP asks the router for the port.
void NetplayLobbyListen(bool outside) {
    const LinkSettings settings = Settings();
    const char        *stun = TestEnv("DC_NETPLAY_STUN");

    MenuOnline(true);
    g_lobby_hosts = true;
    if (!NetSocketOpen(settings.port + 1, "any", settings.port + 2)) {
        std::fprintf(stderr, "netplay: lobby: cannot listen on UDP port %d\n", settings.port + 1);
        return;
    }
    // (a local-only lobby listens for its own windows' copies alone: no outside address, no router asked)
    if (outside && dc::platform::ConfigGet().netplay_wan_lookup) {
        NetSocketStunStart(stun != nullptr ? stun : "stun.l.google.com:19302", settings.port + 1);
    }
    if (outside && UpnpOn()) {
        NetUpnpStart(settings.port + 1);
    }
    std::fprintf(stderr, "netplay: lobby: listening on UDP port %d (STUN %s, UPnP %s)\n", settings.port + 1,
                 outside && dc::platform::ConfigGet().netplay_wan_lookup ? "asked" : "off", outside && UpnpOn() ? "asked" : "off");
}

int NetplayActivePad() {
    return ActivePad();
}

int NetplayOwnPad() {
    const int player = NetplayLocalPlayer();

    return player != 0 ? OwnPad(player) : -1;
}

InputPadState NetplayLobbyOwnPad() {
    return OwnInput(2);
}

void NetplayLobbyHandOff(int seat, int host_pad, int pad, std::uint64_t identity) {
    g_handoffs.push_back(HandOff{seat, host_pad, pad, identity});
}

void NetplayLobbyPinPad(int pad) {
    // DC_LOBBY_MUTATE=pad_follows (l-lobby -Case pads' twin): no pin, the controller last pressed as before.
    if (TestEnvIs("DC_LOBBY_MUTATE", "pad_follows") || pad == g_lobby_pad) {
        return;
    }
    g_lobby_pad = pad;
    if (pad >= 0) {
        std::fprintf(stderr, "netplay: lobby: this copy plays controller %d alone\n", pad + 1);
    } else {
        std::fprintf(stderr, "netplay: lobby: closed, the controller last pressed again\n");
    }
}

void NetplayLobbyStartedWindow(int host_pad) {
    g_lobby_spawned = true;
    g_local_pad = host_pad;
    std::fprintf(stderr, "netplay: lobby: a window started for a player on this PC; this copy keeps controller %d\n", host_pad + 1);
}

const char *NetplayLobbyCode() {
    static std::string code;

    // coop-reds2 (mp-nat's "A's code shown in its lobby before its start"): DC_NETPLAY_MUTATE=lobby_code_none (twin): the
    // lobby never has this copy's code to show ("..."), as if STUN never answered.
    if (NetplayMutated("lobby_code_none")) {
        code.clear();
        return code.c_str();
    }
    code = NatOwnCode();
    return code.c_str();
}

// coop-lobby (plan 4.3a): the host's Friend's code: the host punches towards that code's address until the link is up (as
// Host > Code did), so a friend behind a closed router gets in. False when the code does not decode.
bool NetplayLobbyFriendCode(const char *code) {
    std::string address;

    if (code == nullptr || !NatDecode(code, address)) {
        std::fprintf(stderr, "netplay: lobby: friend's code %s does not decode\n", code != nullptr ? code : "(none)");
        return false;
    }
    g_nat = 1;
    g_nat_friend = address;
    g_punch = PunchLog{};
    NatSince();
    // coop-reds1109: the first punch goes out as the code is taken, not at the next net tick, whose socket read comes before
    // its punch: a friend's datagram read first is from an address not yet sent to (a fresh one: "open (proven)").
    // (coop-pilot: its twin is friend_code_late; the old no_punch guard copied here made the registry count no_punch as added,
    // and no test turns no_punch on)
    if (const std::size_t colon = address.rfind(':'); colon != std::string::npos && !NetplayMutated("friend_code_late")) {
        NetSocketPunch(address.substr(0, colon).c_str(), std::atoi(address.c_str() + colon + 1));
    }

    dc::platform::Config config = dc::platform::ConfigGet();

    config.netplay_last_code = code;
    ConfigChange(config);
    std::fprintf(stderr, "netplay: lobby: friend's code taken: %s, punching towards it\n", address.c_str());
    return true;
}

// coop-lobby: this copy's lobby identity (game.netplay_player_id): made once at random and kept in its own config.json (its
// save folder's, never another copy's), so a rejoin by the same way gets its seat, character and bag back.
std::uint64_t NetplayPlayerId() {
    static std::uint64_t id = 0;

    if (id != 0) {
        return id;
    }
    // A Multi screen seat's copy: its lobby seat's identity (DC_NETPLAY_PLAYER_ID, the lobby's hand-off), never kept.
    const char       *handed = std::getenv("DC_NETPLAY_PLAYER_ID");
    if (handed != nullptr && std::strlen(handed) == 16 && (id = std::strtoull(handed, nullptr, 16)) != 0) {
        std::fprintf(stderr, "netplay: player id %s (the lobby's seat)\n", handed);
        return id;
    }
    const std::string kept = dc::platform::ConfigGet().netplay_player_id;

    id = kept.size() == 16 ? std::strtoull(kept.c_str(), nullptr, 16) : 0;
    if (id == 0) {
        std::random_device rd;

        id = ((std::uint64_t) rd() << 32) ^ rd();
        id = id != 0 ? id : 1;

        char   text[17];
        dc::platform::Config config = dc::platform::ConfigGet();

        std::snprintf(text, sizeof text, "%016llx", (unsigned long long) id);
        config.netplay_player_id = text;
        ConfigChange(config);
        std::fprintf(stderr, "netplay: player id made: %s\n", text);
    }
    return id;
}

const char *NetplayLobbyFriend() {
    return g_nat_friend.c_str();
}

// coop-lobby + coop-lobbysteam (plan 4.3: Steam is the host's Select in the lobby): on starts Steam and opens the friends' lobby
// for six (NetSteamLobbyListen) as NAT > Steam's host (Steam hands the codes over, play goes direct); off closes it (and at
// the game's start, so the game does not show as Spacewar while playing: Jep's rule of 2026-10-21).
bool NetplayLobbySteam(bool on) {
    if (on) {
        SteamAsNat(3, "the lobby's Select");
        NetSteamStart();
        g_nat_published = false;
        // DC_NETPLAY_MUTATE=no_steam_lobby (mp-title-menu host-steam's twin): Steam starts but no lobby opens.
        const bool open = !NetplayMutated("no_steam_lobby") && NetSteamLobbyListen(true);

        std::fprintf(stderr, "netplay: lobby: Steam %s\n", open ? "listening" : NetSteamStatus());
        return open;
    }
    NetSteamLobbyListen(false);
    if (g_nat == 3) {
        g_nat = 0;
    }
    // Steam goes with it (Spacewar only while Steam is in use: Jep, 2026-10-21). DC_NETPLAY_MUTATE=steam_kept (mp-title-menu
    // host-steam-back's twin): left running, as Host > Steam's Back once did.
    if (!NetplayMutated("steam_kept")) {
        NetSteamStop("the lobby's Steam closed");
    }
    std::fprintf(stderr, "netplay: lobby: Steam closed\n");
    return false;
}

// The lobby started with Steam listening (its Select): Steam is carried into the game as NAT > Steam's host had it, so a
// friend can still join by Join > Steam after the start, Steam stops once a player 2 is linked directly, and starts again
// when they drop (coop-steamhelper, Jep 2026-10-30: "it should still just relaunch the helper when P2 disconnects"). The
// lobby's own Steam lobby closes here; the game's hosting opens the game's (NetSteamHost, g_host_via 5).
void NetplayLobbySteamToGame() {
    NetSteamLobbyListen(false);
    g_host_via = 5;
    // The game's lobby is a new one: its code goes on it again (mp-nat -Steam's first run, 2026-11-01: "this host's code in
    // the lobby" never came, the lobby's own publish counted as done). NAT > Steam's host from here on.
    g_nat = 3;
    g_nat_published = false;
    std::fprintf(stderr, "netplay: lobby: Steam carried into the game (NAT > Steam's host)\n");
}

// Each lobby frame while Steam listens: the host's code on the lobby once STUN has it, a guest's code taken from Steam (the
// punch towards it then runs from NetplayTick), and the seat table as the lobby's data.
void NetplayLobbySteamTick(const void *data, int size) {
    const std::string seats((const char *) data, (std::size_t) size);

    if (!NetSteamListening()) {
        return;
    }
    const std::string own = NatOwnCode();

    if (!g_nat_published && !own.empty() && NetSteamLobbySet("nat", own.c_str())) {
        g_nat_published = true;
        std::fprintf(stderr, "netplay: lobby: this host's code on the Steam lobby: %s\n", own.c_str());
    }
    std::string heard;

    if (NatDecode(NetSocketNatHeard(), heard) && heard != g_nat_friend) {
        g_nat_friend = heard;
        std::fprintf(stderr, "netplay: lobby: a guest's code by Steam: %s (punching towards it)\n", heard.c_str());
    }
    static std::string said;

    if (seats != said) {
        said = seats;
        NetSteamLobbySetData(seats);
    }
}

bool NetplayTitleTakeSteamList() {
    const bool wanted = g_entry_steam_list;

    g_entry_steam_list = false;
    return wanted;
}

bool NetplayQaStopSeen() {
    return QaStopSeen();
}

bool NetplayLobbyJoining() {
    return g_join_picked && g_link.started && !g_link.up && g_link.listening && g_role == 2;
}

bool NetplayLobbyServesJoin() {
    return g_link.started && !g_link.up && g_link.listening && g_link.player == 1 && mode != GAME_MODE_TITLE;
}

void NetplayLobbyLeaveJoin(const char *note) {
    if (g_join_picked && !g_link.up) {
        JoinGiveUp("left the lobby", note);
    }
}

int NetplayLobbyPort() {
    return Settings().port + 1;
}

bool NetplayLobbyUpnpWanted() {
    return UpnpOn();
}

// coop-punchlog: one line of a NAT join's story: "netplay: punch: <text>" in the log, and an event in netplay-diag.jsonl
// (held until the file opens with the link). DC_NETPLAY_MUTATE=punch_no_log (twin): none.
void PunchSay(const char *text, const char *json_extra = "") {
    if (NetplayMutated("punch_no_log")) {
        return;
    }
    std::fprintf(stderr, "netplay: punch: %s\n", text);

    char        t[32];
    std::string line;

    std::snprintf(t, sizeof t, "%.1f", NatSince() / 1000.0);
    line = std::string("{\"t\":") + t + ",\"event\":\"punch\",\"text\":\"";
    for (const char *c = text; *c != '\0'; c++) {
        if (*c == '"' || *c == '\\') {
            line += '\\';
        }
        line += *c;
    }
    line += "\"";
    line += json_extra;
    line += "}\n";
    if (g_link.diag == nullptr) {
        // Held for the file the link opens (it starts it afresh, these first), and on disk now too: a join given up
        // before its link ever opened (coop-lobby's Join > Steam pick, the punch blocked) still leaves its story there.
        g_punch.early.push_back(line);
        const std::filesystem::path diag = PathsSaveRoot() / "netplay-diag.jsonl";

        static bool started = false; // (this session's file: a last session's lines never stand in for these)

        if (std::FILE *file = std::fopen(PathsDisplay(diag).c_str(), started ? "a" : "w")) {
            started = true;
            std::fputs(line.c_str(), file);
            std::fclose(file);
        }
        return;
    }
    for (const std::string &held : g_punch.early) {
        std::fputs(held.c_str(), g_link.diag);
    }
    g_punch.early.clear();
    std::fputs(line.c_str(), g_link.diag);
    std::fflush(g_link.diag);
}

// coop-punchlog: 15 s of punching without a link (NAT > Steam's guest switches to Steam then: switching).
void PunchNoAnswer(bool switching, const char *given_up = nullptr) {
    PunchLog &p = g_punch;

    if (!p.on || p.late) {
        return;
    }
    p.late = true;

    long long   sent = 0, heard = 0;
    const char *from = "";
    char        text[256];

    NetSocketUdpCounts(sent, heard, from);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - p.at).count();
    const char  *then = given_up != nullptr ? given_up : switching ? ", switching to Steam" : g_nat == 3 ? (NetplayMutated("nat_steam_fallback") ? "; the guest switches to Steam" : "; the guest gives up at its menu, unless the link comes") : "; NAT > Code has no fallback: still punching";

    if (heard == p.heard0) {
        std::snprintf(text, sizeof text, "no answer after %.0f s (%lld probes sent)%s", seconds, sent - p.sent0, then);
    } else {
        std::snprintf(text, sizeof text, "%lld datagrams heard but no link after %.0f s (%lld probes sent)%s", heard - p.heard0, seconds,
                      sent - p.sent0, then);
    }
    PunchSay(text);
}

// coop-punchlog: the Steam leg's time on its route so far, added to its relayed or direct seconds.
void PunchLegTime(std::chrono::steady_clock::time_point now) {
    PunchLog &p = g_punch;

    if (!p.route.empty()) {
        (p.route == "direct" ? p.direct_s : p.relayed_s) += std::chrono::duration<double>(now - p.route_at).count();
    }
    p.route_at = now;
}

// coop-punchlog: a Steam leg's end (the link down, a host's lobby opened again, the game quitting): its time on each route.
void PunchLegEnd() {
    PunchLog &p = g_punch;

    if (p.route.empty() || p.ended) {
        return;
    }
    p.ended = true;
    PunchLegTime(std::chrono::steady_clock::now());

    char text[200];

    std::snprintf(text, sizeof text, "Steam leg ended: relayed %.0f s, direct %.0f s, %d route changes, last %s", p.relayed_s, p.direct_s, p.changes,
                  p.route.c_str());
    PunchSay(text);
}

// coop-punchlog, each frame of a NAT join (NAT > Code or NAT > Steam, either side): see PunchLog.
void PunchLogTick() {
    PunchLog &p = g_punch;

    if (g_nat < 1 || g_nat > 4 || p.done) {
        return;
    }

    const auto  now = std::chrono::steady_clock::now();
    long long   sent = 0, heard = 0;
    const char *from = "";
    char        text[320];

    NetSocketUdpCounts(sent, heard, from);

    // Where this copy's datagrams go: the host's punches to the friend's code, the joiner's Hellos to the host's (not
    // when its way is Steam from the start: no code in the host's lobby).
    const bool        host = g_nat == 1 || g_nat == 3;
    const std::string target = host ? g_nat_friend : g_join_picked && g_join_peer.rfind("steam:", 0) != 0 ? g_join_peer : "";

    if (!p.on && !target.empty() && !g_link.up) {
        int         port = 0;
        const char *wan = NetSocketWan(port);
        char        own[40] = "not known yet";

        if (wan[0] != '\0') {
            std::snprintf(own, sizeof own, "%s:%d", wan, port);
        }
        p.on = true;
        p.at = now;
        p.next = now + std::chrono::seconds(3);
        p.sent0 = sent;
        p.heard0 = heard;
        std::snprintf(text, sizeof text, "start towards %s, this side's outside address %s (%s)", target.c_str(), own,
                      host ? "punches 4 a second" : "a Hello twice a second");
        p.wan_said = wan[0] != '\0';
        PunchSay(text);
    }

    // (A joiner's STUN asks only once its link opens, so its own address often comes after the start line.)
    if (p.on && !p.wan_said) {
        int         port = 0;
        const char *wan = NetSocketWan(port);

        if (wan[0] != '\0') {
            p.wan_said = true;
            std::snprintf(text, sizeof text, "this side's outside address %s:%d (STUN)", wan, port);
            PunchSay(text);
        }
    }

    if (p.on && !p.heard && heard > p.heard0) {
        p.heard = true;
        std::snprintf(text, sizeof text, "first datagram heard from %s after %.1f s", from, std::chrono::duration<double>(now - p.at).count());
        PunchSay(text);
    }

    // coop-joinbudget (Jep's 11-03 internet join, pack 8: the host's log said "78 datagrams heard but no link after 15 s"
    // and a probe count every 3 s for the 290 s the two sat in the lobby, then "join time 303.3 s"): the lobby runs over
    // this path, so its datagrams prove it; the game's link opens only at the host's start. While the lobby is open the
    // punch says so once and waits; at its close the punch's clock starts again (the join time is the link's, not the
    // lobby's).
    if (p.on && !p.result && !g_link.up) {
        const bool lobby = coop::lobby::Active();

        if (lobby && !p.in_lobby) {
            p.in_lobby = true;
            p.lobby_s -= std::chrono::duration<double>(now - p.at).count();
            std::snprintf(text, sizeof text, "the lobby is open over this path after %.1f s (%lld datagrams heard): the game's link opens at the host's start",
                          std::chrono::duration<double>(now - p.at).count(), heard - p.heard0);
            PunchSay(text);
        } else if (!lobby && p.in_lobby) {
            p.in_lobby = false;
            p.lobby_s += std::chrono::duration<double>(now - p.at).count();
            p.at = now;
            p.next = now + std::chrono::seconds(3);
            p.sent0 = sent;
            p.heard0 = heard;
            std::snprintf(text, sizeof text, "the lobby closed after %.0f s in it: the game's link from now", p.lobby_s);
            PunchSay(text);
        }
        if (p.in_lobby) {
            PunchLegEnd();
            return;
        }
    }

    if (p.on && !p.result && !g_link.up && now >= p.next) {
        p.next = now + std::chrono::seconds(3);
        std::snprintf(text, sizeof text, "%.0f s: %lld probes sent, %lld datagrams heard", std::chrono::duration<double>(now - p.at).count(),
                      sent - p.sent0, heard - p.heard0);
        PunchSay(text);
    }

    // NAT > Steam's guest says it as it switches (NatSteamTick); everyone else at 15 s.
    if (p.on && !p.result && !g_link.up && g_nat != 4 && now - p.at >= std::chrono::seconds(15)) {
        PunchNoAnswer(false);
    }

    if (!g_link.up) {
        PunchLegEnd();
        return;
    }

    const std::string peer = host ? std::string(NetSocketPeerText()) : g_join_peer;
    const bool        steam = peer.rfind("steam:", 0) == 0;
    const auto        summary = [&](const char *path) {
        char extra[96];

        p.summary = true;
        char lobby[64] = "";

        if (p.lobby_s > 0.0) {
            std::snprintf(lobby, sizeof lobby, "; %.1f s of it in the lobby", p.lobby_s);
        }
        // (coop-joinbudget: a lobby's time is in the join time, NAT picked to link up; said apart, the punch's clock from its close)
        std::snprintf(text, sizeof text, "path = %s, join time %.1f s (NAT picked to link up%s; %.1f s from the punch's start%s)", path,
                      p.link_ms / 1000.0, lobby, p.on ? std::chrono::duration<double>(p.linked - p.at).count() : 0.0,
                      p.lobby_s > 0.0 ? " or the lobby's close" : "");
        std::snprintf(extra, sizeof extra, ",\"path\":\"%s\",\"join_s\":%.1f", path, p.link_ms / 1000.0);
        PunchSay(text, extra);
    };

    if (!p.result) {
        p.result = true;
        p.linked = now;
        p.link_ms = NatSince();
        p.steam_next = now + std::chrono::seconds(2);
        if (!steam) {
            std::snprintf(text, sizeof text, "direct link in %.1f s with %s%s", p.on ? std::chrono::duration<double>(now - p.at).count() : 0.0,
                          peer.c_str(), p.lan ? " (found on the LAN)" : "");
        } else if (p.on) {
            std::snprintf(text, sizeof text, "linked by Steam %.1f s after the punch started", std::chrono::duration<double>(now - p.at).count());
        } else {
            std::snprintf(text, sizeof text, "linked by Steam, no punch (no code in the host's lobby)");
        }
        PunchSay(text);
    }

    if (!steam) {
        summary(p.lan ? "direct-lan" : "direct-punch");
        p.done = true;
        return;
    }

    // The Steam leg: Steam's own word on the session (NetSteamLine: relayed or direct, and by which relay), 2 s after the
    // link and every 5 s while it lasts, a change of route logged as it comes (Valve dropping the relay for a direct
    // way, or back); the summary goes with Steam's first answer, the time on each route with the link's end.
    if (now < p.steam_next) {
        return;
    }
    p.steam_next = now + std::chrono::seconds(5);
    if (!p.summary && ++p.steam_looks > 12) {
        summary("steam-unknown"); // (Steam never said in a minute)
        p.done = true;
        return;
    }

    char line[256];

    if (!NetSocketSteamLine(line, sizeof line)) {
        return;
    }

    const char       *relayed = std::strstr(line, "(relayed");
    const std::string route = relayed != nullptr ? std::string(relayed + 1, std::strcspn(relayed + 1, ")")) : "direct";

    if (route != p.route) {
        const double on_steam = std::chrono::duration<double>(now - p.linked).count();

        PunchLegTime(now);
        if (p.route.empty()) {
            static bool at_exit = false;

            if (!at_exit) {
                at_exit = true;
                std::atexit([] { PunchLegEnd(); });
            }
            std::snprintf(text, sizeof text, "Steam leg %s: %s", route.c_str(), line);
        } else {
            p.changes++;
            std::snprintf(text, sizeof text, "Steam leg now %s, %.0f s into the link (%s): %s", route.c_str(), on_steam,
                          relayed == nullptr ? "Steam dropped the relay for a direct way" : "Steam went back to a relay", line);
        }
        p.route = route;
        PunchSay(text);
    }
    if (!p.summary) {
        summary(relayed != nullptr ? "steam-relay" : "steam-direct");
    }
}

// NAT by Steam once linked (Jep, 2026-10-25: "still leave spacewar running at the end"): Steam only found the friend and
// carried the codes, so 3 s after a direct link (the codes' last sends done) both copies leave the lobby and shut Steam
// down, and neither shows as Spacewar any more. Play by the Steam relay, or pads read through Steam Input, keep it up.
// A host whose player 2 drops opens its lobby again, so they can come back by NAT > Steam. The join's time is logged.
// DC_NETPLAY_MUTATE=nat_keep_steam (twin): Steam never shut down; nat_stop_on_relay (twin): shut down on the relay too.
void NatSteamAfterLink() {
    NatSteamTimes &t = g_nat_times;

    if (g_nat != 3 && g_nat != 4) {
        return;
    }
    if (!g_link.up) {
        // coop-steamsplit (mp-nat -Split, red on main f87d6b82): a split before Steam's 3 s were up kept Steam (and the
        // lobby, and Spacewar on the profile) up for the whole time apart, and shut it only at the merge. The direct link
        // goes on apart (natsplit), so its 3 s count on: Steam shuts down apart as it would have linked.
        // DC_NETPLAY_MUTATE=nat_steam_apart_up (twin): up until the merge, as before.
        if (!t.stopped && t.direct && t.linked >= 0 && CoopWorldsSplitPeerAlive() && !NetSteamInputOn() && !NetplayMutated("nat_steam_apart_up")) {
            const long long now = NatSince();

            if (now - t.linked >= 3000 && !NetplayMutated("nat_keep_steam")) {
                t.stopped = true;
                std::fprintf(stderr, "netplay: nat: direct link up %lld ms: Steam no longer needed (apart, the other copy still heard directly)\n",
                             now - t.linked);
                NetSteamStop("direct link");
            }
            return;
        }
        // coop-natsplit (Jep's 2026-10-28 NAT > Steam play: he went out of a house, she stayed in, and Spacewar came back):
        // a split is no leave. The other copy still talks to this one over the direct link, which comes back at the merge,
        // so Steam stays down; only a player gone quiet (or a dropped link) opens the lobby again for a new handoff.
        // DC_NETPLAY_MUTATE=nat_split_steam (twin): the lobby opens again at every split, as before.
        if (g_nat == 3 && t.stopped && g_link.listening && CoopWorldsSplitPeerAlive() && !NetplayMutated("nat_split_steam")) {
            if (!t.apart_said) {
                t.apart_said = true;
                std::fprintf(stderr, "netplay: nat: apart, player 2 still heard directly: Steam stays down\n");
            }
            return;
        }
        // coop-steamhelper: NetSteamHost starts the Steam helper again (it exited after the direct link) for the new lobby.
        // DC_NETPLAY_MUTATE=steam_no_relaunch (mp-nat -Rejoin's twin): no helper and no lobby after the drop.
        if (g_nat == 3 && t.stopped && g_link.listening && !NetplayMutated("steam_no_relaunch")) {
            t = NatSteamTimes{};
            PunchLegEnd();
            g_punch = PunchLog{};
            g_nat_published = false;
            NatSince();
            NetSteamHost(kSteamTag);
            std::fprintf(stderr, "netplay: nat: player 2 left: the Steam lobby opens again for NAT > Steam\n");
        }
        return;
    }

    const long long   now = NatSince();
    const std::string peer = g_nat == 3 ? std::string(NetSocketPeerText()) : g_join_peer;
    const bool        relay = peer.rfind("steam:", 0) == 0;

    t.direct = t.direct || !relay;
    t.apart_said = false;
    if (t.linked < 0) {
        t.linked = now;
        // (The steps in ms since NAT > Steam was picked; -1, a step this copy never took.)
        std::fprintf(stderr, "netplay: nat: linked %s by %s in %.1f s (lobby code at %lld ms, codes swapped at %lld ms)\n", peer.c_str(),
                     relay ? "the Steam relay" : "a direct link", now / 1000.0, t.lobby, t.code);
    }
    if (t.stopped) {
        return;
    }
    if (relay && !NetplayMutated("nat_stop_on_relay")) {
        if (!t.said) {
            t.said = true;
            std::fprintf(stderr, "netplay: nat: play goes by the Steam relay: Steam stays up\n");
        }
        return;
    }
    if (NetSteamInputOn()) {
        if (!t.said) {
            t.said = true;
            std::fprintf(stderr, "netplay: nat: Steam Input reads the pads: Steam stays up\n");
        }
        return;
    }
    if (now - t.linked < 3000 || NetplayMutated("nat_keep_steam")) {
        return;
    }
    t.stopped = true;
    std::fprintf(stderr, "netplay: nat: direct link up %lld ms: Steam no longer needed\n", now - t.linked);
    NetSteamStop("direct link");
}

// NAT by Steam, each tick: the host puts its code in its lobby and punches towards the code a guest sends; a guest sends
// its own code to the host over Steam until the link is up, and after 15 s without one takes the Steam relay.
void NatSteamTick() {
    static auto next = std::chrono::steady_clock::now();
    auto        now = std::chrono::steady_clock::now();

    int &sent = g_nat_times.sent;

    // coop-natonly (Jep, 2026-10-29: "Steam being a NAT handoff only"): a NAT > Steam host takes the codes over Steam and
    // nothing else, so a guest that came over Steam (an older build's fallback) is never answered and the host waits on,
    // its lobby open for a direct join. DC_NETPLAY_MUTATE=nat_host_takes_steam (twin): taken, play over Steam, as before.
    NetSocketSteamCodesOnly(g_nat == 3 && !NetplayMutated("nat_host_takes_steam"));
    NatSteamAfterLink();

    // Hosting: a guest's code heard over Steam (taken even once the link is up, for the log).
    if (g_nat == 3 && g_nat_friend.empty()) {
        std::string heard;

        if (NatDecode(NetSocketNatHeard(), heard)) {
            g_nat_friend = heard;
            if (g_nat_times.code < 0) {
                g_nat_times.code = NatSince();
            }
            std::fprintf(stderr, "netplay: nat: friend's code by Steam: %s\n", heard.c_str());
        }
    }

    // (Up already on one PC with no NAT between: the codes still go both ways for 5 s, so each side has the other's;
    // 3 sends were sometimes all spent before the host's Steam session took them.)
    if ((g_nat != 3 && g_nat != 4) || !g_link.started || (g_link.up && (g_nat == 3 || sent >= 10)) || now < next) {
        return;
    }
    next = now + std::chrono::milliseconds(500);

    const std::string own = NatOwnCode();

    if (g_nat == 3) {
        if (!g_nat_published && !own.empty() && !NetplayMutated("nat_no_lobby_code") && NetSteamLobbySet("nat", own.c_str())) {
            g_nat_published = true;
            g_nat_times.lobby = NatSince();
            std::fprintf(stderr, "netplay: nat: this host's code in the lobby: %s\n", own.c_str());
        }
        std::string heard;

        if (NatDecode(NetSocketNatHeard(), heard) && heard != g_nat_friend) {
            g_nat_friend = heard;
            if (g_nat_times.code < 0) {
                g_nat_times.code = NatSince();
            }
            std::fprintf(stderr, "netplay: nat: friend's code by Steam: %s\n", heard.c_str());
        }
        return;
    }

    // coop-natsplit: the 15 s counted from this join's start (it was from the first call of the run, so every later start of
    // a link, a split's merge among them, took the relay at once), and never once the pair has linked directly: a split or
    // a drop links again by the same direct way (Jep, 2026-10-28: Spacewar only for the handoff).
    // DC_NETPLAY_MUTATE=nat_split_steam (twin): the old clock, and the relay after a direct link too.
    static const auto first = now;
    const bool        old = NetplayMutated("nat_split_steam");

    if (g_nat_times.guest_start == std::chrono::steady_clock::time_point{}) {
        g_nat_times.guest_start = old ? first : now;
    }

    // coop-steamlobby (Jep's 11-08 playtest: the host's lobby disappeared after 15 s): sitting in the host's lobby is the direct
    // way working (the lobby's own silence check watches it; the link only comes at its start), so the 15 s count from leaving it.
    // Twins: nat_lobby_gives_up (no reset: the count runs through the lobby, as before) and nat_lobby_stale (reset once, as the lobby is
    // entered: the count that ran in the lobby is still behind the guest when the start comes, and it gives up as it leaves).
    static bool in_lobby_before = false;
    const bool  in_lobby = coop::lobby::GuestIn();

    if (in_lobby && !NetplayMutated("nat_lobby_gives_up") && (!in_lobby_before || !NetplayMutated("nat_lobby_stale"))) {
        g_nat_times.guest_start = now;
    }
    in_lobby_before = in_lobby;

    const auto start = g_nat_times.guest_start;
    bool      &relay = g_nat_times.relay;

    if (!own.empty() && g_nat_steam_host != 0 && !g_nat_times.stopped) {
        const std::string message = "DCNAT " + own;

        NetSteamSend(g_nat_steam_host, message.data(), message.size());
        if (sent++ == 0) {
            g_nat_times.code = NatSince();
            std::fprintf(stderr, "netplay: nat: this guest's code sent by Steam: %s\n", own.c_str());
        }
    }
    if (g_nat_times.direct && !old) {
        if (!g_nat_times.apart_said && !g_link.up) {
            g_nat_times.apart_said = true;
            std::fprintf(stderr, "netplay: nat: link down after a direct link: linking again directly, no Steam\n");
        }
        return;
    }
    // coop-natsplit (Jep, 2026-10-29: "have it figure out the entirety of the nat connection before leaving the main menu"):
    // NAT > Steam is the direct way or none. No direct link in 15 s: the join is given up at the title, with a note, and
    // the player picks what next (Join > Steam plays over Steam on purpose). DC_NETPLAY_MUTATE=nat_steam_fallback (twin
    // and the relay tests): the Steam relay after 15 s, as before.
    if (!relay && g_nat_steam_host != 0 && g_join_peer.rfind("steam:", 0) != 0 && now - start > std::chrono::seconds(15) &&
        !NetplayMutated("nat_steam_fallback") && !old) {
        relay = true; // (once)
        PunchNoAnswer(false, ", giving up at the menu (no Steam fallback)");
        JoinGiveUp("given up", "No direct link to the host in 15 s. Try Code, or IP with the host's port forwarded");
        return;
    }
    if (!relay && g_nat_steam_host != 0 && g_join_peer.rfind("steam:", 0) != 0 && now - start > std::chrono::seconds(15)) {
        char peer[40];

        relay = true;
        std::snprintf(peer, sizeof peer, "steam:%llu", (unsigned long long) g_nat_steam_host);
        g_join_peer = peer;
        std::fprintf(stderr, "netplay: nat: no direct link in 15 s: falling back to the Steam relay\n");
        PunchNoAnswer(true);
        NetSocketOpen(Settings().port + 2, peer, 0);
        NetSocketLanSearch(0);
    }
}

void NetplayTitleNatSteam(int role) {
    if (!NetplayTitleMenuShown() || g_join_picked || g_entry.open) {
        return;
    }
    if (role == 1) {
        NetplayTitleHost(5);
        g_nat = 3;
        g_nat_times = NatSteamTimes{};
        g_punch = PunchLog{};
        NatSince();
        std::fprintf(stderr, "netplay: title menu: Host > NAT > Steam\n");
        return;
    }
    NetplayTitleOpenJoin(true);
    g_nat = 4;
    g_nat_times = NatSteamTimes{};
    g_punch = PunchLog{};
    NatSince();
    std::fprintf(stderr, "netplay: title menu: Join > NAT > Steam\n");
}

bool NetplayTitleNatReady() {
    return g_nat == 1 && !g_nat_friend.empty();
}

void NetplayDrawNotice() {
    auto now = std::chrono::steady_clock::now();

    if (g_notice.text.empty() || now >= g_notice.until || NetplayMutated("no_notice")) {
        return;
    }

    // The game's own lettering (not the status font), white with its dark edge, fading over the last second.
    float              left_ms = (float) std::chrono::duration_cast<std::chrono::milliseconds>(g_notice.until - now).count();
    float              fade = std::min(1.0f, left_ms / 1000.0f);
    dc::text::caption::LineStyle  style;
    dc::text::caption::LineBounds bounds;

    style.edge_width = 1.5f;
    // Measured off the screen: a fade of 0 draws the lettering black, not unseen (it showed as a black copy in the middle).
    // (DC_NETPLAY_MUTATE=notice_measure_seen: measured at 320, as before: the test's twin.)
    const float measure = NetplayMutated("notice_measure_seen") ? 320.0f : -1000.0f;

    dc::text::caption::DrawLine(g_notice.text, measure, 14.0f, 12.0f, 1.0f, 600.0f, style, 0.0f, 1.0f, &bounds);
    float centre = 24.0f + (measure - bounds.left);
    dc::text::caption::DrawLine(g_notice.text, centre, 14.0f, 12.0f, 1.0f, 600.0f, style, fade, 1.0f, nullptr);

    if (!g_notice.drawn) {
        g_notice.drawn = true;
        std::fprintf(stderr, "netplay: notice drawn: %s\n", g_notice.text.c_str());
    }
}

const char *NetplayStatus(bool &desync) {
    desync = g_link.desync_tick >= 0;
    return g_link.status;
}

// coop-worlds: the copies go apart (the link's Leave first, so the other copy hears at once).
void NetplayGoApart(const char *why, int mover) {
    if (!g_link.up) {
        return;
    }
    g_apart_mover = mover;
    NetSessionTo(NetSession::Apart, why); // coop-session

    char text[128];

    // coop-leaverace: this copy's inputs the other copy may lack for the ticks up to the split (it plays on to it, and
    // this copy sends nothing more): every one given, twice, a packet's kResend at a time.
    if (!g_catch.active && !NetplayMutated("close_on_leave")) {
        for (int round = 0; round < 2; round++) {
            for (std::int64_t newest = g_link.newest; newest >= 0 && newest > g_link.tick - g_link.delay - kResend && g_link.newest - newest < kRing;
                 newest -= kResend) {
                Send(newest);
            }
        }

        // The host just after a catch-up join: the last blob (J, the joiner's way to the split) held first, for up to
        // kLeaveBlobWaitMs; on a link that lost none of it this returns at once.
        constexpr int kLeaveBlobWaitMs = 1000;
        const auto    began = std::chrono::steady_clock::now();

        while (SendsState() && !g_catch.out.empty() && g_catch.join_tick >= 0 && !CatchOutHeld() &&
               std::chrono::steady_clock::now() - began < std::chrono::milliseconds(kLeaveBlobWaitMs)) {
            CatchSend();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            Receive();
        }
    }

    SayLeaving(g_link.tick);
    std::snprintf(text, sizeof text, "went apart (%s)", why);
    Close(text);
}

bool NetplayPeerQuit() {
    return g_link.peer_left;
}

// coop-quittitle: netplay.hpp.
bool NetplaySessionOpen() {
    return g_link.started && Settings().netplay != 0;
}

bool NetplayTakeQuitToTitle() {
    if (!g_quit_title) {
        return false;
    }
    g_quit_title = false;

    const bool         was_up = g_link.up;
    const std::int64_t tick = g_quit_tick >= 0 ? g_quit_tick : g_link.tick;
    const int          player = g_link.player;

    g_quit_tick = -1;
    // The Leave first, as a closed game's (atexit) says it: the others close on it at once, "player N left" (the host's
    // carries kLeaveEnded, so a copy that missed the quit's tick still goes to the title).
    SayLeaving(-1, g_quit_ends && player == 1);
    SayGaveUp();
    // The link closed and the session over on this copy: nothing listens, no Hello, no recording or catch-up, the role
    // forgotten, as a copy that has just booted to its title. Its socket is kept (NetSocketOpen keeps it on the same port).
    g_link.up = false;
    g_keepalive_on = false;
    g_link.listening = false;
    g_link.dead = false;
    g_link.awaiting_sync = false;
    g_link.load_hold = false;
    g_link.state_due = false;
    g_link.resync_tick = -1;
    g_link.session = NewSession();
    g_catch = Catch{};
    g_solo = Solo{};
    g_snap = Snap{};
    g_command_queue.clear();
    g_went_down = false;
    if (!NetSessionTo(NetSession::Title, "Quit Game") && !NetSessionOff()) {
        std::fprintf(stderr, "netplay: quit: the session table refused the title (now %s)\n", NetSessionName(NetSessionNow()));
    }
    JoinGiveUp("ended by Quit Game", g_quit_note.c_str()); // (the title menu's reset: started, the join, the role, Steam, NAT)
    g_host_via = 0;
    g_handoffs.clear();
    MenuOnline(false); // (a plain copy that went online at its title goes back to single player, as it booted)
    coop::MenusAdoptEnd(); // coop-pt1111o: this copy's own menus settings again
    NetplayAdoptSession(false); // coop-pt1111p: and its own of the rest the host's had been
    CoopResetForSession();   // every co-op module's own state as a fresh session starts it
    CoopWorldsSessionEnded(); // the pair's apart and merge state gone
    std::fprintf(stderr, "netplay: quit: this copy (player %d) left the session at link tick %lld (%s, link was %s): link closed, back to the title\n",
                 player, (long long) tick, g_quit_ends ? "the host's quit: the session over for every player" : "its own player's quit: the others play on",
                 was_up ? "up" : "down");
    std::fflush(stderr);
    g_quit_ends = false;
    g_quit_at_title_due = true;
    return true;
}

// coop-localmenu: netplay_cmd.hpp. Linked, the command waits for this copy's next input frame (TakeQueuedCommand) and is
// applied on that frame's tick on both copies. Alone, it is applied at once as this copy's player and, while a catch-up
// records, kept for the joiner's replay. DC_NETPLAY_MUTATE=command_here_only (twin): applied at once on this copy alone, never
// sent, as a menu in the lockstep's place would have it.
// coop-menustick (Jep's 2026-11-04 pack 8 playtest: his long load back into her town, her Triangle menu open, and her stick
// moved the menu and walked her too): linked, but the sender of a catch-up still records its ticks (the lockstep has not
// started, its last blob not made), so no input frame takes a queued command till the lockstep: her menu's bit
// (coop_actor's kCmdMenu, what stands her walk) waited on it, and for his whole load. Those ticks are recorded, so the
// command goes as it does alone: applied at once and kept for his replay, which applies it on the same tick.
// DC_NETPLAY_MUTATE=catchup_command_queued (twin): queued for the lockstep, as before.
static bool SenderRecordsCatchup() {
    return g_link.up && g_catch.active && g_catch.join_tick < 0 && SendsState() && Catchup() && g_solo.on && !g_solo.replay &&
           !g_solo.checkpoint.empty() && !NetplayMutated("catchup_command_queued");
}

void NetplayQueueCommand(const NetCommand &command) {
    if (command.kind == kCmdNone) {
        return;
    }

    const int  player = g_link.player > 0 ? g_link.player : 1;
    const bool recorded = SenderRecordsCatchup();

    if (g_link.up && !recorded && !NetplayMutated("command_here_only")) {
        g_command_queue.push_back(command);
        return;
    }

    const std::int32_t delta[5] = {kWhatCommandDelta, player | command.kind << 8 | command.a << 16, (std::int32_t) command.b,
                                   (std::int32_t) command.c, 0};

    const std::size_t had = g_solo.deltas.size();

    if (!g_link.up || recorded) {
        NetplayCatchupDelta(delta);
    }
    // coop-stabfix1: recorded (alone or as a catch-up's sender), it waits for the start of its recorded tick, as the replay has it.
    if (g_solo.deltas.size() > had && !g_solo.replay && !NetplayMutated("recorded_command_at_once")) {
        g_recorded_commands.push_back({player, command, g_solo.deltas.back().tick});
        if (recorded) {
            std::fprintf(stderr, "catchup: command %d for player %d recorded at tick %lld, applied as that tick starts\n", (int) command.kind, player,
                         (long long) g_solo.deltas.back().tick);
        }
        return;
    }
    if (recorded) {
        std::fprintf(stderr, "catchup: command %d for player %d applied on the catch-up's recorded tick %lld\n", (int) command.kind, player,
                     (long long) g_link.tick);
    }
    ApplyCommand(player, command, g_link.tick);
}

void NetplaySetCommandHandler(std::uint8_t kind, NetCommandHandler handler) {
    g_command_handlers[kind] = handler;
}

void NetplayApplyRecordedCommand(const std::int32_t delta[5]) {
    NetCommand command;

    command.kind = (std::uint8_t) ((delta[1] >> 8) & 0xFF);
    command.a = (std::uint8_t) ((delta[1] >> 16) & 0xFF);
    command.b = (std::uint16_t) delta[2];
    command.c = (std::uint32_t) delta[3];
    ApplyCommand(delta[1] & 0xFF, command, g_link.tick);
}

void NetplayCatchupDelta(const std::int32_t delta[5]) {
    // The no_catchup_deltas twin: the recording leaves the change out (the old order), so the joiner's replay lacks it.
    if (!SendsState() || g_solo.replay || g_solo.checkpoint.empty() || !Catchup() || NetplayMutated("no_catchup_deltas")) {
        return;
    }

    Solo::Delta entry{g_solo.on ? g_link.tick : 0, {}};

    std::memcpy(entry.value, delta, sizeof entry.value);
    g_solo.deltas.push_back(entry);
    std::fprintf(stderr, "catchup: player 2's change from apart recorded at tick %lld\n", (long long) entry.tick);
    // coop-catchuprace test hook (DC_NETPLAY_TEST_CLOSE_ON_DELTA=1, the host): a change recorded after the first blob closes the
    // catch-up on its own tick, so it is recorded at J every run (train 53's in-sync red: her replay ended before J).
    if (TestEnvOn("DC_NETPLAY_TEST_CLOSE_ON_DELTA") && g_catch.out_id >= 1 && g_catch.join_tick < 0) {
        g_catch.test_close_at = entry.tick;
    }
}

void NetplayTestSeriesMark() {
    if (g_test_series_mark < 0) {
        g_test_series_mark = g_test_series_frame;
        std::fprintf(stderr, "coop-test: series marked at frame %ld\n", g_test_series_mark);
    }
}

// coop-localfix: a test hook's frame to write (the next frame drawn).
void NetplayTestShotTo(const char *path) {
    g_test_shot = path;
}

// coop-drawtwice (tests): the lockstep checksum now, for the draw-twice self-test (coop_camera.cpp).
std::uint32_t NetplayTestChecksum() {
    return TripChecksum();
}

// coop-lobbyslots: a controller's device as SDL names it, the same in every copy on this PC (pad numbers are each copy's
// own order). The pads' slots are numbered by their player index (input.cpp sets it to the slot), so pad's gamepad is the
// one with that index; its HID path, else its serial, else "" (the number is then all there is).
std::string NetplayPadDevice(int pad) {
    SDL_Gamepad *gamepad = pad >= 0 ? SDL_GetGamepadFromPlayerIndex(pad) : nullptr;
    const char  *path = gamepad != nullptr ? SDL_GetGamepadPath(gamepad) : nullptr;
    const char  *serial = gamepad != nullptr ? SDL_GetGamepadSerial(gamepad) : nullptr;

    return path != nullptr && path[0] != '\0' ? std::string(path) : serial != nullptr && serial[0] != '\0' ? std::string("serial:") + serial : std::string();
}

int NetplayPadOfDevice(const std::string &device) {
    for (int pad = 0; pad < kInputPadCount && !device.empty(); pad++) {
        if (NetplayPadDevice(pad) == device) {
            return pad;
        }
    }
    return -1;
}
