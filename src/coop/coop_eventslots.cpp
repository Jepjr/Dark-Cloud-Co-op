// coop-eventslots (Jep's 11-11 playtest item 1: both players use Norune's fishing board at once, each with their own event,
// camera, menus and fishing). Design: debug\scratch\eventslots\design.md.

#include "coop/game_ns.hpp"
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <string>
#include <memory>
#include <new>
#include <optional>
#include <utility>

#include "boxvu0.hpp" // (coop-dayhouses: DistVector, the test line)
#include "camerafollow.hpp"
#include "character.hpp"
#include "clsmes.hpp"
#include "collision.hpp"
#include "dataalloc.hpp"
#include "dataread.hpp"
#include "ebattle.hpp"
#include "edit.hpp"
#include "editloop.hpp"
#include "editloop3.hpp"
#include "editmapscript.hpp" // GameMode, ED_MODE_*
#include "editground.hpp"
#include "gameutil.hpp"
#include "mapparts.hpp" // (coop-pondfish: a pond's Georama part)
#include "fish.hpp"
#include "fishing.hpp"
#include "gamepad.hpp"
#include "gfx/gfx.hpp"
#include "mglib.hpp"
#include "npcharacter.hpp" // (coop-daytrans: EdVillager)
#include "platform/input.hpp"
#include "runscript.hpp"
#include "savedata.hpp"
#include "itemdata.hpp"
#include "sysmes.hpp"
#include "texture.hpp"
#include "textureanime.hpp"

#include "coop/coop.hpp"
#include "coop/coop_actor.hpp"
#include "coop/coop_audio.hpp"
#include "coop/coop_core.hpp"
#include "coop/coop_georama.hpp"
#include "coop/coop_inventory.hpp"
#include "coop/coop_message.hpp"
#include "coop/coop_ready.hpp"
#include "coop/coop_town.hpp"
#include "coop/coop_worlds.hpp"
#include "coop/netplay.hpp"

using namespace coop;

namespace dc::game {
extern CRunScript EdEventScript; // editloop3.cpp
extern int        EdInteriorFlag;
extern s32        mode; // the game's mode (2: the town, its rooms)
extern s32        MapNo; // coop-joinload: the map (the town a world slot runs in)
extern CCameraFollow EventCamera; // editloop.cpp
void EdEventStateRanges(void (*add)(void *at, std::size_t bytes, const char *name, void *context), void *context); // editloop3.cpp (coop-eventslots)
void RunScriptStateRanges(const CRunScript *script, void (*add)(void *at, std::size_t bytes, const char *name, void *context), void *context); // runscript.cpp
void FishingStateRanges(void (*add)(void *at, std::size_t bytes, const char *name, void *context), void *context);   // fishing.cpp (coop-eventslots)
void EdFishingStateRanges(void (*add)(void *at, std::size_t bytes, const char *name, void *context), void *context); // ebattle.cpp (coop-eventslots)
struct EdFishingWeights { // (ebattle.cpp declares it alike)
    float float_weight;
    float hook_weight;
};
void EdFishingStepIn(CCharacter *chara, CCameraFollow *camera, float *pos, EdFishingWeights *out); // ebattle.cpp: EdMoveChara's fishing, split out
void EdFishingLineStep(CCharacter *chara, const EdFishingWeights &weights);
void SysMesStateRanges(void (*add)(void *at, std::size_t bytes, const char *name, void *context), void *context);   // sysmes.cpp (coop-eventslots)
void EdFadeStep(); // editloop3.cpp (coop-daytrans: an event's fade, stepped once a tick)
int  EdVillagerLoadOne(int i, CDataAlloc2<1> *arena); // editloop3.cpp (coop-daytrans: a villager loaded late)
void EdVillagerPlaceOne(int i, CEditGround *ground);  // editloop3.cpp (coop-daytrans: and placed)
void EdSysMesStateRanges(void (*add)(void *at, std::size_t bytes, const char *name, void *context), void *context); // edit.cpp (coop-eventslots)
extern int FishSlots;            // fishing.cpp (coop-pondfish: a fishing set's fish slots)
int        FishingRollKind(int spot); // fishing.cpp (coop-pondfish: retail's roll of one fish's kind for a spot)
} // namespace dc::game

// A player's parked event slot: the town's one event engine's whole state (EdEventStateRanges, the event script's VM stacks) as
// bytes, exchanged with the live statics around that player's own event's steps and draw while another player's own event holds
// the live engine. Static, so the snapshot's state set (snapregions) carries a slot through a merge or resync like the rest.
namespace {
struct Range {
    void       *at;
    std::size_t bytes;
    const char *name;
    std::size_t offset; // in a slot's bytes
};

constexpr int         kMostRanges = 96;
constexpr std::size_t kSlotBytes = 512 * 1024;

Range       g_ranges[kMostRanges] = {};
int         g_range_count = 0;
std::size_t g_range_bytes = 0;
bool        g_ranges_full = false; // the VM's stacks were there when the list was made (the town's first script load)

alignas(16) unsigned char g_slot_bytes[coop::kMaxPlayers][kSlotBytes] = {};

void AddRange(void *at, std::size_t bytes, const char *name, void *) {
    if (g_range_count >= kMostRanges || g_range_bytes + bytes > kSlotBytes) {
        std::fprintf(stderr, "coop-eventslots: the event state set does not fit a slot (%d ranges, %zu + %zu bytes, at %s)\n", g_range_count,
                     g_range_bytes, bytes, name);
        std::fflush(stderr);
        std::abort();
    }
    g_ranges[g_range_count++] = {at, bytes, name, g_range_bytes};
    g_range_bytes += (bytes + 15) & ~static_cast<std::size_t>(15);
}

// The list, made once the event script has its VM stacks (they are part of it); returns false before that.
bool Ranges() {
    if (g_ranges_full) {
        return true;
    }
    g_range_count = 0;
    g_range_bytes = 0;
    EdEventStateRanges(AddRange, nullptr);
    const int before = g_range_count;

    RunScriptStateRanges(&EdEventScript, AddRange, nullptr);
    g_ranges_full = g_range_count - before == 3; // (stack, calls, result)
    // The town's event windows (EdEventInfo.messages: EditMes1, EditEventMes1) and the system message window an event's boxes
    // use: each event's own text, page and cursor.
    AddRange(&EditMes1, sizeof EditMes1, "EditMes1", nullptr);
    AddRange(&EditEventMes1, sizeof EditEventMes1, "EditEventMes1", nullptr);
    if (g_ranges_full) {
        std::fprintf(stderr, "coop-eventslots: the event state set: %d ranges, %zu bytes a slot\n", g_range_count, g_range_bytes);
        std::fflush(stderr);
    }
    return g_ranges_full;
}

// Slot k's bytes exchanged with the live statics (twice puts everything back). DC_COOP_MUTATE=slot_swap_drops (twin): the last
// range is left out of every second exchange (the swap out), so a swap in and out leaves the slot's bytes live.
void Swap(int k) {
    static bool out = false;
    const int   count = Mutated("slot_swap_drops") && out ? g_range_count - 1 : g_range_count;

    out = !out;
    for (int i = 0; i < count; i++) {
        const Range &r = g_ranges[i];
        auto        *live = static_cast<unsigned char *>(r.at);
        auto        *kept = g_slot_bytes[k] + r.offset;

        for (std::size_t b = 0; b < r.bytes; b++) {
            std::swap(live[b], kept[b]);
        }
    }
}

std::uint64_t Hash(const unsigned char *at, std::size_t bytes, std::uint64_t h) {
    for (std::size_t i = 0; i < bytes; i++) {
        h = (h ^ at[i]) * 0x100000001b3ull;
    }
    return h;
}

// The live set's hash, and the slots' bytes' hash.
std::uint64_t LiveHash() {
    std::uint64_t h = 0xcbf29ce484222325ull;

    for (int i = 0; i < g_range_count; i++) {
        h = Hash(static_cast<const unsigned char *>(g_ranges[i].at), g_ranges[i].bytes, h);
    }
    return h;
}

std::uint64_t SlotsHash() {
    return Hash(&g_slot_bytes[0][0], sizeof g_slot_bytes, 0xcbf29ce484222325ull);
}

void FishSetsTripForTest(); // (below: the fishing and message sets are declared there)

// Test hook (DC_COOP_TEST_EVENTSLOT_TRIP=1): the swap's round trip, nothing run between: each slot swapped in and out at once,
// on the town's first walk tick after the fixture's load (idle) and on the 30th tick of an own town event (live engine busy):
// the live set and every slot's bytes as they were. A slot never used holds zeros, so a range not put back shows in both hashes.
void TripForTest() {
    static const bool on = TestEnvOn("DC_COOP_TEST_EVENTSLOT_TRIP");
    static bool       idle_done = false;
    static bool       event_done = false;
    static long long  event_from = -1;

    if (!on || EdInteriorFlag != 0 || (TestEnv("DC_STATE_LOAD_AT") != nullptr && !NetplayStateFileLoaded()) || !Ranges()) {
        return;
    }
    const int owner = CoopTownOwnTownEvent();
    const char *when = nullptr;

    if (!idle_done && owner == 0) {
        idle_done = true;
        when = "idle";
    } else if (!event_done && owner != 0) {
        if (event_from < 0) {
            event_from = NetplayLinkTick();
        } else if (NetplayLinkTick() - event_from >= 30) {
            event_done = true;
            when = "in an own town event";
        }
    }
    if (when == nullptr) {
        return;
    }
    if (std::strcmp(when, "idle") == 0) {
        CoopTownWalkersForTest(); // (design for three: every index's walker as the town's table has it)
        FishSetsTripForTest();    // (and every player's fishing and message sets' own round trip)
    }
    for (int k = 0; k < coop::kMaxPlayers; k++) {
        const std::uint64_t live = LiveHash();
        const std::uint64_t slots = SlotsHash();

        Swap(k);
        Swap(k);
        const std::uint64_t live_after = LiveHash();
        const std::uint64_t slots_after = SlotsHash();

        std::fprintf(stderr, "coop-eventslots: test: trip %s, player %d's slot: %d ranges, %zu bytes; live %016llx -> %016llx, slots %016llx -> %016llx: %s at link tick %lld\n",
                     when, k + 1, g_range_count, g_range_bytes, (unsigned long long) live, (unsigned long long) live_after, (unsigned long long) slots,
                     (unsigned long long) slots_after, live == live_after && slots == slots_after ? "as they were" : "CHANGED", (long long) NetplayLinkTick());
    }
    std::fflush(stderr);
}
} // namespace

// Once a town walk tick (CoopTownStep), every copy.
void CoopEventSlotsStep() {
    TripForTest();
}

// coop-eventslots B: a player's own town event while another player's own event holds the town's live engine, or while that player's
// fishing asks for one (its Circle: quit, 133; its Square: the bait, 134). The live one runs as before (retail's event mode, its owner's
// camera and windows, the others walking beside it). Another player's Cross at a story-free map event (CoopTownPlayerEventAt) starts
// that player's event in their parked slot: the live engine's state swapped out, the slot's in, retail's own EdEventInit / EdEventMode
// run on that player's body, pad, bag, sounds and camera, swapped back. Its windows are drawn from the slot on every copy (they step
// as they draw), shown on its player's copy alone, and that copy's screen shows the slot's camera. It runs to its own end in its slot,
// whatever the live engine does meanwhile. DC_COOP_MUTATE=eventslots_off (twin): no parked start, the one engine as before.
//
// coop-eventslots C (Jep's 11-11: each player fishes on their own, the world running on, their fishing on their own screen): together
// the town never goes into retail's fishing mode. A player's own event that answers "fish" (_GOTO_FISHING) starts that player's own
// fishing: retail's fishing step (EdFishingStepIn / EdFishingLineStep, split out of EdMoveChara) runs on their body and camera with
// their own fishing set (FishingStateRanges, EdFishingStateRanges: the line, hook, float, bait, fish, rod and the step's counters)
// swapped in, their pad, bag and help message. Each player's own event and fishing use their own work arena (kArenaBytes) and their
// own texture blocks (CoopEventTexBlock: the fish, the fish caught, the bait, the event's 45 to 49), so the town's villagers are never
// displaced and never loaded again (CoopEventKeepsVillagers). The fish and the catch's window are drawn on the fisher's copy alone;
// the body, rod, line and float on every copy. Cause of the crash found on the way (P1 fishing together, the clock's period change):
// retail's fishing carves the villagers' buffer (_CLEAR_VILLAGER_BUFF) and loads the fishing body and fish into it; together the clock
// runs on, and the period change's villager load (_LOAD_VILLAGER: EdVillagerBuffer.used = 0) wrote over the fisher's body and both
// copies crashed in its step. Now the town's buffers are put back at every own event's end and the fisher's data is in their arena.
namespace {
constexpr std::size_t kArenaBytes = 4u << 20; // a player's own events' and fishing's work arena (the live one's: the town's EdWorkBuffer rest)
constexpr int         kFishQuit = 0x85;       // retail's fishing: Circle queues event 133 (quit), Square 134 (the bait)
constexpr int         kFishBait = 0x86;

struct Parked {
    bool      on = false;
    int       event = 0;
    long long tick = 0;
    CGamePad  pad{};
    bool      cross_was = false;
    bool      camera_made = false;
    int       frames_hidden = 0; // this copy: its pictures drawn out of sight (another player's), or shown (this copy's player's)
    int       frames_shown = 0;
    int       steps = 0;
    int       queued = 0; // an event of theirs to start at the next step (their fishing's Circle or Square)
    long long pressed = -1; // the link tick their Cross came with nothing live (a tie the other's read took: theirs at the next step)
};

// The event engine's buffers an own event of a player carves (the villagers', the event's, its extra), kept between that player's
// events while they fish (the fishing's data lies in them), and the town's own put back at each own event's end.
struct Buffers {
    bool           valid = false;
    CDataAlloc2<1> villager{-1}, event{-1}, extra{-1};
};

// A player's own fishing: its set (as bytes), its catch window, its pad.
constexpr std::size_t kFishBytes = 16 * 1024;
struct Fishing {
    bool      on = false;
    bool      set_made = false;
    bool      window_made = false;
    long long tick = 0;
    int       steps = 0;
    int       frames_hidden = 0;
    int       frames_shown = 0;
    CGamePad  pad{};
    alignas(16) unsigned char set[kFishBytes];
    alignas(16) unsigned char window[sizeof(ClsMes)];
};

Parked  g_parked[coop::kMaxPlayers];
Buffers g_buffers[coop::kMaxPlayers];
Buffers g_town_buffers; // the town's, while a live own event uses a player's arena
Fishing g_fishing[coop::kMaxPlayers];
alignas(64) unsigned char g_parked_arena[coop::kMaxPlayers][kArenaBytes];
CDataAlloc2<1>            g_parked_alloc[coop::kMaxPlayers] = {CDataAlloc2<1>(-1), CDataAlloc2<1>(-1), CDataAlloc2<1>(-1),
                                                               CDataAlloc2<1>(-1), CDataAlloc2<1>(-1), CDataAlloc2<1>(-1)};
alignas(16) unsigned char g_parked_camera[coop::kMaxPlayers][sizeof(CCameraFollow)];
int g_stepping = 0;     // the player (1 up) whose parked event steps or draws now
int g_event_player = 0; // the player (1 up) whose own event or fishing runs now (its arena, blocks, villagers kept)
int g_live_own = 0;     // the player (1 up) whose own event the live engine runs on their arena (the town's buffers saved)
int g_live_in = 0;      // inside that event's step (CoopEventSlotsLiveIn/Out)

// Test hook (DC_COOP_TEST_PARKED_FRAMES=1): as a parked event ends, the frames this copy drew its pictures out of sight and shown.
void FramesForTest(int k) {
    static const bool on = TestEnvOn("DC_COOP_TEST_PARKED_FRAMES");

    if (on) {
        std::fprintf(stderr, "coop-eventslots: test: player %d's parked event's pictures %d frames out of sight, %d shown on this copy\n", k + 1,
                     g_parked[k].frames_hidden, g_parked[k].frames_shown);
    }
}

CCameraFollow *SlotCamera(int k) {
    return std::launder(reinterpret_cast<CCameraFollow *>(g_parked_camera[k]));
}

bool Together() {
    return NetplayHasPlayer() && CoopWorldsLinkedTogether() && !CoopWorldsApart();
}

int  SysIn(int player); // (each player's own message slot, below)
void SysOut(int token);

// The fishing set's ranges (fishing.cpp's globals, the step's counters).
Range       g_fish_ranges[64] = {};
int         g_fish_range_count = 0;
std::size_t g_fish_range_bytes = 0;

void AddFishRange(void *at, std::size_t bytes, const char *name, void *) {
    if (g_fish_range_count >= 64 || g_fish_range_bytes + bytes > kFishBytes) {
        std::fprintf(stderr, "coop-eventslots: the fishing set does not fit (%d ranges, %zu + %zu bytes, at %s)\n", g_fish_range_count, g_fish_range_bytes, bytes, name);
        std::fflush(stderr);
        std::abort();
    }
    g_fish_ranges[g_fish_range_count++] = {at, bytes, name, g_fish_range_bytes};
    g_fish_range_bytes += (bytes + 15) & ~static_cast<std::size_t>(15);
}

void FishRanges() {
    if (g_fish_range_count > 0) {
        return;
    }
    FishingStateRanges(AddFishRange, nullptr);
    EdFishingStateRanges(AddFishRange, nullptr);
    std::fprintf(stderr, "coop-eventslots: the fishing set: %d ranges, %zu bytes a player\n", g_fish_range_count, g_fish_range_bytes);
}

void SwapBytes(void *live, void *kept, std::size_t bytes) {
    auto *a = static_cast<unsigned char *>(live);
    auto *b = static_cast<unsigned char *>(kept);

    for (std::size_t i = 0; i < bytes; i++) {
        std::swap(a[i], b[i]);
    }
}

// Player k's fishing set (and, for its step and draw, its catch window: EditMes1) exchanged with the live one. A set never used
// starts as the live one at rest (nobody's fishing). DC_COOP_MUTATE=fishing_one_set (twin): no exchange, one fishing for all.
void FishSwap(int k, bool window) {
    coop::FishRecordSwap(k); // (their own fishing points and log, when each player's: Jep's game.coop_fishing_shared off)
    if (Mutated("fishing_one_set")) {
        return;
    }
    FishRanges();
    Fishing &f = g_fishing[k];

    if (!f.set_made) {
        for (int i = 0; i < g_fish_range_count; i++) {
            std::memcpy(f.set + g_fish_ranges[i].offset, g_fish_ranges[i].at, g_fish_ranges[i].bytes);
        }
        f.set_made = true;
    }
    for (int i = 0; i < g_fish_range_count; i++) {
        SwapBytes(g_fish_ranges[i].at, f.set + g_fish_ranges[i].offset, g_fish_ranges[i].bytes);
    }
    if (window) {
        if (!f.window_made) {
            std::memcpy(f.window, static_cast<void *>(&EditMes1), sizeof(ClsMes));
            f.window_made = true;
        }
        SwapBytes(&EditMes1, f.window, sizeof(ClsMes));
    }
}

void BuffersTake(Buffers &into) {
    into.villager = EdVillagerBuffer;
    into.event = EdEventBuffer;
    into.extra = EdEventExBuffer;
}

void BuffersPut(const Buffers &from) {
    EdVillagerBuffer = from.villager;
    EdEventBuffer = from.event;
    EdEventExBuffer = from.extra;
}

// Around player k's fishing step: their own buffers (the ones their board carved, where their fishing's data lies) live, the
// town's kept aside: the catch's model goes past the villager buffer's used part (EdFishingStepIn's ED_FISHING_BATTLE), so in their
// arena, never where the town's villagers load. DC_COOP_MUTATE=catch_town_buffer (twin): the town's buffers stay live.
struct FishBuffersIn {
    int     k;
    bool    in = false;
    Buffers town;

