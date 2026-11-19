// coop-wave4 (localaudit 3.9, Jep-approved Wave 4: per-player presentation out of the state set, a group a session): each
// group here is presentation a copy draws for its own screen alone, which the state set took from the sender at every join,
// merge and state file although nothing in the step reads it. build\snapshot-allow.txt takes each group out ("!ps2:" rules),
// so a copy keeps its own across an apply. Each group was chosen from the census (DC_QA_WRITE_GUARD's: globals that already
// differ between two copies in lockstep) and its readers (draws only), and is proved by two tests: a copy whose group is
// scrambled stays in sync (nothing the step reads depends on it), and a joiner's own group is the same after the apply as
// before (the host's is not carried). Test hooks only, but the twins (DC_SNAPSHOT_MUTATE=look_in_set, sound_in_set: the group back in the set).
//
// Group "message look" (the message windows' look): RandTbl and RandTbl2 (the speech bubbles' wobble, which ClsMes::MakeMesWin
// rolls from TableRand, a generator of its own while linked, and only MakeFukidashi_sub and BubbleTarget read, both draws),
// TableRand's state, MesWinClut (the windows' palette), and two draw functions' frame counters (DrawMenuWaku's MenuWakuCnt,
// CharaStatusMsgDraw's statusCnt). A window's own state (its text, its page, its wait), which scripts read, stays in the set.
//
// Group "special sound set" (coop-soundset, landed first into v0.2 rc3 as a bug fix): which special sound set this copy's own
// sound driver holds (snd.cpp now_sp_no) and the one it is loading (load_sp_no, load_sp_adr).
// A presentation loads its set on its own copy alone (the chest's, the Atla's: btitem.cpp,
// run on its opener's copy), so the copies already differ (the write guard's census). Taken from the sender, the taker's copy
// believed a set loaded that its driver never had: SndSPSeLoad then skipped the load, and its next presentation played another
// set's sounds. Read only by the sound calls (SndSPSeLoad, SndSPSeSyncBG), whose results nothing in the step uses. Twin
// DC_SNAPSHOT_MUTATE=sound_in_set (the group back in the set).

#include "coop/game_ns.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "app/gameloop.hpp" // GameFrameCount

#include "coop/coop.hpp" // TestEnvInt, TestEnvOn
#include "coop/coop_core.hpp"
#include "coop/netplay.hpp"

namespace dc::game {
extern float RandTbl[64];  // clsmes.cpp
extern float RandTbl2[64]; // clsmes.cpp
extern u32   MesWinClut[256]; // gameutil.cpp
void CoopSndSpecialRegions(void (*add)(void *at, std::size_t size, void *context), void *context); // snd.cpp (coop-soundset)
int  CoopSndSpecialNow();
int  SndSPSeLoad(int set_no); // snd.cpp
} // namespace dc::game

