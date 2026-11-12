#include "coop/coop_zonechara.hpp" // coop-pt1111k
#include "coop/game_ns.hpp"
#include "coop/coop.hpp"
#include "coop/coop_perf.hpp"
#include "coop/netplay.hpp"

#include <SDL3/SDL.h>

#include "render/texture_registry.hpp" // coop-floorjoin2: the debug line's texture misses (after SDL: libgraph.h's macros)
#include <libvu0.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>
#include <cmath>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <chrono>
#include <thread>
#include <cstdlib>
#include <cstring>
#include <new>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

#include "btactstatus.hpp"
#include "text/caption_text.hpp"
#include "rect.hpp"
#include "shot_freefuncs.hpp"
#include "snd.hpp"
#include "btitem.hpp"
#include "camerafollow.hpp"
#include "character.hpp"
#include "cloth.hpp"
#include "clothread.hpp"
#include "collision.hpp"
#include "collisiondata.hpp"
#include "dataalloc.hpp"
#include "dataread.hpp"
#include "dataset.hpp" // WorkBuffer (WorkBuffer__2 at 6ec3b808 was its alias)
#include "dranmapfield.hpp"
#include "dun/gameloop.hpp"
#include "dngmessageman.hpp"
#include "dungeoneventdata.hpp"
#include "dungeoneventman.hpp"
#include "dungeonmap.hpp"
#include "sysmes.hpp"
#include "frame.hpp"
#include "frameattr.hpp"
#include "gamepad.hpp"
#include "gameutil.hpp"
#include "hitvalue.hpp"
#include "itemdata.hpp"
#include "menu_draw.hpp"
#include "mglib.hpp"
#include "monstorunit.hpp"
#include "input/mouse_collision.hpp"
#include "shot_utils.hpp"
#include "platform/config.hpp"
#include "platform/input.hpp"
#include "texture.hpp"
#include "textureanime.hpp"
#include "userstatus.hpp"

#include "coop/coop_core.hpp"
#include "savedata.hpp" // coop-pt1109a: the floor hour hook
#include "editloop.hpp"
#include "coop/coop_actor.hpp" // coop-players
#include "coop/coop_audio.hpp" // coop-soundpt
#include "coop/coop_actor.hpp"   // coop-actorswap
#include "coop/coop_townmenu.hpp"
#include "coop/coop_fullmenu.hpp"
#include "coop/coop_camera.hpp"
#include "coop/coop_camtest.hpp"
#include "coop/coop_combat.hpp"
#include "coop/coop_interact.hpp"
#include "coop/coop_inventory.hpp" // coop-inventory
#include "coop/coop_menu.hpp"      // coop-menus
#include "coop/coop_party.hpp"
#include "coop/coop_wheel.hpp"
#include "coop/coop_prompt.hpp"
#include "coop/coop_town.hpp"
#include "coop/coop_georama.hpp"
#include "coop/coop_worlds.hpp" // coop-worlds

namespace dc::game {
int keyCtrl(float x, float y, MOTION_INFO *motion);

extern CFrameVu1 *CharaFrame;
extern int        statusAlarmRate;
extern s32        gameTask;
extern s32        iventMarker;
} // namespace dc::game

// coop-harness3 (Jep, 2026-10-31; harness-review-2 N2): DC_QA_ONE_STREAM=1, which the harness's Start-Copy sets on every
// test copy and no player ever does, makes a copy's log one file in write order: stdout unbuffered (a pipe is block
// buffered, so stdout lines reached the harness's .out file in 4 KB blocks, after stderr lines written later) and
// stderr sent to stdout's handle. Two files could never keep cross-stream order (the mergeview false red). Done once,
// on the first TestEnv call, which is before the first test line is printed.
static void TestLogStreamsOnce() {
    static const bool once = [] {
        const char *one = std::getenv("DC_QA_ONE_STREAM");
        if (one != nullptr && *one != '\0' && *one != '0') {
            std::fflush(stdout);
            std::fflush(stderr);
            std::setvbuf(stdout, nullptr, _IONBF, 0);
#ifdef _WIN32
            _dup2(_fileno(stdout), _fileno(stderr));
#else
            dup2(fileno(stdout), fileno(stderr));
#endif
        }
        return true;
    }();
    (void) once;
}
// At load, before main: the port's config loader prints on stderr before any TestEnv call.
static const bool g_test_log_streams_at_load = (TestLogStreamsOnce(), true);
#ifdef DCO_QA_STAMP
// WS5 (a test's, DC_QA_LOG_COST_MS=N): each line the game thread writes on stdout or stderr costs it N ms more, as a write
// does on a PC whose CPUs are pinned (l-local-play-houseout-slowreplay's red: his replay's pad blobs' log lines, some 10 ms
// each beside a full PC, held the game thread in its first Receive for seconds). Taken here, before main, on the game thread.
static const std::thread::id g_log_cost_thread = std::this_thread::get_id();
#endif

namespace {
std::vector<std::pair<std::string, std::string>> g_job_env; // coop-serve: the running job's switches
int                                              g_job_generation = 0;
}

void TestEnvSetJob(const char *name, const char *value) {
    for (auto &kv : g_job_env) {
        if (kv.first == name) {
            kv.second = value;
            ++g_job_generation;
            return;
        }
    }
    g_job_env.emplace_back(name, value);
    ++g_job_generation;
}

void TestEnvClearJob() {
    g_job_env.clear();
    ++g_job_generation;
}

void TestJobBegin() {
    ++g_job_generation;
}

bool TestJobResetKept(const char *mutation) {
    const char *list = std::getenv("DC_COOP_MUTATE"); // (the process's own: a pair's twin, its signature)
    return list != nullptr && std::strstr(list, mutation) != nullptr;
}

int TestJobGeneration() {
    return g_job_generation;
}

namespace {
std::atomic<long long> g_log_frame{0};
}

void TestLogFrame(long long frame) {
    g_log_frame.store(frame, std::memory_order_relaxed);
}

// The dev build links with --wrap=__stdio_common_vfprintf (every printf and fprintf of the UCRT goes through it): a fixture
// test copy's lines on stdout and stderr start with its frame. Off (any other copy, a player's), it passes straight through.
// (DCO_QA_STAMP: the dev build's alone; the published game links no wrap, so it has none of this.)
#ifdef DCO_QA_STAMP
namespace dc::game {
extern "C" int __real___stdio_common_vfprintf(unsigned long long options, FILE *stream, const char *format, _locale_t locale, va_list args);
extern "C" int __stdio_common_vsprintf(unsigned long long options, char *buffer, size_t count, const char *format, _locale_t locale, va_list args);
} // namespace dc::game
// WS5 (a test copy's alone: DC_QA_ONE_STREAM, one file): QaLogHold(true) keeps its stamped lines here, in write order, and
// QaLogHold(false) writes them as one (l-local-play-houseout's 250-290 ms on her screen: his lockstep's dozen lines, some 10 ms
// each beside a full PC, held his first ticks on the game thread, and she waits on his pads).
static std::atomic<bool> g_log_held{false};
static std::string       g_log_held_text;
static std::mutex        g_log_held_lock;

void QaLogHold(bool hold) {
    static const bool one_stream = [] {
        const char *v = std::getenv("DC_QA_ONE_STREAM");
        return v != nullptr && *v != '\0' && *v != '0';
    }();

    if (!one_stream) {
        return;
    }
    static const bool at_exit = (std::atexit([] { QaLogHold(false); }), true); // (a copy stopping in the hold keeps its lines)
    (void) at_exit;

    std::lock_guard<std::mutex> guard(g_log_held_lock);

    g_log_held = hold;
    if (!hold && !g_log_held_text.empty()) {
        std::fwrite(g_log_held_text.data(), 1, g_log_held_text.size(), stdout);
        g_log_held_text.clear();
    }
}

// coop-small1b: the game thread's time in its log lines' writes (on stdout or stderr, the format included), and their
// count, since the last QaLogCost: the long-frame line says it beside the netplay tick's parts.
static double    g_log_cost_ms = 0.0;
static long long g_log_cost_lines = 0;
static double    g_log_total_ms = 0.0; // (and since the start: QaLogTotals)
static long long g_log_total_lines = 0;

void QaLogTotals(double *ms, long long *lines) {
    *ms = g_log_total_ms;
    *lines = g_log_total_lines;
}

double QaLogCost(long long *lines) {
    const double ms = g_log_cost_ms;

    *lines = g_log_cost_lines;
    g_log_cost_ms = 0.0;
    g_log_cost_lines = 0;
    return ms;
}

static int QaLogWrite(unsigned long long options, FILE *stream, const char *format, _locale_t locale, va_list args);

extern "C" int __wrap___stdio_common_vfprintf(unsigned long long options, FILE *stream, const char *format, _locale_t locale, va_list args) {
    if ((stream != stdout && stream != stderr) || std::this_thread::get_id() != g_log_cost_thread) {
        return QaLogWrite(options, stream, format, locale, args);
    }
    const auto from = std::chrono::steady_clock::now();
    const int  size = QaLogWrite(options, stream, format, locale, args);

    const double spent = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - from).count();

    g_log_cost_ms += spent;
    g_log_cost_lines++;
    g_log_total_ms += spent;
    g_log_total_lines++;
    return size;
}

static int QaLogWrite(unsigned long long options, FILE *stream, const char *format, _locale_t locale, va_list args) {
    static const bool on = [] {
        const char *v = std::getenv("DC_QA_TICK_MARK");
        return v != nullptr && *v != '\0' && *v != '0';
    }();

    static const int cost_ms = [] {
        const char *v = std::getenv("DC_QA_LOG_COST_MS");
        return v != nullptr ? std::atoi(v) : 0;
    }();

    if (cost_ms > 0 && (stream == stdout || stream == stderr) && !g_log_held.load() && std::this_thread::get_id() == g_log_cost_thread) {
        std::this_thread::sleep_for(std::chrono::milliseconds(cost_ms));
    }
    // coop-small1b (the arriver's long frames in the black, 11-10 quiet runs: his state set's apply frame 231-241 ms, of it 144-153
    // ms in 17 log lines): a test copy's stdout is unbuffered (DC_QA_ONE_STREAM, above) and a file, not a console, and the UCRT
    // batches an unbuffered stream's characters only for a console: to a file every character of a printf is a write call of its
    // own (scratch\small1b\wtest: 10.9-12.5 ms a 120-character line, against 0.11-0.14 ms for one fwrite of it). So a line on
    // stdout or stderr is formatted here and written whole, in one call, stamped or not.
    // DC_QA_LOG_MUTATE=char_writes (l-local-play-houseout~charlog): the CRT's own, a write a character, as before.
    static const bool char_writes = [] {
        const char *v = std::getenv("DC_QA_LOG_MUTATE");
        return v != nullptr && std::strcmp(v, "char_writes") == 0;
    }();

    if ((char_writes && !on && !g_log_held.load()) || (stream != stdout && stream != stderr)) {
        return __real___stdio_common_vfprintf(options, stream, format, locale, args);
    }
    va_list copy;
    va_copy(copy, args);
    const int size = __stdio_common_vsprintf(options, nullptr, 0, format, locale, copy);
    va_end(copy);
    if (size <= 0) {
        return __real___stdio_common_vfprintf(options, stream, format, locale, args);
    }
    std::string text((std::size_t) size + 1, '\0');
    __stdio_common_vsprintf(options, text.data(), text.size(), format, locale, args);
    text.resize((std::size_t) size);

    static std::mutex lock;
    static bool       line_start = true; // (one stream: DC_QA_ONE_STREAM)
    std::lock_guard<std::mutex> hold(lock);
    std::string out;
    char        stamp[24];
    const int   stamp_size = std::snprintf(stamp, sizeof stamp, "f%lld ", g_log_frame.load(std::memory_order_relaxed));

    out.reserve(text.size() + 32);
    for (const char c : text) {
        if (on && line_start && c != '\n') {
            out.append(stamp, (std::size_t) stamp_size);
        }
        out.push_back(c);
        line_start = c == '\n';
    }
    {
        std::lock_guard<std::mutex> guard(g_log_held_lock);

        if (g_log_held.load()) {
            g_log_held_text += out;
            return size;
        }
    }
    std::fwrite(out.data(), 1, out.size(), stream);
    return size;
}
#else
void QaLogHold(bool) {}
double QaLogCost(long long *lines) {
    *lines = 0;
    return 0.0;
}
void QaLogTotals(double *ms, long long *lines) {
    *ms = 0.0;
    *lines = 0;
}
#endif