    explicit FishBuffersIn(int index) : k(index) {
        if (g_buffers[k].valid && !Mutated("catch_town_buffer")) {
            BuffersTake(town);
            BuffersPut(g_buffers[k]);
            in = true;
        }
    }
    ~FishBuffersIn() {
        if (in) {
            BuffersTake(g_buffers[k]);
            BuffersPut(town);
        }
    }
    FishBuffersIn(const FishBuffersIn &) = delete;
    FishBuffersIn &operator=(const FishBuffersIn &) = delete;
};

// Test hook (DC_COOP_TEST_FISH_CATCH=1): the fisher's Cross pressed for them where retail's fishing waits for it on a catch (the
// float pulled under: HOOKED; the catch's window: RESULT), from the fishing state every copy has alike, so the catch path runs
// without a human's timing. Into the pad their fishing step reads.
void CatchPressForTest(CGamePad &pad) {
    static const bool on = TestEnvOn("DC_COOP_TEST_FISH_CATCH");

    if (!on || (chara_fishing != ED_FISHING_HOOKED && chara_fishing != ED_FISHING_RESULT) || NetplayLinkTick() % 8 != 0) {
        return;
    }
    PAD_DATA *data = reinterpret_cast<PAD_DATA *>(&pad); // pad[0], pad[1], previous_pad[0], previous_pad[1]

    data[0].input.status.button |= kInputCross;
    data[2].input.status.button &= ~kInputCross;
}

void ArenaFresh(int k) {
    g_parked_alloc[k].base = g_parked_arena[k];
    g_parked_alloc[k].limit = static_cast<int>(kArenaBytes / 16);
    g_parked_alloc[k].used = 0;
}

// Around each start, step and draw of slot k: the slot's state live (and for a start or step its player's pad, bag, sounds and
// fishing set); the step-wide flags an event's step sets (motion, texture animation and camera stops, the pause) put back as the
// live one left them.
struct SlotIn {
    int                       k;
    bool                      step;
    bool                      bag = false;
    int                       player_was;
    int                       sys = 0;
    std::optional<SoundOwner> sound;
    int                       motion_stop, stop_anime, stop_camera, pause;

    SlotIn(int index, bool stepping) : k(index), step(stepping) {
        motion_stop = CCharacter::MotionStopFlag;
        stop_anime = CTextureAnime::stop_anime;
        stop_camera = CCamera::StopCamera;
        pause = EdPauseFlag;
        Swap(k);
        g_stepping = k + 1;
        player_was = g_event_player;
        g_event_player = k + 1;
        if (step) {
            std::swap(GamePad, g_parked[k].pad);
            bag = CoopTownEventBagSwap(k);
            sound.emplace(k + 1, true);
            FishSwap(k, false);
            sys = SysIn(k + 1);
        }
    }
    ~SlotIn() {
        if (step) {
            SysOut(sys);
            FishSwap(k, false);
            sound.reset();
            if (bag) {
                CoopTownEventBagSwap(k);
            }
            std::swap(GamePad, g_parked[k].pad);
        }
        g_event_player = player_was;
        g_stepping = 0;
        Swap(k);
        CCharacter::MotionStopFlag = motion_stop;
        CTextureAnime::stop_anime = stop_anime;
        CCamera::StopCamera = stop_camera;
        EdPauseFlag = pause;
    }
    SlotIn(const SlotIn &) = delete;
    SlotIn &operator=(const SlotIn &) = delete;
};

// Test hook (DC_COOP_TEST_FISH_TRACE=1): each change of player k's fishing step (chara_fishing, its set live), on every copy, with
// the frames this copy drew their fish out of sight and shown so far.
int g_fish_state_said[coop::kMaxPlayers] = {-1, -1, -1, -1, -1, -1};

// coop-faces: each fisher's fishing step as their own step left it (chara_fishing is the live set's; each player's is swapped in for
// their step), for their face (presentation only: the same on every copy, never read by the game).
int g_fish_state_now[coop::kMaxPlayers] = {-1, -1, -1, -1, -1, -1};

int PondOfFish(const CFish *fish); // (coop-pondfish, below)

void FishTrace(int k) {
    static const bool on = TestEnvOn("DC_COOP_TEST_FISH_TRACE");

    g_fish_state_now[k] = chara_fishing;

    if (!on) {
        return;
    }
    // coop-fishspot: their float in the water by retail's own test, on the step retail first applies it (its reel-in, st_cnt over 30).
    static long long wait_from[coop::kMaxPlayers] = {};

    if (chara_fishing == ED_FISHING_WAIT_BITE && g_fish_state_said[k] != ED_FISHING_WAIT_BITE) {
        wait_from[k] = NetplayLinkTick();
    }
    if (chara_fishing == ED_FISHING_WAIT_BITE && NetplayLinkTick() == wait_from[k] + 31) {
        sceVu0FVECTOR float_at;

        FishLineGetUki(float_at);
        std::fprintf(stderr, "coop-fishspot: test: player %d's float at (%.1f %.1f %.1f) %s at link tick %lld\n", k + 1, float_at[0], float_at[1], float_at[2],
                     FishingCheckUkiHook() == 0 ? "in the water" : "OUT of the water (reeled in)", (long long) NetplayLinkTick());
    }
    if (g_fish_state_said[k] == chara_fishing) {
        return;
    }
    g_fish_state_said[k] = chara_fishing;
    std::fprintf(stderr, "coop-eventslots: test: player %d's fishing state %d at link tick %lld (fish %d frames out of sight, %d shown on this copy)\n", k + 1,
                 chara_fishing, (long long) NetplayLinkTick(), g_fishing[k].frames_hidden, g_fishing[k].frames_shown);
    if (chara_fishing == ED_FISHING_REEL_IN) {
        // coop-fishing3: where the float and hook were as retail reeled the line in (FishingCheckUkiHook: outside the area's box,
        // or above the water's level 5 over)
        sceVu0FVECTOR uki, hook;

        FishLineGetUki(uki);
        FishLineGetHook(hook);
        std::fprintf(stderr, "coop-fishspot: test: player %d reels in: float at (%.1f %.1f %.1f), hook at (%.1f %.1f %.1f), the water's level %.1f, at link tick %lld\n", k + 1,
                     uki[0], uki[1], uki[2], hook[0], hook[1], hook[2], FishingGetWaterLevel(), (long long) NetplayLinkTick());
    }
    if (chara_fishing == ED_FISHING_RESULT && AngleFish != nullptr) {
        const auto *model = reinterpret_cast<const unsigned char *>(AngleFish->angle_model.frame);
        const bool  own = model != nullptr && model >= g_parked_arena[k] && model < g_parked_arena[k] + kArenaBytes;

        std::fprintf(stderr, "coop-eventslots: test: player %d's catch's model %s their own arena at link tick %lld\n", k + 1, own ? "in" : "OUTSIDE",
                     (long long) NetplayLinkTick());
        // coop-pondfish: which fish of which pond (-1: the twin's own fish)
        std::fprintf(stderr, "coop-pondfish: test: player %d lands fish %d (kind %d) of pond %d at link tick %lld\n", k + 1, Fish != nullptr ? (int) (AngleFish - Fish) : -1,
                     AngleFish->fish_kind, PondOfFish(Fish), (long long) NetplayLinkTick());
    }
    if (chara_fishing == ED_FISHING_FINISH && SaveData != nullptr) {
        const SV_FISH_DATA *best = SaveData->GetFishingRank(0);

        std::string catchers; // (coop-fishing3: every entry's catcher, best first: a catch need not top the log to be in it)

        for (int rank = 0; rank < 20 && SaveData->GetFishingRank(rank) != nullptr && SaveData->GetFishingRank(rank)->fish_id > 0; rank++) {
            catchers += (rank > 0 ? " " : "") + coop::FishLogWho(rank);
        }
        std::fprintf(stderr, "coop-eventslots: test: player %d's catch: the fishing points now %d, the log's first fish %d size %.2f caught by %s, their own pool %d, "
                     "the log's catchers [%s] at link tick %lld\n", k + 1, (int) SaveData->GetFishingPoint(), best != nullptr ? (int) best->fish_id : -1,
                     best != nullptr ? (double) best->rank : -1.0, coop::FishLogWho(0).c_str(), coop::FishPointsOf(k), catchers.c_str(), (long long) NetplayLinkTick());
    }
    if (chara_fishing == ED_FISHING_WAIT_BITE) {
        sceVu0FVECTOR float_at;
        sceVu0FVECTOR body_at;
        CCharacter   *body = CoopTownPlayerBody(k, nullptr, nullptr);

        FishLineGetUki(float_at);
        if (body != nullptr) {
            body->GetPosition(body_at);
        }
        std::fprintf(stderr, "coop-eventslots: test: player %d's float out at (%.1f %.1f %.1f), %.1f from them\n", k + 1, float_at[0], float_at[1], float_at[2],
                     body != nullptr ? std::hypot(float_at[0] - body_at[0], float_at[2] - body_at[2]) : -1.0f);
        // And every other fisher's float on this same tick, each from their own set (one set for all would put them in one place).
        for (int j = 0; j < coop::kMaxPlayers; j++) {
            if (j == k || !g_fishing[j].on) {
                continue;
            }
            sceVu0FVECTOR other;

            FishSwap(k, false);
            FishSwap(j, false);
            FishLineGetUki(other);
            FishSwap(j, false);
            FishSwap(k, false);
            std::fprintf(stderr, "coop-eventslots: test: floats on one tick: player %d's at (%.1f %.1f %.1f), player %d's at (%.1f %.1f %.1f)\n", k + 1, float_at[0],
                         float_at[1], float_at[2], j + 1, other[0], other[1], other[2]);
        }
    }
}

// Player k's agreed pad into a pad of theirs (as the game's own is updated each tick).
void FeedPad(int k, CGamePad &pad) {
    InputPadState now{};

    if (NetplayPad(k, now)) {
        PadUpdateFrom(pad, now);
    }
}

// A pad starting as a press's own start: the tick before without the button, then now.
void PadFresh(int k, CGamePad &pad) {
    InputPadState now{};

    pad = GamePad;
    pad.AutoRepeatOff();
    if (NetplayPad(k, now)) {
        InputPadState before = now;

        before.buttons &= ~(kInputCross | kInputCircle | kInputSquare);
        PadUpdateFrom(pad, before);
        PadUpdateFrom(pad, now);
    }
}

void FishingStarts(int k, const char *how) {
    Fishing &f = g_fishing[k];

    f.on = true;
    f.tick = NetplayLinkTick();
    f.steps = 0;
    PadFresh(k, f.pad);
    std::fprintf(stderr, "coop-eventslots: player %d fishes on their own (%s) at link tick %lld\n", k + 1, how, (long long) NetplayLinkTick());
    std::fflush(stderr);
}

// An own event of player k ended (live or parked): its buffers kept while k fishes, the town's own back in the live statics.
bool g_town_arena = false; // the live own event runs on its player's arena, the town's buffers kept aside (not the twin's own_arena_off)

void OwnEventEnds(int k, bool live) {
    Buffers &b = g_buffers[k];

    b.valid = g_fishing[k].on && (!live || g_town_arena);
    if (live && g_town_arena) {
        BuffersTake(b);
        if (!Mutated("town_buffers_left")) { // (twin: the town's villager buffer left in the player's arena, where their fish lie)
            BuffersPut(g_town_buffers);
        }
    }
}

// Start player k's event in its slot (every copy alike, on the tick its Cross or fishing asked for it). cross: the Cross that asked
// came on the tick before (a tie's): it is given to the event's first step as a press, as it would have been on its own tick.
bool Start(int k, int event, bool cross = false) {
    CTexAnimeData *anime = nullptr;
    int            anime_count = 0;
    CCharacter    *body = CoopTownPlayerBody(k, &anime, &anime_count);
    CCamera       *from = CoopTownPlayerCamera(k);

    if (body == nullptr || from == nullptr || !Ranges()) {
        return false;
    }
    Parked &slot = g_parked[k];

    // The slot begins as a copy of the live engine (the town's event set-up: main character, windows, NPCs, ground), then retail's
    // EdEventInit makes it this event, in the player's own arena (its fishing's buffers, while they fish).
    for (int i = 0; i < g_range_count; i++) {
        std::memcpy(g_slot_bytes[k] + g_ranges[i].offset, g_ranges[i].at, g_ranges[i].bytes);
    }
    if (!slot.camera_made) {
        new (g_parked_camera[k]) CCameraFollow(60.0f, 20.0f, 0.0f, 4.0f);
        slot.camera_made = true;
    }
    sceVu0FVECTOR pos;
    sceVu0FVECTOR ref;

    from->GetPos(pos);
    from->GetRef(ref);
    SlotCamera(k)->FollowOff();
    SlotCamera(k)->SetPos(pos);
    SlotCamera(k)->SetRef(ref);
    SlotCamera(k)->Step(-1);
    ArenaFresh(k);
    PadFresh(k, slot.pad);
    if (cross) {
        PAD_DATA *data = reinterpret_cast<PAD_DATA *>(&slot.pad); // pad[0], pad[1], previous_pad[0], previous_pad[1]

        data[0].input.status.button |= kInputCross;
        data[2].input.status.button &= ~kInputCross;
    }

    const bool fishing = g_fishing[k].on;

    g_fishing[k].on = false; // (retail's: its fishing mode ends as its event begins)
    int started = 0;
    {
        SlotIn in(k, true);

        EdEventInfo.main_character = body;
        if (anime != nullptr) {
            EdEventInfo.main_texture_animation = anime;
            EdEventInfo.main_texture_animation_count = anime_count;
        }
        if (g_buffers[k].valid) {
            BuffersPut(g_buffers[k]);
        }
        started = EdEventInit(event, &g_parked_alloc[k], EdEventData);
        if (started != 0) {
            EdInitMesParam();
        } else {
            g_buffers[k].valid = false;
        }
    }
    std::fprintf(stderr, "coop-eventslots: player %d's %s starts the town's event %d in their parked slot (player %d's own holds the live engine) at link tick %lld: %s\n",
                 k + 1, fishing ? "fishing" : "Cross", event, CoopTownOwnTownEvent(), (long long) NetplayLinkTick(),
                 started != 0 ? "it runs" : "nothing to run (a simple event)");
    std::fflush(stderr);
    if (started == 0) {
        return false;
    }
    slot.on = true;
    slot.event = event;
    slot.tick = NetplayLinkTick();
    slot.frames_hidden = 0;
    slot.frames_shown = 0;
    slot.steps = 0;
    return true;
}

void Over(int k, int result) {
    Parked &slot = g_parked[k];

    slot.on = false;
    FramesForTest(k);
    std::fprintf(stderr, "coop-eventslots: player %d's own town event %d over in their parked slot at link tick %lld (%lld ticks, return %d): its pictures %d frames out of sight here\n",
                 k + 1, slot.event, (long long) NetplayLinkTick(), (long long) (NetplayLinkTick() - slot.tick), result, slot.frames_hidden);
    std::fflush(stderr);
}

void Step(int k) {
    Parked &slot = g_parked[k];
    int     result = 0;

    FeedPad(k, slot.pad);
    {
        SlotIn in(k, true);

        result = EdEventMode(SlotCamera(k), 0);
        SlotCamera(k)->Step(-1);
        if (CoopTownFadeStepped()) { // coop-daytrans: the slot's fade steps with its event, every copy (its draw only draws it)
            EdFadeStep();
        }
        if (result > 0) {
            BuffersTake(g_buffers[k]); // (the slot's: its fishing's, if it fishes now)
        }
    }
    slot.steps++;
    if (result <= 0) {
        return;
    }
    if (result == ED_EVENT_RETURN_FISHING && !Mutated("parked_fishing_off")) { // (DC_COOP_MUTATE=parked_fishing_off: twin)
        FishingStarts(k, "its parked event's answer");
    } else if (result == ED_EVENT_RETURN_TALK || result == ED_EVENT_RETURN_FISHING || result == ED_EVENT_RETURN_INTERIOR ||
               result == ED_EVENT_RETURN_DUNGEON || result == ED_EVENT_RETURN_MAP_JUMP) {
        std::fprintf(stderr, "coop-eventslots: player %d's parked event %d asks for return %d: not run from a parked slot (ends here)\n", k + 1, slot.event, result);
    }
    g_buffers[k].valid = g_fishing[k].on;
    Over(k, result);
}

// Player k's Cross pressed this tick (its agreed pad), every copy.
bool CrossPressed(int k) {
    InputPadState now{};
    const bool    cross = NetplayPad(k, now) && (now.buttons & kInputCross) != 0;
    const bool    pressed = cross && !g_parked[k].cross_was;

    g_parked[k].cross_was = cross;
    return pressed;
}

gfx::TextureHandle g_es_hidden = gfx::kNullTexture;
gfx::TextureHandle g_es_previous = gfx::kNullTexture;

bool HideBegin(int k) {
    if (NetplayLocalPlayer() == k + 1 || Mutated("parked_shown")) {
        return false;
    }
    if (g_es_hidden == gfx::kNullTexture) {
        g_es_hidden = gfx::CreateRenderTarget(640, 480, true);
    }
    g_es_previous = gfx::CurrentRenderTarget();
    gfx::SetRenderTarget(g_es_hidden);
    return true;
}

void HideEnd(bool hidden) {
    if (hidden) {
        gfx::SetRenderTarget(g_es_previous);
    }
}

// Player k's fishing step, on their body and camera (not the game's player: his is his walk's own, CoopFishingWalkIn/Out).
void FishStep(int k) {
    Fishing       &f = g_fishing[k];
    CCharacter    *body = CoopTownPlayerBody(k, nullptr, nullptr);
    CCameraFollow *camera = CoopTownPlayerFollowCamera(k);

    if (body == nullptr || camera == nullptr) {
        return;
    }
    FeedPad(k, f.pad);
    const ED_MOVE_CHARA_INFO info = EdMoveCharaInfo;

    EdMoveCharaInfo.fishing = 1;
    EdMoveCharaInfo.chara = body;
    EdMoveCharaInfo.camera = camera;
    EdMoveCharaInfo.event_no = -1;
    EdMoveCharaInfo.system_event_no = -1;
    EdMoveCharaInfo.acted = false;
    int event = -1;
    {
        const int                 player_was = g_event_player;
        const int                 sys = SysIn(k + 1); // (their help bar and messages their own)
        const bool                bag = CoopTownEventBagSwap(k);
        coop::MessageBy           by(k + 1);
        std::optional<SoundOwner> sound;

        sound.emplace(k + 1, true); // (ended before the bag and message slots go back, below)
        g_event_player = k + 1;
        std::swap(GamePad, f.pad);
        FishSwap(k, true);
        std::optional<FishBuffersIn> buffers;

        buffers.emplace(k);
        CatchPressForTest(GamePad);

        sceVu0FVECTOR    pos;
        EdFishingWeights weights{};

        CoopTownFishWalk(k, body, camera); // coop-xiaofish: her walk with the rod before the cast, as EdMoveChara's for its player
        body->GetPosition(pos);
        EdFishingStepIn(body, camera, pos, &weights);
        body->Step();
        body->ShadowStep();
        body->ClothStep(0);
        EdFishingLineStep(body, weights);
        event = EdMoveCharaInfo.event_no;
        FishTrace(k);
        buffers.reset();

        FishSwap(k, true);
        std::swap(GamePad, f.pad);
        g_event_player = player_was;
        sound.reset();
        if (bag) {
            CoopTownEventBagSwap(k);
        }
        SysOut(sys);
    }
    EdMoveCharaInfo = info;
    camera->Step(1);
    f.steps++;
    if (event == kFishQuit || event == kFishBait) {
        g_parked[k].queued = event;
    }
}
} // namespace

