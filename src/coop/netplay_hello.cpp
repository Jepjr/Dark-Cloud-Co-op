#include "coop/game_ns.hpp"
#include "coop/netplay_hello.hpp"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <thread>
#include <vector>

#include "coop/coop.hpp"
#include "coop/coop_camera.hpp"
#include "coop/coop_inventory.hpp"
#include "coop/coop_lobby.hpp" // coop-joinname: OwnName
#include "coop/coop_menu.hpp" // coop-pt1111o
#include "coop/coop_prompt.hpp" // coop-revive
#include "coop/netplay.hpp"     // coop-lobby: NetplayPlayerId
#include "coop/coop_worlds.hpp" // coop-localfix2: the stayer
#include "coop/netplay.hpp" // coop-steamsplit: NetplayCatchupSetting
#include "coop/netplay_slots.hpp"
#include "coop/netplay_test.hpp"
#include "main.hpp"
#include "mainselect.hpp"
#include "savedata.hpp"
#include "platform/config.hpp"
#include "platform/paths.hpp"
#include "save/save_slots.hpp" // coop-indoorfps: DC_QA_SLOT_BULK

// coop-buildid: the version this exe was made from, written into the staged tree by build\BuildVersion.ps1. A tree made
// some other way (no build script) has none: such a copy proves its build by its exe's bytes alone.
#if __has_include("coop/build_version.hpp")
#include "coop/build_version.hpp"
#endif
#ifndef DCO_VERSION_CONTENT
#define DCO_VERSION_CONTENT ""
#define DCO_VERSION_CHRONICLE ""
#define DCO_VERSION_KAZAROV ""
#define DCO_VERSION_FLAVOR ""
#endif
#ifndef DCO_VERSION_RELEASE
#define DCO_VERSION_RELEASE "" // (version-scheme: build\version.txt; none in a tree made without build\BuildVersion.ps1)
#endif