bool TestTraceKept() {
    static const bool kept = [] {
        const char *list = std::getenv("DC_COOP_MUTATE");
        return list != nullptr && std::strstr(list, "trace_once_kept") != nullptr;
    }();
    return kept;
}

bool TestSwitchStale() {
    // (the process's own environment: Mutated reads its list through COOP_TEST_SWITCH itself)
    static const bool stale = [] {
        const char *list = std::getenv("DC_COOP_MUTATE");
        return list != nullptr && std::strstr(list, "switch_stale") != nullptr;
    }();
    return stale;
}

const char *TestEnv(const char *name) {
    TestLogStreamsOnce();
    for (const auto &kv : g_job_env) {
        if (kv.first == name) {
            return kv.second.empty() ? nullptr : kv.second.c_str();
        }
    }
    const char *value = std::getenv(name);
    return value != nullptr && *value != '\0' ? value : nullptr;
}

const char *TestEnvOr(const char *name, const char *fallback) {
    const char *value = TestEnv(name);
    return value != nullptr ? value : fallback;
}

int TestEnvInt(const char *name, int fallback) {
    const char *value = TestEnv(name);
    return value != nullptr ? std::atoi(value) : fallback;
}

bool TestEnvOn(const char *name) {
    const char *value = TestEnv(name);
    return value != nullptr && *value != '0';
}

bool TestEnvIs(const char *name, const char *value) {
    const char *set = TestEnv(name);
    return set != nullptr && std::strcmp(set, value) == 0;
}

bool CoopDebug() {
    static const bool debug = TestEnv("DC_COOP_DEBUG") != nullptr;
    return debug;
}

namespace coop {

Partner g_partner;

bool Enabled() {
    return dc::platform::ConfigGet().coop;
}

float Stick(std::uint8_t axis) {
    return static_cast<float>(AxisCalibration(axis)) / 128.0f;
}

// coop: gaps2 (the Atla test's measure: GatherPolys without the Atlas)
bool g_skip_atla = false;

// The collision polygons around pos, gathered the way DunMoveChara gathers the player's.
int GatherPolys(const float *pos, CCPoly **out) {
    CBoxVu0 bound;

    WorkBuffer->used = 0;
    CCPoly *polys = (CCPoly *) WorkBuffer->Alloc(0x7D0);
    int     count = 0;

    bound.max[0] = pos[0] + 20.0f;
    bound.max[1] = pos[1] + 20.0f;
    bound.max[2] = pos[2] + 20.0f;
    bound.min[0] = pos[0] - 20.0f;
    bound.min[1] = pos[1] - 40.0f;
    bound.min[2] = pos[2] - 20.0f;

    auto turn_of = [](int turn) {
        if (turn > 3) {
            turn -= 3;
        }

        if (turn == 3) {
            turn = -1;
        }

        return PI * (-90.0f * turn) / 180.0f;
    };

    if (NowDngMap->map_type != 1) {
        for (int i = 0; NowDngMap->parts[i].frame[0] != nullptr; i++) {
            CDungeonParts *part = &NowDngMap->parts[i];
            CFrame        *collision = part->collision;

            if (collision != nullptr) {
                sceVu0FVECTOR parts_pos;

                sceVu0CopyVector(parts_pos, part->frame_offset[0]);
                collision->SetRotation(0.0f, turn_of((int) part->frame_turn[0] + part->collision_turn), 0.0f);
                collision->SetPosition(parts_pos);
                count += collision->PickUpNearPoly(&polys[count], bound);
            }
        }
    } else {
        for (int z = 0; z < 20; z++) {
            for (int x = 0; x < 20; x++) {
                MAP_CELL *cell = &NowDngMap->cells[x + z * 20];
                int       parts_no = cell->parts_no;

                if (parts_no == -1) {
                    continue;
                }

                CFrame *collision = NowDngMap->parts[parts_no].collision;

                // The player's own pass keeps the cells near the camera; the partner's keeps the
                // ones its bound reaches.
                float cx = 160.0f * x;
                float cz = 160.0f * z;

                if (collision == nullptr || std::fabs(cx - pos[0]) > 240.0f || std::fabs(cz - pos[2]) > 240.0f) {
                    continue;
                }

                collision->SetRotation(0.0f, turn_of(cell->direction + NowDngMap->parts[parts_no].collision_turn), 0.0f);
                collision->SetPosition(cx, 0.0f, cz);
                count += collision->PickUpNearPoly(&polys[count], bound);
            }
        }
    }

    for (int i = 0; i < 24; i++) {
        if (NowDngMap->boxes[i].used != 0) {
            CFrame *collision = NowDngMap->box_collision_model;

            collision->SetPosition(NowDngMap->boxes[i].pos);
            count += collision->PickUpNearPoly(&polys[count], bound);
        }
    }

    // coop: gaps2
    // The Atlas, round pos: retail's CreateCollision gathers the ones within 400 of the player only,
    // so a partner far from the player (a camera each) walked through them. DC_COOP_MUTATE=atla
    // gathers them as retail, for the test's twin; g_skip_atla leaves them out, for its measure.
    if (Mutated("atla")) {
        count = NowDngMap->CreateCollision(polys, bound, count);
    } else if (!g_skip_atla && NowDngMap->collision_model != nullptr) {
        for (int i = 0; i < NowDngMap->atra_num; i++) {
            if (NowDngMap->atra[i].used != 0 && DistVector(NowDngMap->atra[i].pos, (float *) pos) <= 400.0f) {
                NowDngMap->collision_model->SetPosition(NowDngMap->atra[i].pos);
                count += NowDngMap->collision_model->PickUpNearPoly(&polys[count], bound);
            }
        }
    }

    if (NowDngMap->map_type != 1) {
        count = NowDranMapField->AddCollision(polys, count, bound);
    }

    *out = polys;
    return count;
}

} // namespace coop

using namespace coop;

