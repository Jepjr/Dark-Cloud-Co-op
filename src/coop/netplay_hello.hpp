#pragma once

#include "coop/game_ns.hpp"

#include <cstdint>
#include <string>

// The co-op link's handshake (port/src/coop/netplay.cpp): before the first tick each copy sends the
// other a Hello and waits for the other's. Lockstep only holds when both copies run the same build
// with the same settings that steer the game, so the two must agree on the protocol, the build (the
// version it was made from, else a hash of the exe's bytes) and those settings; the joiner (player 2) takes the host's input delay. On a
// mismatch neither links, and both say what differs.

constexpr std::uint32_t kNetplayHelloMagic = 0x44434849; // "IHCD"

#pragma pack(push, 1)
struct NetplayHello {
    std::uint32_t magic;
    std::uint32_t player;
    std::uint32_t protocol; // version-scheme: frozen at 28 from 0.1.1.5 (the counter is retired; release says the version)
    // version-scheme (Jep 11-14): this copy's version, "0.1.1.5" (build\version.txt), NUL-padded. Right after protocol, so a
    // Hello of another layout still carries it as far as the size check reads (netplay.cpp's helloHead).
    char          release[16];
    std::uint64_t build;
    std::int32_t  delay;
    // This copy's session (new each time it links), whether it is at its first ready tick (not playing
    // alone since), and a digest of its state there (random generator, SaveData, where it is): when
    // both are fresh with one digest, they start alike; else the host's state is sent first.
    std::uint32_t session;
    std::uint8_t  fresh;
    std::uint64_t digest;
    // coop-localfix2 ("whoever is already there wins", Jep 2026-10-26): this copy stayed in the zone at the last split
    // (coop_worlds' stayer) and can send its state for the merge's catch-up: the merge's state goes from it.
    std::uint8_t  stayer;
    // coop-georamaload: player 2's copy waiting to join builds in the game's Georama (the first link waits until she
    // closes it); player 1's Toan where he walks in a town's outside now (has_stand), for a join by her state.
    std::uint8_t  builds;
    std::uint8_t  has_stand;
    std::int32_t  stand_map;
    float         stand_pos[3];
    float         stand_yaw;
    // coop-session: this copy has never linked yet (a join from its title, a host's first): a peer the other copy still
    // holds as apart or dropped that says so has come back new (netplay_session.hpp's Rejoining).
    std::uint8_t  first_link;
    // coop-stayerhello (protocol 14): set in the Hello the host links with (and sends again with each tick until the joiner's
    // come), the merge's source as the host decided it: 1 the host's state, 2 player 2 stayed (hers), 3 the host stayed
    // (his, player 2 arriving). 0 in every other Hello. The joiner links only on it and goes by it, never deciding alone.
    std::uint8_t  link_source;
    // coop-buildid (protocol 16), release4 (content, not commit): the version this exe was made from (build\BuildVersion.ps1:
    // the content hash of the files that compose the build and the decompile's pins), as a hash, or 0 for an exe built
    // without build\BuildVersion.ps1, which has no version to prove. Two copies with a version link on it alone, whatever
    // their exe bytes; else the bytes (build) must agree, and a copy with a version sends build 0 (it reads no exe bytes),
    // so it never links with one without. version_content and version_chronicle: the first 8 hex digits of the content
    // hash and of the Chronicle commit, for the refusal's text.
    std::uint64_t version;
    std::uint32_t version_content;
    std::uint32_t version_chronicle;
    // The settings both copies must share (config.hpp), in a fixed layout.
    double        tick_rate;
    std::int32_t  language;
    std::uint8_t  debug_mode;
    std::uint8_t  qte_always_win;
    std::uint8_t  element_quick_select;
    std::uint8_t  swap_keeps_lock_on;
    std::uint8_t  coop;
    std::uint8_t  coop_independent_cameras; // (retired by coopoptions: always 0, every copy's camera its own)
    float         detail_distance;
    std::uint8_t  fast_messages;
    std::uint8_t  clock;
    std::uint8_t  fast_time;
    std::int32_t  map;
    std::uint8_t  enemy_damage;
    std::uint8_t  player_damage;
    std::uint8_t  own_bags; // (retired by coopoptions: always 0, every player's bag their own)
    std::uint8_t  revive_powder; // coop-revive: the Revive Powder rule as it is in effect (DC_COOP_REVIVE_POWDER)
    std::uint8_t  story_gather;  // bits 0 and 1 retired (coop-gatherpicker; were story gather, gate picker); coop-eventslots: not game.coop_fishing_shared (bit 2); bits 3 and 4 retired (coopoptions); trade3: game.coop_swap_same_zone (bit 5)
    // coop-lobby: this copy's lobby identity (game.netplay_player_id), so a rejoin gets its seat back (protocol 12).
    std::uint64_t player_id;
    // release2 (protocol 21, train 56's one bump): the disc release this copy's game data comes from (NetplayHelloRegion).
    // Set by NetplayHelloMake alone, like player_id, so the settings part (NetplayHelloSettings) never differs by it.
    std::uint8_t  region;
    // coop-joinload (protocol 24; Jep 11-11: a joiner always loads in as if it had just loaded the game): a copy joining from its title
    // says 1 (kJoinAsks); the host answers 2 (kJoinLoads: its load pieces follow, the joiner loads the town as a save load does and the
    // pair is apart from there) or links as before (the one exception: a rejoin to the floor it dropped from); the joiner, loaded,
    // says 3 (kJoinLoaded). 0 in every other Hello. Set by netplay.cpp, never by NetplayHelloMake.
    std::uint8_t  start_mode;
    // coop-entrwin: this copy's place in a floor's entrance window apart (CoopWorldsWalkInRole: 1 the lead, 2 a walker, 0 none);
    // and, set in the Hello the host links with alone (as link_source), the player whose own entrance both copies play at the
    // lockstep at its entrance spot (coop::EntranceWalkStart), the host deciding it from both Hellos; 0 none.
    std::uint8_t  walk_in;
    std::uint8_t  entrance_walk;
    // coop-entrance2 rule 3: a walker's arrival after the lead (ticks, CoopWorldsWalkInGap; 255 none), the stagger by arrival order.
    std::uint8_t  walk_gap;
    // coop-b4arrive (Jep's rc3 playtest: "P2 joined P1 on B4 and the floor entrance animation didn't play"): this copy's own arrival
    // on the other's floor is under way and the merge forms in it (a pick join, CoopWorldsArrivalCut), so the other's state will
    // land over it before it ends; and, set in the Hello the host links with alone (as entrance_walk), the arriver whose cut
    // arrival both copies play at the lockstep (coop::ArrivalWalkStart), the host deciding it from the arriver's Hello; 0 none.
    std::uint8_t  arrival_cut;
    std::uint8_t  cut_walk;
    // coop-joinname (Jep 11-16: "when a player joins the game, it shouldn't show their IP anymore since they will always join with an
    // associated profile"): this copy's own profile name as its lobby shows it (coop::lobby::OwnName), NUL-padded, "" none. Set by
    // NetplayHelloMake alone, like player_id. The join and leave notices name the player by it (netplay.cpp's PeerName).
    char          profile[16];
};
#pragma pack(pop)

