// coop-eventslots (Jep's 11-11 playtest item 1: both players use Norune's fishing board at once, each with their own event,
// camera, menus and fishing). Design: debug\scratch\eventslots\design.md.

#include "coop/game_ns.hpp"
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <optional>
#include <utility>

#include "camerafollow.hpp"
#include "character.hpp"
#include "clsmes.hpp"
#include "dataalloc.hpp"
#include "dataread.hpp"
#include "ebattle.hpp"
#include "edit.hpp"
#include "editloop.hpp"
#include "editloop3.hpp"
#include "editmapscript.hpp" // GameMode, ED_MODE_*
#include "fish.hpp"
#include "fishing.hpp"
#include "gamepad.hpp"
#include "gfx/gfx.hpp"
#include "mglib.hpp"
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
#include "coop/coop_inventory.hpp"
#include "coop/coop_message.hpp"
#include "coop/coop_town.hpp"
#include "coop/coop_worlds.hpp"
#include "coop/netplay.hpp"

using namespace coop;

namespace dc::game {
extern CRunScript EdEventScript; // editloop3.cpp
extern int        EdInteriorFlag;
extern s32        mode; // the game's mode (2: the town, its rooms)
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
void EdSysMesStateRanges(void (*add)(void *at, std::size_t bytes, const char *name, void *context), void *context); // edit.cpp (coop-eventslots)
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

void FishTrace(int k) {
    static const bool on = TestEnvOn("DC_COOP_TEST_FISH_TRACE");

    if (!on || g_fish_state_said[k] == chara_fishing) {
        return;
    }
    g_fish_state_said[k] = chara_fishing;
    std::fprintf(stderr, "coop-eventslots: test: player %d's fishing state %d at link tick %lld (fish %d frames out of sight, %d shown on this copy)\n", k + 1,
                 chara_fishing, (long long) NetplayLinkTick(), g_fishing[k].frames_hidden, g_fishing[k].frames_shown);
    if (chara_fishing == ED_FISHING_RESULT && AngleFish != nullptr) {
        const auto *model = reinterpret_cast<const unsigned char *>(AngleFish->angle_model.frame);
        const bool  own = model != nullptr && model >= g_parked_arena[k] && model < g_parked_arena[k] + kArenaBytes;

        std::fprintf(stderr, "coop-eventslots: test: player %d's catch's model %s their own arena at link tick %lld\n", k + 1, own ? "in" : "OUTSIDE",
                     (long long) NetplayLinkTick());
    }
    if (chara_fishing == ED_FISHING_FINISH && SaveData != nullptr) {
        const SV_FISH_DATA *best = SaveData->GetFishingRank(0);

        std::fprintf(stderr, "coop-eventslots: test: player %d's catch: the fishing points now %d, the log's first fish %d size %.2f at link tick %lld\n", k + 1,
                     (int) SaveData->GetFishingPoint(), best != nullptr ? (int) best->fish_id : -1, best != nullptr ? (double) best->rank : -1.0,
                     (long long) NetplayLinkTick());
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
        if (result > 0) {
            BuffersTake(g_buffers[k]); // (the slot's: its fishing's, if it fishes now)
        }
    }
    slot.steps++;
    if (result <= 0) {
        return;
    }
    if (result == ED_EVENT_RETURN_FISHING) {
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

bool CoopFishingHere() {
    return CoopFishingOn(NetplayLocalPlayer() - 1);
}

// The town's texture block for one an event or its fishing names, while player p's own event or fishing runs together: p's own
// (eight a player from 80: the fish, the fish caught, the bait, the event's 45 to 49 in a parked slot; the fishing body's: p's
// own body's). Retail's own number otherwise.
int CoopEventTexBlock(int block) {
    const int p = g_event_player;

    if (p <= 0 || !Together()) {
        return block;
    }
    const int base = 80 + 8 * (p - 1);

    switch (block) {
        case 8:
            return CoopTownPlayerTextureBlock(p - 1);
        case 54:
            return base;
        case 55:
            return base + 1;
        case 40:
            return base + 2;
        case 45:
        case 46:
        case 47:
        case 48:
        case 49:
            return g_stepping == p ? base + 3 + (block - 45) : block;
        default:
            return block;
    }
}

// An own event together never displaced the town's villagers (its own arena and blocks): it keeps them, loads none again.
bool CoopEventKeepsVillagers() {
    if (g_event_player <= 0 || !Together() || EdInteriorFlag != 0) {
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
    if (g_event_player <= 0 || !Together() || EdEventInfo.main_character == nullptr || EdEventInfo.main_character == Chara) {
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
        ::GameMode = ED_MODE_WALK;
    }
    g_walk_fishing = g_fishing[0].on && Together();
    if (!g_walk_fishing) {
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

// The town's draw: each fisher's fish (their own screen alone: drawn out of sight elsewhere, as they step as they draw), line
// and float (every screen). pass 0 under the water's surface, 1 over it, as retail's two draws. DC_COOP_MUTATE=fish_shown (twin):
// the fish on every screen.
void CoopFishingDraw(int pass) {
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
        if (pass == 0) {
            const bool hidden = NetplayLocalPlayer() != k + 1 && !Mutated("fish_shown") && HideBegin(k);

            FishingDrawFish();
            HideEnd(hidden);
            (hidden ? g_fishing[k].frames_hidden : g_fishing[k].frames_shown)++;
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
        if (!(pressed || tie) || live == 0 || live == k + 1 || g_fishing[k].on || coop::PlayerInMenu(k + 1)) {
            continue;
        }
        if (const int event = CoopTownPlayerEventAt(k); event > 0) {
            Start(k, event, tie && !pressed);
        }
    }
}

// The town's draw (every copy): each parked event's sprites, windows and fade from its slot, and each fisher's catch window,
// shown on its player's copy alone. DC_COOP_MUTATE=parked_shown (twin): on every copy.
void CoopEventSlotsDraw() {
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

// This copy's player's parked event: the frame drawn from its camera.
bool CoopEventSlotsView(float (*view)[4], float *eye) {
    const int k = NetplayLocalPlayer() - 1;

    if (k < 0 || k >= coop::kMaxPlayers || !g_parked[k].on) {
        return false;
    }
    SlotCamera(k)->GetCameraMatrix(view);
    SlotCamera(k)->GetPos(eye);
    return true;
}

// Test hook (DC_COOP_TEST_EXT_TRACE=1): every ext call of the town's event script, with its id and first arguments (an int as
// itself, a float as f<value>, a string quoted), on stderr: which retail state a running event touches.
void CoopScriptExtTrace(const void *script, int index, const RS_STACKDATA *args, int count) {
    static const bool on = TestEnvOn("DC_COOP_TEST_EXT_TRACE");

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