namespace {

// Logs which gamepad drives which player whenever the set changes: Kazarov's input numbers each
// gamepad it opens with the pad it drives (SDL_SetGamepadPlayerIndex), the first player's pad 0.
void LogPads() {
    static std::string last;
    int                count = 0;
    SDL_JoystickID    *ids = SDL_GetGamepads(&count);
    std::string        now;

    for (int i = 0; i < count; i++) {
        char line[192];
        int  player = SDL_GetGamepadPlayerIndexForID(ids[i]);
        const char *name = SDL_GetGamepadNameForID(ids[i]);

        std::snprintf(line, sizeof line, "coop: gamepad %u \"%s\" (%04x:%04x) -> %s\n", (unsigned) ids[i], name ? name : "?",
                      SDL_GetGamepadVendorForID(ids[i]), SDL_GetGamepadProductForID(ids[i]),
                      player == 0 ? "player 1" : player == 1 ? "player 2" : "unused");
        now += line;
    }

    SDL_free(ids);

    if (now != last) {
        std::fputs(now.empty() ? "coop: no gamepads\n" : now.c_str(), stderr);
        last = now;
    }
}

// Test hook (DC_COOP_PLACE=wake): once, puts the partner on the line from the player to the nearest
// monster still asleep, 40 units inside the distance it wakes at, when that is within the leash, to
// show the partner waking a monster the player is too far from to wake.
void PlaceForWakeTest() {
    static const char *mode = TestEnv("DC_COOP_PLACE"); // (the pair's: every row of a set has the same)
    static bool        done = false;

    // coop-warmconvert (WS7 round 4): once a job on a warm pair (its fixture put her back where she stood). keydoor_job_kept: once a
    // process, as before.
    COOP_TEST_JOB_RESET(done, false, "keydoor_job_kept");

    // coop-pt1109b: DC_COOP_PLACE=far:<dx>: from link tick 200 the partner is held dx to the player's side, at his height, on both copies
    // alike (the draw-distance row: the far player's body is hidden past retail's 300).
    if (mode != nullptr && std::strncmp(mode, "far:", 4) == 0) {
        if (NetplayLinkTick() >= 200 && g_partner.chara != nullptr) {
            g_partner.pos[0] = CharaMain.pos[0] + (float) std::atof(mode + 4);
            g_partner.pos[1] = CharaMain.pos[1];
            g_partner.pos[2] = CharaMain.pos[2];
            g_partner.chara->frame->SetPosition(g_partner.pos);
        }
        return;
    }

    if (done || mode == nullptr) {
        return;
    }

    // DC_COOP_PLACE=chest: the partner on the nearest chest's event point, where the player's Cross
    // opens it.
    // coop-party: DC_COOP_PLACE=event:<prefix>: on each floor that has one, once, the partner on the first
    // event whose name starts with prefix (e.g. chr1_ for Xiao's spots).
    if (std::strncmp(mode, "event:", 6) == 0) {
        static int placed_floor = -1;
        int        floor = selectMapNo * 256 + UserStatus->cur_floor;

        if (placed_floor == floor) {
            return;
        }

        for (CDungeonEventData &data : NowEventMan->event) {
            if (data.event != nullptr && data.enabled && std::strncmp(data.event->name, mode + 6, std::strlen(mode + 6)) == 0) {
                placed_floor = floor;

                for (int axis : {0, 1, 2}) {
                    g_partner.pos[axis] = data.pos[axis];
                }

                g_partner.chara->frame->SetPosition(g_partner.pos);
                std::printf("coop-debug: partner placed on event %s (script %d) on floor %d, player at (%.0f %.0f %.0f)\n",
                            data.event->name, data.event->script_no, (int) UserStatus->cur_floor, CharaMain.pos[0], CharaMain.pos[1],
                            CharaMain.pos[2]);
                return;
            }
        }

        return;
    }

    // coop-party: DC_COOP_PLACE=gate: the partner on the floor's exit gate event ("out"), or its gate key's
    // ("gatekey") when the gate is locked behind it, where the player's Cross runs its script.
    if (std::string(mode) == "gate" || std::string(mode) == "gatekey") {
        done = true;

        for (CDungeonEventData &data : NowEventMan->event) {
            bool key = std::string(mode) == "gatekey";

            if (data.event != nullptr && data.enabled &&
                (key ? std::strncmp(data.event->name, "gatekey", 7) == 0 : std::strcmp(data.event->name, "out") == 0)) {
                for (int axis : {0, 1, 2}) {
                    g_partner.pos[axis] = data.pos[axis];
                }

                g_partner.chara->frame->SetPosition(g_partner.pos);

                // DC_COOP_MEMBER_EVENT=N: the event becomes member N's (no early floor has one), for the test.
                if (const char *member = TestEnv("DC_COOP_MEMBER_EVENT")) {
                    data.event->chara_no = std::atoi(member);
                }

                std::printf("coop-debug: partner placed on event %s (script %d), player at (%.0f %.0f %.0f)\n", data.event->name,
                            data.event->script_no, CharaMain.pos[0], CharaMain.pos[1], CharaMain.pos[2]);
                return;
            }
        }

        std::printf("coop-debug: no %s event on the floor\n", mode);
        return;
    }

    // coop: gaps2
    // DC_COOP_PLACE=trapcircle:<kind>: once, on the 60th tick of play, a ready trap circle of that kind
    // (TrapCircleKind) where the partner stands, in the first free slot, as buildEventData lays them.
    if (std::strncmp(mode, "trapcircle:", 11) == 0) {
        static int ticks = 0;

        if (++ticks < 60) {
            return;
        }

        done = true;

        for (MAP_TRAP_CIRCLE &circle : NowDngMap->trap_circle) {
            if (circle.state == 0) {
                sceVu0CopyVector(circle.pos, g_partner.pos);
                circle.state = 1;
                circle.kind = std::atoi(mode + 11);
                circle.timer = 0.0f;
                std::printf("coop-debug: trap circle kind %d laid under the partner at (%.0f %.0f %.0f), player %.0f away\n", circle.kind,
                            circle.pos[0], circle.pos[1], circle.pos[2], DistVector(circle.pos, CharaMain.pos));
                return;
            }
        }

        std::printf("coop-debug: no free trap circle slot\n");
        return;
    }

    if (std::string(mode) == "chest") {
        float best = 1.0e9f;
        int   chosen = -1;

        for (int i = 0; i < 48; i++) {
            if (NowDngMap->events[i].kind != DNG_EVENT_TREASURE_BOX) {
                continue;
            }

            float distance = DistVector(NowDngMap->events[i].pos, CharaMain.pos);

            if (distance < best) {
                best = distance;
                chosen = i;
            }
        }

        done = true;

        if (chosen == -1) {
            std::printf("coop-debug: no chest on the floor\n");
            return;
        }

        const float *event = NowDngMap->events[chosen].pos;

        for (int axis : {0, 1, 2}) {
            g_partner.pos[axis] = event[axis];
        }

        g_partner.chara->frame->SetPosition(g_partner.pos);
        std::printf("coop-debug: partner placed on chest event %d (box %d, reach %.0f), player %.0f from it\n", chosen,
                    NowDngMap->events[chosen].index, NowDngMap->events[chosen].radius, best);
        return;
    }
    if (std::string(mode) != "wake") {
        return;
    }
    // coop-fx51e (g-monster-body on a fixture): not before a state fixture is put back, which would undo it.
    if (NetplayStateLoadPending()) {
        return;
    }

    float best = 1.0e9f;
    int   chosen = -1;

    for (int i = 0; i < 16; i++) {
        float position[4];

        if (NowMonstorUnit->monster[i].state != 1) {
            continue;
        }

        NowMonstorUnit->chara[i][0].GetPosition(position);
        float distance = DistVector(position, CharaMain.pos);

        if (distance < best) {
            best = distance;
            chosen = i;
        }
    }

    if (chosen == -1) {
        return;
    }

    float monster_position[4];
    float reach = NowMonstorUnit->monster[chosen].clip_distance - 40.0f;

    NowMonstorUnit->chara[chosen][0].GetPosition(monster_position);

    float along = best - reach;

    if (along <= 0.0f || along > kLeashDistance - 20.0f) {
        return;
    }

    done = true;

    for (int axis : {0, 2}) {
        g_partner.pos[axis] = CharaMain.pos[axis] + (monster_position[axis] - CharaMain.pos[axis]) * along / best;
    }

    g_partner.pos[1] = CharaMain.pos[1] + 20.0f;
    g_partner.chara->frame->SetPosition(g_partner.pos);
    std::printf("coop-debug: partner placed %.0f from monster %d (wakes at %.0f), player %.0f from it\n", reach, chosen,
                NowMonstorUnit->monster[chosen].clip_distance, best);
}

// Test hook (DC_COOP_PLACE=chestside:<dx>:<dz>): pad 2's Cross, with the partner away from the closed
// chest nearest the player, puts the partner dx, dz from that chest's event point (where Cross reaches
// it) and does nothing else; the next Cross opens it from there, as if the partner had walked up from
// that side. Again after a resync put it back beside the player. A press, not a tick, so linked copies
// (which see pad 2's presses on the same tick) place it alike. True when it took the press.
bool PlaceBesideChest() {
    static const char *mode = TestEnv("DC_COOP_PLACE");
    float              dx = 0.0f;
    float              dz = 0.0f;

    if (mode == nullptr || gameTask != GAME_TASK_PLAY || std::sscanf(mode, "chestside:%f:%f", &dx, &dz) != 2) {
        return false;
    }

    // coop-kocatchup (q-chest-face-xiao red under load, 2026-11-03): a monster hit her as she came, and her hurt motion's
    // knock-back went on after the placement, 16 from the event, past the chest's reach, so every Cross after it shot.
    // A partner who walked up was not reeling: a Cross during the hurt motion is hers (it does nothing then), the next
    // one places her. DC_COOP_MUTATE=place_hurt (twin): placed in the hurt motion, as before.
    // DC_COOP_TEST_PLACE_BLOW=1 (q-chest-face -PlaceBlow): the first press that would place her starts her flinch first, so the
    // case comes on every run. Knocked back across her side of the chest at 0.66 (the red run's blow was 1.0 and slid her 16
    // in its 19 ticks): some 9, which leaves her past the chest's reach (8) but inside twice its radius, where this hook no
    // longer places her again, as the red run's 15.5 did.
    static bool blown = false;

    if (!blown && TestEnvOn("DC_COOP_TEST_PLACE_BLOW") && HasMotion(6)) {
        const float length = std::sqrt(dx * dx + dz * dz);

        blown = true;
        g_combat.blow[0] = 0.66f * (length > 0.0f ? dz / length : 0.0f);
        g_combat.blow[1] = 0.0f;
        g_combat.blow[2] = 0.66f * (length > 0.0f ? -dx / length : 1.0f);
        BeginHurt(6, 3);
        std::fprintf(stderr, "coop-debug: test: her flinch at the placement's press, knock-back (%.2f %.2f)\n", g_combat.blow[0], g_combat.blow[2]);
    }
    if (g_combat.hurt != 0 && !Mutated("place_hurt")) {
        return false;
    }

    // The chest is picked once and kept: once it is open, no more placing (the test's look after the
    // opening must find the partner where the opening left it).
    static int target = -1;
    float      best = 1.0e9f;
    int        chosen = target;

    for (int i = 0; target == -1 && i < 48; i++) {
        const TREASURE_BOX &box = NowDngMap->boxes[NowDngMap->events[i].index];

        // Not a trapped large chest: that one still runs retail's presentation with the player.
        if (NowDngMap->events[i].kind != DNG_EVENT_TREASURE_BOX || box.closed == 0 || (box.kind == TREASURE_BOX_LARGE && box.trap_no != 0)) {
            continue;
        }

        float distance = DistVector(NowDngMap->events[i].pos, CharaMain.pos);

        if (distance < best) {
            best = distance;
            chosen = i;
        }
    }

    target = chosen;

    if (chosen == -1 || NowDngMap->events[chosen].kind != DNG_EVENT_TREASURE_BOX || NowDngMap->boxes[NowDngMap->events[chosen].index].closed == 0 ||
        DistVector(NowDngMap->events[chosen].pos, g_partner.pos) <= 2.0f * NowDngMap->events[chosen].radius) {
        return false;
    }

    const float *box = NowDngMap->boxes[NowDngMap->events[chosen].index].pos;
    const float *event = NowDngMap->events[chosen].pos;

    g_partner.pos[0] = event[0] + dx;
    g_partner.pos[2] = event[2] + dz;
    g_partner.chara->frame->SetPosition(g_partner.pos);
    // On stderr with the netplay lines, so a test can tell a desync before the placement from one after.
    std::fprintf(stderr, "coop-debug: partner placed beside chest event %d at %.1f %.1f %.1f (box %d at %.1f %.1f %.1f, reach %.0f), "
                "%.1f %.1f from the event, partner y %.1f\n",
                chosen, event[0], event[1], event[2], NowDngMap->events[chosen].index, box[0], box[1], box[2],
                NowDngMap->events[chosen].radius, dx, dz, g_partner.pos[1]);
    return true;
}

// coop-monsterpush test line (DC_COOP_SEEK=into): how far her place is inside an awake monster's body (flat, the body below her
// head; negative: outside), the deepest over the floor's monsters; -1000 for none. The body: 6 plus its body radius round the
// monster; with retail set, as CMonstorUnit::MoveCheck draws it (a monster with effect bodies has 6 plus each one's radius round
// it instead: they follow its motions, so an attack's swing moves them into her while she stands). which and reach: the deepest
// body's monster and its 6 plus radius.
bool MonsterWalkLogged() {
    static const bool on = TestEnvIs("DC_COOP_SEEK", "into");
    return on;
}

float MonsterBodyDepth(const float *pos, bool retail, int *which = nullptr, float *reach = nullptr) {
    float deepest = -1000.0f;

    if (!MonsterWalkLogged()) {
        return deepest;
    }

    auto body = [&](int i, const float *center, float radius) {
        float dx = center[0] - pos[0];
        float dz = center[2] - pos[2];
        float depth = 6.0f + radius - sqrtf(dx * dx + dz * dz);

        if (center[1] < 18.0f + pos[1] && depth > deepest) {
            deepest = depth;

            if (which != nullptr) {
                *which = i;
            }

            if (reach != nullptr) {
                *reach = 6.0f + radius;
            }
        }
    };

    for (int i = 0; i < 16; i++) {
        const MONSTOR &monster = NowMonstorUnit->monster[i];

        if (monster.state != 2 || monster.revealed == 0 || monster.hp <= 0) {
            continue;
        }

        if (!retail || NowMonstorUnit->effect3[i].count == 0) {
            float position[4];

            NowMonstorUnit->chara[i][0].GetPosition(position);
            body(i, position, monster.body_radius);
        } else {
            for (int j = 0; j < NowMonstorUnit->effect3[i].count; j++) {
                body(i, NowMonstorUnit->effect3[i].position[j], NowMonstorUnit->effect3[i].radius[j]);
            }
        }
    }

    return deepest;
}

// Every 30 of her steps: her place, the nearest monster's, how deep she is in its body round it, the deepest yet, the steps of
// her own that took her deeper into a body as retail's check draws it (before: from before the step's check; after: its end)
// and the steps she stood at a body (within 4). On stderr, both copies.
void MonsterWalkLog(float before, float after, const float *step) {
    static int   steps = 0;
    static int   deeper = 0;
    static int   at_body = 0;
    static float deepest = -1000.0f;

    if (!MonsterWalkLogged() || g_partner.down > 0) {
        return;
    }

    float in = MonsterBodyDepth(g_partner.pos, false);

    steps++;

    if (after > 0.0f && after > before + 0.01f) {
        int   which = -1;
        float at[4] = {0.0f, 0.0f, 0.0f, 0.0f};

        deeper++;
        MonsterBodyDepth(g_partner.pos, true, &which);

        if (which >= 0) {
            NowMonstorUnit->chara[which][0].GetPosition(at);
        }

        // Why the check let it through: her step before it, both heights, the monster's state and effect bodies.
        std::fprintf(stderr,
                     "coop: test: P2 step %d went deeper %.2f -> %.2f: step (%.2f %.2f %.2f), she y %.1f, monster %d y %.1f state %d "
                     "revealed %d effect bodies %d\n",
                     steps, before, after, step[0], step[1], step[2], g_partner.pos[1], which, at[1],
                     which >= 0 ? NowMonstorUnit->monster[which].state : -1, which >= 0 ? NowMonstorUnit->monster[which].revealed : 0,
                     which >= 0 ? NowMonstorUnit->effect3[which].count : -1);
    }

    at_body += in > -4.0f;
    deepest = std::max(deepest, in);

    if (steps % 30 != 0) {
        return;
    }

    int   which = -1;
    float reach = 0.0f;
    float now = MonsterBodyDepth(g_partner.pos, false, &which, &reach);
    float at[4] = {0.0f, 0.0f, 0.0f, 0.0f};

    if (which >= 0) {
        NowMonstorUnit->chara[which][0].GetPosition(at);
    }

    std::fprintf(stderr,
                 "coop: test: P2 walks into monsters at step %d: she at (%.1f %.1f), monster %d at (%.1f %.1f) body %.1f, in %.2f, "
                 "deepest %.2f, her steps deeper %d, at a body %d\n",
                 steps, g_partner.pos[0], g_partner.pos[2], which, at[0], at[2],
                 reach, now, deepest, deeper, at_body);
}

void Walk() {
    SoundFrom sound_from(g_partner.pos); // coop-soundpt: her step's sounds come from where she stands
    PlaceForWakeTest();
    KeyDoorTest(); // coop-p2keys
    // Linked copies (game.netplay) both read the second player's pad as the two agreed on it.
    InputPadState linked;
    InputPadState        pad = NetplayPad(1, linked) ? linked : InputGetPad(1);
    bool                 menu = MenuHoldsPartner(pad); // coop-menus: P2's overlay holds the partner
    float                lx = Stick(pad.left_x);
    float                ly = Stick(pad.left_y);
    CamTestStick(lx, ly); // coop-strafe: the strafe check's stick (DC_COOP_CAMTEST=strafe)
    bool                 cross = (pad.buttons & kInputCross) != 0;
    bool                 pressed = cross && !g_partner.cross_held;
    CCharacter          *partner = g_partner.chara;
    CFrameVu1           *frame = (CFrameVu1 *) partner->frame;

    g_partner.cross_held = cross;

    if (pressed && PlaceBesideChest()) { // test hook (DC_COOP_PLACE=chestside)
        pressed = false;
    }
    g_partner.since_attack = std::min(g_partner.since_attack + 1, 3600);

    if (g_partner.invincible > 0) {
        g_partner.invincible--;
    }

    StepShots();
    PartnerWeaponWarning(); // coop-soundpt: her weapon about to break, on her copy
    StepWheel(pad); // coop-party: pad 2's character wheel (her Select; coop-wheelcrash)
    // Test hook (DC_COOP_TEST_UNHURT=1, t-wheel): neither player takes a blow, so the floor's monsters cannot knock either
    // out of play (a blow's recoil holds Select's wheel shut, as retail's). DC_COOP_TEST_UNHURT=p2 (q-chest-face): player 2
    // only, so a row whose setup needs player 1 knocked out by the floor's monsters keeps that.
    COOP_TEST_SWITCH(bool, unhurt, TestEnvOn("DC_COOP_TEST_UNHURT"));
    COOP_TEST_SWITCH(bool, hers_only, TestEnvIs("DC_COOP_TEST_UNHURT", "p2"));
    if (unhurt) {
        if (!hers_only) {
            BtActStatus.invincible_frames = std::max<int>(BtActStatus.invincible_frames, 2);
        }
        g_partner.invincible = std::max(g_partner.invincible, 2);
    }
    if (InputPadState one; NetplayPlayerPad(1, one)) {
        StepPlayerWheel(one); // coop-wheelcrash: player 1's own ring (his Select, linked)
    }
    bool picking = StepKeyPick(pad); // coop-keypick: her key picker at a gate's lock holds her, the game plays on
    picking = StepUsePrompt(2, pad) || picking; // coop-p2prompt: her chest's or gate's prompt holds her, the game plays on
    picking = DoorRefusing(2) || picking;       // coop-skeldoor: her wrong item's refusal at a key door holds her
    StepPrompt();   // coop-party: the player's member as on the floor (the swap question steps in MenuTick)
    StepSwap(pad);  // coop-party

    bool hurt = StepHurt(); // coop-p2hurt: a blow's flinch or knockdown holds her until it has played

    // (coop-serve, DC_COOP_DEBUG: her stick held but her walk held back, and by what, once a second: a job after her Atla
    // job left her standing, 2026-11-01)
    const bool  stick_held = lx != 0.0f || ly != 0.0f;
    const char *held_by = nullptr;

    // Down, the partner lies still until it gets up (StepDown has the revive rule). // coop-party
    if (StepDown() || StepBusy(pad) || hurt) { // coop-party: StepBusy holds it still while it opens a chest
        lx = ly = 0.0f;
        held_by = g_partner.down > 0 ? "down" : hurt ? "hurt" : "busy";
    } else if (menu || picking) { // coop-menus, coop-keypick
        lx = ly = 0.0f;
        held_by = menu ? "menu" : "picking";
    } else if (CombatInput(pad, pressed)) { // coop: combat (swings, charge, lock-on, aim, items, panel)
        lx = ly = 0.0f;
        held_by = "combat";
    }
    if (static int said = 0; CoopDebug() && stick_held && held_by != nullptr && ++said % 60 == 1) {
        std::fprintf(stderr, "coop-debug: p2 stick held, her walk held by %s (busy %d attacking %d)\n", held_by, (int) g_partner.busy,
                     (int) g_partner.attacking);
    }
    if (held_by != nullptr) {
    } else if (g_combat.guard != 0) { // coop-swapfix: guarding, it walks slowly, as the player does
        lx *= 0.4f;
        ly *= 0.4f;
    }

    // coop: cameras
    float angle = PartnerCameraAngleH();
    float move_x = lx * cosf(angle) + ly * sinf(angle);
    float move_z = ly * cosf(angle) - lx * sinf(angle);

    // Test hook (DC_COOP_SEEK): with the stick at rest the partner turns to the nearest monster taking
    // part, walks the last 60 units to it (it does not find a way round walls) and swings once within reach, so a headless run can fight without knowing
    // the floor's layout.
    // DC_COOP_SEEK=passive walks up without swinging, to let the monster land its blows.
    static const char *seek_mode = TestEnv("DC_COOP_SEEK");
    static const bool  seek = seek_mode != nullptr;
    static const bool  passive = seek && std::string(seek_mode) == "passive";
    // DC_COOP_SEEK=wake walks all the way to the nearest monster still asleep, without swinging.
    static const bool  wake = seek && std::string(seek_mode) == "wake";
    // coop-monsterpush: DC_COOP_SEEK=into walks straight at the nearest monster, asleep or awake, and keeps walking into it
    // without swinging (the monster-body walk test).
    static const bool  into = seek && std::string(seek_mode) == "into";

    if (seek && lx == 0.0f && ly == 0.0f && g_partner.down == 0) {
        float best = 1.0e9f;
        float to[4] = {0.0f, 0.0f, 0.0f, 0.0f};

        for (int i = 0; i < 16; i++) {
            float position[4];
            int   state = NowMonstorUnit->monster[i].state;

            if ((into ? state != 1 && state != 2 : state != (wake ? 1 : 2)) || NowMonstorUnit->monster[i].hp <= 0) {
                continue;
            }

            NowMonstorUnit->chara[i][0].GetPosition(position);
            float distance = DistVector(position, g_partner.pos);

            if (distance < best) {
                best = distance;
                to[0] = position[0] - g_partner.pos[0];
                to[2] = position[2] - g_partner.pos[2];
            }
        }

        if (best < 1.0e9f) {
            float length = sqrtf(to[0] * to[0] + to[2] * to[2]);

            if (length > 0.0f) {
                ((CFrameVu1 *) g_partner.chara->frame)->SetRotation(0.0f, atan2f(to[0], to[2]), 0.0f);
            }

            if (best <= 18.0f && !g_partner.attacking && !into) {
                if (!passive && !wake && AttackReady()) {
                    StartAttack();
                }
            } else if (!g_partner.attacking && length > 0.0f && (best < 60.0f || wake || into)) {
                move_x = to[0] / length;
                move_z = to[2] / length;
                lx = 1.0f;
            }
        }
    }
    // coop-strafe: locked on, she walks as the player does locked on (DunMoveChara's lock-on branch): at the stick's own
    // pace, not the run's, facing the monster (CombatFace) while the strafe motions show which way she steps (Jep's
    // 2026-10-27 playtest: her lock-on strafe had the wrong facing; she ran sideways and backwards on the run's motion).
    bool  strafe = Strafing();
    float speed = strafe ? 1.0f : SpeedBoosted() ? kBoostSpeed : kRunSpeed; // coop-p2feather

    g_partner.velocity[0] = move_x * speed;
    g_partner.velocity[2] = move_z * speed;
    HurtPush(g_partner.velocity); // coop-p2hurt

    if (move_x != 0.0f || move_z != 0.0f) {
        if (!strafe) {
            frame->SetRotation(0.0f, unitRotation(frame, atan2f(move_x, move_z)), 0.0f);
        }
        g_partner.heading[0] = move_x;
        g_partner.heading[1] = 0.0f;
        g_partner.heading[2] = move_z;
        sceVu0Normalize(g_partner.heading, g_partner.heading);
    }

    CombatFace(); // coop: combat

    int motion = g_partner.motion;

    if (g_partner.down == 0 && !g_partner.attacking && g_partner.busy == 0 && g_combat.guard == 0 && g_combat.hurt == 0 && g_partner.key_stage == 0 &&
        !(PartnerAtlaPresenting() && !Mutated("atlagrab"))) { // coop-party: busy; coop-swapfix: guard; coop-p2hurt; coop-actorswap: her Atla grab
        partner->motion_type.motion_info->speed = 0.05f;
        motion = keyCtrl(lx, ly, partner->motion_type.motion_info);

        if (strafe) {
            motion = StrafeMotion(move_x, move_z, motion);
        }
    }

    CCPoly       *polys;
    int           count = GatherPolys(g_partner.pos, &polys);
    sceVu0FVECTOR moved;
    MoveCheckInfo info;

    // coop-nopush: she stops at the player's body as he stops at hers (CoopPartnerWall); standing still she has no wall,
    // so his walk up to her never steps her aside. DC_COOP_MUTATE=push_through (twin): none (KeepApart steps her out).
    if ((g_partner.velocity[0] != 0.0f || g_partner.velocity[2] != 0.0f) && !Mutated("push_through")) {
        count += CharaMain.PickUpPoly(g_partner.pos, &polys[count]);
    }

    // coop-openpos (Jep's 2026-10-28 playtest: her Atla's place "still off", her chest's opening "sometimes too far from the
    // chest"): her opening and her Atla put her on their spot (StartOpening, TakeAtla) and the presentation owns it until it
    // ends, as retail's holds its player (step_disable, no DunMoveChara). Her walk's checks kept running on it, from the
    // Cross's own tick (Interact, inside CombatInput above): KeepApart stepped her out of player 1 whenever he stood within
    // kApart of the spot, which carried her chest frame (PoseBusy) and her Atla stance off with it. Held, nothing moves her.
    // DC_COOP_MUTATE=openshove: the checks run on, as before.
    if (PartnerPresenting() && !Mutated("openshove")) {
        g_partner.velocity[0] = g_partner.velocity[1] = g_partner.velocity[2] = 0.0f;
        info.landed = 0;
    } else {
        // coop-monsterpush (Jep's 2026-10-30 playtest: "P2 can walk straight through enemies"): her step goes through
        // retail's monster check first, as the player's does in DunMoveChara (CMonstorUnit::MoveCheck before MoveCheck):
        // a step that ends within 6 of an awake monster's body, heading at it, is cut to a fall in place. Her own place,
        // step, lock-on and height (CharaHeight reads the save's cur_chara: hers for the one call). The monsters already
        // stop at her (MoveCheck2, coop_target.cpp). DC_COOP_MUTATE=monsterpush_off (twin): no check, she walks through.
        float before = MonsterBodyDepth(g_partner.pos, true);
        float step[4];

        sceVu0CopyVector(step, g_partner.velocity);

        if (!Mutated("monsterpush_off") && g_partner.loaded_chara >= 0) {
            s8 cur_chara = UserStatus->cur_chara;

            UserStatus->cur_chara = (s8) g_partner.loaded_chara;
            NowMonstorUnit->MoveCheck(g_partner.pos, g_partner.velocity, g_combat.lock >= 0 ? 1 : 0);
            UserStatus->cur_chara = cur_chara;
        }

        MoveCheck(g_partner.pos, g_partner.velocity, moved, &info, polys, count, 1);
        g_partner.velocity[1] -= 0.1f;

        if (g_partner.velocity[1] < -10.0f) {
            g_partner.velocity[1] = -10.0f;
        }

        sceVu0CopyVector(g_partner.pos, moved);
        KeepApart(polys, count); // coop-party
        MonsterWalkLog(before, MonsterBodyDepth(g_partner.pos, true), step); // coop-monsterpush test line
    }

    if (info.landed != 0) {
        g_partner.velocity[1] = 0.0f;
        partner->SetFootSoundID(info.poly.attr.foot_sound);
    }

    float dx = g_partner.pos[0] - CharaFrame->position[0];
    float dz = g_partner.pos[2] - CharaFrame->position[2];

    // A test placement (DC_COOP_PLACE) may stand the partner past the leash.
    static const bool placed = TestEnv("DC_COOP_PLACE") != nullptr;

    // coop: cameras, combat
    // The two may go anywhere apart on a floor (Jep); only a fall off the floor brings the partner back.
    // The cameras' twin (pullback) puts the old leash back.
    bool leashed = !placed && CoopMutate("pullback");

    if (g_partner.pos[1] < -100.0f || (leashed && dx * dx + dz * dz > kLeashDistance * kLeashDistance)) {
        PlaceBesidePlayer();
        g_partner.attacking = false;

        if (g_partner.down == 0) {
            motion = 0;
        }
    }

    frame->SetPosition(g_partner.pos);
    PoseBusy(); // coop-party: an opening chest's pose, as retail's

    if (g_partner.attacking) {
        StepAttack();
    } else if (motion != g_partner.motion) {
        PlayMotion(motion, 0);
    }

    StepGauge(); // coop-swapfix: the partner's speed gauge

    if (g_partner.down == 0) {
        TakeHits();
        PickUp(); // coop-party
        PartnerTrapCircle(); // coop: gaps2
    }

    // coop: gaps2
    // The rooms the partner walks into show on the mini-map, as the player's do (DunMoveChara's
    // checkMask, there every fifth tick; here every tick, so no count to keep in step). Both copies.
    // Only inside the floor's 20 by 20 cells, which checkMask does not check.
    int cell_x = (int) ((80.0f + g_partner.pos[0]) / 160.0f);
    int cell_z = (int) ((80.0f + g_partner.pos[2]) / 160.0f);

    if (!Mutated("minimap") && g_partner.pos[0] > -80.0f && g_partner.pos[2] > -80.0f && cell_x < 20 && cell_z < 20) {
        NowDngMap->checkMask(g_partner.pos[0], g_partner.pos[2]);
    }
}

// With DC_COOP_DEBUG set, once a second: where the two stand, the partner's life, and each monster
// taking part with its life, distances and whom it goes after.
void DebugLog() {
    static const bool enabled = CoopDebug();
    static int        wait = 0;

    if (!enabled) {
        return;
    }

    // coop: gaps2
    // How far the awake monsters have come into the partner's body (retail's 6 units plus the
    // monster's body radius, flat, which the monsters keep from the player), the deepest since the
    // copy started, for the monster-body test.
    static float deepest = 0.0f;
    static int   deep_ticks = 0;
    // (coop-warmreset, WS7: put back at each warm-pair job as a fresh copy has it; trace_once_kept twin: kept)
    COOP_TEST_TRACE_RESET(wait, 0);
    COOP_TEST_TRACE_RESET(deepest, 0.0f);
    COOP_TEST_TRACE_RESET(deep_ticks, 0);

    for (int i = 0; i < 16 && g_partner.down == 0; i++) {
        const MONSTOR &monster = NowMonstorUnit->monster[i];
        float          position[4];

        if (monster.state != 2 || monster.hp <= 0) {
            continue;
        }

        NowMonstorUnit->chara[i][0].GetPosition(position);
        float dx = position[0] - g_partner.pos[0];
        float dz = position[2] - g_partner.pos[2];
        float into = 6.0f + monster.body_radius - sqrtf(dx * dx + dz * dz);

        if (into > 2.0f && position[1] < 18.0f + g_partner.pos[1]) {
            deep_ticks++;
        }

        deepest = std::max(deepest, into);
    }

    if (--wait > 0) {
        return;
    }

    wait = 60;
    std::printf("coop-debug: monsters into p2 deepest %.1f, ticks over 2 %d\n", deepest, deep_ticks);

    TargetOpcodeCheck(); // coop: gaps2 (the monster-script test)

    // coop: gaps2 (the Atla test): each Atla on the floor, how far from the player, and how many of its
    // collision polygons GatherPolys picks up standing on it (with them less without them).
    for (int i = 0; i < NowDngMap->atra_num; i++) {
        if (NowDngMap->atra[i].used == 0) {
            continue;
        }

        CCPoly *polys;
        float   at[4];

        sceVu0CopyVector(at, NowDngMap->atra[i].pos);
        at[1] += 10.0f;
        g_skip_atla = true;
        int without = GatherPolys(at, &polys);
        g_skip_atla = false;
        int with = GatherPolys(at, &polys);
        std::printf("coop-debug: atla %d %.0f from p1, its polys gathered %d\n", i, DistVector(NowDngMap->atra[i].pos, CharaMain.pos),
                    with - without);
    }

    // coop: gaps2 (the minimap test): the mini-map cells the two stand in and whether each shows.
    int p1_cell = (int) ((80.0f + CharaMain.pos[0]) / 160.0f) + 20 * (int) ((80.0f + CharaMain.pos[2]) / 160.0f);
    int p2_cell = (int) ((80.0f + g_partner.pos[0]) / 160.0f) + 20 * (int) ((80.0f + g_partner.pos[2]) / 160.0f);

    if (p1_cell >= 0 && p1_cell < 400 && p2_cell >= 0 && p2_cell < 400) {
        std::printf("coop-debug: map p1 cell %d seen %d, p2 cell %d seen %d\n", p1_cell, NowDngMap->mask[p1_cell], p2_cell,
                    NowDngMap->mask[p2_cell]);
    }
    std::printf("coop-debug: p1 (%.0f %.0f %.0f) p2 (%.0f %.0f %.0f) p2 life %d/%d down %d\n", CharaMain.pos[0], CharaMain.pos[1],
                CharaMain.pos[2], g_partner.pos[0], g_partner.pos[1], g_partner.pos[2], Life(), MaxLife(), g_partner.down);

    // coop-party: where the partner's cloth (Goro's cape) hangs, for the party tests.
    for (int i = 0; g_partner.chara != nullptr && g_partner.chara->cloth != nullptr && i < 8 && g_partner.chara->cloth[i] != nullptr; i++) {
        CCloth      *cloth = g_partner.chara->cloth[i];
        const float *corner = cloth->point[0][0];

        std::printf("coop-debug: p2 cloth %d '%s' (found %d) at (%.0f %.0f %.0f), %.0f from p2\n", i, cloth->material.texture,
                    TexManager.GetTextureHandle(cloth->material.texture, -1) >= 0, corner[0], corner[1], corner[2],
                    DistVector((float *) corner, g_partner.pos));
    }

    for (int i = 0; i < 16; i++) {
        MONSTOR &monster = NowMonstorUnit->monster[i];
        float    position[4];

        if (monster.state != 2 && monster.state != 1) {
            continue;
        }

        NowMonstorUnit->chara[i][0].GetPosition(position);
        std::printf("coop-debug:   monster %d %s hp %d/%d wakes at %.0f to p1 %.0f to p2 %.0f target p%d\n", i,
                    monster.state == 2 ? "awake" : "asleep", monster.hp, monster.max_hp, monster.clip_distance,
                    DistVector(position, CharaMain.pos), DistVector(position, g_partner.pos), CoopMonsterTarget(i) + 1);
    }
}

// coop: gaps2
// An event script (a Gate Key door, a back-floor door, a boss's arena, a fall) may move the player
// while the partner is hidden; nothing brought the partner along, so it came back where it stood,
// behind the door or outside the arena (with a camera each there is no leash). When an event ends
// with the player more than kEventFollow from where it began, the partner is put beside the player:
// at its right hand, else its left, behind or before it, the first the player could walk to.
// DC_COOP_WARP_AT=N (test hook): on the Nth tick of play, an event's worth of moving: the player is
// put in the farthest room cell of the floor within 200 units, as a script would. DC_COOP_MUTATE=follow
// leaves the partner where it was, for the test's twin.
constexpr float kEventFollow = 40.0f;

void WarpForTest() {
    static const char *warp_at = TestEnv("DC_COOP_WARP_AT");
    static int         ticks = 0;

    if (warp_at == nullptr || gameTask != GAME_TASK_PLAY || ++ticks != std::atoi(warp_at)) {
        return;
    }

    float best = 0.0f;
    float to[4] = {0.0f, 0.0f, 0.0f, 1.0f};

    for (int i = 0; i < 400; i++) {
        if (NowDngMap->cells[i].parts_no == MAP_PARTS_NONE) {
            continue;
        }

        float cell[4] = {160.0f * (i % 20), CharaMain.pos[1], 160.0f * (i / 20), 1.0f};
        float distance = DistVector(cell, CharaMain.pos);

        if (distance > best && distance <= 200.0f) {
            best = distance;
            sceVu0CopyVector(to, cell);
        }
    }

    if (best <= 0.0f) {
        std::printf("coop: warp test found no cell\n");
        return;
    }

    g_partner.event_held = true;
    sceVu0CopyVector(g_partner.event_from, CharaMain.pos);
    CharaMain.SetPosition(to);
    sceVu0CopyVector(CharaMain.pos, to);
    std::printf("coop: warp test moves p1 %.0f to (%.0f %.0f %.0f)\n", best, to[0], to[1], to[2]);
}

// coop: minimap2
// DC_COOP_CRYSTAL_AT=N (test hook): on the Nth tick of play the magic crystal is taken, as its small
// chest gives it (BtEquipMasuisyou), on both copies at the same tick.
void CrystalForTest() {
    static const char *crystal_at = TestEnv("DC_COOP_CRYSTAL_AT");
    static int         ticks = 0;

    if (crystal_at == nullptr || gameTask != GAME_TASK_PLAY || ++ticks != std::atoi(crystal_at)) {
        return;
    }

    BtEquipMasuisyou = 1;
    std::printf("coop: crystal test takes the magic crystal\n");
}

// coop-hudicon: DC_COOP_STAMINA_AT=N (test hook): on the Nth tick of play the partner's member drinks a Stamina Drink
// (PartnerSetAilment, as its own use does), on both copies at the same tick. Before it, on its 60th tick, the partner is
// put 30 units to the player's right (PlaceBesidePlayer's side), clear of his body in his camera's view. Logged on stdout,
// the stream the frame series' lines are on, so a test can tell which frames came before the drink.
// coop-fx51e (Jep, 2026-11-04: rows start on a fixture at their moment): DC_COOP_STAMINA_AT=loaded:N counts its ticks from
// the state fixture's load (DC_STATE_LOAD_AT, the same link tick on both copies) instead of from play: she is put at his
// right on the first tick after the load, the frame series is marked there (DC_COOP_TEST_SHOT_SERIES_MARK), and she drinks
// on the Nth.
void StaminaForTest() {
    static const char *stamina_at = TestEnv("DC_COOP_STAMINA_AT");
    static const bool  on_load = stamina_at != nullptr && std::strncmp(stamina_at, "loaded:", 7) == 0;
    static const int   drink_at = stamina_at == nullptr ? -1 : std::atoi(on_load ? stamina_at + 7 : stamina_at);
    static int         ticks = 0;

    // (Not g_partner.visible: the tick clears it before this runs and draws set it again.)
    if (stamina_at == nullptr || gameTask != GAME_TASK_PLAY || g_partner.loaded_chara < 0 || g_partner.chara == nullptr) {
        return;
    }
    if (on_load && NetplayStateLoadPending()) {
        return;
    }

    ticks++;

    if (ticks == (on_load ? 1 : 60)) {
        sceVu0FVECTOR rotation;

        CharaMain.GetRotation(rotation);
        g_partner.pos[0] = CharaMain.pos[0] - 30.0f * cosf(rotation[1]);
        g_partner.pos[1] = CharaMain.pos[1];
        g_partner.pos[2] = CharaMain.pos[2] + 30.0f * sinf(rotation[1]);
        g_partner.pos[3] = 1.0f;
        g_partner.velocity[0] = g_partner.velocity[1] = g_partner.velocity[2] = 0.0f;
        g_partner.chara->frame->SetPosition(g_partner.pos);
        g_partner.chara->SetPosition(g_partner.pos);
        std::printf("coop: stamina test puts the partner at the player's right at (%.0f %.0f %.0f)\n", g_partner.pos[0], g_partner.pos[1],
                    g_partner.pos[2]);
        if (on_load) {
            NetplayTestSeriesMark();
        }
    }

    if (ticks != drink_at) {
        return;
    }

    PartnerSetAilment(AILMENT_STAMINA);
    std::printf("coop: stamina test gives partner %d stamina (ailments %#x)\n", g_partner.loaded_chara,
                (unsigned) UserStatus->ailments[g_partner.loaded_chara]);

    // coop-statusicons: DC_COOP_PLAYER_POISON=1 poisons the player's member on the same tick (BtSetStatusErr, as a
    // monster's bite does), so each bar has its own ailment.
    if (TestEnv("DC_COOP_PLAYER_POISON") != nullptr) {
        BtSetStatusErr(AILMENT_POISON);
        std::printf("coop: stamina test poisons player %d (ailments %#x)\n", (int) UserStatus->cur_chara,
                    (unsigned) UserStatus->ailments[UserStatus->cur_chara]);
    }
    std::fflush(stdout);
}

// coop-p2hurt: DC_COOP_TEST_P2HURT=case:N (test hook, x-p2-hurt): on the Nth tick of play, on both copies at the same
// tick, after (poison, corner) the partner was put 40 units to the player's right on the 60th: poison (her life 1, poisoned, the bite 10
// ticks off), blow (a monster's blow, kind 3, thrown the way with the most open floor), heavy (a heavy blow, kind 4), thirst (her water out) or corner (nothing) on her. For 300 ticks after
// it, every 10th, a line with her motion, its clock, her life, place and down count, so a test reads both copies' alike.
void HurtForTest() {
    // (coop-warmconvert, WS7: the case is a warm pair job's own, read again at each job, ServePool's job switches; and the
    // case's counts start again with it, as a fresh copy's. DC_COOP_MUTATE=p2hurt_job_kept, the twin: kept from the job before.)
    COOP_TEST_SWITCH(const char *, spec, TestEnv("DC_COOP_TEST_P2HURT"));
    static int         ticks = 0;
    static int         at = -1;
    static std::string kind;
    static float       thrown[2] = {0.0f, 0.0f};
    static int         p1_blow_at = -1;
    static int         hurt_job = TestJobGeneration();

    if (hurt_job != TestJobGeneration()) {
        hurt_job = TestJobGeneration();
        if (!TestJobResetKept("p2hurt_job_kept")) {
            ticks = 0;
            at = -1;
            kind.clear();
            thrown[0] = thrown[1] = 0.0f;
            p1_blow_at = -1;
        }
    }

    if (spec == nullptr || gameTask != GAME_TASK_PLAY || g_partner.loaded_chara < 0 || g_partner.chara == nullptr) {
        return;
    }

    if (at < 0) {
        const char *colon = std::strchr(spec, ':');

        kind = colon != nullptr ? std::string(spec, colon) : std::string(spec);
        at = colon != nullptr ? std::atoi(colon + 1) : 300;
    }

    // coop-rumble (fixture, Jep 11-04: no tick waits): the rumble case counts from the dun1-f1 fixture's load, the same
    // lockstep tick on both copies, and places her on its 2nd tick (the others on their 60th from the floor's start).
    // coop-fx51d: every case counts from a state file's load when the copy has one to load (x-p2-hurt -Fixture), and
    // places her on its 2nd tick then; with none, as before.
    // (coop-warmconvert: a warm pair's job loads its fixture at its begin, DC_QA_SERVE: counted from there as well)
    static const bool loads = TestEnv("DC_STATE_LOAD_AT") != nullptr || TestEnv("DC_QA_SERVE") != nullptr;

    if ((kind == "rumble" || loads) && NetplayStateLoadPending()) {
        return;
    }

    ticks++;

    // (Only for poison and corner: past the revive reach. A blow's case keeps her where the game put her, on open floor.)
    // 40 units out the first of eight ways from his right that his walk's MoveCheck reaches without a drop: a fixed spot
    // to his right was over a hole on some floor 1 layouts (train 49, floor fa0f4a02), the fall put her back 14 from him
    // and his still stand revived her. DC_COOP_MUTATE=p2hurt_fixed_spot: the fixed spot, as before.
    if (ticks == (kind == "rumble" || loads ? 2 : 60) && (kind == "poison" || kind == "corner" || kind == "rumble")) {
        sceVu0FVECTOR rotation;
        sceVu0FVECTOR spot = {0.0f, 0.0f, 0.0f, 1.0f};
        int           way = -1;

        CharaMain.GetRotation(rotation);

        for (int w = 0; w < 8 && way < 0; w++) {
            float yaw = rotation[1] - HALF_PI + (float) w * 0.7853982f;

            if (Mutated("p2hurt_fixed_spot")) {
                spot[0] = CharaMain.pos[0] + 40.0f * sinf(yaw);
                spot[1] = CharaMain.pos[1];
                spot[2] = CharaMain.pos[2] + 40.0f * cosf(yaw);
                way = 0;
                break;
            }

            sceVu0FVECTOR step = {sinf(yaw), 0.0f, cosf(yaw), 0.0f};
            bool          fell = false;

            sceVu0CopyVector(spot, CharaMain.pos);

            for (int t = 0; t < 40 && !fell; t++) {
                CCPoly       *polys;
                int           count = GatherPolys(spot, &polys);
                sceVu0FVECTOR moved;
                MoveCheckInfo info;

                MoveCheck(spot, step, moved, &info, polys, count, 1);
                step[1] = info.landed != 0 ? 0.0f : std::max(-10.0f, step[1] - 0.1f);
                sceVu0CopyVector(spot, moved);
                fell = spot[1] < CharaMain.pos[1] - 5.0f;
            }

            float dx = spot[0] - CharaMain.pos[0];
            float dz = spot[2] - CharaMain.pos[2];

            if (!fell && dx * dx + dz * dz >= 36.0f * 36.0f) {
                way = w;
            }
        }

        if (way >= 0) {
            sceVu0CopyVector(g_partner.pos, spot);
            g_partner.pos[3] = 1.0f;
            g_partner.velocity[0] = g_partner.velocity[1] = g_partner.velocity[2] = 0.0f;
            g_partner.chara->frame->SetPosition(g_partner.pos);
            g_partner.chara->SetPosition(g_partner.pos);
        }

        std::fprintf(stderr, "coop-p2hurt: test puts her way %d at (%.1f %.1f), player at (%.1f %.1f)\n", way, g_partner.pos[0], g_partner.pos[2],
                     CharaMain.pos[0], CharaMain.pos[2]);
    }

    if (ticks == at) {
        g_partner.invincible = 0;

        if (kind == "thirst") {
            // coop-p2hurt: her water out, as walking dry leaves it (her thirst's messages follow; DngMessMan's are his).
            UserStatus->water_now[g_partner.loaded_chara] = 0.0f;
        } else if (kind == "corner") {
            // Nothing on her: the corner icon case's ailments come from DC_COOP_STAMINA_AT and DC_COOP_PLAYER_POISON.
        } else if (kind == "poison") {
            SetLife(1);
            PartnerSetAilment(AILMENT_POISON);
            g_combat.poison_ticks = 170;
        } else {
            float position[4];

            sceVu0CopyVector(position, g_partner.pos);
            position[1] += 8.0f;

            int hit = NowColData->Set(position, 1, 6, 8.0f, 0.0f, 1, kind == "heavy" ? 4 : 3, 0, 0);

            // Thrown a unit a tick the way of the eight with the most open floor: each tried by 30 ticks of her walk's
            // MoveCheck (a wall stops it; a drop of over 5 rules the way out), the same on both copies.
            float best = -1.0f;

            for (int way = 0; way < 8; way++) {
                float         yaw = (float) way * 0.7853982f;
                sceVu0FVECTOR at;
                sceVu0FVECTOR step = {sinf(yaw), 0.0f, cosf(yaw), 0.0f};
                bool          fell = false;

                sceVu0CopyVector(at, g_partner.pos);

                for (int t = 0; t < 30 && !fell; t++) {
                    CCPoly       *polys;
                    int           count = GatherPolys(at, &polys);
                    sceVu0FVECTOR moved;
                    MoveCheckInfo info;

                    MoveCheck(at, step, moved, &info, polys, count, 1);
                    step[1] = info.landed != 0 ? 0.0f : std::max(-10.0f, step[1] - 0.1f);
                    sceVu0CopyVector(at, moved);
                    fell = at[1] < g_partner.pos[1] - 5.0f;
                }

                float dx = at[0] - g_partner.pos[0];
                float dz = at[2] - g_partner.pos[2];
                float open = fell ? 0.0f : sqrtf(dx * dx + dz * dz);

                if (open > best) {
                    best = open;
                    thrown[0] = sinf(yaw);
                    thrown[1] = cosf(yaw);
                }
            }

            if (hit >= 0) {
                NowColData->hit[hit].velocity[0] = thrown[0];
                NowColData->hit[hit].velocity[1] = 0.0f;
                NowColData->hit[hit].velocity[2] = thrown[1];
            }
        }

        std::fprintf(stderr, "coop-p2hurt: test %s at tick %d on member %d, life %d/%d at (%.1f %.1f) thrown (%.2f %.2f), player at (%.1f %.1f)\n", kind.c_str(), ticks,
                     g_partner.loaded_chara, Life(), MaxLife(), g_partner.pos[0], g_partner.pos[2], thrown[0], thrown[1], CharaMain.pos[0],
                     CharaMain.pos[2]);
    }

    // coop-rumble: her blow's window (30 ticks: a blow shakes for 22 from the tick it lands) over, and once her flinch is
    // over (game state, alike on both copies) a blow on him, with his window's end 30 ticks after it; each copy's
    // controller lines between show whose hit shook it, and the test stops on the second window's line.

    if (kind == "rumble" && ticks == at + 30) {
        std::fprintf(stderr, "coop-p2hurt: test rumble: her window over at tick %d\n", ticks);
    }

    if (kind == "rumble" && p1_blow_at >= 0 && ticks == p1_blow_at + 30) {
        std::fprintf(stderr, "coop-p2hurt: test rumble: his window over at tick %d\n", ticks);
        std::fflush(stdout);
    }

    if (kind == "rumble" && p1_blow_at < 0 && ticks > at + 30 && g_combat.hurt == 0) {
        p1_blow_at = ticks;
        float position[4];

        sceVu0CopyVector(position, CharaMain.pos);
        position[1] += 8.0f;

        int hit = NowColData->Set(position, 1, 6, 8.0f, 0.0f, 1, 3, 0, 0);

        std::fprintf(stderr, "coop-p2hurt: test rumble's blow on the player at tick %d (hit %d), player at (%.1f %.1f), she at (%.1f %.1f)\n", ticks, hit,
                     CharaMain.pos[0], CharaMain.pos[2], g_partner.pos[0], g_partner.pos[2]);
    }

    if (ticks > at && ticks <= at + 300 && (ticks - at) % 10 == 0) {
        std::fprintf(stderr, "coop-p2hurt: test +%d: motion %d time %.1f life %d/%d down %d hurt %d at (%.1f %.1f)\n", ticks - at, g_partner.motion,
                     g_partner.chara->motion_type.state.time, Life(), MaxLife(), g_partner.down, g_combat.hurt, g_partner.pos[0],
                     g_partner.pos[2]);

        if (ticks == at + 300) {
            std::fflush(stdout); // the mode lines (stdout), before the test stops the copy
        }
    }
}

// Beside the player: at its right hand, else its left, behind or before it, the first the player could walk to.
void PutBesidePlayer() {
    sceVu0FVECTOR rotation;

    CharaMain.GetRotation(rotation);
    static const float kSides[4] = {0.0f, PI, HALF_PI, -HALF_PI};

    for (float side : kSides) {
        float yaw = rotation[1] + side;

        g_partner.pos[0] = CharaMain.pos[0] - kSpawnDistance * cosf(yaw);
        g_partner.pos[1] = CharaMain.pos[1];
        g_partner.pos[2] = CharaMain.pos[2] + kSpawnDistance * sinf(yaw);

        if (PathClear(CharaMain.pos, g_partner.pos)) {
            break;
        }
    }

    g_partner.pos[3] = 1.0f;
    g_partner.velocity[0] = g_partner.velocity[1] = g_partner.velocity[2] = 0.0f;
    g_partner.chara->frame->SetPosition(g_partner.pos);
}

void FollowAfterEvent(bool in_event) {
    if (in_event) {
        if (!g_partner.event_held) {
            g_partner.event_held = true;
            std::memcpy(g_partner.event_from, CharaMain.pos, sizeof g_partner.event_from);
            std::memcpy(g_partner.event_last, CharaMain.pos, sizeof g_partner.event_last);
            return;
        }

        // coop-startcell (g-minimap red: P2's start cell never shown): a floor's opening scene first stands the player where
        // the floor loaded him ((200 0 150) on a --jump) and cuts him to the stairs a few ticks in. She was placed beside the
        // first spot and stood there, drawn (CoopWorldsPartnerStandsInScene), the whole scene, in a room nobody entered,
        // until the follow at its end. A cut (the player more than kEventFollow from his last tick's spot) while she stands
        // through the scene puts her beside him again. With a camera each the opening's own placing has her
        // (StandBesideArrival, FloorIntro), and this is not called. DC_COOP_MUTATE=scene_cut (twin): left where she was.
        float dx = CharaMain.pos[0] - g_partner.event_last[0];
        float dz = CharaMain.pos[2] - g_partner.event_last[2];
        float jump = sqrtf(dx * dx + dz * dz);

        std::memcpy(g_partner.event_last, CharaMain.pos, sizeof g_partner.event_last);

        if (jump > kEventFollow && g_partner.chara != nullptr && CoopWorldsPartnerStandsInScene() && !Mutated("scene_cut")) {
            PutBesidePlayer();
            std::fprintf(stderr, "coop: the scene cut the player %.0f; p2 stands beside him at (%.0f %.0f %.0f)\n", jump, g_partner.pos[0],
                         g_partner.pos[1], g_partner.pos[2]);
        }

        return;
    }

    WarpForTest();
    CrystalForTest(); // coop: minimap2
    StaminaForTest(); // coop-hudicon
    HurtForTest();    // coop-p2hurt

    if (!g_partner.event_held) {
        return;
    }

    g_partner.event_held = false;
    float dx = CharaMain.pos[0] - g_partner.event_from[0];
    float dz = CharaMain.pos[2] - g_partner.event_from[2];
    float moved = sqrtf(dx * dx + dz * dz);

    if (moved <= kEventFollow || g_partner.chara == nullptr || Mutated("follow")) {
        return;
    }

    PutBesidePlayer();
    std::printf("coop: the player moved %.0f in an event; p2 follows to (%.0f %.0f %.0f)\n", moved, g_partner.pos[0],
                g_partner.pos[1], g_partner.pos[2]);
}

} // namespace