// This copy's Hello as player.
NetplayHello NetplayHelloMake(int player, int delay, std::uint32_t session, bool fresh);

// A hash of the settings both copies must share (as a Hello carries them), for the lockstep
// checksum: a joiner that did not take the host's settings parts from it there.
std::uint32_t NetplaySettingsHash();

// The digest of this copy's state that a Hello carries.
std::uint64_t NetplayStateDigest();

// coop-buildid: the two copies run one game: one version when both have one, else the same exe bytes.
bool NetplayHelloSameBuild(const NetplayHello &mine, const NetplayHello &theirs);

// release2 (Jep, 2026-11-10: NTSC only until PAL is tested): this copy's disc release, from the "release" dcdata wrote into
// the data's languages.json: 1 NTSC 1.02, 2 PAL, 0 another or none. Two copies link only when both are NTSC 1.02.
std::uint8_t NetplayHelloRegion();
bool         NetplayHelloRegionsLink(const NetplayHello &mine, const NetplayHello &theirs);

// coop-buildid: once at the link's start, this copy's version, its id and its exe's SHA-256 (AGENTS rule 5: a run names the
// build it ran).
void NetplayHelloLogVersion();

// coop-buildid: each tick, the exe's SHA-256 line once its thread has it (once).
void NetplayHelloLogSha();
// coop-cand58: the exe hasher's thread joined as the game ends (NetplayStopThreads); true when there was one.
bool NetplayHelloStop();

