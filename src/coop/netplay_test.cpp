#include "coop/game_ns.hpp"
#include "coop/netplay_test.hpp"

#include <SDL3/SDL.h>
#include <libvu0.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>
#include <thread>
#include <vector>

#include "coop/coop.hpp"
#include "coop/coop_core.hpp"
#include "coop/coop_actor.hpp" // coop-soakfix5: DC_NETPLAY_PERTURB_MENUBITS
#include "coop/coop_duntest.hpp" // coop-dungeonsuite
#include "coop/input_anchors.hpp" // coop-gatekey52: hook events
#include "coop/netplay.hpp"
#include "coop/netplay_cmd.hpp" // coop-localmenu
#include "coop/netplay_hello.hpp"  // coop-joinfix: NetplayTestSettingFlip
#include "camera.hpp"
#include "character.hpp"
#include "dngstatusdata.hpp"
#include "dun/gameloop.hpp"
#include "dungeoneventdata.hpp"
#include "dungeoneventman.hpp"
#include "dungeonmap.hpp" // gaps2: PerturbFloor
#include "effectmacro.hpp"
#include "fireomni.hpp"
#include "itemdata.hpp" // coop-itemdot
#include "mglib.hpp"
#include "objanime.hpp"
#include "randomitem.hpp" // coop-itemdot
#include "savedata.hpp"
#include "textureanime.hpp"
#include "userstatus.hpp"

namespace dc::game {
extern s32 gameTask;
} // namespace dc::game