int CoopVillagerLoadFrom(); // (coop-daytrans, below)

namespace {
// coop-daytrans (Jep 11-11: the time of day's change plays only for players not busy). Retail's change (MainEditMode's change_time_event,
// then town event 0x84 on the one event engine, the game in its event mode): its script freezes the main character, climbs the camera to
// the sky with "Day N" (160 frames), waits, fades out, and under the black clears the villagers' buffer, sets who is out this period and
// loads the period's villagers again (_CLEAR_VILLAGER_BUFF, _APPEAR_VILLAGER_*, _LOAD_VILLAGER), then fades in. Together it ran as one
// scene for everyone and waited (eventslots' dayhold, pt1109c's owe) while anyone talked or had an event of their own. Now, together
// outdoors, it starts on the tick the change is asked for, every copy alike, in a world slot of its own (the event engine's state set,
// as a player's parked slot, owned by nobody, on the town's work buffer as retail's): what it does to the world (the villagers, who is
// out) happens on every copy on one tick. Its presentation (the camera's climb, the title, the fades, the window, the walk held) is for
// the players free as it began (CoopTownPlayerBusy), each from their own camera; a busy player keeps their own state and skips it, and
// is in the new period when they come out of it. A villager in someone's talk as the villagers load is kept as it is (its body and its
// data: the load goes past it) and given its new period's model and place as that talk ends (EdVillagerLoadOne, EdVillagerPlaceOne).
// DC_COOP_MUTATE=daytrans_off (twin): retail's event for everyone together, waiting for talks and own events, as before.
struct World {
    bool          on = false;
    long long     tick = 0;
    int           steps = 0;
    bool          camera_made = false;
    bool          viewer[coop::kMaxPlayers] = {};        // by player number (n - 1), as the town's table holds them together
    bool          decided[coop::kMaxPlayers] = {};       // coop-joinload (daytrans2's owed item): whether each player's watch is settled
    int           map = -1;                              // ...the town it runs in
    sceVu0FVECTOR from_pos = {};                         // the world camera as it began (the game's walk camera, as retail's RunEvent)
    sceVu0FVECTOR from_ref = {};
    sceVu0FVECTOR own_pos[coop::kMaxPlayers] = {};       // each watcher's own camera as it began
    sceVu0FVECTOR own_ref[coop::kMaxPlayers] = {};
    sceVu0FVECTOR body_from[coop::kMaxPlayers] = {};     // each player's body's place as it began (its test line: a watcher stands)
    bool          had_body[coop::kMaxPlayers] = {};
    bool          kept[10] = {};                         // villagers left as they were by its load (in a talk then)
    bool          late[10] = {};                         // ...still owed their new period's model and place
    float         clock_from = 0.0f;                     // coop-daytrans2: the clock as it began (its line: it stood, or ran)
    int           held = 0;                              // ...its steps with the world held (every player in a pause state)
};

World g_world;
int   g_dt_frames_shown = 0; // (this copy's draw counts: the tests' lines; out of the state set)
int   g_dt_frames_hidden = 0;
int   g_dt_frames_view = 0;
alignas(16) unsigned char g_world_bytes[kSlotBytes] = {};
alignas(16) unsigned char g_world_camera[sizeof(CCameraFollow)];
alignas(16) unsigned char g_view_camera[sizeof(CCameraFollow)];
bool               g_view_camera_made = false;
bool               g_world_in = false; // inside its step
bool               g_world_loaded = false; // a villager loaded in that step (its _LOAD_VILLAGER)
int                g_late_index = -1;  // the villager loaded or placed late now (the others left as they are)
const unsigned char *g_span_from[10] = {};
const unsigned char *g_span_to[10] = {};

CCameraFollow *WorldCamera() {
    return std::launder(reinterpret_cast<CCameraFollow *>(g_world_camera));
}

// This copy's own player index (0 up), a player's number from their index (the lines), and player n's (1 up) index on this copy, or -1:
// together every copy steps the town's players by their table index; apart a copy is one player, the game's (index 0), the link down.
// coop-joinload: this copy's own player number (1 up): the link's while together; else (apart, or alone after every other copy's Leave,
// when the link's is 0) its role, as apart. Before, alone it was 0: the watcher here lost its own scene (drawn out of sight: x-town-daytrans-drop,
// 11-12: "shown 31 frames, out of sight 319" after the Leave).
int OwnNo() {
    const int linked = NetplayLocalPlayer();

    return linked != 0 && Together() ? linked : NetplayRole() != 0 ? NetplayRole() : 1;
}

// The world slot's watchers are kept by player number: this copy's own player's slot in them.
int LocalIndex() {
    return Mutated("daytrans_index_fixed") ? NetplayLocalPlayer() - 1 : OwnNo() - 1;
}

int PlayerNo(int k) {
    return k + 1;
}

int IndexOf(int n) {
    if (!Together()) { // (coop-joinload: apart, or alone after a Leave: index 0 is this copy's own player)
        return n == OwnNo() ? 0 : -1;
    }
    return n >= 1 && n <= coop::kMaxPlayers ? n - 1 : -1;
}

// coop-joinload (daytrans2's owed item: a merge or a split under the scene left the world slot's watchers at the wrong index): the player
// number (1 up) of index k on this copy now, 0 none. The world slot keeps its watchers by player number; together the index is the number
// less one, apart index 0 is this copy's own player (IndexOf's other way).
int NoOfIndex(int k) {
    if (!Together()) {
        return k == 0 ? OwnNo() : 0;
    }
    return k >= 0 && k < coop::kMaxPlayers ? k + 1 : 0;
}

// Whether player n (1 up) watches the scene.
bool Watches(int n) {
    return g_world.on && n >= 1 && n <= coop::kMaxPlayers && g_world.viewer[n - 1] && !Mutated("daytrans_viewer_free");
}

// The world slot's bytes exchanged with the live statics (twice puts everything back).
void WorldSwap() {
    for (int i = 0; i < g_range_count; i++) {
        const Range &r = g_ranges[i];
        auto        *live = static_cast<unsigned char *>(r.at);
        auto        *kept = g_world_bytes + r.offset;

        for (std::size_t b = 0; b < r.bytes; b++) {
            std::swap(live[b], kept[b]);
        }
    }
}

// Around the world slot's start, steps and draw: its state live; the step-wide flags an event's step sets put back as the live one left them.
struct WorldIn {
    bool     step;
    CGamePad pad{};
    int      motion_stop, stop_anime, stop_camera, pause;

    explicit WorldIn(bool stepping) : step(stepping) {
        motion_stop = CCharacter::MotionStopFlag;
        stop_anime = CTextureAnime::stop_anime;
        stop_camera = CCamera::StopCamera;
        pause = EdPauseFlag;
        WorldSwap();
        if (step) {
            pad = GamePad;
            pad.AutoRepeatOff();
            PadUpdateFrom(pad, InputPadState{}); // (nobody's pad: the scene asks for none)
            PadUpdateFrom(pad, InputPadState{});
            std::swap(GamePad, pad);
            g_world_in = true;
        }
    }
    ~WorldIn() {
        if (step) {
            g_world_in = false;
            std::swap(GamePad, pad);
        }
        WorldSwap();
        CCharacter::MotionStopFlag = motion_stop;
        CTextureAnime::stop_anime = stop_anime;
        CCamera::StopCamera = stop_camera;
        EdPauseFlag = pause;
    }
    WorldIn(const WorldIn &) = delete;
    WorldIn &operator=(const WorldIn &) = delete;
};

// The villagers in a talk now (every copy alike: the talks' slots).
void KeptNow(bool *kept) {
    for (int i = 0; i < 10; i++) {
        kept[i] = false;
    }
    if (Mutated("daytrans_talker_reloaded")) { // (twin: the villager in a talk loaded again under it, as retail's one player never meets)
        return;
    }
    for (int k = 0; k < coop::kMaxPlayers; k++) {
        if (const int v = CoopTalkVillager(k); v >= 0) {
            kept[v] = true;
        }
    }
}

// The town's villager buffer as the walk has it (the live one, or the one set aside while a live own event runs on a player's arena).
CDataAlloc2<1> &TownVillagerBuffer() {
    return g_live_own != 0 && g_town_arena ? g_town_buffers.villager : EdVillagerBuffer;
}

void WorldOver(int result) {
    g_world.on = false;
    std::fprintf(stderr, "coop-daytrans: the time of day's event over in its world slot at link tick %lld (%lld ticks, return %d): shown %d frames, out of sight %d here\n",
                 (long long) NetplayLinkTick(), (long long) (NetplayLinkTick() - g_world.tick), result, g_dt_frames_shown, g_dt_frames_hidden);
    // (each player's body: how far it went through the scene; a watcher's is held: it stands)
    // (coop-joinload: by player number, as the slot keeps them; index k is this copy's body, apart or alone index 0 its own player)
    for (int k = 0; k < coop::kMaxPlayers; k++) {
        CCharacter *body = CoopTownPlayerBody(k, nullptr, nullptr);
        const int   n = Mutated("daytrans_index_fixed") ? k + 1 : NoOfIndex(k);

        if (body == nullptr || n < 1 || n > coop::kMaxPlayers || !g_world.had_body[n - 1]) {
            continue;
        }
        sceVu0FVECTOR now;

        body->GetPosition(now);
        std::fprintf(stderr, "coop-daytrans: player %d %s through it: moved %.1f\n", n, g_world.viewer[n - 1] ? "watched" : "skipped it",
                     std::hypot(now[0] - g_world.body_from[n - 1][0], now[2] - g_world.body_from[n - 1][2]));
    }
    // coop-daytrans2: the world through it (retail's event mode stands the clock and the villagers; here while every player is in a pause state)
    std::fprintf(stderr, "coop-daytrans: through it the world held %d of %d ticks; the clock %.4f -> %.4f\n", g_world.held, g_world.steps, g_world.clock_from, NowTime);
    std::printf("coop: time of day event ends (clock %.3f, world slot)\n", NowTime);
    std::fflush(stderr);
}

// A kept villager whose talk has ended: its new period's model (past every villager's data) and place, every copy alike.
void LateVillagers() {
    bool talking[10];

    KeptNow(talking);
    for (int i = 0; i < 10; i++) {
        if (!g_world.late[i] || talking[i]) {
            continue;
        }
        g_world.late[i] = false;

        CDataAlloc2<1> &town = TownVillagerBuffer();
        const int       from = CoopVillagerLoadFrom();
        const int       used_was = town.used;
        int             loaded = 0;

        g_late_index = i;
        if (EdVillagerInfo[i].placed != 0 || EdVillagerInfo[i].name[0] != '\0') {
            town.used = std::max(town.used, from);
            loaded = EdVillagerLoadOne(i, &town);
            EdVillagerPlaceOne(i, pEditGround);
        } else {
            EdVillager[i].Initialize(); // (not in this period: gone as its talk ends)
        }
        g_late_index = -1;
        std::fprintf(stderr, "coop-daytrans: villager %d's talk is over: its new period's %s %s at link tick %lld (buffer %d -> %d of %d)\n", i,
                     EdVillagerInfo[i].name, loaded != 0 ? "loaded and placed" : "not loaded", (long long) NetplayLinkTick(), used_was, town.used, town.limit);
        std::fflush(stderr);
    }
}

// Past the data of the villagers flagged in which (their spans as EdLoadVillager put them), in quads from buffer's base; 0 when none.
int PastSpans(const CDataAlloc2<1> &buffer, const bool *which) {
    int from = 0;

    for (int i = 0; i < 10; i++) {
        if (!which[i] || g_span_to[i] == nullptr || buffer.base == nullptr) {
            continue;
        }
        const std::ptrdiff_t past = (g_span_to[i] - buffer.base + 15) / 16;

        if (past > from && past <= buffer.limit) {
            from = static_cast<int>(past);
        }
    }
    return from;
}

// The world slot's step (every copy, every town tick outdoors together, from the tick's start).
void WorldStep() {
    int            result = 0;
    CDataAlloc2<1> villagers{-1};
    bool           loaded = false;

    KeptNow(g_world.kept);
    {
        WorldIn in(true);

        g_world_loaded = false;
        result = EdEventMode(WorldCamera(), 0);
        WorldCamera()->Step(-1);
        if (CoopTownFadeStepped()) {
            EdFadeStep();
        }
        villagers = EdVillagerBuffer;
        loaded = g_world_loaded;
        g_world_loaded = false;
    }
    g_world.steps++;
    if (loaded) {
        // (its villagers' buffer is the town's from now: the walk's events carve after it, as after retail's 0x84)
        TownVillagerBuffer() = villagers;
        for (int i = 0; i < 10; i++) {
            if (g_world.kept[i]) {
                g_world.late[i] = true;
            }
        }
        std::fprintf(stderr, "coop-daytrans: the villagers of the period loaded at link tick %lld, %d of %d quads used; kept as they were (a talk):", (long long) NetplayLinkTick(),
                     villagers.used, villagers.limit);
        for (int i = 0; i < 10; i++) {
            if (g_world.kept[i]) {
                std::fprintf(stderr, " %d", i);
            }
        }
        std::fprintf(stderr, "\n");
    }
    if (result > 0) {
        WorldOver(result);
    }
}

// Every watcher stands through the scene, as retail's event mode stands its player (their walk held: CoopDayTransHolds).
void WatchersStand() {
    for (int k = 0; k < coop::kMaxPlayers; k++) {
        // (k this copy's index of a body; the watcher by its player's number: apart index 0 is this copy's own, coop-joinload)
        const int n = Mutated("daytrans_index_fixed") ? k + 1 : NoOfIndex(k);

        if (!Watches(n)) {
            continue;
        }
        CCharacter *body = CoopTownPlayerBody(k, nullptr, nullptr);

        if (body == nullptr || body->frame == nullptr) {
            continue;
        }
        body->motion_no = 0;
        body->motion_flags = 0;
        body->motion_speed = -1.0f;
        body->SetVelocity(CVector3_f_(0.0f, 0.0f, 0.0f));
        body->Step();
        body->ShadowStep();
        body->ClothStep(0);
    }
}

// Hidden draws: this copy's player does not watch the scene (drawn out of sight: its parts step as they draw).
gfx::TextureHandle g_dt_hidden = gfx::kNullTexture;
gfx::TextureHandle g_dt_previous = gfx::kNullTexture;

bool WorldHideBegin() {
    const int k = LocalIndex();

    if (k >= 0 && k < coop::kMaxPlayers && g_world.viewer[k]) {
        return false;
    }
    if (Mutated("daytrans_shown")) { // (twin: on every screen, a busy player's too)
        return false;
    }
    if (g_dt_hidden == gfx::kNullTexture) {
        g_dt_hidden = gfx::CreateRenderTarget(640, 480, true);
    }
    g_dt_previous = gfx::CurrentRenderTarget();
    gfx::SetRenderTarget(g_dt_hidden);
    return true;
}

void WorldHideEnd(bool hidden) {
    if (hidden) {
        gfx::SetRenderTarget(g_dt_previous);
    }
}

// Test hook (DC_COOP_TEST_DAYTRANS_SAY=1, or DC_COOP_DEBUG): a watcher's drawn eye every 10 frames, the vertical share of its look ("up": the
// climb to the sky), and their own camera's eye as the scene began (the shot from their own place: n-time-event).
void DayTransViewSay(const float *eye, const float (*view)[4]) {
    static const bool on = TestEnvOn("DC_COOP_TEST_DAYTRANS_SAY") || CoopDebug();
    const int         k = LocalIndex();

    if (on && (g_dt_frames_view % 10) == 1 && k >= 0 && k < coop::kMaxPlayers) {
        std::fprintf(stderr, "coop-daytrans: test: player %d watches from eye (%.1f %.1f %.1f) up %.2f, their own camera's eye then (%.1f %.1f %.1f), at link tick %lld\n",
                     PlayerNo(k), eye[0], eye[1], eye[2], view[1][2], g_world.own_pos[k][0], g_world.own_pos[k][1], g_world.own_pos[k][2], (long long) NetplayLinkTick());
    }
}
} // namespace

// ---- coop-daytrans: the world slot's hooks (global scope) ----

bool CoopDayTransOn() {
    return g_world.on;
}

// coop-daycrash (the developer's rc4 playtest 11-18, both copies crashed in EditInInit's LoadTexture -> EnterIMGFile): this copy leaves the town (a
// building's door, a map or a script's way out; CoopWorldsTakeInterior / TakeExit / TakeScriptExit said it goes) while the time of day's event
// runs in its world slot. Its last step loads the new period's villagers into the town's villager buffer (WorldStep: TownVillagerBuffer() =
// the slot's), and the town's villager buffer is the arena a building's files are read into as its door opens (EdMapJump: the room's .pak/.img
// at EdNPCBuffer.base, 0x40 below EdVillagerBuffer.base in the dumps): a story door's yes taken during the scene read the room's textures there,
// the scene's end loaded 200548 quads of villagers over them, and the room's load read villager data as its texture pack. Retail's event mode
// runs no door. The slot ends here with the town, as a zone load ends it (CoopEventSlotsSessionReset): nothing of it is live (WorldIn swaps
// it in and back each step), the clock already stands in the new period, and the town's next load takes that period's villagers.
// DC_COOP_MUTATE=daytrans_runs_on_leave (twin): it runs on, as before.
void CoopDayTransTownLeft(const char *by) {
    if (!g_world.on || Mutated("daytrans_runs_on_leave")) {
        return;
    }
    g_world.on = false;
    for (int i = 0; i < 10; i++) {
        g_world.late[i] = false;
    }
    std::fprintf(stderr, "coop-daytrans: this copy leaves the town (%s) at link tick %lld: the time of day's event in its world slot ends with it (%lld ticks in, %d steps)\n",
                 by, (long long) NetplayLinkTick(), (long long) (NetplayLinkTick() - g_world.tick), g_world.steps);
    std::printf("coop: time of day event ends (clock %.3f, world slot, the town left)\n", NowTime);
    std::fflush(stderr);
}

// (coop-daytrans2: apart a copy is one player, the link down: retail's change, which no talk holds there, CoopTownDayChangeHeld)
bool CoopDayTransTakes() {
    return Together() && EdInteriorFlag == 0 && !Mutated("daytrans_off");
}

bool CoopDayTransStepping() {
    return g_world_in;
}

// (index: this copy's index of the player, as CoopTownPlayerBody's; apart 0 is its own: coop-joinload, by the player's number)
bool CoopDayTransHolds(int index) {
    return Watches(Mutated("daytrans_index_fixed") ? index + 1 : NoOfIndex(index));
}

// coop-joinload (design for three): player number player (1 up) watches the scene (its walk, Cross, menus held).
bool CoopDayTransHoldsPlayer(int player) {
    return Watches(player);
}

// EdInitVilager / EdInitVilagerPosition: a villager this pass leaves as it is (one in a talk under the world slot's load; every other one
// while one villager is loaded or placed late).
bool CoopVillagerLeftAsIs(int index) {
    if (index < 0 || index >= 10) {
        return false;
    }
    if (g_late_index >= 0) {
        return index != g_late_index;
    }
    return g_world_in && g_world.kept[index];
}

// _LOAD_VILLAGER: where the load starts (0 as retail's; under the world slot with a villager kept, past that villager's data).
// A late load (EdVillagerLoadOne): past every other villager's data.
int CoopVillagerLoadFrom() {
    if (g_late_index >= 0) {
        bool all[10];

        for (int i = 0; i < 10; i++) {
            all[i] = i != g_late_index;
        }
        return PastSpans(TownVillagerBuffer(), all);
    }
    return g_world_in ? PastSpans(EdVillagerBuffer, g_world.kept) : 0;
}