// coop-pt1111o: what a joiner is told when it takes the host's co-op settings: those of the host's that differ from this
// copy's, as "game.coop_swap_same_zone on (yours: off)", comma-separated. Empty when none differ.
std::string NetplayHelloAdoptNote(const NetplayHello &mine, const NetplayHello &host);

// Empty when the two copies can link; else what keeps them apart, as the HUD and the log say it.
std::string NetplayHelloCompare(const NetplayHello &mine, const NetplayHello &theirs);
// coop-hellosize51: this build's protocol (a Hello of another size is read against it). Frozen at 28 (version-scheme).
std::uint32_t NetplayHelloProtocol();
// version-scheme: this copy's version ("0.1.1.5"), and whether two Hellos say one version.
const char *NetplayHelloRelease();
bool        NetplayHelloSameRelease(const NetplayHello &mine, const NetplayHello &theirs);

// The way past differing settings: when only settings a running game can change differ (not the
// build, the protocol or the language), the joiner takes the host's for this run only: config.json keeps
// its own (coop-pt1111p: the keys below are a session's, NetplayAdoptSession, ended at Quit Game).
bool NetplayHelloSettingsOnly(const NetplayHello &mine, const NetplayHello &theirs);
void NetplayHelloAdopt(const NetplayHello &host);
// coop-pt1111p: the keys NetplayHelloAdopt sets, copied from one settings set into another (config.json keeps from's).
namespace dc::platform { struct Config; }
void NetplayHelloAdoptedKeys(dc::platform::Config &to, const dc::platform::Config &from);
// coop-soakjoin: the adopted keys one at a time (their config.json names, each value in 32 bits), for netplay.cpp's kCmdSetting.
int           NetplaySharedKeyCount();
const char   *NetplaySharedKeyName(int key);
std::uint32_t NetplaySharedKeyGet(const dc::platform::Config &config, int key);
void          NetplaySharedKeySet(dc::platform::Config &config, int key, std::uint32_t value);
// netplay.cpp: the adopted keys as this run's (on) or not (off); with the title's online session in one.
void NetplayAdoptSession(bool on);
// coop-soakjoin (netplay.cpp): linked, the shared keys change on every copy on one tick (kCmdSetting); a link up (they are
// those running), the session over (its own again), and an adopt's change (Taking: the host's, set as they come).
void NetplaySharedSettingsLinked();
void NetplaySharedSettingsEnded();
void NetplaySharedSettingsTaking(bool on);

// coop-joinfix: only the settings part of this copy's Hello (from tick_rate on), without its state digest (which reads every
// save slot); and whether two Hellos' settings parts differ.
void NetplayHelloSettings(NetplayHello &hello);
bool NetplayHelloSettingsDiffer(const NetplayHello &a, const NetplayHello &b);

// coop-joinfix test hook (DC_NETPLAY_TEST_SETTING_FLIP=T): this copy's element quick select turned over at link tick T.
void NetplayTestSettingFlip(std::int64_t tick);