namespace {

// Whether a floor change the hooks started is under way (from the gate key's script to the floor
// after the gate), when NetplayTestPad answers its questions.
bool g_driving = false;
// The floor hooks' key script done, the gate next (FloorHooks).
bool g_to_gate = false;

// Test hook (DC_NETPLAY_FAKE_KEYS=FROM,TO): real SDL key-down events for D (the left stick's right) from link
// tick FROM and its key-up at TO, as a hand on a keyboard would, so the link's keyboard filter decides
// whether they reach the game (game.netplay_keyboard).
void PushFakeKeys(std::int64_t tick) {
    static const char *spec = TestEnv("DC_NETPLAY_FAKE_KEYS");

    if (spec == nullptr) {
        return;
    }

    static const long long from = std::atoll(spec);
    static const long long to = std::strchr(spec, ',') != nullptr ? std::atoll(std::strchr(spec, ',') + 1) : from + 600;

    if (tick < from || tick > to) {
        return;
    }

    // Down every tick in between, as a held key repeats (a hidden test window can lose the first one).
    SDL_Event key{};

    key.type = tick < to ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    key.key.scancode = SDL_SCANCODE_D;
    key.key.key = SDLK_D;
    key.key.down = tick < to;
    key.key.repeat = tick > from && tick < to;
    SDL_PushEvent(&key);

    if (tick == from || tick == to) {
        std::fprintf(stderr, "netplay: test: fake key D %s at tick %lld\n", tick == from ? "down" : "up", (long long) tick);
    }
}
// Test hook (DC_COOP_FAKE_MOUSE): from tick 700 to 1500 pushes real SDL mouse motion, wheel and
// key events every few ticks, as a hand on the mouse and keyboard would, to show what they do to a
// linked copy.
// coop-fx51d: DC_COOP_FAKE_MOUSE=load: the same 800 ticks, from 40 after a state file's load (net-suite-firstperson -Fixture).
void PushFakeInput(std::int64_t tick) {
    static const char        *spec = TestEnv("DC_COOP_FAKE_MOUSE");
    static const bool         fake = spec != nullptr;
    static const bool         from_load = fake && std::strcmp(spec, "load") == 0;
    static std::int64_t       start = from_load ? -1 : 700;

    if (from_load && start < 0) {
        if (NetplayStateLoadPending()) {
            return;
        }
        start = tick + 40;
        std::fprintf(stderr, "netplay: test: fake keyboard and mouse from tick %lld to %lld (after the load)\n", (long long) start, (long long) start + 800);
    }
    if (!fake || tick < start || tick > start + 800) {
        return;
    }

    SDL_Event event{};

    event.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.xrel = 25.0f;
    event.motion.yrel = 4.0f;

    static int accepted = 0;
    static int dropped = 0;

    (SDL_PushEvent(&event) ? accepted : dropped)++;

    if (tick % 40 == 0 || tick % 40 == 20) {
        SDL_Event key{};

        key.type = tick % 40 == 0 ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        key.key.scancode = SDL_SCANCODE_D;
        key.key.key = SDLK_D;
        key.key.down = tick % 40 == 0;
        (SDL_PushEvent(&key) ? accepted : dropped)++;
    }

    if (tick % 30 == 0) {
        SDL_Event wheel{};

        wheel.type = SDL_EVENT_MOUSE_WHEEL;
        wheel.wheel.y = 1.0f;
        (SDL_PushEvent(&wheel) ? accepted : dropped)++;
    }

    if (tick == start + 800) {
        std::fprintf(stderr, "netplay: fake keyboard and mouse events: %d reached the game, %d dropped\n", accepted, dropped);
    }
}

// Test hook (DC_NETPLAY_PERTURB=N, on one copy): at tick N this copy alone draws a random number and
// takes a point of the player.s life, as a desync would, to show the checksum catch it.
void Perturb(std::int64_t tick) {
    static const char *at = TestEnv("DC_NETPLAY_PERTURB");

    if (at == nullptr || tick != std::atoll(at)) {
        return;
    }

    rand();

    if (UserStatus != nullptr) {
        UserStatus->hp[UserStatus->cur_chara] -= 1;
    }

    std::fprintf(stderr, "netplay: test perturbation at tick %lld\n",(long long) tick);
}

// coop: gaps2
// Test hook (DC_NETPLAY_PERTURB_FLOOR=N, on one copy): at tick N this copy alone lays a trap circle in
// a free slot, far off the floor where nobody steps on it or sees it: a desync only the floor sum sees,
// to show the checksum catch it.
void PerturbFloor(std::int64_t tick) {
    static const char *at = TestEnv("DC_NETPLAY_PERTURB_FLOOR");

    if (at == nullptr || tick != std::atoll(at) || NowDngMap == nullptr) {
        return;
    }

    for (int i = 0; i < 3; i++) {
        MAP_TRAP_CIRCLE &circle = NowDngMap->trap_circle[i];

        if (circle.state == 0) {
            circle.pos[0] = circle.pos[2] = -10000.0f;
            circle.pos[1] = 0.0f;
            circle.pos[3] = 1.0f;
            circle.state = 1;
            circle.kind = 9;
            circle.timer = 0.0f;
            std::fprintf(stderr, "netplay: test floor perturbation at tick %lld (trap circle slot %d)\n", (long long) tick, i);
            return;
        }
    }

    std::fprintf(stderr, "netplay: test floor perturbation at tick %lld found no free trap circle slot\n", (long long) tick);
}

// The comma-separated ticks in test variable name.
std::vector<std::int64_t> TickList(const char *name) {
    std::vector<std::int64_t> ticks;
    const char               *list = TestEnv(name);

    while (list != nullptr && *list != '\0') {
        char *end = nullptr;

        ticks.push_back(std::strtoll(list, &end, 10));
        list = *end == ',' ? end + 1 : nullptr;
    }

    return ticks;
}

// Whether the next of a test variable's ticks is due: tick has reached it and the player is on the floor
// under control. Each tick in the list is used once.
bool TickDue(const std::vector<std::int64_t> &ticks, std::size_t &next, std::int64_t tick) {
    if (next >= ticks.size() || tick < ticks[next] || gameTask != GAME_TASK_PLAY || UserStatus == nullptr) {
        return false;
    }

    next++;
    return true;
}

// coop-gatekey52 (Jep, 2026-11-04: no fixed tick waits): DC_NETPLAY_NEXT_FLOOR's and DC_NETPLAY_LEAVE's entries, each a
// tick, or "loaded" (once a state file is put back: a fixture's moment, the same tick on both linked copies), or, as the
// last entry, "@REGEX[ >> REGEX...]" (the rest of the value, commas and all): once this copy's own log has shown them in
// order (CoopLogEventSeen; an apart copy's, whose log no lockstep holds). Each is used once, the player on the floor under
// control (TickDue). coop-fx51c: an "@" entry ends at a ",@" (the next entry an event too: w-world-floors -P1Paused, the first
// gate on his pause, the second on the first gate's own line).
struct HookEntry {
    std::int64_t tick = 0;
    bool         loaded = false;
    int          event = -1;
};

std::vector<HookEntry> HookList(const char *name) {
    std::vector<HookEntry> entries;
    const char            *list = TestEnv(name);

    while (list != nullptr && *list != '\0') {
        HookEntry entry;

        if (*list == '@') {
            const char *next = std::strstr(list, ",@");

            entry.event = CoopLogEventAdd(next != nullptr ? std::string_view(list + 1, (std::size_t) (next - list - 1)) : std::string_view(list + 1));
            entries.push_back(entry);
            if (next == nullptr) {
                break;
            }
            list = next + 1;
            continue;
        }

        const char *end = std::strchr(list, ',');

        if (std::strncmp(list, "loaded", 6) == 0) {
            entry.loaded = true;
        } else {
            entry.tick = std::strtoll(list, nullptr, 10);
        }
        entries.push_back(entry);
        list = end != nullptr ? end + 1 : nullptr;
    }

    return entries;
}

// late (DC_NETPLAY_MUTATE=floor_tick's twin on a load's or an event's entry, which has no tick to take early): due a tick after
// it came, so the copies part as the tick entry's twin parts them.
bool HookDue(const char *name, const std::vector<HookEntry> &entries, std::size_t &next, std::int64_t tick, bool late = false) {
    if (next >= entries.size() || gameTask != GAME_TASK_PLAY || UserStatus == nullptr) {
        return false;
    }

    const HookEntry &entry = entries[next];
    const bool       due = entry.event >= 0 ? CoopLogEventSeen(entry.event) : entry.loaded ? NetplayStateFileLoaded() : tick >= entry.tick;

    if (!due) {
        return false;
    }

    static std::int64_t came = -1;

    if (late && (entry.event >= 0 || entry.loaded)) {
        if (came < 0) {
            came = tick;
            return false;
        }
        if (tick <= came) {
            return false;
        }
        came = -1;
    }

    if (entry.event >= 0 || entry.loaded) {
        std::fprintf(stderr, "netplay: test: %s's %s came at tick %lld\n", name, entry.loaded ? "load" : "event", (long long) tick);
    }
    next++;
    return true;
}

// The floor's event whose name starts with prefix ("out" the exit gate, "gatekey" its key), or null.
CDungeonEventData *FindEvent(const char *prefix) {
    if (NowEventMan == nullptr) {
        return nullptr;
    }

    for (CDungeonEventData &data : NowEventMan->event) {
        if (data.event != nullptr && data.enabled && std::strncmp(data.event->name, prefix, std::strlen(prefix)) == 0) {
            return &data;
        }
    }

    return nullptr;
}

// Starts an event's script as the player's Cross at it does (DunMoveChara's GAME_TASK_PLAY).
void RunEvent(CDungeonEventData &data) {
    BtEventInfo.script_no = data.event->script_no;
    BtEventInfo.ext_memory = data.event->ext_mem;
    BtEventInfo.event_marker = true;
    BtEventInfo.action_mode = 1;
    sceVu0CopyVector(BtEventInfo.position, data.pos);
    sceVu0CopyVector(BtEventInfo.direction, data.dir);
    gameTask = GAME_TASK_SCRIPT_START;
}

// Test hooks, set alike on both copies. From each tick DC_NETPLAY_NEXT_FLOOR lists, once the player is
// on the floor under control, the party takes the floor's key and then its exit gate, each script run
// as the player's Cross at it runs it (the script input answers its questions). From each tick
// DC_NETPLAY_LEAVE lists the party leaves for the town as a knockout does. At each tick
// DC_NETPLAY_LIST_EVENTS lists, the floor's events are logged.
void FloorHooks(std::int64_t tick) {
    static const std::vector<std::int64_t> lists = TickList("DC_NETPLAY_LIST_EVENTS");
    static const std::vector<HookEntry>    floors = HookList("DC_NETPLAY_NEXT_FLOOR"); // coop-gatekey52
    static const std::vector<HookEntry>    leaves = HookList("DC_NETPLAY_LEAVE");
    static std::size_t                     next_list = 0;
    static std::size_t                     next_floor = 0;
    static std::size_t                     next_leave = 0;

    if (TickDue(lists, next_list, tick) && NowEventMan != nullptr) {
        for (const CDungeonEventData &data : NowEventMan->event) {
            if (data.event != nullptr && data.enabled) {
                std::fprintf(stderr, "netplay: event %s script %d key %d chara %d at (%.0f %.0f %.0f)\n", data.event->name,
                             data.event->script_no, data.event->key_id, data.event->chara_no, data.pos[0], data.pos[1], data.pos[2]);
            }
        }
    }

    // The gate's script done: on the next floor, under control.
    if (g_driving && !g_to_gate && gameTask == GAME_TASK_PLAY) {
        g_driving = false;
    }

    // The key's script done and the party back on the floor: the gate.
    if (g_to_gate && gameTask == GAME_TASK_PLAY) {
        g_to_gate = false;

        if (CDungeonEventData *gate = FindEvent("out")) {
            g_driving = true;
            std::fprintf(stderr, "netplay: test: exit gate (script %d) at tick %lld\n", gate->event->script_no, (long long) tick);
            RunEvent(*gate);
        }

        return;
    }

    if (HookDue("DC_NETPLAY_NEXT_FLOOR", floors, next_floor, NetplayMutated("floor_tick") ? tick - 1 : tick, NetplayMutated("floor_tick"))) {
        CDungeonEventData *key = FindEvent("gatekey");

        std::fprintf(stderr, "netplay: test: leaving floor %d at tick %lld\n", UserStatus->cur_floor, (long long) tick);
        g_to_gate = true;
        g_driving = true;

        if (key != nullptr) {
            std::fprintf(stderr, "netplay: test: gate key (script %d) at tick %lld\n", key->event->script_no, (long long) tick);
            RunEvent(*key);
        }
    } else if (HookDue("DC_NETPLAY_LEAVE", leaves, next_leave, tick)) {
        std::fprintf(stderr, "netplay: test: leaving the dungeon at tick %lld\n", (long long) tick);
        gameTask = GAME_TASK_EXIT_FADE_START;
    }
}

// From each tick DC_NETPLAY_RUN_EVENTS lists, once the player is on the floor under control, the
// next of the floor's events (in slot order, past the exit gate and its key) runs as the player's
// Cross at it runs it: the ura passage, doors, chests' scenes, whatever the floor has.
void EventHooks(std::int64_t tick) {
    static const std::vector<std::int64_t> events = TickList("DC_NETPLAY_RUN_EVENTS");
    static std::size_t                     next_event = 0;
    static int                             next_slot = 0;

    if (!TickDue(events, next_event, tick) || NowEventMan == nullptr) {
        return;
    }

    if (NetplayMutated("event_slot")) {
        next_slot = (next_slot + 1) % 96;
    }

    for (int tried = 0; tried < 96; tried++) {
        CDungeonEventData &data = NowEventMan->event[next_slot];

        next_slot = (next_slot + 1) % 96;

        if (data.event == nullptr || !data.enabled || data.event->script_no < 0 || std::strncmp(data.event->name, "out", 3) == 0 ||
            std::strncmp(data.event->name, "gatekey", 7) == 0) {
            continue;
        }

        std::fprintf(stderr, "netplay: test: event %s (script %d) at tick %lld\n", data.event->name, data.event->script_no,
                     (long long) tick);
        RunEvent(data);
        return;
    }
}

} // namespace