// EdLoadVillager: villager index's data lies in arena from quad from to its used now.
void CoopVillagerLoaded(int index, const CDataAlloc2<1> *arena, int from) {
    if (index < 0 || index >= 10 || arena == nullptr || arena->base == nullptr) {
        return;
    }
    g_world_loaded = g_world_loaded || g_world_in;
    g_span_from[index] = arena->base + static_cast<std::ptrdiff_t>(from) * 16;
    g_span_to[index] = arena->base + static_cast<std::ptrdiff_t>(arena->used) * 16;
}

// The town's walk asks for the time of day's event (MainMode: change_time_event; CoopDayTransTakes, together outdoors): it starts here in
// its world slot, every copy alike: true, the ask answered. False while another still runs (or the state set is not listed yet): asked again.
bool CoopDayTransStart() {
    if (g_world.on || !Ranges()) {
        return false;
    }
    if (EdEventData == nullptr) {
        return true;
    }
    // Its watchers: the players here free on this tick.
    char said[256] = {};
    int  at = 0;

    for (int k = 0; k < coop::kMaxPlayers; k++) {
        g_world.viewer[k] = false;
        g_world.had_body[k] = false;
        g_world.decided[k] = false;
        if (!CoopTownPlayerPresent(k)) {
            continue;
        }
        const char *busy = CoopTownPlayerBusy(k);

        g_world.decided[k] = true;
        g_world.viewer[k] = busy == nullptr;
        if (at < (int) sizeof said - 64) {
            at += std::snprintf(said + at, sizeof said - at, "%splayer %d %s%s%s", at > 0 ? ", " : "", PlayerNo(k), busy == nullptr ? "watches" : "skips it (",
                                busy == nullptr ? "" : busy, busy == nullptr ? "" : ")");
        }
        if (CCamera *own = CoopTownPlayerCamera(k); own != nullptr) {
            own->GetPos(g_world.own_pos[k]);
            own->GetRef(g_world.own_ref[k]);
        }
        CCharacter *body = CoopTownPlayerBody(k, nullptr, nullptr);

        g_world.had_body[k] = body != nullptr;
        if (body != nullptr) {
            body->GetPosition(g_world.body_from[k]);
        }
    }
    // The slot begins as a copy of the live engine, then retail's EdEventInit makes it event 0x84 on the town's work buffer (as retail's
    // RunEvent and EventMode do), from the game's walk camera.
    for (int i = 0; i < g_range_count; i++) {
        std::memcpy(g_world_bytes + g_ranges[i].offset, g_ranges[i].at, g_ranges[i].bytes);
    }
    if (!g_world.camera_made) {
        new (g_world_camera) CCameraFollow(60.0f, 20.0f, 0.0f, 4.0f);
        g_world.camera_made = true;
    }
    MainCamera.GetPos(g_world.from_pos);
    MainCamera.GetRef(g_world.from_ref);
    WorldCamera()->FollowOff();
    WorldCamera()->SetPos(g_world.from_pos);
    WorldCamera()->SetRef(g_world.from_ref);
    WorldCamera()->SetRoll(0.0f);
    WorldCamera()->Step(-1);

    int started = 0;

    {
        WorldIn in(true);
        CTexAnimeData *anime = nullptr;
        int            anime_count = 0;

        EdEventInfo.main_character = CoopTownPlayerBody(0, &anime, &anime_count);
        started = EdEventInit(0x84, &EdWorkBuffer, EdEventData);
        if (started != 0) {
            EdInitMesParam();
        }
    }
    std::fprintf(stderr, "coop-daytrans: the time of day's event starts in its world slot at link tick %lld, clock %.3f, period %d: %s%s\n",
                 (long long) NetplayLinkTick(), NowTime, EdGetTime(NowTime), said, started != 0 ? "" : " (nothing to run)");
    std::fflush(stderr);
    if (started == 0) {
        return true;
    }
    std::printf("coop: time of day event starts (clock %.3f, world slot)\n", NowTime);
    g_world.on = true;
    g_world.map = MapNo;
    g_world.tick = NetplayLinkTick();
    g_world.steps = 0;
    g_world.held = 0;
    g_world.clock_from = NowTime;
    g_dt_frames_shown = 0;
    g_dt_frames_hidden = 0;
    return true;
}

// Each town tick's start outdoors (CoopGeoramaP1StepBegin), every copy: the world slot's step, its watchers stood, kept villagers' late load.
// coop-joinload (daytrans2's owed item, design for three): who watches follows who is here, by player number, each tick the slot runs
// (every copy alike: presence is the town's table's). A player gone (a split, a dropped link, a door) watches no more, so no hold stays
// on a player no longer here; a player come since the start (a merge into the scene) watches it unless busy, as the scene's own watchers
// did at its start (Jep 11-10's arriver rule: the joiner watches the scene, its model hidden). Its own camera as it came is its start.
// A copy that left the town the slot runs in (apart, another map) ends its slot. DC_COOP_MUTATE=daytrans_viewers_fixed (twin): as at the start.
void WorldPlayersNow() {
    if (MapNo != g_world.map) {
        std::fprintf(stderr, "coop-daytrans: this copy left the town its world slot runs in (map %d, now %d): the slot ends here\n", g_world.map, (int) MapNo);
        WorldOver(-1);
        return;
    }
    if (Mutated("daytrans_viewers_fixed")) {
        return;
    }
    for (int n = 1; n <= coop::kMaxPlayers; n++) {
        const int  k = IndexOf(n);
        const bool here = k >= 0 && CoopTownPlayerPresent(k);

        if (!here && g_world.viewer[n - 1]) {
            g_world.viewer[n - 1] = false;
            std::fprintf(stderr, "coop-daytrans: player %d is no longer here: it watches the scene no more (link tick %lld)\n", n, (long long) NetplayLinkTick());
        } else if (here && !g_world.decided[n - 1]) {
            const char *busy = CoopTownPlayerBusy(k);

            g_world.decided[n - 1] = true;
            g_world.viewer[n - 1] = busy == nullptr;
            if (CCamera *own = CoopTownPlayerCamera(k); own != nullptr) {
                own->GetPos(g_world.own_pos[n - 1]);
                own->GetRef(g_world.own_ref[n - 1]);
            }
            std::fprintf(stderr, "coop-daytrans: player %d came into the scene (link tick %lld): %s%s%s\n", n, (long long) NetplayLinkTick(),
                         busy == nullptr ? "watches it" : "skips it (", busy == nullptr ? "" : busy, busy == nullptr ? "" : ")");
        }
    }
}

void CoopDayTransTick() {
    if (EdInteriorFlag != 0) {
        return;
    }
    // Test hook (DC_COOP_TEST_DAYTRANS_ON_MENU=<player>, x-town-daytrans menu): on the tick that player's menu is open (their bit, every copy
    // alike), the clock to dusk (coop-daytrans2: the period itself, not just short of it: a world that runs crossed it again under the scene and
    // asked a second one) and the change asked for (as DC_COOP_TEST_DUSK_AT, anchored on the menu, not a tick).
    if (static const int on_menu = TestEnvInt("DC_COOP_TEST_DAYTRANS_ON_MENU", 0); on_menu > 0) {
        static bool asked = false;

        if (!asked && SaveData != nullptr && coop::PlayerInMenu(on_menu)) {
            asked = true;
            NowTime = 2.5f;
            SaveData->SetNowTime(NowTime);
            change_time_event = 1;
            std::fprintf(stderr, "coop-daytrans: test: player %d's menu open: the clock to 2.5, the change asked for at link tick %lld\n", on_menu,
                         (long long) NetplayLinkTick());
        }
    }
    // Test hook (DC_COOP_TEST_DAYTRANS_ASK=busy:<n> | apart:<n> | question:<n> | clock:<n> | menus | free; coop-daytrans2, x-town-daytrans): once the state file is back, on the first tick player n
    // (1 up) is busy with anything (CoopTownPlayerBusy: a talk, a shop, a chest's message ...), or every present player is free with two or more here,
    // the clock to just before dusk and the change asked for, every copy alike (anchored on that state, not a tick).
    if (static const char *const ask = TestEnv("DC_COOP_TEST_DAYTRANS_ASK"); ask != nullptr && *ask != '\0' && SaveData != nullptr &&
        (TestEnv("DC_STATE_LOAD_AT") == nullptr || NetplayStateFileLoaded())) {
        static bool asked = false; // (every copy alike)
        const bool  apart = std::strncmp(ask, "apart:", 6) == 0; // (apart:<n>: busy, and apart: the clock alone, below)
        // (coop-daycrash: question:<n>: player n's story start question up; clock:<n>: the same, the clock just short of the period alone, its own step
        // to cross it or not, and the clock said every 60 ticks while the story start's box is open)
        // (menus: every present player in a menu, the clock just short of the period alone, and the clock and the villagers' walk said every 60
        // ticks while they all are)
        const bool  clock = std::strncmp(ask, "clock:", 6) == 0;
        const bool  menus = std::strcmp(ask, "menus") == 0;
        const bool  question = clock || std::strncmp(ask, "question:", 9) == 0;
        const bool  short_of = apart || clock || menus;
        const int   n = std::strncmp(ask, "busy:", 5) == 0 ? std::atoi(ask + 5) : apart || clock ? std::atoi(ask + 6) : question ? std::atoi(ask + 9) : 0;
        const char *why = nullptr;

        if (!asked && question) {
            why = coop::ready::Asking(n) ? "is asked a story start's question" : nullptr;
        } else if (!asked && n > 0 && (!apart || CoopWorldsApart())) {
            const int k = IndexOf(n);

            why = k >= 0 && CoopTownPlayerPresent(k) ? CoopTownPlayerBusy(k) : nullptr;
        } else if (!asked && menus) {
            int here = 0;
            int in = 0;

            for (int k = 0; k < coop::kMaxPlayers; k++) {
                if (CoopTownPlayerPresent(k)) {
                    here++;
                    in += coop::PlayerInMenu(PlayerNo(k)) ? 1 : 0;
                }
            }
            why = here >= 2 && in == here ? "every player in a menu" : nullptr;
        } else if (!asked && std::strcmp(ask, "free") == 0) {
            int here = 0;
            int idle = 0;

            for (int k = 0; k < coop::kMaxPlayers; k++) {
                if (CoopTownPlayerPresent(k)) {
                    here++;
                    idle += CoopTownPlayerBusy(k) == nullptr ? 1 : 0;
                }
            }
            why = here >= 2 && idle == here ? "every player free" : nullptr;
        }
        if (why != nullptr) {
            asked = true;
            NowTime = short_of ? 2.4999f : 2.5f; // (asked: the period itself, as the clock crossing it asks; apart, clock: just short of it)
            SaveData->SetNowTime(NowTime);
            change_time_event = short_of ? change_time_event : 1; // (apart, clock: the clock to just before the period; its own step crosses it, or not)
            char who[96];

            if (n > 0) {
                std::snprintf(who, sizeof who, "player %d %s", n, why);
            } else {
                std::snprintf(who, sizeof who, "%s", why);
            }
            std::fprintf(stderr, "coop-daytrans: test: %s: the clock to %s%s at link tick %lld\n", who, short_of ? "2.4999" : "2.5", short_of ? " (not asked)" : ", the change asked for",
                         (long long) NetplayLinkTick());
        }
        // (the villagers' walk: how far the drawn villagers moved since the last line, every copy alike)
        // (from the state's load, every 60 ticks: the free ticks before are the test's baseline; each line names the state it was read in)
        static float last[10][3] = {};
        static float walked = 0.0f;
        int          drawn = 0;

        for (int i = 0; i < 10 && (clock || menus); i++) {
            sceVu0FVECTOR at;

            EdVillager[i].GetPosition(at);
            if (EdVillager[i].initialized != 0 && EdVillager[i].villager_id >= 0) { // (placed: a headless copy draws nothing)
                walked += std::hypot(at[0] - last[i][0], at[2] - last[i][2]);
                drawn++;
            }
            last[i][0] = at[0];
            last[i][1] = at[1];
            last[i][2] = at[2];
        }
        bool all_menus = menus;

        for (int k = 0; all_menus && k < coop::kMaxPlayers; k++) {
            all_menus = !CoopTownPlayerPresent(k) || coop::PlayerInMenu(PlayerNo(k));
        }
        const bool box = clock && coop::ready::IsOpen();

        if (static long long said = -1; (clock || menus) && NetplayLinkTick() >= said + 60) {
            static bool was = false; // (the state the 60 ticks were read in: the same at both ends, else "changing")
            const bool  now = box || all_menus;

            static long long asks_was = 0;
            static long long let_was = 0;
            long long        asks = 0;
            long long        let = 0;

            CoopTownVillagerWalkCounts(&asks, &let); // (the walk's mover asks once a tick it would run: CoopTownVillagersHeld)
            if (said >= 0) {
                std::fprintf(stderr, "coop-daytrans: test: %s: clock %.4f, villagers walked %.1f (%d placed), their walk let run %lld of %lld asks at link tick %lld\n",
                             now != was ? "changing" : !now ? "free" : box ? "the story start's box open" : "every player in a menu", SaveData->GetNowTime(), walked, drawn,
                             let - let_was, asks - asks_was, NetplayLinkTick());
            }
            asks_was = asks;
            let_was = let;
            said = NetplayLinkTick();
            walked = 0.0f;
            was = now;
        }
    }
    if (g_world.on) {
        WorldPlayersNow();
    }
    // coop-joinload (daytrans2's owed item): not together (a split, a dropped link, or every other copy gone by its Leave under the scene)
    // each copy steps its slot on for its own player, as retail's event plays for one: before, its slot stood for good (stepped together
    // only) and held its watcher's walk. A Leave is neither together nor apart (DropApart leaves a Leave alone: the session is over, no
    // merge comes), which is how x-town-daytrans-drop found it (proven 11-12: together 0, apart 0, the slot never stepped).
    // DC_COOP_MUTATE=daytrans_split_stalls (twin): together only, as before.
    if (g_world.on && (Together() || !Mutated("daytrans_split_stalls"))) {
        // coop-daytrans2: the world held on this step (every present player in a pause state, watching it among them: CoopTalkWorldHeld)
        g_world.held += CoopTalkWorldHeld() ? 1 : 0;
        WorldStep();
        if (g_world.on) {
            WatchersStand();
        }
    }
    LateVillagers();
}

// The town's draw (every copy): the scene's windows and fade, shown where this copy's player watches it.
void CoopDayTransDraw() {
    if (!g_world.on) {
        return;
    }
    const bool hidden = WorldHideBegin();

    (hidden ? g_dt_frames_hidden : g_dt_frames_shown)++;
    {
        WorldIn in(false);

        EdEventSpriteDraw();
        TexManager.ReloadTexture(Vif1Packet, EditMes1.tex_block);
        EditMes1.DrawMesWin();
        EditEventMes1.DrawMesWin();
        EdFadeInOut();
    }
    WorldHideEnd(hidden);
}

// The day's title (DrawDay): out of sight on a copy whose player skips the scene, while it runs.
bool CoopDayTransHideBegin() {
    return g_world.on && WorldHideBegin();
}

void CoopDayTransHideEnd(bool hidden) {
    WorldHideEnd(hidden);
}

// This copy's frame while its player watches: the world camera's climb, from this player's own camera as the scene began. The world
// camera's place and look are taken in the frame of its start (from its look point, turned by its heading) and put in the watcher's.
// DC_COOP_MUTATE=daytrans_view_his (twin): the world camera as it is (the game's player's place) on every watcher's screen.
bool CoopDayTransView(float (*view)[4], float *eye) {
    const int k = LocalIndex();

    if (!g_world.on || k < 0 || k >= coop::kMaxPlayers || !g_world.viewer[k]) {
        return false;
    }
    sceVu0FVECTOR pos;
    sceVu0FVECTOR ref;

    WorldCamera()->GetPos(pos);
    WorldCamera()->GetRef(ref);
    if (Mutated("daytrans_view_his")) {
        WorldCamera()->GetCameraMatrix(view);
        WorldCamera()->GetPos(eye);
        g_dt_frames_view++;
        DayTransViewSay(eye, view);
        return true;
    }
    const float yaw0 = std::atan2(g_world.from_ref[0] - g_world.from_pos[0], g_world.from_ref[2] - g_world.from_pos[2]);
    const float yawk = std::atan2(g_world.own_ref[k][0] - g_world.own_pos[k][0], g_world.own_ref[k][2] - g_world.own_pos[k][2]);
    auto        put = [&](const float *point, float *out) {
        const float x = point[0] - g_world.from_ref[0];
        const float z = point[2] - g_world.from_ref[2];
        const float lx = x * std::cos(yaw0) - z * std::sin(yaw0); // (into the start's frame: turned back by its heading)
        const float lz = x * std::sin(yaw0) + z * std::cos(yaw0);

        out[0] = g_world.own_ref[k][0] + lx * std::cos(yawk) + lz * std::sin(yawk); // (out into the watcher's, turned by theirs)
        out[1] = g_world.own_ref[k][1] + (point[1] - g_world.from_ref[1]);
        out[2] = g_world.own_ref[k][2] - lx * std::sin(yawk) + lz * std::cos(yawk);
        out[3] = 1.0f;
    };
    sceVu0FVECTOR own_pos;
    sceVu0FVECTOR own_ref;

    put(pos, own_pos);
    put(ref, own_ref);
    if (!g_view_camera_made) {
        new (g_view_camera) CCameraFollow(60.0f, 20.0f, 0.0f, 4.0f);
        g_view_camera_made = true;
    }
    auto *camera = std::launder(reinterpret_cast<CCameraFollow *>(g_view_camera));

    camera->FollowOff();
    camera->SetPos(own_pos);
    camera->SetRef(own_ref);
    camera->SetRoll(0.0f);
    camera->Step(-1);
    camera->GetCameraMatrix(view);
    camera->GetPos(eye);
    g_dt_frames_view++;
    DayTransViewSay(eye, view);
    return true;
}

int CoopEventSlotsStepPlayer() {
    return g_stepping;
}

bool CoopEventSlotsAnyParked() {
    for (const Parked &slot : g_parked) {
        if (slot.on) {
            return true;
        }
    }
    return false;
}

bool CoopEventSlotParked(int index) {
    return index >= 0 && index < coop::kMaxPlayers && g_parked[index].on;
}

bool CoopFishingOn(int index) {
    return index >= 0 && index < coop::kMaxPlayers && g_fishing[index].on;
}

int CoopFishingStateOf(int index) {
    if (index < 0 || index >= coop::kMaxPlayers) {
        return -1;
    }
    if (g_fishing[index].on) {
        return g_fish_state_now[index];
    }
    // (player 1 fishing by retail's own path: alone, or apart; the live set is his)
    return index == 0 && ::GameMode == ED_MODE_FISHING ? chara_fishing : -1;
}