void CoopStep(bool playing) {
    CoopPerfScope perf(kCoopPerfStep); // coop: perf
    // The camera stepped with the distance CoopBeforeMotion widened; DunMoveChara gets its own back.
    if (g_partner.camera_widened) {
        NowCamera__3->SetDistance(g_partner.camera_distance);
        g_partner.camera_widened = false;
    }

    PresenceTrace("dungeon", g_partner.visible); // coop-p2absent

    // coop-floorjoin2 (DC_COOP_DEBUG): once a second of floor ticks, whether the last tick left the partner to be drawn and
    // where (the draw itself does not run headless), with the renderer's texture misses so far, for x-floor-join -Case
    // late's "the partner is visible on both copies" after a snapshot join; and the floor's events as they begin and end
    // (BtEventMode, the running script), so a test sees an opening scene run. On stderr, unbuffered, as the catch-up's
    // lines are: the tests cut their logs at those lines.
    static int  ticks = 0;
    // (coop-warmreset, WS7: put back at each warm-pair job as a fresh copy has it; trace_once_kept twin: kept)
    COOP_TEST_TRACE_RESET(ticks, 0);
    static bool was_event = false;
    static int  event_began = 0;
    static int  event_script = -1;
    const bool  event = BtEventMode != 0;

    if (CoopDebug() && ++ticks % 60 == 0) {
        std::fprintf(stderr, "coop-debug: draw %d: partner %s at (%.0f %.0f %.0f), player at (%.0f %.0f %.0f), task %d, texture misses %lu\n", ticks,
                    g_partner.visible ? "drawn" : "hidden", g_partner.pos[0], g_partner.pos[1], g_partner.pos[2], CharaMain.pos[0],
                    CharaMain.pos[1], CharaMain.pos[2], (int) gameTask, dc::render::PortTextureMisses());
    }
    if (CoopDebug() && event != was_event) {
        if (event) {
            event_began = ticks;
            event_script = (int) BtEventInfo.running_script_no;
            std::fprintf(stderr, "coop-debug: floor event begins: script %d, task %d, floor %d, at tick %d\n", event_script, (int) gameTask,
                         UserStatus != nullptr ? (int) UserStatus->cur_floor + 1 : 0, ticks);
        } else {
            std::fprintf(stderr, "coop-debug: floor event ends: script %d ran %d ticks, task %d, floor %d, at tick %d\n", event_script, ticks - event_began,
                         (int) gameTask, UserStatus != nullptr ? (int) UserStatus->cur_floor + 1 : 0, ticks);
        }
        was_event = event;
    }

    StandInWeaponStep(); // coop: p2weapon
    ArrivalTrailTick();  // coop-arrivalfull: this copy's own arrival, recorded
    ArrivalWalkTick();   // coop-arrivalfull: the arriver's, played again
    g_partner.visible = false;
    PartnerCameraTick(); // coop: cameras
    CoopWorldsSimTick(); // coop-worlds: its test exit
    KeyRingTest();       // coop-keyring: its test hook, apart too
    ItemSceneTest();     // pt1111c: the item scene's test hook
    GateOpenTick();      // coop-gateopen: a gate opened once stays open, apart too
    KeyDoorTick();       // coop-keydoor: key doors opening beside the game, apart too

    // coop: gaps2
    if (Enabled() && BtAllDrawFlag != 0 && NowDngMap != nullptr && CharaFrame != nullptr) {
        FollowAfterEvent(BtEventMode != 0 && !FloorIntro());
    }

    // coop: cameras (FloorIntro: the partner stands through the floor's opening)
    // coop-worlds: apart, the partner is on neither copy. coop-dungeonjoin: but for her own floor merge (her copy was there first),
    // from its start: she is the partner already while the other copy catches up (CoopWorldsFloorBecomePartner).
    // coop-b1lesson (Jep: "neither P2 or the old man ... show up"): and the floor's opening scene both copies play together
    // (B1's elder lesson): she stands there, drawn, through it (CoopWorldsPartnerStandsInScene).
    const bool scene_stand = CoopWorldsPartnerStandsInScene();
    // coop-sceneowner (Jep's 2026-11-04 playtest of the fix: "When P2 starts the cutscene and p1 joins, p1 turns into xiao"): her
    // joined floor scene plays on with her copy's player, her member drawn as it, and the partner stood beside it as her member
    // too (PickPartner: the party's next, hers): two of her, nothing of his. He is not on the floor until her scene's end (the
    // handover places him): no partner through it. DC_COOP_MUTATE=scene_partner_shown (twin): she stands beside herself, as before.
    const bool her_scene = CoopWorldsSceneOwner() == 2 && !Mutated("scene_partner_shown");

    if (!Enabled() || BtAllDrawFlag == 0 || (BtEventMode != 0 && !FloorIntro() && !scene_stand) || NowDngMap == nullptr || CharaFrame == nullptr ||
        (CoopWorldsApart() && !NetplayStayerMerge()) || her_scene) {
        return;
    }

    static int log_wait = 0;

    if (--log_wait <= 0) {
        LogPads();
        log_wait = 120;
    }

    // coop-party: test hook (DC_COOP_RESET_AT=N): on the Nth tick of play, a session reset, then the
    // state both copies must agree on after it.
    static const char *reset_at = TestEnv("DC_COOP_RESET_AT");
    static int         reset_ticks = 0;

    if (reset_at != nullptr && playing && ++reset_ticks == std::atoi(reset_at)) {
        CoopResetForSession();
        std::printf("coop: session state loaded %d choice %d down %d revive %d busy %d wheel %d editor %d lives %d %d %d %d %d %d\n",
                    g_partner.loaded_chara, g_partner.choice, g_partner.down, g_partner.revive, g_partner.busy, (int) WheelOpen(),
                    (int) CoopGeoramaPartnerHolds(), g_partner.stand_in_life[0], g_partner.stand_in_life[1], g_partner.stand_in_life[2],
                    g_partner.stand_in_life[3], g_partner.stand_in_life[4], g_partner.stand_in_life[5]);
    }

    NewFloorCheck();     // coop-party

    // coop-party: no partner without a player 2 (solo, or an online host still waiting for its guest);
    // a pad or link that drops out for a moment changes nothing, and one who comes back after a real
    // absence finds the partner where it was left (StepPresence). Only the twin re-places it.
    bool rejoined = false;

    if (!StepPresence(&rejoined)) {
        return;
    }

    if (rejoined && g_partner.chara != nullptr && g_partner.loaded_chara >= 0) {
        PlaceBesidePlayer("rejoin (twin)");
    }

    ZoneStep(); // coop-pt1111k: a character-limited floor's member kept played (and its test hooks)

    int chara = PickPartner();

    if (g_partner.chara == nullptr || g_partner.loaded_chara != chara || BlockEmpty()) {
        if (!Load(chara)) {
            g_partner.loaded_chara = -1;
            return;
        }
    }

    CCharacter *partner = g_partner.chara;

    // coop-menus: P2 has left the floor (her overlay's System page); not stepped, drawn or targeted.
    if (MenuPartnerAway()) {
        return;
    }

    StandBesideArrival(); // coop: cameras

    if (ArrivalWalkPartner()) { // coop-arrivalfull: her arrival plays again after her catch-up
    } else if (playing && !scene_stand) {
        Walk();
    } else if (!FloorIntro() && g_partner.down == 0 && !g_partner.attacking && g_partner.motion != 0 &&
               (!PartnerPresenting() || Mutated("presreset"))) { // coop: cameras
        // coop-atlachest (Jep's 2026-11-04 playtest: "P1 opened a atla while P2 was opening a chest and P2 got warped inside the
        // chest"): his Cross on the Atla gives one tick of GAME_TASK_ATRA_GET_INIT (not played through), and this stood her
        // still mid-opening: her chest's motion went to standing while her frame stays on the box (PartnerFramePlace), so
        // her body stood in the box until her hold's motion. Her own presentation's motion (chest, Atla, gate key) is
        // kept. DC_COOP_MUTATE=presreset (twin): stood still, as before.
        PlayMotion(0, 0);
    }

    sceVu0FVECTOR rotation;

    sceVu0FVECTOR place;

    partner->frame->GetRotation(rotation);
    PartnerFramePlace(place); // coop-openpos: on the box while she opens a chest
    partner->SetPosition(place);
    partner->SetRotation(rotation[0], rotation[1], rotation[2]);
    partner->Step();
    partner->ShadowStep();

    // coop-party: the cloth the member wears (Goro's cape) hangs from its body, as the game steps the
    // player's each tick (CharaMain.ClothStep). Never stepped, it stayed where it was made and drew as a
    // flat square on Goro's back. DC_COOP_MUTATE=cloth leaves it unstepped for the test's twin.
    if (!Mutated("cloth")) {
        partner->ClothStep(0);
    }

    g_partner.visible = true;
    StepPartnerCamera(playing); // coop: cameras
    CamTestTick();              // coop: cameras
    ActorTableTrace();          // coop-players: test hook
    InventoryStep(playing);     // coop-inventory: P1's send (R3) and its test bot
    DebugLog();
    ActorTestTick();            // coop-actorswap: the swap-roundtrip self-test
}