namespace coop {

namespace {

struct Part {
    void       *at;
    std::size_t size;
};

std::vector<Part> LookParts() {
    return {{dc::game::RandTbl, sizeof dc::game::RandTbl}, {dc::game::RandTbl2, sizeof dc::game::RandTbl2},
            {dc::game::MesWinClut, sizeof dc::game::MesWinClut}};
}

std::uint32_t Hash(const std::vector<Part> &parts) {
    std::uint32_t hash = 2166136261u;

    for (const Part &part : parts) {
        for (std::size_t i = 0; i < part.size; i++) {
            hash = (hash ^ static_cast<const unsigned char *>(part.at)[i]) * 16777619u;
        }
    }
    return hash;
}

std::uint32_t g_before = 0;

} // namespace

std::uint32_t OwnLookHash() {
    return Hash(LookParts());
}

// The twin's regions (snapshot_state.cpp, DC_SNAPSHOT_MUTATE=look_in_set): the group as at, size.
void OwnLookRegionsAll(void (*add)(void *at, std::size_t size, void *context), void *context) {
    for (const Part &part : LookParts()) {
        add(part.at, part.size, context);
    }
}

// Around a state set's apply (netplay.cpp StateSetApplyCore): the group's hash before and after, on this copy.
void OwnLookApplyBefore() {
    g_before = OwnLookHash();
}

void OwnLookApplyAfter() {
    const std::uint32_t after = OwnLookHash();

    std::fprintf(stderr, "coop-ownlook: the message look across the state set's apply on player %d's copy: %08x before, %08x after (%s)\n",
                 NetplayLocalPlayer(), g_before, after, g_before == after ? "this copy's own" : "taken from the set");
}

// Each frame (NetplayFrameStart). Test hooks: DC_COOP_TEST_LOOK_SCRAMBLE=T, at this copy's link tick T (alone or linked) its message look made other
// than any copy's (the wobble tables a ramp, the palette turned), said with its hash; DC_COOP_TEST_LOOK_TRACE=1, the hash every 60
// link ticks while linked.
void OwnLookFrame() {
    static const int  scramble_at = TestEnvInt("DC_COOP_TEST_LOOK_SCRAMBLE", -1);
    static const bool trace = TestEnvOn("DC_COOP_TEST_LOOK_TRACE");
    static bool       scrambled = false;

    if (scramble_at >= 0 && !scrambled && NetplayLinkTick() >= scramble_at) {
        scrambled = true;
        for (int i = 0; i < 64; i++) {
            dc::game::RandTbl[i] = 0.125f * (float) i;
            dc::game::RandTbl2[i] = 0.5f + 0.001f * (float) i;
        }
        for (int i = 0; i < 256; i++) {
            dc::game::MesWinClut[i] = (dc::game::MesWinClut[i] & 0xFF000000u) | ((dc::game::MesWinClut[i] + 0x00203040u) & 0x00FFFFFFu);
        }
        std::fprintf(stderr, "coop-ownlook: test: the message look scrambled on player %d's copy at frame %lld, link tick %lld: %08x\n", NetplayLocalPlayer(),
                     (long long) dc::app::GameFrameCount(), (long long) NetplayLinkTick(), OwnLookHash());
    }
    if (trace && NetplayLinkUp()) {
        static long long said = -1;
        const long long  tick = NetplayLinkTick();

        if (tick >= 0 && tick % 60 == 0 && tick != said) {
            said = tick;
            // (and the palette alone: the game never writes it again, so a scramble's change stays; the wobble re-rolls with each message)
            std::fprintf(stderr, "coop-ownlook: the message look on player %d's copy at link tick %lld: %08x, palette %08x\n", NetplayLocalPlayer(), tick,
                         OwnLookHash(), Hash({{dc::game::MesWinClut, sizeof dc::game::MesWinClut}}));
        }
    }
}

// coop-soundset: the special sound set.
namespace {
int g_sound_before = 0;
} // namespace

// The twin's regions (snapshot_state.cpp, DC_SNAPSHOT_MUTATE=sound_in_set).
void OwnSoundRegionsAll(void (*add)(void *at, std::size_t size, void *context), void *context) {
    dc::game::CoopSndSpecialRegions(add, context);
}

// Around a state set's apply (netplay.cpp StateSetApplyCore): the group before and after, on this copy.
void OwnSoundApplyBefore() {
    g_sound_before = dc::game::CoopSndSpecialNow();
}

void OwnSoundApplyAfter() {
    const int sound = dc::game::CoopSndSpecialNow();

    std::fprintf(stderr, "coop-ownlook: the special sound set across the state set's apply on player %d's copy: %d before, %d after (%s)\n",
                 NetplayLocalPlayer(), g_sound_before, sound, g_sound_before == sound ? "this copy's own" : "taken from the set");
}

// Each frame (NetplayFrameStart). Test hooks: DC_COOP_TEST_SOUND_LOAD=S, at link tick 60 this copy alone loads special sound set S, as
// a presentation of its own does; DC_COOP_TEST_SOUND_TRACE=1, each change of this copy's special sound set, with the link tick.
void OwnSoundFrame() {
    if (static const int sound_load = TestEnvInt("DC_COOP_TEST_SOUND_LOAD", -1); sound_load >= 0) {
        static bool loaded = false;

        if (!loaded && NetplayLinkTick() >= 60) {
            loaded = true;
            dc::game::SndSPSeLoad(sound_load);
            std::fprintf(stderr, "coop-ownlook: test: special sound set %d loaded on player %d's copy alone at link tick %lld\n", sound_load, NetplayLocalPlayer(),
                         (long long) NetplayLinkTick());
        }
    }
    if (static const bool sound_trace = TestEnvOn("DC_COOP_TEST_SOUND_TRACE"); sound_trace) {
        static int said = -2;

        if (const int now = dc::game::CoopSndSpecialNow(); now != said) {
            said = now;
            std::fprintf(stderr, "coop-ownlook: player %d's copy's special sound set is %d at link tick %lld\n", NetplayLocalPlayer(), now,
                         (long long) NetplayLinkTick());
        }
    }
}

} // namespace coop