// coop-dayhouses (the developer 11-18: "when P1 was fishing, P2 saw the time of day change cinematic, and then all houses became invisible to them").
// Cause: an event's script clips the town's ground to a sphere (_SET_CLIP_POINT: CEditGround::clip_plane; CEditGround::Draw skips every
// placed part outside it). Retail's one player only meets it inside the event's own scene, and retail's event end (editloop's return to
// the walk) takes it off. The pond's board (event 11) sets one around the pond and answers "fish": retail keeps it through its fishing
// (its one screen looks at the pond) and the quit's event (133) ends in the walk, which takes it off. Together the board runs on the
// live engine for its player alone and its fishing is theirs (coop-eventslots C): the clip landed on the town's one ground, which every
// player's screen draws, and stayed there (the fishing's quit runs in its player's parked slot, no retail end): every house further than
// its radius from the pond was gone from every other player's screen from the board on, and from the fisher's own after their fishing.
// the playtest's P2 stood by the board, so her houses went as P1's fishing began; the time of day's scene (its camera high over the town) was the
// first view far from the pond. Now together outdoors a clip point an own event, an own fishing or the world slot's scene sets is that
// scene's own: kept for its player (by player, one each, design for six) or for the world slot's watchers, and put on the ground only
// for that player's own screen's frame (CoopEventClipDrawBegin/End: the town's ground is never left changed); a player's clip goes
// as their own events and fishing are all over, the world slot's as its scene ends. DC_COOP_MUTATE=event_clip_shared (twin): retail's,
// on the town's one ground.
namespace {
struct ClipPoint {
    bool  on = false;
    float at[4] = {};
};
ClipPoint g_clip[coop::kMaxPlayers]; // each player's own events' and fishing's clip point, by player (1 up) less one
ClipPoint g_world_clip;              // the world slot's scene's (its watchers')
float     g_clip_kept[4] = {};       // the town's ground's own while a screen's frame draws with a player's
bool      g_clip_in = false;

void ClipSay(const char *whose, int player, const ClipPoint &c, const char *what) {
    std::fprintf(stderr, "coop-dayhouses: %s%s%.0d's clip point %s (%.1f %.1f %.1f) radius %.1f at link tick %lld\n", whose, player > 0 ? " " : "", player, what,
                 c.at[0], c.at[1], c.at[2], c.at[3], (long long) NetplayLinkTick());
    std::fflush(stderr);
}

// Test hook (DC_COOP_TEST_DAYHOUSES=1): every 60 town ticks, as this copy's screen's frame draws the ground, its placed parts that
// CEditGround::Draw's own tests take (their area shown, inside the clip point) and the clip point it draws with.
void PartsSay() {
    static const bool on = TestEnvOn("DC_COOP_TEST_DAYHOUSES");
    static long long  said = -1;

    if (!on || pEditGround == nullptr || (NetplayLinkTick() % 60) != 0 || said == NetplayLinkTick()) {
        return;
    }
    said = NetplayLinkTick();
    CEditGround &g = *pEditGround;
    int          placed = 0;
    int          area_hidden = 0;
    int          clip_hidden = 0;

    for (CMapParts &part : g.parts) {
        if (part.handle < 0) {
            continue;
        }
        placed++;
        if (part.area >= 0 && (part.area >= 4 || g.area_visible[part.area] == 0)) {
            area_hidden++;
            continue;
        }
        sceVu0FVECTOR at;

        part.GetPosition(at);
        if (!(g.clip_plane[3] <= 0.0f) && !(DistVector(at, g.clip_plane) <= g.clip_plane[3]) && !part.ChangeDigData()) {
            clip_hidden++;
        }
    }
    std::fprintf(stderr, "coop-dayhouses: test: player %d's screen draws the town's parts %d of %d (area hidden %d, clip hidden %d; clip point (%.1f %.1f %.1f) radius %.1f) at link tick %lld\n",
                 NetplayLocalPlayer(), placed - area_hidden - clip_hidden, placed, area_hidden, clip_hidden, g.clip_plane[0], g.clip_plane[1], g.clip_plane[2],
                 g.clip_plane[3], (long long) NetplayLinkTick());
}
} // namespace

// _SET_CLIP_POINT: true when the clip point is the running scene's own (kept for it, the town's ground left as it is).
bool CoopEventClipPoint(const float *point, float radius) {
    if (!Together() || EdInteriorFlag != 0 || Mutated("event_clip_shared")) {
        return false;
    }
    ClipPoint *c = g_event_player >= 1 && g_event_player <= coop::kMaxPlayers ? &g_clip[g_event_player - 1] : g_world_in ? &g_world_clip : nullptr;

    if (c == nullptr) {
        return false;
    }
    c->on = true;
    std::memcpy(c->at, point, sizeof(float) * 3);
    c->at[3] = radius;
    ClipSay(c == &g_world_clip ? "the time of day's scene" : "player", c == &g_world_clip ? 0 : g_event_player, *c, "set, its own");
    return true;
}

// Each copy's screen's town frame: the ground with this copy's player's own clip point (their event's or fishing's; else the world
// slot's scene's while they watch it), the town's own put back after the frame.
void CoopEventClipDrawBegin() {
    g_clip_in = false;
    if (pEditGround != nullptr && Together() && EdInteriorFlag == 0) {
        const int        n = NetplayLocalPlayer();
        const ClipPoint *c = n >= 1 && n <= coop::kMaxPlayers && g_clip[n - 1].on ? &g_clip[n - 1] : g_world_clip.on && CoopDayTransHoldsPlayer(n) ? &g_world_clip : nullptr;

        if (c != nullptr) {
            std::memcpy(g_clip_kept, pEditGround->clip_plane, sizeof g_clip_kept);
            std::memcpy(pEditGround->clip_plane, c->at, sizeof c->at);
            g_clip_in = true;
        }
    }
    PartsSay();
}

void CoopEventClipDrawEnd() {
    if (g_clip_in && pEditGround != nullptr) {
        std::memcpy(pEditGround->clip_plane, g_clip_kept, sizeof g_clip_kept);
    }
    g_clip_in = false;
}

// A player's clip point goes as their own events and fishing are all over (every copy alike, after the event steps); apart, retail's.
static void ClipsOver(bool together) {
    for (int k = 0; k < coop::kMaxPlayers; k++) {
        if (g_clip[k].on && (!together || (!g_parked[k].on && !g_fishing[k].on && g_live_own != k + 1))) {
            g_clip[k].on = false;
            ClipSay("player", k + 1, g_clip[k], "off: their own scenes are over");
        }
    }
    if (g_world_clip.on && (!together || !g_world.on)) {
        g_world_clip.on = false;
        ClipSay("the time of day's scene", 0, g_world_clip, "off: its scene is over");
    }
}

bool CoopFishingHere() {
    return CoopFishingOn(NetplayLocalPlayer() - 1);
}

// The town's texture block for one an event or its fishing names, while player p's own event or fishing runs together: p's own
// (eight a player from 80: the fish, the fish caught, the bait, the event's 45 to 49 in a parked slot; the fishing body's: p's
// own body's). Retail's own number otherwise. DC_COOP_MUTATE=event_blocks_shared (twin): retail's numbers always.
int CoopEventTexBlock(int block) {
    const int p = g_event_player;

    if (p <= 0 || !Together() || Mutated("event_blocks_shared")) {
        return block;
    }
    const int base = 80 + 8 * (p - 1);
    int       own = block;

    switch (block) {
        case 8:
            own = CoopTownPlayerTextureBlock(p - 1);
            break;
        case 54:
            own = base;
            break;
        case 55:
            own = base + 1;
            break;
        case 40:
            own = base + 2;
            break;
        case 45:
        case 46:
        case 47:
        case 48:
        case 49:
            own = g_stepping == p ? base + 3 + (block - 45) : block;
            break;
        default:
            break;
    }
    // coop-owed1112 (x-eventslots-bothfish reads it; twin event_blocks_shared): each player's first use of each own block, once.
    if (static std::uint64_t said[coop::kMaxPlayers] = {}; own != block && p <= coop::kMaxPlayers && block < 64 && (said[p - 1] & (1ull << block)) == 0) {
        said[p - 1] |= 1ull << block;
        std::fprintf(stderr, "coop-eventslots: player %d's event texture block %d is their own %d (link tick %lld)\n", p, block, own, (long long) NetplayLinkTick());
    }
    return own;
}

// An own event together never displaced the town's villagers (its own arena and blocks): it keeps them, loads none again.
// DC_COOP_MUTATE=own_event_villagers (twin): retail's wipe and load again, as before.
bool CoopEventKeepsVillagers() {
    if (g_event_player <= 0 || !Together() || EdInteriorFlag != 0 || Mutated("own_event_villagers")) {
        return false;
    }
    if (static long long said = -1; said != NetplayLinkTick()) {
        said = NetplayLinkTick();
        std::fprintf(stderr, "coop-eventslots: player %d's own event keeps the town's villagers (link tick %lld)\n", g_event_player, (long long) NetplayLinkTick());
    }
    return true;
}

// _LOAD_MAIN_CHARA in an own event whose main character is another player's body: retail loads into the game's player's
// (MainChara); this loads into that body (its own block and the event's arena), or that player's own body again for the town's.
bool CoopEventLoadMainChara(char *model, char *info, CDataAlloc2<1> *arena) {
    if (g_event_player <= 0 || !Together() || EdEventInfo.main_character == nullptr || EdEventInfo.main_character == Chara || Mutated("fishing_body_his")) {
        return false;
    }
    const int      k = g_event_player - 1;
    CTexAnimeData *anime = nullptr;
    int            anime_count = 0;
    CCharacter    *body = CoopTownPlayerBody(k, &anime, &anime_count);

    if (body == nullptr || body != EdEventInfo.main_character) {
        return false;
    }
    if (model == nullptr || std::strstr(model, "_turi") == nullptr) {
        return CoopTownPlayerBodyAgain(k);
    }
    sceVu0FVECTOR pos;
    sceVu0FVECTOR rot;

    body->GetPosition(pos);
    body->GetRotation(rot);
    LoadFile(model, read_buffer, nullptr);
    const int block = CoopTownPlayerTextureBlock(k);

    TexManager.DeleteTextureBlock(block);
    TexManager.CleanUpBuffer();
    body->Initialize();
    if (anime != nullptr) {
        for (int i = 0; i < anime_count; i++) {
            anime[i].Initialize();
        }
        body->InitializeTexAnime(anime, anime_count);
    }
    body->LoadPackData2(read_buffer, info, arena != nullptr ? arena : &g_parked_alloc[k], block, arena != nullptr ? arena : &g_parked_alloc[k], 0);
    if (body->frame != nullptr) {
        CFrameAttr attr;

        attr.clip_enable = false;
        attr.fog_enable = true;
        body->frame->SetAttr(attr, 1, 4);
    }
    body->SetPosition(pos);
    body->SetRotation(rot);
    std::fprintf(stderr, "coop-eventslots: player %d's own event loads %s into their own body (block %d) at link tick %lld\n", k + 1, model, block,
                 (long long) NetplayLinkTick());
    std::fflush(stderr);
    return true;
}

// The live engine starts an own event of player (1 up) together: its arena theirs (the town's buffers kept aside), their fishing
// set live. Returns the arena EdEventInit takes. DC_COOP_MUTATE=own_arena_off (twin): the town's EdWorkBuffer, as before.
void *CoopEventSlotsLiveInitIn(void *work, int player, int event_no) {
    if (!Together() || EdInteriorFlag != 0 || player < 1 || player > coop::kMaxPlayers ||
        !(event_no > 0 && CoopWorldsPartnerRoomEvent(EdEventData, event_no) && CoopTownPlayerEventIsMap(event_no))) {
        return work;
    }
    const int k = player - 1;

    if (g_parked[k].on) {
        return work;
    }
    g_live_own = player;
    g_event_player = player;
    FishSwap(k, false);
    if (Mutated("own_arena_off")) {
        g_town_arena = false;
        return work;
    }
    g_town_arena = true;
    BuffersTake(g_town_buffers);
    if (g_buffers[k].valid) {
        BuffersPut(g_buffers[k]);
    }
    ArenaFresh(k);
    return &g_parked_alloc[k];
}

void CoopEventSlotsLiveInitOut(bool started) {
    if (g_live_own == 0 || g_event_player != g_live_own) {
        return;
    }
    const int k = g_live_own - 1;

    FishSwap(k, false);
    g_event_player = 0;
    if (!started) {
        OwnEventEnds(k, true);
        g_live_own = 0;
    }
}

// Around the live engine's step of that own event.
void CoopEventSlotsLiveIn() {
    if (g_live_own == 0 || CoopTownOwnTownEvent() != g_live_own) {
        return;
    }
    g_live_in = g_live_own;
    g_event_player = g_live_own;
    FishSwap(g_live_own - 1, false);
}

void CoopEventSlotsLiveOut() {
    if (g_live_in == 0) {
        return;
    }
    FishSwap(g_live_in - 1, false);
    g_event_player = 0;
    g_live_in = 0;
}

// Retail's event mode, the live event answering "fish": together, its player's own fishing (true: the town walks on).
bool CoopEventFishingStarts() {
    if (g_live_in == 0 || Mutated("live_fishing_mode")) {
        return false;
    }
    FishingStarts(g_live_in - 1, "the live event's answer");
    return true;
}

// The game's player's walk step: his own fishing set live around it while he fishes together; his fishing's Circle or Square
// starts his own event in his parked slot (retail's walk would have run it as the town's).
static bool                        g_walk_fishing = false;
static std::optional<FishBuffersIn> g_walk_buffers;

void CoopFishingWalkIn() {
    // A merge into retail's fishing (begun apart, where the copy was its player's alone, the world the merge took his): together the town
    // never stays in that one mode; it is the game's player's own fishing from this step, its set and buffers as retail left them, and the
    // town walks (the arriver walks on beside it). DC_COOP_MUTATE=apart_fishing_kept (twin): retail's mode kept, as before.
    if (::GameMode == ED_MODE_FISHING && !g_fishing[0].on && Together() && !Mutated("apart_fishing_kept") && !Mutated("live_fishing_mode")) {
        BuffersTake(g_buffers[0]);
        g_buffers[0].valid = true;
        FishingStarts(0, "carried from apart by the merge");
        // coop-dayhouses: the board's clip point retail put on the town's ground apart is his fishing's own from here
        if (pEditGround != nullptr && !(pEditGround->clip_plane[3] <= 0.0f) && !Mutated("event_clip_shared")) {
            g_clip[0].on = true;
            std::memcpy(g_clip[0].at, pEditGround->clip_plane, sizeof g_clip[0].at);
            pEditGround->clip_plane[3] = -1.0f;
            ClipSay("player", 1, g_clip[0], "carried from apart, their own");
        }
        ::GameMode = ED_MODE_WALK;
    }
    g_walk_fishing = g_fishing[0].on && Together();
    if (!g_walk_fishing) {
        if (::GameMode == ED_MODE_FISHING && CoopWorldsApart()) {
            CatchPressForTest(GamePad); // (coop-fishing3: apart, retail's own fishing of this copy's player: the test's presses too)
        }
        return;
    }
    FeedPad(0, g_fishing[0].pad);
    g_event_player = 1;
    FishSwap(0, true);
    g_walk_buffers.emplace(0);
    CatchPressForTest(GamePad);
    // (DC_COOP_TEST_FISH_TRACE: his Cross as his fishing step will read it)
    if (static const bool on = TestEnvOn("DC_COOP_TEST_FISH_TRACE"); on && GamePad.On(0x40) != 0) {
        std::fprintf(stderr, "coop-eventslots: test: player 1's Cross while fishing (state %d): down %d, as the walk reads it %d, key lock %d, system message %d at link tick %lld\n",
                     chara_fishing, GamePad.Down(0x40), EdPadDown(0x40, 0xFFFF), (int) EdMoveCharaInfo.key_lock, EdSystemMesCheck(), (long long) NetplayLinkTick());
    }
}

void CoopFishingWalkOut() {
    if (!g_walk_fishing) {
        if (::GameMode == ED_MODE_FISHING && CoopWorldsApart()) {
            FishTrace(std::clamp(NetplayRole() - 1, 0, coop::kMaxPlayers - 1)); // (coop-fishing3: apart, this copy's player's fishing, traced)
        }
        return;
    }
    FishTrace(0);
    g_walk_buffers.reset();
    FishSwap(0, true);
    g_event_player = 0;
    g_fishing[0].steps++;
    const int event = EdMoveCharaInfo.event_no;

    if (event == kFishQuit || event == kFishBait) {
        g_parked[0].queued = event;
        EdMoveCharaInfo.event_no = -1;
        EdMoveCharaInfo.system_event_no = -1;
        EdMoveCharaInfo.acted = false;
    }
    g_walk_fishing = false;
}

// The other players' fishing steps (CoopTownStep, where their walk would run).
void CoopFishingStepOthers() {
    if (!Together()) {
        return;
    }
    for (int k = 1; k < coop::kMaxPlayers; k++) {
        if (g_fishing[k].on && !g_parked[k].on) {
            FishStep(k);
        }
    }
}