bool CoopPartnerTarget(float *out_position, float *out_heading, int *out_since_attack, int *out_chara) {
    if (!Enabled() || !g_partner.visible || g_partner.down > 0) {
        return false;
    }

    sceVu0CopyVector(out_position, g_partner.pos);

    if (out_heading != nullptr) {
        sceVu0CopyVector(out_heading, g_partner.heading);
    }

    if (out_since_attack != nullptr) {
        *out_since_attack = g_partner.since_attack;
    }

    if (out_chara != nullptr) {
        *out_chara = g_partner.loaded_chara;
    }

    return true;
}

void CoopResetForSession() {
    // Party (coop_party.cpp): the partner unloaded, no pick, nobody down, stand-in lives full.
    // The DC_COOP_MUTATE=sessionreset twin skips it, and its test must then fail.
    if (!Mutated("sessionreset")) {
        PartyResetForSession();
    }

    ActorSequencesReset();        // coop-actorswap: no retail sequence held
    WheelResetForSession();       // pad 2's wheel closed (coop_wheel.cpp)
    KeyPickResetForSession();     // coop-keypick: her key picker closed (coop_interact.cpp)
    UsePromptResetForSession();   // coop-p2prompt: no prompt open (coop_useprompt.cpp)
    PromptResetForSession();      // no swap question waiting (coop_prompt.cpp)
    FullMenuResetForSession();    // the game's menu not open beside the world (coop_fullmenu.cpp)
    TownResetForSession();        // the town partner reloads on the next town tick (coop_town.cpp)
    CoopGeoramaResetForSession(); // the town editor back to player 1 (coop_georama.cpp)
    CoopP2GeoramaResetForSession(); // coop-town: player 2's own Georama closed
    CoopP2TownMenuResetForSession(); // coop-town: player 2's own menu closed

    CombatResetForSession(); // combat as a fresh boot has it (coop_combat.cpp, combat thread)
    CameraResetForSession(); // the cameras as they boot (coop_camera.cpp, cameras thread)
    MenuResetForSession();   // coop-menus: both overlays closed, P2 on the floor
    g_partner.event_held = false; // no event under way (FollowAfterEvent, gaps2)
    // Audio (coop_audio.cpp): its reset, if it keeps any, goes here.
}