namespace {

// version-scheme (Jep 11-14: one numbering): the protocol counter is RETIRED. From 0.1.1.5 the Hello carries the full version
// (build\version.txt, NetplayHello::release) and the join compares it; this field is frozen at 28, the Hello's layout from
// 0.1.1.5 on, never bumped again (a copy from before says 27 or less: OldVersionName names it). The history below is kept as history.
constexpr std::uint32_t kProtocol = 28; // 28 (0.1.1.5: retired, frozen; the version string says the version); 27 (v0.1.2-rc4, coop-rc4, one bump: coop-trade3's game.coop_swap_same_zone in the Hello, coop-b4arrive's Hello arrival_cut and cut_walk, and the state-set changes of coop-p1body, coop-towncrash, coop-p2boot, coop-georama2, coop-comboaudio, coop-p2actions and coop-chestrolls; coop-rehost and coop-padlight carry none); 26 (v0.1.2-rc3, coop-rc3, one bump: coop-readyscreen's ready session and item-scene split globals (g_session, g_item_scene_hold, g_item_scene_script, g_item_left) and coop-stealback's g_full_said in the state set; coop-farcull2 draw only); 25 (v0.1.2-rc2, coop-rc2, one bump for every branch merged in it: coop-doorcam's town Circle swing, one int more in TownSync; coop-swapduel's story owner (whoever plays Toan plays the scene); the state-set and message changes of coop-atlafly, coop-b9arrive/resyncmenu, coop-farcull, coop-pickup, coop-apartfps, coop-storywarp, coop-entrance8, coop-resyncblack, coop-whereishouse and coop-xiaogap); 24 (the v0.1.2 candidate cand1112, one bump: coop-joinload's Hello start_mode and the join's load pieces, kLoadPieceMagic/kLoadAckMagic; coop-whereis's activity byte in the worlds presence Message; coop-choosing's floor half (back half, ura) in the worlds presence and the back-half unlock table in the snapshot; coop-entrance's Hello walk_in, entrance_walk and walk_gap and the presence's walk_key, walk_role, walk_begun_ms and sent_ms, on its branch at 23 with no bump of its own); 23 (the 11-11 train, cand1111b, one bump: coop-eventslots' parked event slots, fishing set, arena and texture blocks in the state set, the texture table of 128 blocks, game.coop_fishing_shared in the Hello (story_gather bit 2); coop-pt1111e's kCmdDialogPick (16), kExtraQuests/kExtraDialog (0xC000000A/B) and the quests checksum part; coop-pt1111o's game.coop_menus / coop_menu_panel in the Hello (bits 3, 4); the 11-11 playtest fixes' state-set changes); 22 (train 57 with the repin to Chronicle 9418a550 / Kazarov b619d1aa: the state set's whole layout, coop-quittitle's kCmdQuit (15), the floor autosave's mark and the system message buffer in the state set); 21 (train 56 = cand56 + cand57, one bump): coop-small2's kCmdStorageClaim/kCmdStorageMove (12/13, the party storage by option C), its storage asks apart and a script menu's record in the state set (kExtraScriptMenu), coop-opt7's kCmdPresent and the player table's g_present in the state set, coop-opt8's talk slots (each player's talk carried in the snapshot extras, kExtraTalks; his talk together no longer the town's mode), coop-release2's region in the Hello (NTSC 1.02 only), coop-scriptmenus' kCmdScriptMenuEnd (a script's menu run on its opener's copy alone), coop-pt1108's kCmdPartsPoke, the Georama menu's writes outside the party status, and the playtest fixes' state-set changes (scene music, the texture table of 80 blocks); 20 (train 55, one bump): coop-catchuprace's kCmdSlot (a late slot record by a lockstep command) and coop-joinlobby's pick in the catch-up's last blob and coop-stage4's town menus local (a town menu runs on its owner's copy alone; its close is a command); 19: coop-fx51a's floor build random and key and floor entrance in the state set (b2; the build check refuses mixed builds anyway, the bump keeps the convention 15 set); 18: coop-worldkeys key ring in the bag bytes and the slot sync (17 is joinlate's); 16: coop-buildid's version (joinlate takes 17; whichever lands second bumps again); 15: coop-kocatchup's weapon element spark slot in the state set (train 50); 14: coop-stayerhello's link_source; 13: coop-localmenu's commands (an Entry's NetCommand), train 48 with coop-lobby's 12; 12: coop-lobby's player_id; 11: coop-session's first_link; 5: coop-revive's revive_powder; 6: coop-localfix2's stayer; 7: coop-georamaload's builds, stand; 8: coop-apartstats' gains, characters now; 9: coop-pagediff's page list, need and pages; 10: coop-floorgate's story_gather

// FNV-1a over the executable's bytes, read once: two copies of one build agree, any rebuild differs.
std::uint64_t BuildHash() {
    static std::uint64_t hash = 0;

    if (hash != 0) {
        return hash;
    }

    hash = 14695981039346656037ull;
    std::ifstream     file(PathsExecutable(), std::ios::binary);
    std::vector<char> buffer(1 << 20);

    while (file) {
        file.read(buffer.data(), (std::streamsize) buffer.size());

        for (std::streamsize i = 0; i < file.gcount(); i++) {
            hash = (hash ^ (unsigned char) buffer[i]) * 1099511628211ull;
        }
    }

    return hash;
}

// coop-buildid: the exe's SHA-256, for the startup line only (FIPS 180-4). The link compares the version, else BuildHash.
std::string ExeSha256() {
    static const std::uint32_t k[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01,
        0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
        0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
        0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08,
        0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
        0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    std::uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    auto rotr = [](std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); };
    auto block = [&](const unsigned char *p) {
        std::uint32_t w[64];
        for (int i = 0; i < 16; i++) {
            w[i] = (std::uint32_t) p[4 * i] << 24 | (std::uint32_t) p[4 * i + 1] << 16 | (std::uint32_t) p[4 * i + 2] << 8 | p[4 * i + 3];
        }
        for (int i = 16; i < 64; i++) {
            std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; i++) {
            std::uint32_t t1 = hh + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
            std::uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    };

    std::ifstream       file(PathsExecutable(), std::ios::binary);
    std::vector<char>   buffer(1 << 20);
    unsigned char       tail[128];
    std::size_t         held = 0;
    std::uint64_t       length = 0;

    while (file) {
        file.read(buffer.data(), (std::streamsize) buffer.size());
        std::size_t got = (std::size_t) file.gcount(), at = 0;
        length += got;
        if (held) {
            std::size_t take = std::min<std::size_t>(64 - held, got);
            std::memcpy(tail + held, buffer.data(), take);
            held += take; at = take;
            if (held == 64) { block(tail); held = 0; }
        }
        for (; at + 64 <= got; at += 64) {
            block((const unsigned char *) buffer.data() + at);
        }
        std::memcpy(tail + held, buffer.data() + at, got - at);
        held += got - at;
    }
    if (length == 0) {
        return "unreadable";
    }
    tail[held++] = 0x80;
    std::size_t end = held <= 56 ? 64 : 128;
    std::memset(tail + held, 0, end - held);
    for (int i = 0; i < 8; i++) {
        tail[end - 1 - i] = (unsigned char) ((length * 8) >> (8 * i));
    }
    block(tail);
    if (end == 128) {
        block(tail + 64);
    }

    char out[65];
    for (int i = 0; i < 8; i++) {
        std::snprintf(out + 8 * i, 9, "%08x", h[i]);
    }
    return out;
}

// coop-buildid, release4 (Jep 11-10: a README commit must not change the exe): this exe's version, from build_version.hpp:
// the id is FNV-1a 64 of "content <sha256>\nchronicle <commit>\nkazarov <commit>\n" (plus "flavor <f>\n" for a dev
// build), or 0 for an exe with no version (built without build\BuildVersion.ps1). The content hash covers exactly the
// files that compose the build (src, recipes, build, chronicle-pin.txt), never a commit. Test hooks change only what this
// copy says, never the header: DC_NETPLAY_MUTATE=version_other (twin) says another content (its first digit turned),
// =version_dirty (twin) says it has no version.
struct BuildVersion {
    std::string   content, chronicle, kazarov, flavor;
    std::uint64_t id = 0;
    std::uint32_t content8 = 0, chronicle8 = 0;
};

std::uint32_t Hex8(const std::string &s) {
    return s.size() >= 8 ? (std::uint32_t) std::strtoul(s.substr(0, 8).c_str(), nullptr, 16) : 0;
}

const BuildVersion &Version() {
    static const BuildVersion version = [] {
        BuildVersion v;
        v.content = DCO_VERSION_CONTENT;
        v.chronicle = DCO_VERSION_CHRONICLE;
        v.kazarov = DCO_VERSION_KAZAROV;
        v.flavor = DCO_VERSION_FLAVOR;
        if (NetplayMutated("version_other") && !v.content.empty()) {
            v.content[0] = v.content[0] == '0' ? '1' : '0';
        }
        v.content8 = Hex8(v.content);
        v.chronicle8 = Hex8(v.chronicle);
        if (v.content.size() != 64 || NetplayMutated("version_dirty")) {
            return v;
        }
        std::string canon = "content " + v.content + "\nchronicle " + v.chronicle + "\nkazarov " + v.kazarov + "\n";
        if (!v.flavor.empty()) {
            canon += "flavor " + v.flavor + "\n";
        }
        v.id = 14695981039346656037ull;
        for (unsigned char c : canon) {
            v.id = (v.id ^ c) * 1099511628211ull;
        }
        return v;
    }();
    return version;
}

std::string       g_exe_sha;              // coop-buildid: the exe's SHA-256, from its thread
std::atomic<bool> g_exe_sha_ready{false}; // ... once written
std::thread       g_exe_sha_thread;        // coop-cand58: joined as the game ends (NetplayHelloStop), never left running

void Differs(std::string &out, const char *name, bool differs) {
    if (differs) {
        out += out.empty() ? "" : ", ";
        out += name;
    }
}

} // namespace

namespace dc::game {
unsigned int PortRandState();
extern s32   mode;
extern s32   MapNo;
} // namespace dc::game

namespace {

// coop-drawwrite (tests): DC_COOP_TEST_DETAIL=<distance> (env_a / env_b): this copy's own video.detail_distance, as its config.json
// would set it (a test writes only the game section of a copy's file), taken once, before this copy's first Hello is made.
void TestOwnDetail() {
    static bool done = false;

    if (done) {
        return;
    }
    done = true;
    const int distance = TestEnvInt("DC_COOP_TEST_DETAIL", -1);

    if (distance < 0) {
        return;
    }
    dc::platform::Config config = dc::platform::ConfigGet();

    config.detail_distance = (float) distance;
    ConfigChange(config);
    std::fprintf(stderr, "netplay: test: this copy's own detail distance %d\n", distance);
}

// The settings part of a Hello (tick_rate to the end), as both NetplayHelloMake and the checksum's
// NetplaySettingsHash take it.
void PutSettings(NetplayHello &hello) {
    TestOwnDetail();
    const dc::platform::Config &config = dc::platform::ConfigGet();

    hello.tick_rate = config.tick_rate;
    hello.language = LanguageCode;
    hello.debug_mode = config.debug_mode;
    hello.coop = config.coop;
    // (coopoptions: hello.coop_independent_cameras and hello.own_bags stay 0, retired: every copy has its own camera and its own bag)
    // coop-drawwrite (Jep's 5.3): video.detail_distance is each player's own (it decides only what that player's screen draws:
    // dun/gameloop.cpp DrawMonstorDetail, editloop3.cpp KeepNearVillagers), so it is no shared setting and hello.detail_distance stays 0
    // (DC_NETPLAY_MUTATE=detail_shared, the twin: sent and the host's taken, as before).
    hello.detail_distance = NetplayMutated("detail_shared") ? config.detail_distance : 0.0f;
    // coop-wave2 (Jep's 5.4): game.message_speed is each player's own (their own talks run at it; shared windows at player 1's, netplay.cpp
    // CoopSharedMessagesFast from the own-options row), so it is no shared setting and hello.fast_messages stays 0.
    hello.fast_time = config.options.fast_time;
    // coop-settings (Jep's option C, 11-17): Clock, Dungeon Map, Enemy Damage and Party Damage are each player's own (drawn on that
    // player's screen alone: the town clock, the mini-map, the damage numbers), so they are no part of the shared settings and their
    // Hello fields stay 0 (hello.clock, map, enemy_damage, player_damage); each copy keeps its own (netplay.cpp KeepOwnOptions). So too
    // Element Quick Select, Swap Keeps Lock-On and QTE Always Win: each player's own, every copy knowing each (netplay.cpp kCmdOwnOptions).
    hello.revive_powder = coop::RevivePowderOn(); // coop-revive
    // (bits 0 and 1 were game.coop_story_gather and game.coop_gate_picker, retired by coop-gatherpicker: always on)
    hello.story_gather = config.coop_fishing_shared ? 0 : 4; // (coop-eventslots: bit 2)
    // (bits 3 and 4 were game.coop_menus and game.coop_menu_panel, retired by coopoptions: the co-op menus are always on)
    hello.story_gather |= config.coop_swap_same_zone ? 32 : 0; // trade3: game.coop_swap_same_zone in bit 5
}

} // namespace

void NetplayHelloSettings(NetplayHello &hello) {
    PutSettings(hello);
}

bool NetplayHelloSettingsDiffer(const NetplayHello &a, const NetplayHello &b) {
    const std::size_t from = offsetof(NetplayHello, tick_rate);

    return std::memcmp((const unsigned char *) &a + from, (const unsigned char *) &b + from, sizeof(NetplayHello) - from) != 0;
}

// Once a second, from the checksum: the settings alone. (It used to make a whole Hello, whose state
// digest read every save slot file from disk and hashed SaveData, then left the digest out.)
std::uint32_t NetplaySettingsHash() {
    NetplayHello  hello{};
    std::uint32_t hash = 2166136261u;

    PutSettings(hello);
    const auto   *from = (const unsigned char *) &hello.tick_rate;
    const auto   *to = (const unsigned char *) &hello + sizeof hello;

    for (const unsigned char *at = from; at < to; at++) {
        hash = (hash ^ *at) * 16777619u;
    }

    return hash;
}

// coop-indoorfps test seam (DC_QA_SLOT_BULK=N:BYTES): N files of BYTES each written once into this copy's slot folder
// (saves/1/qa-bulk), a stand-in for a long-played save folder (the developer's: 104 files, 1.7 MB), which a test's saved state has not.
static void SlotBulkTestFiles() {
    static bool done = false;
    int         count = 0;
    int         bytes = 0;

    if (done) {
        return;
    }
    done = true;
    if (std::sscanf(TestEnvOr("DC_QA_SLOT_BULK", ""), "%d:%d", &count, &bytes) != 2 || count <= 0 || bytes <= 0) {
        return;
    }
    std::error_code             error;
    const std::filesystem::path folder = dc::save::SaveSlotsRoot() / "1" / "qa-bulk";
    const std::vector<char>     filler((std::size_t) bytes, 'x');

    std::filesystem::create_directories(folder, error);
    for (int n = 0; n < count; n++) {
        char name[32];
        std::snprintf(name, sizeof name, "bulk-%03d.bin", n);
        std::ofstream(folder / name, std::ios::binary).write(filler.data(), (std::streamsize) filler.size());
    }
    std::fprintf(stderr, "netplay: test: %d slot files of %d bytes written in %s (DC_QA_SLOT_BULK)\n", count, bytes, folder.string().c_str());
}

std::uint64_t NetplayStateDigest() {
    std::uint64_t hash = 14695981039346656037ull;
    auto          add = [&hash](const void *data, std::size_t size) {
        for (std::size_t i = 0; i < size; i++) {
            hash = (hash ^ ((const unsigned char *) data)[i]) * 1099511628211ull;
        }
    };
    unsigned int rand = PortRandState();

    add(SaveData, sizeof(CSaveData));
    add(&rand, sizeof rand);
    add(&mode, sizeof mode);
    add(&MapNo, sizeof MapNo);

    std::uint64_t slots = NetplaySlotsHash();

    add(&slots, sizeof slots);
    return hash;
}

NetplayHello NetplayHelloMake(int player, int delay, std::uint32_t session, bool fresh) {
    NetplayHello hello{};

    hello.magic = kNetplayHelloMagic;
    hello.player = (std::uint32_t) player;
    // DC_NETPLAY_MUTATE=protocol_unbumped (x-protocol-refuse~unbumped): train 50's bump undone, the version a protocol-14 build says.
    hello.protocol = NetplayMutated("protocol_unbumped") ? 14 : kProtocol;
    std::snprintf(hello.release, sizeof hello.release, "%s", NetplayHelloRelease()); // version-scheme
    // coop-buildid: a copy with a version proves its build by it and reads no exe bytes (the FNV over the 106 MB dev exe took
    // about 100 ms, measured 2026-11-03); one without (built without build\BuildVersion.ps1) sends the bytes' hash.
    hello.version = Version().id;
    hello.build = hello.version != 0 ? 0 : BuildHash();
    hello.version_content = Version().content8;
    hello.version_chronicle = Version().chronicle8;
    hello.delay = delay;
    hello.session = session;
    hello.fresh = fresh;
    // coop-indoorfps (the developer's v0.2-rc2 playtest, 11-18: a hitch about once a second indoors, never outdoors): only a fresh Hello's
    // digest is ever compared (Begin's "alike" needs both fresh), but every Hello made one, and the digest reads every save slot
    // file (NetplaySlotsHash: 104 files, 1.7 MB in the developer's saves, 28-31 ms warm and 66 ms cold on PC-02, istreambuf_iterator byte
    // by byte) on the game thread. Apart (one player in a house, the other out) a Hello goes every 500 ms: a tick of 30-140 ms
    // twice a second (his log: Listen 1.9-2.5 ms a tick, worst ticks 44-144 ms in netplay, 120-260 ticks a minute made up).
    // DC_NETPLAY_MUTATE=hello_digest_always (the twin): every Hello makes it, as before.
    SlotBulkTestFiles();
    hello.digest = fresh || NetplayMutated("hello_digest_always") ? NetplayStateDigest() : 0;
    // coop-steamsplit (mp-nat -Split, red on main f87d6b82): only a copy that can send its state by a catch-up says it
    // stayed. Her copy with catch-up off (a plain DC_NETPLAY=3 copy) said so, both copies took her as the merge's source,
    // and with no place checkpoint to send her copy fell to the resync, which only the host sends: it "took" the state its
    // first link had left in its resync buffer (the town as it was then), his copy waited for a state that never came, and
    // the pair was out of sync from link tick 60 for good. Without it the host sends, a resync both take.
    // DC_NETPLAY_MUTATE=stayer_without_catchup (twin): says it stayed whatever its catch-up, as before.
    hello.stayer = CoopWorldsSendsMerge() && (NetplayCatchupSetting() || NetplayMutated("stayer_without_catchup")) ? 1 : 0;
    hello.builds = player == 2 && CoopWorldsPreJoinBuilds() ? 1 : 0;
    hello.has_stand = player == 1 && CoopWorldsOwnStand(hello.stand_pos, &hello.stand_yaw, &hello.stand_map) ? 1 : 0;
    hello.player_id = NetplayPlayerId(); // coop-lobby
    hello.region = NetplayHelloRegion(); // release2
    hello.walk_in = (std::uint8_t) CoopWorldsWalkInRole(); // coop-entrwin
    hello.entrance_walk = 0;
    hello.walk_gap = (std::uint8_t) std::clamp(CoopWorldsWalkInGap() < 0 ? 255 : CoopWorldsWalkInGap(), 0, 255); // coop-entrance2 rule 3
    hello.arrival_cut = CoopWorldsArrivalCut() ? 1 : 0; // coop-b4arrive
    hello.cut_walk = 0;
    std::snprintf(hello.profile, sizeof hello.profile, "%s", coop::lobby::OwnName().c_str()); // coop-joinname
    PutSettings(hello);
    return hello;
}

bool NetplayHelloSameBuild(const NetplayHello &mine, const NetplayHello &theirs) {
    if (mine.version != 0 && theirs.version != 0) {
        return mine.version == theirs.version;
    }
    return mine.build == theirs.build;
}

// release2: read once (the data never changes under a running game). Test hook DC_NETPLAY_TEST_REGION=pal (n-region-refuse):
// this copy says PAL whatever its data, as a PAL player's copy would.
std::uint8_t NetplayHelloRegion() {
    static const std::uint8_t region = [] {
        if (TestEnvIs("DC_NETPLAY_TEST_REGION", "pal")) {
            return (std::uint8_t) 2;
        }
        std::ifstream in(PathsDataRoot() / "languages.json", std::ios::binary);
        std::string   text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        return (std::uint8_t) (text.find("\"NTSC 1.02\"") != std::string::npos ? 1 : text.find("PAL") != std::string::npos ? 2 : 0);
    }();
    return region;
}

// DC_NETPLAY_MUTATE=region_ignored (n-region-refuse's twin): the region not checked, as before release2.
bool NetplayHelloRegionsLink(const NetplayHello &mine, const NetplayHello &theirs) {
    return (mine.region == 1 && theirs.region == 1) || NetplayMutated("region_ignored");
}

void NetplayHelloLogVersion() {
    static bool said = false;

    if (said) {
        return;
    }
    said = true;
    const BuildVersion &v = Version();
    char                id[40];

    if (v.id != 0) {
        std::snprintf(id, sizeof id, "id %016llx", (unsigned long long) v.id);
    } else {
        std::snprintf(id, sizeof id, "no id (built without build\\BuildVersion.ps1)");
    }
    // (the content hash's first 16 digits + the Chronicle / Kazarov pins; a dev build says so)
    std::fprintf(stderr, "netplay: version %s\n", NetplayHelloRelease()); // version-scheme: the one number shown
    std::fprintf(stderr, "netplay: build version %.16s+%.8s/%.8s%s %s\n", v.content.empty() ? "none" : v.content.c_str(), v.chronicle.c_str(),
                 v.kazarov.c_str(), v.flavor.empty() ? "" : "+dev", id);
    // The exe's SHA-256 for the log alone, off the startup path (about 280 ms over the 106 MB dev exe, 17 ms over the shipped
    // one, measured 2026-11-03): hashed on its own thread, said by the main thread once ready (NetplayHelloLogSha). The
    // thread itself never writes: with a test's one log stream its line came out cut into the main thread's lines.
    if (!g_exe_sha_thread.joinable()) {
        g_exe_sha_thread = std::thread([] {
            g_exe_sha = ExeSha256();
            g_exe_sha_ready.store(true, std::memory_order_release);
        });
    }
}

// coop-cand58: the hasher joined (it writes g_exe_sha, a string the exit's teardown destroys). True when it ran.
bool NetplayHelloStop() {
    if (!g_exe_sha_thread.joinable()) {
        return false;
    }
    g_exe_sha_thread.join();
    return true;
}

void NetplayHelloLogSha() {
    static bool said = false;

    if (!said && g_exe_sha_ready.load(std::memory_order_acquire)) {
        said = true;
        std::fprintf(stderr, "netplay: exe SHA-256 %s\n", g_exe_sha.c_str());
    }
}

std::uint32_t NetplayHelloProtocol() { return kProtocol; }

// version-scheme: this copy's version as the player sees it ("0.1.1.5"; "unknown" in a tree built without build\version.txt).
// DC_NETPLAY_MUTATE=release_older (x-version-refuse): its last number one lower, as the build before says (0.1.1.4).
const char *NetplayHelloRelease() {
    static const std::string release = [] {
        std::string r = DCO_VERSION_RELEASE;
        if (r.empty()) {
            return std::string("unknown");
        }
        if (NetplayMutated("release_older")) {
            const std::size_t dot = r.rfind('.');
            const int         last = std::atoi(r.c_str() + dot + 1);
            r = r.substr(0, dot + 1) + std::to_string(last > 0 ? last - 1 : 0);
        }
        return r;
    }();
    return release.c_str();
}

namespace {

// A copy from before 0.1.1.5 says its protocol, not its version: the releases each protocol shipped in (version-scheme: 0.1.1.N
// after v0.1.1 = protocol 23; cand1112 0.1.1.1 = 24, rc2 0.1.1.2 = 25, rc3 0.1.1.3 = 26, rc4 0.1.1.4 = 27).
std::string OldVersionName(std::uint32_t protocol) {
    if (protocol >= 23 && protocol <= 27) {
        return protocol == 23 ? "0.1.1" : "0.1.1." + std::to_string(protocol - 23);
    }
    return protocol > kProtocol ? "a newer version" : "an older version";
}

// "Host is on 0.1.1.5, you're on 0.1.1.4. Update to play together." from this copy's side (player 1 is the host).
std::string VersionRefusal(const NetplayHello &theirs, const std::string &their_version) {
    const std::string who = theirs.player == 1 ? "Host" : "Player " + std::to_string(theirs.player);
    return who + " is on " + their_version + ", you're on " + NetplayHelloRelease() + ". Update to play together.";
}

std::string ReleaseOf(const NetplayHello &hello) {
    return std::string(hello.release, strnlen(hello.release, sizeof hello.release));
}

} // namespace

bool NetplayHelloSameRelease(const NetplayHello &mine, const NetplayHello &theirs) {
    // (a Hello from before 0.1.1.5 has no release field: its protocol differs, said as such; release_ignored: the twin's, as before)
    return theirs.protocol != kProtocol || ReleaseOf(theirs) == ReleaseOf(mine) || NetplayMutated("release_ignored");
}

std::string NetplayHelloCompare(const NetplayHello &mine, const NetplayHello &theirs) {
    // version-scheme: the version first, in the player's words, naming both; the build check stays underneath.
    if (theirs.protocol != mine.protocol) {
        return VersionRefusal(theirs, OldVersionName(theirs.protocol));
    }
    if (!NetplayHelloSameRelease(mine, theirs)) {
        return VersionRefusal(theirs, ReleaseOf(theirs));
    }

    if (!NetplayHelloSameBuild(mine, theirs) && !NetplayMutated("build_ignored")) {
        if (mine.version != 0 && theirs.version != 0) {
            char text[160];
            std::snprintf(text, sizeof text, "the other copy is a different version of the mod or the decompile (content %08x/%08x, Chronicle %08x/%08x)",
                          mine.version_content, theirs.version_content, mine.version_chronicle, theirs.version_chronicle);
            return text;
        }
        return "the other copy is a different build of the game (copy the same darkcloud.exe to both PCs)";
    }

    // release2: NTSC 1.02 only, said from whichever side is not (the lobby shows it as the link's status).
    if (!NetplayHelloRegionsLink(mine, theirs)) {
        const char *kRegions[] = {"not from NTSC 1.02", "NTSC 1.02", "from a PAL disc"};
        const bool  mine_off = mine.region != 1;
        char        text[150];
        std::snprintf(text, sizeof text, "%s game data is %s; only NTSC (US 1.02) discs can play for now", mine_off ? "this copy's" : "the other player's",
                      kRegions[std::min<int>(mine_off ? mine.region : theirs.region, 2)]);
        return text;
    }

    std::string out;

    Differs(out, "game.tick_rate", theirs.tick_rate != mine.tick_rate);
    Differs(out, "the language", theirs.language != mine.language && !NetplayMutated("lang_ignored"));
    Differs(out, "game.debug_mode", theirs.debug_mode != mine.debug_mode);
    Differs(out, "game.coop", theirs.coop != mine.coop);
    Differs(out, "game.time_speed", theirs.fast_time != mine.fast_time);
    Differs(out, "DC_COOP_REVIVE_POWDER", theirs.revive_powder != mine.revive_powder); // coop-revive
    Differs(out, "game.coop_fishing_shared", (theirs.story_gather & 4) != (mine.story_gather & 4)); // coop-eventslots
    Differs(out, "game.coop_swap_same_zone", (theirs.story_gather & 32) != (mine.story_gather & 32)); // trade3

    return out.empty() ? out : "settings differ: " + out;
}

std::string NetplayHelloAdoptNote(const NetplayHello &mine, const NetplayHello &host) {
    std::string out;
    const auto  say = [&](const char *name, bool theirs, bool ours) {
        if (theirs == ours) {
            return;
        }
        out += std::string(out.empty() ? "" : ", ") + name + (theirs ? " on" : " off") + " (yours: " + (ours ? "on" : "off") + ")";
    };

    say("game.coop_fishing_shared", (host.story_gather & 4) == 0, (mine.story_gather & 4) == 0);
    say("game.coop_swap_same_zone", (host.story_gather & 32) != 0, (mine.story_gather & 32) != 0); // trade3
    return out;
}

bool NetplayHelloSettingsOnly(const NetplayHello &mine, const NetplayHello &theirs) {
    return theirs.protocol == mine.protocol && NetplayHelloSameRelease(mine, theirs) && (NetplayHelloSameBuild(mine, theirs) || NetplayMutated("build_ignored")) &&
           NetplayHelloRegionsLink(mine, theirs) && // release2: NTSC 1.02 on both
           (theirs.language == mine.language || NetplayMutated("lang_ignored"));
}

void NetplayHelloAdoptedKeys(dc::platform::Config &to, const dc::platform::Config &from) {
    // (the keys NetplayHelloAdopt sets: one list kept beside it)
    to.tick_rate = from.tick_rate;
    to.debug_mode = from.debug_mode;
    to.coop = from.coop;
    to.options.fast_time = from.options.fast_time;
    to.coop_fishing_shared = from.coop_fishing_shared;
    to.coop_swap_same_zone = from.coop_swap_same_zone; // trade3
}

// coop-soakjoin (soak seed 1545): the shared settings one key at a time, as a change made while linked rides the lockstep
// (netplay.cpp kCmdSetting): the keys NetplayHelloAdoptedKeys copies, each as 32 bits (a double or float as a float's bits).
namespace {
std::uint32_t FloatBits(float value) {
    std::uint32_t bits = 0;

    std::memcpy(&bits, &value, sizeof bits);
    return bits;
}
float BitsFloat(std::uint32_t bits) {
    float value = 0.0f;

    std::memcpy(&value, &bits, sizeof value);
    return value;
}
struct SharedKey {
    const char *name;
    std::uint32_t (*get)(const dc::platform::Config &config);
    void (*set)(dc::platform::Config &config, std::uint32_t value);
};
using C = dc::platform::Config;
const SharedKey kSharedKeys[] = {
    {"game.tick_rate", [](const C &c) { return FloatBits((float) c.tick_rate); }, [](C &c, std::uint32_t v) { c.tick_rate = BitsFloat(v); }},
    {"game.debug_mode", [](const C &c) { return (std::uint32_t) c.debug_mode; }, [](C &c, std::uint32_t v) { c.debug_mode = v != 0; }},
    {"game.coop", [](const C &c) { return (std::uint32_t) c.coop; }, [](C &c, std::uint32_t v) { c.coop = v != 0; }},
    {"game.time_speed", [](const C &c) { return (std::uint32_t) c.options.fast_time; }, [](C &c, std::uint32_t v) { c.options.fast_time = v != 0; }},
    {"game.coop_fishing_shared", [](const C &c) { return (std::uint32_t) c.coop_fishing_shared; }, [](C &c, std::uint32_t v) { c.coop_fishing_shared = v != 0; }},
    {"game.coop_swap_same_zone", [](const C &c) { return (std::uint32_t) c.coop_swap_same_zone; }, [](C &c, std::uint32_t v) { c.coop_swap_same_zone = v != 0; }},
};
} // namespace

int NetplaySharedKeyCount() {
    return (int) std::size(kSharedKeys);
}
const char *NetplaySharedKeyName(int key) {
    return key >= 0 && key < NetplaySharedKeyCount() ? kSharedKeys[key].name : "?";
}
std::uint32_t NetplaySharedKeyGet(const dc::platform::Config &config, int key) {
    return key >= 0 && key < NetplaySharedKeyCount() ? kSharedKeys[key].get(config) : 0u;
}
void NetplaySharedKeySet(dc::platform::Config &config, int key, std::uint32_t value) {
    if (key >= 0 && key < NetplaySharedKeyCount()) {
        kSharedKeys[key].set(config, value);
    }
}

void NetplayHelloAdopt(const NetplayHello &host) {
    TestOwnDetail(); // coop-drawwrite (tests): this copy's own file value first, so a taken host's value (the twin) stands over it
    if (NetplayMutated("no_adopt")) {
        return;
    }

    // coop-pt1111p (Jep's rule: the joiner plays by the host's settings for the run only, never in its config.json): before, the
    // ConfigChange below saved them into the joiner's file (ConfigSave writes the running settings when no session keeps keys
    // apart), so after a joined run its own settings were the host's. The adopted keys are a session's now, set before the change
    // so ConfigSave keeps the file's values of them. DC_NETPLAY_MUTATE=adopt_saved (twin): no session, the file written as before.
    NetplayAdoptSession(true);

    dc::platform::Config config = dc::platform::ConfigGet();

    coop::DransCheatNoteOwnDebug(config.debug_mode); // coop-debuggate: before the host's value replaces it (the cheat's twin)
    config.tick_rate = host.tick_rate;
    config.debug_mode = host.debug_mode;
    config.coop = host.coop;
    if (NetplayMutated("detail_shared")) { // coop-drawwrite (twin, with DC_COOP_MUTATE=detail_step_own): the host's, as before
        config.detail_distance = host.detail_distance;
    }
    config.options.fast_time = host.fast_time;
    config.coop_fishing_shared = (host.story_gather & 4) == 0; // coop-eventslots
    config.coop_swap_same_zone = (host.story_gather & 32) != 0; // trade3
    NetplaySharedSettingsTaking(true); // coop-soakjoin: the host's, no change of this copy's player's
    ConfigChange(config);
    NetplaySharedSettingsTaking(false);
    coop::RevivePowderAdopt(host.revive_powder != 0); // coop-revive: this run only, as the launcher's switch is
}

// coop-joinfix test hook (DC_NETPLAY_TEST_SETTING_FLIP=T, net-settings-change): at link tick T this copy turns its element
// quick select over in its options, as Jep did mid-link (item 2), and logs it.
void NetplayTestSettingFlip(std::int64_t tick) {
    static const int at = TestEnvInt("DC_NETPLAY_TEST_SETTING_FLIP", -1);

    if (at < 0 || tick != at) {
        return;
    }
    dc::platform::Config config = dc::platform::ConfigGet();

    config.element_quick_select = !config.element_quick_select;
    ConfigChange(config);
    std::fprintf(stderr, "netplay: test: element quick select turned %s at link tick %lld (settings hash %08x)\n",
                 config.element_quick_select ? "on" : "off", (long long) tick, NetplaySettingsHash());
}