// coop-pondfish (the developer, 11-19: "make it so the fish in the pond are synced"; the developer's rules of 11-19, for v0.2). Retail rolls a board's 4 or 5
// fish (rand) at each read of it (_LOAD_FISHING_DATA: FishingLoadFish), places them (_INIT_FISH), steps all six every frame against
// its one hook (FishingStepFish: CFish::Step's approach, nibble, bite, leave on rand) and draws them while its player fishes; the
// Cross hooks the one biting; a catch takes it out (FishingDeleteAngleFish: its kind -1) till the next read rolls fresh fish. Cause
// (coop-eventslots C): each fisher's own fishing set rolled fish of their own into their own arena, drawn on their own screen alone,
// so the others saw no fish in the water, nor the fish caught in the fisher's hand (the rc5b playtest 11-19: "p2 also cant actually
// see the fish p1 caught"), and two fishers at one board fished two different ponds. Now, in a co-op game (NetplayConfigured: the
// host alone too; a single-player START or CONTINUE stays retail), each pond retail's boards fish (kPondDefs: from the boards' own
// scripts, e01's program 11, e02's 13 and 14, e04's 12: their _LOAD_FISHING_DATA and _INIT_FISH, about the pond's Georama part
// where there is one) is one shared pond, always in the water: retail's roll of its fish and two more (6 or 7), made as the town
// loads, stepped every town tick on every copy (CoopPondTick) and drawn on every screen. A board read joins it (no roll). Every
// fisher fishes every fish: a fish goes for the nearest baited hook, as retail's to its one; its nibble and bite are at that hook
// and at any other hook right at it (BiteHook's reach, 1), where the first Cross takes it (the fishers step in player order: on one
// tick the lower player's); hooked it is on that fisher's line till landed or gone. A catch empties its slot: kPondRefillTicks later
// the slot rolls a fresh fish (retail's kinds and odds for that pond). Each one's rod, line, help bar and catch window stay their
// own; every screen draws each catch held up. DC_COOP_MUTATE=pond_own (twin): each fisher's own fish on their screen alone, as
// before; catch_hidden: a catch on its fisher's screen alone; pond_tie_upper: a bite at two hooks goes to the higher player;
// pond_refill_off: a caught fish's slot stays empty.
namespace {
constexpr int         kPondSlots = 8;   // a pond's fish slots (retail's 6: its 4 or 5 and two more need 7)
constexpr int         kPondExtra = 2;   // the developer, 11-19: retail's roll and two
constexpr int         kMostPonds = 2;   // a town's ponds (Matataki's two)
constexpr int         kPondBlock = 140; // pond j's fish texture block 140 + j (the town's bodies are 128 to 139)
// A slot refills 600 ticks (10 s) after its catch empties it: about one retail catch from the hook to the next float in the water
// (measured, her catch in tests/test_pondfish.py: hooked at link tick 929, standing again with the rod at 1369 (the fight's 120 and
// more, the catch held up, its window closed at once, FINISH's 120 and its motion: 440), the next cast to the float in the water
// 151; 591 in all, a human's reading of the window more). One fisher catching without a pause then keeps all but one fish of 6
// or 7; two keep 4 or 5: nobody waits on an empty pond.
constexpr int         kPondRefillTicks = 600;
constexpr std::size_t kPondFishBytes = (sizeof(CFish) * kPondSlots + 63) & ~static_cast<std::size_t>(63);
constexpr int         kPondMostWalls = 1024; // (_LOAD_FISHING_DATA's own limit)
constexpr std::size_t kPondWallBytes = sizeof(CCPoly) * kPondMostWalls;
constexpr std::size_t kPondModelBytes = 192 * 1024; // a slot's fish model (LoadFish: 143 KB measured)
constexpr std::size_t kPondBytes = kPondFishBytes + kPondWallBytes + kPondModelBytes * kPondSlots;

struct PondDef {
    int   map;      // the town (MapNo)
    int   spot;     // retail's fishing spot (its fish set, _LOAD_FISHING_DATA's first)
    int   part;     // the Georama part the board's script measures from (_GET_EDIT_PARTS_POS), -1: the numbers are the town's own
    float area[4];  // the board's fishing area: x0 z0 x1 z1 (about the part's place)
    float start[4]; // _INIT_FISH's box (its centre, at the water less 12, is where the fish start)
    float water;    // _LOAD_FISHING_DATA's water level
};
constexpr PondDef kPondDefs[] = {
    {0, 0, 12, {-100.0f, -100.0f, 100.0f, 100.0f}, {-50.0f, -50.0f, 50.0f, 50.0f}, -13.0f},        // Norune (e01 program 11)
    {1, 1, -1, {-1340.0f, -140.0f, -750.0f, 200.0f}, {-1181.0f, -10.0f, -964.0f, 50.0f}, -13.0f},  // Matataki (e02 13/14, its first)
    {1, 2, -1, {550.0f, 1000.0f, 1200.0f, 1200.0f}, {650.0f, 1120.0f, 1050.0f, 1209.0f}, -13.0f},  // Matataki (its second)
    {3, 4, 11, {-160.0f, -160.0f, 160.0f, 160.0f}, {-50.0f, -50.0f, 50.0f, 50.0f}, -5.0f},         // Muska Lacka (e04 program 12)
};

struct Pond {
    bool      on = false;
    int       def = -1;
    int       map = -1;
    int       spot = -1;
    CFish    *fish = nullptr;           // its slots, in its own arena
    int       num = 0;                  // the slots in use (6 or 7)
    CCPoly   *walls = nullptr;          // the fish's walls (the area's ground polygons), in its arena
    int       wall_count = 0;
    float     start[4] = {};            // where a fish starts (a fresh one too)
    int       line[kPondSlots] = {};    // the player (1 up) whose line holds the fish (hooked, fought, held up), 0 none
    int       target[kPondSlots] = {};  // the player (1 up) whose hook it goes for, 0 none
    int       refill[kPondSlots] = {};  // ticks left till an empty slot's fresh fish, 0 none
    float     hook[coop::kMaxPlayers][4] = {}; // each fisher's hook, bait and the bait's reach, as their last fishing step left them
    int       food[coop::kMaxPlayers] = {};
    float     reach[coop::kMaxPlayers] = {};
    long long recorded[coop::kMaxPlayers] = {-1, -1, -1, -1, -1, -1};
    long long made = -1;
    long long stepped = -1;
};
Pond g_ponds[kMostPonds];
alignas(64) unsigned char g_pond_arena[kMostPonds][kPondBytes];
CCPoly g_pond_polys[kPondMostWalls + 64]; // (scratch: the area's polygons as the ground gives them, before the copy)
bool   g_pond_joined[coop::kMaxPlayers] = {}; // the board read just joined a pond: _INIT_FISH leaves its fish where they swim
// The ponds' own clock: one a town tick (CoopPondTick), every copy alike (the link tick stands still apart). A fisher's hook is the
// pond's food on the tick it was given (their step comes before the tick's CoopPondTick).
long long g_pond_clock = 0;

// This copy's frames that drew the ponds (presentation only, for the test's trace), and each fisher's catch drawn here.
int g_pond_drawn_shown = 0;
int g_catch_shown[coop::kMaxPlayers] = {};
int g_catch_hidden[coop::kMaxPlayers] = {};
int g_catch_kind[coop::kMaxPlayers] = {-1, -1, -1, -1, -1, -1};

bool PondsOn() {
    return !Mutated("pond_own");
}

// The town outdoors in a co-op game, where the ponds are.
bool PondsHere() {
    return PondsOn() && NetplayConfigured() && dc::game::mode == 2 && EdInteriorFlag == 0 && pEditGround != nullptr;
}

// The fisher (0 up) whose fishing runs now: the running own event's or fishing's player together; apart or alone, the game's player,
// whose retail fishing a merge carries as player 1's own (CoopFishingWalkIn), also in a catch-up's replay of another copy's world.
int PondFisher() {
    return Together() ? std::clamp(g_event_player - 1, 0, coop::kMaxPlayers - 1) : 0;
}

int PondOfFish(const CFish *fish) {
    for (int j = 0; fish != nullptr && j < kMostPonds; j++) {
        if (g_ponds[j].on && g_ponds[j].fish == fish) {
            return j;
        }
    }
    return -1;
}

bool BlockHolds(int block) {
    for (const CTexture &texture : TexManager.textures) {
        if (texture.block == block) {
            return true;
        }
    }
    return false;
}

std::string PondKinds(const Pond &p) {
    std::string out;

    for (int i = 0; i < p.num; i++) {
        out += (i > 0 ? " " : "") + std::to_string(p.fish[i].fish_kind);
    }
    return out;
}

// Slot i's fish: retail's LoadFish (its model in the slot's own part of the arena, the pond's block), at the start, in its walls.
void SlotLoad(int j, int i, int kind, bool textures_loaded) {
    Pond          &p = g_ponds[j];
    CDataAlloc2<1> slot(-1);

    slot.base = g_pond_arena[j] + kPondFishBytes + kPondWallBytes + kPondModelBytes * i;
    slot.limit = static_cast<int>(kPondModelBytes / 16);
    slot.used = 0;
    LoadFish(&p.fish[i], kind, kPondBlock + j, &slot, textures_loaded);
    sceVu0FVECTOR at = {0.5f * (p.start[0] + p.start[2]), kPondDefs[p.def].water - 12.0f, 0.5f * (p.start[1] + p.start[3]), 1.0f};

    p.fish[i].SetPosition(at);
    p.fish[i].SetCPoly(p.walls, p.wall_count);
    p.line[i] = 0;
    p.target[i] = 0;
    p.refill[i] = 0;
}

void PondMake(int j, int d) {
    const PondDef &def = kPondDefs[d];
    Pond          &p = g_ponds[j];
    float          x = 0.0f, z = 0.0f;

    if (def.part >= 0) {
        CMapParts *part = pEditGround->GetPartsObject(def.part);

        if (part == nullptr) { // (no pond built here: no board either)
            p = Pond{};
            return;
        }
        sceVu0FVECTOR at;

        part->GetPosition(at);
        x = at[0];
        z = at[2];
    }
    p = Pond{};
    p.on = true;
    p.def = d;
    p.map = def.map;
    p.spot = def.spot;
    p.made = NetplayLinkTick();
    p.start[0] = x + def.start[0];
    p.start[1] = z + def.start[1];
    p.start[2] = x + def.start[2];
    p.start[3] = z + def.start[3];
    p.fish = reinterpret_cast<CFish *>(g_pond_arena[j]);
    p.walls = reinterpret_cast<CCPoly *>(g_pond_arena[j] + kPondFishBytes);
    for (int i = 0; i < kPondSlots; i++) {
        new (&p.fish[i]) CFish();
    }
    CBoxVu0 area;

    area.min[0] = x + def.area[0];
    area.min[1] = -1000.0f;
    area.min[2] = z + def.area[1];
    area.max[0] = x + def.area[2];
    area.max[1] = 1000.0f;
    area.max[2] = z + def.area[3];
    p.wall_count = std::clamp(pEditGround->PickUpPoly(g_pond_polys, area, 0), 0, kPondMostWalls);
    std::memcpy(static_cast<void *>(p.walls), g_pond_polys, sizeof(CCPoly) * static_cast<std::size_t>(p.wall_count));
    TexManager.DeleteTextureBlock(kPondBlock + j);
    TexManager.CleanUpBuffer();
    p.num = (def.spot == 0 || def.spot == 4 ? 4 : 5) + kPondExtra; // (retail's FishNum, and two)
    for (int i = 0; i < p.num; i++) {
        SlotLoad(j, i, FishingRollKind(def.spot), i > 0);
    }
    std::fprintf(stderr, "coop-pondfish: the pond %d (spot %d, map %d) is made with %d fish [%s], %d walls, at (%.0f %.0f) at link tick %lld\n", j, p.spot, p.map, p.num,
                 PondKinds(p).c_str(), p.wall_count, 0.5f * (p.start[0] + p.start[2]), 0.5f * (p.start[1] + p.start[3]), (long long) NetplayLinkTick());
    std::fflush(stderr);
}

// A fisher's caught fish loading into the read buffer (BATTLE: LoadFileBG till its ReadBGSync), which LoadFish's LoadFile would
// write over: a slot's refill waits for it.
bool AnyFightLoading() {
    if (chara_fishing == ED_FISHING_BATTLE) { // (apart or alone: retail's own fishing, its set live)
        return true;
    }
    for (int k = 0; k < coop::kMaxPlayers; k++) {
        if (g_fishing[k].on && g_fish_state_now[k] == ED_FISHING_BATTLE) {
            return true;
        }
    }
    return false;
}

// At hook m (0 up): the fish goes for m's hook, or m's hook is right at it (BiteHook's reach). Test hook DC_COOP_TEST_POND_ONE_HOOK:
// every fisher's hook at every bite (two Crosses on one bite, without placing two hooks by hand).
bool AtHook(const Pond &p, int i, int m) {
    static const bool one = TestEnvOn("DC_COOP_TEST_POND_ONE_HOOK");

    if (p.recorded[m] < 0) {
        return false;
    }
    if (p.target[i] == m + 1 || one) {
        return true;
    }
    sceVu0FVECTOR at;

    const_cast<CFish &>(p.fish[i]).GetPosition(at);
    return std::hypot(p.hook[m][0] - at[0], p.hook[m][1] - at[1], p.hook[m][2] - at[2]) <= 1.0f;
}

void PondTrace(int j);

// Pond j's step, once a town tick, every copy alike: an empty slot's refill, then retail's FishingStepFish for each fish in the water,
// its food the nearest baited hook of the fishers who stepped this tick (no bait: the nearest hook, nothing found).
void PondStep(int j) {
    Pond      &p = g_ponds[j];
    const auto tick = g_pond_clock;

    if (p.stepped == tick) {
        return;
    }
    p.stepped = tick;
    for (int i = 0; i < p.num; i++) {
        CFish &fish = p.fish[i];

        if (fish.fish_kind >= 0) {
            continue;
        }
        if (Mutated("pond_refill_off")) { // (twin: a caught fish's slot stays empty, as retail's till the next read)
            continue;
        }
        if (p.refill[i] == 0) {
            p.refill[i] = kPondRefillTicks;
        } else if (--p.refill[i] == 0) {
            if (AnyFightLoading()) {
                p.refill[i] = 1;
                continue;
            }
            SlotLoad(j, i, FishingRollKind(p.spot), true);
            std::fprintf(stderr, "coop-pondfish: the pond %d's slot %d rolls a fresh fish (kind %d), %d ticks after it was caught, at link tick %lld\n", j, i, fish.fish_kind,
                         kPondRefillTicks, (long long) NetplayLinkTick());
        }
    }
    for (int i = 0; i < p.num; i++) {
        CFish &fish = p.fish[i];

        if (fish.fish_kind < 0 || fish.use_angle_model != 0) { // (a catch held up: its fisher's step steps it, as retail's)
            continue;
        }
        sceVu0FVECTOR at;

        fish.GetPosition(at);
        int by = 0;

        for (int pass = 0; by == 0 && pass < 2; pass++) {
            float best = 0.0f;

            for (int k = 0; k < coop::kMaxPlayers; k++) {
                if (p.recorded[k] != tick || (pass == 0 && p.food[k] < 0)) {
                    continue;
                }
                const float d = std::hypot(p.hook[k][0] - at[0], p.hook[k][1] - at[1], p.hook[k][2] - at[2]);

                if (by == 0 || d < best) {
                    by = k + 1;
                    best = d;
                }
            }
        }
        if (by == 0) {
            fish.SetFoodPos(-1, at, 0.0f);
        } else {
            fish.SetFoodPos(p.food[by - 1], p.hook[by - 1], p.food[by - 1] < 0 ? 0.0f : p.reach[by - 1]);
        }
        fish.Step();
        p.target[i] = by;
        if (p.line[i] != 0 && fish.action != FISH_ACTION_BATTLE && fish.action != FISH_ACTION_ANGLE && fish.use_angle_model == 0) {
            p.line[i] = 0; // (off the line: gone)
        }
    }
    PondTrace(j);
}

// Test hook (DC_COOP_TEST_POND_TRACE=1): every 30 link ticks each pond (each fish's kind, place, action, the hook it goes for and the
// line it is on), and this copy's frames that drew the ponds since the last line.
void PondTrace(int j) {
    static const bool on = TestEnvOn("DC_COOP_TEST_POND_TRACE");

    if (!on || NetplayLinkTick() % 30 != 0) {
        return;
    }
    const Pond &p = g_ponds[j];
    std::string fish;
    char        one[96];

    for (int i = 0; i < p.num; i++) {
        sceVu0FVECTOR at;

        const_cast<CFish &>(p.fish[i]).GetPosition(at);
        std::snprintf(one, sizeof one, "%s%d:%d@%.2f,%.2f/%d/%d/%d", i > 0 ? " " : "", i, p.fish[i].fish_kind, at[0], at[2], (int) p.fish[i].action, p.target[i],
                      p.line[i]);
        fish += one;
    }
    std::fprintf(stderr, "coop-pondfish: test: the pond %d (spot %d) at link tick %lld: %d fish [%s]\n", j, p.spot, (long long) NetplayLinkTick(), p.num, fish.c_str());
    if (j == 0) {
        std::fprintf(stderr, "coop-pondfish: test: the ponds drawn on player %d's copy by link tick %lld: %d frames in sight\n", NetplayLocalPlayer(),
                     (long long) NetplayLinkTick(), g_pond_drawn_shown);
        g_pond_drawn_shown = 0;
    }
}

// Each fisher's catch drawn on this copy, said as it is put away (test hook DC_COOP_TEST_POND_TRACE).
void CatchDrawnSay(int k) {
    static const bool on = TestEnvOn("DC_COOP_TEST_POND_TRACE");

    if (g_catch_shown[k] + g_catch_hidden[k] > 0 && on) {
        std::fprintf(stderr, "coop-pondfish: test: player %d's catch (kind %d) drawn on player %d's copy: %d frames in sight, %d out of sight, by link tick %lld\n", k + 1,
                     g_catch_kind[k], NetplayLocalPlayer(), g_catch_shown[k], g_catch_hidden[k], (long long) NetplayLinkTick());
    }
    g_catch_shown[k] = 0;
    g_catch_hidden[k] = 0;
    g_catch_kind[k] = -1;
}

// Every pond of the town on every screen (but each catch held up: its fisher's FishingDrawFish).
void PondsDraw() {
    if (!PondsHere()) {
        return;
    }
    for (int j = 0; j < kMostPonds; j++) {
        Pond &p = g_ponds[j];

        if (!p.on || p.map != MapNo) {
            continue;
        }
        TexManager.ReloadTexture(GetVif1Packet(), kPondBlock + j);
        for (int i = 0; i < p.num; i++) {
            if (p.fish[i].use_angle_model == 0) {
                p.fish[i].Draw();
            }
        }
        g_pond_drawn_shown++;
    }
}
} // namespace

// Each town tick (CoopEventSlotsAfterLive, CoopTownFishingModeStep), every copy alike: the town's ponds made as it loads (a load
// clears their texture blocks: made again), then stepped.
void CoopPondTick() {
    if (!PondsHere()) {
        return;
    }
    g_pond_clock++;
    int j = 0;

    for (int d = 0; d < static_cast<int>(std::size(kPondDefs)) && j < kMostPonds; d++) {
        if (kPondDefs[d].map != MapNo) {
            continue;
        }
        Pond &p = g_ponds[j];

        if (!(p.on && p.def == d && p.map == MapNo && BlockHolds(kPondBlock + j))) {
            if (p.on) {
                std::fprintf(stderr, "coop-pondfish: the pond %d (spot %d, map %d) goes with the town's load at link tick %lld\n", j, p.spot, p.map, (long long) NetplayLinkTick());
            }
            PondMake(j, d);
        }
        if (p.on) {
            PondStep(j);
        }
        j++;
    }
    for (; j < kMostPonds; j++) {
        g_ponds[j] = Pond{};
    }
}

// _LOAD_FISHING_DATA's fish (the fisher's set live): the board's pond, joined (its fish and count, its 8 slots; the fisher's own
// texture block kept for their catch: fish_texb + 1). False: retail's own roll (single-player, or no pond here).
bool CoopPondLoadFish(int spot, int slot) {
    if (!PondsOn() || !NetplayConfigured()) {
        return false;
    }
    const int k = PondFisher();

    for (int j = 0; j < kMostPonds; j++) {
        Pond &p = g_ponds[j];

        if (!p.on || p.map != MapNo || p.spot != spot) {
            continue;
        }
        Fish = p.fish;
        FishNum = p.num;
        FishSlots = kPondSlots;
        draw_under_water = 1;
        fish_texb = slot;
        p.recorded[k] = -1;
        g_pond_joined[k] = true;
        std::fprintf(stderr, "coop-pondfish: player %d's board joins the pond %d (spot %d): %d fish [%s] at link tick %lld\n", k + 1, j, spot, p.num, PondKinds(p).c_str(),
                     (long long) NetplayLinkTick());
        std::fflush(stderr);
        return true;
    }
    return false;
}

bool CoopPondKeepsFish() {
    const int k = PondFisher();
    const bool joined = g_pond_joined[k];

    g_pond_joined[k] = false;
    return PondsOn() && (joined || PondOfFish(Fish) >= 0);
}

// The running fisher's fishing ends: the pond's fish stay; one on their line goes free.
bool CoopPondExit() {
    const int j = PondsOn() ? PondOfFish(Fish) : -1;

    if (j < 0) {
        return false;
    }
    Pond     &p = g_ponds[j];
    const int k = PondFisher();

    for (int i = 0; i < kPondSlots; i++) {
        if (p.line[i] == k + 1) {
            p.line[i] = 0;
            p.fish[i].action = FISH_ACTION_NONE;
            p.fish[i].move_mode = FISH_MOVE_UNSET;
            p.fish[i].use_angle_model = false;
        }
    }
    p.recorded[k] = -1;
    return true;
}

// This fish's nibble or bite is at the running fisher's hook (or it is on their line): retail's status, hook and catch see it.
bool CoopPondFishMine(int fish_no) {
    const int j = PondsOn() ? PondOfFish(Fish) : -1;

    if (j < 0 || fish_no < 0 || fish_no >= kPondSlots) {
        return true;
    }
    const Pond  &p = g_ponds[j];
    const CFish &f = p.fish[fish_no];
    const int    k = PondFisher();

    if (p.line[fish_no] != 0) {
        return p.line[fish_no] == k + 1;
    }
    if (f.action != FISH_ACTION_EAT_FOOD && f.action != FISH_ACTION_BITE_HOOK) {
        return false;
    }
    if (!AtHook(p, fish_no, k)) {
        return false;
    }
    for (int m = k + 1; Mutated("pond_tie_upper") && m < coop::kMaxPlayers; m++) {
        if (AtHook(p, fish_no, m)) {
            return false; // (the twin: the higher player's)
        }
    }
    return true;
}