void CoopResetForJob() {
    // (job_menu_kept, warm-repeat-selftest's -MenuTwin: the last job's menu is kept, so its session reset, which resets the menus
    // too, is left out with it, as before this reset)
    if (!TestJobResetKept("job_session_kept") && !TestJobResetKept("job_menu_kept")) {
        CoopResetForSession();
    }
    CoopWorldsResetForJob(); // (its own twins: job_gate_kept, job_trace_kept)
    if (!TestJobResetKept("job_scratch_kept")) {
        PartyResetForJob();
        AudioResetForJob();
        MessageResetForJob();
        CamtestResetForJob();
    }
}

std::string CoopJobLeftovers() {
    char vote[64];
    char line[256];
    int  loaded = 0, shots = 0, flashes = 0;

    CoopWorldsJobLeftovers(vote, sizeof vote);
    RingJobLeftovers(&loaded, &shots, &flashes);
    std::snprintf(line, sizeof line, "wheel opens %d stepped %lld, targets on p2 %d, ring %d shots %d flashes %d, %s, presence %d, voice %d, dry %d",
                  WheelJobOpens(), WheelJobStepped(), TargetsOnPartner(), loaded, shots, flashes, vote, PartyJobPresenceCalls(), VoiceJobChara(),
                  MessageJobDry());
    return line;
}