bool NetplayMutated(const char *name) {
    COOP_TEST_SWITCH(const char *, mutation, TestEnv("DC_NETPLAY_MUTATE")); // (a job's own list on a warm pair)

    if (mutation == nullptr) {
        return false;
    }

    // One name, or several separated by commas (a twin that needs one mutation to cause a fault and another to break
    // what reports it).
    std::size_t length = std::strlen(name);

    for (const char *at = mutation; *at != '\0';) {
        const char *end = std::strchr(at, ',');
        std::size_t size = end != nullptr ? (std::size_t) (end - at) : std::strlen(at);

        if (size == length && std::strncmp(at, name, length) == 0) {
            return true;
        }

        at += size + (end != nullptr ? 1 : 0);
    }

    return false;
}

void NetplayTestPad(std::int64_t tick, InputPadState &pad) {
    if (g_driving) {
        pad = InputPadState{};
        pad.connected = true;
        pad.buttons = tick % 20 < 4 ? kInputCross : 0;
    }
}

// DC_NETPLAY_STALL=tick:ms: this copy stops for ms at that tick, as a slow load stops it (the link's
// keepalive must keep the other copy waiting). coop-fx51b (no fixed tick waits): =synced:ms, on the first tick after this
// copy's first in-sync compare (net-stall on a fixture: the pair compared alike after the load, then the stall).
bool g_test_synced_once = false;