bool CoopPondFishReset(int fish_no) {
    const int j = PondsOn() ? PondOfFish(Fish) : -1;

    if (j < 0 || fish_no < 0 || fish_no >= kPondSlots) {
        return true;
    }
    Pond &p = g_ponds[j];

    if (p.line[fish_no] != PondFisher() + 1) {
        return false;
    }
    p.line[fish_no] = 0;
    return true;
}

void CoopPondOnLine(int fish_no) {
    const int j = PondsOn() ? PondOfFish(Fish) : -1;

    if (j < 0 || fish_no < 0 || fish_no >= kPondSlots) {
        return;
    }
    const int k = PondFisher();

    g_ponds[j].line[fish_no] = k + 1;
    std::fprintf(stderr, "coop-pondfish: player %d hooks fish %d (kind %d) of the pond %d at link tick %lld\n", k + 1, fish_no, g_ponds[j].fish[fish_no].fish_kind, j,
                 (long long) NetplayLinkTick());
}

void CoopPondRecord(float *hook, int food, float radius) {
    const int j = PondsOn() ? PondOfFish(Fish) : -1;

    if (j < 0) {
        return;
    }
    Pond     &p = g_ponds[j];
    const int k = PondFisher();

    std::memcpy(p.hook[k], hook, sizeof p.hook[k]);
    p.food[k] = food;
    p.reach[k] = radius;
    p.recorded[k] = g_pond_clock + 1; // (this tick's: its CoopPondTick comes after the fishers' steps)
}

bool CoopPondSteps() {
    return PondsOn() && PondOfFish(Fish) >= 0;
}

bool CoopPondDrawsFish() {
    return PondsOn() && PondOfFish(Fish) >= 0;
}

// The sync check's read (netplay.cpp Checksum, in the town): each pond's slots (kind, action, place but a held catch's, the hook it
// goes for, its line, its refill).
std::uint32_t CoopPondHash() {
    std::uint64_t h = 0xcbf29ce484222325ull;

    for (Pond &p : g_ponds) {
        if (!p.on) {
            continue;
        }
        const int head[3] = {p.def, p.map, p.num};

        h = Hash(reinterpret_cast<const unsigned char *>(head), sizeof head, h);
        h = Hash(reinterpret_cast<const unsigned char *>(p.line), sizeof p.line, h);
        h = Hash(reinterpret_cast<const unsigned char *>(p.target), sizeof p.target, h);
        h = Hash(reinterpret_cast<const unsigned char *>(p.refill), sizeof p.refill, h);
        for (int i = 0; i < p.num; i++) {
            CFish        &f = p.fish[i];
            const int     what[3] = {f.fish_kind, (int) f.action, (int) f.move_mode};
            sceVu0FVECTOR at = {};

            h = Hash(reinterpret_cast<const unsigned char *>(what), sizeof what, h);
            if (f.fish_kind >= 0 && f.use_angle_model == 0) {
                f.GetPosition(at);
                h = Hash(reinterpret_cast<const unsigned char *>(at), sizeof(float) * 3, h);
            }
        }
    }
    return static_cast<std::uint32_t>(h ^ (h >> 32));
}

// The town's draw: each pond's fish on every screen (coop-pondfish); each fisher's catch held up (every screen), line and float
// (every screen), and with the pond_own twin their own fish (their own screen alone: drawn out of sight elsewhere, as they step as
// they draw). pass 0 under the water's surface, 1 over it, as retail's two draws. DC_COOP_MUTATE=fish_shown (twin, without a pond):
// the fish on every screen.
void CoopFishingDraw(int pass) {
    if (pass == 0) {
        PondsDraw();
    }
    if (!Together()) {
        return;
    }
    for (int k = 0; k < coop::kMaxPlayers; k++) {
        if (!g_fishing[k].on) {
            continue;
        }
        const int player_was = g_event_player;

        g_event_player = k + 1;
        FishSwap(k, false);
        const int pond = PondOfFish(Fish); // coop-pondfish: their fish the pond's (PondsDraw, every screen)

        if (pass == 0 && pond >= 0) {
            // Their catch held up, on every screen (the fisher's own pose and line with it). DC_COOP_MUTATE=catch_hidden (twin): theirs alone.
            if (AngleFish != nullptr) {
                const bool hidden = NetplayLocalPlayer() != k + 1 && Mutated("catch_hidden") && HideBegin(k);

                FishingDrawFish();
                HideEnd(hidden);
                (hidden ? g_catch_hidden[k] : g_catch_shown[k])++;
                g_catch_kind[k] = AngleFish->fish_kind >= 0 ? AngleFish->fish_kind : g_catch_kind[k]; // (its kind goes as it is put away)
            } else {
                CatchDrawnSay(k);
            }
            g_fishing[k].frames_shown++; // (their fish, the pond's, in sight on this copy: the fishing trace's count)
            FishLineDraw(0);
        } else if (pass == 0) {
            const bool hidden = NetplayLocalPlayer() != k + 1 && !Mutated("fish_shown") && HideBegin(k);

            FishingDrawFish();
            HideEnd(hidden);
            (hidden ? g_fishing[k].frames_hidden : g_fishing[k].frames_shown)++;
            if (AngleFish != nullptr) {
                (hidden ? g_catch_hidden[k] : g_catch_shown[k])++;
                g_catch_kind[k] = AngleFish->fish_kind >= 0 ? AngleFish->fish_kind : g_catch_kind[k];
            } else {
                CatchDrawnSay(k);
            }
            FishLineDraw(0);
        } else {
            FishLineDraw(1);
        }
        FishSwap(k, false);
        g_event_player = player_was;
    }
}

// editloop's event mode (after the live event's step) and the town's walk, every copy alike: the live own event's end (the town's
// buffers back), parked starts and steps.
void CoopEventSlotsAfterLive() {
    CoopPondTick(); // coop-pondfish: the town's ponds, every copy (after every fisher's step this tick)
    if (g_live_own != 0 && CoopTownOwnTownEvent() != g_live_own) { // (the live own event ended: before anything else this tick)
        const int k = g_live_own - 1;

        OwnEventEnds(k, true);
        g_live_own = 0;
        // Test hook (DC_COOP_TEST_FISH_TRACE): a fisher's fish against the town's villager buffer as their board ends (the villagers
        // load there again at the period's change: inside, they are written over, the crash x-eventslots fishclock guards).
        if (static const bool on = TestEnvOn("DC_COOP_TEST_FISH_TRACE"); on && g_fishing[k].on) {
            FishSwap(k, false);
            const auto *fish = reinterpret_cast<const unsigned char *>(Fish);
            const auto *from = EdVillagerBuffer.base;
            const bool  inside = fish != nullptr && from != nullptr && fish >= from && fish < from + static_cast<std::ptrdiff_t>(EdVillagerBuffer.limit) * 16;

            FishSwap(k, false);
            std::fprintf(stderr, "coop-eventslots: test: player %d's fish lie %s the town's villager buffer at link tick %lld\n", k + 1, inside ? "INSIDE" : "outside",
                         (long long) NetplayLinkTick());
        }
    }
    if (!Together() || EdInteriorFlag != 0 || Mutated("eventslots_off")) {
        for (Parked &slot : g_parked) {
            slot.cross_was = false;
        }
        ClipsOver(Together() && EdInteriorFlag == 0); // coop-dayhouses
        return;
    }
    const int live = CoopTownOwnTownEvent(); // (the live engine's own event's player, 1 up; 0 none or a shared one)

    // Test hook (DC_COOP_TEST_FISH_TRACE): the fishing points as each player has them, on each change (their own, or the party's pool).
    if (static const bool trace = TestEnvOn("DC_COOP_TEST_FISH_TRACE"); trace) {
        static int said[2] = {-1, -1};
        const int  now[2] = {coop::FishPointsOf(0), coop::FishPointsOf(1)};

        if (now[0] != said[0] || now[1] != said[1]) {
            said[0] = now[0];
            said[1] = now[1];
            std::fprintf(stderr, "coop-eventslots: test: fishing points: player 1 %d, player 2 %d (%s) at link tick %lld\n", now[0], now[1],
                         coop::FishingPerPlayer() ? "each player's" : "the party's", (long long) NetplayLinkTick());
        }
    }

    // Test hook (DC_COOP_TEST_TIE=1): the tie made exact. As a player's own event goes live, every other player in reach of a board is
    // given a Cross on that very tick (the tick nothing was live yet), as two presses landing on one tick are: theirs must start at
    // this step (the tie press kept, below). Every copy alike.
    static int live_was = 0;

    if (static const bool tie = TestEnvOn("DC_COOP_TEST_TIE"); tie && live != 0 && live_was == 0) {
        for (int k = 0; k < coop::kMaxPlayers; k++) {
            if (k != live - 1 && !g_parked[k].on && CoopTownPlayerEventAt(k) > 0) {
                g_parked[k].pressed = NetplayLinkTick() - 1;
                std::fprintf(stderr, "coop-eventslots: test: player %d's Cross put on the tie tick %lld (player %d's read went live on it)\n", k + 1,
                             (long long) NetplayLinkTick() - 1, live);
            }
        }
    }
    live_was = live;

    for (int k = 0; k < coop::kMaxPlayers; k++) {
        if (g_parked[k].on) {
            Step(k);
            continue;
        }
        if (g_parked[k].queued > 0) {
            const int event = g_parked[k].queued;

            g_parked[k].queued = 0;
            Start(k, event);
            continue;
        }
        // A Cross on a tick with nothing live, when the other's Cross took the board that tick (their walk asked first, x-town-sign
        // bothpress): theirs starts at this step, as it would have beside a running one. DC_COOP_MUTATE=tie_press_lost (twin): lost.
        const bool pressed = CrossPressed(k);
        const bool tie = !pressed && g_parked[k].pressed == NetplayLinkTick() - 1 && !Mutated("tie_press_lost");

        if (pressed && live == 0) {
            g_parked[k].pressed = NetplayLinkTick();
        }
        if (!(pressed || tie) || live == 0 || live == k + 1 || g_fishing[k].on || coop::PlayerInMenu(k + 1) || CoopDayTransHoldsPlayer(k + 1)) { // (coop-daytrans: nor while they watch the time of day's scene)
            continue;
        }
        if (const int event = CoopTownPlayerEventAt(k); event > 0) {
            Start(k, event, tie && !pressed);
        }
    }
    ClipsOver(true); // coop-dayhouses: a clip point whose scenes are over goes
}

// The town's draw (every copy): each parked event's sprites, windows and fade from its slot, and each fisher's catch window,
// shown on its player's copy alone. DC_COOP_MUTATE=parked_shown (twin): on every copy.
void CoopEventSlotsDraw() {
    CoopDayTransDraw(); // coop-daytrans: the time of day's scene from its world slot
    for (int k = 0; k < coop::kMaxPlayers; k++) {
        if (g_fishing[k].on && Together()) {
            const bool hidden = HideBegin(k);

            FishSwap(k, true);
            TexManager.ReloadTexture(Vif1Packet, EditMes1.tex_block);
            EditMes1.DrawMesWin();
            FishSwap(k, true);
            HideEnd(hidden);
        }
        if (!g_parked[k].on) {
            continue;
        }
        const bool hidden = HideBegin(k);

        (hidden ? g_parked[k].frames_hidden : g_parked[k].frames_shown)++;
        {
            SlotIn in(k, false);

            EdEventSpriteDraw();
            TexManager.ReloadTexture(Vif1Packet, EditMes1.tex_block);
            EditMes1.DrawMesWin();
            EditEventMes1.DrawMesWin();
            EdFadeInOut();
        }
        HideEnd(hidden);
    }
}

// This copy's player's parked event: the frame drawn from its camera. DC_COOP_MUTATE=parked_view_walk (twin): from the walk's.
bool CoopEventSlotsView(float (*view)[4], float *eye) {
    const int k = NetplayLocalPlayer() - 1;

    if (k < 0 || k >= coop::kMaxPlayers || !g_parked[k].on || Mutated("parked_view_walk")) {
        return false;
    }
    SlotCamera(k)->GetCameraMatrix(view);
    SlotCamera(k)->GetPos(eye);
    // coop-owed1112 (x-eventslots-both reads it; twin parked_view_walk): once a parked event, its first frame from its camera.
    if (static int said_event = -1; said_event != g_parked[k].event) {
        said_event = g_parked[k].event;
        std::fprintf(stderr, "coop-eventslots: player %d's copy draws their parked event %d from its own camera (link tick %lld)\n", k + 1, g_parked[k].event,
                     (long long) NetplayLinkTick());
    }
    return true;
}

// coop-signcircle (Jep's v0.2 rc4 playtest: "you should be able to close the fishing sign menu with Circle"): the sign's choice window (the
// town's map event 11: Fish, Exchange, Records, Cancel) is the event script's own loop (_SET_MES_CURSOR with the row, _GET_APAD, _GET_PADDOWN each
// tick) and tests Cross, Up and Down only; Circle did nothing there (retail's script is the same: its exchange and record pages take Circle,
// the choice never did). A Circle pressed while the window is up now does what the player means by it: the script is fed Down presses to its
// last row (Cancel) and then a Cross, so it closes its window and ends its event as its own Cancel does (the same Circle is taken out of the
// pad, so nothing else answers it). The pad is the running event's own (the game's, or the player's the event carries), so it is every
// player's alike, alone or linked, and each player's progress is a slot of its own.
// DC_COOP_MUTATE=sign_circle_off (twin): Circle ignored, as before.
namespace {
constexpr int kSignEvent = 11;
constexpr int kSignCancelRow = 3;
constexpr int kExtPadDownId = 1;     // _GET_PADDOWN
constexpr int kExtMesCursorId = 199; // _SET_MES_CURSOR (window, row)

struct SignCircle {
    long long seen = -1000; // the link tick of the choice's last cursor call
    int       row = 0;
    bool      closing = false;
};
SignCircle g_sign_circle[coop::kMaxPlayers];

void SignCircleExt(int index, const RS_STACKDATA *args, int count) {
    if (EdEventInfo.event_no != kSignEvent || MapNo != 0 || !CoopTownPlayerEventIsMap(kSignEvent)) {
        return;
    }
    const int player = CoopTownEventPadPlayer();

    if (player < 1 || player > coop::kMaxPlayers) {
        return;
    }
    SignCircle     &mine = g_sign_circle[player - 1];
    const long long now = NetplayLinkTick();

    if (index == kExtMesCursorId && count >= 2) {
        mine.seen = now;
        mine.row = args[1].i;
        return;
    }
    if (index != kExtPadDownId || now - mine.seen > 1 || Mutated("sign_circle_off")) {
        if (index == kExtPadDownId) {
            mine.closing = false;
        }
        return;
    }
    PAD_DATA *data = reinterpret_cast<PAD_DATA *>(&GamePad); // pad[0], pad[1], previous_pad[0], previous_pad[1]
    int      &down = data[0].input.status.button;
    int      &was = data[2].input.status.button;

    if ((down & ~was & PAD_CIRCLE) != 0 && !mine.closing) {
        mine.closing = true;
        std::fprintf(stderr, "coop-signcircle: player %d's Circle closes the fishing sign's menu from row %d at link tick %lld\n", player, mine.row, (long long) now);
    }
    if (!mine.closing) {
        return;
    }
    down &= ~PAD_CIRCLE;
    if (mine.row != kSignCancelRow) {
        down |= PAD_DOWN;
        was &= ~PAD_DOWN;
    } else {
        down |= PAD_CROSS;
        was &= ~PAD_CROSS;
        mine.closing = false;
    }
}
} // namespace

// Test hook (DC_COOP_TEST_EXT_TRACE=1): every ext call of the town's event script, with its id and first arguments (an int as
// itself, a float as f<value>, a string quoted), on stderr: which retail state a running event touches.
void CoopScriptExtTrace(const void *script, int index, const RS_STACKDATA *args, int count) {
    static const bool on = TestEnvOn("DC_COOP_TEST_EXT_TRACE");

    if (script == &EdEventScript) {
        SignCircleExt(index, args, count);
    }
    if (!on || script != &EdEventScript) {
        return;
    }
    char text[256];
    int  at = 0;

    for (int i = 0; i < count && i < 5 && at < (int) sizeof text - 48; i++) {
        const RS_STACKDATA &arg = args[i];

        if (arg.type == 2 && arg.s != nullptr) { // (RS_STR)
            at += std::snprintf(text + at, sizeof text - at, " '%.24s'", arg.s);
        } else if (arg.type == 1) { // (RS_FLOAT)
            at += std::snprintf(text + at, sizeof text - at, " f%.2f", arg.f);
        } else {
            at += std::snprintf(text + at, sizeof text - at, " %d", arg.i);
        }
    }
    text[at] = '\0';
    std::fprintf(stderr, "coop-eventslots: test: ext %d (%d args)%s at link tick %lld\n", index, count, text, (long long) NetplayLinkTick());
}

// A session reset (a resync: the host's state taken, the zone loaded again): as every talk and her room event (TownResetForSession),
// every parked event and every player's fishing end; the zone's load makes the bodies, windows and pictures they pointed at anew.
// DC_COOP_MUTATE=slots_kept_over_reset (twin): kept, pointing into the old load.
void CoopEventSlotsSessionReset() {
    if (Mutated("slots_kept_over_reset")) {
        return;
    }
    bool any = g_live_own != 0;

    for (int k = 0; k < coop::kMaxPlayers; k++) {
        any = any || g_parked[k].on || g_fishing[k].on;
        g_parked[k].on = false;
        g_parked[k].queued = 0;
        g_fishing[k].on = false;
        g_fishing[k].set_made = false;
        g_fishing[k].window_made = false;
        g_buffers[k].valid = false;
    }
    g_live_own = 0;
    g_live_in = 0;
    g_event_player = 0;
    g_stepping = 0;
    for (int j = 0; j < kMostPonds; j++) { // coop-pondfish: the zone loads again, its texture blocks with it
        if (g_ponds[j].on) {
            std::fprintf(stderr, "coop-pondfish: the session reset ends the pond %d at link tick %lld\n", j, (long long) NetplayLinkTick());
            g_ponds[j] = Pond{};
        }
    }
    if (g_world.on) { // coop-daytrans: the zone loads again (its villagers with it)
        std::fprintf(stderr, "coop-daytrans: the session reset ends the time of day's event in its world slot at link tick %lld\n", (long long) NetplayLinkTick());
    }
    g_world.on = false;
    for (int i = 0; i < 10; i++) {
        g_world.late[i] = false;
    }
    if (any) {
        std::fprintf(stderr, "coop-eventslots: the session reset ends every parked event and every player's fishing (the zone loads again) at link tick %lld\n",
                     (long long) NetplayLinkTick());
        std::fflush(stderr);
    }
}

// A state set put back (a merge's or a file's): each parked event's main character is its player's body here (another copy's heap
// address otherwise: the second walker's body is this copy's own allocation).
void CoopEventSlotsApplied() {
    if (!Ranges()) {
        return;
    }
    if (g_world.on) { // coop-daytrans: the world slot's main character (the game's player, as retail's 0x84 has it) is this copy's
        for (int i = 0; i < g_range_count; i++) {
            if (g_ranges[i].at == static_cast<void *>(&EdEventInfo)) {
                reinterpret_cast<ED_EVENT_INFO *>(g_world_bytes + g_ranges[i].offset)->main_character = CoopTownPlayerBody(0, nullptr, nullptr);
            }
        }
    }
    for (int k = 0; k < coop::kMaxPlayers; k++) {
        if (!g_parked[k].on) {
            continue;
        }
        CTexAnimeData *anime = nullptr;
        int            anime_count = 0;
        CCharacter    *body = CoopTownPlayerBody(k, &anime, &anime_count);

        for (int i = 0; i < g_range_count; i++) {
            if (g_ranges[i].at != static_cast<void *>(&EdEventInfo)) {
                continue;
            }
            auto *info = reinterpret_cast<ED_EVENT_INFO *>(g_slot_bytes[k] + g_ranges[i].offset);

            info->main_character = body;
            if (anime != nullptr) {
                info->main_texture_animation = anime;
                info->main_texture_animation_count = anime_count;
            }
        }
        std::fprintf(stderr, "coop-eventslots: the state set brings player %d's parked event %d: its main character their body here\n", k + 1, g_parked[k].event);
    }
}

// coop-eventslots (11-11, from pt1111d: the town's system messages were one global slot, so two players' box and gate messages
// overwrote each other, and any player's message stood the game's walker, whose key lock reads the one message): each player's own
// system message and help bar in a slot of their own (sysmes.cpp SysMesStateRanges, edit.cpp EdSysMesStateRanges, the owner's
// tag). The game's player's are the live ones; another player's are exchanged in around what makes them (SetSystemMes for them,
// their own event's or fishing's steps), their step and their draw (shown on their copy alone). Each player's key lock reads their
// own message. Linked and together only; apart or alone, retail's one message. DC_COOP_MUTATE=sysmes_one_slot (twin): one for all.
namespace {
constexpr std::size_t kSysBytes = 128 * 1024;
Range                 g_sys_ranges[32] = {};
int                   g_sys_range_count = 0;
std::size_t           g_sys_range_bytes = 0;
alignas(16) unsigned char g_sys_slot[coop::kMaxPlayers][kSysBytes];
bool g_sys_made[coop::kMaxPlayers] = {};
int  g_sys_live = 1;        // whose slot is live now (1 up; the game's player's at rest)
bool g_sys_others = false;  // stepping or drawing another player's slot (the hooks in EdSystemMesStep/Draw then do nothing)
int  g_sys_hidden_frames[coop::kMaxPlayers] = {};
int  g_sys_shown_frames[coop::kMaxPlayers] = {};

void AddSysRange(void *at, std::size_t bytes, const char *name, void *) {
    if (g_sys_range_count >= 32 || g_sys_range_bytes + bytes > kSysBytes) {
        std::fprintf(stderr, "coop-eventslots: the message set does not fit (%d ranges, %zu + %zu bytes, at %s)\n", g_sys_range_count, g_sys_range_bytes, bytes, name);
        std::fflush(stderr);
        std::abort();
    }
    g_sys_ranges[g_sys_range_count++] = {at, bytes, name, g_sys_range_bytes};
    g_sys_range_bytes += (bytes + 15) & ~static_cast<std::size_t>(15);
}

void SysRanges() {
    if (g_sys_range_count > 0) {
        return;
    }
    SysMesStateRanges(AddSysRange, nullptr);
    EdSysMesStateRanges(AddSysRange, nullptr);
    coop::MessageTagRanges(AddSysRange, nullptr);
    std::fprintf(stderr, "coop-eventslots: the message set: %d ranges, %zu bytes a player\n", g_sys_range_count, g_sys_range_bytes);
}

// Player p's (2 up) slot exchanged with the live one; a slot never used starts as the live one cleared (no message up).
void SysSwap(int p) {
    SysRanges();
    unsigned char *slot = g_sys_slot[p - 1];

    if (!g_sys_made[p - 1]) {
        for (int i = 0; i < g_sys_range_count; i++) {
            std::memcpy(slot + g_sys_ranges[i].offset, g_sys_ranges[i].at, g_sys_ranges[i].bytes);
        }
        g_sys_made[p - 1] = true;
        for (int i = 0; i < g_sys_range_count; i++) {
            SwapBytes(g_sys_ranges[i].at, slot + g_sys_ranges[i].offset, g_sys_ranges[i].bytes);
        }
        EdClearSystemMes();
        coop::MessageTagClear();
        for (int i = 0; i < g_sys_range_count; i++) {
            SwapBytes(g_sys_ranges[i].at, slot + g_sys_ranges[i].offset, g_sys_ranges[i].bytes);
        }
    }
    for (int i = 0; i < g_sys_range_count; i++) {
        SwapBytes(g_sys_ranges[i].at, slot + g_sys_ranges[i].offset, g_sys_ranges[i].bytes);
    }
}

// Player's (1 up) slot live; returns a token for SysOut (0: nothing changed).
int SysIn(int player) {
    // (the town and its rooms alone: a floor steps and draws the one message, its own rule for whose it is: coop_message)
    if (player <= 1 || player > coop::kMaxPlayers || player == g_sys_live || mode != 2 || !Together() || Mutated("sysmes_one_slot")) {
        return 0;
    }
    const int was = g_sys_live;

    if (was != 1) {
        SysSwap(was);
    }
    SysSwap(player);
    g_sys_live = player;
    return was;
}

void SysOut(int token) {
    if (token == 0) {
        return;
    }
    SysSwap(g_sys_live);
    g_sys_live = 1;
    if (token != 1) {
        SysSwap(token);
        g_sys_live = token;
    }
}

// Test hook (DC_COOP_TEST_SYSMES=<tick>): on that link tick a got-item message that waits for its owner's Cross for each of the
// first two players, on every copy; then each change of a player's message (up, gone) and of the game's walker's key lock, with
// the frames this copy drew each player's own message out of sight and shown, and where the game's player stands.
void SysMesForTest() {
    static const char *spec = TestEnv("DC_COOP_TEST_SYSMES");
    static int         up_said[coop::kMaxPlayers] = {-1, -1, -1, -1, -1, -1};
    static int         lock_said = -1;

    if (spec == nullptr || !Together() || EdInteriorFlag != 0) {
        return;
    }
    if (NetplayLinkTick() == std::atoll(spec)) {
        for (int p = 1; p <= 2; p++) {
            coop::MessageBy by(p);
            ItemGetMes(ITEM_PRICKLY, -1, 2, 1);
        }
        std::fprintf(stderr, "coop-eventslots: test: a got-item message waiting for a Cross set for players 1 and 2 at link tick %lld\n", (long long) NetplayLinkTick());
    }
    sceVu0FVECTOR toan = {};
    sceVu0FVECTOR her = {};
    CCharacter   *partner_body = CoopTownPlayerBody(1, nullptr, nullptr);

    if (Chara != nullptr) {
        Chara->GetPosition(toan);
    }
    if (partner_body != nullptr) {
        partner_body->GetPosition(her);
    }
    for (int p = 1; p <= 2; p++) {
        const int token = SysIn(p);
        const int up = SystemMesCheck() != 0 ? 1 : 0;

        SysOut(token);
        if (up != up_said[p - 1]) {
            up_said[p - 1] = up;
            std::fprintf(stderr, "coop-eventslots: test: player %d's system message %s at link tick %lld (out of sight %d frames, shown %d here; Toan at %.1f %.1f, player 2 at %.1f %.1f)\n", p,
                         up != 0 ? "up" : "gone", (long long) NetplayLinkTick(), g_sys_hidden_frames[p - 1], g_sys_shown_frames[p - 1], toan[0], toan[2], her[0], her[2]);
        }
    }
    const int lock = EdSystemMesCheck() != 0 ? 1 : 0;

    if (lock != lock_said) {
        lock_said = lock;
        std::fprintf(stderr, "coop-eventslots: test: the game's walker %s by a message at link tick %lld\n", lock != 0 ? "held" : "free", (long long) NetplayLinkTick());
    }
}
} // namespace

// coop-fishhud (the developer 11-18: "When P1 has the fishing rod out, P2 can see the control menu on their screen"): the fisher's help bar
// ("Cross: cast", the bait and reel lines: EdFishingWalkHelpMes through EdSetHelpMes) lives in a message slot. A player other than the
// game's has theirs in their own slot, drawn from there on their copy alone (CoopSysMesOthersDraw); the game's player's is the live
// slot, which the town drew on every copy (the live draw gated only for the Georama's bar, CoopTownHidesBuildHelp). A help bar sets
// SystemMesNo back to -1, so the message tag (coop_message's owner) never reaches it either. The fix is at the draw: while the game's
// player fishes together, the live slot's help bar is theirs and is left out on every other player's copy. Draw only: the slots and
// steps are as they were. DC_COOP_MUTATE=fishhud_shown (twin): drawn on every copy, as before.
namespace {
bool LiveHelpUp() {
    for (int i = 0; i < g_sys_range_count; i++) {
        if (std::strcmp(g_sys_ranges[i].name, "HelpMesNo") == 0) {
            return *static_cast<const int *>(g_sys_ranges[i].at) > 0;
        }
    }
    return false;
}

// Test hook (DC_COOP_TEST_FISH_TRACE): each fisher's help bar, as it goes down: the frames it was up, drawn on this copy and left out.
struct HelpFrames {
    int  shown = 0;
    int  hidden = 0;
    bool was = false;
} g_help_frames[coop::kMaxPlayers];

void HelpFrame(int player, bool up, bool shown) {
    static const bool on = TestEnvOn("DC_COOP_TEST_FISH_TRACE");
    HelpFrames       &h = g_help_frames[player - 1];

    if (up) {
        (shown ? h.shown : h.hidden)++;
        h.was = true;
    } else if (h.was) {
        h.was = false;
        if (on) {
            std::fprintf(stderr, "coop-fishhud: test: player %d's fishing help bar went down: %d frames drawn on player %d's copy, %d left out, at link tick %lld\n", player,
                         h.shown, NetplayLocalPlayer(), h.hidden, (long long) NetplayLinkTick());
        }
        h = HelpFrames{};
    }
}
} // namespace

// The town's draw, every frame: whether the live slot's help bar is left out on this copy (coop_town's CoopTownHidesBuildHelp).
bool CoopFishingHidesLiveHelp() {
    const int  local = NetplayLocalPlayer();
    const bool fishes = Together() && g_fishing[0].on && local != 0;

    if (!fishes || CoopP2Building()) {
        HelpFrame(1, false, false);
        return false;
    }
    const bool hide = local != 1 && !Mutated("fishhud_shown");

    HelpFrame(1, LiveHelpUp(), !hide);
    return hide;
}

int CoopSysMesSetIn() {
    return g_sys_others ? 0 : SysIn(coop::MessageOwnerNext());
}

void CoopSysMesSetOut(int token) {
    SysOut(token);
}

// EdSystemMesStep (every copy, each tick, before the live one): every other player's own messages stepped from their slot.
void CoopSysMesOthersStep() {
    if (g_sys_others || !Together()) {
        return;
    }
    SysMesForTest();
    for (int p = 2; p <= coop::kMaxPlayers; p++) {
        if (!g_sys_made[p - 1]) {
            continue;
        }
        const int token = SysIn(p);

        if (token == 0) {
            continue;
        }
        g_sys_others = true;
        EdSystemMesStep();
        g_sys_others = false;
        SysOut(token);
    }
}

// EdSystemMesDraw (every copy): every other player's own messages drawn from their slot, shown on their copy alone (out of sight
// elsewhere: they step as they draw); the live one's own rule (coop_message: whose it is) decides for the game's player's.
void CoopSysMesOthersDraw() {
    if (g_sys_others || !Together()) {
        return;
    }
    for (int p = 2; p <= coop::kMaxPlayers; p++) {
        if (!g_sys_made[p - 1]) {
            continue;
        }
        const int token = SysIn(p);

        if (token == 0) {
            continue;
        }
        const bool up = SystemMesCheck() != 0;
        const bool hidden = HideBegin(p - 1);

        HelpFrame(p, LiveHelpUp() && g_fishing[p - 1].on, !hidden);
        if (up) {
            (hidden ? g_sys_hidden_frames[p - 1] : g_sys_shown_frames[p - 1])++;
        }
        g_sys_others = true;
        EdSystemMesDraw();
        g_sys_others = false;
        HideEnd(hidden);
        SysOut(token);
    }
}

// Player's (1 up) own message waits for their Cross (their walk stands, as retail's walker by his own).
bool CoopSysMesHolds(int player) {
    if (player <= 1) {
        return EdSystemMesCheck() != 0;
    }
    const int  token = SysIn(player);
    const bool held = token != 0 && SystemMesCheck() != 0;

    SysOut(token);
    return held;
}

// Her own room or town event's step (coop_town.cpp CoopTownHerRoomEventStepIn/Out): her messages her own slot's.
int CoopSysMesPlayerIn(int player) {
    return SysIn(player);
}

void CoopSysMesPlayerOut(int token) {
    SysOut(token);
}

// The self-test's other half (DC_COOP_TEST_EVENTSLOT_TRIP, idle): each player's (all six) fishing set and window, and each other player's
// message slot, swapped in and out at once with nothing run: the live sets as they were.
namespace {
std::uint64_t RangesHash(const Range *ranges, int count) {
    std::uint64_t h = 0xcbf29ce484222325ull;

    for (int i = 0; i < count; i++) {
        h = Hash(static_cast<const unsigned char *>(ranges[i].at), ranges[i].bytes, h);
    }
    return h;
}

void FishSetsTripForTest() {
    FishRanges();
    SysRanges();
    for (int k = 0; k < coop::kMaxPlayers; k++) {
        const std::uint64_t before = RangesHash(g_fish_ranges, g_fish_range_count) ^ Hash(reinterpret_cast<const unsigned char *>(&EditMes1), sizeof EditMes1, 7);

        FishSwap(k, true);
        FishSwap(k, true);
        const std::uint64_t after = RangesHash(g_fish_ranges, g_fish_range_count) ^ Hash(reinterpret_cast<const unsigned char *>(&EditMes1), sizeof EditMes1, 7);

        std::fprintf(stderr, "coop-eventslots: test: fishing set trip, player %d: %s\n", k + 1, before == after ? "as they were" : "CHANGED");
    }
    for (int p = 2; p <= coop::kMaxPlayers; p++) {
        const std::uint64_t before = RangesHash(g_sys_ranges, g_sys_range_count);
        const int           token = SysIn(p);

        SysOut(token);
        const std::uint64_t after = RangesHash(g_sys_ranges, g_sys_range_count);

        std::fprintf(stderr, "coop-eventslots: test: message set trip, player %d: %s (its own %s)\n", p, before == after ? "as they were" : "CHANGED",
                     token != 0 ? "swapped in" : "NOT SWAPPED");
    }
}
} // namespace

// coop-fishing3 (Jep 11-18: "entering fishing mode should not move the player"): choosing Fish at a board enters fishing mode where the
// player stands, facing where they face; they walk with the rod (retail's own walk with it, held in by the pond's fishing polygons)
// and cast from wherever they choose. Retail's boards (Norune's program 11, e02's 13 and 14, e04's 12) put their fisher on one
// spot under the fade (_SET_NPC_POS / _SET_NPC_ROT on index -1, the main character: Norune (40, 0, 96) facing 3.14 in the pond
// part's frame, 4.8 behind the board's face) before _GOTO_FISHING. Each event's start keeps where its main character stands
// and faces (CoopEventStartsAt, from EdEventInit: every event, live or in a player's parked slot, alone too); _GOTO_FISHING puts
// that character back there (a bait's event, 134, moves nobody: the same place). The cast, the float's water test (the pond's
// rect, FishingCheckUkiHook), the fish (the pond's, going for bait near the hook), the line and the camera follow the fisher
// wherever they stand. Two fishers are two bodies that walk apart themselves; each one's line, float and fish are their own
// (their fishing sets). Replaces coop-fishspot's own spot per fisher. DC_COOP_MUTATE=fishing_entry_moved (twin): retail's move and turn.
// The fishing area takes in where they stand (below); DC_COOP_MUTATE=fishing_area_as_retail (twin): retail's area as it is.
namespace {
struct EntryAt {
    CCharacter   *body = nullptr;
    sceVu0FVECTOR at, turn;
};
EntryAt g_entry_at[coop::kMaxPlayers]; // (one per body: a slot's own main character, the game's player's)
// How far from the fishing area's walls a fisher entering it is kept: the walk holds a body 5.0 from a wall (4.98 measured, Norune).
constexpr float kEntryClear = 6.0f;
} // namespace

void CoopEventStartsAt(CCharacter *main) {
    if (main == nullptr) {
        return;
    }
    EntryAt *slot = nullptr;

    for (EntryAt &e : g_entry_at) {
        slot = slot == nullptr && (e.body == main || e.body == nullptr) ? &e : slot;
    }
    for (EntryAt &e : g_entry_at) {
        slot = e.body == main ? &e : slot;
    }
    if (slot == nullptr) {
        slot = &g_entry_at[0];
    }
    slot->body = main;
    main->GetPosition(slot->at);
    main->GetRotation(slot->turn);
}

void CoopFishingSpot(CCharacter *body) {
    // (whose: the event's player together; apart or alone, this copy's player)
    const int k = std::clamp(Together() ? g_event_player - 1 : NetplayRole() - 1, 0, coop::kMaxPlayers - 1);

    for (EntryAt &e : g_entry_at) {
        if (e.body != body || body == nullptr) {
            continue;
        }
        sceVu0FVECTOR put, turned;
        float         fade[4];
        CBoxVu0      *area = nullptr; // (the board's fishing area, _LOAD_FISHING_DATA's: its box's faces are the fishing walk's walls)
        float         grown = 0.0f;

        body->GetPosition(put);
        body->GetRotation(turned);
        FishRanges();
        for (int i = 0; i < g_fish_range_count; i++) {
            area = std::strcmp(g_fish_ranges[i].name, "fishing_rect") == 0 ? static_cast<CBoxVu0 *>(g_fish_ranges[i].at) : area;
        }
        if (!Mutated("fishing_entry_moved")) {
            body->SetPosition(e.at);
            body->SetRotation(e.turn[0], e.turn[1], e.turn[2]);
            // The area's box (its faces are walls both ways: FishingPickUpPoly) takes in where they stand, a wall's clearance round
            // them, on this fisher's own fishing set (live now: their event's _LOAD_FISHING_DATA set it). Retail's spot is inside it;
            // a reader at Norune's board stands just outside it (z -98 against its -100.1): its face pushed them out on their first
            // step, a hop the others saw, and held them outside it, too far for a cast to reach the water (it landed on the bank).
            // The float's own test (FishingCheckUkiHook) also needs the water's height, so the bank strip taken in reels in as before.
            for (int i = 0; area != nullptr && !Mutated("fishing_area_as_retail") && i < 3; i += 2) {
                const float low = std::fmin(area->min[i], e.at[i] - kEntryClear), high = std::fmax(area->max[i], e.at[i] + kEntryClear);

                grown = std::fmax(grown, std::fmax(area->min[i] - low, high - area->max[i]));
                area->min[i] = low;
                area->max[i] = high;
            }
        }
        sceVu0FVECTOR now, facing; // (where the body is as fishing begins: the rows hold it to where they stood)

        body->GetPosition(now);
        body->GetRotation(facing);
        EdGetFadeColor(fade);
        std::fprintf(stderr, "coop-fishspot: player %d enters fishing at (%.1f %.1f %.1f) facing %.3f, where they stood (%.1f %.1f %.1f) facing %.3f (the board's "
                     "script put them %.1f away, facing %.3f), the fishing area x %.1f..%.1f z %.1f..%.1f (grown %.1f to take them in), the screen's fade %.0f "
                     "of 128, at link tick %lld\n",
                     k + 1, now[0], now[1], now[2], facing[1], e.at[0], e.at[1], e.at[2], e.turn[1], std::hypot(put[0] - e.at[0], put[2] - e.at[2]), turned[1],
                     area != nullptr ? area->min[0] : 0.0f,
                     area != nullptr ? area->max[0] : 0.0f, area != nullptr ? area->min[2] : 0.0f, area != nullptr ? area->max[2] : 0.0f, grown, fade[3],
                     (long long) NetplayLinkTick());
        std::fflush(stderr);
        return;
    }
}