void Stall(std::int64_t tick) {
    static const char *at = TestEnv("DC_NETPLAY_STALL");
    static const bool  on_sync = at != nullptr && std::strncmp(at, "synced:", 7) == 0;
    static bool        done = false;

    if (at == nullptr || std::strchr(at, ':') == nullptr || (on_sync ? (done || !g_test_synced_once) : tick != std::atoll(at))) {
        return;
    }
    done = true;

    int ms = std::atoi(std::strchr(at, ':') + 1);

    std::fprintf(stderr, "netplay: test stall of %d ms at tick %lld\n", ms, (long long) tick);
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

// DC_NETPLAY_PERTURB_SAVE=tick / DC_NETPLAY_PERTURB_SHOT=tick: on this copy alone, a byte the game never reads
// (the SaveData's last, unused) or a shot not in the air (pad 2's last) changes, which only the checksum's
// SaveData or shot sums can see.
void PerturbUnseen(std::int64_t tick) {
    static const char *save = TestEnv("DC_NETPLAY_PERTURB_SAVE");
    static const char *shot = TestEnv("DC_NETPLAY_PERTURB_SHOT");

    if (save != nullptr && tick == std::atoll(save) && SaveData != nullptr) {
        ((unsigned char *) SaveData)[sizeof(CSaveData) - 1] ^= 0x5A;
        std::fprintf(stderr, "netplay: test: SaveData's last byte changed at tick %lld\n", (long long) tick);
    }

    coop::Shot &last = coop::g_partner.shots[coop::kShots - 1];

    if (shot != nullptr && tick == std::atoll(shot) && last.life == 0) {
        last.pos[0] += 1.0f;
        std::fprintf(stderr, "netplay: test: pad 2's last shot moved at tick %lld\n", (long long) tick);
    }
}

namespace dc::game {
void PortBuildMapGuardsFill(unsigned char value);
} // namespace dc::game

// DC_NETPLAY_PERTURB_GUARD=tick: on that tick this copy's floor-builder guards fill with junk, as a long session
// might leave them (port/src/dungeonmap.cpp); a resync after it must clear them.
void PerturbGuard(std::int64_t tick) {
    static const char *at = TestEnv("DC_NETPLAY_PERTURB_GUARD");

    if (at != nullptr && tick == std::atoll(at)) {
        PortBuildMapGuardsFill(0x5A);
        std::fprintf(stderr, "netplay: test: floor-builder guards filled at tick %lld\n", (long long) tick);
    }
}

void PortWeaponFxSlotSet(int slot);

// DC_NETPLAY_PERTURB_WEAPONFX=tick: on that tick this copy's next weapon element slot moves to the last one, as a copy
// that has fought a while leaves it; a resync after it must start it over.
void PerturbWeaponFx(std::int64_t tick) {
    static const char *at = TestEnv("DC_NETPLAY_PERTURB_WEAPONFX");

    if (at != nullptr && tick == std::atoll(at)) {
        PortWeaponFxSlotSet(3);
        std::fprintf(stderr, "netplay: test: weapon element slot moved at tick %lld\n", (long long) tick);
    }
}

// DC_NETPLAY_PERTURB_STOPS=1 / DC_NETPLAY_PERTURB_VIEW=1: as this copy takes a resync's state, it holds what a copy that
// has played elsewhere can bring: the town's stop flags set (object, character and texture animations, camera), or a
// last view far from anything. The state must replace both with the host's (own_stops / own_view break it).
namespace dc::game {
extern CFireOmni Fire; // objanime.cpp: the editor's shared fire
extern bool      all_stop; // objanime.cpp
extern int       effect_count; // effectmacro.cpp
extern int       EdMoveCharaRotCount; // coop-soakfix5: the town walk's camera swing (ebattle.cpp EdMoveChara)
} // namespace dc::game

void NetplayTestResyncLeftovers() {
    static const char *stops = TestEnv("DC_NETPLAY_PERTURB_STOPS");
    static const char *view = TestEnv("DC_NETPLAY_PERTURB_VIEW");
    static const char *effects = TestEnv("DC_NETPLAY_PERTURB_EFFECTCOUNT");
    static const char *fire = TestEnv("DC_NETPLAY_PERTURB_FIRE");

    // DC_NETPLAY_PERTURB_EFFECTCOUNT=1: the towns' effect clock three frames on from the host's start (keep_effect_count).
    if (effects != nullptr) {
        effect_count = 3;
        std::fprintf(stderr, "netplay: test: effect clock moved before the host's state\n");
    }

    // coop-soakfix5 (the soak's leave seeds, test_soak_reds.py): what a rejoining copy's ticks before its resync left on it alone,
    // planted on that copy as it takes the host's state, so the row's moment does not hang on where the seed's pads fall.
    // DC_NETPLAY_PERTURB_TOWNSWING=1: the town walk's camera swing behind Toan running, 30 ticks (his button 0x20; keep_town_swing).
    // DC_NETPLAY_PERTURB_MENUBITS=1: player 1's town menu bits set, town and local (his menu opened there; resync_menubits_kept).
    static const char *swing = TestEnv("DC_NETPLAY_PERTURB_TOWNSWING");
    static const char *menubits = TestEnv("DC_NETPLAY_PERTURB_MENUBITS");

    if (swing != nullptr) {
        EdMoveCharaRotCount = 30;
        std::fprintf(stderr, "netplay: test: the town walk's camera swing set running before the host's state\n");
    }
    if (menubits != nullptr) {
        coop::ActorMenuOpenHere(1, coop::kActorMenuTown | coop::kActorMenuLocal);
        std::fprintf(stderr, "netplay: test: player 1's town menu bits set before the host's state\n");
    }

    // DC_NETPLAY_PERTURB_FIRE=<n>: the towns' shared fire n draws into its flicker (0-4), as a copy that drew other
    // fires before has it; the test gives each copy a different n (townjoin~draw_rng_shared).
    if (fire != nullptr) {
        Fire.flicker_count = std::atoi(fire) % 5;
        std::fprintf(stderr, "netplay: test: fire flicker set to %d before the host's state\n", Fire.flicker_count);
    }

    if (stops != nullptr) {
        all_stop = 1;
        CCharacter::MotionStopFlag = 1;
        CTextureAnime::stop_anime = 1;
        CCamera::StopCamera = 1;
        std::fprintf(stderr, "netplay: test: stop flags set before the host's state\n");
    }

    if (view != nullptr) {
        // A view looking straight down from far above, at the world's edge: nothing a town has is in it.
        sceVu0FMATRIX matrix;

        sceVu0UnitMatrix(matrix);
        matrix[1][1] = 0.0f;
        matrix[1][2] = -1.0f;
        matrix[2][1] = 1.0f;
        matrix[2][2] = 0.0f;
        matrix[3][0] = 100000.0f;
        matrix[3][1] = -100000.0f;
        matrix[3][2] = 100000.0f;
        MGSetViewMatrix(matrix, matrix[3]);
        std::fprintf(stderr, "netplay: test: view moved away before the host's state\n");
    }
}

// DC_NETPLAY_PERTURB_FLOORSLOTS=tick: on that tick this copy fills the floor's free chest and trap-circle slots with
// junk, as a floor's start leaves the last floor's leftovers in them; the floor checksum must not see it (the
// stale_floor_sum twin, which hashes free slots too, then fails every time).
// coop-fx51b (no fixed tick waits): =lockstep: once this copy's catch-up reached its lockstep and a compare after it was in
// sync (so the join's own in-sync check comes before the junk), on the next link tick.
std::int64_t g_test_lockstep_tick = -1;
bool         g_test_synced_after_lockstep = false;

void NetplayTestLockstep(std::int64_t tick) {
    g_test_lockstep_tick = tick;
    g_test_synced_after_lockstep = false;
}

void NetplayTestInSync(std::int64_t tick) {
    g_test_synced_once = true; // (DC_NETPLAY_STALL=synced)
    if (g_test_lockstep_tick >= 0 && tick > g_test_lockstep_tick) {
        g_test_synced_after_lockstep = true;
    }
}

void PerturbFloorSlots(std::int64_t tick) {
    static const char *at = TestEnv("DC_NETPLAY_PERTURB_FLOORSLOTS");
    static const bool  on_lockstep = at != nullptr && std::strcmp(at, "lockstep") == 0;
    static bool        done = false;

    if (at == nullptr || NowDngMap == nullptr) {
        return;
    }
    if (on_lockstep ? (done || !g_test_synced_after_lockstep) : tick != std::atoll(at)) {
        return;
    }
    done = true;

    int boxes = 0;
    int circles = 0;

    for (TREASURE_BOX &box : NowDngMap->boxes) {
        if (box.used == 0) {
            box.closed = 1;
            box.item_no = 259;
            box.kind = 1;
            box.trap_no = 3;
            boxes++;
        }
    }

    for (MAP_TRAP_CIRCLE &circle : NowDngMap->trap_circle) {
        if (circle.state == 0) {
            circle.kind = 2;
            circles++;
        }
    }

    std::fprintf(stderr, "netplay: test: junk in %d free chest and %d free circle slots at tick %lld\n", boxes, circles, (long long) tick);
}
// coop-itemdot (DC_COOP_MAPITEMS_DROP=K, both copies): at link tick K, on a dungeon floor, a Bread is left lying 200 units
// east of player 1, as a monster's drop is (CRandomItem::Set), so the map has a dropped item to mark away from his arrow.
void MapItemDrop(std::int64_t tick) {
    static const std::int64_t at = TestEnvInt("DC_COOP_MAPITEMS_DROP", -1);

    if (tick == at && RandomItem != nullptr && NowDngMap != nullptr) {
        sceVu0FVECTOR spot;
        sceVu0CopyVector(spot, CharaMain.pos);
        spot[0] += 200.0f;
        RandomItem->Set(spot, 101, -1, ITEM_BREAD);
        std::fprintf(stderr, "netplay: test: a Bread dropped at (%.0f %.0f %.0f) at tick %lld\n", spot[0], spot[1], spot[2], (long long) tick);
    }
}

// DC_NETPLAY_TEST_POISON_AT=K (both copies): at link tick K, on a dungeon floor, the player is poisoned, as a bat's bite does.
void PoisonAt(std::int64_t tick) {
    static const std::int64_t at = TestEnvInt("DC_NETPLAY_TEST_POISON_AT", -1);

    if (tick == at && UserStatus != nullptr) {
        UserStatus->ailments[UserStatus->cur_chara] |= AILMENT_POISON;
        std::fprintf(stderr, "netplay: test: the player poisoned at tick %lld\n", (long long) tick);
    }
}

// coop-localmenu test hook (DC_NETPLAY_TEST_COMMAND=P:K[,P:K...], both copies): at link tick K, player P's copy queues a test
// command (a = P, c = K); every copy logs it as it applies it ("netplay: command 1 ... from player P applied at tick T").
void TestCommands(std::int64_t tick) {
    static const char *const spec = TestEnv("DC_NETPLAY_TEST_COMMAND");

    if (spec == nullptr) {
        return;
    }

    for (const char *at = spec; *at != '\0';) {
        int       player = 0;
        long long when = -1;

        if (std::sscanf(at, "%d:%lld", &player, &when) == 2 && when == tick && player == NetplayLocalPlayer()) {
            NetCommand command;

            command.kind = kCmdTest;
            command.a = (std::uint8_t) player;
            command.c = (std::uint32_t) tick;
            std::fprintf(stderr, "netplay: test: player %d queues a test command at tick %lld\n", player, (long long) tick);
            NetplayQueueCommand(command);
        }
        at += std::strcspn(at, ",");
        at += *at == ',' ? 1 : 0;
    }
}

void NetplayTestTick(std::int64_t tick) {
    TestCommands(tick);
    PoisonAt(tick);
    MapItemDrop(tick); // coop-itemdot
    PerturbFloorSlots(tick);
    PerturbGuard(tick);
    PerturbWeaponFx(tick);
    PushFakeInput(tick);
    PushFakeKeys(tick);
    Perturb(tick);
    NetplayTestSettingFlip(tick); // coop-joinfix
    PerturbFloor(tick); // coop: gaps2
    PerturbUnseen(tick);
    Stall(tick);
    FloorHooks(tick);
    EventHooks(tick);
    coop::DunTestTick(tick); // coop-dungeonsuite: the suite's driver (DC_COOP_DUNTEST)
}

bool NetplayTestDrivingGate() {
    return g_driving;
}

void NetplayTestFloorApart(std::int64_t tick) {
    FloorHooks(tick);
    coop::DunTestTick(tick); // coop-dungeonsuite
}

void NetplayTestFloorMerged() {
    if (g_driving || g_to_gate) {
        std::fprintf(stderr, "netplay: test: the floor hooks stop driving at the merge\n");
    }

    g_driving = false;
    g_to_gate = false;
}

// coop-pilot (the test redesign's unit layer, Jep 2026-11-09): DC_UNIT=checksum runs the checksum's cases in one copy, no
// link and no second copy: once the copy plays a floor (after its fixture's load, DC_STATE_LOAD_AT), each case changes
// one declared part of the state, takes the lockstep checksum, puts the part back and takes it again. A case passes when
// the change moves the sum and the put-back returns it; the copy then leaves with 0 (all passed) or 3. One line a case
// ("unit: checksum <case>: PASS|FAIL ..."), then "unit: checksum done: N passed, M failed". It replaces g-floor-sum's
// pair rows (a trap circle, the SaveData's unused byte, pad 2's resting shot) and adds the chest, map event, random
// state and life parts. Its twins are the checksum's own mutations (DC_NETPLAY_MUTATE=no_floor_sum, no_save_sum,
// no_shot_sum): each fails its own case.
namespace dc::game {
unsigned int PortRandState();            // port/src/runtime.cpp
void         PortRandSetState(unsigned int state);
} // namespace dc::game

void NetplayUnitTick() {
    static const char *const unit = TestEnv("DC_UNIT");
    static bool              done = false;

    if (done || unit == nullptr || std::strcmp(unit, "checksum") != 0 || gameTask != GAME_TASK_PLAY || NowDngMap == nullptr || BtAllDrawFlag == 0 ||
        SaveData == nullptr || UserStatus == nullptr || (TestEnv("DC_STATE_LOAD_AT") != nullptr && !NetplayStateFileLoaded())) {
        return;
    }

    done = true;
    int passed = 0, failed = 0;

    // change: alters the part and returns a put-back; an empty name skips (the part has nothing to change here).
    auto run = [&](const char *name, auto change) {
        const std::uint32_t before = NetplayTestChecksum();
        auto                undo = change();
        const std::uint32_t changed = NetplayTestChecksum();
        undo();
        const std::uint32_t after = NetplayTestChecksum();
        const bool          ok = changed != before && after == before;

        (ok ? passed : failed)++;
        std::fprintf(stderr, "unit: checksum %s: %s (sum %08x, changed %08x, put back %08x)\n", name, ok ? "PASS" : "FAIL", before, changed, after);
    };

    run("rand", [] {
        const auto state = PortRandState();
        rand();
        return [state] { PortRandSetState(state); };
    });
    run("life", [] {
        auto &hp = UserStatus->hp[UserStatus->cur_chara];
        hp -= 1;
        return [&hp] { hp += 1; };
    });
    run("floor-circle", [] {
        MAP_TRAP_CIRCLE &circle = NowDngMap->trap_circle[0];
        const MAP_TRAP_CIRCLE kept = circle;
        circle.state = circle.state == 0 ? 1 : 0;
        circle.kind = 9;
        return [&circle, kept] { circle = kept; };
    });
    run("floor-chest", [] {
        TREASURE_BOX &box = NowDngMap->boxes[0];
        const TREASURE_BOX kept = box;
        box.used = 1;
        box.closed ^= 1;
        return [&box, kept] { box = kept; };
    });
    run("floor-event", [] {
        DUNGEON_EVENT &event = NowDngMap->events[0];
        const auto kept = event.kind;
        event.kind ^= 1;
        return [&event, kept] { event.kind = kept; };
    });
    run("save", [] {
        unsigned char &last = ((unsigned char *) SaveData)[sizeof(CSaveData) - 1];
        last ^= 0x5A;
        return [&last] { last ^= 0x5A; };
    });
    run("shot", [] {
        coop::Shot &shot = coop::g_partner.shots[coop::kShots - 1];
        shot.pos[0] += 1.0f;
        return [&shot] { shot.pos[0] -= 1.0f; };
    });

    std::fprintf(stderr, "unit: checksum done: %d passed, %d failed\n", passed, failed);
    std::fflush(nullptr);
    std::_Exit(failed == 0 ? 0 : 3);
}


// coop-pilot: under no draw each kept list's texture and mesh uploads are applied as it is recorded (gameloop.cpp). The twin,
// DC_QA_DRAW_MUTATE=skip_nodraw_uploads, leaves them out, as before e7bd56ea: a texture filled while recording never reaches
// the GPU and a capture draws without it (i-keys-tab-p1~nodrawuploads FAILs on its key icon).
bool NetplayTestNoDrawUploads() {
    static const char *m = std::getenv("DC_QA_DRAW_MUTATE");
    static const bool  skip = m != nullptr && std::strstr(m, "skip_nodraw_uploads") != nullptr;
    return !skip;
}

bool NetplayTestNoDraw() {
    static const bool on = TestEnvOn("DC_QA_NO_DRAW");
    static bool       said = false;

    if (on && !said) {
        said = true;
        std::fprintf(stderr, "netplay: test: no draw (DC_QA_NO_DRAW): ticks are not rendered; a capture renders its tick\n");
    }
    return on;
}
